# SPDX-License-Identifier: GPL-2.0-or-later
"""Dump locales and the existing SystemService language contract. No Qt dependency.

IDs: OpenOrbis include/orbis/_types/sys_service.h, OrbisSystemParamLanguage.
Asset codes are the directories present in this CUSA08014 dump, not inferred from SFO titles.
"""
from pathlib import Path

LANGUAGES = (
    {'code': 'pt-BR', 'label': 'Português (Brasil)', 'asset': 'pta', 'id': 17},
    {'code': 'en-US', 'label': 'English', 'asset': 'ena', 'id': 1},
    {'code': 'es-419', 'label': 'Español (Latinoamérica)', 'asset': 'esa', 'id': 20},
    {'code': 'fr-FR', 'label': 'Français', 'asset': 'fre', 'id': 2},
)
BY_CODE = {entry['code']: entry for entry in LANGUAGES}


def language_environment(code):
    if code is None:
        return {}
    if not isinstance(code, str) or code not in BY_CODE:
        raise ValueError('Idioma indisponível: ' + str(code))
    return {'TLG_LANGUAGE': str(BY_CODE[code]['id'])}


def discover_languages(game):
    """Inspect installed language assets only. Never copy or edit the dump."""
    directory = Path(game) / 'data/lang'
    return [entry['code'] for entry in LANGUAGES
            if (directory / entry['asset']).is_dir() and
            any(p.is_file() for p in (directory / entry['asset']).iterdir())]
