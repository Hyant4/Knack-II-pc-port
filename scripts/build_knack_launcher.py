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
    parser.add_argument('--stage-dir', type=Path,
                        help='New isolated deployment directory; never replaces installed launcher files.')
    parser.add_argument('--pkg-helper-dir', type=Path,
                        help='Include the separately built KnackPkg helper and its DLLs.')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root))
    from port.launcher.knack2.paths import display_version
    application_version = display_version(root)
    if application_version == 'dev':
        raise ValueError('A semantic application version is required in VERSION before packaging.')
    major, minor, patch = map(int, application_version.split('-', 1)[0].split('.'))
    from port.launcher.knack2.identity import identity_assets
    icon = identity_assets() / 'Knack.ico'
    if not icon.is_file():
        raise ValueError('Prepare the original local Knack icon before packaging.')
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    if args.stage_dir:
        stage = args.stage_dir.resolve()
        if stage.exists() and any(stage.iterdir()):
            raise ValueError('Stage directory must be new or empty.')
        output = stage / 'Knack II.exe'
        helper_output = stage / 'port/out/knack-launcher-supervisor'
    else:
        output = root / 'Knack II.exe'
        helper_output = root / 'port/out/knack-launcher-supervisor'
        if output.exists() or helper_output.exists():
            raise ValueError('Existing compiled launcher preserved. Choose a new deployment after reviewing the build.')
    if args.pkg_helper_dir:
        pkg_helper = args.pkg_helper_dir.resolve()
        if not (pkg_helper / 'KnackPkg.exe').is_file() or not (pkg_helper / 'helper-manifest.json').is_file():
            raise ValueError('PKG helper executable and manifest are required.')
        manifest = json.loads((pkg_helper / 'helper-manifest.json').read_text(encoding='utf-8'))
        if sha(pkg_helper / 'KnackPkg.exe') != manifest['sha256']:
            raise ValueError('PKG helper differs from its manifest.')
        for name, expected in manifest.get('bundle_files', {}).items():
            if Path(name).name != name or sha(pkg_helper / name) != expected:
                raise ValueError('PKG helper bundle differs from its manifest.')
    version = build / 'launcher-version.txt'
    version.write_text(f"""VSVersionInfo(
ffi=FixedFileInfo(filevers=({major},{minor},{patch},0),prodvers=({major},{minor},{patch},0),mask=0x3f,flags=2,OS=0x40004,fileType=1,subtype=0,date=(0,0)),
kids=[StringFileInfo([StringTable('040904B0',[
StringStruct('CompanyName','Knack II PC Port contributors'),
StringStruct('FileDescription','Knack II PC Launcher'),
StringStruct('FileVersion','{major}.{minor}.{patch}.0'),StringStruct('InternalName','KnackII.Launcher'),
StringStruct('OriginalFilename','Knack II.exe'),StringStruct('ProductName','Knack II PC Port'),
StringStruct('ProductVersion','{application_version}'),StringStruct('LegalCopyright','See README.md and upstream licenses.')])]),
VarFileInfo([VarStruct('Translation',[1033,1200])])])""", encoding='utf-8')
    env = dict(os.environ, KNACK_BUILD_ROOT=str(root), KNACK_BUILD_ICON=str(icon), KNACK_BUILD_VERSION=str(version))
    command = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--log-level', 'WARN',
               '--distpath', str(build / 'dist'), '--workpath', str(build / 'work'),
               str(root / 'port/launcher/knack2/packaging/knack2.spec')]
    subprocess.run(command, cwd=root, env=env, check=True)
    executable = build / 'dist/Knack II.exe'
    helper = build / 'dist/KnackSupervisor'
    # Copy to new destinations only. All caches and preparation/runtime outputs stay separate.
    helper_output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(helper, helper_output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with executable.open('rb') as source, output.open('xb') as destination:
        shutil.copyfileobj(source, destination)
    shutil.copy2(root / 'VERSION', output.parent / 'VERSION')
    if args.pkg_helper_dir:
        pkg_output = output.parent / 'port/out/knack-pkg-helper'
        pkg_output.mkdir(parents=True, exist_ok=False)
        for path in pkg_helper.iterdir():
            if path.is_file() and (path.suffix.lower() == '.dll' or path.name in (
                    'KnackPkg.exe', 'helper-manifest.json', 'CryptoPP-License.txt', 'PKGInstall-COPYING.txt',
                    'NOTICE.txt', 'helper-sources.zip')):
                shutil.copy2(path, pkg_output / path.name)
    report = {'launcher': str(output), 'launcher_sha256': sha(output),
              'supervisor': str(helper_output / 'KnackSupervisor.exe'),
              'supervisor_sha256': sha(helper_output / 'KnackSupervisor.exe'),
              'pyinstaller': importlib.metadata.version('pyinstaller'),
              'pyside6': importlib.metadata.version('PySide6-Essentials'),
              'icon_sha256': sha(icon), 'stage_dir': str(args.stage_dir.resolve()) if args.stage_dir else None,
              'game_started': False}
    if args.pkg_helper_dir:
        report['pkg_helper'] = str(pkg_output / 'KnackPkg.exe')
        report['pkg_helper_sha256'] = sha(pkg_output / 'KnackPkg.exe')
    (build / 'build-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
