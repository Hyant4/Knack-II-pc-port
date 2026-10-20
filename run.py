#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare and supervise Knack 2's original executables on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent
PORT = ROOT / 'port'
sys.path.insert(0, str(PORT / 'scripts'))
import prepare
import link_modules
import content_profile


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_game(game, profile):
    values = prepare.load_sfo(game / 'sce_sys/param.sfo')
    if (values.get('TITLE_ID'), values.get('APP_VER')) != (profile['title_id'], profile['version']):
        raise ValueError('This profile requires CUSA08014 version 01.01')
    for name, expected in profile['executables'].items():
        if digest(prepare.executable_path(game, name)) != expected:
            raise ValueError(f'{name}: SHA-256 differs from the supported dump')
    for name in {m for modules in profile['modules'].values() for m in modules}:
        link_modules.module(game / 'sce_module' / name)
    return values


def prepare_image(game, profile, executable, out):
    folder = out / executable.replace('.', '_')
    folder.mkdir(parents=True, exist_ok=True)
    modules = profile['modules'][executable]
    fingerprint = {name: digest(path) for name, path in [
        ('executable', game / executable), ('prepare', PORT / 'scripts/prepare.py'),
        ('linker', PORT / 'scripts/link_modules.py'), ('sfo', game / 'sce_sys/param.sfo'),
        *[(name, game / 'sce_module' / name) for name in modules]]}
    marker = folder / 'prepared.json'
    cached = marker.is_file() and json.loads(marker.read_text()) == fingerprint
    if not cached or not all((folder / name).is_file() for name in ('boot-linked.bin', 'analysis.json', 'link.json')):
        prepare.prepare(game, folder, executable)
        link_modules.link(game, folder, modules, executable)
        marker.write_text(json.dumps(fingerprint, indent=2), encoding='utf-8')
    content_profile.prepare(game, folder)
    return folder


def read_exec_request(path, profile):
    data = path.read_bytes()
    if len(data) > 270000 or data[:8] != b'K2EXEC01':
        raise ValueError('invalid LoadExec request')
    pos = 8
    def number():
        nonlocal pos
        value, = struct.unpack('<I', prepare.span(data, pos, 4))
        pos += 4
        return value
    def string():
        nonlocal pos
        n = number()
        if n > 4096:
            raise ValueError('LoadExec argument exceeds limit')
        value = prepare.span(data, pos, n).decode('utf-8')
        pos += n
        if '\0' in value:
            raise ValueError('NUL in LoadExec argument')
        return value
    guest = string()
    if not guest.startswith('/app0/') or guest[6:] not in profile['executables']:
        raise ValueError('LoadExec target is outside this profile')
    count = number()
    if count >= 64:
        raise ValueError('too many LoadExec arguments')
    args = [string() for _ in range(count)]
    if pos != len(data):
        raise ValueError('trailing LoadExec data')
    return guest[6:], args


def runtime_environment(out, *, pad_file=None, capture_dir=None):
    # Old launchers' title-specific overrides must not bleed into this profile.
    env = {k: v for k, v in os.environ.items()
           if k == 'TLG_TOGGLE_FILE' or not k.startswith(('TLG_', 'ISS_', 'GR2_', 'GOW3_', 'BB_'))}
    toolchain = Path(env.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
    env['PATH'] = str(toolchain / 'clang64/bin') + os.pathsep + env.get('PATH', '')
    system_font = Path(env.get('WINDIR', 'C:/Windows')) / 'Fonts/arial.ttf'
    if system_font.is_file():
        env.setdefault('KNACK2_SYSTEM_FONT', str(system_font))
    env.update(KNACK2_PROFILE='CUSA08014-01.01', TLG_UPSCALER='off', TLG_FULLSCREEN='0',
               TLG_CONFIG=str(out / 'knack2.ini'), TLG_RED_ZONE_REWRITE='1',
               TLG_FREEZE_REPORT_SECONDS='10', TLG_GPU_LOG='info', TLG_VERBOSE_LOG='1')
    if pad_file is not None:
        pad_file = Path(pad_file).resolve()
        if not pad_file.is_file():
            raise ValueError('--pad-file must name an existing input file')
        env['TLG_PAD_FILE'] = str(pad_file)
    if capture_dir is not None:
        capture_dir = Path(capture_dir).resolve()
        capture_dir.mkdir(parents=True, exist_ok=True)
        env['TLG_DUMP_DIR'] = str(capture_dir)
        env['TLG_FRAME_DUMP_TRIGGER'] = str(capture_dir / 'capture.request')
    return env


def run_child(command, env, log, seconds):
    with log.open('wb') as output:
        process = subprocess.Popen(command, env=env, cwd=PORT, stdin=subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT)
        try:
            return process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            return 124
        except BaseException:
            process.kill()
            process.wait()
            raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--analyze', action='store_true')
    mode.add_argument('--check-imports', action='store_true')
    parser.add_argument('--timeout', type=int, default=180, help='maximum seconds for the entire guest session')
    parser.add_argument('--out', type=Path, default=ROOT / 'out/knack2')
    parser.add_argument('--pad-file', type=Path, help='explicit scePad button input file for diagnostics')
    parser.add_argument('--capture-dir', type=Path, help='write capture.request here to dump the next presented frame')
    args = parser.parse_args(argv)
    if args.timeout < 1:
        parser.error('--timeout must be positive')
    game, out = args.game_dir.resolve(), args.out.resolve()
    profile = json.loads((ROOT / 'profiles/CUSA08014-01.01.json').read_text())
    validate_game(game, profile)
    out.mkdir(parents=True, exist_ok=True)
    images = {name: prepare_image(game, profile, name, out) for name in profile['executables']}
    if args.analyze:
        print(f'Analysis complete: {out}. Execution and visual boot remain unverified.')
        return 0
    probe = PORT / 'out/tlg-probe.exe'
    if not probe.is_file():
        raise ValueError('Build the runtime first: .\\build.ps1')
    env = runtime_environment(out, pad_file=args.pad_file, capture_dir=args.capture_dir)
    user = out / 'user'
    user.mkdir(exist_ok=True)
    if not args.check_imports:
        for folder in images.values():
            subprocess.run([sys.executable, str(PORT / 'scripts/isa_rewrite.py'), str(folder / 'boot-linked.bin'),
                            '--out', str(folder), '--red-zone'], check=True)
    session = out / ('session-' + time.strftime('%Y%m%d-%H%M%S'))
    session.mkdir()
    report = {'profile': 'CUSA08014-01.01', 'mode': 'imports' if args.check_imports else 'run',
              'visual_verified': False, 'stages': []}
    deadline = time.monotonic() + args.timeout
    executable, guest_args = profile['entry'], []
    status = 0
    try:
        for stage in range(8):
            folder = images[executable]
            remaining = max(1, int(deadline - time.monotonic()))
            command = [str(probe), str(folder / 'boot-linked.bin'), '--app0', str(game), '--user', str(user),
                       '--content-profile', str(folder / 'content.bin'), '--timeout', str(remaining),
                       '--guest-executable', '/app0/' + executable]
            if executable == 'eboot.bin':
                command.append('--cpu-only')
            if args.check_imports:
                command.append('--check-imports')
            else:
                command += ['--isa-rewrite', str(folder / 'isa-rewrite.bin')]
            for value in guest_args:
                command += ['--guest-arg', value]
            log = session / f'{stage:02d}-{executable}.log'
            with tempfile.TemporaryDirectory(prefix='exec-', dir=session) as temporary:
                request = Path(temporary) / 'request.bin'
                child_env = {**env, 'KNACK2_EXEC_REQUEST': str(request)}
                print(f'Starting {executable}; log: {log}', flush=True)
                code = run_child(command, child_env, log, remaining + 5)
                report['stages'].append({'executable': executable, 'arguments': guest_args, 'exit_code': code,
                                         'log': log.name})
                print(f'{executable}: exit {code}', flush=True)
                if args.check_imports:
                    status = status or code
                    if executable == 'eboot.bin':
                        executable = 'game.elf'
                        continue
                    return status
                if code == 75:
                    executable, guest_args = read_exec_request(request, profile)
                    if time.monotonic() >= deadline:
                        return 124
                    continue
                return code
        raise ValueError('LoadExec transition limit reached')
    finally:
        (session / 'session.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(f'Session evidence: {session}', flush=True)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, struct.error, subprocess.CalledProcessError) as error:
        print(f'Knack 2: {error}', file=sys.stderr)
        sys.exit(1)
