"""Regional profile selection and isolation; synthetic inputs only, no guest execution."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import validator
from port.launcher.knack2.languages import discover_languages, language_environment

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('regions_supervisor', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


def sfo_bytes(title, version='01.01'):
    keys, values, entries = bytearray(), bytearray(), bytearray()
    for key, value in {'TITLE_ID': title, 'APP_VER': version, 'TITLE': 'Synthetic Knack II'}.items():
        encoded = value.encode() + b'\0'
        entries.extend(struct.pack('<HHIII', len(keys), 0x204, len(encoded), len(encoded), len(values)))
        keys.extend(key.encode() + b'\0')
        values.extend(encoded)
    start = 20 + len(entries)
    return struct.pack('<4sIIII', b'\0PSF', 0x101, start, start + len(keys), 3) + entries + keys + values


class RegionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knack-regions-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'
        (self.repo / 'profiles').mkdir(parents=True)
        self.profiles = {}
        self.games = {}
        for title in ('CUSA08014', 'CUSA07670'):
            game = self.root / title
            (game / 'sce_sys').mkdir(parents=True)
            (game / 'sce_sys/param.sfo').write_bytes(sfo_bytes(title))
            profile = copy.deepcopy(validator.load_profile(ROOT / 'profiles' / f'{title}-01.01.json'))
            for name in profile['executables']:
                content = f'synthetic {title} {name}'.encode()
                (game / name).write_bytes(content)
                profile['executables'][name] = hashlib.sha256(content).hexdigest()
            (game / 'sce_module').mkdir()
            for name in ('libc.prx', 'libSceFios2.prx', 'libSceJobManager.prx'):
                (game / 'sce_module' / name).write_bytes(b'synthetic module')
            (self.repo / 'profiles' / f'{title}-01.01.json').write_text(json.dumps(profile))
            self.profiles[title], self.games[title] = profile, game

    def test_both_profiles_auto_select_and_validate(self):
        with patch.object(validator.link_modules, 'module') as module:
            for title, game in self.games.items():
                with self.subTest(title=title):
                    selected = validator.load_profile(game_dir=game, repo_root=self.repo)
                    self.assertEqual(selected, self.profiles[title])
                    result = validator.validate_knack2_dump(game, repo_root=self.repo)
                    self.assertTrue(result['valid'])
                    self.assertEqual(result['title_id'], title)
                    run.validate_game(game, selected)
            self.assertEqual(module.call_count, 12)

    def test_folder_name_never_selects_region(self):
        game = self.games['CUSA07670']
        (game / 'sce_sys/param.sfo').write_bytes(sfo_bytes('CUSA08014'))
        selected = validator.load_profile(game_dir=game, repo_root=self.repo)
        self.assertEqual(selected['title_id'], 'CUSA08014')
        with self.assertRaises(validator.KnackHashMismatchError):
            validator.validate_knack2_dump(game, repo_root=self.repo)

    def test_foreign_ids_and_versions_fail_before_profile_reads(self):
        game = self.games['CUSA07670']
        for title, version in [('CUSA07670', '01.00'), ('CUSA99999', '01.01'), ('../../outside', '01.01')]:
            (game / 'sce_sys/param.sfo').write_bytes(sfo_bytes(title, version))
            with self.subTest(title=title, version=version), \
                 patch.object(Path, 'read_text', side_effect=AssertionError('read unlisted profile')), \
                 self.assertRaises(validator.KnackVersionMismatchError):
                validator.load_profile(game_dir=game, repo_root=self.repo)

    def test_misnamed_profile_is_rejected(self):
        file = self.repo / 'profiles/CUSA07670-01.01.json'
        file.write_text(json.dumps(self.profiles['CUSA08014']))
        with self.assertRaises(validator.KnackProfileError):
            validator.load_profile(game_dir=self.games['CUSA07670'], repo_root=self.repo)

    def test_mixed_executable_cannot_pass_hash_validation(self):
        eu, us = self.games['CUSA07670'], self.games['CUSA08014']
        (eu / 'game.elf').write_bytes((us / 'game.elf').read_bytes())
        with self.assertRaises(validator.KnackHashMismatchError):
            validator.validate_knack2_dump(eu, repo_root=self.repo)
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            run.validate_game(eu, self.profiles['CUSA07670'])

    def test_explicit_wrong_profile_cannot_override_sfo(self):
        with self.assertRaises(validator.KnackVersionMismatchError):
            validator.validate_knack2_dump(self.games['CUSA07670'], profile=self.profiles['CUSA08014'])

    def test_invalid_european_module_still_fails(self):
        with patch.object(validator.link_modules, 'module', side_effect=ValueError('synthetic bad ELF')), \
             self.assertRaises(validator.KnackModuleError):
            validator.validate_knack2_dump(self.games['CUSA07670'], repo_root=self.repo)

    def test_actual_european_asset_names_and_system_language_ids(self):
        game = self.games['CUSA07670']
        expected = {'fre': ('fr-FR', 2), 'pte': ('pt-PT', 7), 'ene': ('en-GB', 18),
                    'ese': ('es-ES', 3), 'deu': ('de-DE', 4), 'ita': ('it-IT', 5),
                    'nld': ('nl-NL', 6), 'rus': ('ru-RU', 8), 'fin': ('fi-FI', 12),
                    'swe': ('sv-SE', 13), 'dan': ('da-DK', 14), 'nor': ('nb-NO', 15),
                    'pol': ('pl-PL', 16), 'ara': ('ar-SA', 21)}
        for asset, (code, value) in expected.items():
            directory = game / 'data/lang' / asset
            directory.mkdir(parents=True)
            (directory / 'synthetic.txt').write_text('synthetic')
            self.assertEqual(language_environment(code), {'TLG_LANGUAGE': str(value)})
        self.assertEqual(set(discover_languages(game)), {item[0] for item in expected.values()})
        self.assertNotIn('pt-BR', discover_languages(game))

    def test_runtime_profile_cannot_be_inherited_or_forged(self):
        with patch.dict(os.environ, {'KNACK2_PROFILE': 'foreign', 'TLG_LANGUAGE': '999'}):
            for name in validator.SUPPORTED_PROFILES:
                env = run.runtime_environment(self.root, profile=name, language='en-GB')
                self.assertEqual(env['KNACK2_PROFILE'], name)
                self.assertEqual(env['TLG_LANGUAGE'], '18')
        with self.assertRaises(ValueError):
            run.runtime_environment(self.root, profile='CUSA07670-01.00', capture_dir=self.root / 'unwritten')
        self.assertFalse((self.root / 'unwritten').exists())

    def test_analyze_passes_the_selected_profile_to_both_images(self):
        for title, game in self.games.items():
            selected = self.profiles[title]
            out = self.root / ('analysis-' + title)
            with patch.object(run, 'ROOT', self.repo), \
                 patch.object(run.link_modules, 'module'), patch.object(run, 'prepare_image') as prepare, \
                 patch.object(run, 'run_child', side_effect=AssertionError('guest executed')), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run.main(['--game-dir', str(game), '--out', str(out), '--analyze']), 0)
            self.assertEqual(prepare.call_count, 2)
            for call in prepare.call_args_list:
                self.assertEqual(call.args[1], selected)
            self.assertFalse((out / 'user').exists())

    def test_regional_profile_survives_loadexec_and_session_report(self):
        runtime = self.root / 'synthetic-runtime.exe'
        runtime.write_bytes(b'never executed')
        runtime.chmod(0o700)
        for title, game in self.games.items():
            out = self.root / ('session-' + title)
            calls = []

            def child(command, env, log, seconds):
                calls.append(command)
                self.assertEqual(env['KNACK2_PROFILE'], title + '-01.01')
                self.assertEqual(command[command.index('--user') + 1], str(out / 'user'))
                if len(calls) == 1:
                    target, argument = b'/app0/game.elf', b'preserved argument'
                    Path(env['KNACK2_EXEC_REQUEST']).write_bytes(
                        b'K2EXEC01' + struct.pack('<I', len(target)) + target +
                        struct.pack('<II', 1, len(argument)) + argument)
                    return 75
                self.assertEqual(command[command.index('--guest-executable') + 1], '/app0/game.elf')
                self.assertEqual(command[-2:], ['--guest-arg', 'preserved argument'])
                return 0

            with patch.object(run, 'ROOT', self.repo), patch.object(run.link_modules, 'module'), \
                 patch.object(run, 'prepare_image', side_effect=lambda g, p, name, output: output / name), \
                 patch.object(run.subprocess, 'run'), patch.object(run, 'run_child', side_effect=child), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run.main(['--game-dir', str(game), '--out', str(out),
                                           '--runtime', str(runtime)]), 0)
            report = json.loads(next(out.glob('session-*/session.json')).read_text())
            self.assertEqual(report['profile'], title + '-01.01')
            self.assertEqual([stage['exit_code'] for stage in report['stages']], [75, 0])
            self.assertFalse(report['visual_verified'])

    def test_prepared_images_do_not_replace_american_cache(self):
        out = self.root / 'cache'
        us = out / 'game_elf'
        us.mkdir(parents=True)
        sentinel = us / 'boot-linked.bin'
        sentinel.write_bytes(b'preserve American image')
        with patch.object(run.prepare, 'prepare'), patch.object(run.link_modules, 'link'), \
             patch.object(run.content_profile, 'prepare'):
            folder = run.prepare_image(self.games['CUSA07670'], self.profiles['CUSA07670'], 'game.elf', out)
        self.assertEqual(folder, out / 'CUSA07670-01.01/game_elf')
        self.assertEqual(sentinel.read_bytes(), b'preserve American image')


if __name__ == '__main__':
    unittest.main()
