"""Compile the real-mixer continuity, pause/resume and capacity contracts."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ngs2ContinuityTests(unittest.TestCase):
    def test_per_source_continuity_and_pause(self):
        toolchain = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
        compiler = toolchain / 'clang64/bin/clang.exe'
        self.assertTrue(compiler.is_file(), 'MSYS2 CLANG64 is required')
        env = {k: v for k, v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='knack-continuity-') as tmp:
            exe = Path(tmp) / 'continuity.exe'
            command = [str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                       '-Wframe-larger-than=65536', '-I' + str(ROOT / 'port/src'),
                       '-I' + str(ROOT / 'port'), str(ROOT / 'tests/test_ngs2_continuity.c'),
                       '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('PASS: all source continuity contracts', run.stdout)


if __name__ == '__main__':
    unittest.main()
