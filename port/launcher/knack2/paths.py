# SPDX-License-Identifier: GPL-2.0-or-later
"""Find project files beside compiled launchers, outside extraction caches."""
from pathlib import Path
import os
import sys


def project_root():
    if not getattr(sys, 'frozen', False):
        return Path(__file__).resolve().parents[3]
    for directory in Path(sys.executable).resolve().parents:
        if (directory / 'run.py').is_file() and (directory / 'profiles/CUSA08014-01.01.json').is_file():
            return directory
    raise FileNotFoundError('Knack II: mantenha o launcher junto ao repositório do projeto.')


def supervisor_executable():
    if not getattr(sys, 'frozen', False):
        return Path(sys.executable).resolve()
    name = 'KnackSupervisor.exe' if sys.platform == 'win32' else 'KnackSupervisor'
    executable = project_root() / 'port/out/knack-launcher-supervisor' / name
    if not executable.is_file():
        raise FileNotFoundError('Supervisor Knack II ausente. Reinstale o build completo do launcher.')
    return executable


def external_process_environment(env=None):
    """Restore host libraries before a frozen Linux app starts another executable."""
    if sys.platform != 'linux' or not getattr(sys, 'frozen', False):
        return env
    values = dict(os.environ if env is None else env)
    original = values.pop('LD_LIBRARY_PATH_ORIG', None)
    if original:
        values['LD_LIBRARY_PATH'] = original
    else:
        values.pop('LD_LIBRARY_PATH', None)
    return values
