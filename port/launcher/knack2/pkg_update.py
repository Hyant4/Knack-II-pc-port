# SPDX-License-Identifier: GPL-2.0-or-later
"""Local update installation with a durable undo journal; never touches user saves."""
from __future__ import annotations

from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import struct
import subprocess
import threading
import tempfile
import time
import uuid

from . import validator
from .paths import external_process_environment, project_root

MARGIN = 64 * 1024 * 1024
MAX_FILES = 200_000
MAX_UPDATE = 64 * 1024**3


class UpdateError(ValueError):
    pass


class UpdateCancelled(UpdateError):
    pass


def _linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def safe_path(root, name):
    """Reject Windows aliases and redirected ancestors on every supported host."""
    if not isinstance(name, str) or not name or '\\' in name:
        raise UpdateError('Caminho inválido no update.')
    parts = name.split('/')
    if PureWindowsPath(name).drive or any(
        p in ('', '.', '..') or p.endswith((' ', '.')) or
        any(c in p for c in ':<>"|?*') or any(ord(c) < 32 for c in p) or
        p.split('.')[0].upper() in {'CON', 'PRN', 'AUX', 'NUL',
                                    *(f'COM{i}' for i in range(1, 10)),
                                    *(f'LPT{i}' for i in range(1, 10))}
        for p in parts
    ):
        raise UpdateError('Caminho inválido no update: ' + name)
    root = Path(root)
    current = root
    for part in (None, *parts):
        if part is not None:
            current = current / part
        if _linked(current):
            raise UpdateError('Link ou junction não permitido: ' + name)
    if not current.resolve().is_relative_to(root.resolve()):
        raise UpdateError('Caminho fora da instalação: ' + name)
    return current


def _read(stream, offset, size, limit):
    if offset < 0 or size < 0 or offset + size > limit:
        raise UpdateError('PKG incompleto ou tabela fora dos limites.')
    stream.seek(offset)
    value = stream.read(size)
    if len(value) != size:
        raise UpdateError('Falha de leitura do PKG.')
    return value


def inspect_pkg(path, *, category='gp', cancel=None):
    """Read bounded PKG header/PSF metadata before invoking any extractor."""
    path = Path(path).resolve(strict=True)
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = _read(stream, 0, 0x1000, size)
        if header[:4] != b'\x7fCNT':
            raise UpdateError('Arquivo não é um PKG de PS4.')
        count = struct.unpack_from('>I', header, 0x10)[0]
        table = struct.unpack_from('>I', header, 0x18)[0]
        declared = struct.unpack_from('>Q', header, 0x430)[0]
        cache = struct.unpack_from('>I', header, 0x43c)[0]
        if declared != size or not 1 <= count <= 16384 or not 0 < cache <= 128 * 1024**2:
            raise UpdateError('PKG incompleto ou formato não suportado.')
        content_id = header[0x40:0x64].split(b'\0')[0].decode('ascii', errors='strict')
        entries = _read(stream, table, count * 32, size)
        sfo = None
        metadata_size = 0
        for i in range(count):
            entry, _, _, _, offset, length = struct.unpack_from('>6I', entries, i * 32)
            if offset + length > size or length > 32 * 1024**2:
                raise UpdateError('Entrada PKG fora dos limites suportados.')
            metadata_size += length
            if entry == 0x1000:
                if sfo is not None or length > 1024**2:
                    raise UpdateError('Metadados PKG duplicados ou grandes demais.')
                sfo = validator.prepare.sfo(_read(stream, offset, length, size))
        if category == 'gd' and (sfo is None or sfo.get('CATEGORY') != 'gd'):
            raise UpdateError('Selecione um PKG do jogo-base; updates e DLC não são aceitos.')
        if category not in ('gd', 'gp'):
            raise ValueError('Unsupported PKG inspection category')
        if sfo is None or sfo.get('CATEGORY') != category:
            raise UpdateError('Selecione um PKG de atualização; base e DLC não são aceitos.')
        title, version = sfo.get('TITLE_ID'), sfo.get('APP_VER')
        if not isinstance(title, str) or not re.fullmatch(r'CUSA\d{5}', title):
            raise UpdateError('TITLE_ID inválido no update.')
        if content_id[7:16] != title or sfo.get('CONTENT_ID', content_id) != content_id:
            raise UpdateError('CUSA/CONTENT_ID do PKG e param.sfo são inconsistentes.')
        validator.profile_id({'title_id': title, 'version': version})
        if cancel is None:
            digest = validator.digest_file(path)
        else:
            digest = hashlib.sha256()
            stream.seek(0)
            while chunk := stream.read(4 * 1024**2):
                if cancel.is_set():
                    raise UpdateCancelled('Extração do jogo cancelada.' if category == 'gd' else 'Atualização cancelada.')
                digest.update(chunk)
            digest = digest.hexdigest()
        return {'title_id': title, 'version': version, 'content_id': content_id,
                'pkg_size': size, 'metadata_size': metadata_size, 'sfo_values': sfo,
                'path': str(path), 'sha256': digest}


def _version(value):
    if not isinstance(value, str) or not re.fullmatch(r'\d{2}\.\d{2}', value):
        raise UpdateError('Versão inválida nos metadados.')
    return tuple(map(int, value.split('.')))


def check_identity(game, info):
    sfo = validator.prepare.load_sfo(safe_path(game, 'sce_sys/param.sfo'))
    if sfo.get('TITLE_ID') != info['title_id']:
        raise UpdateError(f"Update incompatível: jogo {sfo.get('TITLE_ID')}; "
                          f"PKG {info['title_id']}. Use atualização do mesmo CUSA.")
    old_content = sfo.get('CONTENT_ID')
    if old_content and old_content != info['content_id']:
        raise UpdateError('CONTENT_ID do jogo difere do update. Instalação preservada.')
    if _version(info['version']) <= _version(sfo.get('APP_VER')):
        raise UpdateError('Update já instalado ou mais antigo. Nenhum arquivo alterado.')
    return sfo


def transaction_root(game):
    game = Path(game).resolve()
    return game.parent / ('.knack-update-' + game.name)


@contextmanager
def game_lock(game):
    """Shared by launcher sessions and updater; OS releases it on process exit."""
    game = Path(game).resolve()
    if not game.is_dir() or _linked(game):
        raise UpdateError('Pasta do jogo inválida.')
    # Playing from a read-only dump must not require writing beside the game.
    key = hashlib.sha256(os.path.normcase(str(game)).encode('utf-8')).hexdigest()
    path = Path(tempfile.gettempdir()) / ('knack-ii-installation-' + key + '.lock')
    if _linked(path):
        raise UpdateError('Lock de instalação redirecionado.')
    with path.open('a+b') as stream:
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
            raise UpdateError('Instalação em uso por outro launcher ou atualização.') from error
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == 'nt':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def _journal(root, value):
    target = safe_path(root, 'journal.json')
    temporary = safe_path(root, 'journal.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, target)


def read_journal(game):
    root = transaction_root(game)
    if not root.exists():
        return None
    path = safe_path(root, 'journal.json')
    if not path.is_file() or path.stat().st_size > 64 * 1024**2:
        raise UpdateError('Área de atualização incompleta. Preserve-a e consulte o diagnóstico.')
    value = json.loads(path.read_text(encoding='utf-8'))
    if (value.get('schema') != 1 or value.get('game') != str(Path(game).resolve()) or
            value.get('state') not in ('extracting', 'prepared', 'applying', 'rolling_back', 'rolled_back', 'complete') or
            not isinstance(value.get('files'), list) or len(value['files']) > MAX_FILES):
        raise UpdateError('Registro de recuperação inválido. Nenhum arquivo alterado.')
    seen = set()
    for item in value['files']:
        name = item['path']
        safe_path(game, name)
        safe_path(root / 'backup', name)
        if name.casefold() in seen or not re.fullmatch(r'[0-9a-f]{64}', item['after']):
            raise UpdateError('Registro de recuperação inválido.')
        if item['before'] is not None and not re.fullmatch(r'[0-9a-f]{64}', item['before']):
            raise UpdateError('Registro de recuperação inválido.')
        seen.add(name.casefold())
    return value


def pending_update(game):
    record = read_journal(game)
    return bool(record and record['state'] not in ('complete', 'rolled_back'))


def assert_playable(game):
    if pending_update(game):
        raise UpdateError('Atualização interrompida. Restaure o backup na tela Instalação antes de jogar.')


def _cancel(event):
    if event.is_set():
        raise UpdateCancelled('Atualização cancelada.')


def extract_pkg(pkg, destination, cancel, progress, *, base=False):
    """The native helper has no permission to target the installed game directory."""
    name = 'KnackPkg.exe' if os.name == 'nt' else 'KnackPkg'
    helper = project_root() / 'port/out/knack-pkg-helper' / name
    if not helper.is_file():
        raise UpdateError('Extrator PKG ausente. Use um pacote do launcher com KnackPkg incluído.')
    log = destination.parent / 'extract.log'
    with log.open('wb') as stream:
        command = [str(helper), *(['--base'] if base else []), str(pkg), str(destination)]
        process = subprocess.Popen(command, stdout=stream,
                                   stderr=subprocess.STDOUT, env=external_process_environment(),
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        started = time.monotonic()
        last_progress = ''
        try:
            while process.poll() is None:
                if base and cancel.is_set():
                    raise UpdateCancelled('Extração do jogo cancelada.')
                _cancel(cancel)
                if base and time.monotonic() - started > 21600:
                    raise UpdateError('Extração do jogo excedeu seis horas.')
                if not base and time.monotonic() - started > 1800:
                    raise UpdateError('Extração excedeu 30 minutos. Instalação anterior preservada.')
                if base:
                    with log.open('rb') as reader:
                        reader.seek(max(0, log.stat().st_size - 4096))
                        matches = re.findall(rb'KNACK_PKG_PROGRESS_V1 (\d+) (\d+)\r?\n', reader.read())
                    if matches:
                        done, total = (int(value) for value in matches[-1])
                        message = f'Extraindo itens do jogo: {done}/{total}.'
                        if message != last_progress:
                            progress(message)
                            last_progress = message
                time.sleep(0.1)
            if process.returncode:
                detail = log.read_text(encoding='utf-8', errors='replace')[-1500:]
                raise UpdateError('Falha ao extrair PKG: ' + detail)
            if 'KNACK_PKG_COMPLETE_V1' not in log.read_text(encoding='utf-8', errors='replace'):
                raise UpdateError('Extrator não confirmou a conclusão da atualização.')
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()


class UpdateInstaller:
    def __init__(self, *, extract=extract_pkg, validate=validator.validate_knack2_dump):
        self.extract = extract
        self.validate = validate

    def preview(self, game, pkg):
        game = Path(game).resolve(strict=True)
        assert_playable(game)
        info = inspect_pkg(pkg)
        check_identity(game, info)
        return info

    def install(self, game, info, *, cancel=None, progress=lambda text: None):
        game = Path(game).resolve(strict=True)
        cancel = cancel or threading.Event()
        with game_lock(game):
            assert_playable(game)
            # The user reviews this exact package. Replacement after preview never applies.
            fresh = inspect_pkg(info['path'])
            if fresh != info:
                raise UpdateError('PKG mudou após a análise. Selecione o arquivo novamente.')
            check_identity(game, fresh)
            root = transaction_root(game)
            if root.exists():
                previous = read_journal(game)
                if previous['state'] not in ('complete', 'rolled_back'):
                    raise UpdateError('Restaure a atualização interrompida antes de tentar novamente.')
                # Keep previous backups rather than silently deleting recovery data.
                root.rename(root.with_name(root.name + '-history-' + uuid.uuid4().hex))
            if shutil.disk_usage(game).free < max(info['pkg_size'], info['metadata_size']) + MARGIN:
                raise UpdateError('Espaço insuficiente para iniciar extração do update.')
            root.mkdir()
            stage, backup = root / 'stage', root / 'backup'
            stage.mkdir()
            backup.mkdir()
            record = {'schema': 1, 'game': str(game), 'state': 'extracting',
                      'package_sha256': info['sha256'], 'files': [], 'directories': []}
            _journal(root, record)
            try:
                progress('Extraindo atualização. Instalação atual preservada.')
                self.extract(Path(info['path']), stage, cancel, progress)
                _cancel(cancel)
                # A write into the package during extraction cannot produce an accepted candidate.
                if validator.digest_file(info['path']) != info['sha256']:
                    raise UpdateError('PKG mudou durante a extração.')
                progress('Conferindo arquivos da atualização sobre a instalação atual.')
                entries = self._inventory(game, stage)
                candidate = validator.prepare.load_sfo(safe_path(stage, 'sce_sys/param.sfo'))
                if candidate != info['sfo_values']:
                    raise UpdateError('Metadados extraídos diferem do PKG analisado.')
                self.validate(game, overlay_dir=stage)
                record['files'] = entries
                record['state'] = 'prepared'
                _journal(root, record)
                # Originals move to backup on the same volume; their bytes are not copied.
                if shutil.disk_usage(game).free < MARGIN:
                    raise UpdateError('Espaço livre abaixo da margem para aplicar update.')
                _cancel(cancel)
                record['state'] = 'applying'
                _journal(root, record)
                progress('Aplicando atualização com backup dos arquivos substituídos.')
                for item in entries:
                    _cancel(cancel)
                    name = item['path']
                    target = safe_path(game, name)
                    source = safe_path(stage, name)
                    if (validator.digest_file(target) if target.is_file() else None) != item['before']:
                        raise UpdateError('Arquivo da instalação mudou: ' + name)
                    if validator.digest_file(source) != item['after']:
                        raise UpdateError('Arquivo temporário mudou: ' + name)
                    if item['before'] is not None:
                        undo = safe_path(backup, name)
                        undo.parent.mkdir(parents=True, exist_ok=True)
                        os.replace(target, undo)
                    missing = []
                    parent = target.parent
                    while parent != game and not parent.exists():
                        missing.append(parent.relative_to(game).as_posix())
                        parent = parent.parent
                    if missing:
                        record['directories'] += list(reversed(missing))
                        _journal(root, record)
                        target.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(source, target)
                _cancel(cancel)
                progress('Validando instalação atualizada.')
                result = self.validate(game)
                record['state'] = 'complete'
                _journal(root, record)
                self._clean_stage(root)
                progress('Atualização concluída. Backup preservado.')
                return result
            except BaseException as error:
                try:
                    self._restore(game, root, record)
                    self._clean_stage(root)
                except Exception as recovery:
                    raise UpdateError(f'Falha: {error}. Recuperação pendente: {recovery}. '
                                      f'Preserve a pasta {root}.') from error
                raise

    @staticmethod
    def _clean_stage(root):
        stage = safe_path(root, 'stage')
        if stage.exists():
            for directory, folders, files in os.walk(stage, followlinks=False):
                for name in folders + files:
                    safe_path(stage, (Path(directory) / name).relative_to(stage).as_posix())
            if stage.resolve().parent != root.resolve():
                raise UpdateError('Pasta temporária redirecionada.')
            shutil.rmtree(stage)

    @staticmethod
    def _inventory(game, stage):
        entries, seen, total = [], set(), 0
        for directory, folders, files in os.walk(stage, followlinks=False):
            for name in folders + files:
                path = Path(directory) / name
                relative = path.relative_to(stage).as_posix()
                safe_path(stage, relative)
                target = safe_path(game, relative)
                if relative.casefold() in seen:
                    raise UpdateError('Nomes de arquivos ambíguos no update.')
                seen.add(relative.casefold())
                if len(seen) > MAX_FILES:
                    raise UpdateError('Update contém arquivos demais.')
                if path.is_dir():
                    if target.exists() and not target.is_dir():
                        raise UpdateError('Conflito entre pasta e arquivo: ' + relative)
                    continue
                if not path.is_file() or (target.exists() and not target.is_file()):
                    raise UpdateError('Tipo de arquivo incompatível: ' + relative)
                total += path.stat().st_size
                if total > MAX_UPDATE:
                    raise UpdateError('Update excede limite de 64 GiB.')
                after = validator.digest_file(path)
                before = validator.digest_file(target) if target.is_file() else None
                # Identical files do not need replacement or backup.
                if before != after:
                    entries.append({'path': relative, 'before': before, 'after': after})
        return sorted(entries, key=lambda x: x['path'])

    @staticmethod
    def _restore(game, root, record):
        applying = record['state'] in ('applying', 'rolling_back', 'complete')
        record['state'] = 'rolling_back'
        _journal(root, record)
        if applying:
            for item in reversed(record['files']):
                name = item['path']
                target = safe_path(game, name)
                undo = safe_path(root / 'backup', name)
                current = validator.digest_file(target) if target.is_file() else None
                if undo.is_file():
                    if validator.digest_file(undo) != item['before']:
                        raise UpdateError('Backup alterado: ' + name)
                    if current not in (None, item['before'], item['after']):
                        raise UpdateError('Arquivo alterado após o update: ' + name)
                    target.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(undo, target)
                elif item['before'] is None:
                    if current == item['after']:
                        target.unlink()
                    elif current is not None:
                        raise UpdateError('Arquivo novo foi alterado: ' + name)
                elif current != item['before']:
                    raise UpdateError('Backup ausente: ' + name)
            for name in reversed(record.get('directories', [])):
                path = safe_path(game, name)
                if path.is_dir() and not any(path.iterdir()):
                    path.rmdir()
        record['state'] = 'rolled_back'
        _journal(root, record)

    def restore(self, game):
        game = Path(game).resolve(strict=True)
        with game_lock(game):
            record = read_journal(game)
            if not record or record['state'] == 'rolled_back':
                raise UpdateError('Nenhuma atualização para restaurar.')
            self._restore(game, transaction_root(game), record)
            self._clean_stage(transaction_root(game))
