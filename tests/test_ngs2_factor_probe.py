"""Finite Windows test of the opt-in evaluator probe, using synthetic code and buffers."""
import os
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class FactorProbeTests(unittest.TestCase):
    def test_call_boundary_and_observation_contract(self):
        cc = c_compiler()
        self.assertTrue(cc.is_file())
        env={k:v for k,v in os.environ.items() if not k.startswith(('TLG_','SDL_'))}
        env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-factor-probe-') as tmp:
            exe=Path(tmp)/'probe.exe'
            build=subprocess.run([str(cc),'-std=c11','-O2','-Wall','-Wextra','-Werror',
                '-Wframe-larger-than=65536','-I'+str(ROOT/'port/src'),str(ROOT/'tests/test_ngs2_factor_probe.c'),
                '-o',str(exe)],env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS: actual SysV CALL',run.stdout)
            self.assertIn('C=00000000',run.stdout)
            self.assertIn('metadata_valid=1f',run.stdout)
            self.assertIn('own_flags=00000008 own_B=3e800000 descriptor_flags=00000008 descriptor_B=3f000000',run.stdout)
            self.assertEqual(run.stdout.count('budget=changes trace_incomplete=1'),1)
            self.assertEqual(run.stdout.count('budget=summaries trace_incomplete=1'),1)

if __name__=='__main__':
    unittest.main()
