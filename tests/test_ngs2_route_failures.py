"""Independent synthetic analysis & contract tests for Knack 2 NGS2 HLE (Revision 2 & 3)."""
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

class Ngs2RouteRevision2Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tc = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
        cc = c_compiler()
        if not cc.is_file():
            raise unittest.SkipTest(f"Compiler not found at {cc}")
        cls.cc = cc

        cls.env = {k: v for k, v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        cls.env['PATH'] = str(cc.parent) + os.pathsep + cls.env.get('PATH', '')

        cls.tmpdir = tempfile.TemporaryDirectory(prefix='knack-rev3-')
        cls.exe = Path(cls.tmpdir.name) / 'route_rev3.exe'

        cmd = [
            str(cls.cc), '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-Wframe-larger-than=65536',
            '-I' + str(ROOT / 'port/src'),
            '-I' + str(ROOT / 'port'),
            str(ROOT / 'tests/test_ngs2_route_failures.c'),
            '-pthread', '-lm', '-o', str(cls.exe)
        ]
        build = subprocess.run(cmd, env=cls.env, capture_output=True, text=True, timeout=60)
        if build.returncode != 0:
            cls.tmpdir.cleanup()
            raise RuntimeError(f"Build failed: {build.stdout}\n{build.stderr}")

    @classmethod
    def tearDownClass(cls):
        cls.tmpdir.cleanup()

    def test_baseline_observations_and_layer_separation(self):
        # 1. Run baseline observation mode
        run_base = subprocess.run([str(self.exe)], env=self.env, capture_output=True, text=True, timeout=20)
        self.assertEqual(run_base.returncode, 0, run_base.stdout + run_base.stderr)
        self.assertIn('BASELINE OBSERVATION: Cmd5 with target=RackHandle returns 0', run_base.stdout)
        self.assertIn('LAYER 1 (Source-only 0.5f)', run_base.stdout)
        self.assertIn('LAYER 2 (Bus-only 0.25f)', run_base.stdout)
        self.assertIn('LAYER 3 (Distinct: Src=0.5f, Bus=0.25f)', run_base.stdout)
        self.assertIn('PASS: Voice isolation verified', run_base.stdout)

    def test_simulation_action3_kill_coupling(self):
        # 2. Run simulation showing that hard-killing on Action 3 without VoiceGetState silences audio
        run_sim = subprocess.run([str(self.exe), '--simulate-kill'], env=self.env, capture_output=True, text=True, timeout=20)
        self.assertEqual(run_sim.returncode, 0, run_sim.stdout + run_sim.stderr)
        self.assertIn('STATIC_SIMULATION: Hard kill on Action 3 with VoiceGetState=0 discards 7 grains and silences audio!', run_sim.stdout)

    def test_cmd5_positive_contract(self):
        run_cmd5 = subprocess.run([str(self.exe), '--assert-cmd5-contract'], env=self.env,
                                 capture_output=True, text=True, timeout=20)
        output = run_cmd5.stdout + run_cmd5.stderr
        self.assertEqual(run_cmd5.returncode, 0, output)
        self.assertIn('PASS_CMD5_POSITIVE_CONTRACT', output)

    def test_lifecycle_positive_contract_green(self):
        run_life = subprocess.run([str(self.exe), '--assert-lifecycle-contract'],
                                 env=self.env, capture_output=True, text=True, timeout=20)
        self.assertEqual(run_life.returncode, 0, run_life.stdout + run_life.stderr)
        self.assertIn('PASS_LIFECYCLE_POSITIVE_CONTRACT', run_life.stdout)

if __name__ == '__main__':
    unittest.main()
