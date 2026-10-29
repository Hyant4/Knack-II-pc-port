"""Synthetic regression tests; no game files are required."""
import importlib.util
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from port.launcher.knack2.identity import runtime_names

ROOT = Path(__file__).resolve().parents[1]
LOADEXEC_HARNESS = ROOT / 'out' / ('knack-loadexec-test.exe' if os.name == 'nt' else 'knack-loadexec-test')
spec = importlib.util.spec_from_file_location('knack_run', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)
LOAD_EXEC_TEST = Path(os.environ.get('KNACK2_TEST_BUILD_DIR', ROOT / 'out')) / (
    'knack-loadexec-test.exe' if os.name == 'nt' else 'knack-loadexec-test')


def request(target='/app0/game.elf', args=()):
    def string(value):
        b = value.encode('utf-8')
        return struct.pack('<I', len(b)) + b
    return b'K2EXEC01' + string(target) + struct.pack('<I', len(args)) + b''.join(map(string, args))


class KnackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knack test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = json.loads((ROOT / 'profiles/CUSA08014-01.01.json').read_text())

    def parse(self, data):
        file = self.root / 'request.bin'
        file.write_bytes(data)
        return run.read_exec_request(file, self.profile)

    def test_load_exec_preserves_arguments(self):
        values = ['game.elf', 'argument with spaces', '', 'ação', '--option']
        self.assertEqual(self.parse(request(args=values)), ('game.elf', values))

    def test_load_exec_rejects_every_truncation(self):
        data = request(args=['one', 'two'])
        for size in range(len(data)):
            with self.subTest(size=size), self.assertRaises((ValueError, struct.error)):
                self.parse(data[:size])

    def test_load_exec_rejects_unlisted_path_and_trailer(self):
        for path in ['/app0/../game.elf', '/app0/other.elf', 'C:/game.elf', '/savedata0/game.elf']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.parse(request(path))
        with self.assertRaises(ValueError):
            self.parse(request() + b'extra')

    def test_request_limits(self):
        for data in [request(args=['x'] * 64), request(args=['x' * 4097]), request(args=['x\0y'])]:
            with self.assertRaises(ValueError):
                self.parse(data)

    def test_executable_stays_inside_dump(self):
        self.assertEqual(run.prepare.executable_path(self.root, 'game.elf'), self.root / 'game.elf')
        for target in ['../escape.elf', str(self.root.parent / 'escape.elf')]:
            with self.assertRaises(ValueError):
                run.prepare.executable_path(self.root, target)

    def test_firmware_ngs2_is_linked_into_game_elf_only(self):
        self.assertIs(run.with_system_modules(self.profile, None), self.profile)
        sprx = self.root / 'firmware' / run.NGS2_MODULE
        linked = run.with_system_modules(self.profile, sprx)
        self.assertEqual(linked['modules']['game.elf'], self.profile['modules']['game.elf'] + [str(sprx)])
        self.assertEqual(linked['modules']['eboot.bin'], self.profile['modules']['eboot.bin'])
        self.assertNotIn(str(sprx), self.profile['modules']['game.elf'])  # the loaded profile is not mutated
        # The linker reads game / 'sce_module' / name: an absolute name must escape the dump.
        self.assertEqual(self.root / 'dump' / 'sce_module' / str(sprx), sprx)

    def test_firmware_ngs2_search_and_environment(self):
        folder = self.root / 'modules'
        folder.mkdir()
        with patch.dict(os.environ, {'KNACK2_SYSTEM_MODULES': str(folder)}), \
             patch.object(run, 'ROOT', self.root), patch.object(run.Path, 'home', return_value=self.root):
            self.assertIsNone(run.ngs2_module())
            (folder / run.NGS2_MODULE).write_bytes(b'module')
            for platform in ('linux', 'win32'):  # explicit opt-in, so no platform restriction
                with patch('sys.platform', platform), patch.object(run.link_modules, 'module') as parse:
                    self.assertEqual(run.ngs2_module(), (folder / run.NGS2_MODULE).resolve())
                parse.assert_called_once_with(folder / run.NGS2_MODULE)
        with patch.dict(os.environ, {'TLG_NGS2': 'native'}):
            self.assertNotIn('TLG_NGS2', run.runtime_environment(self.root))
        self.assertEqual(run.runtime_environment(self.root, ngs2=True)['TLG_NGS2'], 'native')

    def test_firmware_ngs2_is_opt_in(self):
        game = self.root / 'dump'
        base = ['--game-dir', str(game), '--out', str(self.root / 'out'), '--analyze']
        seen = []
        metadata = {'executable_layout': 'elf-80ccc228', 'game_elf_sha256': self.profile['executables']['game.elf']}
        with patch.object(run, 'load_profile', return_value=self.profile), \
             patch.object(run, 'validate_game', return_value=metadata), patch.object(run, 'ngs2_module', return_value=None) as search, \
             patch.object(run, 'prepare_image', side_effect=lambda game, profile, name, out: seen.append(profile)), \
             contextlib.redirect_stdout(io.StringIO()) as stdout:
            self.assertEqual(run.main(base), 0)  # default: no search, no link, no audio hint
            search.assert_not_called()
            self.assertNotIn('Audio:', stdout.getvalue())
            self.assertTrue(all(profile == self.profile for profile in seen))
            with contextlib.redirect_stderr(io.StringIO()) as stderr, self.assertRaises(SystemExit) as result:
                run.main(base + ['--firmware-ngs2'])  # requested but absent: fail before preparing
            self.assertEqual(result.exception.code, 2)
            self.assertIn(run.NGS2_MODULE, stderr.getvalue())
            self.assertEqual(len(seen), len(self.profile['executables']))
            sprx = self.root / run.NGS2_MODULE
            search.return_value = sprx
            seen.clear()
            self.assertEqual(run.main(base + ['--firmware-ngs2']), 0)
            self.assertIn(f'Audio: firmware NGS2 {sprx}', stdout.getvalue())
            self.assertTrue(all(profile['modules']['game.elf'][-1] == str(sprx) for profile in seen))

    def test_foreign_game_overrides_are_isolated(self):
        with patch.dict(os.environ, {'ISS_FORCE_PATCHES': '1', 'TLG_PATCHES': 'unsafe', 'GOW3_FPS': '240'}):
            env = run.runtime_environment(self.root)
        self.assertNotIn('ISS_FORCE_PATCHES', env)
        self.assertNotIn('TLG_PATCHES', env)
        self.assertNotIn('GOW3_FPS', env)
        self.assertEqual(env['KNACK2_PROFILE'], 'CUSA08014-01.01')

    def test_toggle_file_only_external_override_reaches_child(self):
        toggle_file = self.root / 'toggle mask with spaces.txt'
        toggle_file.write_text('68719476736\n')
        absolute_path = str(toggle_file.resolve())
        blocked = {'TLG_PATCHES': 'unsafe', 'TLG_DRAW_PIPE': 'override',
                   'TLG_PAD_FILE': 'unrelated.txt', 'ISS_FORCE_PATCHES': '1',
                   'GR2_CUSTOM': '1', 'GOW3_FPS': '240', 'BB_CUSTOM': '1'}
        with patch.dict(os.environ, {'TLG_TOGGLE_FILE': absolute_path, **blocked}):
            env = run.runtime_environment(self.root)
            self.assertEqual(env['TLG_TOGGLE_FILE'], absolute_path)
            for name in blocked:
                self.assertNotIn(name, env)
            self.assertEqual(env['TLG_UPSCALER'], 'off')

            log = self.root / 'toggle-child.log'
            code = ('import os; print(os.environ.get("TLG_TOGGLE_FILE", "")); '
                    'print(os.environ.get("TLG_PATCHES", "filtered"))')
            self.assertEqual(run.run_child([sys.executable, '-c', code], env, log, 5), 0)
            self.assertEqual(log.read_text().splitlines(), [absolute_path, 'filtered'])

            del os.environ['TLG_TOGGLE_FILE']
            self.assertNotIn('TLG_TOGGLE_FILE', run.runtime_environment(self.root))

    def test_empty_toolchain_variable_uses_default(self):
        with patch.object(run.sys, 'platform', 'win32'), patch.dict(os.environ, {'MSYS2_ROOT': ''}):
            env = run.runtime_environment(self.root)
        self.assertEqual(Path(env['PATH'].split(os.pathsep)[0]),
                         Path.home() / 'AppData/Local/knack2-toolchain/msys64/clang64/bin')

    def test_linux_environment_uses_native_path_and_bundled_font(self):
        with patch('sys.platform', 'linux'), patch.dict(os.environ, {'PATH': '/usr/bin', 'MSYS2_ROOT': '/unused', 'LOCALAPPDATA': str(self.root), 'XDG_DATA_HOME': str(self.root)}, clear=True):
            env = run.runtime_environment(self.root)
        self.assertEqual(env['PATH'], '/usr/bin')
        self.assertEqual(env['TLG_RED_ZONE_REWRITE'], '0')
        self.assertEqual(Path(env['KNACK2_SYSTEM_FONT']), run.PORT / 'gpu/third_party/fonts/DejaVuSans.ttf')

    def test_explicit_linux_system_font_is_preserved(self):
        with patch('sys.platform', 'linux'), patch.dict(os.environ, {'KNACK2_SYSTEM_FONT': '/custom/font.ttf'}):
            self.assertEqual(run.runtime_environment(self.root)['KNACK2_SYSTEM_FONT'], '/custom/font.ttf')

    def test_local_users_filter_and_decimal_contract(self):
        inherited = {'TLG_LOCAL_USERS': '2', 'TLG_LOCAL_P2_USER_ID': '99',
                     'TLG_GPU_USER_DIR': 'real-user', 'TLG_PAD_RECORD': 'real-recording'}
        with patch.dict(os.environ, inherited):
            env = run.runtime_environment(self.root)
            for key in inherited:
                self.assertNotIn(key, env)
            env = run.runtime_environment(self.root, local_users=2, second_user=4099)
            self.assertEqual(env['TLG_LOCAL_USERS'], '2')
            self.assertEqual(env['TLG_LOCAL_P2_USER_ID'], '4099')
            self.assertNotIn('TLG_GPU_USER_DIR', env)
            self.assertNotIn('TLG_PAD_RECORD', env)
        for value, expected in [('2', 2), ('0004099', 4099), ('2147483647', 2147483647)]:
            self.assertEqual(run.p2_user_id(value), expected)
        for users, second in [(1, 4099), (2, None), (2, True), (2, 1), (2, 2147483648)]:
            with self.subTest(users=users, second=second), self.assertRaises(ValueError):
                run.runtime_environment(self.root, local_users=users, second_user=second)

    def test_local_users_invalid_cli_has_no_preparation_or_writes(self):
        game = self.root / 'synthetic dump'
        out = self.root / 'unused'
        base = ['--game-dir', str(game), '--out', str(out)]
        parent_file = self.root / 'not-a-directory'
        parent_file.write_bytes(b'unchanged')
        invalid = [['--local-users', '2'], ['--p2-user-id', '4099'], ['--local-users', '3'],
                   ['--timeout', '-1'], ['--pad-file', str(self.root / 'absent')],
                   ['--out', str(parent_file / 'child')],
                   ['--capture-dir', str(parent_file / 'frames')]]
        for value in ['0', '1', '-1', '+2', '2.0', '2x', ' 2', '2 ', '٢', '2147483648', '9' * 5000]:
            invalid.append(['--local-users', '2', '--p2-user-id=' + value])
        with patch.object(run, 'validate_game') as validate, patch.object(run, 'prepare_image') as prepare, \
             patch.object(run, 'run_child') as child, patch.object(run.subprocess, 'run') as rewrite, \
             patch.object(Path, 'mkdir', side_effect=AssertionError('validation wrote a directory')), \
             patch.object(Path, 'read_text', side_effect=AssertionError('validation read profile before rejecting')):
            for options in invalid:
                with self.subTest(options=options[:2]), contextlib.redirect_stderr(io.StringIO()), \
                     self.assertRaises(SystemExit) as result:
                    run.main(base + options)
                self.assertEqual(result.exception.code, 2)
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                run.main(['--game-dir', str(game), '--local-users', '2', '--p2-user-id', '4099'])
            validate.assert_not_called()
            prepare.assert_not_called()
            child.assert_not_called()
            rewrite.assert_not_called()
        self.assertFalse(out.exists())

    def test_coop_output_must_be_fresh_and_contain_capture(self):
        used = self.root / 'used'
        used.mkdir()
        sentinel = used / 'save.dat'
        sentinel.write_bytes(b'P1 must survive')
        game = self.root / 'dump'
        base = ['--game-dir', str(game), '--local-users', '2', '--p2-user-id', '4099']
        outputs = [used, ROOT / 'out/knack2', ROOT / 'out/knack2/coop', run.PORT / 'user', game]
        cases = [['--out', str(path)] for path in outputs]
        cases.append(['--out', str(self.root / 'fresh'), '--capture-dir', str(self.root / 'external')])
        with patch.object(run, 'validate_game') as validate, patch.object(run, 'prepare_image') as prepare:
            for options in cases:
                with self.subTest(options=options), contextlib.redirect_stderr(io.StringIO()), \
                     self.assertRaises(SystemExit) as result:
                    run.main(base + options)
                self.assertEqual(result.exception.code, 2)
            validate.assert_not_called()
            prepare.assert_not_called()
        self.assertEqual(sentinel.read_bytes(), b'P1 must survive')
        self.assertFalse((self.root / 'fresh').exists())
        self.assertFalse((self.root / 'external').exists())

    def test_validated_users_survive_real_supervisor_loadexec_loop(self):
        for coop in (False, True):
            with self.subTest(coop=coop):
                out = self.root / ('coop' if coop else 'single')
                game = self.root / 'synthetic dump'
                port = self.root / 'synthetic port'
                (port / 'out').mkdir(parents=True, exist_ok=True)
                runtime = port / 'out' / runtime_names()[1]
                runtime.write_bytes(b'not an executable')
                runtime.chmod(0o700)
                stages = []

                def fake_child(command, env, log, seconds):
                    stages.append((command, dict(env)))
                    self.assertEqual(command[command.index('--user') + 1], str(out / 'user'))
                    self.assertTrue(Path(env['TLG_CONFIG']).is_relative_to(out))
                    self.assertTrue(Path(env['KNACK2_EXEC_REQUEST']).is_relative_to(out))
                    self.assertNotIn('TLG_GPU_USER_DIR', env)
                    if coop:
                        self.assertEqual(env['TLG_LOCAL_USERS'], '2')
                        self.assertEqual(env['TLG_LOCAL_P2_USER_ID'], '4099')
                        self.assertTrue(Path(env['TLG_DUMP_DIR']).is_relative_to(out))
                    else:
                        self.assertNotIn('TLG_LOCAL_USERS', env)
                        self.assertNotIn('TLG_LOCAL_P2_USER_ID', env)
                    if len(stages) == 1:
                        Path(env['KNACK2_EXEC_REQUEST']).write_bytes(request(args=['from title']))
                        return 75
                    self.assertEqual(command[command.index('--guest-executable') + 1], '/app0/game.elf')
                    self.assertIn('from title', command)
                    return 0

                options = ['--game-dir', str(game), '--out', str(out)]
                if coop:
                    options += ['--local-users', '2', '--p2-user-id', '0004099',
                                '--capture-dir', str(out / 'frames')]
                with patch.object(run, 'PORT', port), patch.object(run, 'validate_game', return_value={
                        'executable_layout': 'elf-80ccc228', 'game_elf_sha256': '0' * 64}), \
                     patch.object(run, 'load_profile', return_value=self.profile), \
                     patch.object(run, 'prepare_image', side_effect=lambda g, p, name, o: o / name), \
                     patch.object(run.subprocess, 'run') as rewrite, \
                     patch.object(run, 'run_child', side_effect=fake_child), \
                     patch.dict(os.environ, {'TLG_LOCAL_USERS': '2', 'TLG_LOCAL_P2_USER_ID': '999',
                                             'TLG_GPU_USER_DIR': 'real-user'}), \
                     contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(run.main(options), 0)
                self.assertEqual(len(stages), 2)
                self.assertEqual(rewrite.call_count, 2)
                for call in rewrite.call_args_list:
                    self.assertEqual('--red-zone' in call.args[0], sys.platform == 'win32')
                report = json.loads(next(out.glob('session-*/session.json')).read_text())
                self.assertEqual(report['local_users'], 2 if coop else 1)
                self.assertEqual(report['p2_user_id'], 4099 if coop else None)
                self.assertEqual(len(report['stages']), 2)

    def test_explicit_input_and_capture_paths(self):
        pad = self.root / 'pad input.txt'
        capture = self.root / 'frame captures'
        with self.assertRaises(ValueError):
            run.runtime_environment(self.root, pad_file=pad)
        pad.write_text('')
        with patch.dict(os.environ, {'TLG_PAD_FILE': 'unrelated.txt'}):
            self.assertNotIn('TLG_PAD_FILE', run.runtime_environment(self.root))
            env = run.runtime_environment(self.root, pad_file=pad, capture_dir=capture)
        self.assertEqual(env['TLG_PAD_FILE'], str(pad.resolve()))
        self.assertEqual(env['TLG_FRAME_DUMP_TRIGGER'], str(capture / 'capture.request'))
        self.assertTrue(capture.is_dir())

    def test_supervisor_timeout_and_child_exit(self):
        command = [sys.executable, '-c', 'import sys; print("stage"); sys.exit(75)']
        log = self.root / 'child.log'
        self.assertEqual(run.run_child(command, os.environ.copy(), log, 5), 75)
        self.assertIn(b'stage', log.read_bytes())
        command = [sys.executable, '-c', 'import time; time.sleep(20)']
        self.assertEqual(run.run_child(command, os.environ.copy(), log, 0.1), 124)

    @unittest.skipUnless(LOAD_EXEC_TEST.is_file(), 'native harness has not been built')
    def test_native_loadexec_roundtrip_and_rejection(self):
        exe = LOAD_EXEC_TEST
        path = self.root / 'request.bin'
        env = {**run.runtime_environment(self.root), 'KNACK2_EXEC_REQUEST': str(path)}
        values = ['game.elf', 'argument with spaces', '--option', '']
        result = subprocess.run([str(exe), '/app0/game.elf', *values], env=env, capture_output=True)
        self.assertEqual(result.returncode, 75, result.stderr)
        self.assertEqual(run.read_exec_request(path, self.profile), ('game.elf', values))
        path.unlink()
        result = subprocess.run([str(exe), '/app0/../escape.elf'], env=env, capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertFalse(path.exists())


if __name__ == '__main__':
    unittest.main()
