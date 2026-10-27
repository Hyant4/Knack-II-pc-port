# SPDX-License-Identifier: GPL-2.0-or-later
"""F3B presentation model, using the Knack II supervisor without guest changes."""
from __future__ import annotations

from pathlib import Path
import queue
import threading
import uuid

from PySide6.QtCore import Property, QTimer, Signal, Slot, QUrl, Qt
from PySide6.QtGui import QDesktopServices, QGuiApplication

from .. import config
from ..gpu_presets import matching_gpu_preset
from ..gtx1060_diagnostics import PRESET as GTX1060_PRESET, validate_mode as validate_gtx1060_mode
from ..runner import KnackRunner, RunnerState
from ..languages import BY_CODE, REGION_LANGUAGES
from ..identity import runtime_path
from ..storage import application_data_root, profile_user
from ..paths import project_root
from .backend import Backend, DEFAULTS, MAX_LINE

ROOT = project_root()
P2_USER_ID = 4099
VALIDATION_RECORD_SCHEMA = 1
# Existing B2-F2C profile, already compiled into the verified COOP-05 artifact.
AUDIO_PROFILE = ('# Knack II audio baseline: COOP-05 / B2-F2C\n'
                 'ngs2_atrac9_hle=1\nngs2_render_mix=1\nngs2_audio_diag=1\n'
                 'ngs2_b2f2a_stable_cohort=0\nngs2_b2f2c_zero_matrix_bypass=1')
LABELS = {'IDLE': 'Pronto', 'STARTING': 'Iniciando sessão', 'RUNNING': 'Supervisor ativo',
          'STOPPING': 'Encerrando sessão', 'FINISHED': 'Sessão encerrada', 'FAILED': 'Falha na sessão'}


class RealBackend(Backend):
    sessionChanged = Signal()
    closeReady = Signal()
    _stateTokenArrived = Signal(str, object, str, str)

    def __init__(self, *, data_dir=None, session_factory=KnackRunner, auto_play=False, **kwargs):
        super().__init__(**kwargs)
        self._real_mode = True
        self._workspace.cleanup()
        self.workspace = Path(data_dir if data_dir is not None else application_data_root()).resolve()
        self._session_factory = session_factory
        self._players = 1
        self._available_languages = ()
        self._operation = None
        self._results = queue.Queue()
        self._closing = False
        self._generation = ''
        self._stop_completed = False
        self._out = None
        self._log_path = None
        self._supervisor_stream = None
        self._log_error_reported = False
        self._stream_lock = threading.Lock()
        self._runner = None
        self._request_auto_play = bool(auto_play)
        self._auto_play_record = None
        self.changed.connect(self.sessionChanged.emit)
        self._stateTokenArrived.connect(self._apply_token_state, Qt.QueuedConnection)
        self._metadata = ''
        self._notification = 'Selecione e valide seu dump. O jogo inicia somente ao clicar em Jogar.'
        saved = config.load_launcher_settings(self.workspace / 'launcher_settings.json')
        self._game_dir = str(saved.get('game_dir', ''))
        self._saved_validation = self._load_validation_record(saved.get('validation'))
        self._language = saved.get('language', 'pt-BR')
        if not isinstance(self._language, str) or self._language not in BY_CODE:
            self._language = 'pt-BR'
        ui_language = saved.get('ui_language')
        if isinstance(ui_language, str) and ui_language in ('pt-BR', 'en-US', 'ru-RU'):
            self._ui_language = ui_language
        # P2 is deliberately never restored. Every opt-in is explicit for this UI session.
        for key, value in saved.get('settings', {}).items() if isinstance(saved.get('settings'), dict) else ():
            try:
                if key in DEFAULTS:
                    self._settings[key] = config.sanitize_and_validate_key_value(key, value)[1]
            except ValueError:
                pass
        selected = saved.get('gpu_preset')
        if isinstance(selected, str) and selected != 'custom' and matching_gpu_preset(self._settings, selected) == selected:
            self._selected_gpu_preset = selected
        if self.gpuPresetId == GTX1060_PRESET:
            try:
                self._gtx1060_diagnostic = validate_gtx1060_mode(saved.get('gtx1060_diagnostic', 'off'))
            except ValueError:
                pass

    @staticmethod
    def _canonical_game_dir(path):
        return str(Path(path).resolve()) if path else ''

    @staticmethod
    def _load_validation_record(value):
        if not isinstance(value, dict):
            return None
        required = ('schema', 'status', 'game_dir', 'title_id', 'version')
        if (value.get('schema') != VALIDATION_RECORD_SCHEMA or
                value.get('status') not in ('valid', 'invalid') or
                not all(isinstance(value.get(key), str) and value[key] for key in required[2:])):
            return None
        return {key: value[key] for key in required}

    def _validation_record(self, result, error):
        record = {
            'schema': VALIDATION_RECORD_SCHEMA,
            'status': 'invalid' if error else 'valid',
            'game_dir': self._canonical_game_dir(self._game_dir),
            'title_id': str((result or {}).get('title_id') or 'unknown'),
            'version': str((result or {}).get('version') or 'unknown'),
        }
        if error:
            record['detail'] = str(error)[:MAX_LINE]
        return record

    def _save_validation_record(self, result, error):
        record = self._validation_record(result, error)
        try:
            self._check_data_root()
            path = self._data_path('launcher_settings.json')
            saved = config.load_launcher_settings(path)
            saved['game_dir'] = self._game_dir
            saved['validation'] = record
            config.save_launcher_settings(saved, path=path)
            self._saved_validation = record
        except (OSError, ValueError) as exc:
            self._notice(f'Validação concluída, mas não foi possível registrar o resultado: {exc}', True)

    def _matches_saved_validation(self, record, result):
        return bool(record and record.get('status') == 'valid' and
                    record.get('game_dir') == self._canonical_game_dir(self._game_dir) and
                    record.get('title_id') == str(result.get('title_id')) and
                    record.get('version') == str(result.get('version')))

    def _working(self):
        return self._operation is not None and self._operation.is_alive()

    players = Property(int, lambda self: self._players, notify=sessionChanged)
    outputText = Property(str, lambda self: str(self._out or self.workspace / 'single'), notify=sessionChanged)
    sessionDetails = Property(str, lambda self: self._metadata, notify=sessionChanged)

    def _state_label(self):
        return self._translate(LABELS[self._state])

    def _editable(self):
        return (not self._closed and not self._validating and not self._working() and
                self._state in ('IDLE', 'FINISHED', 'FAILED'))

    def _can_play(self):
        return (self.editable and not self._closing and self._validation_state == 'valid' and
                (self._runner is None or not self._runner.is_running))

    def _language_codes(self):
        return self._available_languages

    @Slot(str)
    def setUiLanguage(self, code):
        if self._closed or code not in ('pt-BR', 'en-US', 'ru-RU'):
            return
        super().setUiLanguage(code)
        try:
            self._check_data_root()
            path = self._data_path('launcher_settings.json')
            saved = config.load_launcher_settings(path)
            saved['ui_language'] = self._ui_language
            config.save_launcher_settings(saved, path=path)
        except (OSError, ValueError) as error:
            self._notice(str(error), True)

    def _on_validated(self, result):
        regional = REGION_LANGUAGES.get(result.get('title_id'), ())
        self._available_languages = tuple(code for code in regional
                                          if code in result.get('languages', regional))
        if not self._available_languages:
            self._validation_state = 'invalid'
            self._validation = 'Nenhum idioma compatível encontrado no dump.'
        elif self._language not in self._available_languages:
            regional = {'pt-BR': 'pt-PT', 'pt-PT': 'pt-BR', 'en-US': 'en-GB',
                        'en-GB': 'en-US', 'es-419': 'es-ES', 'es-ES': 'es-419'}
            fallback = next((code for code in (regional.get(self._language), 'en-US', 'en-GB')
                             if code in self._available_languages), self._available_languages[0])
            self.setLanguage(fallback)
        self.changed.emit()

    def _on_validation_finished(self, result, error, *, auto_play=False):
        previous = self._auto_play_record
        self._auto_play_record = None
        self._save_validation_record(result, error)
        if not auto_play:
            return
        if error:
            self._notice('Não iniciou: a validação automática encontrou o erro acima.', True)
            return
        if self._matches_saved_validation(previous, result):
            QTimer.singleShot(0, self.play)
            return
        if previous is None:
            reason = ('este dump ainda não tinha uma validação salva antes desta abertura. '
                      'A validação atual foi concluída; clique em Jogar para confirmar.')
        else:
            reason = ('o dump validado anteriormente não é o mesmo título, versão ou pasta da instalação atual. '
                      'Revise o resultado e clique em Jogar para confirmar.')
        self._notice('Não iniciou automaticamente: ' + reason, True)

    def _can_stop(self):
        return not self._closed and (self._state in ('STARTING', 'RUNNING', 'STOPPING') or
                                    (self._runner is not None and self._runner.is_running))

    @Slot(int)
    def setPlayers(self, value):
        if self.editable and value in (1, 2):
            self._players = value
            self.changed.emit()

    @Slot(str)
    def selectFolder(self, text):
        if self.editable:
            self._available_languages = ()
            super().selectFolder(text)
            try:
                self._check_data_root()
                path = self._data_path('launcher_settings.json')
                saved = config.load_launcher_settings(path)
                saved['game_dir'] = self._game_dir
                config.save_launcher_settings(saved, path=path)
            except (OSError, ValueError) as error:
                self._notice(str(error), True)
            else:
                QTimer.singleShot(0, self.autoValidateSavedDump)

    @Slot()
    def validateFolder(self):
        if self.editable:
            self._available_languages = ()
            self._auto_play_record = None
            self._start_validation()

    @Slot()
    def autoValidateSavedDump(self):
        if self._closed or self._validating or not self._game_dir:
            if self._request_auto_play and not self._game_dir:
                self._notice('Não iniciou: nenhuma pasta de dump foi salva. Escolha uma pasta e valide-a primeiro.', True)
            return
        self._available_languages = ()
        auto_play = self._request_auto_play
        self._request_auto_play = False
        self._auto_play_record = dict(self._saved_validation) if auto_play and self._saved_validation else None
        self._start_validation(auto_play=auto_play)

    def _check_data_root(self):
        # This launcher has its own data root. Never write into dump or habitual saves.
        protected = [ROOT / 'out/knack2', ROOT / 'port/user']
        if self._game_dir:
            protected.append(Path(self._game_dir).resolve())
        for path in protected:
            path = path.resolve()
            if self.workspace.is_relative_to(path) or path.is_relative_to(self.workspace):
                raise ValueError('Área F3B sobrepõe dump ou dados habituais. Escolha outro --data-dir.')

    def _data_path(self, relative):
        path = (self.workspace / relative).resolve()
        if not path.is_relative_to(self.workspace):
            raise ValueError('Caminho de saída redirecionado para fora da área F3B.')
        return path

    @Slot()
    def saveSettings(self):
        if not self.editable:
            return
        try:
            self._check_data_root()
            config.update_knack2_ini(self._data_path('knack2.ini'), self._settings)
            path = self._data_path('launcher_settings.json')
            saved = config.load_launcher_settings(path)
            saved.update({'game_dir': self._game_dir, 'settings': self._settings,
                          'language': self._language, 'ui_language': self._ui_language,
                          'gpu_preset': self.gpuPresetId,
                          'gtx1060_diagnostic': self.gtx1060Diagnostic})
            config.save_launcher_settings(saved, path=path)
        except (OSError, ValueError) as error:
            self._notice(str(error), True)
        else:
            self._notice('Configurações salvas. Aplicação na próxima partida.')

    def post_log(self, source, line):
        if source == 'supervisor':
            with self._stream_lock:
                if self._supervisor_stream is not None:
                    try:
                        self._supervisor_stream.write(str(line) + '\n')
                        self._supervisor_stream.flush()
                    except OSError as error:
                        if not self._log_error_reported:
                            self._log_error_reported = True
                            self._results.put(('log', str(error)))
        super().post_log(source, line)

    def _post_session_log(self, token, source, line):
        if token == self._generation:
            self.post_log(source, line)

    @Slot(str, object, str, str)
    def _apply_token_state(self, token, state, session_id, error):
        if token == self._generation:
            self._apply_state(state, session_id, error)

    def _apply_state(self, state, session_id, error):
        if self._closed or self._stop_completed or (self._stop_requested and state in (RunnerState.STARTING, RunnerState.RUNNING)):
            return
        # Keep the launch reserved until a pending stop and worker cleanup complete.
        self._state = 'STOPPING' if self._stop_requested and self._working() else state.value
        self._session = session_id
        if error:
            self._notice(error, True)
        elif not self._stop_requested:
            self._notice(LABELS[self._state] + '.')

    def _operate(self, kind, action):
        def work():
            try:
                action()
                error = ''
            except Exception as exc:
                error = str(exc) or type(exc).__name__
            self._results.put((kind, error))
        self._operation = threading.Thread(target=work, name='Knack2-SessionAction', daemon=True)
        self._operation.start()

    def _close_stream(self):
        with self._stream_lock:
            if self._supervisor_stream is not None:
                self._supervisor_stream.close()
                self._supervisor_stream = None

    @Slot()
    def play(self):
        if not self.canPlay:
            if self._validating:
                reason = 'a validação automática ainda está em andamento. Aguarde o resultado.'
            elif self._validation_state == 'invalid':
                reason = self._validation
            elif self._state in ('STARTING', 'RUNNING', 'STOPPING'):
                reason = 'já existe uma sessão em andamento.'
            else:
                reason = 'nenhuma instalação válida foi confirmada.'
            self._notice('Não iniciou: ' + reason, True)
            return
        try:
            self._check_data_root()
            if not runtime_path(ROOT / 'port').is_file():
                raise ValueError('Runtime ausente: compile tlg-probe antes de jogar.')
            token = uuid.uuid4().hex
            out = self._data_path('coop-' + token if self._players == 2 else 'single')
            if self._players == 2 and (out.exists() or len(str(out / 'user').encode('utf-8')) >= 512):
                raise ValueError('Saída P2 precisa ser nova e ter caminho inferior a 512 bytes.')
            second_user = P2_USER_ID if self._players == 2 else None
            user = profile_user(self.workspace, self._players)
            if not user.is_relative_to(self.workspace) or len(str(user).encode('utf-8')) >= 512:
                raise ValueError('Caminho do perfil de saves inválido.')
            settings_file = self._data_path(Path('launch-configs') / (token + '.ini'))
            log = self._data_path(Path('logs') / (token + '-supervisor.log'))
            config.update_knack2_ini(settings_file, self._settings, header_comment=AUDIO_PROFILE)
            log.parent.mkdir(parents=True, exist_ok=True)
            previous = self._runner
            diagnostic_options = ({'gtx1060_diagnostic': self.gtx1060Diagnostic}
                                  if self.gtx1060Diagnostic != 'off' else {})
            runner = self._session_factory(self._game_dir, timeout=0, out_dir=out,
                                          **diagnostic_options,
                                          config_file=settings_file, local_users=self._players, language=self._language,
                                          second_user=second_user,
                                          user_data=user,
                                          on_state_change=lambda state: self._stateTokenArrived.emit(
                                              token, state, runner.session_info.session_id, runner.error_message),
                                          on_log_line=lambda line: self._post_session_log(token, 'supervisor', line),
                                          on_guest_log_line=lambda line: self._post_session_log(token, 'guest', line))
        except (OSError, ValueError) as error:
            self._notice(str(error), True)
            return
        self.clearLogs()
        self._stop_requested = False
        self._stop_completed = False
        self._generation = token
        self._state, self._session = 'STARTING', ''
        self._out, self._log_path = out, log
        self._notice('Preparando sessão pelo supervisor oficial. Aguarde os registros de execução.')

        def launch():
            if previous is not None:
                previous.close()  # Join old workers before replacing callback ownership.
            self._close_stream()
            self._supervisor_stream = log.open('w', encoding='utf-8')
            self._log_error_reported = False
            self._runner = runner
            runner.start()  # Never pass inherited runtime overrides; run.py filters them.
        self._operate('start', launch)

    @Slot()
    def stop(self):
        if not self.canStop:
            return
        self._stop_requested = True
        self._state = 'STOPPING'
        self._notice('Encerramento solicitado. Aguardando supervisor e processo do jogo.')
        if not self._working():
            def cancel():
                if self._runner is not None:
                    self._runner.stop()
                    self._runner.close()
                self._close_stream()
            self._operate('stop', cancel)

    @Slot()
    def _drain(self):
        super()._drain()
        if self._closed:
            return
        if not self._working():
            while not self._results.empty():
                kind, error = self._results.get_nowait()
                if kind == 'log':
                    self._notice('Falha ao gravar log do supervisor: ' + error, True)
                    continue
                self._operation = None
                if error:
                    # Do not discard a live runner, unblock launch, or close the window on stop failure.
                    self._closing = False
                    self._state = 'STOPPING' if kind == 'stop' or (self._runner and self._runner.is_running) else 'FAILED'
                    self._notice(error, True)
                    self._stop_requested = False
                elif kind == 'stop':
                    self._state = 'FINISHED'
                    self._stop_completed = True
                    self._stop_requested = False
                    self._notice('Sessão encerrada por solicitação. Logs e saída preservados.')
                self.changed.emit()
            if self._stop_requested:
                self.stop()
            elif self._closing:
                self.closeReady.emit()
        if self._runner is not None:
            info = self._runner.session_info
            details = ('Sessão ambígua; consulte a saída do supervisor.' if info.is_ambiguous else
                       f'{info.current_executable or "Preparação"} · PID {self._runner.pid or "—"} · '
                       f'Código {self._runner.exit_code if self._runner.exit_code is not None else "—"}\n'
                       f'Logs do supervisor: {self._log_path}\nLogs do jogo: {info.session_dir or "aguardando sessão"}')
            if details != self._metadata or self._session != info.session_id:
                self._metadata, self._session = details, info.session_id
                self.changed.emit()

    @Slot()
    def openLogs(self):
        directory = self._out if self._out and self._out.is_dir() else self.workspace / 'logs'
        if not directory.is_dir() or not QDesktopServices.openUrl(QUrl.fromLocalFile(str(directory))):
            self._notice('Não foi possível abrir a pasta de logs.', True)

    @Slot()
    def copyLogs(self):
        QGuiApplication.clipboard().setText(f'Knack II / F3B / {self.sessionId}\n{self._metadata}\n\nSupervisor\n' +
                                          '\n'.join(self.supervisor_model.lines) + '\n\nJogo\n' +
                                          '\n'.join(self.guest_model.lines))
        self._notice('Logs copiados.')

    @Slot(result=bool)
    def requestClose(self):
        if self._working() or self._state in ('STARTING', 'RUNNING', 'STOPPING'):
            self._closing = True
            self.stop()
            return False
        self.shutdown()
        return True

    @Slot()
    def shutdown(self):
        if self._closed:
            return
        # QML defers closing while an action runs. Never drop ownership of a live session.
        if self._working() or (self._runner is not None and self._runner.is_running):
            self._closing = True
            self.stop()
            return
        if self._runner is not None:
            self._runner.close()
        self._close_stream()
        self._closed = True
        self._validation_cancel.set()
        self._timer.stop()
