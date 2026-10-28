"""Experimental build selection keeps strict validation and reaches the supervisor."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run as supervisor
from port.launcher.knack2 import validator
from port.launcher.knack2.runner import KnackRunner

ROOT = Path(__file__).resolve().parents[1]


class ExperimentalBuildTests(unittest.TestCase):
    def test_profiles_only_differ_in_updated_executable(self):
        normal = validator.load_profile()
        experimental = validator.load_profile(validator.default_profile_path(experimental_build=True))
        self.assertNotEqual(normal['executables']['game.elf'], experimental['executables']['game.elf'])
        experimental['executables']['game.elf'] = normal['executables']['game.elf']
        self.assertEqual(normal, experimental)

    def test_experimental_validator_keeps_hash_version_and_module_checks(self):
        profile = validator.load_profile(validator.default_profile_path(experimental_build=True))
        with tempfile.TemporaryDirectory() as temporary:
            game = Path(temporary)
            (game / 'sce_sys').mkdir()
            (game / 'sce_sys/param.sfo').touch()
            (game / 'sce_module').mkdir()
            for name in profile['executables']:
                (game / name).touch()
            for name in {m for modules in profile['modules'].values() for m in modules}:
                (game / 'sce_module' / name).touch()
            def digest(path):
                return profile['executables'][Path(path).name]
            with patch.object(validator.prepare, 'load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01'}) as sfo, patch.object(validator, 'digest_file', side_effect=digest), patch.object(validator.link_modules, 'module') as module:
                self.assertTrue(validator.validate_knack2_dump(game, profile=profile)['valid'])
                self.assertEqual(module.call_count, 3)
                with self.assertRaises(validator.KnackHashMismatchError):
                    validator.validate_knack2_dump(game)
                sfo.return_value['APP_VER'] = '01.00'
                with self.assertRaises(validator.KnackVersionMismatchError):
                    validator.validate_knack2_dump(game, profile=profile)
                sfo.return_value['APP_VER'] = '01.01'
                module.side_effect = ValueError('bad module')
                with self.assertRaises(validator.KnackModuleError):
                    validator.validate_knack2_dump(game, profile=profile)
            with patch.object(validator.prepare, 'load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01'}), patch.object(validator, 'digest_file', return_value='0' * 64):
                with self.assertRaises(validator.KnackHashMismatchError):
                    validator.validate_knack2_dump(game, profile=profile)

    def test_runner_option_selects_supervisor_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            game = Path(temporary) / 'dump'
            runner = KnackRunner(game, experimental_build=True, analyze_only=True,
                                 out_dir=Path(temporary) / 'analysis')
            seen = []
            with patch.object(supervisor, 'validate_game') as validate, patch.object(supervisor, 'ngs2_module', return_value=None), patch.object(supervisor, 'prepare_image', side_effect=lambda game, profile, name, out: seen.append(profile) or out), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(supervisor.main(runner.build_command()[2:]), 0)
            expected = validator.load_profile(validator.default_profile_path(experimental_build=True))
            self.assertEqual(validate.call_args.args[1], expected)
            self.assertTrue(all(profile == expected for profile in seen))
            self.assertNotIn('--experimental-build', KnackRunner(game).build_command())

    def test_gui_propagates_mode_and_isolates_profile(self):
        from PySide6.QtGui import QGuiApplication
        from port.launcher.knack2.ui.real import RealBackend
        app = QGuiApplication.instance() or QGuiApplication([])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            captured = []
            def factory(game, **kwargs):
                captured.append(KnackRunner(game, **kwargs))
                raise ValueError('command captured without starting a process')
            backend = RealBackend(data_dir=root, session_factory=factory, experimental_build=True)
            try:
                self.assertEqual(backend.workspace, root / 'experimental-build')
                self.assertEqual(backend._validate_dump.keywords['profile_path'],
                                 validator.default_profile_path(experimental_build=True))
                backend._game_dir = str(root / 'dump')
                backend._validation_state = 'valid'
                backend._on_validated({'languages': ['en-US']})
                backend.play()
                self.assertEqual(len(captured), 1)
                self.assertIn('--experimental-build', captured[0].build_command())
                self.assertTrue(captured[0].user_data.is_relative_to(root / 'experimental-build'))
                self.assertIn('Experimental', backend._installation)
            finally:
                backend.shutdown()
                app.processEvents()
