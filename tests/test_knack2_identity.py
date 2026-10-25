"""Identity and PE preservation checks. Runtime binaries are read as data only."""
import ctypes
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2.identity import game_identity_environment, runtime_names, runtime_path, LAUNCHER_APP_ID, set_launcher_app_id, identity_assets
from port.launcher.knack2.branding import convert_original_icon, pe_contract, read_resources, icon_resources, VERSION_STRINGS, brand_runtime, digest

ROOT = Path(__file__).resolve().parents[1]


class IdentityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        directory = Path(self.temporary.name)
        self.storage = patch('port.launcher.knack2.identity.application_data_root', return_value=directory)
        self.storage.start()
        self.addCleanup(self.storage.stop)
        self.ui = identity_assets()
        from PySide6.QtGui import QImage
        original = QImage(512, 512, QImage.Format_RGB32)
        original.fill(0xff123456)
        source = directory / 'synthetic-original.png'
        original.save(str(source))
        convert_original_icon(source, self.ui)

    def test_canonical_name_and_legacy_diagnostic_fallback(self):
        with tempfile.TemporaryDirectory() as temporary, patch(
                'port.launcher.knack2.identity.runtime_names', return_value=('knack2-runtime.exe', 'tlg-probe.exe')):
            port = Path(temporary)
            (port/'out').mkdir()
            self.assertEqual(runtime_path(port).name, 'knack2-runtime.exe')
            (port/'out/tlg-probe.exe').write_bytes(b'not executable')
            self.assertEqual(runtime_path(port).name, 'tlg-probe.exe')
            (port/'out/knack2-runtime.exe').write_bytes(b'not executable')
            self.assertEqual(runtime_path(port).name, 'knack2-runtime.exe')

    def test_linux_runtime_name_and_legacy_diagnostic_fallback(self):
        self.assertEqual(runtime_names(is_windows=False), ('knack2-runtime', 'tlg-probe'))
        with tempfile.TemporaryDirectory() as temporary, patch(
                'port.launcher.knack2.identity.runtime_names', return_value=('knack2-runtime', 'tlg-probe')):
            port = Path(temporary)
            (port/'out').mkdir()
            self.assertEqual(runtime_path(port).name, 'knack2-runtime')
            (port/'out/tlg-probe').write_bytes(b'not executable')
            self.assertEqual(runtime_path(port).name, 'tlg-probe')
            (port/'out/knack2-runtime').write_bytes(b'not executable')
            self.assertEqual(runtime_path(port).name, 'knack2-runtime')
            (port/'out-linux').mkdir()
            (port/'out-linux/tlg-probe').write_bytes(b'not executable')
            self.assertEqual(runtime_path(port), port/'out-linux/tlg-probe')

    def test_fixed_game_identity_and_local_bmp(self):
        env = game_identity_environment(ROOT)
        self.assertEqual(env['TLG_APP_NAME'], 'Knack II')
        self.assertEqual(env['TLG_APP_ID'], 'KnackII.PCPort.Game')
        icon = Path(env['TLG_WINDOW_ICON'])
        self.assertEqual(icon, (self.ui/'Knack.bmp').resolve())
        self.assertEqual(icon.read_bytes()[:2], b'BM')
        group, images = icon_resources(self.ui/'Knack.ico')
        self.assertEqual(len(images), 6)
        self.assertEqual(len(group), 6+14*6)

    def test_launcher_uses_knack_app_id_and_icon(self):
        if os.name != 'nt':
            self.skipTest('Windows shell identity')
        from PySide6.QtGui import QGuiApplication, QIcon
        app = QGuiApplication.instance() or QGuiApplication([])
        set_launcher_app_id()
        shell = ctypes.WinDLL('shell32')
        function = shell.GetCurrentProcessExplicitAppUserModelID
        function.argtypes = [ctypes.POINTER(ctypes.c_wchar_p)]
        function.restype = ctypes.c_long
        value = ctypes.c_wchar_p()
        self.assertEqual(function(ctypes.byref(value)), 0)
        try:
            self.assertEqual(value.value, LAUNCHER_APP_ID)
        finally:
            free = ctypes.WinDLL('ole32').CoTaskMemFree
            free.argtypes = [ctypes.c_void_p]
            free(ctypes.cast(value, ctypes.c_void_p))
        icon = QIcon(str(self.ui/'Knack.ico'))
        self.assertFalse(icon.pixmap(32, 32).isNull())
        self.assertFalse(icon.pixmap(256, 256).isNull())

    def test_branding_never_overwrites_an_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            original = Path(temporary)/'original'
            existing = Path(temporary)/'existing'
            original.write_bytes(b'original')
            existing.write_bytes(b'preserve')
            with self.assertRaises(ValueError):
                brand_runtime(original, existing, self.ui/'Knack.ico', original, '')
            self.assertEqual(existing.read_bytes(), b'preserve')
            self.assertEqual(original.read_bytes(), b'original')


@unittest.skipUnless(os.name == 'nt' and os.environ.get('KNACK_BRAND_SOURCE'), 'Requires explicit runtime artifact pair')
class RuntimeResourceTests(unittest.TestCase):
    def setUp(self):
        self.ui = identity_assets()
        self.source = Path(os.environ['KNACK_BRAND_SOURCE'])
        self.derived = ROOT/'port/out/knack2-runtime.exe'

    @unittest.skipUnless(os.environ.get('KNACK_BRAND_ORIGINAL_ICON'), 'Requires explicit original icon')
    def test_sdl_bitmap_is_original_game_art_without_redrawing(self):
        from PySide6.QtCore import Qt
        from PySide6.QtGui import QImage
        source = QImage(os.environ['KNACK_BRAND_ORIGINAL_ICON'])
        expected = source.scaled(256, 256, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)
        self.assertEqual(QImage(str(self.ui/'Knack.bmp')).convertToFormat(QImage.Format_RGB32),
                         expected.convertToFormat(QImage.Format_RGB32))

    def test_all_nonresource_bytes_addresses_and_loader_contracts_preserved(self):
        self.assertEqual(pe_contract(self.source), pe_contract(self.derived))
        self.assertEqual(digest(self.source), '3db2a9d1240a20e7de1815ddf0946c34277808543f1969c076b533239d252d65')
        resources = read_resources(self.derived)
        group, images = icon_resources(self.ui/'Knack.ico')
        self.assertEqual(resources[(14, 1, 0)], group)
        for index, image in images.items():
            self.assertEqual(resources[(3, index, 0)], image)
        old = read_resources(self.source)
        for key, payload in old.items():
            if key[0] not in (3, 14, 16, 24):
                self.assertEqual(resources[key], payload)

    def test_windows_file_properties_expose_knack_not_other_ports(self):
        version = ctypes.WinDLL('version', use_last_error=True)
        version.GetFileVersionInfoSizeW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p]
        version.GetFileVersionInfoSizeW.restype = ctypes.c_uint32
        version.GetFileVersionInfoW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        version.VerQueryValueW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p,
                                         ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint32)]
        size = version.GetFileVersionInfoSizeW(str(self.derived), None)
        self.assertGreater(size, 0)
        data = ctypes.create_string_buffer(size)
        self.assertTrue(version.GetFileVersionInfoW(str(self.derived), 0, size, data))
        for key, expected in VERSION_STRINGS.items():
            pointer, count = ctypes.c_void_p(), ctypes.c_uint32()
            query = '\\StringFileInfo\\040904B0\\'+key
            self.assertTrue(version.VerQueryValueW(data, query, ctypes.byref(pointer), ctypes.byref(count)))
            self.assertEqual(ctypes.wstring_at(pointer), expected)


if __name__ == '__main__':
    unittest.main()
