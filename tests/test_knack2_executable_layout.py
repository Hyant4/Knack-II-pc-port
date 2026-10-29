"""Exact executable variants preserve regional identity; all sessions are synthetic."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import validator

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('layout_supervisor', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)

region_spec = importlib.util.spec_from_file_location('layout_regions', ROOT / 'tests/test_knack2_regions.py')
regions = importlib.util.module_from_spec(region_spec)
region_spec.loader.exec_module(regions)


class ExecutableLayoutTests(unittest.TestCase):
    def setUp(self):
        regions.RegionTests.setUp(self)

    def test_alternate_hash_keeps_US_identity_and_selects_shared_layout(self):
        game = self.games['CUSA08014']
        alternate = self.profiles['CUSA07670']['executables']['game.elf']
        profile_file = self.repo / 'profiles/CUSA08014-01.01.json'
        profile = self.profiles['CUSA08014']
        profile['game_elf_variants'] = [{'sha256': alternate, 'layout': 'elf-0b7843d3'}]
        profile_file.write_text(json.dumps(profile))
        (game / 'game.elf').write_bytes((self.games['CUSA07670'] / 'game.elf').read_bytes())
        with patch.object(validator.link_modules, 'module'):
            result = validator.validate_knack2_dump(game, repo_root=self.repo)
            supervisor_result = run.validate_game(game, profile)
        self.assertEqual((result['title_id'], result['version']), ('CUSA08014', '01.01'))
        self.assertEqual(result['executable_layout'], 'elf-0b7843d3')
        self.assertEqual(result['game_elf_sha256'], alternate)
        self.assertEqual(supervisor_result['executable_layout'], result['executable_layout'])
        (game / 'game.elf').write_bytes(b'unknown variant')
        with self.assertRaises(validator.KnackHashMismatchError):
            validator.validate_knack2_dump(game, repo_root=self.repo)
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            run.validate_game(game, profile)

    def test_variant_schema_rejects_unknown_layout_duplicate_and_wrong_version(self):
        profile = copy.deepcopy(self.profiles['CUSA08014'])
        primary = profile['executables']['game.elf']
        variant = {'sha256': 'a' * 64, 'layout': 'elf-0b7843d3'}
        for variants in (None, {}, [None], [{'sha256': 'bad', 'layout': 'elf-0b7843d3'}],
                         [{**variant, 'layout': 'unknown'}], [{**variant, 'extra': 'ignored'}],
                         [{**variant, 'sha256': primary}], [variant, variant]):
            with self.subTest(variants=variants), self.assertRaises(validator.KnackProfileError):
                validator.validate_profile_schema({**profile, 'game_elf_variants': variants})
        with self.assertRaises(validator.KnackProfileError):
            validator.validate_profile_schema({**profile, 'version': '01.00', 'game_elf_variants': [variant]})
        self.assertEqual(validator.validate_profile_schema(profile), profile)

    def test_environment_cannot_inherit_or_inject_unknown_layout(self):
        with patch.dict(os.environ, {'KNACK2_EXECUTABLE_LAYOUT': 'forged'}):
            default = run.runtime_environment(self.root)
            variant = run.runtime_environment(self.root, executable_layout='elf-0b7843d3')
        self.assertEqual(default['KNACK2_EXECUTABLE_LAYOUT'], 'elf-80ccc228')
        self.assertEqual(variant['KNACK2_PROFILE'], 'CUSA08014-01.01')
        self.assertEqual(variant['KNACK2_EXECUTABLE_LAYOUT'], 'elf-0b7843d3')
        for profile, layout in [('CUSA08014-01.00', 'elf-0b7843d3'),
                                ('CUSA07670-01.01', 'elf-80ccc228'), ('CUSA08014-01.01', '../bad')]:
            with self.subTest(profile=profile), self.assertRaises(ValueError):
                run.runtime_environment(self.root, profile=profile, executable_layout=layout,
                                        capture_dir=self.root / 'must-not-exist')
        self.assertFalse((self.root / 'must-not-exist').exists())

    def test_alternate_cache_preserves_canonical_images(self):
        out = self.root / 'cache'
        sentinel = out / 'game_elf/boot-linked.bin'
        sentinel.parent.mkdir(parents=True)
        sentinel.write_bytes(b'canonical cache')
        with patch.object(run.prepare, 'prepare'), patch.object(run.link_modules, 'link'), \
             patch.object(run.content_profile, 'prepare'):
            folder = run.prepare_image(self.games['CUSA08014'], self.profiles['CUSA08014'], 'game.elf', out,
                                       executable_layout='elf-0b7843d3')
        self.assertEqual(folder, out / 'CUSA08014-01.01/elf-0b7843d3/game_elf')
        self.assertEqual(sentinel.read_bytes(), b'canonical cache')

    def test_old_runtime_cannot_silently_ignore_variant_identity(self):
        for output, code in [('CPU: complete\n', 0),
                             ('KNACK2_EXECUTABLE_LAYOUTS_V1=elf-80ccc228\n', 0),
                             ('KNACK2_EXECUTABLE_LAYOUTS_V1=elf-80ccc228,elf-0b7843d3\n', 1)]:
            with self.subTest(output=output), patch.object(run.subprocess, 'run', return_value=
                    subprocess.CompletedProcess([], code, stdout=output)), self.assertRaises(ValueError):
                run.require_runtime_layout(self.root / 'never-executed.exe', 'elf-0b7843d3')
        with patch.object(run.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0,
                stdout='CPU: complete\nKNACK2_EXECUTABLE_LAYOUTS_V1=elf-80ccc228,elf-0b7843d3\n')):
            run.require_runtime_layout(self.root / 'never-executed.exe', 'elf-0b7843d3')

    def test_variant_survives_loadexec_with_US_arguments_and_save_root(self):
        game = self.games['CUSA08014']
        profile = self.profiles['CUSA08014']
        alternate = self.profiles['CUSA07670']['executables']['game.elf']
        profile['game_elf_variants'] = [{'sha256': alternate, 'layout': 'elf-0b7843d3'}]
        (self.repo / 'profiles/CUSA08014-01.01.json').write_text(json.dumps(profile))
        (game / 'game.elf').write_bytes((self.games['CUSA07670'] / 'game.elf').read_bytes())
        runtime = self.root / 'synthetic-runtime.exe'
        runtime.write_bytes(b'never executed')
        runtime.chmod(0o700)
        calls = []
        out = self.root / 'session'

        def child(command, env, log, seconds):
            calls.append(command)
            self.assertEqual(env['KNACK2_PROFILE'], 'CUSA08014-01.01')
            self.assertEqual(env['KNACK2_EXECUTABLE_LAYOUT'], 'elf-0b7843d3')
            self.assertEqual(command[command.index('--user') + 1], str(out / 'user'))
            if len(calls) == 1:
                target = b'/app0/game.elf'
                arguments = [target, b'-region=a']
                Path(env['KNACK2_EXEC_REQUEST']).write_bytes(b'K2EXEC01' + struct.pack('<I', len(target)) + target +
                    struct.pack('<I', len(arguments)) + b''.join(struct.pack('<I', len(arg)) + arg for arg in arguments))
                return 75
            self.assertEqual(command[-2:], ['--guest-arg', '-region=a'])
            return 0

        with patch.object(run, 'ROOT', self.repo), patch.object(run.link_modules, 'module'), \
             patch.object(run.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, stdout='CPU: complete\n')), \
             patch.object(run, 'prepare_image') as prepare, patch.object(run, 'run_child') as execute:
            with self.assertRaisesRegex(ValueError, 'Runtime instalado'):
                run.main(['--game-dir', str(game), '--out', str(out), '--runtime', str(runtime)])
            prepare.assert_not_called()
            execute.assert_not_called()
            self.assertFalse(out.exists())

        with patch.object(run, 'ROOT', self.repo), patch.object(run.link_modules, 'module'), \
             patch.object(run, 'prepare_image', side_effect=lambda g, p, n, o, **kw: o / kw['executable_layout'] / n), \
             patch.object(run.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0,
                 stdout='KNACK2_EXECUTABLE_LAYOUTS_V1=elf-80ccc228,elf-0b7843d3\n')), \
             patch.object(run, 'run_child', side_effect=child):
            self.assertEqual(run.main(['--game-dir', str(game), '--out', str(out), '--runtime', str(runtime)]), 0)
        report = json.loads(next(out.glob('session-*/session.json')).read_text())
        self.assertEqual(report['profile'], 'CUSA08014-01.01')
        self.assertEqual(report['executable_layout'], 'elf-0b7843d3')
        self.assertEqual(report['game_elf_sha256'], alternate)
        self.assertEqual(report['stages'][1]['arguments'][-1], '-region=a')


if __name__ == '__main__':
    unittest.main()
