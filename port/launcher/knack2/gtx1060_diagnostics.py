# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit GTX 1060 experiments; never change the normal graphics defaults."""
from pathlib import Path

PRESET = 'gtx_1060_black_texture'
MODES = ('off', 'cache', 'cache_sync')


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
    gpu_user = session / 'gtx1060-gpu'
    # Never reuse a directory, including a symlink to the habitual cache.
    gpu_user.mkdir(exist_ok=False)
    values = {'TLG_GPU_USER_DIR': str(gpu_user)}
    if mode == 'cache_sync':
        values.update(TLG_ASYNC_SHADERS='0', TLG_COMPUTE_ASYNC_BUILD='0')
    return values
