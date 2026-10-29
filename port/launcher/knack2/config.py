# SPDX-License-Identifier: GPL-2.0-or-later
"""Configuration management for Knack II Launcher.

Handles:
1. Application settings stored in %APPDATA%/Knack2-PC/launcher_settings.json.
2. Non-destructive reading and atomic updating of out/knack2/knack2.ini.

Hardening:
- Restricts updates to strictly authorized launcher keys (ALLOWED_MANAGED_KEYS).
- Validates control characters and newlines BEFORE strip (injection prevention).
- Validates semantic domain/ranges for every managed key.
- Preserves native line endings (CRLF / LF), comments, and unmanaged keys (NGS2, GPU).
- Uses unique temp file names (PID + UUID) with atomic replacement to prevent collisions.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
from typing import Any, Mapping
import uuid

# Diagnostic overrides are opt-in. Missing/auto leaves the runtime default intact.
GPU_DIAGNOSTIC_ENV = {
    'pipeline_binaries': 'TLG_PIPELINE_BINARIES',
    'bp06': 'TLG_BP06',
    'compute_async_build': 'TLG_COMPUTE_ASYNC_BUILD',
}

# Keys that the Knack II launcher is authorized to modify in knack2.ini
ALLOWED_MANAGED_KEYS = frozenset({
    'display_mode',
    'window_resolution',
    'internal_resolution',
    'aspect_ratio',
    'upscaler',
    'antialiasing',
    'preset',
    'vsync',
    'fps_limit',
    'async_shaders',
    'fsr1',
    'rcas',
    'rcas_strength',
    'frames_queued',
    'gpu_readbacks',
    'eager_readback',
    'shader_preload',
    'key_map',
    'mouse_enabled',
    'mouse_sensitivity',
    'mouse_invert_y',
    'mouse_smoothing',
    'mouse_accel',
    'mouse_deadzone',
}) | frozenset(GPU_DIAGNOSTIC_ENV)

# Rejection of control characters and newline injection (checked on raw strings before strip)
_CONTROL_CHAR_RE = re.compile(r'[\x00-\x1f\x7f]')

VALID_DISPLAY_MODES = frozenset({'windowed', 'borderless', 'fullscreen'})
VALID_WINDOW_RESOLUTIONS = ('auto', '1280x720', '1600x900', '1920x1080', '2560x1440', '3840x2160',
                            '2560x1080', '3440x1440', '3840x1600', '5120x1440', '5120x2160', '7680x2160')
VALID_ASPECT_RATIOS = ('auto', '16:9', '21:9', '32:9')
VALID_INTERNAL_RESOLUTIONS = ('auto', '1280x720', '1600x900', '1920x1080', '2560x1440', '3840x2160')


def default_app_data_dir(env: Mapping[str, str] | None = None,
                         nt: bool | None = None) -> Path:
    """Return the base directory for Knack 2 PC user configuration."""
    environ = os.environ if env is None else env
    is_nt = (os.name == 'nt') if nt is None else nt
    if is_nt:
        appdata = environ.get('APPDATA')
        base = Path(appdata) if appdata else Path.home() / 'AppData' / 'Roaming'
        return base / 'Knack2-PC'
    xdg = environ.get('XDG_CONFIG_HOME')
    base = Path(xdg) if xdg else Path.home() / '.config'
    return base / 'knack2-pc'


def launcher_settings_path(env: Mapping[str, str] | None = None,
                           nt: bool | None = None) -> Path:
    """Return the path to the launcher settings JSON file."""
    return default_app_data_dir(env, nt=nt) / 'launcher_settings.json'


def load_launcher_settings(path: Path | str | None = None,
                           env: Mapping[str, str] | None = None) -> dict[str, Any]:
    """Load launcher settings from JSON file. Returns empty dict on missing/corrupt file."""
    target = Path(path) if path is not None else launcher_settings_path(env)
    try:
        data = json.loads(target.read_text(encoding='utf-8'))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError, TypeError):
        return {}


def save_launcher_settings(settings: Mapping[str, Any],
                           path: Path | str | None = None,
                           env: Mapping[str, str] | None = None) -> Path:
    """Save launcher settings atomically to JSON file using a unique temp file. Returns saved path."""
    target = Path(path) if path is not None else launcher_settings_path(env)
    target.parent.mkdir(parents=True, exist_ok=True)
    unique_suffix = f'{os.getpid()}-{uuid.uuid4().hex[:8]}'
    temp_path = target.with_name(f'{target.name}.tmp-{unique_suffix}')
    try:
        content = json.dumps(dict(settings), indent=2, ensure_ascii=False) + '\n'
        with temp_path.open('w', encoding='utf-8') as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp_path, target)
        return target
    except OSError as error:
        if temp_path.is_file():
            try:
                temp_path.unlink()
            except OSError:
                pass
        raise OSError(f'Failed to save launcher settings to {target}: {error}') from error


def _validate_bool_string(key: str, val_str: str) -> str:
    """Validate and normalize a boolean string representation ('0' or '1')."""
    lower = val_str.lower()
    if lower in ('1', 'true', 'yes', 'on'):
        return '1'
    if lower in ('0', 'false', 'no', 'off'):
        return '0'
    raise ValueError(f"Valor booleano inválido para '{key}': '{val_str}' (esperado 0/1)")


def _validate_int_range(key: str, val_str: str, min_val: int, max_val: int | None = None) -> str:
    """Validate integer range."""
    try:
        num = int(val_str)
    except ValueError as error:
        raise ValueError(f"Valor inteiro inválido para '{key}': '{val_str}'") from error
    if num < min_val or (max_val is not None and num > max_val):
        bounds = f'>= {min_val}' if max_val is None else f'{min_val}..{max_val}'
        raise ValueError(f"Valor fora do intervalo permitido para '{key}': {num} (esperado {bounds})")
    return str(num)


def _validate_float_range(key: str, val_str: str, min_val: float, max_val: float) -> str:
    """Validate float range."""
    try:
        num = float(val_str)
    except ValueError as error:
        raise ValueError(f"Valor decimal inválido para '{key}': '{val_str}'") from error
    if num < min_val or num > max_val:
        raise ValueError(f"Valor fora do intervalo permitido para '{key}': {num} (esperado {min_val}..{max_val})")
    return f'{num:.2f}' if '.' in val_str else str(num)


def sanitize_and_validate_key_value(raw_key: Any, raw_value: Any | None) -> tuple[str, str | None]:
    """Validate that the key is authorized and both key and value are semantic and injection-free.

    Checks control characters BEFORE strip.
    """
    if not isinstance(raw_key, str):
        raise ValueError(f'A chave de configuração deve ser uma string: {raw_key!r}')

    # Validate control characters on RAW key BEFORE strip
    if _CONTROL_CHAR_RE.search(raw_key):
        raise ValueError(f"Caracteres de controle inválidos na chave bruta: '{raw_key!r}'")

    key = raw_key.strip()
    if key not in ALLOWED_MANAGED_KEYS:
        raise ValueError(f"Chave de configuração não autorizada para o launcher: '{key}'")

    if raw_value is None:
        return key, None

    # Convert value to string representation
    if isinstance(raw_value, bool):
        raw_val_str = '1' if raw_value else '0'
    elif isinstance(raw_value, (int, float, str)):
        raw_val_str = str(raw_value)
    else:
        raise ValueError(f"Tipo de valor inválido para '{key}': {type(raw_value).__name__}")

    # Validate control characters on RAW value string BEFORE strip
    if _CONTROL_CHAR_RE.search(raw_val_str):
        raise ValueError(f"Caracteres de controle ou quebra de linha inválidos no valor bruto de '{key}'")

    val = raw_val_str.strip()

    # Semantic domain validation per managed key
    if key in GPU_DIAGNOSTIC_ENV:
        if val not in ('auto', '0', '1'):
            raise ValueError(f"Diagnóstico GPU inválido para '{key}': '{val}' (esperado auto/0/1)")
        return key, val

    if key == 'antialiasing':
        if val not in ('native', 'off'):
            raise ValueError(f"Suavização inválida: '{val}'")
        return key, val
    if key == 'upscaler':
        if val not in ('off', 'fsr3', 'fsr4'):
            raise ValueError(f"FSR temporal inválido: '{val}'")
        return key, val
    if key == 'preset':
        return key, _validate_int_range(key, val, 0, 3)

    if key == 'internal_resolution':
        if val not in VALID_INTERNAL_RESOLUTIONS:
            raise ValueError(f"Resolução interna inválida: '{val}'")
        return key, val

    if key == 'window_resolution':
        if val not in VALID_WINDOW_RESOLUTIONS:
            raise ValueError(f"Resolução de janela inválida: '{val}'")
        return key, val
    if key == 'aspect_ratio':
        if val not in VALID_ASPECT_RATIOS:
            raise ValueError(f"Proporção inválida: '{val}'")
        return key, val

    if key == 'display_mode':
        lower_mode = val.lower()
        if lower_mode not in VALID_DISPLAY_MODES:
            raise ValueError(f"Modo de exibição inválido: '{val}' (esperado windowed, borderless ou fullscreen)")
        return key, lower_mode

    if key in ('vsync', 'async_shaders', 'fsr1', 'rcas', 'mouse_enabled', 'mouse_invert_y', 'eager_readback',
               'shader_preload'):
        return key, _validate_bool_string(key, val)

    if key == 'gpu_readbacks':
        return key, _validate_int_range(key, val, 0, 2)

    if key == 'rcas_strength':
        return key, _validate_int_range(key, val, 0, 100)

    if key == 'fps_limit':
        return key, _validate_int_range(key, val, 0, None)

    if key == 'frames_queued':
        return key, _validate_int_range(key, val, 1, 4)

    if key == 'mouse_sensitivity':
        return key, _validate_float_range(key, val, 0.05, 10.0)

    if key == 'mouse_smoothing':
        return key, _validate_float_range(key, val, 0.0, 0.95)

    if key == 'mouse_accel':
        return key, _validate_float_range(key, val, 0.0, 2.0)

    if key == 'mouse_deadzone':
        return key, _validate_float_range(key, val, 0.0, 0.5)

    if key == 'key_map':
        if len(val) > 1024:
            raise ValueError("O mapeamento de teclado 'key_map' excede o limite máximo de 1024 caracteres")
        return key, val

    return key, val


def gpu_diagnostic_environment(settings: Mapping[str, Any]) -> dict[str, str]:
    """Return only validated, explicit diagnostic overrides for the child runtime."""
    overrides = {}
    for key, variable in GPU_DIAGNOSTIC_ENV.items():
        _, value = sanitize_and_validate_key_value(key, settings.get(key, 'auto'))
        if value not in (None, 'auto'):
            overrides[variable] = value
    return overrides


def validate_graphics_settings(settings: Mapping[str, Any]) -> None:
    """Reject incompatible native-AA requests before writing or starting the runtime."""
    aa = settings.get('antialiasing', 'native')
    if aa not in ('native', 'off'):
        raise ValueError(f"Suavização inválida: '{aa}'")
    if aa == 'off':
        if settings.get('upscaler', 'off') != 'off':
            raise ValueError('Para desligar a suavização nativa, desligue primeiro o FSR temporal.')
        if settings.get('internal_resolution', 'auto') not in ('auto', '1920x1080', '2560x1440', '3840x2160'):
            raise ValueError('Suavização desligada requer resolução interna 1080p, 1440p ou 4K.')


def load_knack2_ini(path: Path | str) -> dict[str, str]:
    """Read knack2.ini and return key-value pairs as strings.

    Unknown keys, NGS2 flags, and graphics settings are all parsed.
    Comments and blank lines are ignored for key lookup.
    """
    target = Path(path)
    if not target.is_file():
        raise FileNotFoundError(f'Configuration file not found: {target}')

    entries: dict[str, str] = {}
    lines = target.read_text(encoding='utf-8', errors='replace').splitlines()
    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith('#') or stripped.startswith(';'):
            continue
        if '=' in line:
            key, _, val = line.partition('=')
            entries[key.strip()] = val.strip()
    return entries


def update_knack2_ini(path: Path | str,
                      updates: Mapping[str, Any | None],
                      header_comment: str | None = None) -> Path:
    """Update knack2.ini non-destructively, preserving line endings, comments and unmanaged keys.

    Hardening:
    - Rejects any key not in ALLOWED_MANAGED_KEYS.
    - Validates control characters and newlines before strip (injection prevention).
    - Rejects semantically invalid values for each managed key.
    - Preserves native CRLF/LF line endings of the source file.
    - Writes to a uniquely named temp file and atomically replaces target on success.
    - Leaves original file untouched on any validation or I/O failure.
    """
    # 1. Strict validation of all updates before touching disk
    validated_updates: dict[str, str | None] = {}
    for raw_k, raw_v in updates.items():
        k, v = sanitize_and_validate_key_value(raw_k, raw_v)
        # The native bpNN parser treats nonnumeric values as false; never write "auto".
        validated_updates[k] = None if k in GPU_DIAGNOSTIC_ENV and v == 'auto' else v

    target = Path(path)
    newline = '\n'
    try:
        raw_bytes = target.read_bytes()
        # Detect line endings
        if b'\r\n' in raw_bytes:
            newline = '\r\n'
        raw_text = raw_bytes.decode('utf-8', errors='replace')
        lines = raw_text.splitlines()
    except FileNotFoundError:
        lines = [header_comment] if header_comment else ['# Knack II configuration']
    except OSError as error:
        raise OSError(f'Cannot read configuration {target}: {error}') from error

    pending = {}
    for line in lines:
        if '=' in line and not line.lstrip().startswith(('#', ';')):
            key, _, value = line.partition('=')
            pending[key.strip()] = value.strip()
    for key, value in validated_updates.items():
        if value is None:
            pending.pop(key, None)
        else:
            pending[key] = value
    validate_graphics_settings(pending)

    out: list[str] = []
    seen: set[str] = set()

    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith('#') or stripped.startswith(';'):
            out.append(line)
            continue
        if '=' in line:
            key, _, _ = line.partition('=')
            key = key.strip()
            if key in validated_updates:
                new_val = validated_updates[key]
                if key not in seen:
                    seen.add(key)
                    if new_val is not None:
                        out.append(f'{key}={new_val}')
                # Subsequent duplicate occurrences of updated key are dropped
            else:
                # Unmanaged keys (e.g. ngs2_*, lod_*, etc.) are preserved verbatim
                out.append(line)
        else:
            out.append(line)

    # Append any newly added keys
    for key, val in validated_updates.items():
        if val is not None and key not in seen:
            out.append(f'{key}={val}')
            seen.add(key)

    target.parent.mkdir(parents=True, exist_ok=True)
    unique_suffix = f'{os.getpid()}-{uuid.uuid4().hex[:8]}'
    temp_path = target.with_name(f'{target.name}.tmp-{unique_suffix}')
    try:
        content = newline.join(out) + newline
        with temp_path.open('w', encoding='utf-8', newline='') as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp_path, target)
        return target
    except OSError as error:
        if temp_path.is_file():
            try:
                temp_path.unlink()
            except OSError:
                pass
        raise OSError(f'Failed to atomically write configuration to {target}: {error}') from error
