"""HLE-2.2D-B0: passive NGS2 diagnostics and no audio behavior regression."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')


class Ngs2B0DiagnosticTests(unittest.TestCase):
    def test_passive_diagnostics(self):
        compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        self.assertTrue(compiler.is_file(), f'MSYS2 CLANG64 compiler not found: {compiler}')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack-ngs2-b0-') as temporary:
            exe = Path(temporary) / 'ngs2-b0.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_ngs2_b0_diag.c'),
                       '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('ALL HLE-2.2D-B0 DIAGNOSTIC TESTS PASSED', result.stdout)


if __name__ == '__main__':
    unittest.main()
