"""Positive API regressions for finite completion and explicit unsupported boundaries."""
import os
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CASES = ('finished_reuse', 'eof_before_play', 'empty_eof_and_short_command',
         'descriptor_control_boundaries', 'cancel_during_snapshot_then_reuse',
         'input_failure_not_natural_completion', 'hevag_terminal_drain')

class CompletionEdges(unittest.TestCase):
    def test_completion_edges(self):
        tc = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
        cc = c_compiler()
        self.assertTrue(cc.is_file())
        env = {k: v for k, v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        env['PATH'] = str(cc.parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='knack-completion-edges-') as tmp:
            exe = Path(tmp) / 'edges.exe'
            command = [str(cc), '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-Wframe-larger-than=65536', '-I' + str(ROOT / 'port/src'),
                       '-I' + str(ROOT / 'port'), str(ROOT / 'tests/test_ngs2_completion_edges.c'),
                       '-pthread', '-lm', '-o', str(exe)]
            built = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            for case in CASES:
                with self.subTest(case=case):
                    result = subprocess.run([str(exe), case], env=env, capture_output=True, text=True, timeout=15)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('PASS_COMPLETION_EDGE ' + case, result.stdout)

if __name__ == '__main__':
    unittest.main()
