#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package an existing Linux bundle as an AppImage without changing the runtime.

Run inside the Linux packaging virtualenv (PyInstaller required). Supply local,
verified appimagetool and type-2 runtime files. This script downloads nothing.
The host glibc and graphics drivers remain external; build on the oldest target
distribution when producing a broadly compatible release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def elf(path):
    with path.open('rb') as stream:
        return stream.read(4) == b'\x7fELF'


def external_library(name):
    # Keep glibc/loader and vendor graphics implementations matched to the host.
    return bool(re.match(
        r'^(ld-linux|lib(?:c|m|dl|pthread|rt|resolv|util|anl)\.so|libnss_|'
        r'lib(?:GL|EGL|GLES|OpenGL|cuda|nvidia)|libvulkan_(?:radeon|intel|lvp))', name))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True, help='new output directory')
    parser.add_argument('--appimagetool', type=Path, required=True, help='extracted appimagetool AppRun')
    parser.add_argument('--runtime-file', type=Path, required=True)
    parser.add_argument('--runtime-license', type=Path, required=True)
    parser.add_argument('--version', default='local-20261030', help='local artifact label; does not publish a release')
    args = parser.parse_args(argv)
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,79}', args.version):
        parser.error('Version label must contain only letters, digits, dots, underscores and hyphens.')
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        parser.error('AppImage packaging requires Linux x86-64.')
    source, build = args.bundle.resolve(), args.build_dir.resolve()
    if build.exists() or source.is_relative_to(build) or build.is_relative_to(source):
        parser.error('Use a new output directory outside the input bundle.')
    for name in ['Knack II', 'run.py', 'BUILDINFO.json', 'FILE-MANIFEST.json',
                 'port/out-linux/tlg-probe', 'port/out-linux/libtlggpu.so']:
        if not (source / name).is_file():
            parser.error(f'Missing bundle input: {name}')
    manifest = json.loads((source / 'FILE-MANIFEST.json').read_text())
    actual = {path.relative_to(source).as_posix() for path in source.rglob('*') if path.is_file()}
    if actual != set(manifest) | {'FILE-MANIFEST.json'}:
        parser.error('Bundle has unlisted or missing files; regenerate and review its manifest.')
    for name, digest in manifest.items():
        path = (source / name).resolve()
        if not path.is_relative_to(source) or sha(path) != digest:
            parser.error(f'Bundle manifest mismatch: {name}')
    from PyInstaller.archive.readers import CArchiveReader

    build.mkdir(parents=True)
    appdir = build / 'Knack-II.AppDir'
    payload = appdir / 'usr/knack'
    shutil.copytree(source, payload, symlinks=True)
    guide = Path(__file__).resolve().parents[1] / 'docs/APPIMAGE_LINUX.md'
    if guide.is_file():
        shutil.copy2(guide, payload / 'docs/APPIMAGE_LINUX.md')
    (payload / 'FILE-MANIFEST.json').unlink()  # Replaced by the complete AppDir manifest below.
    libraries = appdir / 'usr/lib'
    libraries.mkdir()
    analysis = build / 'gui-analysis'
    analysis.mkdir()
    archive = CArchiveReader(str(source / 'Knack II'))
    for name in archive.toc:
        if '.so' not in name:
            continue
        data = archive.extract(name)
        if not isinstance(data, bytes) or not data.startswith(b'\x7fELF'):
            continue
        path = (analysis / name).resolve()
        if not path.is_relative_to(analysis):
            raise ValueError(f'Invalid PyInstaller member: {name}')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    scan = [path for directory in [payload, analysis] for path in directory.rglob('*')
            if path.is_file() and elf(path)]
    private_dirs = sorted({str(path.parent) for path in scan if path.is_relative_to(analysis)})
    private_dirs += [str(payload / 'port/out/knack-launcher-supervisor/_internal')]
    env = dict(os.environ, LD_LIBRARY_PATH=':'.join([str(libraries), *private_dirs]))
    bundled, external, missing = {}, {}, set()
    for path in scan:
        result = subprocess.run(['ldd', str(path)], env=env, capture_output=True, text=True, timeout=30)
        text = result.stdout + result.stderr
        for line in text.splitlines():
            if '=> not found' in line:
                missing.add(line.strip())
            match = re.match(r'\s*(\S+)\s+=>\s+(/\S+)', line)
            if not match:
                continue
            name, filename = match.groups()
            library = Path(filename).resolve()
            if library.is_relative_to(libraries):
                continue
            if external_library(name):
                external[name] = filename
                continue
            digest = sha(library)
            destination = libraries / name
            if name in bundled and bundled[name]['sha256'] != digest:
                raise ValueError(f'Conflicting dependency: {name}')
            if not destination.exists():
                shutil.copy2(library, destination)
                bundled[name] = {'source': str(library), 'sha256': digest}
    if missing:
        raise RuntimeError('Unresolved input libraries: ' + '; '.join(sorted(missing)))

    notices = payload / 'licenses/appimage-system-libraries'
    notices.mkdir()
    shutil.copy2(args.runtime_license, payload / 'licenses/AppImage-type2-runtime-LICENSE.txt')
    (payload / 'licenses/AppImage-type2-runtime-SOURCE.txt').write_text(
        'https://github.com/AppImage/type2-runtime\n'
        'https://github.com/AppImage/type2-runtime/releases/tag/continuous\n'
        'Runtime SHA-256: ' + sha(args.runtime_file.resolve()) + '\n')
    packages = set()
    for info in bundled.values():
        owner = subprocess.run(['dpkg-query', '-S', info['source']], capture_output=True, text=True)
        for line in owner.stdout.splitlines():
            if ': ' in line:
                packages.add(line.rsplit(': ', 1)[0])
    for package in sorted(packages):
        name = package.split(':')[0]
        copyright = Path('/usr/share/doc') / name / 'copyright'
        if copyright.is_file():
            shutil.copy2(copyright, notices / (name + '-copyright'))
    (notices / 'packages.txt').write_text('\n'.join(sorted(packages)) + '\n')

    (appdir / 'AppRun').write_text('''#!/bin/sh
set -eu
appdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$appdir/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$appdir/usr/knack/Knack II" "$@"
''')
    (appdir / 'AppRun').chmod(0o755)
    (appdir / 'knack-ii.desktop').write_text('''[Desktop Entry]
Type=Application
Name=Knack II
Comment=Experimental Knack II PC port
Exec=AppRun
Icon=knack-ii
Terminal=false
Categories=Game;ActionGame;
X-AppImage-Version={version}
'''.format(version=args.version))
    (appdir / 'knack-ii.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
<rect width="256" height="256" rx="48" fill="#123451"/>
<path d="M54 56h30v63l47-63h37l-57 73 60 71h-40l-47-59v59H54z" fill="#f2ba58"/>
<path d="M184 56h18v144h-18zM215 56h18v144h-18z" fill="#e5eef4"/>
</svg>
''')
    (appdir / '.DirIcon').symlink_to('knack-ii.svg')
    versions = set()
    for path in scan + list(libraries.iterdir()):
        result = subprocess.run(['readelf', '--version-info', str(path)],
                                capture_output=True, text=True, timeout=30, check=True)
        versions.update(tuple(map(int, version.split('.'))) for version in
                        re.findall(r'Name: GLIBC_([0-9.]+)', result.stdout))
    required_glibc = '.'.join(map(str, max(versions)))
    info = json.loads((payload / 'BUILDINFO.json').read_text())
    info.update(format='AppImage Type 2', required_glibc=required_glibc,
                system_libraries_bundled=True, host_graphics_drivers_required=True,
                game_started=False, gameplay_verified=False,
                appimagetool_sha256=sha(args.appimagetool.resolve()),
                type2_runtime_sha256=sha(args.runtime_file.resolve()))
    (payload / 'BUILDINFO.json').write_text(json.dumps(info, indent=2))
    (payload / 'APPIMAGE-NOTES.md').write_text(
        '# Knack II — AppImage local\n\n'
        f'Linux x86-64; glibc {required_glibc} ou mais recente. '
        'Build original Ubuntu 26.04; outras distribuições não validadas.\n'
        'Python, Qt e dependências nativas incluídos. Drivers GPU e glibc são do sistema.\n'
        'Requer Vulkan 1.3 e dump próprio já extraído. Não inclui jogo ou saves.\n'
        'Dar permissão de execução e abrir o AppImage não inicia o jogo automaticamente.\n'
        'Sem FUSE, usar --appimage-extract-and-run. Preferências ficam no diretório de usuário.\n'
        'As quatro falhas nativas e demais limites da revisão anterior permanecem; '
        'consulte docs/REVISAO_BUILD_LOCAL_20261030.md.\n', encoding='utf-8')
    report = {'input_manifest_sha256': sha(source / 'FILE-MANIFEST.json'),
              'bundled_libraries': bundled, 'external_libraries': external,
              'scanned_elf_files': len(scan), 'required_glibc': required_glibc}
    (build / 'dependency-report.json').write_text(json.dumps(report, indent=2))
    final = {path.relative_to(appdir).as_posix(): sha(path) for path in appdir.rglob('*') if path.is_file()}
    (appdir / 'FILE-MANIFEST.json').write_text(json.dumps(final, indent=2))
    output = build / ('Knack-II-x86_64-' + args.version + '.AppImage')
    env = dict(os.environ, ARCH='x86_64', VERSION=args.version)
    with (build / 'appimagetool.log').open('w') as log:
        subprocess.run([str(args.appimagetool.resolve()), '--no-appstream',
                        '--runtime-file', str(args.runtime_file.resolve()),
                        '--comp', 'zstd', '--mksquashfs-opt', '-processors',
                        '--mksquashfs-opt', '4', str(appdir), str(output)],
                       env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    result = {'appimage': str(output), 'sha256': sha(output), 'bytes': output.stat().st_size,
              'required_glibc': required_glibc, 'bundled_library_count': len(bundled),
              'game_started': False}
    (build / 'appimage-result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
