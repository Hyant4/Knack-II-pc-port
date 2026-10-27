"""Host portability contracts with synthetic stages; no game or real save data."""
import contextlib
import importlib.util
import io
import itertools
import json
import os
from pathlib import Path
import struct
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from port.launcher.knack2 import runner as runner_module

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('linux_supervisor', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


class LinuxSupervisorTests(unittest.TestCase):
    def test_linux_environment_keeps_host_path_and_disables_windows_red_zone(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(run.sys, 'platform', 'linux'), \
                patch.dict(os.environ, {'PATH': '/usr/bin:/bin', 'MSYS2_ROOT': '/unused/msys',
                                        'TLG_RED_ZONE_REWRITE': '1', 'TLG_PATCHES': 'foreign'}):
            env = run.runtime_environment(Path(temporary))
        self.assertEqual(env['PATH'], '/usr/bin:/bin')
        self.assertEqual(env['TLG_RED_ZONE_REWRITE'], '0')
        self.assertNotIn('TLG_PATCHES', env)
        self.assertEqual(env['KNACK2_PROFILE'], 'CUSA08014-01.01')

    def exercise(self, *, restart=False, timeout=0, expire=False, codes=None):
        with tempfile.TemporaryDirectory(prefix='knack linux ') as temporary:
            root = Path(temporary)
            runtime = root / 'native build/tlg-probe'
            runtime.parent.mkdir()
            runtime.write_bytes(b'synthetic file; never executed')
            runtime.chmod(0o700)
            config = root / 'knack2.ini'
            config.write_text('gpu_readbacks=2\neager_readback=0\n')
            user = root / 'persistent/user'
            user.mkdir(parents=True)
            sentinel = user / 'save.sentinel'
            sentinel.write_bytes(b'preserved synthetic save')
            output = root / 'new sessions'
            calls = []
            stages = iter(codes if codes is not None else ([75, 76, 0] if restart else [75, 0]))

            def child(command, env, log, seconds):
                calls.append((command, dict(env), seconds))
                self.assertEqual(command[0], str(runtime))
                self.assertEqual(command[command.index('--user') + 1], str(user))
                self.assertEqual(env['TLG_RED_ZONE_REWRITE'], '0')
                self.assertEqual(env['TLG_LOCAL_P2_USER_ID'], '4099')
                self.assertEqual(sentinel.read_bytes(), b'preserved synthetic save')
                with self.assertRaises(ValueError):
                    with run.user_data_lock(user):
                        self.fail('save lock released between stages')
                code = next(stages)
                if code == 75:
                    target, argument = b'/app0/game.elf', b'argument with spaces'
                    Path(env['KNACK2_EXEC_REQUEST']).write_bytes(
                        b'K2EXEC01' + struct.pack('<I', len(target)) + target +
                        struct.pack('<II', 1, len(argument)) + argument)
                if code == 76:
                    config.write_text('gpu_readbacks=1\neager_readback=0\n')
                return code

            clock = iter([100, 110, 120, 130, 170]) if expire else itertools.repeat(100)
            with patch.object(run.sys, 'platform', 'linux'), \
                    patch.object(run, 'validate_game', return_value={
                        'executable_layout': 'elf-80ccc228', 'game_elf_sha256': '0' * 64}), \
                    patch.object(run, 'load_profile', return_value=run.load_profile()), \
                    patch.object(run, 'prepare_image', side_effect=lambda g, p, name, o: o / name), \
                    patch.object(run.subprocess, 'run') as rewrite, \
                    patch.object(run, 'run_child', side_effect=child), \
                    patch.object(run.time, 'monotonic', side_effect=clock), \
                    contextlib.redirect_stdout(io.StringIO()):
                options = ['--game-dir', str(root / 'dump'), '--out', str(output),
                           '--runtime', str(runtime), '--config', str(config),
                           '--user-data', str(user), '--local-users', '2', '--p2-user-id', '4099',
                           '--timeout', str(timeout)]
                if codes is not None:
                    with self.assertRaisesRegex(ValueError, 'transition limit'):
                        run.main(options)
                else:
                    self.assertEqual(run.main(options), 124 if expire else 0)
            for call in rewrite.call_args_list:
                self.assertNotIn('--red-zone', call.args[0])
            report = json.loads(next(output.glob('session-*/session.json')).read_text())
            self.assertEqual(len(report['stages']), len(calls))
            with run.user_data_lock(user):
                pass
            return calls

    def test_linux_loadexec_uses_explicit_isolated_runtime(self):
        calls = self.exercise()
        self.assertEqual(len(calls), 2)
        self.assertIn('--cpu-only', calls[0][0])
        self.assertNotIn('--cpu-only', calls[1][0])
        self.assertIn('argument with spaces', calls[1][0])

    def test_restart_reloads_config_and_preserves_current_executable_and_arguments(self):
        calls = self.exercise(restart=True)
        self.assertEqual(len(calls), 3)
        self.assertEqual(calls[1][0], calls[2][0])
        self.assertEqual(calls[1][1]['TLG_READBACKS'], '2')
        self.assertEqual(calls[2][1]['TLG_READBACKS'], '1')
        self.assertTrue(all(seconds is None for _, _, seconds in calls))

    def test_restart_keeps_shared_positive_deadline(self):
        calls = self.exercise(restart=True, timeout=60, expire=True)
        self.assertEqual(len(calls), 2)
        self.assertEqual([c[c.index('--timeout') + 1] for c, _, _ in calls], ['50', '30'])

    def test_repeated_restarts_are_bounded_and_reported(self):
        self.assertEqual(len(self.exercise(codes=[76] * 8)), 8)

    def test_missing_runtime_fails_before_dump_preparation_or_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / 'never created'
            with patch.object(run.sys, 'platform', 'linux'), \
                    patch.object(run, 'validate_game') as validate, \
                    patch.object(run, 'prepare_image') as prepare, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
                run.main(['--game-dir', str(root / 'dump'), '--out', str(output),
                          '--runtime', str(root / 'missing')])
            self.assertEqual(result.exception.code, 2)
            validate.assert_not_called()
            prepare.assert_not_called()
            self.assertFalse(output.exists())

    def test_nonexecutable_runtime_fails_before_reading_dump(self):
        with tempfile.TemporaryDirectory() as temporary:
            runtime = Path(temporary) / 'tlg-probe'
            runtime.write_bytes(b'not executable')
            with patch.object(run.sys, 'platform', 'linux'), \
                    patch.object(run.os, 'access', return_value=False), \
                    patch.object(run, 'validate_game') as validate, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as result:
                run.main(['--game-dir', str(Path(temporary) / 'dump'), '--runtime', str(runtime)])
            self.assertEqual(result.exception.code, 2)
            validate.assert_not_called()


class LinuxProcessTests(unittest.TestCase):
    @unittest.skipUnless(hasattr(os, 'killpg'), 'Requires real POSIX process groups')
    def test_forced_stop_ends_synthetic_supervisor_and_descendant(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            script = root / 'synthetic_supervisor.py'
            pidfile = root / 'child.pid'
            script.write_text(
                'import os, signal, subprocess, sys, time\n'
                'from pathlib import Path\n'
                'signal.signal(signal.SIGINT, signal.SIG_IGN)\n'
                'child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])\n'
                'Path(os.environ["KNACK_SYNTHETIC_PID"]).write_text(str(child.pid))\n'
                'print("synthetic child ready", flush=True)\n'
                'time.sleep(60)\n', encoding='utf-8')
            runner = runner_module.KnackRunner(game_dir=root, run_py_path=script, out_dir=root / 'out')
            descendant = None
            try:
                runner.start(env={**os.environ, 'KNACK_SYNTHETIC_PID': str(pidfile)})
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    if pidfile.is_file():
                        value = pidfile.read_text().strip()
                        if value.isdecimal():
                            descendant = int(value)
                            break
                    time.sleep(.02)
                self.assertIsNotNone(descendant, 'synthetic supervisor did not start its child')
                self.assertEqual(os.getpgid(descendant), runner.pid)
                self.assertTrue(runner.stop(grace_period=.1, kill_wait=2))
                status = Path(f'/proc/{descendant}/status')
                # A killed grandchild can briefly await reaping by init as a zombie.
                if status.is_file():
                    try:
                        state = next(line for line in status.read_text().splitlines() if line.startswith('State:'))
                    except FileNotFoundError:
                        state = 'State: Z'
                    self.assertIn('Z', state, 'descendant still executes after forced stop')
            finally:
                runner.close()
                if descendant is not None:
                    try:
                        os.kill(descendant, 9)
                    except ProcessLookupError:
                        pass

    def test_runner_starts_an_owned_posix_session(self):
        process = Mock(pid=2468)
        with tempfile.TemporaryDirectory() as temporary:
            runner = runner_module.KnackRunner(game_dir=temporary, out_dir=Path(temporary) / 'out')
            with patch.object(runner_module, 'os', SimpleNamespace(name='posix')), \
                    patch.object(runner_module.subprocess, 'Popen', return_value=process) as spawn, \
                    patch.object(runner_module.threading, 'Thread'):
                runner.start()
            self.assertTrue(spawn.call_args.kwargs['start_new_session'])
            self.assertEqual(runner._posix_group_pid, process.pid)

    def test_escalation_kills_owned_group_including_guest(self):
        process = Mock(pid=2468)
        group_kill = Mock()
        process.poll.side_effect = lambda: -9 if group_kill.called else None
        with tempfile.TemporaryDirectory() as temporary:
            runner = runner_module.KnackRunner(game_dir=temporary, out_dir=Path(temporary) / 'out')
            runner._process = process
            runner._posix_group_pid = process.pid
            runner._state = runner_module.RunnerState.RUNNING
            with patch.object(runner_module, 'os', SimpleNamespace(name='posix', killpg=group_kill)), \
                    patch.object(runner_module.signal, 'SIGKILL', 9, create=True):
                self.assertTrue(runner.stop(grace_period=0, kill_wait=.2))
            group_kill.assert_called_once_with(process.pid, 9)
            process.kill.assert_not_called()

    def test_unowned_process_never_targets_callers_group(self):
        process = Mock(pid=2468)
        group_kill = Mock()
        process.poll.side_effect = lambda: -9 if process.kill.called else None
        with tempfile.TemporaryDirectory() as temporary:
            runner = runner_module.KnackRunner(game_dir=temporary, out_dir=Path(temporary) / 'out')
            runner._process = process
            runner._state = runner_module.RunnerState.RUNNING
            with patch.object(runner_module, 'os', SimpleNamespace(name='posix', killpg=group_kill)):
                self.assertTrue(runner.stop(grace_period=0, kill_wait=.2))
            process.kill.assert_called_once_with()
            group_kill.assert_not_called()


if __name__ == '__main__':
    unittest.main()
