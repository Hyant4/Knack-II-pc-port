"""Temporal FSR selection is validated and reaches the native profile; no game launch."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import config

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('temporal_supervisor', ROOT / 'run.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


class TemporalConfigTests(unittest.TestCase):
    def test_internal_resolution_roundtrip_and_validated_environment(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {
                'TLG_INTERNAL_RESOLUTION': '99999x99999', 'TLG_RENDER_RES': '3840x2160',
                'TLG_OUTPUT_RES': '3840x2160'}):
            root = Path(temporary)
            ini = root / 'knack2.ini'
            original = b'# keep\r\nngs2_render_mix=1\r\nwindow_resolution=2560x1440\r\n'
            ini.write_bytes(original)
            self.assertEqual(supervisor.runtime_environment(root)['TLG_INTERNAL_RESOLUTION'], 'auto')
            for size in config.VALID_INTERNAL_RESOLUTIONS:
                config.update_knack2_ini(ini, {'internal_resolution': size})
                self.assertEqual(config.load_knack2_ini(ini)['internal_resolution'], size)
                self.assertIn(original, ini.read_bytes())
                self.assertNotIn(b'\n', ini.read_bytes().replace(b'\r\n', b''))
                for profile in supervisor.SUPPORTED_PROFILES:
                    env = supervisor.runtime_environment(root, profile=profile)
                    self.assertEqual(env['TLG_INTERNAL_RESOLUTION'], size)
                    self.assertNotIn('TLG_RENDER_RES', env)
                    self.assertNotIn('TLG_OUTPUT_RES', env)
                    self.assertEqual(env['TLG_READBACKS'], '2')

    def test_bad_internal_resolution_never_writes_or_reaches_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            ini = root / 'knack2.ini'
            for bad in ('8K', '0x0', '3840x1080', '2560x1440\nfsr1=1',
                        '1920x1080\x00', 1080, True):
                original = b'# keep\ninternal_resolution=auto\n'
                ini.write_bytes(original)
                with self.subTest(value=bad), self.assertRaises(ValueError):
                    config.update_knack2_ini(ini, {'internal_resolution': bad})
                self.assertEqual(ini.read_bytes(), original)
            ini.write_text('internal_resolution=99999x99999\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                supervisor.runtime_environment(root)

    def test_validated_ini_wins_over_inherited_provider(self):
        with tempfile.TemporaryDirectory() as temporary, patch.dict(os.environ, {
                'TLG_UPSCALER': 'fsr4', 'TLG_UPSCALE_PRESET': '4', 'TLG_OBJECT_MOTION': '1'}):
            root = Path(temporary)
            self.assertEqual(supervisor.runtime_environment(root)['TLG_UPSCALER'], 'off')
            for provider in ('off', 'fsr3', 'fsr4'):
                for preset in range(4):
                    config.update_knack2_ini(root / 'knack2.ini', {'upscaler': provider, 'preset': preset})
                    for profile in supervisor.SUPPORTED_PROFILES:
                        with self.subTest(profile=profile, provider=provider, preset=preset):
                            env = supervisor.runtime_environment(root, profile=profile)
                            self.assertEqual(env['KNACK2_PROFILE'], profile)
                            self.assertEqual(env['TLG_UPSCALER'], provider)
                            self.assertEqual(env['TLG_UPSCALE_PRESET'], str(preset))
                            self.assertEqual(env['TLG_OBJECT_MOTION'], '0')
                            self.assertEqual(env['TLG_READBACKS'], '2')

    def test_invalid_provider_or_preset_does_not_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            ini = Path(temporary) / 'knack2.ini'
            original = b'# keep\r\nupscaler=off\r\nngs2_render_mix=1\r\n'
            ini.write_bytes(original)
            for key, values in (('upscaler', ('taa', 'dlss', 'fsr411', 'fsr4\ninjected=1')),
                                ('preset', ('-1', '4', '1.0', 'nan'))):
                for value in values:
                    with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                        config.update_knack2_ini(ini, {key: value})
                    self.assertEqual(ini.read_bytes(), original)

    def test_camera_and_motion_contract_in_native_header(self):
        toolchain = Path(os.environ.get('MSYS2_ROOT') or
            Path(os.environ.get('LOCALAPPDATA', '')) / 'knack2-toolchain/msys64') / 'clang64'
        compiler = toolchain / 'bin/clang++.exe'
        if not compiler.is_file(): self.skipTest('Existing clang++ toolchain unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / 'knack-temporal.exe'
            env = dict(os.environ, PATH=str(toolchain / 'bin') + os.pathsep + os.environ.get('PATH', ''))
            result = subprocess.run([str(compiler), '-std=c++23', '-I', str(ROOT / 'port/gpu/shadps4'),
                str(ROOT / 'port/tests/test_knack_temporal.cpp'), '-o', str(executable)],
                capture_output=True, text=True, env=env, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, env=env, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
