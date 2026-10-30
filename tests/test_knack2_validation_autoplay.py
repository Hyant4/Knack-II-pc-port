"""Persistent validation and direct-play gates without starting a game process."""
from __future__ import annotations

import os
from pathlib import Path
import tempfile
import time
import unittest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QUICK_BACKEND', 'software')

try:
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtTest import QTest
    from port.launcher.knack2 import config, validator
    from port.launcher.knack2.ui.real import RealBackend
    HAS_QT = True
except ImportError:
    HAS_QT = False


@unittest.skipUnless(HAS_QT, 'PySide6 unavailable')
class ValidationAutoplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QGuiApplication.instance() or QGuiApplication(['knack-validation-tests'])

    def pump_until(self, predicate, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.app.processEvents()
            if predicate():
                return
            QTest.qWait(10)
        self.fail('condition did not become true')

    @staticmethod
    def result(title_id='CUSA08014', version='01.01'):
        return {'app_title': 'Knack II synthetic', 'title_id': title_id,
                'version': version, 'languages': ('pt-BR',)}

    def make_backend(self, data_dir, *, result=None, error=None, auto_play=False):
        def validate(_path):
            if error is not None:
                raise error
            return dict(result or self.result())
        return RealBackend(data_dir=data_dir, validate_dump=validate, auto_play=auto_play)

    def validate(self, backend):
        backend.autoValidateSavedDump()
        self.pump_until(lambda: not backend.validating)

    def test_validation_is_saved_and_matching_record_allows_direct_play(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, game_dir = root / 'data', root / 'game'
            data_dir.mkdir(); game_dir.mkdir()
            config.save_launcher_settings({'game_dir': str(game_dir)}, path=data_dir / 'launcher_settings.json')

            first = self.make_backend(data_dir)
            self.validate(first)
            saved = config.load_launcher_settings(data_dir / 'launcher_settings.json')['validation']
            self.assertEqual(saved['status'], 'valid')
            self.assertEqual(saved['game_dir'], str(game_dir.resolve()))
            self.assertEqual((saved['title_id'], saved['version']), ('CUSA08014', '01.01'))
            first.setPlayers(2)
            first.saveSettings()
            self.assertEqual(
                config.load_launcher_settings(data_dir / 'launcher_settings.json')['validation']['status'], 'valid')
            first.shutdown()

            second = self.make_backend(data_dir, auto_play=True)
            launched = []
            second.play = lambda: launched.append(second.players)
            self.validate(second)
            self.pump_until(lambda: bool(launched))
            self.assertEqual(launched, [2])
            second.shutdown()

    def test_direct_play_needs_prior_validation_even_when_current_check_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, game_dir = root / 'data', root / 'game'
            data_dir.mkdir(); game_dir.mkdir()
            config.save_launcher_settings({'game_dir': str(game_dir)}, path=data_dir / 'launcher_settings.json')
            backend = self.make_backend(data_dir, auto_play=True)
            launched = []
            backend.play = lambda: launched.append(True)
            self.validate(backend)
            self.assertEqual(launched, [])
            self.assertEqual(backend.validationState, 'valid')
            self.assertIn('ainda não tinha uma validação salva', backend.notification)
            backend.shutdown()

    def test_direct_play_blocks_when_saved_version_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, game_dir = root / 'data', root / 'game'
            data_dir.mkdir(); game_dir.mkdir()
            config.save_launcher_settings({
                'game_dir': str(game_dir),
                'validation': {'schema': 1, 'status': 'valid', 'game_dir': str(game_dir.resolve()),
                               'title_id': 'CUSA08014', 'version': '01.01'},
            }, path=data_dir / 'launcher_settings.json')
            backend = self.make_backend(data_dir, result=self.result(version='01.02'), auto_play=True)
            launched = []
            backend.play = lambda: launched.append(True)
            self.validate(backend)
            self.assertEqual(launched, [])
            self.assertIn('não é o mesmo título, versão ou pasta', backend.notification)
            backend.shutdown()

    def test_hash_failure_explains_error_and_persists_invalid_record(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data_dir, game_dir = root / 'data', root / 'game'
            data_dir.mkdir(); game_dir.mkdir()
            config.save_launcher_settings({'game_dir': str(game_dir)}, path=data_dir / 'launcher_settings.json')
            backend = self.make_backend(
                data_dir, error=validator.KnackHashMismatchError('eboot.bin: SHA-256 difere do dump suportado'))
            self.validate(backend)
            self.assertEqual(backend.validationState, 'invalid')
            self.assertIn('Falha de validação: eboot.bin', backend.validationText)
            self.assertIn('Motivo: Um executável não corresponde', backend.validationText)
            saved = config.load_launcher_settings(data_dir / 'launcher_settings.json')['validation']
            self.assertEqual(saved['status'], 'invalid')
            backend.shutdown()


if __name__ == '__main__':
    unittest.main()
