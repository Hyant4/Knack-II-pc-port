"""Compile and run the isolated SaveData contract against synthetic files."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')


class SaveDataTests(unittest.TestCase):
    def test_metadata_backup_and_events(self):
        compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        self.assertTrue(compiler.exists(), 'MSYS2 CLANG64 is required for this native test')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack save test ') as temporary:
            root = Path(temporary)
            exe = root / 'savedata-test.exe'
            command = [str(compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_knack_savedata.c'), '-pthread', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            user = root / 'user with spaces'
            user.mkdir()
            result = subprocess.run([str(exe), str(user)], env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            backup = user / 'savedata-backups/1/CUSA08014/SAVEDATA00'
            current = (backup / 'current.txt').read_text().strip()
            generation = backup / current
            save = user / 'savedata/1/CUSA08014/SAVEDATA00'
            self.assertEqual((generation / 'data/payload.dat').read_bytes(), (save / 'payload.dat').read_bytes())
            self.assertEqual((generation / 'metadata/param.bin').read_bytes(),
                             save.with_name('SAVEDATA00.sce_sys').joinpath('param.bin').read_bytes())
            previous = [p for p in backup.glob('gen-*/data/payload.dat')
                        if p.parent.parent != generation]
            self.assertEqual([p.read_bytes() for p in previous], [b'known save payload'])
            self.assertEqual((generation / 'data/payload.dat').read_bytes(), b'updated save payload')
            self.assertIn('PASS:', result.stdout)


if __name__ == '__main__':
    unittest.main()
