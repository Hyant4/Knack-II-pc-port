"""Route diagnostics must preserve PCM and state with opt-in both off and on."""
import os
try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class Ngs2RouteDiagTests(unittest.TestCase):
    def test_opt_in_output_parity_fields_and_limit(self):
        tc = Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')
        cc = c_compiler()
        self.assertTrue(cc.is_file())
        env = {k:v for k,v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        env['PATH'] = str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-route-diag-') as temp:
            temp = Path(temp)
            exe = temp/'route.exe'
            cmd = [str(cc),'-std=c11','-Wall','-Wextra','-Werror','-Wframe-larger-than=65536',
                   '-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),str(ROOT/'tests/test_ngs2_route_diag.c'),
                   '-pthread','-lm','-o',str(exe)]
            build = subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            summaries = []
            for enabled in (False, True):
                config = temp/('on.ini' if enabled else 'off.ini')
                config.write_text('ngs2_route_diag=1\n' if enabled else '',encoding='ascii')
                child_env = dict(env, TLG_CONFIG=str(config))
                run = subprocess.run([str(exe),'on' if enabled else 'off'],env=child_env,
                                     capture_output=True,text=True,timeout=20)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertIn('PASS PASSIVE ROUTE DIAGNOSTICS',run.stdout)
                summaries.append(re.search(r'^PCM_DIGEST=.*$',run.stdout,re.M).group())
                lines = [x for x in run.stdout.splitlines() if x.startswith('NGS2-ROUTE:')]
                self.assertEqual(bool(lines),enabled)
                if enabled:
                    self.assertTrue(any('cmd=2 ' in x and 'gain=0.5 ' in x for x in lines))
                    self.assertTrue(any('cmd=5 ' in x and 'target_rack=0x2000/' in x for x in lines))
                    self.assertTrue(any('cmd=6 ' in x and 'action=4 ' in x for x in lines))
                    for line in lines:
                        self.assertLess(len(line),512)
                        self.assertIn('generation=',line)
                        self.assertIn('handle=',line)
            self.assertEqual(summaries[0],summaries[1])
if __name__ == '__main__':
    unittest.main()
