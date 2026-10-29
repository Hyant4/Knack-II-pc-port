"""Compile and run the isolated NGS2 HLE-1 contract with synthetic data."""
import os
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')


class Ngs2HleTests(unittest.TestCase):
    def test_handles_commands_state_and_silent_render(self):
        compiler = c_compiler()
        self.assertTrue(compiler.exists(), 'MSYS2 CLANG64 is required for this native test')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        env.pop('TLG_CONFIG', None)
        with tempfile.TemporaryDirectory(prefix='knack ngs2 test ') as temporary:
            exe = Path(temporary) / 'ngs2-hle-test.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_ngs2_hle.c'), '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: NGS2 HLE-1', result.stdout)


if __name__ == '__main__':
    unittest.main()
