"""Long compressed segments retain per-voice/global bounds and byte ownership."""
import os
from pathlib import Path
import subprocess,tempfile,unittest
from tests.native_toolchain import c_compiler
ROOT=Path(__file__).resolve().parents[1]

class SegmentCapacity(unittest.TestCase):
    def test_large_segments_and_existing_bounds(self):
        cc=c_compiler()
        env={k:v for k,v in os.environ.items() if not k.startswith(('TLG_','SDL_'))}
        env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-long-segment-') as folder:
            exe=Path(folder)/'capacity.exe'
            command=[str(cc),'-std=c11','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),
                str(ROOT/'tests/test_ngs2_segment_capacity.c'),'-pthread','-lm','-o',str(exe)]
            result=subprocess.run(command,env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS failed copy, reconfiguration and destruction',result.stdout)
            if os.environ.get('KNACK_CAPACITY_TEST_LOG'):
                Path(os.environ['KNACK_CAPACITY_TEST_LOG']).write_text(result.stdout+result.stderr,encoding='utf-8')
if __name__=='__main__':unittest.main()
