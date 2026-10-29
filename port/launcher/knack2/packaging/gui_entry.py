# SPDX-License-Identifier: GPL-2.0-or-later
"""Compiled Knack II launcher entrypoint; --play remains validation-gated."""
import ctypes
import os
import sys
import traceback
import uuid
from port.launcher.knack2.storage import application_data_root


def packaging_check(report):
    """Exercise bundled paths and real process cancellation, using a synthetic child."""
    import json
    from pathlib import Path
    import tempfile
    import time
    import subprocess
    from port.launcher.knack2.paths import project_root, supervisor_executable, external_process_environment
    from port.launcher.knack2.runner import KnackRunner
    from port.launcher.knack2.validator import SUPPORTED_PROFILES, load_profile, profile_id
    root = project_root()
    profiles = []
    for name in SUPPORTED_PROFILES:
        profile = load_profile(root / 'profiles' / (name + '.json'))
        assert profile_id(profile) == name, name
        profiles.append(name)
    resources = {}
    if sys.platform == 'linux':
        import hashlib
        fonts = root / 'port/gpu/third_party/fonts'
        for name in ('DejaVuSans.ttf', 'LICENSE-DejaVu.txt'):
            path = fonts / name
            if not path.is_file():
                raise FileNotFoundError(f'Linux runtime font resource missing: {name}')
            resources[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    messages = []
    with tempfile.TemporaryDirectory(prefix='knack-package-check-') as directory:
        folder = Path(directory)
        shared_profile = folder / 'shared-profile'
        first = KnackRunner(folder / 'dump', timeout=0, out_dir=folder / 'single',
                            user_data=shared_profile, on_log_line=messages.append)
        second = KnackRunner(folder / 'dump', timeout=0, out_dir=folder / 'new-coop',
                             user_data=shared_profile, local_users=2, second_user=4099)
        commands = [first.build_command(), second.build_command()]
        first.build_command = lambda: [str(supervisor_executable()), '--selftest-wait']
        try:
            first.start()
            deadline = time.monotonic() + 15
            while 'Knack packaging child ready' not in messages and time.monotonic() < deadline:
                time.sleep(.02)
            assert 'Knack packaging child ready' in messages, messages
            assert first.stop(grace_period=5, kill_wait=3)
            assert 'Knack packaging child stopped' in messages, messages
            assert first.exit_code == 0
        finally:
            first.close()
            second.close()
        native = subprocess.run([str(supervisor_executable()), '--selftest-cpu'],
                                capture_output=True, text=True, timeout=20,
                                env=external_process_environment())
        assert native.returncode == 0, native.stdout + native.stderr
        assert 'CPU:' in native.stdout, native.stdout
        Path(report).write_text(json.dumps({'root': str(root), 'profiles': profiles, 'resources': resources, 'commands': commands,
                                           'messages': messages, 'controlled_stop': True,
                                           'native_cpu_smoke': native.stdout.strip(),
                                           'game_started': False}, indent=2), encoding='utf-8')
    return 0


def main():
    logs = application_data_root() / 'logs'
    logs.mkdir(parents=True, exist_ok=True)
    path = logs / ('launcher-' + uuid.uuid4().hex + '.log')
    with path.open('w', encoding='utf-8', buffering=1) as stream:
        if getattr(sys, 'frozen', False) or sys.stdout is None:
            sys.stdout = stream
        if getattr(sys, 'frozen', False) or sys.stderr is None:
            sys.stderr = stream
        try:
            if len(sys.argv) == 3 and sys.argv[1] == '--packaging-check':
                return packaging_check(sys.argv[2])
            from port.launcher.knack2.ui.app import main as launch
            return launch()
        except Exception:
            traceback.print_exc(file=stream)
            if os.name == 'nt':
                ctypes.windll.user32.MessageBoxW(None, 'Falha ao abrir Knack II.\nLog: ' + str(path), 'Knack II', 0x10)
            return 1


if __name__ == '__main__':
    raise SystemExit(main())
