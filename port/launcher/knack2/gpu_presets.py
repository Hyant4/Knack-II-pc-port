# SPDX-License-Identifier: GPL-2.0-or-later
"""Guided compatibility profiles for the existing Knack GPU diagnostic controls.

Target/matching approach follows HayatoG/iss_pc port/launcher/qt/gfxlogic.py.
These profiles adapt only controls supported here; they do not certify a GPU.
"""
from .config import GPU_DIAGNOSTIC_ENV, sanitize_and_validate_key_value

GPU_PRESETS = (
    {
        'id': 'default',
        'label': 'Padrão do port',
        'when': 'Comece aqui se o jogo abre e funciona normalmente.',
        'changes': 'Usa os padrões de compatibilidade do port e remove os ajustes de teste.',
        'experimental': False,
        'overrides': {},
    },
    {
        'id': 'amd_compat',
        'label': 'AMD: falha ao iniciar',
        'when': 'Teste se o jogo fecha logo após clicar em Jogar em uma placa AMD.',
        'changes': 'Desativa duas opções avançadas para comparar a inicialização. Pode aumentar pausas e alterar efeitos visuais.',
        'experimental': True,
        'overrides': {'pipeline_binaries': '0', 'bp06': '0'},
    },
    {
        'id': 'conservative',
        'label': 'Inicialização conservadora',
        'when': 'Teste se o perfil AMD não ajudar ou se outra GPU fechar ao iniciar.',
        'changes': 'Também desliga a compilação compute em segundo plano. Pode causar mais pausas ao carregar conteúdo.',
        'experimental': True,
        'overrides': {'pipeline_binaries': '0', 'bp06': '0', 'compute_async_build': '0'},
    },
    {
        'id': 'gtx_1060_black_texture',
        'label': 'GTX 1060 black texture',
        'when': 'Teste superfícies pretas na GTX 1060. Correção ainda não confirmada.',
        'changes': 'Ativa leitura precisa da GPU e desliga leitura antecipada. Salve e reinicie o jogo; demais ajustes são preservados.',
        'experimental': True,
        'preserve_diagnostics': True,
        'overrides': {'gpu_readbacks': '2', 'eager_readback': '0'},
    },
)


def gpu_preset_targets(preset_id):
    """Validate targets; readback presets leave the diagnostic selection intact."""
    profile = next((profile for profile in GPU_PRESETS if profile['id'] == preset_id), None)
    if profile is None:
        raise ValueError('Predefinição de compatibilidade inválida.')
    values = {} if profile.get('preserve_diagnostics') else dict.fromkeys(GPU_DIAGNOSTIC_ENV, 'auto')
    values.update(profile['overrides'])
    return dict(sanitize_and_validate_key_value(key, value) for key, value in values.items())


def matching_gpu_preset(settings):
    """Match diagnostic profiles; applying readbacks does not identify a GPU or profile."""
    current = {key: settings.get(key, 'auto') for key in GPU_DIAGNOSTIC_ENV}
    return next((profile['id'] for profile in GPU_PRESETS
                 if not profile.get('preserve_diagnostics')
                 and gpu_preset_targets(profile['id']) == current), 'custom')
