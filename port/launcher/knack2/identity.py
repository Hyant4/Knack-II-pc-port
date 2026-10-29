# SPDX-License-Identifier: GPL-2.0-or-later
"""Public Knack II identity, independent of inherited internal symbol names."""
import os
from pathlib import Path
import sys
from .storage import application_data_root

APP_NAME = 'Knack II'
GAME_APP_ID = 'KnackII.PCPort.Game'
LAUNCHER_APP_ID = 'KnackII.PCPort.Launcher'


def identity_assets():
    # Original artwork stays local, outside the repository and distributable sources.
    return application_data_root() / 'identity'


def runtime_names(is_windows=None):
    if is_windows is None:
        is_windows = os.name == 'nt'
    return ('knack2-runtime.exe', 'tlg-probe.exe') if is_windows else ('knack2-runtime', 'tlg-probe')


def runtime_path(port):
    """Prefer the platform runtime; retain compatibility with diagnostic builds."""
    directory = Path(port) / 'out'
    names = runtime_names()
    if not names[0].endswith('.exe'):
        # Keep native build output separate from installed Windows artifacts.
        linux = Path(port) / 'out-linux'
        for name in names:
            if (linux / name).is_file():
                return linux / name
    branded, legacy = (directory / name for name in names)
    return branded if branded.is_file() or not legacy.is_file() else legacy


def game_identity_environment(root):
    icon = identity_assets() / 'Knack.bmp'
    values = {'TLG_APP_NAME': APP_NAME, 'TLG_APP_ID': GAME_APP_ID}
    if icon.is_file():
        values['TLG_WINDOW_ICON'] = str(icon.resolve())
    return values


def set_launcher_app_id():
    import os
    if os.name == 'nt':
        import ctypes
        function = ctypes.WinDLL('shell32').SetCurrentProcessExplicitAppUserModelID
        function.argtypes = [ctypes.c_wchar_p]
        function.restype = ctypes.c_long
        result = function(LAUNCHER_APP_ID)
        if result < 0:
            raise OSError(f'SetCurrentProcessExplicitAppUserModelID failed: {result:#x}')
