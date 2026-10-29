"""Regressions for PR #19 reconciled with the current regional launcher."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run as supervisor
from port.launcher.knack2 import validator


class Pr19IntegrationTests(unittest.TestCase):
    def test_pinned_experimental_hash_keeps_its_actual_native_layout(self):
        profile = validator.load_profile(validator.default_profile_path(experimental_build=True))
        digest = profile['executables']['game.elf']
        self.assertEqual(validator.executable_layout_for_hash(profile, digest), 'elf-0b7843d3')
        with self.assertRaises(validator.KnackHashMismatchError):
            validator.executable_layout_for_hash(profile, 'f' * 64)

    def test_firmware_environment_requires_explicit_opt_in(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {'TLG_NGS2': 'native'}):
            root = Path(temporary)
            self.assertNotIn('TLG_NGS2', supervisor.runtime_environment(root))
            self.assertEqual(supervisor.runtime_environment(root, ngs2=True)['TLG_NGS2'], 'native')

    def test_initial_game_language_preserves_independent_saved_ui_language(self):
        from PySide6.QtGui import QGuiApplication
        from port.launcher.knack2.ui.real import RealBackend
        app = QGuiApplication.instance() or QGuiApplication([])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'launcher_settings.json').write_text(json.dumps({'language': 'pt-BR', 'ui_language': 'ru-RU'}))
            backend = RealBackend(data_dir=root, initial_language='en-US')
            try:
                self.assertEqual(backend._language, 'en-US')
                self.assertEqual(backend.uiLanguage, 'ru-RU')
                self.assertFalse(backend._can_play())
            finally:
                backend.shutdown()
                app.processEvents()


if __name__ == '__main__':
    unittest.main()
