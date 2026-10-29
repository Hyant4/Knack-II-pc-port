"""Guarded native preset regression; synthetic buffers, no game launch."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from port.launcher.knack2 import config

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('native_resolution_supervisor', ROOT/'run.py')
supervisor=importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)

class NativeResolutionTests(unittest.TestCase):
    def test_native_arguments_are_profile_stage_and_resolution_specific(self):
        for size in ('2560x1440','3840x2160'):
            self.assertEqual(supervisor.native_resolution_arguments('game.elf',[],size),
                             ['/app0/game.elf','-2160p'])
            original=['/app0/game.elf','existing']
            self.assertEqual(supervisor.native_resolution_arguments('game.elf',original,size),
                             original+['-2160p'])
            self.assertEqual(original,['/app0/game.elf','existing'])
            self.assertEqual(supervisor.native_resolution_arguments('eboot.bin',['boot'],size),['boot'])
            existing=['/app0/game.elf','-2160p']
            self.assertEqual(supervisor.native_resolution_arguments('game.elf',existing,size),existing)
            with self.assertRaises(ValueError):
                supervisor.native_resolution_arguments('game.elf',['arg']*64,size)
        for size in ('auto','1280x720','1600x900','1920x1080'):
            self.assertEqual(supervisor.native_resolution_arguments('game.elf',[],size),[])

    def test_native_image_guards(self):
        toolchain=Path(os.environ.get('MSYS2_ROOT') or Path(os.environ.get('LOCALAPPDATA',''))/'knack2-toolchain/msys64')/'clang64'
        compiler=toolchain/'bin/clang.exe'
        if not compiler.is_file(): self.skipTest('Existing clang toolchain unavailable')
        env=dict(os.environ,PATH=str(toolchain/'bin')+os.pathsep+os.environ.get('PATH',''))
        with tempfile.TemporaryDirectory() as temporary:
            executable=Path(temporary)/'native-resolution.exe'
            result=subprocess.run([str(compiler),'-std=c11','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'port/src'),str(ROOT/'tests/test_knack2_native_resolution.c'),
                '-o',str(executable)],env=env,capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            budgets=[]
            with patch.dict(os.environ,{'TLG_DMEM_MB':'16384'}):
                for size in ('2560x1440','3840x2160','1920x1080'):
                    config.update_knack2_ini(Path(temporary)/'knack2.ini',{'internal_resolution':size})
                    runtime=supervisor.runtime_environment(Path(temporary))
                    if size=='1920x1080': self.assertNotIn('TLG_DMEM_MB',runtime)
                    else: budgets.append(runtime['TLG_DMEM_MB'])
            self.assertEqual(budgets,['5824','6592'])
            result=subprocess.run([str(executable),*budgets],env=env,capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__': unittest.main()
