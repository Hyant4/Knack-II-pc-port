"""Exercise the actual FreeType HLE and Knack's writing-step byte offsets."""
import os
from pathlib import Path
import subprocess
import shlex
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')


class FontTests(unittest.TestCase):
    def test_horizontal_phrase_positions(self):
        env = dict(os.environ)
        if os.name == 'nt':
            prefix = TOOLCHAIN / 'clang64'
            compiler = [str(prefix / 'bin/clang.exe')]
            flags = ['-I' + str(prefix / 'include/freetype2'), '-L' + str(prefix / 'lib'), '-lfreetype']
            env['PATH'] = str(prefix / 'bin') + os.pathsep + env.get('PATH', '')
        else:
            compiler = shlex.split(os.environ.get('CC', 'cc'))
            flags = shlex.split(subprocess.check_output(
                ['pkg-config', '--cflags', '--libs', 'freetype2'], text=True)) + ['-lm']
        with tempfile.TemporaryDirectory(prefix='knack font test ') as temporary:
            exe = Path(temporary) / ('font-test.exe' if os.name == 'nt' else 'font-test')
            command = compiler + ['-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(ROOT / 'port/src'), '-I' + str(ROOT / 'port'),
                       str(ROOT / 'tests/test_knack_font.c')] + flags + ['-pthread', '-o', str(exe)]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            font = ROOT / 'port/gpu/third_party/fonts/DejaVuSans.ttf'
            result = subprocess.run([str(exe), str(font)], env=env, capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS:', result.stdout)


if __name__ == '__main__':
    unittest.main()
