#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Create a local Linux development bundle from an already built native runtime.

No game files, saves, installed launchers or dependency trees are modified.
The bundle targets the host distribution; native system libraries stay external.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-build', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True, help='new, empty output directory')
    args = parser.parse_args(argv)
    if sys.platform != 'linux' or platform.machine() not in ('x86_64', 'AMD64'):
        parser.error('Build this bundle on Linux x86-64.')
    root = Path(__file__).resolve().parents[1]
    native = args.native_build.resolve()
    build = args.build_dir.resolve()
    if build == root or root.is_relative_to(build) or native.is_relative_to(build):
        parser.error('Output directory overlaps the source root or native build.')
    if build.exists() and (not build.is_dir() or any(build.iterdir())):
        parser.error('Output directory must be new or empty; existing artifacts are preserved.')
    for name in ('tlg-probe', 'libtlggpu.so'):
        artifact = native / name
        if not artifact.is_file():
            parser.error(f'Missing native ELF artifact: {artifact}')
        with artifact.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                parser.error(f'Expected native ELF artifact: {artifact}')
    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, KNACK_BUILD_ROOT=str(root))
    subprocess.run([sys.executable, '-m', 'PyInstaller', '--log-level', 'WARN',
                    '--distpath', str(build / 'dist'), '--workpath', str(build / 'work'),
                    str(root / 'port/launcher/knack2/packaging/knack2.spec')],
                   cwd=root, env=env, check=True)
    stage = build / 'Knack-II-Linux-x86_64-dev'
    stage.mkdir()
    shutil.copy2(build / 'dist/Knack II', stage / 'Knack II')
    shutil.copytree(build / 'dist/KnackSupervisor', stage / 'port/out/knack-launcher-supervisor')
    runtime_dir = stage / 'port/out-linux'
    runtime_dir.mkdir()
    for name in ('tlg-probe', 'libtlggpu.so'):
        shutil.copy2(native / name, runtime_dir / name)
    shutil.copy2(root / 'run.py', stage / 'run.py')
    (stage / 'profiles').mkdir()
    sys.path.insert(0, str(root))
    from port.launcher.knack2.validator import SUPPORTED_PROFILES
    for profile in SUPPORTED_PROFILES:
        shutil.copy2(root / 'profiles' / (profile + '.json'), stage / 'profiles')
    scripts = stage / 'port/scripts'
    scripts.mkdir()
    for source in (root / 'port/scripts').glob('*.py'):
        shutil.copy2(source, scripts)
    for relative in ('port/LICENSE', 'port/UPSTREAM.md', 'README.upstream.md',
                     'docs/CREDITS.md', 'docs/DESENVOLVIMENTO_LINUX.md'):
        source = root / relative
        destination = stage / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    notices = stage / 'licenses'
    for directory in (root / 'port/gpu', root / 'port/third_party'):
        for source in directory.rglob('*'):
            if source.is_file() and source.name.upper().startswith(('LICENSE', 'COPYING', 'COPYRIGHT', 'NOTICE')):
                destination = notices / source.relative_to(root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
    # PyInstaller includes Python/Qt/iced-x86; retain their installed notices too.
    for distribution in ('PyInstaller', 'PySide6-Essentials', 'shiboken6', 'iced-x86'):
        metadata = importlib.metadata.distribution(distribution)
        for relative in metadata.files or ():
            if any(part.lower() == 'licenses' for part in relative.parts) or relative.name.lower().startswith(('license', 'copying')):
                source = Path(metadata.locate_file(relative))
                if source.is_file():
                    destination = notices / distribution / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, destination)
    python_notice = Path('/usr/share/doc/python3.14/copyright')
    if python_notice.is_file():
        shutil.copy2(python_notice, notices / 'CPython-copyright')
    (stage / 'LEIA-ME.txt').write_text(
        'Knack II — pacote local de desenvolvimento Linux x86-64\n\n'
        'Abra ./"Knack II". Abertura não inicia o jogo automaticamente.\n'
        'Python e Qt estão incluídos. Bibliotecas nativas do sistema não estão.\n'
        'Alvo inicial: Ubuntu 26.04; outras distribuições não foram validadas.\n'
        'Requer driver Vulkan e SDL3, FFmpeg 8, FreeType, fmt 10, Zydis e xxHash do sistema.\n'
        'Qt/X11 requer libxcb-cursor0, libxcb-shape0, libxcb-icccm4, libxcb-keysyms1,\n'
        'libxcb-render-util0, libxcb-image0, libxcb-xkb1 e libxkbcommon-x11-0.\n'
        'Seu dump CUSA08014 ou CUSA07670 01.01 deve ser selecionado separadamente.\n'
        'Não inclui dump, save ou execução de gameplay validada.\n'
        'Consulte docs/DESENVOLVIMENTO_LINUX.md para limites e testes.\n'
        'Arte local da interface exige conferência antes de qualquer distribuição pública.\n',
        encoding='utf-8')
    report = {'platform': platform.platform(), 'libc': platform.libc_ver(),
              'pyinstaller': importlib.metadata.version('PyInstaller'),
              'pyside6': importlib.metadata.version('PySide6-Essentials'),
              'native_sha256': sha(runtime_dir / 'tlg-probe'), 'game_started': False,
              'gameplay_verified': False, 'system_libraries_bundled': False}
    (stage / 'BUILDINFO.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    archive = build / (stage.name + '.tar.gz')
    with tarfile.open(archive, 'x:gz') as output:
        output.add(stage, arcname=stage.name)
    print(json.dumps({'stage': str(stage), 'archive': str(archive), 'sha256': sha(archive)}, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
