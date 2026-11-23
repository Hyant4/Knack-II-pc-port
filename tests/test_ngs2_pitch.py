"""Regression for source-rate consumption using the real mixer and synthetic PCM."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler

ROOT = Path(__file__).resolve().parents[1]


class Ngs2Pitch(unittest.TestCase):
    def test_pcm_clock_hold_fractions_and_transactional_state(self):
        compiler = c_compiler()
        env = {k: v for k, v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='knack-pitch-') as tmp:
            binary = Path(tmp) / 'pitch.exe'
            config = Path(tmp) / 'settings.ini'
            config.write_text('', encoding='ascii')
            env['TLG_CONFIG'] = str(config)
            build = subprocess.run([str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-Wframe-larger-than=65536', '-I'+str(ROOT/'port/src'), '-I'+str(ROOT/'port'),
                str(ROOT/'tests/test_ngs2_pitch.c'), '-pthread', '-lm', '-o', str(binary)],
                env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout+build.stderr)
            run = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            self.assertIn('PASS: source-rate PCM', run.stdout)


if __name__ == '__main__':
    unittest.main()
