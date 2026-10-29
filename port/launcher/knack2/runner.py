# SPDX-License-Identifier: GPL-2.0-or-later
"""Process runner and session supervisor for Knack II Launcher (Feature Track F2 / F2.1).

Supervises the execution of the official `run.py` script without altering
core game runtime contracts or invoking tlg-probe directly.

Hardening (F2.1 - Fechamento Definitivo):
1. Stop retry and concurrency protection:
   - Synchronized via `_stop_lock` to serialize concurrent stop calls.
   - Allows genuine retry of `stop()` when a process remains alive in `STOPPING` state.
   - Never returns `True` if `process.poll()` is still `None`.
   - Preserves `_process` reference on termination failure so the caller can retry.
2. Complete elimination of lock inversions:
   - Strict lock hierarchy: `_lock` is NEVER acquired while holding `_log_lock`.
   - `poll_logs()` reads state under `_lock`, releases `_lock`, then acquires `_log_lock`.
   - `_detect_session_fallback()` never acquires `_log_lock` under `_lock`.
   - Mathematical guarantee against deadlocks between log polling and session detection.
3. External callbacks executed 100% outside locks:
   - Callbacks for `STARTING`, `RUNNING`, `STOPPING`, `FINISHED`, and `FAILED` are always
     invoked after releasing all internal locks (including `_stop_lock`).
   - Supports re-entrant calls to `stop()` or `close()` within callbacks without deadlock.
   - External threads can safely query `runner.state`, `runner.is_running`, etc. during callbacks.
4. Clean consecutive runs and thread integrity:
   - `_cleanup_previous_run()` checks `is_alive()` after join timeouts. If any thread fails
     to terminate, it raises `KnackRunnerError` and strictly preserves thread references,
     refusing to launch a new session while old workers could interfere.
5. Strict session disambiguation:
   - Prioritizes authoritative stdout announcements from run.py.
   - Fails closed (`is_ambiguous = True`, `session_dir = None`) if multiple concurrent
     session directories are detected.

NOTE FOR GUI INTEGRATION (Qt/QML in Track F3):
The callbacks `on_state_change` and `on_log_line` fire on background worker threads
(`KnackRunner-Monitor` and `KnackRunner-LogPoller`). GUI implementations must marshal
these events to the UI main thread (e.g. via Qt signals/slots or thread-safe queues)
rather than touching GUI widgets directly.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import enum
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time
from typing import Callable, Mapping
from .languages import language_environment

# Re-use the existing lightweight log reader
try:
    from port.launcher.logtail import LogTail
except ImportError:
    # Fallback if imported from a different working directory
    _LAUNCHER_DIR = Path(__file__).resolve().parents[1]
    if str(_LAUNCHER_DIR) not in sys.path:
        sys.path.insert(0, str(_LAUNCHER_DIR))
    from logtail import LogTail  # type: ignore[no-redef]


class RunnerState(enum.Enum):
    """Explicit lifecycle states for the Knack II runner."""
    IDLE = 'IDLE'
    STARTING = 'STARTING'
    RUNNING = 'RUNNING'
    STOPPING = 'STOPPING'
    FINISHED = 'FINISHED'
    FAILED = 'FAILED'


class KnackRunnerError(Exception):
    """Base exception for Knack II process runner errors."""


class KnackRunnerStateError(KnackRunnerError):
    """Operation invalid for the current runner state (e.g. duplicate launch)."""


class KnackRunnerConfigError(KnackRunnerError, ValueError):
    """Invalid runner configuration (bad timeout, missing paths, etc.)."""


class KnackRunnerTerminationError(KnackRunnerError):
    """Process termination/cancellation timed out or failed."""


@dataclass
class SessionInfo:
    """Metadata regarding the active or last supervised Knack II session."""
    session_id: str = ''
    session_dir: Path | None = None
    start_time: float = 0.0
    end_time: float = 0.0
    current_executable: str = ''
    active_log_path: Path | None = None
    known_log_paths: list[Path] = field(default_factory=list)
    is_ambiguous: bool = False
    source: str = 'none'  # 'stdout', 'fallback', or 'none'


# Regex patterns matching run.py stdout announcements
_STARTING_LOG_RE = re.compile(r'^Starting\s+([^;]+);\s*log:\s*(.+)$', re.MULTILINE)
_SESSION_EVIDENCE_RE = re.compile(r'^Session\s+evidence:\s*(.+)$', re.MULTILINE)


class KnackRunner:
    """Supervises the execution and logs of the official Knack II `run.py` script.

    Thread-safe and suitable for integration with Qt/QML UI event loops.
    """

    def __init__(
        self,
        game_dir: Path | str,
        run_py_path: Path | str | None = None,
        timeout: int = 180,
        out_dir: Path | str | None = None,
        pad_file: Path | str | None = None,
        capture_dir: Path | str | None = None,
        analyze_only: bool = False,
        check_imports: bool = False,
        python_exe: Path | str | None = None,
        max_log_lines: int = 500,
        log_poll_interval: float = 0.25,
        on_state_change: Callable[[RunnerState], None] | None = None,
        on_log_line: Callable[[str], None] | None = None,
        on_guest_log_line: Callable[[str], None] | None = None,
        local_users: int = 1,
        second_user: int | None = None,
        config_file: Path | str | None = None,
        language: str | None = None,
        user_data: Path | str | None = None,
        experimental_build: bool = False,
        firmware_ngs2: bool = False,
    ) -> None:
        if not game_dir:
            raise KnackRunnerConfigError('O parâmetro game_dir é obrigatório')

        self.experimental_build = bool(experimental_build)
        self.firmware_ngs2 = bool(firmware_ngs2)  # opt-in: link the console firmware's NGS2
        self.game_dir = Path(game_dir).resolve()

        if run_py_path is None:
            # Default to repo root run.py
            repo_root = Path(__file__).resolve().parents[3]
            self.run_py_path = (repo_root / 'run.py').resolve()
        else:
            self.run_py_path = Path(run_py_path).resolve()

        if not self.run_py_path.is_file():
            raise KnackRunnerConfigError(f'Supervisor run.py não encontrado: {self.run_py_path}')

        if type(timeout) is not int or timeout < 0:
            raise KnackRunnerConfigError(f'O timeout deve ser inteiro >= 0 (0 = sem limite), obtido: {timeout}')
        self.timeout = int(timeout)
        language_environment(language)
        self.language = language
        self.user_data = Path(user_data).resolve() if user_data is not None else None
        if type(local_users) is not int or local_users not in (1, 2):
            raise KnackRunnerConfigError('Selecione um ou dois jogadores.')
        if ((local_users == 1 and second_user is not None) or
                (local_users == 2 and (type(second_user) is not int or not 2 <= second_user <= 2147483647))):
            raise KnackRunnerConfigError('P2 exige ID entre 2 e 2147483647 e dois jogadores explícitos.')
        if local_users == 2 and out_dir is None:
            raise KnackRunnerConfigError('P2 exige saída isolada explícita.')
        self.local_users, self.second_user = local_users, second_user
        self.config_file = Path(config_file).resolve() if config_file else None
        if self.config_file is not None and not self.config_file.is_file():
            raise KnackRunnerConfigError('Arquivo INI não encontrado.')

        if out_dir is None:
            # Same default as run.py: the experimental build never shares the normal saves (out/user).
            name = 'knack2-experimental' if self.experimental_build else 'knack2'
            self.out_dir = (self.run_py_path.parent / 'out' / name).resolve()
        else:
            self.out_dir = Path(out_dir).resolve()

        self.pad_file = Path(pad_file).resolve() if pad_file else None
        self.capture_dir = Path(capture_dir).resolve() if capture_dir else None

        if analyze_only and check_imports:
            raise KnackRunnerConfigError('analyze_only e check_imports são mutuamente exclusivos')
        self.analyze_only = bool(analyze_only)
        self.check_imports = bool(check_imports)

        # Resolving a venv's interpreter symlink selects the system Python and
        # loses its site-packages (including iced_x86 used by preparation).
        self.python_exe = Path(os.path.abspath(python_exe or sys.executable))
        self.max_log_lines = max_log_lines
        self.log_poll_interval = max(0.05, float(log_poll_interval))

        self._on_state_change = on_state_change
        self._on_log_line = on_log_line
        self._on_guest_log_line = on_guest_log_line

        # Strict lock hierarchy:
        # 1. _stop_lock: serializes stop() calls to prevent concurrent cancellation races.
        # 2. _lock: standard Lock (non-reentrant) guarding runner state, process, and session metadata.
        # 3. _log_lock: standard Lock guarding LogTail reads. NEVER acquire _lock while holding _log_lock!
        self._stop_lock = threading.Lock()
        self._lock = threading.Lock()
        self._log_lock = threading.Lock()
        self._stop_event = threading.Event()

        self._state = RunnerState.IDLE
        self._process: subprocess.Popen[str] | None = None
        self._monitor_thread: threading.Thread | None = None
        self._log_poll_thread: threading.Thread | None = None

        self._exit_code: int | None = None
        self._error_message: str = ''
        self._session_info = SessionInfo()
        self._logtail: LogTail | None = None
        self._log_history: list[str] = []
        self._pre_existing_sessions: set[str] = set()

    @property
    def state(self) -> RunnerState:
        """Current runner state."""
        with self._lock:
            return self._state

    @property
    def is_running(self) -> bool:
        """Whether the runner is currently active (STARTING, RUNNING, or STOPPING)."""
        with self._lock:
            return self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING)

    @property
    def pid(self) -> int | None:
        """Process identifier (PID) of the supervisor, if alive or launched."""
        with self._lock:
            return self._process.pid if self._process else None

    @property
    def exit_code(self) -> int | None:
        """Exit code of the supervisor process once completed."""
        with self._lock:
            return self._exit_code

    @property
    def error_message(self) -> str:
        """Error message or failure reason, if any."""
        with self._lock:
            return self._error_message

    @property
    def session_info(self) -> SessionInfo:
        """Information about the current/last session."""
        with self._lock:
            return self._session_info

    def _set_state(self, new_state: RunnerState) -> None:
        """Update runner state and invoke external callback outside locks."""
        callback = None
        with self._lock:
            if self._state != new_state:
                self._state = new_state
                callback = self._on_state_change
        if callback:
            try:
                callback(new_state)
            except Exception:
                pass  # External callback errors must not disrupt runner

    def build_command(self) -> list[str]:
        """Build the official command line list for subprocess execution.

        Handles spaces and Unicode paths natively without shell expansion.
        """
        cmd: list[str] = [
            str(self.python_exe),
            str(self.run_py_path),
            '--game-dir', str(self.game_dir),
            '--timeout', str(self.timeout),
            '--out', str(self.out_dir),
        ]
        if self.experimental_build:
            cmd.append('--experimental-build')
        if self.firmware_ngs2:
            cmd.append('--firmware-ngs2')
        if self.pad_file:
            cmd.extend(['--pad-file', str(self.pad_file)])
        if self.capture_dir:
            cmd.extend(['--capture-dir', str(self.capture_dir)])
        if self.config_file:
            cmd.extend(['--config', str(self.config_file)])
        if self.user_data is not None:
            cmd.extend(['--user-data', str(self.user_data)])
        if self.language is not None:
            cmd.extend(['--language', self.language])
        if self.local_users == 2:
            cmd.extend(['--local-users', '2', '--p2-user-id', str(self.second_user)])
        if self.analyze_only:
            cmd.append('--analyze')
        elif self.check_imports:
            cmd.append('--check-imports')
        return cmd

    def _cleanup_previous_run(self, timeout: float = 2.0) -> None:
        """Join background worker threads from a previous run.

        Verifies is_alive() after join timeout. If any worker thread remains active,
        raises KnackRunnerError and preserves references to prevent interference
        with new sessions.
        """
        self._stop_event.set()
        curr = threading.current_thread()

        if self._monitor_thread and self._monitor_thread.is_alive():
            if self._monitor_thread is not curr:
                self._monitor_thread.join(timeout=timeout)
                if self._monitor_thread.is_alive():
                    raise KnackRunnerError(
                        f'Thread de monitoramento de saída anterior ({self._monitor_thread.name}) '
                        'ainda está ativa após o tempo limite de espera.'
                    )
            self._monitor_thread = None

        if self._log_poll_thread and self._log_poll_thread.is_alive():
            if self._log_poll_thread is not curr:
                self._log_poll_thread.join(timeout=timeout)
                if self._log_poll_thread.is_alive():
                    raise KnackRunnerError(
                        f'Thread de leitura de logs anterior ({self._log_poll_thread.name}) '
                        'ainda está ativa após o tempo limite de espera.'
                    )
            self._log_poll_thread = None

    def start(self, env: Mapping[str, str] | None = None) -> None:
        """Start the official run.py supervisor asynchronously.

        Supports clean consecutive runs on the same KnackRunner instance.

        Raises:
            KnackRunnerStateError: If the supervisor is already running.
            KnackRunnerError: If starting the subprocess fails or previous threads cannot stop.
        """
        with self._lock:
            if self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING):
                raise KnackRunnerStateError(
                    f'Supervisor já está em execução (estado atual: {self._state.name})'
                )

        # Cleanup background threads from previous run outside locks (fails closed if hung)
        self._cleanup_previous_run(timeout=2.0)

        callback_starting = None
        with self._lock:
            if self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING):
                raise KnackRunnerStateError(
                    f'Supervisor já está em execução (estado atual: {self._state.name})'
                )

            self._state = RunnerState.STARTING
            callback_starting = self._on_state_change
            self._exit_code = None
            self._error_message = ''
            self._log_history.clear()
            self._session_info = SessionInfo(start_time=time.time())
            self._pre_existing_sessions = self._find_existing_sessions()
            self._stop_event.clear()

        with self._log_lock:
            self._logtail = None

        # Invoke STARTING callback outside all locks
        if callback_starting:
            try:
                callback_starting(RunnerState.STARTING)
            except Exception:
                pass

        # Check if launch was cancelled or closed during callback_starting!
        with self._lock:
            if self._state != RunnerState.STARTING:
                return

        cmd = self.build_command()
        creationflags = 0
        if os.name == 'nt':
            # Create a new process group so cooperative CTRL_BREAK signals can be targeted
            creationflags = subprocess.CREATE_NEW_PROCESS_GROUP

        try:
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding='utf-8',
                errors='replace',
                bufsize=1,  # Line buffered
                creationflags=creationflags,
                env=env,
            )
        except Exception as error:
            err_msg = f'Falha ao iniciar processo supervisor: {error}'
            callback_failed = None
            with self._lock:
                self._process = None
                self._error_message = err_msg
                self._state = RunnerState.FAILED
                callback_failed = self._on_state_change
            if callback_failed:
                try:
                    callback_failed(RunnerState.FAILED)
                except Exception:
                    pass
            raise KnackRunnerError(err_msg) from error

        callback_running = None
        with self._lock:
            if self._state != RunnerState.STARTING:
                # Cancelled or closed while Popen was spawning!
                try:
                    if proc.stdout and not proc.stdout.closed:
                        proc.stdout.close()
                    proc.kill()
                    proc.wait()
                except OSError:
                    pass
                return

            self._process = proc
            self._state = RunnerState.RUNNING
            callback_running = self._on_state_change

        # Invoke RUNNING callback outside all locks
        if callback_running:
            try:
                callback_running(RunnerState.RUNNING)
            except Exception:
                pass

        # Check if runner was stopped or closed during callback_running!
        with self._lock:
            if self._state != RunnerState.RUNNING or self._process is None:
                if proc.stdout and not proc.stdout.closed:
                    try:
                        proc.stdout.close()
                    except OSError:
                        pass
                return

            # 1. Background stdout monitoring thread
            self._monitor_thread = threading.Thread(
                target=self._monitor_loop,
                name='KnackRunner-Monitor',
                daemon=True,
            )
            self._monitor_thread.start()

            # 2. Independent background guest log polling thread
            self._log_poll_thread = threading.Thread(
                target=self._log_poll_loop,
                name='KnackRunner-LogPoller',
                daemon=True,
            )
            self._log_poll_thread.start()

    def _find_existing_sessions(self) -> set[str]:
        """List existing session-* directory names in out_dir."""
        if not self.out_dir.is_dir():
            return set()
        try:
            return {
                entry.name
                for entry in self.out_dir.iterdir()
                if entry.is_dir() and entry.name.startswith('session-')
            }
        except OSError:
            return set()

    def _log_poll_loop(self) -> None:
        """Periodic worker polling the active guest log file independently of stdout."""
        while not self._stop_event.wait(timeout=self.log_poll_interval):
            if not self.is_running:
                break
            # Check if active log needs fallback discovery under _lock
            has_active_log = False
            with self._lock:
                has_active_log = bool(self._session_info.active_log_path)
                if not has_active_log:
                    self._detect_session_fallback()
            # poll_logs() handles its own locking without holding _lock
            self.poll_logs()

    def _monitor_loop(self) -> None:
        """Background thread monitoring stdout, active session, and process termination."""
        with self._lock:
            process = self._process
        if not process or not process.stdout:
            return

        stdout_pipe = process.stdout
        line_callback = self._on_log_line

        # Consume stdout line by line until process terminates
        for raw_line in stdout_pipe:
            line = raw_line.rstrip('\r\n')
            with self._lock:
                self._log_history.append(line)
                if len(self._log_history) > self.max_log_lines:
                    self._log_history.pop(0)

            # Parse session announcements (acquires _lock then _log_lock sequentially, never nested)
            self._parse_stdout_announcement(line)

            # Deliver stdout line callback outside locks
            if line_callback:
                try:
                    line_callback(line)
                except Exception:
                    pass

        # Process closed stdout, wait for exit code
        code = process.wait()
        stdout_pipe.close()

        # Signal log poller thread that process has finished
        self._stop_event.set()
        if self._log_poll_thread and self._log_poll_thread.is_alive():
            self._log_poll_thread.join(timeout=1.0)

        with self._lock:
            self._exit_code = code
            self._session_info.end_time = time.time()
            # Final check for session evidence on disk if not announced in stdout
            self._detect_session_fallback()

        # Final flush of logtail (outside self._lock)
        self.poll_logs(final=True)

        # Transition state and invoke callback outside all locks
        final_state: RunnerState
        err_msg: str = ''
        callback = None

        with self._lock:
            if self._state == RunnerState.STOPPING:
                final_state = RunnerState.FINISHED if code == 0 else RunnerState.FAILED
            elif code == 0:
                final_state = RunnerState.FINISHED
            else:
                final_state = RunnerState.FAILED
                err_msg = f'Processo supervisor encerrou com código de saída {code}'

            self._state = final_state
            if err_msg:
                self._error_message = err_msg
            callback = self._on_state_change

        if callback:
            try:
                callback(final_state)
            except Exception:
                pass

    def _parse_stdout_announcement(self, line: str) -> None:
        """Inspect a line of stdout for authoritative session evidence or log switches."""
        m_start = _STARTING_LOG_RE.match(line)
        if m_start:
            # The previous stage has exited before run.py announces LoadExec's
            # next stage. Drain its last lines before switching log ownership.
            self.poll_logs(final=True)
            executable = m_start.group(1).strip()
            log_path_str = m_start.group(2).strip()
            log_path = Path(log_path_str).resolve()

            with self._lock:
                self._session_info.current_executable = executable
                self._session_info.active_log_path = log_path
                if log_path not in self._session_info.known_log_paths:
                    self._session_info.known_log_paths.append(log_path)

                session_dir = log_path.parent
                if session_dir.name.startswith('session-'):
                    self._session_info.session_dir = session_dir
                    self._session_info.session_id = session_dir.name
                    self._session_info.source = 'stdout'
                    self._session_info.is_ambiguous = False

            # Switch LogTail under _log_lock (outside _lock!)
            with self._log_lock:
                self._logtail = LogTail(log_path, max_lines=self.max_log_lines)
            return

        m_ev = _SESSION_EVIDENCE_RE.match(line)
        if m_ev:
            evidence_str = m_ev.group(1).strip()
            ev_dir = Path(evidence_str).resolve()
            with self._lock:
                self._session_info.session_dir = ev_dir
                self._session_info.session_id = ev_dir.name
                self._session_info.source = 'stdout'
                self._session_info.is_ambiguous = False

    def _detect_session_fallback(self) -> None:
        """Identify new session directory if stdout announcements were absent.

        Must be called while holding self._lock. Never acquires _log_lock.
        """
        if self._session_info.source == 'stdout' and self._session_info.session_dir and self._session_info.session_dir.is_dir():
            return
        if not self.out_dir.is_dir():
            return

        try:
            candidates: list[Path] = []
            for entry in self.out_dir.iterdir():
                if entry.is_dir() and entry.name.startswith('session-'):
                    # Must not be an old session
                    if entry.name not in self._pre_existing_sessions:
                        # Must have been created around or after start time
                        mtime = entry.stat().st_mtime
                        if mtime >= (self._session_info.start_time - 2.0):
                            candidates.append(entry)

            if len(candidates) > 1:
                # Ambiguous: multiple concurrent session directories created!
                self._session_info.is_ambiguous = True
                self._session_info.session_dir = None
                self._session_info.session_id = ''
                return

            if len(candidates) == 1:
                single = candidates[0]
                logs = sorted(single.glob('*.log'))
                has_session_json = (single / 'session.json').is_file()
                if logs or has_session_json:
                    self._session_info.session_dir = single
                    self._session_info.session_id = single.name
                    self._session_info.source = 'fallback'
                    self._session_info.is_ambiguous = False
                    self._session_info.known_log_paths = logs
                    if logs:
                        self._session_info.active_log_path = logs[-1]
        except OSError:
            pass

    def poll_logs(self, final: bool = False) -> list[str]:
        """Incrementally read new lines from the active guest log file.

        Lock hierarchy rule:
        1. Reads state under `_lock`, then releases `_lock`.
        2. Acquires `_log_lock` to serialize LogTail reads.
        3. Invokes external callbacks outside all locks.
        """
        active_path: Path | None = None
        callback: Callable[[str], None] | None = None

        with self._lock:
            active_path = self._session_info.active_log_path
            callback = self._on_guest_log_line or self._on_log_line

        new_lines: list[str] = []
        with self._log_lock:
            tail = self._logtail
            if not tail:
                if active_path and active_path.is_file():
                    tail = LogTail(active_path, max_lines=self.max_log_lines)
                    self._logtail = tail
                else:
                    return []

            new_lines = tail.poll(final=final)

        # Invoke callback outside all locks
        if new_lines and callback:
            for line in new_lines:
                try:
                    callback(line)
                except Exception:
                    pass
        return new_lines

    def get_recent_logs(self) -> list[str]:
        """Return cached recent supervisor output lines."""
        with self._lock:
            return list(self._log_history)

    def get_log_tail_lines(self) -> list[str]:
        """Return all lines currently buffered by the active guest LogTail instance."""
        with self._log_lock:
            if self._logtail:
                return list(self._logtail.lines)
            return []

    def stop(self, grace_period: float = 5.0, kill_wait: float = 2.0) -> bool:
        """Cancel the running supervisor session cleanly.

        Hardened retry, reentrancy, and concurrency behavior (F2.1):
        - Handles reentrant stop() or close() during STARTING, RUNNING, or STOPPING callbacks.
        - When cancelled during STARTING with no active process, transitions safely to STOPPING -> FINISHED.
        - When cancelled during RUNNING before threads are started, transitions safely to FINISHED/FAILED.
        - Callback for STOPPING is fired with ZERO locks held (_lock, _stop_lock, _log_lock).
        - Supports re-entrant calls to stop() or close() within callbacks without deadlock.
        - Synchronized via `_stop_lock` to serialize concurrent stop calls.
        - Allows genuine retry of `stop()` when a process remains alive in `STOPPING` state.
        - Never returns `True` if `process.poll()` is still `None`.
        - Preserves `_process` reference on termination failure so the caller can retry.

        Returns:
            True if process terminated within grace_period or kill_wait.
        """
        callback_stopping = None
        callback_final = None
        final_state: RunnerState | None = None

        with self._lock:
            process = self._process

            if process is None:
                if self._state == RunnerState.STARTING:
                    self._state = RunnerState.STOPPING
                    callback_stopping = self._on_state_change
                    self._stop_event.set()
                else:
                    if self._state == RunnerState.STOPPING:
                        self._state = RunnerState.FINISHED
                        final_state = RunnerState.FINISHED
                        callback_final = self._on_state_change
                    return True
            elif process.poll() is not None:
                if self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING):
                    code = process.poll()
                    self._exit_code = code
                    self._state = RunnerState.FINISHED if code == 0 else RunnerState.FAILED
                    final_state = self._state
                    callback_final = self._on_state_change
                return True
            else:
                if self._state != RunnerState.STOPPING:
                    self._state = RunnerState.STOPPING
                    callback_stopping = self._on_state_change

        # Fire callback when ZERO locks are held! (Permits re-entrant stop()/close() calls)
        if callback_stopping:
            try:
                callback_stopping(RunnerState.STOPPING)
            except Exception:
                pass

        # If process was None during STARTING, finalize transition to FINISHED
        with self._lock:
            if self._process is None:
                if self._state == RunnerState.STOPPING:
                    self._state = RunnerState.FINISHED
                    final_state = RunnerState.FINISHED
                    callback_final = self._on_state_change

        if callback_final and final_state:
            try:
                callback_final(final_state)
            except Exception:
                pass

        with self._lock:
            if self._process is None:
                return True

        stopped_successfully = False

        # 2. Acquire _stop_lock for process signal / wait / kill serialization
        with self._stop_lock:
            with self._lock:
                process = self._process
                # Re-check in case re-entrant call or concurrent thread already stopped it
                if process is None or process.poll() is not None:
                    if process is not None and self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING):
                        code = process.poll()
                        self._exit_code = code
                        self._state = RunnerState.FINISHED if code == 0 else RunnerState.FAILED
                        final_state = self._state
                        callback_final = self._on_state_change
                    elif process is None and self._state == RunnerState.STOPPING:
                        self._state = RunnerState.FINISHED
                        final_state = RunnerState.FINISHED
                        callback_final = self._on_state_change
                    stopped_successfully = True

            if not stopped_successfully:
                # Cooperative request
                try:
                    if os.name == 'nt':
                        # Sending CTRL_BREAK_EVENT to the process group allows run.py's
                        # BaseException handler to run, clean up child probe, and write session.json
                        os.kill(process.pid, signal.CTRL_BREAK_EVENT)
                    else:
                        process.send_signal(signal.SIGINT)
                except OSError:
                    # Fall back to terminate if signal cannot be delivered
                    if os.name != 'nt':
                        try:
                            process.terminate()
                        except OSError:
                            pass

                # Bounded wait for cooperative shutdown
                deadline = time.monotonic() + grace_period
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        self._stop_event.set()
                        if self._monitor_thread and self._monitor_thread.is_alive():
                            self._monitor_thread.join(timeout=1.0)
                        if self._log_poll_thread and self._log_poll_thread.is_alive():
                            self._log_poll_thread.join(timeout=1.0)

                        with self._lock:
                            if self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING):
                                code = process.poll()
                                self._exit_code = code
                                self._state = RunnerState.FINISHED if code == 0 else RunnerState.FAILED
                                final_state = self._state
                                callback_final = self._on_state_change
                        stopped_successfully = True
                        break
                    time.sleep(0.05)

            if not stopped_successfully:
                # Escalation: kill
                try:
                    if os.name == 'nt':
                        # Kill the tree before its root; never orphan the guest.
                        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL, timeout=kill_wait,
                                       creationflags=subprocess.CREATE_NO_WINDOW, check=False)
                    else:
                        process.kill()
                except (OSError, subprocess.TimeoutExpired):
                    pass

                # Final bounded wait
                kill_deadline = time.monotonic() + kill_wait
                while time.monotonic() < kill_deadline:
                    if process.poll() is not None:
                        self._stop_event.set()
                        if self._monitor_thread and self._monitor_thread.is_alive():
                            self._monitor_thread.join(timeout=1.0)
                        if self._log_poll_thread and self._log_poll_thread.is_alive():
                            self._log_poll_thread.join(timeout=1.0)

                        with self._lock:
                            if self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING):
                                code = process.poll()
                                self._exit_code = code
                                self._state = RunnerState.FINISHED if code == 0 else RunnerState.FAILED
                                final_state = self._state
                                callback_final = self._on_state_change
                        stopped_successfully = True
                        break
                    time.sleep(0.05)

            if not stopped_successfully:
                # Still running after kill escalation.
                # Never return True when process is still alive!
                # Preserve self._process so caller can inspect or retry.
                raise KnackRunnerTerminationError(
                    f'O processo supervisor {process.pid} não encerrou após período de graça ({grace_period}s) '
                    f'e escalação forçada ({kill_wait}s).'
                )

        # 3. Fire final callback outside _stop_lock, _lock, and _log_lock!
        if callback_final and final_state:
            try:
                callback_final(final_state)
            except Exception:
                pass

        return True

    def close(self) -> None:
        """Release runner resources, closing streams and stopping background threads.

        Hardening (F2.1):
        - Preserves self._process reference if the process fails to terminate.
        - Raises KnackRunnerTerminationError if the process could not be confirmed stopped.
        - Verifies threads have joined; raises KnackRunnerError if threads refuse to stop.
        - Cleans up internal resources only when the process and threads are confirmed dead.
        - Guarantees runner state transitions out of active states (STARTING/RUNNING/STOPPING).
        """
        if self.is_running:
            self.stop(grace_period=2.0, kill_wait=1.0)

        # Verify that process is dead before clearing references
        with self._lock:
            if self._process is not None and self._process.poll() is None:
                raise KnackRunnerTerminationError(
                    f'Não foi possível fechar o runner: o processo supervisor {self._process.pid} ainda está ativo.'
                )

        self._cleanup_previous_run(timeout=2.0)

        with self._lock:
            if self._process is not None:
                if self._process.stdout and not self._process.stdout.closed:
                    try:
                        self._process.stdout.close()
                    except OSError:
                        pass
                if self._process.stderr and not self._process.stderr.closed:
                    try:
                        self._process.stderr.close()
                    except OSError:
                        pass
            self._process = None
            if self._state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING):
                self._state = RunnerState.FINISHED

        with self._log_lock:
            self._logtail = None
