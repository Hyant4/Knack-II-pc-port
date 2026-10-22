# SPDX-License-Identifier: GPL-2.0-or-later
"""Public Knack II identity, independent of inherited internal symbol names."""
from pathlib import Path
from .storage import application_data_root

APP_NAME = 'Knack II'
GAME_APP_ID = 'KnackII.PCPort.Game'
LAUNCHER_APP_ID = 'KnackII.PCPort.Launcher'


def identity_assets():
    # Original artwork stays local, outside the repository and distributable sources.
    return application_data_root() / 'identity'


def runtime_path(port):
    """Prefer the branded artifact; retain compatibility with existing diagnostic builds."""
    directory = Path(port) / 'out'
    branded = directory / 'knack2-runtime.exe'
    legacy = directory / 'tlg-probe.exe'
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
