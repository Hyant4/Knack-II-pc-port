# SPDX-License-Identifier: GPL-2.0-or-later
"""Dump locales and the existing SystemService language contract. No Qt dependency.

IDs: OpenOrbis include/orbis/_types/sys_service.h, OrbisSystemParamLanguage.
Asset codes come from the inspected American and European dumps.
"""
from pathlib import Path

LANGUAGES = (
    {'code': 'pt-BR', 'label': 'Português (Brasil)', 'asset': 'pta', 'id': 17},
    {'code': 'en-US', 'label': 'English', 'asset': 'ena', 'id': 1},
    {'code': 'es-419', 'label': 'Español (Latinoamérica)', 'asset': 'esa', 'id': 20},
    {'code': 'fr-FR', 'label': 'Français', 'asset': 'fre', 'id': 2},
    {'code': 'pt-PT', 'label': 'Português (Portugal)', 'asset': 'pte', 'id': 7},
    {'code': 'en-GB', 'label': 'English (UK)', 'asset': 'ene', 'id': 18},
    {'code': 'es-ES', 'label': 'Español (España)', 'asset': 'ese', 'id': 3},
    {'code': 'de-DE', 'label': 'Deutsch', 'asset': 'deu', 'id': 4},
    {'code': 'it-IT', 'label': 'Italiano', 'asset': 'ita', 'id': 5},
    {'code': 'nl-NL', 'label': 'Nederlands', 'asset': 'nld', 'id': 6},
    {'code': 'ru-RU', 'label': 'Русский', 'asset': 'rus', 'id': 8},
    {'code': 'fi-FI', 'label': 'Suomi', 'asset': 'fin', 'id': 12},
    {'code': 'sv-SE', 'label': 'Svenska', 'asset': 'swe', 'id': 13},
    {'code': 'da-DK', 'label': 'Dansk', 'asset': 'dan', 'id': 14},
    {'code': 'nb-NO', 'label': 'Norsk', 'asset': 'nor', 'id': 15},
    {'code': 'pl-PL', 'label': 'Polski', 'asset': 'pol', 'id': 16},
    {'code': 'ar-SA', 'label': 'العربية', 'asset': 'ara', 'id': 21},
)
BY_CODE = {entry['code']: entry for entry in LANGUAGES}
REGION_LANGUAGES = {
    'CUSA08014': ('pt-BR', 'en-US', 'es-419', 'fr-FR'),
    'CUSA07670': ('fr-FR', 'pt-PT', 'en-GB', 'es-ES', 'de-DE', 'it-IT', 'nl-NL',
                  'ru-RU', 'fi-FI', 'sv-SE', 'da-DK', 'nb-NO', 'pl-PL', 'ar-SA'),
}


def language_environment(code):
    if code is None:
        return {}
    if not isinstance(code, str) or code not in BY_CODE:
        raise ValueError('Idioma indisponível: ' + str(code))
    return {'TLG_LANGUAGE': str(BY_CODE[code]['id'])}


def discover_languages(game, *, title_id=None):
    """Inspect installed language assets only. Never copy or edit the dump."""
    directory = Path(game) / 'data/lang'
    allowed = BY_CODE if title_id is None else REGION_LANGUAGES.get(title_id, ())
    return [entry['code'] for entry in LANGUAGES
            if entry['code'] in allowed and (directory / entry['asset']).is_dir() and
            any(p.is_file() for p in (directory / entry['asset']).iterdir())]
