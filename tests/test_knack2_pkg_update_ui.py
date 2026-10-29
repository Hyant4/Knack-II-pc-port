"""Qt update ownership and actual QML controls with synthetic installer only."""
import os
from pathlib import Path
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QUICK_BACKEND', 'software')
from PySide6.QtCore import QObject, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtTest import QTest
from port.launcher.knack2 import config, pkg_update
from port.launcher.knack2.ui.app import create_window
from port.launcher.knack2.ui.real import RealBackend


class UiUpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        QQuickStyle.setStyle('Basic')
        cls.app = QGuiApplication.instance() or QGuiApplication(['pkg-ui-tests'])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='knack-pkg-ui-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.game = self.root / 'game'
        self.game.mkdir()
        self.result = {'valid': True, 'app_title': 'Synthetic', 'title_id': 'CUSA08014',
                       'version': '01.01', 'languages': ['pt-BR']}
        self.backend = RealBackend(data_dir=self.root / 'data', validate_dump=lambda path: self.result)
        self.backend._game_dir = str(self.game)
        self.manager = self.backend.updateManager
        self.errors = []
        self.handler = qInstallMessageHandler(lambda kind, ctx, message: self.errors.append(message))
        self.engine, self.window, _ = create_window(self.backend)
        self.window.setProperty('page', 3)
        self.pump(lambda: self.window.isVisible())

    def tearDown(self):
        self.manager.cancel()
        self.pump(lambda: not self.manager.busy)
        self.window.close()
        self.backend.shutdown()
        self.engine.deleteLater()
        self.app.processEvents()
        qInstallMessageHandler(self.handler)
        failures = [s for s in self.errors if any(x in s for x in ('ReferenceError', 'TypeError', 'binding loop', 'failed to load'))]
        self.assertEqual(failures, [])

    def pump(self, predicate, timeout=3):
        end = time.monotonic() + timeout
        while not predicate() and time.monotonic() < end:
            QTest.qWait(10)
        self.assertTrue(predicate())

    def test_real_controls_localization_and_preview_confirmation(self):
        class Fake:
            def preview(_self, game, path):
                return {'title_id': 'CUSA08014', 'version': '01.01', 'pkg_size': 1024}
        self.manager.installer = Fake()
        self.manager.inspect('synthetic.pkg')
        self.pump(lambda: not self.manager.busy)
        dialog = self.window.findChild(QObject, 'updateConfirm')
        self.assertTrue(dialog.property('visible'))
        self.assertIn('CUSA08014', self.manager.preview)
        button = self.window.findChild(QObject, 'installUpdateButton')
        self.assertIsNotNone(button)
        self.backend.setUiLanguage('en-US')
        self.app.processEvents()
        self.assertEqual(button.property('text'), 'Install update (.pkg)')
        self.backend.setUiLanguage('ru-RU')
        self.app.processEvents()
        self.assertEqual(button.property('text'), 'Установить обновление (.pkg)')

    def test_install_blocks_play_folder_changes_and_double_apply_then_revalidates(self):
        entered, release = threading.Event(), threading.Event()
        calls = []
        result = self.result
        class Fake:
            def install(_self, game, info, *, cancel, progress):
                calls.append(game)
                progress('Aplicando atualização com backup dos arquivos substituídos.')
                entered.set()
                release.wait(2)
                return result
        self.manager.installer = Fake()
        self.manager._game = str(self.game)
        self.manager._info = {'path': 'synthetic.pkg', 'title_id': 'CUSA08014', 'version': '01.01', 'pkg_size': 1024}
        self.backend._validation_state = 'valid'
        self.manager.install()
        self.pump(entered.is_set)
        self.assertFalse(self.backend.editable)
        self.assertFalse(self.backend.canPlay)
        self.backend.selectFolder('another-folder')
        self.assertEqual(self.backend.gameDir, str(self.game))
        self.manager.install()
        self.assertEqual(len(calls), 1)
        release.set()
        self.pump(lambda: not self.manager.busy and not self.backend.validating)
        self.assertTrue(self.backend.canPlay)
        saved = config.load_launcher_settings(self.root / 'data/launcher_settings.json')
        self.assertEqual(saved['validation']['status'], 'valid')

    def test_close_waits_for_cancel_and_invalidates_failed_update(self):
        entered = threading.Event()
        class Fake:
            def install(_self, game, info, *, cancel, progress):
                entered.set()
                cancel.wait(2)
                raise pkg_update.UpdateCancelled('synthetic cancellation after rollback')
        self.manager.installer = Fake()
        self.manager._game = str(self.game)
        self.manager._info = {'path': 'synthetic.pkg', 'title_id': 'CUSA08014', 'version': '01.01', 'pkg_size': 1024}
        self.manager.install()
        self.pump(entered.is_set)
        self.assertFalse(self.backend.requestClose())
        self.pump(lambda: not self.manager.busy)
        self.assertEqual(self.backend.validationState, 'invalid')
        self.assertFalse(self.backend.canPlay)
        self.assertIn('synthetic cancellation', self.manager.status)

    def test_base_extraction_needs_no_game_selection_and_selects_pkg_parent(self):
        destination = self.root / 'downloads'; destination.mkdir()
        self.backend._game_dir = ''
        self.result['version'] = '01.00'
        class Fake:
            def preview(_self, path, **kwargs):
                return {'title_id': 'CUSA08014', 'version': '01.00', 'pkg_size': 1024,
                        'destination': str(destination)}
            def install(_self, info, **kwargs):
                return {'destination': str(destination)}
        self.manager.base_extractor = Fake()
        self.manager.inspectBase(str(destination / 'game.pkg'))
        self.pump(lambda: not self.manager.busy)
        dialog = self.window.findChild(QObject, 'baseConfirm')
        self.assertTrue(dialog.property('visible'))
        button = self.window.findChild(QObject, 'extractBaseButton')
        for language, expected in [('en-US', 'Extract base game'), ('ru-RU', 'Извлечь основную игру')]:
            self.backend.setUiLanguage(language)
            self.assertIn(expected, button.property('text'))
            self.assertIn(str(destination), self.manager.preview)
        self.manager.extractBase()
        self.pump(lambda: not self.manager.busy and not self.backend.validating)
        self.assertEqual(self.backend.gameDir, str(destination))
        self.assertEqual(self.backend.validationState, 'valid')

    def test_base_close_cancels_and_preserves_previous_game_validation(self):
        entered = threading.Event()
        self.backend._validation_state = 'valid'
        class Fake:
            def install(_self, info, *, cancel, progress):
                entered.set(); cancel.wait(2)
                raise pkg_update.UpdateCancelled('Extração do jogo cancelada.')
        self.manager.base_extractor = Fake()
        self.manager._mode = 'base'
        self.manager._info = {'destination': str(self.root), 'title_id': 'CUSA08014', 'version': '01.00'}
        self.manager.extractBase()
        self.pump(entered.is_set)
        self.assertFalse(self.backend.requestClose())
        self.pump(lambda: not self.manager.busy)
        self.assertEqual(self.backend.gameDir, str(self.game))
        self.assertEqual(self.backend.validationState, 'valid')

    def test_interrupted_transaction_stays_invalid_after_ordinary_validation(self):
        root = pkg_update.transaction_root(self.game)
        root.mkdir()
        pkg_update._journal(root, {'schema': 1, 'game': str(self.game), 'state': 'applying', 'files': []})
        self.backend.validateFolder()
        self.pump(lambda: not self.backend.validating)
        self.assertEqual(self.backend.validationState, 'invalid')
        self.assertFalse(self.backend.canPlay)
        saved = config.load_launcher_settings(self.root / 'data/launcher_settings.json')
        self.assertEqual(saved['validation']['status'], 'invalid')
        self.assertIn('interrompida', self.backend.validationText)


if __name__ == '__main__':
    unittest.main()
