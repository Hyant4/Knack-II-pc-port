"""Exercise actual runtime diagnostics with isolated INI configuration and synthetic PCM."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class MatrixDiagnostics(unittest.TestCase):
    def test_real_runtime_parity_dedup_counters_classification_and_budgets(self):
        tc=Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')
        cc=tc/'clang64/bin/clang.exe'
        self.assertTrue(cc.is_file())
        env={k:v for k,v in os.environ.items() if not k.startswith(('TLG_','SDL_'))}
        env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-matrix-diag-') as tmp:
            tmp=Path(tmp); exe=tmp/'matrix.exe'
            args=[str(cc),'-std=c11','-Wall','-Wextra','-Werror','-Wframe-larger-than=65536',
                  '-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),
                  str(ROOT/'tests/test_ngs2_matrix_diag.c'),'-pthread','-lm','-o',str(exe)]
            build=subprocess.run(args,env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            parity=[]
            for enabled in (False,True):
                config=tmp/('on.ini' if enabled else 'off.ini')
                config.write_text('ngs2_matrix_diag=1\n' if enabled else '',encoding='ascii')
                run=subprocess.run([str(exe),'on' if enabled else 'off'],
                                   env=dict(env,TLG_CONFIG=str(config)),capture_output=True,text=True,timeout=20)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertIn('PASS MATRIX DIAGNOSTICS',run.stdout)
                summary=re.search(r'^PARITY=(.*?) calls=(\d+)$',run.stdout,re.M)
                self.assertIsNotNone(summary,run.stdout); parity.append(summary.group(1))
                self.assertEqual(int(summary.group(2)),11 if enabled else 0)
                lines=[line for line in run.stdout.splitlines() if line.startswith('NGS2-CMD1:')]
                self.assertEqual(bool(lines),enabled)
                if not enabled:
                    continue
                self.assertTrue(all(len(line)<768 for line in lines))
                self.assertTrue(any('event=cmd1 ' in x and 'calls=1 ' in x for x in lines))
                self.assertFalse(any('event=cmd1 ' in x and 'calls=2 ' in x for x in lines))
                self.assertTrue(any('event=cmd1 ' in x and 'calls=5 ' in x and 'post_action=1 ' in x for x in lines))
                self.assertTrue(any('event=query ' in x and 'calls=7 ' in x for x in lines))
                self.assertTrue(any('raw_nonzero=5 finite=62 effective_nonzero=0 ' in x for x in lines))
                self.assertTrue(any('last_action=4 ' in x and 'query_flags_valid=1 query_flags=0x1 ' in x for x in lines))
                self.assertTrue(any('last_action=5 ' in x and 'query_flags_valid=1 query_flags=0x3 ' in x for x in lines))
                self.assertEqual(sum('budget=changes ' in x for x in lines),1)
                self.assertEqual(sum('budget=state ' in x for x in lines),1)
                seq=[int(re.search(r'seq=(\d+)',x).group(1)) for x in lines]
                self.assertEqual(seq,list(range(1,len(seq)+1)))
            self.assertEqual(parity[0],parity[1])

if __name__=='__main__':
    unittest.main()
