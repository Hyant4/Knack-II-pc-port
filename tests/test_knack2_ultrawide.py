"""Ultrawide configuration and isolated native proofs; never run a game."""
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest
from port.launcher.knack2 import config

ROOT=Path(__file__).resolve().parents[1]
TOOLS=Path(os.environ.get('MSYS2_ROOT') or Path.home()/'AppData/Local/knack2-toolchain/msys64')/'clang64/bin'


class UltrawideConfigTests(unittest.TestCase):
    def test_aspect_roundtrip_preserves_audio_and_rejects_invalid_values(self):
        with tempfile.TemporaryDirectory() as temporary:
            ini=Path(temporary)/'knack2.ini'
            prefix=b'# keep\r\nngs2_render_mix=1\r\n'
            ini.write_bytes(prefix)
            for aspect in config.VALID_ASPECT_RATIOS:
                config.update_knack2_ini(ini,{'aspect_ratio':aspect})
                self.assertEqual(config.load_knack2_ini(ini)['aspect_ratio'],aspect)
                self.assertTrue(ini.read_bytes().startswith(prefix))
            before=ini.read_bytes()
            for invalid in ['stretch','0:0','99:9','nan','21:9\nvsync=0',True,21]:
                with self.subTest(value=invalid),self.assertRaises(ValueError):
                    config.update_knack2_ini(ini,{'aspect_ratio':invalid})
                self.assertEqual(ini.read_bytes(),before)

    def test_ultrawide_window_presets_are_explicit(self):
        for value in ['2560x1080','3440x1440','3840x1600','5120x1440','5120x2160','7680x2160']:
            self.assertEqual(config.sanitize_and_validate_key_value('window_resolution',value),('window_resolution',value))


class UltrawideNativeTests(unittest.TestCase):
    def test_projection_patch_and_presentation_geometry(self):
        env=dict(os.environ)
        if os.name=='nt':
            self.assertTrue((TOOLS/'clang.exe').is_file(),'Existing native toolchain required')
            env['PATH']=str(TOOLS)+os.pathsep+env.get('PATH','')
        with tempfile.TemporaryDirectory(prefix='knack-wide-native-') as temporary:
            for suffix,compiler,include in [('c','clang.exe','port/src'),('cpp','clang++.exe','port/gpu/shim')]:
                executable=str(TOOLS/compiler) if os.name=='nt' else shutil.which('cc' if suffix=='c' else 'c++')
                self.assertTrue(executable,'Existing native compiler required')
                target=Path(temporary)/('wide-'+suffix+('.exe' if os.name=='nt' else ''))
                command=[executable,'-std=c11' if suffix=='c' else '-std=c++23','-Wall','-Wextra','-Werror','-I',str(ROOT/include),str(ROOT/f'tests/test_knack2_ultrawide.{suffix}'),'-o',str(target)]
                if os.name!='nt':command.append('-lm')
                built=subprocess.run(command,env=env,capture_output=True,text=True,timeout=45)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                ran=subprocess.run([str(target)],env=env,capture_output=True,text=True,timeout=15)
                self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':
    unittest.main()
