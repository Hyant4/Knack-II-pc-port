"""Build and run NGS2 HLE-2.2A1 against a temporary user-provided ATRAC9 fixture."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
FIXTURE_ENV = 'KNACK2_ATRAC9_FIXTURE'


class Ngs2Atrac9WorkerTests(unittest.TestCase):
    def test_continuity_independence_bounds_and_silence(self):
        compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        fixture = Path(os.environ.get(FIXTURE_ENV, ''))
        self.assertTrue(compiler.exists(), 'MSYS2 CLANG64 is required for this native test')
        self.assertTrue(fixture.is_file(), f'{FIXTURE_ENV} must name the 6144-byte temporary fixture')
        sources = sorted((ROOT / 'port/third_party/LibAtrac9/C/src').glob('*.c'))
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack ngs2 atrac9 ') as temporary:
            temporary = Path(temporary)
            ini = temporary / 'knack2.ini'
            ini.write_text('ngs2_atrac9_hle=1\n', encoding='ascii')
            env['TLG_CONFIG'] = str(ini)
            exe = temporary / 'ngs2-atrac9-test.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_ngs2_atrac9_worker.c'),
                       str(ROOT / 'port/src/runtime_atrac9.c'), *(str(source) for source in sources),
                       '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe), str(fixture)], env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: NGS2 HLE-2.2A1', result.stdout)


if __name__ == '__main__':
    unittest.main()
