"""Positive Cmd5 contracts, executed independently so early RED does not hide other cases."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
CASES = ('rack','voice','disconnect','invalid','foreign_voice','foreign_rack','self','cycle2',
         'chain_cycle','rack_endpoint','existing_cycle','destroy_rack','destroy_voice_target',
         'short_command','pcm_preserved','trace_rack')
class Ngs2Cmd5Tests(unittest.TestCase):
    def test_independent_storage_contracts(self):
        tc = Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')
        cc = tc/'clang64/bin/clang.exe'
        self.assertTrue(cc.is_file())
        env = {k:v for k,v in os.environ.items() if not k.startswith(('TLG_','SDL_'))}
        env['PATH'] = str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-cmd5-') as temp:
            exe = Path(temp)/'cmd5.exe'
            cmd = [str(cc),'-std=c11','-Wall','-Wextra','-Werror','-Wframe-larger-than=65536',
                   '-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),str(ROOT/'tests/test_ngs2_cmd5.c'),
                   '-pthread','-lm','-o',str(exe)]
            build = subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            for case in CASES:
                with self.subTest(case=case):
                    run = subprocess.run([str(exe),case],env=env,capture_output=True,text=True,timeout=10)
                    self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                    self.assertIn('PASS_CMD5_CASE '+case,run.stdout)
if __name__ == '__main__':
    unittest.main()
