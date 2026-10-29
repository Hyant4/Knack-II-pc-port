"""Build shared-adapter lock tests and real LibAtrac9 concurrency tests."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
ATRAC9_SRC = ROOT / 'port/third_party/LibAtrac9/C/src'


class RuntimeAtrac9Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='knack runtime atrac9 ')
        cls.output = Path(cls.temporary.name)
        cls.compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        if not cls.compiler.exists():
            raise unittest.SkipTest('MSYS2 CLANG64 is required')
        cls.env = dict(os.environ, PATH=str(cls.compiler.parent) + os.pathsep + os.environ.get('PATH', ''))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def build(self, name, source, real_library):
        sources = [ROOT / 'tests' / source, ROOT / 'port/src/runtime_atrac9.c']
        if real_library:
            sources.extend(sorted(ATRAC9_SRC.glob('*.c')))
        exe = self.output / name
        command = [str(self.compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(ROOT / 'port'), '-I' + str(ROOT / 'port/src'),
                   '-I' + str(ATRAC9_SRC), *(str(item) for item in sources),
                   '-pthread', '-lm', '-o', str(exe)]
        built = subprocess.run(command, env=self.env, capture_output=True, text=True, timeout=120)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        return exe

    def test_lock_contract_and_bounded_stress(self):
        exe = self.build('runtime-atrac9-lock.exe', 'test_runtime_atrac9_lock.c', False)
        result = subprocess.run([str(exe)], env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('30000 operations', result.stdout)

    def test_explicit_rwlock_initialization_platform_probe(self):
        exe = self.output / 'runtime-atrac9-rwlock-probe.exe'
        command = [str(self.compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   str(ROOT / 'tests/test_runtime_atrac9_rwlock_probe.c'),
                   '-pthread', '-o', str(exe)]
        built = subprocess.run(command, env=self.env, capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        result = subprocess.run([str(exe)], env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS: pthread rwlock direct exclusion', result.stdout)

    def test_real_init_decode_concurrency(self):
        fixture_value = os.environ.get('KNACK2_ATRAC9_FIXTURE')
        if not fixture_value:
            self.skipTest('KNACK2_ATRAC9_FIXTURE is not set')
        fixture = Path(fixture_value)
        self.assertTrue(fixture.is_file(), fixture)
        exe = self.build('runtime-atrac9-real.exe', 'test_runtime_atrac9_concurrency.c', True)
        result = subprocess.run([str(exe), str(fixture)], env=self.env,
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('hash=a79abed856a64db5', result.stdout)


if __name__ == '__main__':
    unittest.main()
