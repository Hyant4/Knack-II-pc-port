# SPDX-License-Identifier: GPL-2.0-or-later
"""Dump validation for the American and European Knack II 01.01 profiles.

Guarantees strict functional equivalence with `run.py`'s `validate_game()`:
1. Validates profile schema (rejects malformed, non-string, or incomplete profiles).
2. Selects an allowlisted profile from param.sfo TITLE_ID and APP_VER.
3. Confirms official SHA-256 digests for `eboot.bin` and `game.elf`.
4. Verifies required modules in `sce_module/` using link_modules.
5. Fail-closed behavior if prepare or link_modules components are unavailable.
"""

from __future__ import annotations
from .languages import discover_languages
from .paths import project_root

import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, Mapping

# Ensure port/scripts is importable for prepare and link_modules
_REPO_ROOT = project_root()
_SCRIPTS_DIR = _REPO_ROOT / 'port' / 'scripts'
if str(_SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS_DIR))

try:
    import prepare
    import link_modules
except ImportError:
    prepare = None  # type: ignore[assignment]
    link_modules = None  # type: ignore[assignment]

_HEX_64_RE = re.compile(r'^[0-9a-fA-F]{64}$')
SUPPORTED_PROFILES = ('CUSA08014-01.01', 'CUSA07670-01.01')


class KnackDumpError(Exception):
    """Base exception for Knack II dump validation errors."""


class KnackProfileError(KnackDumpError, ValueError):
    """Profile is missing, malformed, or missing required executables/modules."""


class KnackDumpNotFoundError(KnackDumpError, FileNotFoundError):
    """Dump directory or essential game file not found."""


class KnackVersionMismatchError(KnackDumpError, ValueError):
    """TITLE_ID or APP_VER does not match a supported profile."""


class KnackHashMismatchError(KnackDumpError, ValueError):
    """Executable SHA-256 digest differs from the official dump."""


class KnackModuleError(KnackDumpError, ValueError):
    """Required sce_module file missing or invalid ELF structure."""


def describe_validation_failure(error: BaseException) -> str:
    """Return a user-facing reason for a failed validation without hiding detail."""
    detail = str(error).strip() or type(error).__name__
    if isinstance(error, KnackDumpNotFoundError):
        reason = 'A pasta selecionada não contém todos os arquivos obrigatórios do dump.'
    elif isinstance(error, KnackVersionMismatchError):
        reason = 'O título ou a versão instalada não pertence a um perfil suportado pelo launcher.'
    elif isinstance(error, KnackHashMismatchError):
        reason = 'Um executável não corresponde ao dump validado para este perfil.'
    elif isinstance(error, KnackModuleError):
        reason = 'Um módulo obrigatório está ausente, corrompido ou não pôde ser lido.'
    elif isinstance(error, KnackProfileError):
        reason = 'O perfil local do launcher está incompleto ou incompatível.'
    else:
        reason = 'A verificação não conseguiu comprovar que este dump é compatível.'
    return f'Falha de validação: {detail}\nMotivo: {reason}'


def digest_file(path: Path | str) -> str:
    """Compute the SHA-256 digest of a file."""
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def default_profile_path(repo_root: Path | None = None) -> Path:
    """Return the default path to the CUSA08014 01.01 profile JSON."""
    root = repo_root or _REPO_ROOT
    return root / 'profiles' / 'CUSA08014-01.01.json'


def profile_id(profile: Mapping[str, Any]) -> str:
    """Return an exact allowlisted profile ID; never derive arbitrary file paths."""
    name = f"{profile.get('title_id')}-{profile.get('version')}"
    if name not in SUPPORTED_PROFILES:
        raise KnackProfileError(f'Perfil não suportado: {name}')
    return name


def validate_profile_schema(profile: Any) -> dict[str, Any]:
    """Validate that the profile dictionary contains the required schema and hashes.

    Ensures type safety without unexpected TypeErrors:
    - title_id: string exactly 'CUSA08014' or 'CUSA07670'
    - version: string exactly '01.01'
    - executables: dict containing 'eboot.bin' and 'game.elf' with string 64-char hex SHA-256
    - modules: dict containing module requirements for executables
    """
    if not isinstance(profile, dict):
        raise KnackProfileError(f'Perfil inválido: deve ser um objeto JSON (dicionário), obtido: {type(profile).__name__}')

    title_id = profile.get('title_id')
    version = profile.get('version')
    if not isinstance(title_id, str) or not title_id:
        raise KnackProfileError("Perfil malformado: 'title_id' ausente ou não é uma string válida")
    if not isinstance(version, str) or not version:
        raise KnackProfileError("Perfil malformado: 'version' ausente ou não é uma string válida")

    profile_id(profile)

    executables = profile.get('executables')
    if not isinstance(executables, dict) or not executables:
        raise KnackProfileError("Perfil malformado: 'executables' deve ser um dicionário não vazio")

    for required_exe in ('eboot.bin', 'game.elf'):
        if required_exe not in executables:
            raise KnackProfileError(f"Perfil incompleto: executável obrigatório '{required_exe}' ausente no perfil")
        exe_hash = executables[required_exe]
        # Validate type first before applying regex to prevent TypeError
        if not isinstance(exe_hash, str) or not _HEX_64_RE.match(exe_hash):
            raise KnackProfileError(
                f"Perfil malformado: hash de '{required_exe}' deve ser uma string SHA-256 hexadecimal de 64 caracteres"
            )

    modules = profile.get('modules')
    if not isinstance(modules, dict):
        raise KnackProfileError("Perfil malformado: 'modules' deve ser um dicionário mapeando executáveis para módulos")

    for required_exe in ('eboot.bin', 'game.elf'):
        exe_modules = modules.get(required_exe)
        if not isinstance(exe_modules, list) or not exe_modules:
            raise KnackProfileError(f"Perfil incompleto: lista de módulos de '{required_exe}' ausente ou vazia")
        for mod in exe_modules:
            if not isinstance(mod, str) or not mod:
                raise KnackProfileError(f"Perfil malformado: módulo inválido na lista de '{required_exe}': {mod!r}")

    return profile


def load_profile(profile_path: Path | str | None = None,
                 repo_root: Path | None = None, *,
                 game_dir: Path | str | None = None) -> dict[str, Any]:
    """Load a profile, selecting by SFO when no explicit profile path is given."""
    selected = None
    if profile_path is None and game_dir is not None:
        if prepare is None:
            raise KnackDumpError('Submódulo prepare indisponível para validação do dump (fail-closed)')
        game = Path(game_dir).resolve()
        if not game.is_dir():
            raise KnackDumpNotFoundError(f'Diretório do jogo não encontrado: {game}')
        sfo_path = game / 'sce_sys/param.sfo'
        if not sfo_path.is_file():
            raise KnackDumpNotFoundError(f'Arquivo de metadados não encontrado: {sfo_path}')
        try:
            values = prepare.load_sfo(sfo_path)
            selected = profile_id({'title_id': values.get('TITLE_ID'), 'version': values.get('APP_VER')})
        except (OSError, ValueError) as error:
            raise KnackVersionMismatchError(f'Instalação não suportada: {error}') from error
        path = (repo_root or _REPO_ROOT) / 'profiles' / (selected + '.json')
    else:
        path = Path(profile_path) if profile_path else default_profile_path(repo_root)
    if not path.is_file():
        raise KnackProfileError(f'Arquivo de perfil não encontrado: {path}')
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise KnackProfileError(f'Falha ao ler arquivo de perfil {path}: {error}') from error

    profile = validate_profile_schema(data)
    if selected is not None and profile_id(profile) != selected:
        raise KnackProfileError('Identidade do perfil difere do arquivo selecionado')
    return profile


def validate_knack2_dump(game_dir: Path | str,
                         profile: Mapping[str, Any] | None = None,
                         profile_path: Path | str | None = None,
                         repo_root: Path | None = None) -> dict[str, Any]:
    """Validate a Knack II dump folder with strict functional equivalence to run.py.

    Raises:
        KnackProfileError: Profile missing or malformed.
        KnackDumpNotFoundError: Directory, sfo, or executable missing.
        KnackVersionMismatchError: Title ID or version differs from profile.
        KnackHashMismatchError: SHA-256 hash does not match expected executable.
        KnackModuleError: Module missing, corrupt, or parser unavailable.
        KnackDumpError: Underlying tools unavailable (fail-closed).

    Returns:
        Structured dict with validation metadata.
    """
    if profile is not None:
        active_profile = validate_profile_schema(profile)
    else:
        active_profile = load_profile(profile_path, repo_root, game_dir=game_dir)

    # Fail-closed checks on required tooling
    if prepare is None:
        raise KnackDumpError('Submódulo prepare indisponível para validação do dump (fail-closed)')
    if link_modules is None:
        raise KnackModuleError('Submódulo link_modules indisponível para validação de módulos ELF (fail-closed)')

    game = Path(game_dir).resolve()
    if not game.is_dir():
        raise KnackDumpNotFoundError(f'Diretório do jogo não encontrado: {game}')

    # 1. SFO Validation
    sfo_path = game / 'sce_sys' / 'param.sfo'
    if not sfo_path.is_file():
        raise KnackDumpNotFoundError(f'Arquivo de metadados não encontrado: {sfo_path}')

    try:
        sfo_values = prepare.load_sfo(sfo_path)
    except Exception as error:
        raise KnackVersionMismatchError(f'param.sfo inválido ou corrompido: {error}') from error

    title_id = sfo_values.get('TITLE_ID')
    app_ver = sfo_values.get('APP_VER')
    expected_id = active_profile['title_id']
    expected_ver = active_profile['version']

    if (title_id, app_ver) != (expected_id, expected_ver):
        raise KnackVersionMismatchError(
            f'Este perfil requer {expected_id} versão {expected_ver} (obtido: {title_id} {app_ver})'
        )

    # 2. Executables Validation (eboot.bin and game.elf)
    executables = active_profile['executables']
    for name, expected_hash in executables.items():
        try:
            exe_path = prepare.executable_path(game, name)
        except Exception as error:
            raise KnackDumpNotFoundError(f'{name}: caminho inválido ou fora do dump: {error}') from error

        if not exe_path.is_file():
            raise KnackDumpNotFoundError(f'Executável ausente no dump: {exe_path}')

        actual_hash = digest_file(exe_path)
        if actual_hash.lower() != expected_hash.lower():
            raise KnackHashMismatchError(f'{name}: SHA-256 difere do dump suportado')

    # 3. Modules Validation (libc.prx, libSceFios2.prx, libSceJobManager.prx, etc.)
    modules_map = active_profile['modules']
    required_modules = {m for mods in modules_map.values() for m in mods}

    for mod_name in sorted(required_modules):
        mod_path = game / 'sce_module' / mod_name
        if not mod_path.is_file():
            raise KnackModuleError(f'Módulo essencial ausente: sce_module/{mod_name}')

        try:
            link_modules.module(mod_path)
        except Exception as error:
            raise KnackModuleError(f'Módulo inválido ou incompatível ({mod_name}): {error}') from error

    return {
        'valid': True,
        'title_id': title_id,
        'version': app_ver,
        'app_title': sfo_values.get('TITLE', 'Knack™ II'),
        'game_dir': str(game),
        'executables': list(executables.keys()),
        'modules': sorted(required_modules),
        'sfo_values': sfo_values,
        'languages': discover_languages(game, title_id=title_id),
    }
