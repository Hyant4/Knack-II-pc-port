# SPDX-License-Identifier: GPL-2.0-or-later
"""Bundled interpreter for the official supervisor and its existing preparation step."""
import ctypes
import os
from pathlib import Path
import runpy
import subprocess
import signal
import sys
import time
import iced_x86  # Explicitly collect the existing ISA decoder dependency.
from port.launcher.knack2.paths import project_root, external_process_environment
# run.py imports these dynamically; make them visible to the packager's analysis.
from port.launcher.knack2 import config, languages, identity, storage, validator


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    for stream in (sys.stdout, sys.stderr):
        if stream is not None:
            stream.reconfigure(encoding='utf-8', errors='replace')
    root = project_root()
    # Avoid propagating the packager's DLL search path to the native game runtime.
    if os.name == 'nt' and getattr(sys, 'frozen', False):
        ctypes.windll.kernel32.SetDllDirectoryW(None)
    if sys.platform == 'linux' and getattr(sys, 'frozen', False):
        clean = external_process_environment()
        for name in ('LD_LIBRARY_PATH', 'LD_LIBRARY_PATH_ORIG'):
            os.environ.pop(name, None)
            if name in clean:
                os.environ[name] = clean[name]
    if argv == ['--selftest-wait']:
        # Packaging-only stop test: no guest, dump, Vulkan, controller or saves.
        if hasattr(signal, 'SIGBREAK'):
            signal.signal(signal.SIGBREAK, signal.default_int_handler)
        try:
            print('Knack packaging child ready', flush=True)
            while True:
                time.sleep(.1)
        except KeyboardInterrupt:
            print('Knack packaging child stopped', flush=True)
            return 0
    if argv == ['--selftest-cpu']:
        # Packaging-only native loader check; no dump, guest, GPU or savedata.
        # The GUI-subsystem Windows loader needs explicit standard handles.
        result = subprocess.run([str(identity.runtime_path(root / 'port')), '--cpu-features'],
                                capture_output=True, text=True, encoding='utf-8', errors='replace',
                                timeout=20, check=False)
        for stream, output in ((sys.stdout, result.stdout), (sys.stderr, result.stderr)):
            if stream is not None and output:
                stream.write(output)
                stream.flush()
        return result.returncode
    if not argv:
        raise ValueError('Use the Knack II launcher to start a session.')
    script = Path(argv[0]).resolve()
    allowed = {root / 'run.py', root / 'port/scripts/isa_rewrite.py'}
    if script not in allowed:
        raise ValueError('Only the official Knack II supervisor and ISA preparation script are supported.')
    sys.path.insert(0, str(root / 'port/scripts'))
    sys.path.insert(0, str(root))
    sys.argv = [str(script), *argv[1:]]
    runpy.run_path(str(script), run_name='__main__')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print('Knack II:', error, file=sys.stderr, flush=True)
        raise SystemExit(1)
