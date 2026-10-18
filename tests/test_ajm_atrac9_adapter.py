"""Compile AJM ATRAC9 regression test against an external authorized fixture."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
ATRAC9_SRC = ROOT / 'port/third_party/LibAtrac9/C/src'


class AjmAtrac9AdapterTests(unittest.TestCase):
    def test_ajm_pcm_matches_known_fixture(self):
        fixture_value = os.environ.get('KNACK2_ATRAC9_FIXTURE')
        if not fixture_value:
            self.skipTest('KNACK2_ATRAC9_FIXTURE is not set')
        fixture = Path(fixture_value)
        self.assertTrue(fixture.is_file(), fixture)
        compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        self.assertTrue(compiler.exists(), 'MSYS2 CLANG64 is required')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack ajm atrac9 ') as temporary:
            output = Path(temporary)
            sources = [ROOT / 'tests/test_ajm_atrac9_adapter.c']
            adapter = ROOT / 'port/src/runtime_atrac9.c'
            if adapter.exists():
                sources.append(adapter)
            sources.extend(sorted(ATRAC9_SRC.glob('*.c')))
            exe = output / 'ajm-atrac9-adapter-test.exe'
            command = [str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port'), '-I' + str(ROOT / 'port/src'),
                       '-I' + str(ATRAC9_SRC), *(str(source) for source in sources),
                       '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe), str(fixture)], env=env,
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('hash=a79abed856a64db5', result.stdout)


if __name__ == '__main__':
    unittest.main()
