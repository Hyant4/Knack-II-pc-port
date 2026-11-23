# SPDX-License-Identifier: GPL-2.0-or-later
"""GPU diagnostic launch contracts using temporary files and synthetic children only."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import config
from port.launcher.knack2.gpu_presets import (GPU_PRESETS, gpu_preset_targets, matching_gpu_preset,
                                            RX580_INACTIVE)

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gpu_diagnostic_supervisor', ROOT / 'run.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)

CONTROLS = {
    'pipeline_binaries': 'TLG_PIPELINE_BINARIES',
    'bp06': 'TLG_BP06',
    'compute_async_build': 'TLG_COMPUTE_ASYNC_BUILD',
    'barrier_merge': 'TLG_BARRIER_MERGE',
    'barrier_trace': 'TLG_BARRIER_TRACE',
}


class GpuDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knack-gpu-diagnostics-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_profiles_are_complete_and_reset_previous_diagnostic_choices(self):
        settings = {'window_resolution': '3440x1440', 'upscaler': 'fsr3', 'gpu_readbacks': '2'}
        settings.update(gpu_preset_targets('conservative'))
        self.assertEqual(matching_gpu_preset(settings), 'conservative')
        settings.update(gpu_preset_targets('amd_compat'))
        self.assertEqual(settings['compute_async_build'], 'auto')
        self.assertEqual(matching_gpu_preset(settings), 'amd_compat')
        settings.update(gpu_preset_targets('default'))
        self.assertEqual(matching_gpu_preset(settings), 'default')
        self.assertEqual({key: settings[key] for key in CONTROLS}, dict.fromkeys(CONTROLS, 'auto'))
        self.assertEqual({key: value for key, value in settings.items() if key not in CONTROLS},
                         {'window_resolution': '3440x1440', 'upscaler': 'fsr3', 'gpu_readbacks': '2',
                          'rx580_preset': '0'})
        for profile in GPU_PRESETS:
            expected = set(profile['overrides']) if profile.get('preserve_diagnostics') else set(CONTROLS)
            if profile['id'] != 'rx580_barriers':
                expected.update(RX580_INACTIVE)
            self.assertEqual(set(gpu_preset_targets(profile['id'])), expected)
            self.assertTrue(all(profile[key] for key in ('label', 'when', 'changes')))

    def test_gtx1060_performance_preserves_other_options_and_reaches_runtime(self):
        original = {'upscaler': 'fsr3', 'internal_resolution': '3840x2160', 'gpu_readbacks': '2',
                    'eager_readback': '1', 'preset': '1', 'window_resolution': '3440x1440',
                    'shader_preload': '0', 'async_shaders': '1', 'vsync': '0',
                    'antialiasing': 'off', 'bp06': '1'}
        gtx_profiles = [p for p in GPU_PRESETS if p['id'].startswith('gtx_1060')]
        self.assertEqual([p['id'] for p in gtx_profiles], ['gtx_1060_black_texture'])
        targets = gpu_preset_targets('gtx_1060_black_texture')
        self.assertEqual(targets, {'upscaler': 'off', 'internal_resolution': '1920x1080',
                                   'gpu_readbacks': '0', 'eager_readback': '0', **RX580_INACTIVE})
        settings = {**original, **targets}
        self.assertEqual({k: v for k, v in settings.items() if k not in targets},
                         {k: v for k, v in original.items() if k not in targets})
        self.assertEqual(matching_gpu_preset(settings, 'gtx_1060_black_texture'), 'gtx_1060_black_texture')
        self.assertNotEqual(matching_gpu_preset(settings), 'gtx_1060_black_texture')
        for key in ('upscaler', 'internal_resolution', 'gpu_readbacks', 'eager_readback'):
            with self.subTest(key=key):
                changed = {**settings, key: original[key]}
                self.assertNotEqual(matching_gpu_preset(changed, 'gtx_1060_black_texture'),
                                    'gtx_1060_black_texture')
        ini = self.root / 'performance.ini'
        config.update_knack2_ini(ini, settings)
        self.assertEqual(config.load_knack2_ini(ini),
                         {k: v for k, v in settings.items()
                          if not (k in CONTROLS and v == 'auto')})
        env = supervisor.runtime_environment(self.root, config_file=ini)
        self.assertEqual(env['TLG_UPSCALER'], 'off')
        self.assertEqual(env['TLG_INTERNAL_RESOLUTION'], '1920x1080')
        self.assertEqual(env['TLG_READBACKS'], '0')
        self.assertEqual(env['TLG_EAGER_READBACK'], '0')

    def test_manual_overrides_are_custom_and_unknown_profiles_are_rejected(self):
        self.assertEqual(matching_gpu_preset({}), 'default')
        self.assertEqual(matching_gpu_preset({'bp06': '1'}), 'custom')
        for preset in ('rx580', 'renderer_v2', 'invalid', None):
            with self.subTest(preset=preset), self.assertRaises(ValueError):
                gpu_preset_targets(preset)

    def test_rx580_is_opt_in_preserves_preferences_and_can_be_disabled_alone(self):
        original = {'internal_resolution': '2560x1440', 'gpu_readbacks': '2', 'upscaler': 'fsr3',
                    'bp06': '1', 'compute_async_build': '0', 'barrier_trace': '1'}
        targets = gpu_preset_targets('rx580_barriers')
        self.assertEqual(targets, {'rx580_preset': '1', 'barrier_merge': '1'})
        settings = {**original, **targets}
        self.assertEqual({k: v for k, v in settings.items() if k not in targets}, original)
        self.assertEqual(matching_gpu_preset(settings, 'rx580_barriers'), 'rx580_barriers')
        ini = self.root / 'rx580.ini'
        for choice in ('1', '0', 'auto'):
            config.update_knack2_ini(ini, {**settings, 'barrier_merge': choice})
            env = supervisor.runtime_environment(self.root, config_file=ini)
            if choice == 'auto':
                self.assertNotIn('TLG_BARRIER_MERGE', env)
            else:
                self.assertEqual(env['TLG_BARRIER_MERGE'], choice)
            self.assertEqual(env['TLG_BARRIER_TRACE'], '1')
            self.assertEqual(env['TLG_RX580_PRESET'], '1')
            self.assertEqual(env['TLG_READBACKS'], '2')
            self.assertEqual(env['TLG_INTERNAL_RESOLUTION'], '2560x1440')
        settings.update(gpu_preset_targets('default'))
        self.assertEqual(settings['barrier_merge'], 'auto')
        self.assertEqual(settings['barrier_trace'], 'auto')

    def test_gtx1060_selection_requires_explicit_id_and_matching_readbacks(self):
        settings = gpu_preset_targets('gtx_1060_black_texture')
        self.assertEqual(matching_gpu_preset(settings), 'default')
        self.assertEqual(matching_gpu_preset(settings, 'gtx_1060_black_texture'), 'gtx_1060_black_texture')
        for key, value in [('gpu_readbacks', '2'), ('eager_readback', '1'),
                           ('upscaler', 'fsr3'), ('internal_resolution', '2560x1440')]:
            with self.subTest(key=key):
                self.assertEqual(matching_gpu_preset({**settings, key: value}, 'gtx_1060_black_texture'), 'default')
        for invalid in (None, 'invalid', [], {'id': 'gtx_1060_black_texture'}):
            with self.subTest(invalid=invalid):
                self.assertEqual(matching_gpu_preset(settings, invalid), 'default')

    def test_defaults_and_foreign_overrides_leave_runtime_defaults_intact(self):
        inherited = {variable: '0' for variable in CONTROLS.values()}
        inherited.update(ISS_PIPELINE_BINARIES='1', TLG_BP08='0')
        inherited['TLG_RX580_PRESET'] = '1'
        with patch.dict(os.environ, inherited):
            for contents in ('', ''.join(f'{key}=auto\n' for key in CONTROLS)):
                ini = self.root / 'settings.ini'
                ini.write_text(contents, encoding='utf-8')
                env = supervisor.runtime_environment(self.root, config_file=ini)
                for variable in inherited:
                    if variable == 'TLG_PIPELINE_BINARIES':
                        self.assertEqual(env[variable], '0')  # Existing shader-preload startup policy.
                    else:
                        self.assertNotIn(variable, env)
                self.assertEqual(env['TLG_READBACKS'], '2')
                self.assertEqual(env['TLG_UPSCALER'], 'off')

    def test_each_explicit_control_reaches_child_without_enabling_others(self):
        ini = self.root / 'settings.ini'
        for key, variable in CONTROLS.items():
            for value in ('0', '1'):
                with self.subTest(key=key, value=value):
                    selection = 'rx580_preset=1\n' if key in ('barrier_merge', 'barrier_trace') else ''
                    ini.write_text(f'{selection}{key}={value}\n', encoding='utf-8')
                    with patch.dict(os.environ, {variable: str(1 - int(value))}):
                        env = supervisor.runtime_environment(self.root, config_file=ini)
                    self.assertEqual(env[variable], value)
                    for other in set(CONTROLS.values()) - {variable}:
                        if other == 'TLG_PIPELINE_BINARIES':
                            self.assertEqual(env[other], '0')
                        else:
                            self.assertNotIn(other, env)

    def test_explicit_pipeline_choice_overrides_preload_without_enabling_foreign_controls(self):
        ini = self.root / 'settings.ini'
        ini.write_text('shader_preload=1\npipeline_binaries=1\n', encoding='utf-8')
        env = supervisor.runtime_environment(self.root, config_file=ini)
        self.assertEqual(env['TLG_PIPELINE_BINARIES'], '1')
        self.assertEqual(env['TLG_PARALLEL_WARMUP'], '1')
        self.assertNotIn('TLG_BP06', env)
        self.assertNotIn('TLG_COMPUTE_ASYNC_BUILD', env)
        ini.write_text('shader_preload=0\n', encoding='utf-8')
        env = supervisor.runtime_environment(self.root, config_file=ini)
        self.assertNotIn('TLG_PIPELINE_BINARIES', env)

    def test_invalid_values_do_not_mutate_ini(self):
        ini = self.root / 'settings.ini'
        original = b'# keep\r\nngs2_render_mix=1\r\n'
        ini.write_bytes(original)
        for key in CONTROLS:
            for value in ('2', '-1', 'yes', '', 'auto\nTLG_BP08=0', '0\x00'):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    config.update_knack2_ini(ini, {key: value})
                self.assertEqual(ini.read_bytes(), original)
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ['settings.ini'])

    def test_default_removes_overrides_and_preserves_unrelated_ini(self):
        ini = self.root / 'settings.ini'
        original = b'# keep\r\nngs2_render_mix=1\r\n'
        ini.write_bytes(original + b'bp06=0\r\nbp06=1\r\npipeline_binaries=0\r\ncompute_async_build=0\r\n')
        config.update_knack2_ini(ini, {key: 'auto' for key in CONTROLS})
        self.assertEqual(ini.read_bytes(), original)

    def test_invalid_diagnostics_fail_before_preparation_or_output_writes(self):
        for explicit in (False, True):
            out = self.root / ('unwritten' if explicit else 'default-config')
            ini = self.root / 'settings.ini' if explicit else out / 'knack2.ini'
            ini.parent.mkdir(exist_ok=True)
            for key in CONTROLS:
                ini.write_text(f'{key}=invalid\n', encoding='utf-8')
                before = set(self.root.rglob('*'))
                arguments = ['--game-dir', str(self.root / 'dump'), '--out', str(out)]
                if explicit:
                    arguments += ['--config', str(ini)]
                with patch.object(supervisor, 'load_profile') as profile, \
                     patch.object(supervisor, 'prepare_image') as prepare, \
                     patch.object(supervisor, 'run_child') as child, \
                     contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    supervisor.main(arguments)
                profile.assert_not_called()
                prepare.assert_not_called()
                child.assert_not_called()
                self.assertEqual(set(self.root.rglob('*')), before)

    def test_choices_survive_loadexec_and_are_recorded(self):
        ini = self.root / 'settings.ini'
        config.update_knack2_ini(ini, {'pipeline_binaries': '0', 'bp06': '1',
                                      'compute_async_build': '0', 'barrier_merge': '1',
                                      'barrier_trace': '1', 'rx580_preset': '1'})
        ini.chmod(0o700)  # Synthetic runtime_path fixture; the subprocess itself is mocked.
        profile = json.loads((ROOT / 'profiles/CUSA07670-01.01.json').read_text(encoding='utf-8'))
        calls = []

        def child(command, env, log, seconds):
            calls.append((command, env.copy()))
            if len(calls) == 1:
                target = b'/app0/game.elf'
                Path(env['KNACK2_EXEC_REQUEST']).write_bytes(
                    b'K2EXEC01' + struct.pack('<I', len(target)) + target + struct.pack('<I', 0))
                return 75
            return 3  # Preserve the synthetic runtime failure, never replace it with success.

        output = io.StringIO()
        with patch.object(supervisor, 'runtime_path', return_value=ini), \
             patch.object(supervisor, 'load_profile', return_value=profile), \
             patch.object(supervisor, 'validate_game', return_value={
                 'executable_layout': 'elf-0b7843d3', 'game_elf_sha256': '0' * 64}), \
             patch.object(supervisor, 'prepare_image', side_effect=lambda g, p, name, out: out / name), \
             patch.object(supervisor.subprocess, 'run'), \
             patch.object(supervisor, 'run_child', side_effect=child), \
             contextlib.redirect_stdout(output):
            self.assertEqual(supervisor.main([
                '--game-dir', str(self.root / 'dump'), '--out', str(self.root / 'out'),
                '--config', str(ini)]), 3)
        self.assertEqual(len(calls), 2)
        for _, env in calls:
            self.assertEqual({key: env[variable] for key, variable in CONTROLS.items()},
                             {'pipeline_binaries': '0', 'bp06': '1', 'compute_async_build': '0',
                              'barrier_merge': '1', 'barrier_trace': '1'})
        report, = (self.root / 'out').glob('session-*/session.json')
        evidence = json.loads(report.read_text(encoding='utf-8'))
        self.assertEqual(evidence['gpu_diagnostics'],
                         {'pipeline_binaries': '0', 'bp06': '1', 'compute_async_build': '0',
                          'barrier_merge': '1', 'barrier_trace': '1'})
        self.assertTrue(evidence['rx580_preset'])
        self.assertEqual([stage['exit_code'] for stage in evidence['stages']], [75, 3])
        self.assertIn('GPU diagnostic overrides: pipeline_binaries=0, bp06=1, compute_async_build=0',
                      output.getvalue())

    def test_residual_rx_flags_and_inherited_marker_cannot_enable_preset(self):
        ini = self.root / 'residual.ini'
        for marker in ('0', None):
            settings = {'barrier_merge': '1', 'barrier_trace': '1'}
            if marker is not None:
                settings['rx580_preset'] = marker
            config.update_knack2_ini(ini, settings)
            with patch.dict(os.environ, {'TLG_RX580_PRESET': '1', 'TLG_BARRIER_MERGE': '1',
                                         'TLG_BARRIER_TRACE': '1'}):
                env = supervisor.runtime_environment(self.root, config_file=ini)
            for name in ('TLG_RX580_PRESET', 'TLG_BARRIER_MERGE', 'TLG_BARRIER_TRACE'):
                self.assertNotIn(name, env)
        before = ini.read_bytes()
        for value in ('auto', 'yes', '2', '1\nTLG_RX580_PRESET=1'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                config.update_knack2_ini(ini, {'rx580_preset': value})
            self.assertEqual(ini.read_bytes(), before)
if __name__ == '__main__':
    unittest.main()
