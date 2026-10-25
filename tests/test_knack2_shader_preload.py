"""Synthetic preload preference and launch-contract tests; no game or GPU."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from port.launcher.knack2 import config

spec = importlib.util.spec_from_file_location('preload_supervisor', ROOT / 'run.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


class ShaderPreloadTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='knack-preload-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_existing_configuration_without_preference_enables_preload(self):
        ini = self.root / 'old.ini'
        ini.write_text('gpu_readbacks=2\neager_readback=0\n')
        env = supervisor.runtime_environment(self.root / 'out', config_file=ini)
        self.assertEqual(env['TLG_PIPELINE_BINARIES'], '0')
        self.assertEqual(env['TLG_BACKGROUND_WARMUP'], '0')
        self.assertEqual(env['TLG_PARALLEL_WARMUP'], '1')
        self.assertEqual(env['TLG_READBACKS'], '2')
        self.assertEqual(env['TLG_EAGER_READBACK'], '0')
        self.assertNotIn('TLG_ASYNC_SHADERS', env)

    def test_disabled_preload_restores_defaults_and_filters_inherited_switches(self):
        ini = self.root / 'off.ini'
        ini.write_text('shader_preload=0\n')
        inherited = {'TLG_PIPELINE_BINARIES': '0', 'TLG_BACKGROUND_WARMUP': '0',
                     'TLG_PARALLEL_WARMUP': '1', 'TLG_ASYNC_SHADERS': '1'}
        with patch.dict(os.environ, inherited):
            env = supervisor.runtime_environment(self.root / 'out', config_file=ini)
        for key in inherited:
            self.assertNotIn(key, env)

    def test_preload_validation_and_non_destructive_persistence(self):
        ini = self.root / 'settings.ini'
        original = b'# retained\r\nngs2_atrac9_hle=1\r\n'
        ini.write_bytes(original)
        config.update_knack2_ini(ini, {'shader_preload': False})
        self.assertTrue(ini.read_bytes().startswith(original))
        self.assertEqual(config.load_knack2_ini(ini)['shader_preload'], '0')
        for invalid in ('2', 'sometimes', '1\nTLG_ASYNC_SHADERS=1'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                config.sanitize_and_validate_key_value('shader_preload', invalid)

    def test_invalid_ini_preference_is_rejected_before_runtime(self):
        ini = self.root / 'bad.ini'
        ini.write_text('shader_preload=2\n')
        with self.assertRaises(ValueError):
            supervisor.runtime_environment(self.root / 'out', config_file=ini)


if __name__ == '__main__':
    unittest.main()
