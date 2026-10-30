# SPDX-License-Identifier: GPL-2.0-or-later
"""Guided compatibility profiles for the existing Knack GPU diagnostic controls.

Target/matching approach follows HayatoG/iss_pc port/launcher/qt/gfxlogic.py.
These profiles adapt only controls supported here; they do not certify a GPU.
"""
from .config import GPU_DIAGNOSTIC_ENV, sanitize_and_validate_key_value

RX580_PRESET = 'rx580_barriers'
RX580_INACTIVE = {'rx580_preset': '0', 'barrier_merge': 'auto', 'barrier_trace': 'auto'}

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
        'id': 'rx580_barriers',
        'label': 'RX 580: compatibilidade experimental',
        'when': 'Teste para o crash no carregamento relatado na issue #20. Correção no Knack ainda não confirmada.',
        'changes': 'Junta barreiras de imagem equivalentes e confere formatos de vértices. Demais ajustes são preservados. Salve e reinicie o jogo para comparar.',
        'experimental': True,
        'preserve_diagnostics': True,
        'overrides': {'rx580_preset': '1', 'barrier_merge': '1'},
    },
    {
        'id': 'gtx_1060_black_texture',
        'label': 'GTX 1060: desempenho',
        'when': 'Ajustes relatados na issue #12 para GTX 1060 6 GB; o desempenho varia conforme a cena e o computador.',
        'changes': 'Usa 1080p interno, FSR temporal desligado e GPU Rápida, sem partículas da GPU. Reduz detalhes da destruição. Salve e reinicie o jogo.',
        'experimental': True,
        'preserve_diagnostics': True,
        'overrides': {'upscaler': 'off', 'internal_resolution': '1920x1080',
                      'gpu_readbacks': '0', 'eager_readback': '0'},
    },
)


def gpu_preset_targets(preset_id):
    """Validate targets; the GTX preset preserves unrelated advanced GPU settings."""
    profile = next((profile for profile in GPU_PRESETS if profile['id'] == preset_id), None)
    if profile is None:
        raise ValueError('Predefinição de compatibilidade inválida.')
    values = {} if profile.get('preserve_diagnostics') else dict.fromkeys(GPU_DIAGNOSTIC_ENV, 'auto')
    if preset_id != RX580_PRESET:
        values.update(RX580_INACTIVE)
    values.update(profile['overrides'])
    return dict(sanitize_and_validate_key_value(key, value) for key, value in values.items())


def gpu_settings_for_preset(settings, preset_id):
    """A session gets RX-specific behavior only from an explicit active selection."""
    values = dict(settings)
    if preset_id == RX580_PRESET:
        values['rx580_preset'] = '1'
    else:
        values.update(RX580_INACTIVE)
    return values


def matching_gpu_preset(settings, selected_id=None):
    """Honor an explicit compatible selection; never infer a GTX GPU from defaults."""
    selected = next((profile for profile in GPU_PRESETS if profile['id'] == selected_id), None)
    if selected_id == RX580_PRESET:
        # The preset remains selected during its bounded on/off diagnostic comparisons.
        return RX580_PRESET if settings.get('rx580_preset', '0') == '1' else 'custom'
    if selected is not None:
        targets = gpu_preset_targets(selected_id)
        if all(settings.get(key, 'auto') == value for key, value in targets.items()):
            return selected_id
    current = {key: settings.get(key, 'auto') for key in GPU_DIAGNOSTIC_ENV}
    current['rx580_preset'] = settings.get('rx580_preset', '0')
    return next((profile['id'] for profile in GPU_PRESETS
                 if not profile.get('preserve_diagnostics')
                 and gpu_preset_targets(profile['id']) == current), 'custom')
