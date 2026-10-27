# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Knack II Windows executable; never builds or starts the game runtime."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root))
    from port.launcher.knack2.identity import identity_assets
    icon = identity_assets() / 'Knack.ico'
    if not icon.is_file():
        raise ValueError('Prepare the original local Knack icon before packaging.')
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    output = root / 'Knack II.exe'
    helper_output = root / 'port/out/knack-launcher-supervisor'
    if output.exists() or helper_output.exists():
        raise ValueError('Existing compiled launcher preserved. Choose a new deployment after reviewing the build.')
    version = build / 'launcher-version.txt'
    version.write_text("""VSVersionInfo(
ffi=FixedFileInfo(filevers=(0,17,7,0),prodvers=(0,17,7,0),mask=0x3f,flags=2,OS=0x40004,fileType=1,subtype=0,date=(0,0)),
kids=[StringFileInfo([StringTable('040904B0',[
StringStruct('CompanyName','Knack II PC Port contributors'),
StringStruct('FileDescription','Knack II PC Launcher'),
StringStruct('FileVersion','0.17.7.0'),StringStruct('InternalName','KnackII.Launcher'),
StringStruct('OriginalFilename','Knack II.exe'),StringStruct('ProductName','Knack II PC Port'),
StringStruct('ProductVersion','0.17.7-candidate'),StringStruct('LegalCopyright','See README.md and upstream licenses.')])]),
VarFileInfo([VarStruct('Translation',[1033,1200])])])""", encoding='utf-8')
    env = dict(os.environ, KNACK_BUILD_ROOT=str(root), KNACK_BUILD_ICON=str(icon), KNACK_BUILD_VERSION=str(version))
    command = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--log-level', 'WARN',
               '--distpath', str(build / 'dist'), '--workpath', str(build / 'work'),
               str(root / 'port/launcher/knack2/packaging/knack2.spec')]
    subprocess.run(command, cwd=root, env=env, check=True)
    executable = build / 'dist/Knack II.exe'
    helper = build / 'dist/KnackSupervisor'
    # Copy to new destinations only. All caches and preparation/runtime outputs stay separate.
    shutil.copytree(helper, helper_output)
    with executable.open('rb') as source, output.open('xb') as destination:
        shutil.copyfileobj(source, destination)
    report = {'launcher': str(output), 'launcher_sha256': sha(output),
              'supervisor': str(helper_output / 'KnackSupervisor.exe'),
              'supervisor_sha256': sha(helper_output / 'KnackSupervisor.exe'),
              'pyinstaller': importlib.metadata.version('pyinstaller'),
              'pyside6': importlib.metadata.version('PySide6-Essentials'),
              'icon_sha256': sha(icon), 'game_started': False}
    (build / 'build-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
