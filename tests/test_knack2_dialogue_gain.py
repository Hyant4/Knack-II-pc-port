"""Execute the exact native dialogue-gain stubs without starting the game."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DialogueGainTests(unittest.TestCase):
    def test_native_category_isolation_and_preservation(self):
        compiler = (Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64') / 'clang64/bin/clang.exe'
                    if os.name == 'nt' else Path(shutil.which(os.environ.get('CC', 'cc')) or '/missing-cc'))
        self.assertTrue(compiler.is_file(), 'A native C compiler is required')
        env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
        with tempfile.TemporaryDirectory(prefix='knack-dialogue-') as tmp:
            exe = Path(tmp) / ('dialogue.exe' if os.name == 'nt' else 'dialogue')
            build = subprocess.run([str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'tests/test_knack2_dialogue_gain.c'), '-lm', '-o', str(exe)],
                                   env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('PASS: dialogue gain contracts', run.stdout)


if __name__ == '__main__':
    unittest.main()
