"""Test runner demonstrating RED on Baseline and GREEN on B2-F2D Candidate.

Compiles and executes against the REAL runtime_tlg.c (both unmodified baseline
and isolated patched candidate copy), ensuring exact same test contracts for both.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')


class TestNgs2VoiceState(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        if not cls.compiler.is_file():
            raise unittest.SkipTest(f'Missing compiler: {cls.compiler}')
        cls.env = dict(os.environ, PATH=str(cls.compiler.parent) + os.pathsep + os.environ.get('PATH', ''))

    def test_baseline_real_runtime(self):
        """Demonstrates baseline parity and RED when expecting B2-F2D contracts."""
        with tempfile.TemporaryDirectory(prefix='knack-baseline-') as tmp:
            binary = Path(tmp) / 'test_baseline.exe'
            cmd = [
                str(self.compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(ROOT / 'port/src'),
                '-I' + str(ROOT / 'port'),
                str(ROOT / 'tests/test_ngs2_voice_state.c'),
                '-pthread', '-lm', '-o', str(binary)
            ]
            res = subprocess.run(cmd, capture_output=True, text=True, env=self.env, timeout=90)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

            # Baseline check (passes baseline contracts):
            run_base = subprocess.run([str(binary)], capture_output=True, text=True, env=self.env, timeout=90)
            self.assertEqual(run_base.returncode, 0, run_base.stdout + run_base.stderr)
            self.assertIn('BASELINE VERIFICATION COMPLETED', run_base.stdout)

            # Baseline expecting B2-F2D contracts: MUST FAIL (Demonstrating RED)
            run_red = subprocess.run([str(binary), '--assert-b2f2d-active'], capture_output=True, text=True, env=self.env, timeout=90)
            self.assertNotEqual(run_red.returncode, 0, "Baseline must fail (RED) on B2-F2D contracts because baseline returns all-zeros")

    def test_candidate_real_runtime_green(self):
        """Demonstrates 100% GREEN when candidate patch is applied to an isolated copy."""
        patch_file = ROOT / 'knack2_B2F2D_candidate.patch'
        self.assertTrue(patch_file.is_file(), f'Missing candidate patch file: {patch_file}')

        with tempfile.TemporaryDirectory(prefix='knack-candidate-') as tmp:
            tmp_path = Path(tmp)
            port_src = tmp_path / 'port/src'
            port_src.mkdir(parents=True)

            # Copy real runtime_tlg.c into isolated directory
            shutil.copy2(ROOT / 'port/src/runtime_tlg.c', port_src / 'runtime_tlg.c')

            # Initialize isolated git repo in temp dir to allow clean git apply
            subprocess.run(['git', 'init'], cwd=tmp_path, capture_output=True, check=True)
            subprocess.run(['git', 'add', 'port/src/runtime_tlg.c'], cwd=tmp_path, capture_output=True, check=True)

            # Apply candidate patch to isolated copy
            patch_cmd = ['git', 'apply', str(patch_file)]
            patch_res = subprocess.run(patch_cmd, cwd=tmp_path, capture_output=True, text=True)
            self.assertEqual(patch_res.returncode, 0, f"Patch apply failed: {patch_res.stderr}")

            binary = tmp_path / 'test_candidate.exe'
            cmd = [
                str(self.compiler), '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(port_src),
                '-I' + str(ROOT / 'port/src'),
                '-I' + str(ROOT / 'port'),
                str(ROOT / 'tests/test_ngs2_voice_state.c'),
                '-pthread', '-lm', '-o', str(binary)
            ]
            res = subprocess.run(cmd, capture_output=True, text=True, env=self.env, timeout=90)
            self.assertEqual(res.returncode, 0, res.stdout + res.stderr)

            # 1. Run with B2-F2D OFF: Demonstrates 100% baseline parity / safe rollback
            env_off = dict(self.env, TLG_NGS2_B2F2D='0')
            run_off = subprocess.run([str(binary)], capture_output=True, text=True, env=env_off, timeout=90)
            self.assertEqual(run_off.returncode, 0, run_off.stdout + run_off.stderr)
            self.assertIn('BASELINE VERIFICATION COMPLETED', run_off.stdout)

            # 2. Run with B2-F2D ON: Demonstrates 100% GREEN on all contracts
            env_on = dict(self.env, TLG_NGS2_B2F2D='1')
            run_on = subprocess.run([str(binary), '--assert-b2f2d-active'], capture_output=True, text=True, env=env_on, timeout=90)
            print('\n' + run_on.stdout)
            self.assertEqual(run_on.returncode, 0, run_on.stdout + run_on.stderr)
            self.assertIn('ALL NGS2 B2-F2D CANDIDATE TESTS PASSED (GREEN)', run_on.stdout)


if __name__ == '__main__':
    unittest.main()
