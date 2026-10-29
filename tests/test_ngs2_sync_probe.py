"""Synthetic verification of the opt-in synchronizer observer."""
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
class SyncProbeTests(unittest.TestCase):
    def test_original_call_and_query_parity(self):
        cc = c_compiler()
        env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-sync-probe-') as tmp:
            exe=Path(tmp)/'sync.exe'
            build=subprocess.run([str(cc),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/test_ngs2_sync_probe.c'),'-o',str(exe)],env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS: sync original once',run.stdout)
            self.assertIn('reference=40600000 mask=00200000',run.stdout)
            self.assertIn('query_valid=1 result=-7 position=3fa00000 difference=bf000000',run.stdout)
            self.assertEqual(run.stdout.count('trace_incomplete=1 limit=1024'),1)
if __name__=='__main__':unittest.main()
