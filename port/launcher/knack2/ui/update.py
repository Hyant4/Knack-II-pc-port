# SPDX-License-Identifier: GPL-2.0-or-later
"""Qt ownership for the local PKG installer. Workers never mutate UI properties."""
from pathlib import Path
import queue
import threading

from PySide6.QtCore import QObject, Property, QTimer, Signal, Slot, QUrl
from PySide6.QtGui import QDesktopServices

from .. import pkg_update, pkg_base


class UpdateController(QObject):
    changed = Signal()
    previewReady = Signal()
    basePreviewReady = Signal()

    def __init__(self, backend, installer=None):
        super().__init__(backend)
        self.backend = backend
        self.installer = installer or pkg_update.UpdateInstaller()
        self.base_extractor = pkg_base.BaseExtractor()
        self._mode = 'update'
        self._thread = None
        self._busy = False
        self._info = None
        self._game = ''
        self._status = ''
        self._status_error = False
        self._cancel = threading.Event()
        self._events = queue.Queue()
        self._timer = QTimer(self)
        self._timer.setInterval(50)
        self._timer.timeout.connect(self._drain)
        self._timer.start()
        backend.uiLanguageChanged.connect(self.changed.emit)

    busy = Property(bool, lambda self: self._busy, notify=changed)
    status = Property(str, lambda self: (self.backend._translate_error if self._status_error
                                        else self.backend._translate)(self._status), notify=changed)
    preview = Property(str, lambda self: self._summary(), notify=changed)

    def _summary(self):
        if not self._info:
            return ''
        info = self._info
        if self._mode == 'base':
            return (f"{info['title_id']} · {info['version']}\n" +
                    self.backend._translate('Arquivos extraídos diretamente na pasta do PKG:') +
                    '\n' + info['destination'] + '\n\n' +
                    self.backend._translate('O PKG original será preservado. Arquivos existentes não serão sobrescritos.'))
        return (f"{info['title_id']} · {info['version']}\n"
                f"PKG: {info['pkg_size'] / 1024**2:.1f} MiB\n" +
                self.backend._translate('O espaço descompactado será conferido durante a extração. Somente arquivos substituídos terão backup.'))

    def _start(self, kind, work):
        if self._busy or not self.backend.editable or self.backend._closing:
            return
        self._busy = True
        self._status_error = False
        self._cancel.clear()
        self.changed.emit()
        self.backend.changed.emit()

        def execute():
            try:
                value = work()
                error = ''
            except Exception as exc:
                value, error = None, str(exc) or type(exc).__name__
            self._events.put((kind, value, error))

        # Non-daemon: application exit cannot silently abandon a file replacement.
        self._thread = threading.Thread(target=execute, name='Knack2-PKG', daemon=False)
        self._thread.start()

    @Slot(str)
    def inspect(self, url):
        if not self.backend.gameDir:
            self.backend._notice('Selecione a pasta do jogo antes de escolher o update.', True)
            return
        if self._busy or not self.backend.editable:
            return
        parsed = QUrl(url)
        pkg = parsed.toLocalFile() if parsed.isLocalFile() else url
        self._game = self.backend.gameDir
        self._mode = 'update'
        self._info = None
        self._status = 'Analisando atualização…'
        self._start('inspect', lambda: self.installer.preview(self._game, pkg))

    @Slot(str)
    def inspectBase(self, url):
        if self._busy or not self.backend.editable:
            return
        parsed = QUrl(url)
        pkg = parsed.toLocalFile() if parsed.isLocalFile() else url
        self._mode = 'base'
        self._info = None
        self._status = 'Analisando PKG do jogo-base…'
        self._start('inspect_base', lambda: self.base_extractor.preview(pkg, cancel=self._cancel))

    @Slot()
    def extractBase(self):
        if not self._info or self._mode != 'base':
            return
        info = dict(self._info)
        self._start('extract_base', lambda: self.base_extractor.install(
            info, cancel=self._cancel, progress=lambda text: self._events.put(('progress', text, ''))))

    @Slot()
    def install(self):
        if not self._info or self._mode != 'update' or self._game != self.backend.gameDir:
            return
        info, game = dict(self._info), self._game

        def progress(message):
            self._events.put(('progress', message, ''))

        self._start('install', lambda: self.installer.install(
            game, info, cancel=self._cancel, progress=progress))

    @Slot()
    def restore(self):
        game = self.backend.gameDir
        if game:
            self._start('restore', lambda: self.installer.restore(game))

    @Slot()
    def cancel(self):
        if self._busy:
            self._cancel.set()
            self._status = ('Cancelando extração; aguarde a limpeza dos arquivos temporários.' if self._mode == 'base' else
                            'Cancelando; aguarde a restauração dos arquivos.')
            self._status_error = False
            self.changed.emit()

    @Slot()
    def openBackup(self):
        if self.backend.gameDir:
            path = pkg_update.transaction_root(self.backend.gameDir)
            if path.is_dir():
                QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))

    def close(self):
        if self._busy:
            self.cancel()
            return False
        self._timer.stop()
        return True

    @Slot()
    def _drain(self):
        while not self._events.empty():
            kind, value, error = self._events.get_nowait()
            if kind == 'progress':
                self._status = value
                self._status_error = False
                self.changed.emit()
                continue
            # Completion can be queued just before the worker actually returns.
            if self._thread:
                self._thread.join()
            self._busy = False
            self._status_error = bool(error)
            if error:
                self._info = None
                self._status = error
                if kind in ('install', 'restore'):
                    self.backend._validation_state = 'invalid'
                    self.backend._validation = error
                    self.backend._save_validation_record(None, error)
                self.backend._notice(error, True)
            elif kind == 'inspect':
                self._info = value
                self._status = 'Atualização pronta para confirmação.'
                if not self.backend._closing:
                    self.previewReady.emit()
            elif kind == 'inspect_base':
                self._info = value
                self._status = 'Jogo-base pronto para extração.'
                if not self.backend._closing:
                    self.basePreviewReady.emit()
            elif kind == 'extract_base':
                self._info = None
                self._status = 'Jogo-base extraído na pasta do PKG. Valide para jogar.'
                self.backend._notice(self._status)
                if not self.backend._closing:
                    self.backend.selectFolder(value['destination'])
                    self.backend.validateFolder()
            else:
                self._info = None
                self._status = ('Atualização concluída. Backup preservado.' if kind == 'install' else
                                'Arquivos anteriores restaurados. Valide a instalação.')
                self.backend._validation_state = 'empty'
                self.backend._save_validation_record(None, 'Instalação alterada; nova validação obrigatória.')
                self.backend._notice(self._status)
                if not self.backend._closing:
                    self.backend.validateFolder()
            self.changed.emit()
            self.backend.changed.emit()
            if self.backend._closing:
                self.backend.closeReady.emit()
