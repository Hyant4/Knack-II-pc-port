# SPDX-License-Identifier: GPL-2.0-or-later
"""Shared game progress for solo and coop; session outputs stay separate."""
from contextlib import contextmanager
import os
from pathlib import Path


def application_data_root():
    if os.name == 'nt':
        return Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData/Local') / 'KnackII-PC'
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'knack-ii-pc'


def profile_user(root, players):
    if players not in (1, 2):
        raise ValueError('Selecione um ou dois jogadores.')
    root = Path(root).resolve()
    shared = root / 'profiles/shared/user'
    if not _has_savedata(shared):
        for mode in ('single', 'coop'):
            legacy = root / 'profiles' / mode / 'user'
            if legacy.resolve() != shared.resolve() and _has_savedata(legacy):
                raise ValueError('Foram encontrados saves antigos de solo/coop. '
                                 'Importe o save que deseja manter antes de jogar; '
                                 'nenhum arquivo foi sobrescrito.')
    return shared


def _has_savedata(user):
    savedata = user / 'savedata'
    return savedata.is_dir() and any(path.is_file() for path in savedata.rglob('*'))


@contextmanager
def user_data_lock(user):
    """Hold an OS lock through all LoadExec stages; process exit releases ownership."""
    user = Path(user)
    user.mkdir(parents=True, exist_ok=True)
    with (user / '.session.lock').open('a+b') as stream:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b'\0')
            stream.flush()
        stream.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise ValueError('Este perfil de saves já está em uso. Encerre a outra sessão.') from error
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == 'nt':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
