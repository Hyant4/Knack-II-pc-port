"""Regression for the SoundX-consumed uint64 counter at Sampler state +0x10."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class SampleClockTests(unittest.TestCase):
    def test_clock_tracks_committed_pcm(self):
        cc=Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')/'clang64/bin/clang.exe'
        env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-sample-clock-') as tmp:
            exe=Path(tmp)/'clock.exe'
            build=subprocess.run([str(cc),'-std=c11','-Wall','-Wextra','-Werror','-Wframe-larger-than=65536','-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),str(ROOT/'tests/test_ngs2_sample_clock.c'),'-pthread','-lm','-o',str(exe)],env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS: sample clock commits',run.stdout)
if __name__=='__main__':unittest.main()
