# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic supervisor: bounded histories, worker callbacks, no process launch."""
from __future__ import annotations

from collections import deque
from dataclasses import replace
import threading
import time
from typing import Callable, Protocol
import uuid

from ..runner import RunnerState, SessionInfo


class DemoRunnerPort(Protocol):
    """F3A injection seam; only synthetic implementations may be connected.

    Lifecycle and state reuse F2.1's public types. Unlike F2.1's combined log
    callback, this seam requires explicit supervisor/guest callbacks. F3B must
    resolve provenance before adapting a real supervisor; F3A has no such adapter.
    """
    state: RunnerState
    session_info: SessionInfo
    error_message: str

    def start(self) -> None: ...
    def stop(self) -> None: ...
    def close(self) -> None: ...
    def inject_error(self) -> None: ...


class SimulatedRunner:
    """Repeatable finite sessions. Callbacks never start or stop sessions."""

    def __init__(self, *, on_state_change: Callable[[RunnerState], None],
                 on_log_line: Callable[[str], None],
                 on_guest_log_line: Callable[[str], None], interval: float = 0.18):
        self._on_state_change = on_state_change
        self._on_log_line = on_log_line
        self._on_guest_log_line = on_guest_log_line
        self._interval = max(0.005, interval)
        self._lock = threading.Lock()
        self._cancel = threading.Event()
        self._failure = threading.Event()
        self._thread: threading.Thread | None = None
        self._state = RunnerState.IDLE
        self._session = SessionInfo()
        self._error = ''
        self._supervisor: deque[str] = deque(maxlen=500)
        self._guest: deque[str] = deque(maxlen=500)

    @property
    def state(self):
        with self._lock:
            return self._state

    @property
    def session_info(self):
        with self._lock:
            return replace(self._session)

    @property
    def error_message(self):
        with self._lock:
            return self._error

    @property
    def is_running(self):
        return self.state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING)

    def _transition(self, state):
        with self._lock:
            self._state = state
        self._on_state_change(state)

    def _log(self, source, message):
        with self._lock:
            history = self._guest if source == 'guest' else self._supervisor
            history.append(message)
        callback = self._on_guest_log_line if source == 'guest' else self._on_log_line
        callback(message)

    def get_recent_logs(self):
        with self._lock:
            return list(self._supervisor)

    def get_log_tail_lines(self):
        with self._lock:
            return list(self._guest)

    def start(self):
        if self._thread is not None and self._thread.is_alive():
            raise RuntimeError('A simulação anterior ainda está ativa.')
        self._cancel.clear()
        self._failure.clear()
        with self._lock:
            self._error = ''
            self._supervisor.clear()
            self._guest.clear()
            self._session = SessionInfo(session_id='demo-' + uuid.uuid4().hex[:10],
                                        start_time=time.time(), source='synthetic')
        self._thread = threading.Thread(target=self._run, name='Knack2-Demo', daemon=True)
        self._thread.start()

    def _run(self):
        self._transition(RunnerState.STARTING)
        self._log('supervisor', 'Preparando sessão sintética. Nenhum processo do jogo foi criado.')
        if not self._cancel.wait(self._interval * 2):
            self._transition(RunnerState.RUNNING)
            self._log('supervisor', 'Supervisor simulado em execução.')
            for tick in range(1, 31):
                if self._cancel.wait(self._interval) or self._failure.is_set():
                    break
                self._log('guest', f'[demo] Evento sintético {tick:02d}: interface respondendo.')
                if tick % 5 == 0:
                    self._log('supervisor', f'Pulso {tick // 5}: sessão sintética ativa.')
        if self._failure.is_set():
            with self._lock:
                self._error = 'Erro simulado: sessão interrompida para testar a interface.'
            self._log('supervisor', 'ERROR: falha sintética solicitada pelo usuário.')
            final = RunnerState.FAILED
        else:
            self._transition(RunnerState.STOPPING)
            self._log('supervisor', 'Encerrando simulação e liberando recursos.')
            self._cancel.wait(self._interval)
            final = RunnerState.FINISHED
        with self._lock:
            self._session.end_time = time.time()
        self._transition(final)

    def stop(self):
        self._cancel.set()
        # Cancellation is asynchronous; only the final state confirms completion.

    def inject_error(self):
        self._failure.set()

    def close(self):
        self._cancel.set()
        if self._thread is not None and self._thread is not threading.current_thread():
            self._thread.join(timeout=1.0)
            if self._thread.is_alive():
                raise RuntimeError('A thread sintética não encerrou no prazo.')
