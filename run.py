#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare and supervise Knack 2's original executables on Windows or Linux."""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent
PORT = ROOT / 'port'
sys.path.insert(0, str(PORT / 'scripts'))
sys.path.insert(0, str(ROOT))
import prepare
import link_modules
import content_profile
from port.launcher.knack2.languages import BY_CODE, discover_languages, language_environment
from port.launcher.knack2.identity import game_identity_environment, runtime_path
from port.launcher.knack2.storage import user_data_lock
from port.launcher.knack2.config import load_knack2_ini, sanitize_and_validate_key_value
from port.launcher.knack2.validator import load_profile, profile_id, SUPPORTED_PROFILES


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate_game(game, profile):
    values = prepare.load_sfo(game / 'sce_sys/param.sfo')
    if (values.get('TITLE_ID'), values.get('APP_VER')) != (profile['title_id'], profile['version']):
        raise ValueError(f"This profile requires {profile['title_id']} version {profile['version']}")
    for name, expected in profile['executables'].items():
        if digest(prepare.executable_path(game, name)) != expected:
            raise ValueError(f'{name}: SHA-256 differs from the supported dump')
    for name in {m for modules in profile['modules'].values() for m in modules}:
        link_modules.module(game / 'sce_module' / name)
    return values


def prepare_image(game, profile, executable, out):
    # Preserve American cache paths; European images never replace those images.
    cache = out if profile_id(profile) == 'CUSA08014-01.01' else out / profile_id(profile)
    folder = cache / executable.replace('.', '_')
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


def p2_user_id(value):
    if not value or any(c not in '0123456789' for c in value):
        raise argparse.ArgumentTypeError('P2 ID must contain decimal digits only')
    # Bound conversion before int() so arbitrarily long arguments fail predictably.
    digits = value.lstrip('0') or '0'
    if len(digits) > 10 or not 2 <= int(digits) <= 2147483647:
        raise argparse.ArgumentTypeError('P2 ID must be between 2 and 2147483647')
    return int(digits)


def local_user_environment(local_users, second_user):
    if type(local_users) is not int or local_users not in (1, 2):
        raise ValueError('--local-users must be 1 or 2')
    if local_users == 1:
        if second_user is not None:
            raise ValueError('--p2-user-id requires --local-users 2')
        return {}
    if type(second_user) is not int or not 2 <= second_user <= 2147483647:
        raise ValueError('--local-users 2 requires a P2 ID between 2 and 2147483647')
    return {'TLG_LOCAL_USERS': '2', 'TLG_LOCAL_P2_USER_ID': str(second_user)}


def runtime_environment(out, *, pad_file=None, capture_dir=None, local_users=1, second_user=None,
                        config_file=None, language=None, profile='CUSA08014-01.01'):
    if profile not in SUPPORTED_PROFILES:
        raise ValueError('Unsupported Knack II runtime profile')
    users = local_user_environment(local_users, second_user)
    locale = language_environment(language)
    if config_file is not None:
        config_file = Path(config_file).resolve()
        if not config_file.is_file():
            raise ValueError('--config must name an existing INI file')
    settings_path = config_file if config_file is not None else Path(out) / 'knack2.ini'
    settings = load_knack2_ini(settings_path) if settings_path.is_file() else {}
    readbacks = sanitize_and_validate_key_value('gpu_readbacks', settings.get('gpu_readbacks', '2'))[1]
    eager = sanitize_and_validate_key_value('eager_readback', settings.get('eager_readback', '0'))[1]
    upscaler = sanitize_and_validate_key_value('upscaler', settings.get('upscaler', 'off'))[1]
    preset = sanitize_and_validate_key_value('preset', settings.get('preset', '0'))[1]
    internal = sanitize_and_validate_key_value('internal_resolution',
                                              settings.get('internal_resolution', 'auto'))[1]
    # Old launchers' title-specific overrides must not bleed into this profile.
    env = {k: v for k, v in os.environ.items()
           if k == 'TLG_TOGGLE_FILE' or not k.startswith(('TLG_', 'ISS_', 'GR2_', 'GOW3_', 'BB_'))}
    if sys.platform == 'win32':
        toolchain = Path(env.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
        env['PATH'] = str(toolchain / 'clang64/bin') + os.pathsep + env.get('PATH', '')
        system_font = Path(env.get('WINDIR', 'C:/Windows')) / 'Fonts/arial.ttf'
        if system_font.is_file():
            env.setdefault('KNACK2_SYSTEM_FONT', str(system_font))
    env.update(KNACK2_PROFILE=profile, TLG_UPSCALER=upscaler,
               TLG_UPSCALE_PRESET=preset, TLG_INTERNAL_RESOLUTION=internal,
               TLG_OBJECT_MOTION='0', TLG_FULLSCREEN='0',
               TLG_CONFIG=str(config_file if config_file is not None else out / 'knack2.ini'),
               TLG_RED_ZONE_REWRITE='1' if sys.platform == 'win32' else '0',
               TLG_FREEZE_REPORT_SECONDS='10', TLG_GPU_LOG='info', TLG_VERBOSE_LOG='1')
    # Native framebuffer storage is allocated from the game's fixed graphics pool.
    # Match its guarded reserve increase with the runtime's existing direct-memory capacity.
    if internal in ('2560x1440', '3840x2160'):
        env['TLG_DMEM_MB'] = {'2560x1440': '5824', '3840x2160': '6592'}[internal]
    env.update(users)  # Only validated CLI values may enable a second local user.
    env.update(locale)  # Inherited TLG_LANGUAGE remains filtered; only explicit CLI values survive.
    env.update(game_identity_environment(ROOT))  # Fixed local identity, never inherited from another port.
    # Startup-only GPU choices come from the validated INI, after inherited overrides are filtered.
    env.update(TLG_READBACKS=readbacks, TLG_EAGER_READBACK=eager if readbacks == '2' else '0')
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


def native_resolution_arguments(executable, arguments, resolution):
    """Use the game's native framebuffer preset only for an explicitly selected high size."""
    result = list(arguments)
    if executable == 'game.elf' and resolution in ('2560x1440', '3840x2160'):
        if not result:
            result.append('/app0/game.elf')
        if '-2160p' not in result:
            if len(result) >= 64:
                raise ValueError('Native resolution requires a free guest argument slot')
            result.append('-2160p')
    return result


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
    with ExitStack() as lifetime:
        return _main(argv, lifetime)


def _main(argv, lifetime):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--analyze', action='store_true')
    mode.add_argument('--check-imports', action='store_true')
    parser.add_argument('--timeout', type=int, default=180,
                        help='maximum seconds for the entire guest session; 0 disables the time limit')
    parser.add_argument('--out', type=Path, help='output root; P2 requires an explicit fresh directory')
    parser.add_argument('--config', type=Path, help='explicit runtime INI; defaults to OUT/knack2.ini')
    parser.add_argument('--runtime', type=Path, help='explicit host runtime from an isolated build')
    parser.add_argument('--user-data', type=Path, help='explicit persistent user profile, separate from session output')
    parser.add_argument('--language', choices=tuple(BY_CODE), help='explicit language installed in the dump')
    parser.add_argument('--local-users', type=int, choices=(1, 2), default=1)
    parser.add_argument('--p2-user-id', type=p2_user_id, help='explicit local P2 ID, required with --local-users 2')
    parser.add_argument('--pad-file', type=Path, help='explicit scePad button input file for diagnostics')
    parser.add_argument('--capture-dir', type=Path, help='write capture.request here to dump the next presented frame')
    args = parser.parse_args(argv)
    if args.timeout < 0:
        parser.error('--timeout must be non-negative (0 means unlimited)')
    # Validate the complete launch configuration before reading/preparing the dump or writing anything.
    try:
        local_user_environment(args.local_users, args.p2_user_id)
        if args.local_users == 2 and args.out is None:
            raise ValueError('--local-users 2 requires --out with a fresh isolated directory')
        game = args.game_dir.resolve()
        out = (args.out if args.out is not None else ROOT / 'out/knack2').resolve()
        user = args.user_data.resolve() if args.user_data is not None else out / 'user'
        if args.user_data is not None:
            for protected in (game, out, (PORT / 'user').resolve(), (ROOT / 'out/knack2').resolve()):
                if user.is_relative_to(protected) or protected.is_relative_to(user):
                    raise ValueError('--user-data overlaps dump, session output or habitual user data')
            if len(str(user).encode('utf-8')) >= 512:
                raise ValueError('user path exceeds the runtime limit of 511 UTF-8 bytes')
            for directory in (user, *user.parents):
                if directory.exists() and not directory.is_dir():
                    raise ValueError('--user-data must name a directory with directory parents')
        if out.exists() and not out.is_dir():
            raise ValueError('--out must name a directory')
        if args.config is not None:
            args.config = args.config.resolve()
            if not args.config.is_file():
                raise ValueError('--config must name an existing INI file')
        if args.pad_file is not None:
            args.pad_file = args.pad_file.resolve()
            if not args.pad_file.is_file():
                raise ValueError('--pad-file must name an existing input file')
        if args.capture_dir is not None:
            args.capture_dir = args.capture_dir.resolve()
            if args.capture_dir.exists() and not args.capture_dir.is_dir():
                raise ValueError('--capture-dir must name a directory')
        for directory in (out, args.capture_dir):
            if directory is not None:
                for parent in directory.parents:
                    if parent.exists() and not parent.is_dir():
                        raise ValueError(f'output parent is not a directory: {parent}')
        if args.local_users == 2:
            for protected in ((ROOT / 'out/knack2').resolve(), (PORT / 'user').resolve(), game):
                if out.is_relative_to(protected) or protected.is_relative_to(out):
                    raise ValueError('--out overlaps existing game or normal user data')
            if out.exists() and any(out.iterdir()):
                raise ValueError('P2 test requires an empty output directory; never reuse saves')
            if args.capture_dir is not None and not args.capture_dir.is_relative_to(out):
                raise ValueError('P2 --capture-dir must stay inside --out')
            if len(str(out / 'user').encode('utf-8')) >= 512:
                raise ValueError('P2 user path exceeds the runtime limit of 511 UTF-8 bytes')
    except (ValueError, OSError) as error:
        parser.error(str(error))
    probe = args.runtime.resolve() if args.runtime is not None else runtime_path(PORT)
    if not args.analyze:
        if not probe.is_file():
            hint = '.\\build.ps1' if sys.platform == 'win32' else 'see docs/DESENVOLVIMENTO_LINUX.md'
            parser.error(f'Build the runtime first: {hint}')
        if sys.platform != 'win32' and not os.access(probe, os.X_OK):
            parser.error(f'Runtime is not executable: {probe}')
    profile = load_profile(game_dir=game, repo_root=ROOT)
    validate_game(game, profile)
    if args.language is not None and args.language not in discover_languages(game, title_id=profile['title_id']):
        raise ValueError('--language is not installed in this dump')
    if args.user_data is not None and not args.analyze:
        lifetime.enter_context(user_data_lock(user))
    out.mkdir(parents=True, exist_ok=True)
    images = {name: prepare_image(game, profile, name, out) for name in profile['executables']}
    if args.analyze:
        print(f'Analysis complete: {out}. Execution and visual boot remain unverified.')
        return 0
    env = runtime_environment(out, pad_file=args.pad_file, capture_dir=args.capture_dir,
                              local_users=args.local_users, second_user=args.p2_user_id,
                              config_file=args.config, language=args.language, profile=profile_id(profile))
    user.mkdir(parents=True, exist_ok=True)
    if not args.check_imports:
        for folder in images.values():
            rewrite = [sys.executable, str(PORT / 'scripts/isa_rewrite.py'), str(folder / 'boot-linked.bin'),
                       '--out', str(folder)]
            if sys.platform == 'win32':
                rewrite.append('--red-zone')
            subprocess.run(rewrite, check=True)
    session = out / ('session-' + time.strftime('%Y%m%d-%H%M%S'))
    session.mkdir()
    report = {'profile': profile_id(profile), 'mode': 'imports' if args.check_imports else 'run',
              'local_users': args.local_users, 'p2_user_id': args.p2_user_id, 'language': args.language,
              'user_data': str(user),
              'gpu_readbacks': int(env['TLG_READBACKS']), 'eager_readback': env['TLG_EAGER_READBACK'] == '1',
              'visual_verified': False, 'stages': []}
    deadline = time.monotonic() + args.timeout if args.timeout else None
    executable, guest_args = profile['entry'], []
    status = 0
    try:
        for stage in range(8):
            folder = images[executable]
            # Pass zero explicitly: omitting the guest flag would select its own default.
            remaining = max(1, int(deadline - time.monotonic())) if deadline is not None else 0
            command = [str(probe), str(folder / 'boot-linked.bin'), '--app0', str(game), '--user', str(user),
                       '--content-profile', str(folder / 'content.bin'), '--timeout', str(remaining),
                       '--guest-executable', '/app0/' + executable]
            if executable == 'eboot.bin':
                command.append('--cpu-only')
            if args.check_imports:
                command.append('--check-imports')
            else:
                command += ['--isa-rewrite', str(folder / 'isa-rewrite.bin')]
            stage_args = native_resolution_arguments(executable, guest_args, env['TLG_INTERNAL_RESOLUTION'])
            for value in stage_args:
                command += ['--guest-arg', value]
            log = session / f'{stage:02d}-{executable}.log'
            with tempfile.TemporaryDirectory(prefix='exec-', dir=session) as temporary:
                request = Path(temporary) / 'request.bin'
                child_env = {**env, 'KNACK2_EXEC_REQUEST': str(request)}
                print(f'Starting {executable}; log: {log}', flush=True)
                code = run_child(command, child_env, log, remaining + 5 if deadline is not None else None)
                report['stages'].append({'executable': executable, 'arguments': stage_args, 'exit_code': code,
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
                    if deadline is not None and time.monotonic() >= deadline:
                        return 124
                    continue
                if code == 76 and sys.platform != 'win32':
                    # Linux overlay restart: keep the same executable, arguments, profile lock and deadline.
                    if deadline is not None and time.monotonic() >= deadline:
                        return 124
                    env = runtime_environment(out, pad_file=args.pad_file, capture_dir=args.capture_dir,
                                              local_users=args.local_users, second_user=args.p2_user_id,
                                              config_file=args.config, language=args.language)
                    continue
                return code
        raise ValueError('LoadExec/restart transition limit reached')
    finally:
        (session / 'session.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(f'Session evidence: {session}', flush=True)


if __name__ == '__main__':
    # Windows launcher sends CTRL_BREAK to this process group. Raise instead of
    # terminating immediately, allowing run_child() to reap the guest and write evidence.
    if hasattr(signal, 'SIGBREAK'):
        signal.signal(signal.SIGBREAK, signal.default_int_handler)
    if sys.platform != 'win32':
        signal.signal(signal.SIGINT, signal.default_int_handler)
        signal.signal(signal.SIGTERM, signal.default_int_handler)
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, struct.error, subprocess.CalledProcessError) as error:
        print(f'Knack 2: {error}', file=sys.stderr)
        sys.exit(1)
