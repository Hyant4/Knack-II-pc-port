# SPDX-License-Identifier: GPL-2.0-or-later
# Independent Knack II packaging; no inherited launcher or title-specific runner.
import os
from pathlib import Path

root = Path(os.environ['KNACK_BUILD_ROOT'])
icon = os.environ.get('KNACK_BUILD_ICON')
version = os.environ.get('KNACK_BUILD_VERSION')
gui_options = {'hide_console': 'hide-early', 'icon': icon, 'version': version} if os.name == 'nt' else {}
helper_options = {'icon': icon} if os.name == 'nt' else {}
entry = root / 'port/launcher/knack2/packaging'
paths = [str(root), str(root / 'port/scripts')]
hidden = ['prepare', 'link_modules', 'link_libc', 'content_profile', 'isa_rewrite', 'red_zone', 'iced_x86']
ui = root / 'port/launcher/knack2/ui'
datas = [(str(p), 'port/launcher/knack2/ui') for p in ui.iterdir() if p.suffix in ('.qml', '.js')]
datas += [(str(p), 'port/launcher/knack2/ui/art') for p in (ui / 'art').iterdir()
          if p.suffix.lower() in ('.png', '.jpg', '.webp')]
unused = ['QtWebEngineCore', 'QtWebEngineWidgets', 'QtMultimedia', 'QtMultimediaWidgets',
          'QtWidgets', 'QtPrintSupport', 'QtQuick3D', 'QtPdf', 'QtSql', 'QtTest',
          'QtDesigner', 'QtHelp', 'QtSvgWidgets', 'Qt3DCore', 'QtBluetooth', 'QtNfc']
gui = Analysis([str(entry / 'gui_entry.py')], pathex=paths, datas=datas,
               hiddenimports=hidden, excludes=['PySide6.' + name for name in unused])
EXE(PYZ(gui.pure), gui.scripts, gui.binaries, gui.datas, name='Knack II',
    console=True, upx=False, strip=False, **gui_options)

helper = Analysis([str(entry / 'supervisor_entry.py')], pathex=paths,
                  hiddenimports=hidden, excludes=['PySide6', 'shiboken6'])
helper_exe = EXE(PYZ(helper.pure), helper.scripts, [], name='KnackSupervisor',
                 exclude_binaries=True, console=True, upx=False, strip=False, **helper_options)
COLLECT(helper_exe, helper.binaries, helper.datas, name='KnackSupervisor', upx=False, strip=False)
