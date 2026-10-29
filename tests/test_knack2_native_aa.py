"""Native-AA configuration and synthetic guards; no game or real user data."""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import config

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('aa_supervisor',ROOT/'run.py')
supervisor=importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


class NativeAaTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec('PySide6'), 'Qt unavailable')
    def test_launcher_control_and_incompatible_choices(self):
        os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
        os.environ.setdefault('QT_QUICK_BACKEND','software')
        from PySide6.QtCore import QObject
        from PySide6.QtGui import QGuiApplication
        from PySide6.QtTest import QTest
        from port.launcher.knack2.ui.app import create_window
        from port.launcher.knack2.ui.backend import Backend
        self.__class__._qtapp=QGuiApplication.instance() or QGuiApplication(['aa-test'])
        backend=Backend()
        engine,window,_=create_window(backend)
        try:
            QTest.qWait(60)
            control=window.findChild(QObject,'temporalPreset')
            temporal=window.findChild(QObject,'temporalUpscaler')
            self.assertIsNotNone(control)
            self.assertIsNone(window.findChild(QObject,'antialiasing'))
            self.assertEqual(control.property('count'),2)
            self.assertTrue(control.property('enabled'))
            backend.setSetting('antialiasing','off');QTest.qWait(20)
            self.assertEqual(control.property('currentIndex'),1)
            self.assertFalse(temporal.property('enabled'))
            for locale,label in [('en-US','Off'),('ru-RU','Выкл.'),('pt-BR','Desligado')]:
                backend.setUiLanguage(locale);QTest.qWait(20)
                self.assertEqual(control.property('displayText'),label)
            backend.setSetting('upscaler','fsr3')
            backend.setSetting('internal_resolution','1280x720')
            self.assertEqual(backend.settings['upscaler'],'off')
            self.assertEqual(backend.settings['internal_resolution'],'auto')
            backend.setSetting('antialiasing','native')
            backend.setSetting('upscaler','fsr3');QTest.qWait(20)
            self.assertTrue(control.property('enabled'))
            self.assertEqual(control.property('count'),4)
            backend.setSetting('preset','2');QTest.qWait(20)
            self.assertEqual(control.property('currentIndex'),2)
            backend.setSetting('upscaler','off');QTest.qWait(20)
            self.assertEqual(control.property('count'),2)
            self.assertEqual(control.property('currentIndex'),0)
            self.assertEqual(backend.settings['antialiasing'],'native')
        finally:
            window.close();backend.shutdown();engine.deleteLater();QTest.qWait(20)

    def test_default_and_validated_environment(self):
        with tempfile.TemporaryDirectory() as d, patch.dict(os.environ,{'TLG_ANTIALIASING':'off'}):
            root=Path(d)
            self.assertEqual(supervisor.runtime_environment(root)['TLG_ANTIALIASING'],'native')
            for size in ('auto','1920x1080','2560x1440','3840x2160'):
                config.update_knack2_ini(root/'knack2.ini',{'antialiasing':'off','internal_resolution':size})
                for profile in supervisor.SUPPORTED_PROFILES:
                    env=supervisor.runtime_environment(root,profile=profile)
                    self.assertEqual(env['TLG_ANTIALIASING'],'off')
                    self.assertEqual(env['TLG_UPSCALER'],'off')

    def test_invalid_updates_are_atomic_and_cli_rejects_bad_combinations(self):
        with tempfile.TemporaryDirectory() as d:
            ini=Path(d)/'knack2.ini'
            original=b'# preserve\r\nantialiasing=off\r\nupscaler=off\r\nngs2_render_mix=1\r\n'
            ini.write_bytes(original)
            for key,value in [('antialiasing','taa'),('antialiasing','off\nfsr1=1'),
                              ('upscaler','fsr3'),('upscaler','fsr4'),
                              ('internal_resolution','1280x720'),('internal_resolution','1600x900')]:
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):
                    config.update_knack2_ini(ini,{key:value})
                self.assertEqual(ini.read_bytes(),original)
            ini.write_text('antialiasing=off\nupscaler=fsr3\n',encoding='utf-8')
            with self.assertRaises(ValueError):supervisor.runtime_environment(Path(d))
            config.update_knack2_ini(ini,{'antialiasing':'native'})
            self.assertEqual(supervisor.runtime_environment(Path(d))['TLG_UPSCALER'],'fsr3')

    def test_shader_copy_and_guest_context_guards(self):
        prefix=Path(os.environ.get('MSYS2_ROOT') or Path(os.environ['LOCALAPPDATA'])/'knack2-toolchain/msys64')/'clang64'
        if not (prefix/'bin/clang.exe').is_file():self.skipTest('Existing clang toolchain unavailable')
        env=dict(os.environ,PATH=str(prefix/'bin')+os.pathsep+os.environ.get('PATH',''))
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'guard.exe'
            result=subprocess.run([str(prefix/'bin/clang.exe'),'-std=c11','-Wall','-Wextra','-Werror',
                '-I',str(ROOT/'port/src'),str(ROOT/'tests/test_knack2_native_aa.c'),'-o',str(exe)],
                capture_output=True,text=True,env=env,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            build=Path(os.environ.get('KNACK_AA_BUILD_DIR',ROOT/'port/out/gpu'))
            ninja=build/'build.ninja'
            if not ninja.is_file():self.skipTest('C guard passed; configured GPU build required for C++ guard')
            block=re.search(r'build CMakeFiles/tlggpu.dir/shadps4/video_core/renderer_vulkan/vk_temporal_upscaler.cpp.obj:.*?(?=\n\n)',ninja.read_text(),re.S)[0]
            includes=shlex.split(re.search(r'^  INCLUDES = (.*)$',block,re.M)[1])
            result=subprocess.run([str(prefix/'bin/clang++.exe'),'-std=c++23','-DNOMINMAX',
                '-include',str(ROOT/'port/gpu/shim/tlg_win_compat.h'),*includes,
                str(ROOT/'port/tests/test_knack2_native_aa.cpp'),'-o',str(exe)],
                capture_output=True,text=True,env=env,timeout=45)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
