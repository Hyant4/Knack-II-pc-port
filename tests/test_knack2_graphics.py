"""Window settings through config, real QML controls and native CPU contracts; no game."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import config

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or
                 Path(os.environ.get('LOCALAPPDATA', '')) / 'knack2-toolchain/msys64') / 'clang64'
COMPILER = TOOLCHAIN / 'bin/clang++.exe'


class WindowConfigTests(unittest.TestCase):
    def test_presets_roundtrip_preserves_native_options_and_line_endings(self):
        with tempfile.TemporaryDirectory() as temporary:
            ini = Path(temporary) / 'graphics.ini'
            prefix = b'# preserve\r\nngs2_render_mix=1\r\ngpu_readbacks=2\r\nupscaler=off\r\n'
            ini.write_bytes(prefix)
            for resolution in config.VALID_WINDOW_RESOLUTIONS:
                config.update_knack2_ini(ini, {'window_resolution': resolution, 'fsr1': True, 'rcas': False})
                self.assertEqual(config.load_knack2_ini(ini)['window_resolution'], resolution)
                self.assertTrue(ini.read_bytes().startswith(prefix))

    def test_bad_sizes_rejected_before_any_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            ini = Path(temporary) / 'graphics.ini'
            ini.write_bytes(b'# unchanged\nwindow_resolution=auto\n')
            for invalid in ('8K', '0x0', '-1x720', '1920x1080\nfsr1=1', '1920x1080\x00',
                            'nan', 1080, True, '1920X1080'):
                with self.subTest(value=invalid), self.assertRaises(ValueError):
                    config.update_knack2_ini(ini, {'window_resolution': invalid})
                self.assertEqual(ini.read_bytes(), b'# unchanged\nwindow_resolution=auto\n')

    @unittest.skipUnless(COMPILER.is_file(), 'Existing clang++ toolchain unavailable')
    def test_real_native_sizing_fsr_and_ini(self):
        with tempfile.TemporaryDirectory(prefix='knack-graphics-native-') as temporary:
            directory = Path(temporary)
            executable = directory / 'graphics.exe'
            env = {k: v for k, v in os.environ.items() if not k.startswith(('TLG_', 'ISS_'))}
            env['PATH'] = str(TOOLCHAIN / 'bin') + os.pathsep + env.get('PATH', '')
            build = subprocess.run([str(COMPILER), '-std=c++23', '-I', str(ROOT / 'port/gpu/shim'),
                                    str(ROOT / 'port/tests/test_knack2_graphics.cpp'),
                                    str(ROOT / 'port/gpu/shim/tlg_settings.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True, env=env, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable), str(directory / 'native.ini')],
                                 capture_output=True, text=True, env=env, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


HAS_QT = importlib.util.find_spec('PySide6') is not None
if HAS_QT:
    os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
    os.environ.setdefault('QT_QUICK_BACKEND', 'software')
    from PySide6.QtCore import QObject, Qt, qInstallMessageHandler
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQuickControls2 import QQuickStyle
    from PySide6.QtTest import QTest
    from port.launcher.knack2.ui.app import create_window
    from port.launcher.knack2.ui.real import RealBackend


@unittest.skipUnless(HAS_QT, 'PySide6 unavailable')
class WindowUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        QQuickStyle.setStyle('Basic')
        cls.app = QGuiApplication.instance() or QGuiApplication(['knack-graphics-tests'])
        cls.app.setQuitOnLastWindowClosed(False)

    def test_real_keyboard_selection_persistence_and_display_mode(self):
        with tempfile.TemporaryDirectory() as temporary, patch('subprocess.Popen',
                side_effect=AssertionError('Graphics UI test must not launch the game')):
            backend = RealBackend(data_dir=temporary)
            messages = []
            previous = qInstallMessageHandler(lambda kind, context, message: messages.append(message))
            engine = window = restored = None
            try:
                engine, window, _ = create_window(backend)
                window.setProperty('page', 1)
                QTest.qWait(60)
                selector = window.findChild(QObject, 'windowResolution')
                self.assertIsNotNone(selector)
                self.assertEqual(selector.property('count'), len(config.VALID_WINDOW_RESOLUTIONS))
                for index, resolution in enumerate(config.VALID_WINDOW_RESOLUTIONS):
                    if index:
                        selector.forceActiveFocus()
                        QTest.keyClick(window, Qt.Key_Down)
                        QTest.qWait(15)
                    self.assertEqual(selector.property('currentIndex'), index)
                    self.assertEqual(backend.settings['window_resolution'], resolution)
                    backend.saveSettings()
                    restored = RealBackend(data_dir=temporary)
                    self.assertEqual(restored.settings['window_resolution'], resolution)
                    restored.shutdown()
                    restored = None
                for mode in ('borderless', 'fullscreen', 'windowed'):
                    backend.setSetting('display_mode', mode)
                    QTest.qWait(15)
                    self.assertEqual(selector.property('enabled'), mode == 'windowed')
                    self.assertEqual(backend.settings['window_resolution'], config.VALID_WINDOW_RESOLUTIONS[-1])
                provider = window.findChild(QObject, 'temporalUpscaler')
                quality = window.findChild(QObject, 'temporalPreset')
                self.assertIsNotNone(provider)
                self.assertIsNotNone(quality)
                self.assertFalse(quality.property('enabled'))
                for provider_index, provider_name in enumerate(('fsr3', 'fsr4'), 1):
                    provider.forceActiveFocus()
                    QTest.keyClick(window, Qt.Key_Down)
                    QTest.qWait(15)
                    self.assertEqual(provider.property('currentIndex'), provider_index)
                    self.assertEqual(backend.settings['upscaler'], provider_name)
                    self.assertTrue(quality.property('enabled'))
                for index in range(1, 4):
                    quality.forceActiveFocus()
                    QTest.keyClick(window, Qt.Key_Down)
                    QTest.qWait(15)
                    self.assertEqual(quality.property('currentIndex'), index)
                    self.assertEqual(str(backend.settings['preset']), str(index))
                    backend.saveSettings()
                    restored = RealBackend(data_dir=temporary)
                    self.assertEqual(restored.settings['upscaler'], 'fsr4')
                    self.assertEqual(str(restored.settings['preset']), str(index))
                    restored.shutdown()
                    restored = None
                internal = window.findChild(QObject, 'internalResolution')
                self.assertIsNotNone(internal)
                for index, size in enumerate(config.VALID_INTERNAL_RESOLUTIONS[1:], 1):
                    internal.forceActiveFocus()
                    QTest.keyClick(window, Qt.Key_Down)
                    QTest.qWait(15)
                    self.assertEqual(internal.property('currentIndex'), index)
                    self.assertEqual(backend.settings['internal_resolution'], size)
                    self.assertFalse(quality.property('enabled'))
                    backend.saveSettings()
                    restored = RealBackend(data_dir=temporary)
                    self.assertEqual(restored.settings['internal_resolution'], size)
                    restored.shutdown()
                    restored = None
                for _ in config.VALID_INTERNAL_RESOLUTIONS[1:]:
                    internal.forceActiveFocus()
                    QTest.keyClick(window, Qt.Key_Up)
                    QTest.qWait(15)
                self.assertEqual(backend.settings['internal_resolution'], 'auto')
                self.assertTrue(quality.property('enabled'))
            finally:
                if restored:
                    restored.shutdown()
                if window:
                    window.close()
                backend.shutdown()
                if engine:
                    engine.deleteLater()
                QTest.qWait(25)
                qInstallMessageHandler(previous)
            self.assertEqual([m for m in messages if any(s in m for s in
                ('ReferenceError', 'TypeError', 'binding loop', 'Binding loop', 'failed to load', 'Error:'))], [])


if __name__ == '__main__':
    unittest.main()
