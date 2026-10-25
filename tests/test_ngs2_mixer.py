"""Compile and run the Knack 2 NGS2 HLE-2.2B real PCM mixer test suite."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')


class Ngs2MixerTests(unittest.TestCase):
    def test_all_twenty_five_mandatory_mixer_contracts(self):
        compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        self.assertTrue(compiler.exists(), 'MSYS2 CLANG64 is required for this native test')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack ngs2 mixer ') as temporary:
            exe = Path(temporary) / 'ngs2-mixer-test.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-Wframe-larger-than=65536',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_ngs2_mixer.c'), '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('ALL 25 MANDATORY TESTS PASSED CLEANLY!', result.stdout)


if __name__ == '__main__':
    unittest.main()
