"""Compile and run guest-memory snapshot safety tests."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')


class RuntimeMemorySnapshotTests(unittest.TestCase):
    def test_snapshot_validation_and_map_lock(self):
        compiler = TOOLCHAIN / 'clang64/bin/clang.exe' if os.name == 'nt' else Path(shutil.which(os.environ.get('CC', 'cc')) or '/missing-cc')
        self.assertTrue(compiler.is_file(), 'A native C compiler is required')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack memory snapshot ') as temporary:
            exe = Path(temporary) / ('runtime-memory-snapshot-test.exe' if os.name == 'nt' else 'runtime-memory-snapshot-test')
            command = [str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_runtime_memory_snapshot.c'), '-pthread', '-o', str(exe)]
            if os.name == 'nt':
                command += [str(ROOT / 'port/src/compat_win.c'), '-lonecore', '-lbcrypt']
            else:
                command.insert(1, '-D_GNU_SOURCE=')
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS: guest snapshot', result.stdout)
            if os.environ.get('KNACK2_TEST_VERBOSE'):
                print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
