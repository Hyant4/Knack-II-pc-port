"""B2-F2C zero-matrix bypass test on Windows clang."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
TOOLCHAIN=Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')
class B2F2C(unittest.TestCase):
    def test_opt_in_and_safe_rollback(self):
        compiler=TOOLCHAIN/'clang64/bin/clang.exe'
        self.assertTrue(compiler.is_file(),f'Missing compiler: {compiler}')
        env=dict(os.environ,PATH=str(compiler.parent)+os.pathsep+os.environ.get('PATH',''))
        with tempfile.TemporaryDirectory(prefix='knack-b2f2c-') as tmp:
            binary=Path(tmp)/'test_b2f2c.exe'
            cmd=[str(compiler),'-std=c11','-Wall','-Wextra','-Werror',
                 '-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),
                 str(ROOT/'tests/test_ngs2_b2f2c.c'),'-pthread','-lm','-o',str(binary)]
            result=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=90)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,env=env,timeout=90)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS: B2-F2C',run.stdout)
if __name__=='__main__': unittest.main()
