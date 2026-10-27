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
from port.launcher.knack2.gpu_presets import GPU_PRESETS, gpu_preset_targets, matching_gpu_preset

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gpu_diagnostic_supervisor', ROOT / 'run.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)

CONTROLS = {
    'pipeline_binaries': 'TLG_PIPELINE_BINARIES',
    'bp06': 'TLG_BP06',
    'compute_async_build': 'TLG_COMPUTE_ASYNC_BUILD',
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
                         {'window_resolution': '3440x1440', 'upscaler': 'fsr3', 'gpu_readbacks': '2'})
        for profile in GPU_PRESETS:
            expected = {'gpu_readbacks', 'eager_readback'} if profile.get('preserve_diagnostics') else set(CONTROLS)
            self.assertEqual(set(gpu_preset_targets(profile['id'])), expected)
            self.assertTrue(all(profile[key] for key in ('label', 'when', 'changes')))

    def test_gtx1060_preset_changes_only_readbacks_and_reaches_runtime_after_save(self):
        original = {'gpu_readbacks': '0', 'eager_readback': '1', 'pipeline_binaries': '0',
                    'bp06': '1', 'compute_async_build': '0', 'upscaler': 'fsr3',
                    'internal_resolution': '2560x1440', 'shader_preload': '0'}
        settings = {**original, **gpu_preset_targets('gtx_1060_black_texture')}
        self.assertEqual(settings, {**original, 'gpu_readbacks': '2', 'eager_readback': '0'})
        self.assertEqual(matching_gpu_preset(settings), 'custom')
        ini = self.root / 'settings.ini'
        ini.write_bytes(b'# preserved\r\nngs2_render_mix=1\r\n')
        config.update_knack2_ini(ini, settings)
        self.assertTrue(ini.read_bytes().startswith(b'# preserved\r\nngs2_render_mix=1\r\n'))
        self.assertEqual(config.load_knack2_ini(ini), {**settings, 'ngs2_render_mix': '1'})
        with patch.dict(os.environ, {'TLG_READBACKS': '0', 'TLG_EAGER_READBACK': '1'}):
            env = supervisor.runtime_environment(self.root, config_file=ini)
        self.assertEqual(env['TLG_READBACKS'], '2')
        self.assertEqual(env['TLG_EAGER_READBACK'], '0')
        self.assertEqual(env['TLG_UPSCALER'], 'fsr3')
        # The preset is a manual readback action, not automatic GPU detection.
        self.assertEqual(matching_gpu_preset({**settings, **gpu_preset_targets('default')}), 'default')
        config.update_knack2_ini(ini, {'gpu_readbacks': '0', 'eager_readback': '1'})
        restored = supervisor.runtime_environment(self.root, config_file=ini)
        self.assertEqual(restored['TLG_READBACKS'], '0')
        self.assertEqual(restored['TLG_EAGER_READBACK'], '0')

    def test_manual_overrides_are_custom_and_unknown_profiles_are_rejected(self):
        self.assertEqual(matching_gpu_preset({}), 'default')
        self.assertEqual(matching_gpu_preset({'bp06': '1'}), 'custom')
        for preset in ('rx580', 'renderer_v2', 'invalid', None):
            with self.subTest(preset=preset), self.assertRaises(ValueError):
                gpu_preset_targets(preset)

    def test_gtx1060_selection_requires_explicit_id_and_matching_readbacks(self):
        settings = {'gpu_readbacks': '2', 'eager_readback': '0'}
        self.assertEqual(matching_gpu_preset(settings), 'default')
        self.assertEqual(matching_gpu_preset(settings, 'gtx_1060_black_texture'), 'gtx_1060_black_texture')
        for key, value in [('gpu_readbacks', '0'), ('eager_readback', '1')]:
            with self.subTest(key=key):
                self.assertEqual(matching_gpu_preset({**settings, key: value}, 'gtx_1060_black_texture'), 'default')
        for invalid in (None, 'invalid', [], {'id': 'gtx_1060_black_texture'}):
            with self.subTest(invalid=invalid):
                self.assertEqual(matching_gpu_preset(settings, invalid), 'default')

    def test_defaults_and_foreign_overrides_leave_runtime_defaults_intact(self):
        inherited = {variable: '0' for variable in CONTROLS.values()}
        inherited.update(ISS_PIPELINE_BINARIES='1', TLG_BP08='0')
        with patch.dict(os.environ, inherited):
            for contents in ('', ''.join(f'{key}=auto\n' for key in CONTROLS)):
                ini = self.root / 'settings.ini'
                ini.write_text(contents, encoding='utf-8')
                env = supervisor.runtime_environment(self.root, config_file=ini)
                for variable in inherited:
                    self.assertNotIn(variable, env)
                self.assertEqual(env['TLG_READBACKS'], '2')
                self.assertEqual(env['TLG_UPSCALER'], 'off')

    def test_each_explicit_control_reaches_child_without_enabling_others(self):
        ini = self.root / 'settings.ini'
        for key, variable in CONTROLS.items():
            for value in ('0', '1'):
                with self.subTest(key=key, value=value):
                    ini.write_text(f'{key}={value}\n', encoding='utf-8')
                    with patch.dict(os.environ, {variable: str(1 - int(value))}):
                        env = supervisor.runtime_environment(self.root, config_file=ini)
                    self.assertEqual(env[variable], value)
                    for other in set(CONTROLS.values()) - {variable}:
                        self.assertNotIn(other, env)

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
                                      'compute_async_build': '0'})
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
                             {'pipeline_binaries': '0', 'bp06': '1', 'compute_async_build': '0'})
        report, = (self.root / 'out').glob('session-*/session.json')
        evidence = json.loads(report.read_text(encoding='utf-8'))
        self.assertEqual(evidence['gpu_diagnostics'],
                         {'pipeline_binaries': '0', 'bp06': '1', 'compute_async_build': '0'})
        self.assertEqual([stage['exit_code'] for stage in evidence['stages']], [75, 3])
        self.assertIn('GPU diagnostic overrides: pipeline_binaries=0, bp06=1, compute_async_build=0',
                      output.getvalue())


if __name__ == '__main__':
    unittest.main()
