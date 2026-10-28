# SPDX-License-Identifier: GPL-2.0-or-later
"""GTX experiments use synthetic stages, private paths and no native runtime."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from port.launcher.knack2 import gtx1060_diagnostics as diagnostic
from port.launcher.knack2.runner import KnackRunner

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gtx1060_supervisor', ROOT / 'run.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


class Gtx1060DiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knack gtx diagnostic ')
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.ini = self.root / 'launch.ini'
        self.ini.write_bytes(b'# preserve\r\nasync_shaders=1\r\nshader_preload=1\r\n'
                             b'gpu_readbacks=2\r\neager_readback=0\r\n')
        self.runtime = self.root / 'native-never-executed'
        self.runtime.write_bytes(b'synthetic fixture')

    def test_normal_environment_and_command_have_no_experiment(self):
        env = supervisor.runtime_environment(self.root, config_file=self.ini)
        self.assertNotIn('TLG_GPU_USER_DIR', env)
        self.assertNotIn('TLG_ASYNC_SHADERS', env)
        self.assertNotIn('TLG_COMPUTE_ASYNC_BUILD', env)
        self.assertEqual(diagnostic.create_environment('off', self.root / 'absent'), {})
        self.assertFalse((self.root / 'absent').exists())
        command = KnackRunner(self.root, out_dir=self.root / 'out').build_command()
        self.assertNotIn('--gtx1060-diagnostic', command)

    def test_explicit_modes_create_distinct_caches_and_do_not_change_settings(self):
        original = self.ini.read_bytes()
        sessions = [self.root / 'cache session', self.root / 'sync session']
        for path in sessions:
            path.mkdir()
        cache = diagnostic.create_environment('cache', sessions[0])
        sync = diagnostic.create_environment('cache_sync', sessions[1])
        self.assertEqual(set(cache), {'TLG_GPU_USER_DIR'})
        self.assertEqual(sync['TLG_ASYNC_SHADERS'], '0')
        self.assertEqual(sync['TLG_COMPUTE_ASYNC_BUILD'], '0')
        for path, values in zip(sessions, (cache, sync)):
            gpu = Path(values['TLG_GPU_USER_DIR'])
            self.assertTrue(gpu.is_relative_to(path))
            self.assertEqual(list(gpu.iterdir()), [])
            with self.assertRaises(FileExistsError):
                diagnostic.create_environment('cache', path)
        self.assertNotEqual(cache['TLG_GPU_USER_DIR'], sync['TLG_GPU_USER_DIR'])
        self.assertEqual(self.ini.read_bytes(), original)
        command = KnackRunner(self.root, out_dir=self.root / 'out',
                              gtx1060_diagnostic='cache_sync').build_command()
        self.assertEqual(command[command.index('--gtx1060-diagnostic') + 1], 'cache_sync')

    def test_unknown_modes_and_unsafe_cli_fail_before_dump_or_output(self):
        for mode in ('invalid', None, [], '../user'):
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                diagnostic.validate_mode(mode)
        arguments = [
            ['--gtx1060-diagnostic', 'invalid'],
            ['--gtx1060-diagnostic', 'cache'],  # missing explicit output
            ['--gtx1060-diagnostic', 'cache', '--out', str(self.root / 'out'), '--analyze'],
            ['--gtx1060-diagnostic', 'cache', '--out', str(self.root / 'out'), '--check-imports'],
            ['--gtx1060-diagnostic', 'cache', '--out', str(self.root / 'dump/cache')],
        ]
        for extra in arguments:
            with self.subTest(extra=extra), patch.object(supervisor, 'load_profile') as load, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                supervisor.main(['--game-dir', str(self.root / 'dump'), *extra])
            load.assert_not_called()
        self.assertFalse((self.root / 'dump').exists())
        self.assertFalse((self.root / 'out').exists())

    def test_capture_has_no_cache_or_compilation_overrides_and_requires_new_runtime(self):
        session = self.root / 'capture-session'
        session.mkdir()
        env = diagnostic.create_environment('capture', session)
        self.assertEqual(env['TLG_KNACK_CAPTURE'], '1')
        self.assertEqual(env['TLG_FRAME_CAPTURE_KEY'], '1')
        self.assertEqual(Path(env['TLG_CAPTURE_DIR']).parent, session)
        for key in ('TLG_GPU_USER_DIR', 'TLG_ASYNC_SHADERS', 'TLG_COMPUTE_ASYNC_BUILD'):
            self.assertNotIn(key, env)
        with patch('subprocess.run', return_value=SimpleNamespace(returncode=0, stdout='CPU: complete\n')):
            with self.assertRaises(ValueError): diagnostic.require_capture_runtime(self.runtime)
        with patch('subprocess.run', return_value=SimpleNamespace(returncode=0, stdout=diagnostic.CAPTURE_MARKER+'\n')):
            diagnostic.require_capture_runtime(self.runtime)

    def test_linear_capture_changes_only_linear_readback(self):
        baseline = self.root / 'capture-baseline'
        linear = self.root / 'capture-linear'
        baseline.mkdir()
        linear.mkdir()
        plain = diagnostic.create_environment('capture', baseline)
        changed = diagnostic.create_environment('capture_linear', linear)
        self.assertEqual(plain['TLG_READBACK_LINEAR'], '0')
        self.assertEqual(changed['TLG_READBACK_LINEAR'], '1')
        self.assertEqual(set(plain) - {'TLG_READBACK_LINEAR'}, set(changed) - {'TLG_READBACK_LINEAR'})
        for values in (plain, changed):
            self.assertNotIn('TLG_GPU_USER_DIR', values)
            self.assertNotIn('TLG_ASYNC_SHADERS', values)
            self.assertNotIn('TLG_COMPUTE_ASYNC_BUILD', values)

    def test_old_capture_runtime_fails_before_preparing_or_writing_output(self):
        profile = {'title_id': 'CUSA08014', 'version': '01.01',
                   'executables': {'eboot.bin': {}, 'game.elf': {}}, 'entry': 'eboot.bin'}
        out = self.root / 'must-not-exist'
        with patch.object(supervisor, 'runtime_path', return_value=self.runtime), \
                patch.object(supervisor, 'load_profile', return_value=profile), \
                patch.object(supervisor, 'validate_game', return_value={
                    'executable_layout': 'elf-80ccc228', 'game_elf_sha256': '8' * 64}), \
                patch('subprocess.run', return_value=SimpleNamespace(returncode=0, stdout='CPU: complete\n')), \
                patch.object(supervisor, 'prepare_image') as prepare, self.assertRaises(ValueError):
            supervisor.main(['--game-dir', str(self.root / 'dump'), '--out', str(out),
                             '--gtx1060-diagnostic', 'capture'])
        prepare.assert_not_called()
        self.assertFalse(out.exists())

    def run_synthetic(self, mode, name, restart=False):
        out = self.root / name
        user = self.root / 'normal saves'
        user.mkdir(exist_ok=True)
        sentinel = user / 'cache-sentinel'
        sentinel.write_bytes(b'normal cache must survive')
        profile = {'title_id': 'CUSA08014', 'version': '01.01',
                   'executables': {'eboot.bin': {}, 'game.elf': {}}, 'entry': 'eboot.bin'}
        calls = []

        def prepare(game, profile, executable, output, **kwargs):
            folder = output / executable.replace('.', '_')
            folder.mkdir()
            return folder

        def child(command, env, log, timeout):
            calls.append((command, dict(env)))
            if '--cpu-only' in command:
                def string(value):
                    data = value.encode('utf-8')
                    return struct.pack('<I', len(data)) + data
                arguments = ['/app0/game.elf', '-w=10', '-cursoroff', '-region=a']
                token = (b'K2EXEC01' + string('/app0/game.elf') + struct.pack('<I', len(arguments))
                         + b''.join(string(value) for value in arguments))
                Path(env['KNACK2_EXEC_REQUEST']).write_bytes(token)
                return 75
            return 76 if restart and len(calls) == 2 else 0

        extra = [] if mode == 'off' else ['--gtx1060-diagnostic', mode]
        output = io.StringIO()
        with patch.object(supervisor, 'runtime_path', return_value=self.runtime), \
                patch.object(supervisor, 'load_profile', return_value=profile), \
                patch.object(supervisor, 'validate_game', return_value={
                    'executable_layout': 'elf-80ccc228', 'game_elf_sha256': '8' * 64}), \
                patch.object(supervisor, 'prepare_image', side_effect=prepare), \
                patch.object(supervisor.subprocess, 'run', return_value=SimpleNamespace(
                    returncode=0, stdout=diagnostic.CAPTURE_MARKER+'\n')), \
                patch.object(supervisor, 'run_child', side_effect=child), \
                patch.dict(os.environ, {'TLG_GPU_USER_DIR': str(user), 'TLG_ASYNC_SHADERS': '1'}), \
                contextlib.redirect_stdout(output):
            self.assertEqual(supervisor.main([
                '--game-dir', str(self.root / 'dump'), '--out', str(out),
                '--config', str(self.ini), '--user-data', str(user), *extra]), 0)
        report_file, = out.glob('session-*/session.json')
        report = json.loads(report_file.read_text(encoding='utf-8'))
        self.assertEqual(report['user_data'], str(user))
        self.assertEqual(sentinel.read_bytes(), b'normal cache must survive')
        for command, env in calls:
            self.assertEqual(command[command.index('--user') + 1], str(user))
        self.assertEqual(calls[1][0][-8:], ['--guest-arg', '/app0/game.elf', '--guest-arg', '-w=10',
                                            '--guest-arg', '-cursoroff', '--guest-arg', '-region=a'])
        return report_file.parent, report, calls, output.getvalue()

    def test_environment_and_metadata_follow_both_loadexec_stages_only_when_selected(self):
        original = self.ini.read_bytes()
        for mode in diagnostic.MODES:
            with self.subTest(mode=mode):
                session, report, calls, output = self.run_synthetic(mode, mode)
                self.assertEqual(len(calls), 2)
                self.assertEqual(report['profile'], 'CUSA08014-01.01')
                if mode == 'off':
                    self.assertNotIn('gtx1060_diagnostic', report)
                    for command, env in calls:
                        self.assertNotIn('TLG_GPU_USER_DIR', env)
                        self.assertNotIn('TLG_ASYNC_SHADERS', env)
                else:
                    evidence = report['gtx1060_diagnostic']
                    self.assertEqual(evidence['mode'], mode)
                    self.assertEqual(evidence['fresh_gpu_cache'], mode not in ('capture', 'capture_linear'))
                    if mode in ('capture', 'capture_linear'):
                        self.assertEqual(report['gtx1060_diagnostic']['capture_limits']['images'], 13)
                        self.assertEqual(report['gtx1060_diagnostic']['capture_limits']['bytes'], 128 * 1024 * 1024)
                        self.assertEqual(report['gtx1060_diagnostic']['capture_limits']['frames'], 1)
                    for command, env in calls:
                        self.assertEqual({k: env[k] for k in evidence['environment']}, evidence['environment'])
                        directory = env['TLG_CAPTURE_DIR'] if mode in ('capture', 'capture_linear') else env['TLG_GPU_USER_DIR']
                        self.assertEqual(Path(directory).parent, session)
                        if mode in ('cache', 'capture', 'capture_linear'):
                            self.assertNotIn('TLG_ASYNC_SHADERS', env)
                            self.assertNotIn('TLG_COMPUTE_ASYNC_BUILD', env)
                    self.assertIn('GTX 1060 diagnostic: ' + mode, output)
                self.assertEqual(self.ini.read_bytes(), original)

    def test_linux_restart_branch_keeps_diagnostic_environment(self):
        # Exercise branch 76 with mocked children, including on a Windows test host.
        with patch.object(supervisor.sys, 'platform', 'linux'), \
                patch.object(supervisor.os, 'access', return_value=True):
            _, _, calls, _ = self.run_synthetic('cache_sync', 'restart', restart=True)
        self.assertEqual(len(calls), 3)
        self.assertEqual(calls[1][1]['TLG_GPU_USER_DIR'], calls[2][1]['TLG_GPU_USER_DIR'])
        self.assertEqual(calls[2][1]['TLG_COMPUTE_ASYNC_BUILD'], '0')


if __name__ == '__main__':
    unittest.main()
