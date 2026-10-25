# SPDX-License-Identifier: GPL-2.0-or-later
"""Qt presentation model. Workers publish data; only Qt slots update models."""
from __future__ import annotations

from collections import deque
import math
from pathlib import Path
import queue
import tempfile
import threading

from PySide6.QtCore import (QAbstractListModel, QModelIndex, QObject, Property,
                            Qt, QTimer, Signal, Slot, QUrl)
from PySide6.QtGui import QGuiApplication

from .. import config, validator
from ..runner import RunnerState
from .demo import DemoRunnerPort, SimulatedRunner
from ..languages import LANGUAGES, BY_CODE
from .i18n import translate

MAX_ROWS = 500
MAX_LINE = 2048
MAX_PENDING = 1000
DEFAULTS = {'display_mode': 'windowed', 'window_resolution': 'auto', 'vsync': '1', 'fps_limit': '60',
            'aspect_ratio': 'auto', 'internal_resolution': 'auto',
            'upscaler': 'off', 'preset': '0',
            'fsr1': '0', 'rcas': '0', 'rcas_strength': '50',
            'mouse_enabled': '0', 'mouse_invert_y': '0',
            'gpu_readbacks': '2', 'eager_readback': '0'}
STATE_LABELS = {'IDLE': 'Pronto para simular', 'STARTING': 'Iniciando simulação',
                'RUNNING': 'Simulação em execução', 'STOPPING': 'Encerrando simulação',
                'FINISHED': 'Simulação encerrada', 'FAILED': 'Erro na simulação'}


class LogModel(QAbstractListModel):
    """Bounded, plain-text log rows; insertion/removal runs on the UI thread."""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.lines: list[str] = []

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.lines)

    def roleNames(self):
        return {Qt.UserRole + 1: b'line', Qt.UserRole + 2: b'isError'}

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid() or not 0 <= index.row() < len(self.lines):
            return None
        line = self.lines[index.row()]
        if role in (Qt.DisplayRole, Qt.UserRole + 1):
            return line
        if role == Qt.UserRole + 2:
            return 'error' in line.lower() or 'erro' in line.lower()
        return None

    def append_batch(self, lines):
        if not lines:
            return
        lines = [str(line)[:MAX_LINE] for line in lines][-MAX_ROWS:]
        excess = max(0, len(self.lines) + len(lines) - MAX_ROWS)
        if excess:
            self.beginRemoveRows(QModelIndex(), 0, excess - 1)
            del self.lines[:excess]
            self.endRemoveRows()
        start = len(self.lines)
        self.beginInsertRows(QModelIndex(), start, start + len(lines) - 1)
        self.lines.extend(lines)
        self.endInsertRows()

    def clear(self):
        self.beginResetModel()
        self.lines.clear()
        self.endResetModel()


class Backend(QObject):
    changed = Signal()
    settingsChanged = Signal()
    uiLanguageChanged = Signal()
    _stateArrived = Signal(object, str, str)

    def __init__(self, *, runner_factory=SimulatedRunner,
                 validate_dump=validator.validate_knack2_dump, parent=None):
        super().__init__(parent)
        self._closed = False
        self._language = 'pt-BR'
        self._ui_language = None
        self._state = 'IDLE'
        self._stop_requested = False
        self._session = ''
        self._game_dir = ''
        self._validation = 'Nenhuma pasta selecionada.'
        self._validation_state = 'empty'
        self._installation = 'Knack II'
        self._installation_id = ''
        self._installation_version = ''
        self._validating = False
        self._notification = 'Modo de demonstração. O jogo real não será iniciado.'
        self._notification_error = False
        self._errors: deque[str] = deque(maxlen=40)
        self._pending: deque[tuple[str, str]] = deque(maxlen=MAX_PENDING)
        self._pending_lock = threading.Lock()
        self._dropped = 0
        self._validation_results: queue.Queue = queue.Queue(maxsize=1)
        self._validation_cancel = threading.Event()
        self._validate_dump = validate_dump
        self._validation_thread = None
        self._settings = dict(DEFAULTS)
        self._workspace = tempfile.TemporaryDirectory(prefix='knack2-f3a-')
        self.workspace = Path(self._workspace.name)
        self.supervisor_model = LogModel(self)
        self.guest_model = LogModel(self)
        self._runner: DemoRunnerPort = runner_factory(on_state_change=self._post_state,
                                      on_log_line=lambda line: self.post_log('supervisor', line),
                                      on_guest_log_line=lambda line: self.post_log('guest', line))
        self._stateArrived.connect(self._apply_state, Qt.QueuedConnection)
        self._timer = QTimer(self)
        self._timer.setInterval(50)
        self._timer.timeout.connect(self._drain)
        self._timer.start()

    state = Property(str, lambda self: self._state, notify=changed)
    uiLanguage = Property(str, lambda self: self._ui_language or self._language, notify=uiLanguageChanged)
    languageOptions = Property('QVariantList', lambda self: [dict(entry) for entry in LANGUAGES
                               if entry['code'] in self._language_codes()], notify=changed)
    languageIndex = Property(int, lambda self: next((i for i, entry in enumerate(self.languageOptions)
                             if entry['code'] == self._language), -1), notify=changed)
    isDemo = Property(bool, lambda self: not getattr(self, '_real_mode', False), constant=True)
    editable = Property(bool, lambda self: self._editable(), notify=changed)
    stateLabel = Property(str, lambda self: self._state_label(), notify=changed)
    sessionId = Property(str, lambda self: self._session or self._translate('Nenhuma sessão'), notify=changed)
    gameDir = Property(str, lambda self: self._game_dir, notify=changed)
    validationText = Property(str, lambda self: self._translate(self._validation), notify=changed)
    validationState = Property(str, lambda self: self._validation_state, notify=changed)
    installation = Property(str, lambda self: self._installation, notify=changed)
    installationId = Property(str, lambda self: self._installation_id, notify=changed)
    installationVersion = Property(str, lambda self: self._installation_version, notify=changed)
    validating = Property(bool, lambda self: self._validating, notify=changed)
    notification = Property(str, lambda self: self._translate(self._notification), notify=changed)
    notificationError = Property(bool, lambda self: self._notification_error, notify=changed)
    errorText = Property(str, lambda self: '\n\n'.join(self._translate(e) for e in self._errors) or self._translate('Nenhum erro registrado.'), notify=changed)
    droppedLogs = Property(int, lambda self: self._dropped, notify=changed)
    canPlay = Property(bool, lambda self: self._can_play(), notify=changed)
    canStop = Property(bool, lambda self: self._can_stop(), notify=changed)
    settings = Property('QVariantMap', lambda self: dict(self._settings), notify=settingsChanged)
    windowResolutions = Property('QVariantList', lambda self: list(config.VALID_WINDOW_RESOLUTIONS), constant=True)
    internalResolutions = Property('QVariantList', lambda self: list(config.VALID_INTERNAL_RESOLUTIONS), constant=True)
    supervisorLogs = Property(QObject, lambda self: self.supervisor_model, constant=True)
    guestLogs = Property(QObject, lambda self: self.guest_model, constant=True)

    def _editable(self):
        return not self._validating

    def _state_label(self):
        return self._translate(STATE_LABELS[self._state])

    def _translate(self, text):
        return translate(self.uiLanguage, text)

    @Slot(str)
    def setUiLanguage(self, code):
        if self._closed or code not in ('pt-BR', 'en-US', 'ru-RU'):
            return
        self._ui_language = code
        self.uiLanguageChanged.emit()
        self.changed.emit()

    def _language_codes(self):
        return tuple(BY_CODE)

    @Slot(str)
    def setLanguage(self, code):
        if self._closed or not self.editable:
            return
        if code not in self._language_codes():
            self._notice('Idioma indisponível neste dump.', True)
            return
        self._language = code
        self.uiLanguageChanged.emit()
        self.changed.emit()
        self.settingsChanged.emit()

    def _on_validated(self, result):
        pass

    def _can_play(self):
        return not self._closed and self._state in ('IDLE', 'FINISHED', 'FAILED')

    def _can_stop(self):
        return not self._closed and self._state in ('STARTING', 'RUNNING')

    def _notice(self, text, error=False):
        self._notification = str(text)[:MAX_LINE]
        self._notification_error = error
        if error:
            self._errors.append(self._notification)
        self.changed.emit()

    def _post_state(self, state):
        # Worker callback: emit data only. Never touch QML or change lifecycle here.
        if not self._closed:
            self._stateArrived.emit(state, self._runner.session_info.session_id,
                                    self._runner.error_message)

    @Slot(object, str, str)
    def _apply_state(self, state, session_id, error):
        if self._closed:
            return
        if self._stop_requested and state in (RunnerState.STARTING, RunnerState.RUNNING):
            return
        self._state = state.value
        self._session = session_id
        if error:
            self._notice(error, True)
        else:
            self._notice(STATE_LABELS[self._state] + '. Nenhum processo real foi iniciado.')

    def post_log(self, source, line):
        # Bound memory before crossing into Qt, including an oversized single line.
        if self._closed:
            return
        with self._pending_lock:
            if len(self._pending) == MAX_PENDING:
                self._dropped += 1
            self._pending.append((source, str(line)[:MAX_LINE]))

    @Slot()
    def _drain(self):
        if self._closed:
            return
        batch = {'supervisor': [], 'guest': []}
        with self._pending_lock:
            for _ in range(min(100, len(self._pending))):
                source, text = self._pending.popleft()
                batch['guest' if source == 'guest' else 'supervisor'].append(text)
        self.supervisor_model.append_batch(batch['supervisor'])
        self.guest_model.append_batch(batch['guest'])
        for lines in batch.values():
            for line in lines:
                if 'error' in line.lower() or 'erro' in line.lower():
                    self._errors.append(line)
        try:
            result, error = self._validation_results.get_nowait()
        except queue.Empty:
            pass
        else:
            self._validating = False
            self._validation_state = 'invalid' if error else 'valid'
            self._validation = error or 'Instalação válida: metadados, executáveis e módulos conferidos.'
            if not error:
                self._installation = f"{result['app_title']} · {result['title_id']} · {result['version']}"
                self._installation_id = result['title_id']
                self._installation_version = result['version']
                self._on_validated(result)
            self._notice(error or self._validation, bool(error))
        if any(batch.values()):
            self.changed.emit()

    @Slot(str)
    def selectFolder(self, text):
        if self._closed or self._validating:
            return
        url = QUrl(text)
        self._game_dir = str(Path(url.toLocalFile())) if url.isLocalFile() else text.strip()
        self._validation_state = 'empty'
        self._validation = 'Pasta selecionada. Valide a instalação.' if self._game_dir else 'Nenhuma pasta selecionada.'
        self._installation = 'Knack II'
        self._installation_id = ''
        self._installation_version = ''
        self.changed.emit()

    @Slot()
    def validateFolder(self):
        if self._closed or self._validating:
            return
        if not self._game_dir:
            self._validation_state = 'invalid'
            self._validation = 'Selecione a pasta do seu dump para validar a instalação.'
            self._notice(self._validation, True)
            return
        self._validating = True
        self._validation_state = 'checking'
        self._validation = 'Conferindo instalação em segundo plano…'
        path = self._game_dir
        self.changed.emit()

        def work():
            try:
                result, error = self._validate_dump(path), ''
            except Exception as exc:
                result, error = None, (str(exc) or type(exc).__name__)[:MAX_LINE]
            if not self._validation_cancel.is_set():
                self._validation_results.put_nowait((result, error))

        self._validation_thread = threading.Thread(target=work, name='Knack2-Validation', daemon=True)
        self._validation_thread.start()

    @Slot(str, 'QVariant')
    def setSetting(self, key, value):
        if self._closed:
            return
        try:
            if key not in DEFAULTS:
                raise ValueError('Opção indisponível nesta interface.')
            key, normalized = config.sanitize_and_validate_key_value(key, value)
            self._settings[key] = normalized
        except ValueError as error:
            self._notice(str(error), True)
        self.settingsChanged.emit()

    @Slot(float)
    def setRcasStrength(self, value):
        if self._closed:
            return
        # QML numbers, including Math.round(), cross QVariant as Python floats.
        # Convert at the presentation boundary; F1 keeps its strict integer rule.
        if not math.isfinite(value):
            self._notice('Intensidade RCAS inválida.', True)
            self.settingsChanged.emit()
            return
        strength = int(math.floor(max(0.0, min(100.0, value)) + 0.5))
        self.setSetting('rcas_strength', strength)

    @Slot()
    def dismissNotification(self):
        if self._closed:
            return
        self._notification = ''
        self._notification_error = False
        self.changed.emit()

    @Slot()
    def saveSettings(self):
        if self._closed:
            return
        try:
            # Targets are private, generated paths. QML cannot supply a real path.
            config.update_knack2_ini(self.workspace / 'knack2.ini', self._settings)
            config.save_launcher_settings({'game_dir': self._game_dir, 'mode': 'demo'},
                                          path=self.workspace / 'launcher_settings.json')
        except (OSError, ValueError) as error:
            self._notice(str(error), True)
        else:
            self._notice('Configurações salvas na área temporária da demonstração. Serão descartadas ao fechar.')

    @Slot()
    def play(self):
        if not self.canPlay:
            return
        self.clearLogs()
        self._stop_requested = False
        # Reserve state synchronously, blocking double clicks before worker delivery.
        self._state = 'STARTING'
        self._session = ''
        self.changed.emit()
        try:
            self._runner.start()
        except Exception as error:
            self._state = 'FAILED'
            self._notice(str(error), True)

    @Slot()
    def stop(self):
        if self.canStop:
            self._stop_requested = True
            self._state = 'STOPPING'
            self.changed.emit()
            self._runner.stop()

    @Slot()
    def simulateError(self):
        if self._state == 'RUNNING':
            self._runner.inject_error()

    @Slot()
    def clearLogs(self):
        with self._pending_lock:
            self._pending.clear()
            self._dropped = 0
        self.supervisor_model.clear()
        self.guest_model.clear()
        self.changed.emit()

    @Slot()
    def copyLogs(self):
        text = (f'Knack II / F3A / {self.sessionId}\n\nSupervisor\n' +
                '\n'.join(self.supervisor_model.lines) + '\n\nGuest (sintético)\n' +
                '\n'.join(self.guest_model.lines))
        QGuiApplication.clipboard().setText(text)
        self._notice('Logs copiados.')

    @Slot()
    def shutdown(self):
        if self._closed:
            return
        self._closed = True
        self._validation_cancel.set()
        self._timer.stop()
        self._runner.close()
        self._workspace.cleanup()

    @Slot(result=bool)
    def requestClose(self):
        self.shutdown()
        return True
