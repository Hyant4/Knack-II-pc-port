# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit GTX 1060 experiments; never change the normal graphics defaults."""
from pathlib import Path

PRESET = 'gtx_1060_black_texture'
MODES = ('off', 'cache', 'cache_sync', 'capture', 'capture_linear')
CAPTURE_MARKER = 'KNACK2_BOUNDED_CAPTURE_V1=128MiB,13images,1frame'


def validate_mode(value):
    if not isinstance(value, str) or value not in MODES:
        raise ValueError('Diagnóstico GTX 1060 inválido.')
    return value


def create_environment(mode, session):
    """Create a fresh GPU-only root, leaving the guest user/save path alone."""
    validate_mode(mode)
    if mode == 'off':
        return {}
    session = Path(session).resolve(strict=True)
    if mode in ('capture', 'capture_linear'):
        capture = session / 'gtx1060-capture'
        capture.mkdir(exist_ok=False)
        return {'TLG_READBACK_LINEAR': '1' if mode == 'capture_linear' else '0',
                'TLG_KNACK_CAPTURE': '1', 'TLG_FRAME_CAPTURE_KEY': '1',
                'TLG_CAPTURE_GPU_FLIP': '1', 'TLG_CAPTURE_SUBMIT_BOUNDARY': '1',
                'TLG_CAPTURE_DIR': str(capture)}
    gpu_user = session / 'gtx1060-gpu'
    # Never reuse a directory, including a symlink to the habitual cache.
    gpu_user.mkdir(exist_ok=False)
    values = {'TLG_GPU_USER_DIR': str(gpu_user)}
    if mode == 'cache_sync':
        values.update(TLG_ASYNC_SHADERS='0', TLG_COMPUTE_ASYNC_BUILD='0')
    return values


def require_capture_runtime(probe):
    """Reject older binaries before enabling an image capture without verified limits."""
    import subprocess
    result = subprocess.run([str(probe), '--cpu-features'], capture_output=True,
                            text=True, encoding='utf-8', errors='replace', timeout=20)
    if result.returncode != 0 or CAPTURE_MARKER not in result.stdout.splitlines():
        raise ValueError('Runtime sem captura limitada. Use o pacote de diagnóstico completo.')
