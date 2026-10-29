"""USA 01.00 profile integration with synthetic files; never execute a game."""
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import validator
from tests.test_knack2_regions import ROOT, run, sfo_bytes


class Usa100Tests(unittest.TestCase):
    def test_installation_hint_names_supported_base_in_all_launcher_languages(self):
        from port.launcher.knack2.ui.i18n import translate
        source = ('América: CUSA08014 01.00 ou 01.01. Europa: CUSA07670 01.01. '
                  'Metadados, hashes e módulos devem ser válidos para jogar.')
        for language in ('pt-BR', 'en-US', 'ru-RU'):
            message = translate(language, source)
            self.assertIn('CUSA08014 01.00', message)
            self.assertIn('CUSA07670 01.01', message)
            self.assertNotIn('CUSA07670 01.00', message)
            if language != 'pt-BR':
                self.assertNotEqual(message, source)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='knack-usa100-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.repo = self.root / 'repo'
        (self.repo / 'profiles').mkdir(parents=True)
        self.game = self.root / 'arbitrary-folder-name'
        (self.game / 'sce_sys').mkdir(parents=True)
        (self.game / 'sce_module').mkdir()
        (self.game / 'sce_sys/param.sfo').write_bytes(sfo_bytes('CUSA08014', '01.00'))
        self.profile = copy.deepcopy(validator.load_profile(ROOT / 'profiles/CUSA08014-01.00.json'))
        for name in self.profile['executables']:
            content = f'synthetic USA executable {name}'.encode()
            (self.game / name).write_bytes(content)
            self.profile['executables'][name] = hashlib.sha256(content).hexdigest()
        for name in {m for modules in self.profile['modules'].values() for m in modules}:
            (self.game / 'sce_module' / name).write_bytes(b'synthetic module')
        language = self.game / 'data/lang/ena'
        language.mkdir(parents=True)
        (language / 'synthetic.txt').write_text('synthetic')
        (self.repo / 'profiles/CUSA08014-01.00.json').write_text(json.dumps(self.profile))

    def test_exact_profile_and_existing_executable_hashes(self):
        actual = validator.load_profile(ROOT / 'profiles/CUSA08014-01.00.json')
        previous = validator.load_profile(ROOT / 'profiles/CUSA08014-01.01.json')
        self.assertEqual(actual['version'], '01.00')
        for key in ('title_id', 'entry', 'executables', 'modules'):
            self.assertEqual(actual[key], previous[key])
        self.assertEqual(actual['executables'], {
            'eboot.bin': 'e65b5fd039833ada7dddd8c197b8b47c283a3f46320cd13ece5ff3973a4041db',
            'game.elf': '80ccc22819fa4eeb5e1824e585f3ac5d5168e9ebc82f629e44cdb63fd573fb20',
        })
        for title, version in [('CUSA07670', '01.00'), ('CUSA08014', '01.02')]:
            with self.subTest(title=title, version=version), self.assertRaises(validator.KnackProfileError):
                validator.profile_id({'title_id': title, 'version': version})

    def test_sfo_auto_selects_100_and_both_validation_paths_preserve_identity(self):
        with patch.object(validator.link_modules, 'module') as module:
            selected = validator.load_profile(game_dir=self.game, repo_root=self.repo)
            self.assertEqual(selected, self.profile)
            result = validator.validate_knack2_dump(self.game, repo_root=self.repo)
            self.assertEqual((result['title_id'], result['version']), ('CUSA08014', '01.00'))
            self.assertEqual(result['languages'], ['en-US'])
            self.assertEqual(run.validate_game(self.game, selected)['APP_VER'], '01.00')
            self.assertEqual(module.call_count, 6)

    def test_both_executable_hashes_and_modules_remain_mandatory(self):
        with patch.object(validator.link_modules, 'module'):
            for name in self.profile['executables']:
                path = self.game / name
                original = path.read_bytes()
                path.write_bytes(original + b'changed')
                with self.subTest(executable=name), self.assertRaises(validator.KnackHashMismatchError):
                    validator.validate_knack2_dump(self.game, repo_root=self.repo)
                with self.assertRaisesRegex(ValueError, 'SHA-256'):
                    run.validate_game(self.game, self.profile)
                path.write_bytes(original)
            (self.game / 'sce_module/libSceJobManager.prx').unlink()
            with self.assertRaises(validator.KnackModuleError):
                validator.validate_knack2_dump(self.game, repo_root=self.repo)

    def test_101_metadata_cannot_be_forced_through_100_profile(self):
        (self.game / 'sce_sys/param.sfo').write_bytes(sfo_bytes('CUSA08014', '01.01'))
        with self.assertRaises(validator.KnackVersionMismatchError):
            validator.validate_knack2_dump(self.game, profile=self.profile)

    def test_cache_is_separate_from_existing_101_images(self):
        out = self.root / 'cache'
        old_image = out / 'game_elf/boot-linked.bin'
        old_image.parent.mkdir(parents=True)
        old_image.write_bytes(b'keep existing 01.01 image')
        with patch.object(run.prepare, 'prepare'), patch.object(run.link_modules, 'link'), \
             patch.object(run.content_profile, 'prepare'):
            folder = run.prepare_image(self.game, self.profile, 'game.elf', out)
        self.assertEqual(folder, out / 'CUSA08014-01.00/game_elf')
        self.assertEqual(old_image.read_bytes(), b'keep existing 01.01 image')

    def test_launcher_persists_100_and_distinguishes_saved_101(self):
        from tests.test_knack2_validation_autoplay import ValidationAutoplayTests, HAS_QT
        if not HAS_QT:
            self.skipTest('PySide6 unavailable')
        ValidationAutoplayTests.setUpClass()
        harness = ValidationAutoplayTests()
        data_dir = self.root / 'launcher-data'
        data_dir.mkdir()
        from port.launcher.knack2 import config
        config.save_launcher_settings({'game_dir': str(self.game)},
                                      path=data_dir / 'launcher_settings.json')
        backend = harness.make_backend(data_dir, result=harness.result(version='01.00'))
        self.addCleanup(backend.shutdown)
        harness.validate(backend)
        saved = config.load_launcher_settings(data_dir / 'launcher_settings.json')['validation']
        self.assertEqual((saved['status'], saved['title_id'], saved['version']),
                         ('valid', 'CUSA08014', '01.00'))
        self.assertTrue(backend._matches_saved_validation(saved, harness.result(version='01.00')))
        self.assertFalse(backend._matches_saved_validation(saved, harness.result(version='01.01')))

    def test_100_profile_survives_loadexec_and_session_report(self):
        runtime = self.root / 'synthetic-runtime.exe'
        runtime.write_bytes(b'never executed')
        runtime.chmod(0o700)
        out = self.root / 'session-output'
        calls = []

        def child(command, env, log, seconds):
            calls.append(command)
            self.assertEqual(env['KNACK2_PROFILE'], 'CUSA08014-01.00')
            if len(calls) == 1:
                target = b'/app0/game.elf'
                Path(env['KNACK2_EXEC_REQUEST']).write_bytes(
                    b'K2EXEC01' + struct.pack('<I', len(target)) + target + struct.pack('<I', 0))
                return 75
            self.assertEqual(command[command.index('--guest-executable') + 1], '/app0/game.elf')
            return 0

        with patch.object(run, 'ROOT', self.repo), patch.object(run.link_modules, 'module'), \
             patch.object(run, 'prepare_image', side_effect=lambda g, p, name, output: output / name), \
             patch.object(run.subprocess, 'run'), patch.object(run, 'run_child', side_effect=child), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run.main(['--game-dir', str(self.game), '--out', str(out),
                                       '--runtime', str(runtime)]), 0)
        self.assertEqual(len(calls), 2)
        session = json.loads(next(out.glob('session-*/session.json')).read_text())
        self.assertEqual(session['profile'], 'CUSA08014-01.00')
        self.assertEqual([stage['exit_code'] for stage in session['stages']], [75, 0])
        self.assertFalse(session['visual_verified'])


if __name__ == '__main__':
    unittest.main()
