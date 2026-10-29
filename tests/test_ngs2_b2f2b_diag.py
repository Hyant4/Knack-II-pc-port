"""Compile and execute passive B2-F2B NGS2 diagnostic regression test."""
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


class Ngs2B2F2BDiagnosticTests(unittest.TestCase):
    def test_passive_diagnostics_do_not_alter_mixer(self):
        compiler = c_compiler()
        self.assertTrue(compiler.is_file(), f'Windows clang not found: {compiler}')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack-b2f2b-diag-') as tmp:
            binary = Path(tmp) / 'ngs2-b2f2b-diag.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I'+str(ROOT/'port/src'), '-I'+str(ROOT/'port'),
                       str(ROOT/'tests/test_ngs2_b2f2b_diag.c'), '-pthread', '-lm',
                       '-o', str(binary)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: B2-F2B-DIAG passive classification', result.stdout)


if __name__ == '__main__':
    unittest.main()
