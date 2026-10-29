"""Exercise independently verified Knack SoundX command-6 wire values."""
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
class Ngs2WireEventTests(unittest.TestCase):
    def test_verified_guest_events(self):
        tc = Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')
        cc = c_compiler()
        self.assertTrue(cc.is_file())
        env = {k:v for k,v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        env['PATH'] = str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-wire-') as tmp:
            exe = Path(tmp)/'wire.exe'
            cmd = [str(cc),'-std=c11','-Wall','-Wextra','-Werror','-Wframe-larger-than=65536',
                   '-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),str(ROOT/'tests/test_ngs2_wire_events.c'),
                   '-pthread','-lm','-o',str(exe)]
            build = subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run = subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS VERIFIED GUEST WIRE CONTRACTS',run.stdout)
if __name__ == '__main__':
    unittest.main()
