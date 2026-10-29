# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic unit tests for Knack II Process Runner and Session Supervisor (Feature Track F2 / F2.1).

Guarantees 100% synthetic isolation:
- No real game execution.
- No Vulkan initialization.
- No tlg-probe.exe invocation.
- Isolated temporary directories and synthetic Python subprocesses only.
"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import venv
from unittest.mock import MagicMock, patch

# Ensure port/launcher is importable
_ROOT = Path(__file__).resolve().parents[1]
_LAUNCHER_DIR = _ROOT / 'port' / 'launcher'
for directory in (str(_ROOT), str(_LAUNCHER_DIR)):
    if directory not in sys.path:
        sys.path.insert(0, directory)

from knack2.runner import (
    KnackRunner,
    KnackRunnerConfigError,
    KnackRunnerError,
    KnackRunnerStateError,
    KnackRunnerTerminationError,
    RunnerState,
)


class TestKnack2Runner(unittest.TestCase):
    """Synthetic unit tests covering the Knack II launcher runner and supervisor."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix='knack2_runner_test_')
        self.addCleanup(self.temp_dir.cleanup)
        self.test_root = Path(self.temp_dir.name)

        # Create dummy game directory and run.py
        self.game_dir = self.test_root / 'game_dump'
        self.game_dir.mkdir(parents=True)

        self.out_dir = self.test_root / 'out'
        self.out_dir.mkdir(parents=True)

        self.mock_run_py = self.test_root / 'run.py'
        self.mock_run_py.write_text('# Dummy run.py\nimport sys\nsys.exit(0)\n', encoding='utf-8')

    def test_supervisor_keeps_virtualenv_dependencies(self) -> None:
        environment = self.test_root / 'python environment'
        venv.EnvBuilder(with_pip=False, symlinks=os.name != 'nt').create(environment)
        python = environment / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')
        site = Path(subprocess.check_output([str(python), '-c',
                    'import sysconfig; print(sysconfig.get_path("purelib"))'], text=True).strip())
        (site / 'launcher_dependency_fixture.py').write_text('VALUE = "venv dependency found"\n')
        self.mock_run_py.write_text('import launcher_dependency_fixture as fixture; print(fixture.VALUE)\n')
        for explicit in (False, True):
            with self.subTest(explicit=explicit), patch('sys.executable', str(python)):
                runner = KnackRunner(self.game_dir, run_py_path=self.mock_run_py,
                                     python_exe=python if explicit else None)
                child = subprocess.run(runner.build_command(), capture_output=True, text=True, timeout=10)
                self.assertEqual(child.returncode, 0, child.stderr)
                self.assertIn('venv dependency found', child.stdout)

    def test_command_construction_defaults_and_options(self) -> None:
        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            timeout=120,
            out_dir=self.out_dir,
        )
        cmd = runner.build_command()
        self.assertEqual(cmd[0], os.path.abspath(sys.executable))
        self.assertEqual(cmd[1], str(self.mock_run_py.resolve()))
        self.assertIn('--game-dir', cmd)
        self.assertEqual(cmd[cmd.index('--game-dir') + 1], str(self.game_dir.resolve()))
        self.assertIn('--timeout', cmd)
        self.assertEqual(cmd[cmd.index('--timeout') + 1], '120')
        self.assertIn('--out', cmd)
        self.assertEqual(cmd[cmd.index('--out') + 1], str(self.out_dir.resolve()))

        # Test optional flags
        pad = self.test_root / 'pad.bin'
        pad.write_bytes(b'PAD')
        cap = self.test_root / 'captures'
        cap.mkdir()

        runner_opts = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            pad_file=pad,
            capture_dir=cap,
            analyze_only=True,
            out_dir=self.out_dir,
        )
        cmd_opts = runner_opts.build_command()
        self.assertIn('--pad-file', cmd_opts)
        self.assertIn('--capture-dir', cmd_opts)
        self.assertIn('--analyze', cmd_opts)
        self.assertNotIn('--firmware-ngs2', cmd_opts)
        self.assertIn('--firmware-ngs2', KnackRunner(game_dir=self.game_dir, run_py_path=self.mock_run_py,
                                                     out_dir=self.out_dir, firmware_ngs2=True).build_command())

        # Mutually exclusive flags
        with self.assertRaises(KnackRunnerConfigError):
            KnackRunner(
                game_dir=self.game_dir,
                run_py_path=self.mock_run_py,
                analyze_only=True,
                check_imports=True,
                out_dir=self.out_dir,
            )

    def test_invalid_timeout(self) -> None:
        for bad_timeout in (-1, -100, 0.5, True):
            with self.assertRaises(KnackRunnerConfigError):
                KnackRunner(
                    game_dir=self.game_dir,
                    run_py_path=self.mock_run_py,
                    timeout=bad_timeout,
                    out_dir=self.out_dir,
                )

    def test_missing_game_dir_or_supervisor(self) -> None:
        with self.assertRaises(KnackRunnerConfigError):
            KnackRunner(game_dir='', run_py_path=self.mock_run_py)

        with self.assertRaises(KnackRunnerConfigError):
            KnackRunner(game_dir=self.game_dir, run_py_path=self.test_root / 'nonexistent_run.py')

    def test_directory_with_spaces_and_unicode(self) -> None:
        unicode_game_dir = self.test_root / 'Knack™ II Edição Especial'
        unicode_game_dir.mkdir(parents=True)
        unicode_out_dir = self.test_root / 'Knack II Saída'
        unicode_out_dir.mkdir(parents=True)

        runner = KnackRunner(
            game_dir=unicode_game_dir,
            run_py_path=self.mock_run_py,
            out_dir=unicode_out_dir,
        )
        cmd = runner.build_command()
        self.assertEqual(cmd[cmd.index('--game-dir') + 1], str(unicode_game_dir.resolve()))
        self.assertEqual(cmd[cmd.index('--out') + 1], str(unicode_out_dir.resolve()))

    def test_subprocess_spawn_failure(self) -> None:
        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        with patch('subprocess.Popen', side_effect=OSError('Access denied mock')):
            with self.assertRaises(KnackRunnerError):
                runner.start()
        self.assertEqual(runner.state, RunnerState.FAILED)
        self.assertIn('Access denied mock', runner.error_message)

    def test_state_transitions_normal_flow(self) -> None:
        recorded_states: list[RunnerState] = []

        # Synthetic script that exits immediately with 0
        script = self.test_root / 'quick_exit.py'
        script.write_text('import sys\nsys.exit(0)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
            on_state_change=lambda s: recorded_states.append(s),
        )
        self.assertEqual(runner.state, RunnerState.IDLE)
        runner.start()

        # Wait for completion
        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        self.assertEqual(runner.state, RunnerState.FINISHED)
        self.assertEqual(runner.exit_code, 0)
        self.assertIn(RunnerState.STARTING, recorded_states)
        self.assertIn(RunnerState.RUNNING, recorded_states)
        self.assertIn(RunnerState.FINISHED, recorded_states)

    def test_state_transitions_failure_flow(self) -> None:
        script = self.test_root / 'fail_exit.py'
        script.write_text('import sys\nsys.exit(42)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
        )
        runner.start()

        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        self.assertEqual(runner.state, RunnerState.FAILED)
        self.assertEqual(runner.exit_code, 42)
        self.assertIn('42', runner.error_message)

    def test_duplicate_launch_rejection(self) -> None:
        # Synthetic script that sleeps
        script = self.test_root / 'slow.py'
        script.write_text('import time\ntime.sleep(1.0)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
        )
        runner.start()
        self.assertTrue(runner.is_running)

        # Attempting second launch while running must raise KnackRunnerStateError
        with self.assertRaises(KnackRunnerStateError):
            runner.start()

        runner.stop(grace_period=2.0)
        runner.close()

    def test_session_identification_without_confusing_old_sessions(self) -> None:
        # 1. Pre-create an old session directory before launch
        old_session = self.out_dir / 'session-20261019-120000'
        old_session.mkdir(parents=True)
        (old_session / '00-eboot.bin.log').write_text('OLD LOG DATA\n', encoding='utf-8')

        # 2. Synthetic script that announces a new session
        new_session = self.out_dir / 'session-20261020-193000'
        new_log = new_session / '00-eboot.bin.log'

        script = self.test_root / 'mock_run_session.py'
        script.write_text(
            f'import sys, pathlib, time\n'
            f'new_dir = pathlib.Path(r"{new_session}")\n'
            f'new_dir.mkdir(parents=True, exist_ok=True)\n'
            f'log_file = pathlib.Path(r"{new_log}")\n'
            f'log_file.write_text("Knack 2 Guest Boot\\n", encoding="utf-8")\n'
            f'print("Starting eboot.bin; log: " + str(log_file), flush=True)\n'
            f'print("Session evidence: " + str(new_dir), flush=True)\n'
            f'time.sleep(0.1)\n',
            encoding='utf-8',
        )

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
        )
        runner.start()

        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        session_info = runner.session_info
        self.assertEqual(session_info.session_id, 'session-20261020-193000')
        self.assertEqual(session_info.session_dir, new_session.resolve())
        self.assertEqual(session_info.active_log_path, new_log.resolve())
        self.assertNotEqual(session_info.session_dir, old_session.resolve())
        self.assertEqual(session_info.source, 'stdout')
        self.assertFalse(session_info.is_ambiguous)

    def test_incremental_logtail_reading(self) -> None:
        log_file = self.test_root / 'test_active.log'
        log_file.write_text('Line 1\nLine 2\n', encoding='utf-8')

        # Create runner with mock log
        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        runner._session_info.active_log_path = log_file

        # 1. First poll reads existing lines
        first_lines = runner.poll_logs()
        self.assertEqual(first_lines, ['Line 1', 'Line 2'])

        # 2. Subsequent poll with no new lines returns empty list
        self.assertEqual(runner.poll_logs(), [])

        # 3. Append new lines
        with log_file.open('a', encoding='utf-8') as f:
            f.write('Line 3\nLine 4\n')

        second_lines = runner.poll_logs()
        self.assertEqual(second_lines, ['Line 3', 'Line 4'])

    def test_simulated_cancellation_cooperative(self) -> None:
        script = self.test_root / 'long_running.py'
        script.write_text(
            'import time\n'
            'try:\n'
            '    while True:\n'
            '        time.sleep(0.05)\n'
            'except BaseException:\n'
            '    pass\n',
            encoding='utf-8',
        )

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
        )
        runner.start()
        self.assertTrue(runner.is_running)

        stopped = runner.stop(grace_period=3.0)
        self.assertTrue(stopped)
        self.assertFalse(runner.is_running)
        self.assertIn(runner.state, (RunnerState.FINISHED, RunnerState.FAILED))

    @patch('knack2.runner.os.kill')
    @patch('knack2.runner.subprocess.run')
    def test_cancellation_timeout_and_escalation(self, tree_kill, signal_kill) -> None:
        # Mock process that ignores cooperative termination until kill()
        mock_proc = MagicMock()
        mock_proc.pid = 99999
        mock_proc.poll.side_effect = lambda: -9 if mock_proc.kill.called else None
        tree_kill.side_effect = lambda *args, **kwargs: mock_proc.kill()

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        runner._process = mock_proc
        runner._state = RunnerState.RUNNING

        # Calling stop should escalate to kill
        stopped = runner.stop(grace_period=0.1, kill_wait=0.2)
        self.assertTrue(stopped)
        mock_proc.kill.assert_called()
        if os.name == 'nt':
            self.assertEqual(tree_kill.call_args.args[0], ['taskkill', '/PID', '99999', '/T', '/F'])

    @patch('knack2.runner.os.kill')
    @patch('knack2.runner.subprocess.run')
    def test_cancellation_failure_raises_termination_error(self, tree_kill, signal_kill) -> None:
        # Mock process that never dies (poll always returns None)
        mock_proc = MagicMock()
        mock_proc.pid = 88888
        mock_proc.poll.return_value = None

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        runner._process = mock_proc
        runner._state = RunnerState.RUNNING

        with self.assertRaises(KnackRunnerTerminationError):
            runner.stop(grace_period=0.05, kill_wait=0.05)

    @patch('knack2.runner.os.kill')
    @patch('knack2.runner.subprocess.run')
    def test_close_preserves_process_reference_on_failure(self, tree_kill, signal_kill) -> None:
        """Requirement 1: close() preserves _process if process fails to terminate."""
        mock_proc = MagicMock()
        mock_proc.pid = 77777
        # Process never exits
        mock_proc.poll.return_value = None

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        runner._process = mock_proc
        runner._state = RunnerState.RUNNING

        # Calling close() must raise KnackRunnerTerminationError
        with self.assertRaises(KnackRunnerTerminationError):
            runner.close()

        # The reference must NOT be cleared!
        self.assertIsNotNone(runner._process)
        self.assertEqual(runner.pid, 77777)

        # Now simulate process exiting: subsequent close() succeeds
        mock_proc.poll.return_value = 0
        runner.close()
        self.assertIsNone(runner._process)
        self.assertIsNone(runner.pid)

    def test_automatic_log_polling_when_stdout_silent(self) -> None:
        """Requirement 2: guest log is incrementally read even when stdout produces no messages."""
        received_lines: list[str] = []

        # Script sleeps for 1.5 seconds WITHOUT printing anything to stdout
        silent_script = self.test_root / 'silent_sleep.py'
        silent_script.write_text('import time\ntime.sleep(1.5)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=silent_script,
            out_dir=self.out_dir,
            log_poll_interval=0.08,  # 80ms polling for fast test
            on_log_line=lambda line: received_lines.append(line),
        )

        runner.start()

        # Create the session folder and log file AFTER start (simulating run.py creating it during execution)
        session_folder = self.out_dir / 'session-20261020-210000'
        session_folder.mkdir(parents=True)
        guest_log = session_folder / '00-eboot.bin.log'
        guest_log.write_text('Header\n', encoding='utf-8')

        # Write lines to guest log while script is running silently
        time.sleep(0.15)
        with guest_log.open('a', encoding='utf-8') as f:
            f.write('Guest Frame 10\nGuest Frame 20\n')

        # Wait for the background poller to read them
        deadline = time.monotonic() + 1.5
        while len(received_lines) < 3 and time.monotonic() < deadline:
            time.sleep(0.05)

        self.assertIn('Header', received_lines)
        self.assertIn('Guest Frame 10', received_lines)
        self.assertIn('Guest Frame 20', received_lines)

        runner.stop(grace_period=2.0)
        runner.close()

    def test_callbacks_executed_outside_locks_no_deadlock(self) -> None:
        """Requirement 3: callbacks can inspect runner state and history without deadlock."""
        state_inspected: list[RunnerState] = []
        log_inspected: list[int] = []

        def on_state(state: RunnerState) -> None:
            # Re-entrant inspection of runner properties that take self._lock
            state_inspected.append(runner.state)
            _ = runner.is_running
            _ = runner.session_info
            _ = runner.pid

        def on_log(line: str) -> None:
            # Inspection of history that takes self._lock
            log_inspected.append(len(runner.get_recent_logs()))

        script = self.test_root / 'callbacks_test.py'
        script.write_text('print("Message 1", flush=True)\nprint("Message 2", flush=True)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
            on_state_change=on_state,
            on_log_line=on_log,
        )
        runner.start()

        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        self.assertGreater(len(state_inspected), 0)
        self.assertGreater(len(log_inspected), 0)
        self.assertEqual(runner.state, RunnerState.FINISHED)

    def test_concurrent_poll_logs_no_duplicates(self) -> None:
        """Requirement 3: serialized reads prevent duplicate or race conditions across threads."""
        guest_log = self.test_root / 'concurrent_test.log'
        guest_log.write_text(''.join(f'LogEntry {i}\n' for i in range(50)), encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        runner._session_info.active_log_path = guest_log

        gathered: list[str] = []
        gather_lock = threading.Lock()

        def worker() -> None:
            for _ in range(10):
                lines = runner.poll_logs()
                if lines:
                    with gather_lock:
                        gathered.extend(lines)
                time.sleep(0.01)

        threads = [threading.Thread(target=worker) for _ in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        # All 50 lines must be present, with zero duplicates
        self.assertEqual(len(gathered), 50)
        self.assertEqual(len(set(gathered)), 50)

    def test_session_fallback_rejects_ambiguous_concurrent_sessions(self) -> None:
        """Requirement 4: fallback does NOT arbitrarily pick when multiple concurrent sessions exist."""
        script = self.test_root / 'no_stdout.py'
        script.write_text('import time\ntime.sleep(0.1)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
        )
        runner.start()

        # Create TWO concurrent session directories during execution
        s1 = self.out_dir / 'session-20261020-111111'
        s2 = self.out_dir / 'session-20261020-222222'
        s1.mkdir(parents=True)
        s2.mkdir(parents=True)
        (s1 / 'session.json').write_text('{}', encoding='utf-8')
        (s2 / 'session.json').write_text('{}', encoding='utf-8')

        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        # Runner must refuse to choose arbitrarily
        session_info = runner.session_info
        self.assertTrue(session_info.is_ambiguous)
        self.assertIsNone(session_info.session_dir)
        self.assertEqual(session_info.session_id, '')

    def test_session_fallback_binds_single_valid_session(self) -> None:
        """Requirement 4: fallback binds cleanly if exactly one valid candidate exists."""
        script = self.test_root / 'no_stdout_single.py'
        script.write_text('import time\ntime.sleep(0.1)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
        )
        runner.start()

        # Create exactly one session directory during execution
        single_session = self.out_dir / 'session-20261020-333333'
        single_session.mkdir(parents=True)
        log_file = single_session / '00-eboot.bin.log'
        log_file.write_text('Knack 2 Single\n', encoding='utf-8')
        (single_session / 'session.json').write_text('{}', encoding='utf-8')

        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        session_info = runner.session_info
        self.assertFalse(session_info.is_ambiguous)
        self.assertEqual(session_info.session_id, 'session-20261020-333333')
        self.assertEqual(session_info.session_dir, single_session.resolve())
        self.assertEqual(session_info.source, 'fallback')

    @patch('knack2.runner.os.kill')
    @patch('knack2.runner.subprocess.run')
    def test_stop_timeout_followed_by_second_retry(self, tree_kill, signal_kill) -> None:
        """Regression 1: Genuine retry of stop() after timeout without returning fake success."""
        mock_proc = MagicMock()
        mock_proc.pid = 66666
        # Attempt 1: Process refuses to terminate
        mock_proc.poll.return_value = None

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        runner._process = mock_proc
        runner._state = RunnerState.RUNNING

        # Attempt 1 fails with timeout
        with self.assertRaises(KnackRunnerTerminationError):
            runner.stop(grace_period=0.05, kill_wait=0.05)

        # Runner is now in STOPPING state, but process is still alive!
        self.assertEqual(runner.state, RunnerState.STOPPING)
        self.assertEqual(runner.pid, 66666)

        # Attempt 2: If process is STILL alive, stop() must NOT return True!
        # It must perform a genuine stop attempt and raise KnackRunnerTerminationError
        with self.assertRaises(KnackRunnerTerminationError):
            runner.stop(grace_period=0.05, kill_wait=0.05)

        # Attempt 3: Process finally exits
        mock_proc.poll.return_value = 0
        success = runner.stop(grace_period=0.05, kill_wait=0.05)
        self.assertTrue(success)

    def test_concurrency_between_session_fallback_and_poll_logs(self) -> None:
        """Regression 2: No lock inversion deadlock between fallback and poll_logs()."""
        guest_log = self.out_dir / 'session-20261020-deadlock' / '00-eboot.bin.log'
        guest_log.parent.mkdir(parents=True)
        guest_log.write_text('Line 1\nLine 2\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )
        # Populate pre-existing sessions so fallback has work to do
        runner._pre_existing_sessions = set()
        runner._session_info.start_time = time.time() - 1.0

        stop_threads = threading.Event()

        def fallback_worker() -> None:
            while not stop_threads.is_set():
                with runner._lock:
                    runner._detect_session_fallback()
                time.sleep(0.002)

        def poller_worker() -> None:
            while not stop_threads.is_set():
                runner.poll_logs()
                time.sleep(0.002)

        t1 = threading.Thread(target=fallback_worker)
        t2 = threading.Thread(target=poller_worker)
        t1.start()
        t2.start()

        # Run concurrently for 0.4 seconds to prove zero deadlock
        time.sleep(0.4)
        stop_threads.set()

        t1.join(timeout=2.0)
        t2.join(timeout=2.0)
        self.assertFalse(t1.is_alive())
        self.assertFalse(t2.is_alive())

    def test_callback_state_query_from_another_thread_no_blocking(self) -> None:
        """Regression 3: Callbacks fire outside locks so another thread can query state instantly."""
        query_successful = threading.Event()
        query_failed = threading.Event()

        def on_state(state: RunnerState) -> None:
            if state == RunnerState.STARTING:
                # Spawn a separate thread to query runner.state while this callback runs
                def query_worker() -> None:
                    try:
                        # Must be able to acquire runner._lock and read runner.state immediately!
                        current_state = runner.state
                        if current_state == RunnerState.STARTING:
                            query_successful.set()
                        else:
                            query_failed.set()
                    except Exception:
                        query_failed.set()

                th = threading.Thread(target=query_worker)
                th.start()
                th.join(timeout=1.0)
                if th.is_alive():
                    # Thread was blocked by internal lock!
                    query_failed.set()

        script = self.test_root / 'instant_exit.py'
        script.write_text('import sys\nsys.exit(0)\n', encoding='utf-8')

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
            on_state_change=on_state,
        )
        runner.start()

        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        self.assertTrue(query_successful.is_set(), "Thread was blocked or failed to query runner.state")
        self.assertFalse(query_failed.is_set(), "Query thread experienced an error or deadlock")
        self.assertEqual(runner.state, RunnerState.FINISHED)

    def test_consecutive_runs_on_same_runner_instance(self) -> None:
        """Regression 4: Two consecutive runs on the same KnackRunner instance without contamination."""
        # Run 1
        session1_dir = self.out_dir / 'session-20261020-000001'
        session1_dir.mkdir(parents=True)
        log1 = session1_dir / '00-eboot.bin.log'
        log1.write_text('Session 1 Output\n', encoding='utf-8')

        script1 = self.test_root / 'mock_run1.py'
        script1.write_text(
            f'import time, pathlib\n'
            f'log1 = pathlib.Path(r"{log1}")\n'
            f'session1 = pathlib.Path(r"{session1_dir}")\n'
            f'print("Starting eboot.bin; log: " + str(log1), flush=True)\n'
            f'print("Session evidence: " + str(session1), flush=True)\n'
            f'time.sleep(0.05)\n',
            encoding='utf-8',
        )

        captured_lines: list[str] = []

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script1,
            out_dir=self.out_dir,
            on_log_line=lambda l: captured_lines.append(l),
        )

        # 1. Start Run 1
        runner.start()
        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        self.assertEqual(runner.state, RunnerState.FINISHED)
        self.assertEqual(runner.exit_code, 0)
        self.assertEqual(runner.session_info.session_id, 'session-20261020-000001')
        self.assertEqual(runner.session_info.session_dir, session1_dir.resolve())
        self.assertIn('Session 1 Output', runner.get_log_tail_lines())
        self.assertIn('Session 1 Output', captured_lines)

        # Run 2 setup
        session2_dir = self.out_dir / 'session-20261020-000002'
        session2_dir.mkdir(parents=True)
        log2 = session2_dir / '00-eboot.bin.log'
        log2.write_text('Session 2 Output\n', encoding='utf-8')

        script2 = self.test_root / 'mock_run2.py'
        script2.write_text(
            f'import time, pathlib\n'
            f'log2 = pathlib.Path(r"{log2}")\n'
            f'session2 = pathlib.Path(r"{session2_dir}")\n'
            f'print("Starting eboot.bin; log: " + str(log2), flush=True)\n'
            f'print("Session evidence: " + str(session2), flush=True)\n'
            f'time.sleep(0.05)\n',
            encoding='utf-8',
        )

        # Reassign run_py_path and clear capture for the second run
        runner.run_py_path = script2
        captured_lines.clear()

        # 2. Start Run 2 on the EXACT SAME instance
        runner.start()
        deadline = time.monotonic() + 3.0
        while runner.is_running and time.monotonic() < deadline:
            time.sleep(0.05)

        self.assertEqual(runner.state, RunnerState.FINISHED)
        self.assertEqual(runner.exit_code, 0)
        # Must bind to session 2, NOT session 1!
        self.assertEqual(runner.session_info.session_id, 'session-20261020-000002')
        self.assertEqual(runner.session_info.session_dir, session2_dir.resolve())
        logs2 = runner.get_log_tail_lines()
        self.assertIn('Session 2 Output', logs2)
        self.assertIn('Session 2 Output', captured_lines)
        # Session 1 logs must not appear in Run 2
        self.assertNotIn('Session 1 Output', logs2)
        self.assertNotIn('Session 1 Output', captured_lines)

    # --- Feature Track F2.1 Fechamento Definitivo Tests ---

    def test_reentrant_stopping_callback_no_deadlock(self) -> None:
        """Fechamento Definitivo 1: STOPPING callback re-entrantly calling stop() does not deadlock."""
        reentrant_stop_success = threading.Event()

        script = self.test_root / 'long_running_reentrant.py'
        script.write_text(
            'import time\n'
            'try:\n'
            '    while True:\n'
            '        time.sleep(0.05)\n'
            'except BaseException:\n'
            '    pass\n',
            encoding='utf-8',
        )

        def on_state(state: RunnerState) -> None:
            if state == RunnerState.STOPPING:
                # Re-entrant stop() call from within the STOPPING callback!
                # Must NOT deadlock even though stop() was already invoked
                res = runner.stop(grace_period=1.0, kill_wait=0.5)
                if res:
                    reentrant_stop_success.set()

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
            on_state_change=on_state,
        )
        runner.start()
        self.assertTrue(runner.is_running)

        # Outer stop call with safety deadline to detect deadlocks
        deadline = time.monotonic() + 5.0
        stopped = runner.stop(grace_period=2.0, kill_wait=1.0)
        self.assertTrue(stopped)
        self.assertTrue(time.monotonic() < deadline, "stop() took too long or deadlocked")
        self.assertTrue(reentrant_stop_success.is_set(), "Re-entrant stop() was not called or failed")
        self.assertFalse(runner.is_running)

    def test_cleanup_previous_run_refuses_when_thread_hung(self) -> None:
        """Fechamento Definitivo 2: _cleanup_previous_run() fails closed and preserves thread references if hung."""
        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=self.mock_run_py,
            out_dir=self.out_dir,
        )

        # Mock a worker thread that refuses to die
        mock_thread = MagicMock()
        mock_thread.is_alive.return_value = True
        mock_thread.name = 'MockHungMonitorThread'

        runner._monitor_thread = mock_thread

        # Calling start() must trigger _cleanup_previous_run() and raise KnackRunnerError
        with self.assertRaises(KnackRunnerError) as cm:
            runner.start()

        self.assertIn('ainda está ativa', str(cm.exception))
        # The reference must NOT be discarded!
        self.assertIs(runner._monitor_thread, mock_thread)
        self.assertNotEqual(runner.state, RunnerState.STARTING)

        # Same for log poller thread
        runner._monitor_thread = None
        mock_poller = MagicMock()
        mock_poller.is_alive.return_value = True
        mock_poller.name = 'MockHungPollerThread'
        runner._log_poll_thread = mock_poller

        with self.assertRaises(KnackRunnerError) as cm:
            runner.start()

        self.assertIn('ainda está ativa', str(cm.exception))
        self.assertIs(runner._log_poll_thread, mock_poller)

    def test_close_during_starting_callback_aborts_launch(self) -> None:
        """Case A: Calling runner.close() from on_state_change(STARTING) aborts launch and does not spawn process."""
        closed_in_starting = threading.Event()

        def on_state(state: RunnerState) -> None:
            if state == RunnerState.STARTING:
                runner.close()
                closed_in_starting.set()

        canary = self.test_root / 'canary_should_not_exist.txt'
        script = self.test_root / 'should_not_run.py'
        script.write_text(
            f'import pathlib\npathlib.Path(r"{canary}").write_text("SPAWNED")\n',
            encoding='utf-8',
        )

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
            on_state_change=on_state,
        )

        runner.start()

        self.assertTrue(closed_in_starting.is_set(), "STARTING callback was not executed")
        self.assertFalse(runner.is_running, "Runner should not be running after close()")
        self.assertIn(runner.state, (RunnerState.FINISHED, RunnerState.FAILED, RunnerState.IDLE))
        self.assertIsNone(runner.pid, "PID should be None after close()")
        self.assertFalse(canary.exists(), "Subprocess was spawned despite close() during STARTING!")

    def test_close_during_running_callback_cleans_up_consistently(self) -> None:
        """Case B: Calling runner.close() from on_state_change(RUNNING) cleans up consistently without orphan threads."""
        closed_in_running = threading.Event()

        def on_state(state: RunnerState) -> None:
            if state == RunnerState.RUNNING:
                runner.close()
                closed_in_running.set()

        script = self.test_root / 'sleep_and_exit.py'
        script.write_text(
            'import time\n'
            'try:\n'
            '    time.sleep(2.0)\n'
            'except BaseException:\n'
            '    pass\n',
            encoding='utf-8',
        )

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script,
            out_dir=self.out_dir,
            on_state_change=on_state,
        )

        runner.start()

        self.assertTrue(closed_in_running.is_set(), "RUNNING callback was not executed")
        self.assertFalse(runner.is_running, "Runner must not report is_running=True after close()")
        self.assertNotEqual(runner.state, RunnerState.STOPPING, "State must not remain stuck in STOPPING")
        self.assertIn(runner.state, (RunnerState.FINISHED, RunnerState.FAILED))
        self.assertIsNone(runner.pid, "PID must be None after close()")
        if runner._monitor_thread is not None:
            self.assertFalse(runner._monitor_thread.is_alive(), "Monitor thread was started after close()")
        if runner._log_poll_thread is not None:
            self.assertFalse(runner._log_poll_thread.is_alive(), "Log poll thread was started after close()")

    def test_reentrant_final_callback_restarts_and_stops_without_deadlock(self) -> None:
        """Regression: Callback FINISHED/FAILED restarts a new session and requests stop() without deadlock on _stop_lock."""
        restarted_and_stopped = threading.Event()
        session2_started = False

        script_session1 = self.test_root / 'script_s1.py'
        script_session1.write_text(
            'import time\n'
            'try:\n'
            '    while True:\n'
            '        time.sleep(0.05)\n'
            'except BaseException:\n'
            '    pass\n',
            encoding='utf-8',
        )

        script_session2 = self.test_root / 'script_s2.py'
        script_session2.write_text(
            'import time\n'
            'try:\n'
            '    while True:\n'
            '        time.sleep(0.05)\n'
            'except BaseException:\n'
            '    pass\n',
            encoding='utf-8',
        )

        def on_state(state: RunnerState) -> None:
            nonlocal session2_started
            # When session 1 completes (FINISHED or FAILED), restart runner with session 2
            if state in (RunnerState.FINISHED, RunnerState.FAILED) and not session2_started:
                session2_started = True
                runner.run_py_path = script_session2
                runner.start()
            elif state == RunnerState.RUNNING and session2_started:
                # Inside the callback of the restarted session, request stop()!
                # If _stop_lock was still held by session 1's stop(), this would deadlock.
                success = runner.stop(grace_period=1.0, kill_wait=0.5)
                if success:
                    restarted_and_stopped.set()

        runner = KnackRunner(
            game_dir=self.game_dir,
            run_py_path=script_session1,
            out_dir=self.out_dir,
            on_state_change=on_state,
        )

        runner.start()
        self.assertTrue(runner.is_running)

        # Stop session 1 with safety timeout to detect deadlock
        deadline = time.monotonic() + 5.0
        stopped_s1 = runner.stop(grace_period=1.5, kill_wait=0.5)
        self.assertTrue(stopped_s1)
        self.assertTrue(time.monotonic() < deadline, "Deadlock or timeout occurred during reentrant restart and stop!")
        self.assertTrue(restarted_and_stopped.wait(timeout=5.0), "Session 2 was not stopped from reentrant callback")
        self.assertFalse(runner.is_running)


if __name__ == '__main__':
    unittest.main()
