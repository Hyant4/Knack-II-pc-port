"""Synthetic opt-in stable cohort selection contract, no game files required."""
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

class B2F2ACohortTests(unittest.TestCase):
    def test_opt_in_continuity_and_baseline_rollback(self):
        compiler = c_compiler()
        self.assertTrue(compiler.is_file(), f'Windows clang not found at {compiler}')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack-b2f2a-') as tmp:
            binary = Path(tmp) / 'ngs2-b2f2a.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I'+str(ROOT/'port/src'), '-I'+str(ROOT/'port'),
                       str(ROOT/'tests/test_ngs2_b2f2a.c'), '-pthread', '-lm',
                       '-o', str(binary)]
            compiled = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            ran = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('PASS: B2-F2A synthetic cohort', ran.stdout)

if __name__ == '__main__':
    unittest.main()
