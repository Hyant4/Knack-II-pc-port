"""Exercise actual runtime diagnostics with isolated INI configuration and synthetic PCM."""
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

ROOT=Path(__file__).resolve().parents[1]

class MatrixDiagnostics(unittest.TestCase):
    def test_pitch_requests_and_rejections_remain_visible_after_trace_budgets(self):
        cc=c_compiler()
        env={k:v for k,v in os.environ.items() if not k.startswith(('TLG_','SDL_'))}
        env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-pitch-diag-') as tmp:
            tmp=Path(tmp);exe=tmp/'pitch.exe'
            build=subprocess.run([str(cc),'-std=c11','-Wall','-Wextra','-Werror',
                '-Wframe-larger-than=65536','-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),
                str(ROOT/'tests/test_ngs2_matrix_diag.c'),'-pthread','-lm','-o',str(exe)],
                env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            parity=[]
            for contents,enabled in (('',False),('ngs2_pitch_diag=0\n',False),('ngs2_pitch_diag=1\n',True)):
                config=tmp/'settings.ini';config.write_text(contents,encoding='ascii')
                run=subprocess.run([str(exe),'pitch'],env=dict(env,TLG_CONFIG=str(config)),
                    capture_output=True,text=True,timeout=20)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertIn('PASS PITCH TELEMETRY',run.stdout)
                summary=re.search(r'^PITCH_PARITY=(.*)$',run.stdout,re.M)
                self.assertIsNotNone(summary,run.stdout);parity.append(summary.group(1))
                lines=[x for x in run.stdout.splitlines() if x.startswith('NGS2-PITCH-HB:')]
                self.assertEqual(len(lines),3 if enabled else 0)
                for line in lines:
                    self.assertLess(len(line),600)
                    self.assertIn('requests=5 zero_requests=1 nonunity_requests=3 rejected=1 ',line)
                    self.assertIn('format=0x40 generation=1 ',line)
                    self.assertIn('requested_bits=bf800000 stored_bits=00000000 ',line)
                    self.assertRegex(line,r'last_zero_us=[1-9]\d* last_zero_voice=')
            self.assertEqual(len(set(parity)),1)

    def test_hevag_opt_in_transitions_generation_and_independent_budgets(self):
        cc=c_compiler()
        env={k:v for k,v in os.environ.items() if not k.startswith(('TLG_','SDL_'))}
        env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-hevag-matrix-') as tmp:
            tmp=Path(tmp); exe=tmp/'matrix.exe'
            build=subprocess.run([str(cc),'-std=c11','-Wall','-Wextra','-Werror',
                '-Wframe-larger-than=65536','-I'+str(ROOT/'port/src'),'-I'+str(ROOT/'port'),
                str(ROOT/'tests/test_ngs2_matrix_diag.c'),'-pthread','-lm','-o',str(exe)],
                env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            parity=[]
            settings=(('',False,False),('ngs2_matrix_diag=0\nngs2_hevag_matrix_diag=0\n',False,False),
                      ('ngs2_matrix_diag=1\n',True,False),('ngs2_hevag_matrix_diag=1\n',False,True),
                      ('ngs2_matrix_diag=1\nngs2_hevag_matrix_diag=1\n',True,True))
            for contents,atrac9,hevag in settings:
                config=tmp/'settings.ini'
                config.write_text(contents,encoding='ascii')
                run=subprocess.run([str(exe),'hevag'],env=dict(env,TLG_CONFIG=str(config)),
                                   capture_output=True,text=True,timeout=20)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertIn('PASS HEVAG MATRIX DIAGNOSTICS',run.stdout)
                summary=re.search(r'^HEVAG_PARITY=(.*)$',run.stdout,re.M)
                self.assertIsNotNone(summary,run.stdout); parity.append(summary.group(1))
                lines=[x for x in run.stdout.splitlines() if x.startswith('NGS2-HEVAG-MATRIX:')]
                self.assertEqual(bool(lines),hevag)
                other_lines=[x for x in run.stdout.splitlines() if x.startswith('NGS2-CMD1:')]
                self.assertEqual(bool(other_lines),atrac9)
                if not hevag:
                    continue
                self.assertTrue(all(len(x)<768 for x in lines))
                data=[x for x in lines if 'trace_incomplete=1' not in x]
                self.assertTrue(all('codec=HEVAG ' in x and 'channels=1 ' in x for x in data))
                self.assertTrue(all(re.search(r' fifo=\d+$',x) for x in data))
                self.assertTrue(any('raw_nonzero=5 finite=14 effective_nonzero=0 ' in x for x in data))
                self.assertTrue(any('words=0 raw_nonzero=0 finite=0 effective_nonzero=0 ' in x for x in data))
                self.assertTrue(any('transition=to_silent ' in x and 'effective_nonzero=0 ' in x for x in data))
                self.assertTrue(any('transition=to_audible ' in x and 'samples_consumed=512 ' in x for x in data))
                self.assertTrue(any('last_action=4 ' in x and 'query_flags=0x1 ' in x for x in data))
                self.assertTrue(any('last_action=5 ' in x and 'query_flags=0x3 ' in x for x in data))
                self.assertTrue(any('event=cmd1 ' in x and 'post_action=1 ' in x for x in data))
                self.assertTrue(any('generation_reset=1 ' in x and 'generation=2 ' in x for x in data))
                panning=run.stdout.split('PHASE panning\n',1)[1].split('PHASE summary',1)[0]
                self.assertLessEqual(panning.count('NGS2-HEVAG-MATRIX:'),1)
                summary_lines=run.stdout.split('PHASE summary\n',1)[1].split('PHASE generation',1)[0]
                self.assertEqual(summary_lines.count('NGS2-HEVAG-MATRIX:'),1)
                rejected=run.stdout.split('PHASE rejected\n',1)[1].split('PHASE limits',1)[0]
                self.assertNotIn('NGS2-HEVAG-MATRIX:',rejected)
                self.assertEqual(sum('budget=changes ' in x for x in lines),1)
                self.assertEqual(sum('budget=state ' in x for x in lines),1)
                seq=[int(re.search(r'seq=(\d+)',x).group(1)) for x in lines]
                self.assertEqual(seq,list(range(1,len(seq)+1)))
            self.assertEqual(len(set(parity)),1)

    def test_real_runtime_parity_dedup_counters_classification_and_budgets(self):
        tc=Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')
        cc = c_compiler()
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
