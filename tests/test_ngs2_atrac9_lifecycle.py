"""Compile deterministic NGS2 HLE-2.2A1 cancellation and destruction races."""
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


class Ngs2Atrac9LifecycleTests(unittest.TestCase):
    def test_cancel_and_destroy_quiescence(self):
        compiler = c_compiler()
        self.assertTrue(compiler.exists(), 'MSYS2 CLANG64 is required for this native test')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack ngs2 lifecycle ') as temporary:
            temporary = Path(temporary)
            ini = temporary / 'knack2.ini'
            ini.write_text('ngs2_atrac9_hle=1\n', encoding='ascii')
            env['TLG_CONFIG'] = str(ini)
            exe = temporary / 'ngs2-lifecycle-test.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_ngs2_atrac9_lifecycle.c'),
                       '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: NGS2 HLE-2.2A1', result.stdout)


if __name__ == '__main__':
    unittest.main()
