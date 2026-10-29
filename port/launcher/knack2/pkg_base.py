# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract a base game beside its PKG, without replacing existing files."""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import shutil
import threading

from . import pkg_update as update, validator

AREA = '.knack-base-extraction'
GAME_ROOTS = ('eboot.bin', 'game.elf', 'data', 'sce_sys', 'sce_module')


def _cancel(event):
    if event.is_set():
        raise update.UpdateCancelled('Extração do jogo cancelada.')


def _identity(path):
    value = path.stat(follow_symlinks=False)
    return [value.st_dev, value.st_ino]


def _rename(source, target):
    """Atomically refuse an existing destination, including a concurrent writer."""
    if os.name == 'nt':
        os.rename(source, target)
        return
    libc = ctypes.CDLL(None, use_errno=True)
    function = getattr(libc, 'renameat2', None)
    if function is None:
        raise update.UpdateError('Renomeação sem sobrescrita indisponível neste sistema.')
    function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    function.restype = ctypes.c_int
    if function(-100, os.fsencode(source), -100, os.fsencode(target), 1):
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(target))


def _area(destination):
    return update.safe_path(destination, AREA)


def _cleanup(destination):
    root = _area(destination)
    # Only this fixed, application-owned child may be removed recursively.
    if root.parent != destination or root.name != AREA:
        raise update.UpdateError('Área de extração inválida.')
    shutil.rmtree(root)


def _recover(destination):
    """Undo interrupted publication by file identity; never overwrite foreign data."""
    root = _area(destination)
    if not root.exists():
        return
    journal = update.safe_path(root, 'journal.json')
    try:
        if journal.stat().st_size > 1024 * 1024:
            raise ValueError()
        record = json.loads(journal.read_text(encoding='utf-8'))
        if (record.get('schema') != 1 or record.get('destination') != str(destination) or
                record.get('state') not in ('extracting', 'publishing', 'complete') or
                not isinstance(record.get('roots'), list) or len(record['roots']) > update.MAX_FILES):
            raise ValueError()
        for entry in record['roots']:
            name = entry['name']
            if '/' in name or not isinstance(entry.get('identity'), list) or len(entry['identity']) != 2:
                raise ValueError()
            update.safe_path(destination, name)
            update.safe_path(root / 'payload', name)
    except (OSError, ValueError, KeyError, TypeError):
        raise update.UpdateError(f'Extração anterior incompleta. Preserve esta pasta: {root}') from None
    if record['state'] == 'publishing':
        for entry in reversed(record['roots']):
            source = update.safe_path(destination, entry['name'])
            target = update.safe_path(root / 'payload', entry['name'])
            if target.exists():
                if _identity(target) != entry['identity']:
                    raise update.UpdateError(f'Extração anterior incompleta. Preserve esta pasta: {root}')
                continue
            if not source.exists() or _identity(source) != entry['identity']:
                raise update.UpdateError(f'Extração anterior incompleta. Preserve esta pasta: {root}')
            _rename(source, target)
    _cleanup(destination)


def _check_conflicts(destination, names=GAME_ROOTS):
    existing = {p.name.casefold() for p in destination.iterdir()}
    for name in names:
        update.safe_path(destination, name)
        if name.casefold() in existing:
            raise update.UpdateError(f'Já existe no destino: {destination / name}. Nenhum arquivo foi sobrescrito.')


class BaseExtractor:
    def __init__(self, *, extract=None, validate=validator.validate_knack2_dump):
        self.extract = extract or (lambda pkg, stage, cancel, progress:
                                   update.extract_pkg(pkg, stage, cancel, progress, base=True))
        self.validate = validate

    def preview(self, pkg, *, cancel=None):
        original = Path(pkg).absolute()
        if update._linked(original):
            raise update.UpdateError('PKG-base não pode ser um link ou junction.')
        info = update.inspect_pkg(original, category='gd', cancel=cancel)
        destination = Path(info['path']).parent
        with update.game_lock(destination):
            _recover(destination)
            _check_conflicts(destination)
        info['destination'] = str(destination)
        return info

    def install(self, info, *, cancel=None, progress=lambda message: None):
        cancel = cancel if cancel is not None else threading.Event()
        pkg = Path(info['path'])
        destination = pkg.parent.resolve(strict=True)
        if info.get('destination') != str(destination):
            raise update.UpdateError('Destino de extração deve ser a pasta do PKG.')
        with update.game_lock(destination):
            _recover(destination)
            _check_conflicts(destination)
            _cancel(cancel)
            progress('Conferindo o PKG-base…')
            fresh = update.inspect_pkg(pkg, category='gd', cancel=cancel)
            if fresh != {k: v for k, v in info.items() if k != 'destination'}:
                raise update.UpdateError('PKG-base alterado após a confirmação.')
            if shutil.disk_usage(destination).free < max(fresh['pkg_size'], fresh['metadata_size']) + update.MARGIN:
                raise update.UpdateError('Espaço insuficiente para extrair o jogo ao lado do PKG.')
            root = _area(destination)
            root.mkdir()
            record = {'schema': 1, 'destination': str(destination), 'state': 'extracting', 'roots': []}
            update._journal(root, record)
            payload = root / 'payload'
            payload.mkdir()
            try:
                progress('Extraindo jogo-base na pasta do PKG…')
                self.extract(pkg, payload, cancel, progress)
                _cancel(cancel)
                progress('Conferindo arquivos extraídos…')
                after = update.inspect_pkg(pkg, category='gd', cancel=cancel)
                if after != fresh:
                    raise update.UpdateError('PKG-base alterado durante a extração.')
                result = self.validate(payload)
                if (result.get('title_id'), result.get('version')) != (info['title_id'], info['version']):
                    raise update.UpdateError('Identidade extraída difere do PKG-base.')
                roots = sorted(payload.iterdir(), key=lambda p: p.name.casefold())
                if not roots or len(roots) > update.MAX_FILES:
                    raise update.UpdateError('Conteúdo extraído inválido.')
                seen = set()
                for path in roots:
                    update.safe_path(payload, path.name)
                    if path.name.casefold() in seen or path.name.casefold() == AREA.casefold():
                        raise update.UpdateError('Conteúdo extraído inválido.')
                    seen.add(path.name.casefold())
                # Validate every descendant before publishing any top-level directory.
                count, total = 0, 0
                for base, directories, files in os.walk(payload, followlinks=False):
                    for name in (*directories, *files):
                        path = Path(base) / name
                        update.safe_path(payload, path.relative_to(payload).as_posix())
                        count += 1
                        if path.is_file():
                            total += path.stat().st_size
                        elif not path.is_dir():
                            raise update.UpdateError('Conteúdo extraído inválido.')
                        if count > update.MAX_FILES or total > update.MAX_UPDATE:
                            raise update.UpdateError('Conteúdo extraído inválido.')
                _check_conflicts(destination, [p.name for p in roots])
                _cancel(cancel)
                record.update(state='publishing', roots=[{'name': p.name, 'identity': _identity(p)} for p in roots])
                update._journal(root, record)
                progress('Concluindo extração na pasta do PKG…')
                for path in roots:
                    _cancel(cancel)
                    _rename(path, update.safe_path(destination, path.name))
                record['state'] = 'complete'
                update._journal(root, record)
            except BaseException:
                _recover(destination)
                raise
            _cleanup(destination)
            return {**result, 'destination': str(destination), 'files': count, 'bytes': total}
