"""Base PKG extraction beside its source; synthetic files only."""
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import threading
import unittest
from unittest.mock import patch

from port.launcher.knack2 import pkg_base as base, pkg_update as update, validator
from tests.test_knack2_pkg_update import pkg_bytes, sfo_bytes


class BasePkgTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='knack-base-pkg-')
        self.addCleanup(temporary.cleanup)
        self.parent = Path(temporary.name)
        self.pkg = self.parent / 'Knack [US] 1.00.pkg'
        self.values = {'TITLE_ID': 'CUSA08014', 'APP_VER': '01.00', 'CATEGORY': 'gd',
                       'CONTENT_ID': 'UP9000-CUSA08014_00-KNACK20000000000'}
        self.pkg.write_bytes(pkg_bytes(self.values))
        self.original = self.pkg.read_bytes()
        (self.parent / 'keep.txt').write_bytes(b'original sibling')
        self.result = {'title_id': 'CUSA08014', 'version': '01.00', 'languages': ['en-US']}
        self.installer = base.BaseExtractor(extract=self.extract, validate=lambda path: self.result)

    def extract(self, pkg, stage, cancel, progress):
        self.assertEqual(pkg, self.pkg)
        self.assertEqual(stage.parent.parent, self.parent)
        (stage / 'eboot.bin').write_bytes(b'synthetic executable')
        (stage / 'game.elf').write_bytes(b'synthetic game')
        (stage / 'data').mkdir()
        (stage / 'data/hello').write_bytes(b'synthetic payload')
        (stage / 'sce_sys').mkdir()
        (stage / 'sce_sys/param.sfo').write_bytes(sfo_bytes(self.values))

    def preview(self):
        return self.installer.preview(self.pkg)

    def assert_originals(self):
        self.assertEqual(self.pkg.read_bytes(), self.original)
        self.assertEqual((self.parent / 'keep.txt').read_bytes(), b'original sibling')

    def test_extracts_directly_into_pkg_parent_without_copying_or_replacing_pkg(self):
        info = self.preview()
        self.assertEqual(Path(info['destination']), self.parent)
        result = self.installer.install(info)
        self.assertEqual(result['destination'], str(self.parent))
        self.assertEqual((self.parent / 'data/hello').read_bytes(), b'synthetic payload')
        self.assertTrue((self.parent / 'eboot.bin').is_file())
        self.assertFalse((self.parent / self.pkg.stem).exists())
        self.assertFalse((self.parent / base.AREA).exists())
        self.assert_originals()

    def test_default_validator_rejects_wrong_executable_hash_before_publication(self):
        self.installer.validate = validator.validate_knack2_dump
        with self.assertRaises(validator.KnackHashMismatchError):
            self.installer.install(self.preview())
        self.assertFalse((self.parent / 'eboot.bin').exists())
        self.assertFalse((self.parent / base.AREA).exists())
        self.assert_originals()

    def test_existing_game_root_or_case_alias_is_never_merged(self):
        for name in ('data', 'DATA', 'eboot.bin'):
            with self.subTest(name=name):
                path = self.parent / name
                path.write_bytes(b'keep')
                with self.assertRaisesRegex(update.UpdateError, 'Já existe'):
                    self.preview()
                self.assertEqual(path.read_bytes(), b'keep')
                path.unlink()

    def test_update_dlc_and_unproven_region_remain_rejected(self):
        for values in ({**self.values, 'CATEGORY': 'gp'}, {**self.values, 'CATEGORY': 'ac'},
                       {**self.values, 'TITLE_ID': 'CUSA07670',
                        'CONTENT_ID': 'EP9000-CUSA07670_00-KNACK20000000000'}):
            with self.subTest(values=values):
                self.pkg.write_bytes(pkg_bytes(values))
                with self.assertRaises(update.UpdateError if values['CATEGORY'] != 'gd' else validator.KnackProfileError):
                    self.preview()

    def test_destination_cannot_be_overridden(self):
        info = self.preview()
        info['destination'] = str(self.parent / 'other')
        with self.assertRaisesRegex(update.UpdateError, 'pasta do PKG'):
            self.installer.install(info)
        self.assert_originals()

    def test_changed_package_after_confirmation_or_during_extraction_is_rejected(self):
        info = self.preview()
        self.pkg.write_bytes(pkg_bytes({**self.values, 'TITLE': 'changed'}))
        with self.assertRaisesRegex(update.UpdateError, 'após a confirmação'):
            self.installer.install(info)
        self.pkg.write_bytes(self.original)
        def changed(pkg, stage, cancel, progress):
            self.extract(pkg, stage, cancel, progress)
            pkg.write_bytes(pkg_bytes({**self.values, 'TITLE': 'changed'}))
        self.installer.extract = changed
        with self.assertRaisesRegex(update.UpdateError, 'durante a extração'):
            self.installer.install(self.preview())
        self.assertFalse((self.parent / 'eboot.bin').exists())

    def test_insufficient_space_stops_before_extractor(self):
        info = self.preview()
        with patch.object(base.shutil, 'disk_usage', return_value=shutil._ntuple_diskusage(100, 99, 1)), \
             patch.object(self.installer, 'extract') as extractor:
            with self.assertRaisesRegex(update.UpdateError, 'Espaço insuficiente'):
                self.installer.install(info)
            extractor.assert_not_called()

    def test_cancellation_cleans_only_new_staging_files(self):
        event = threading.Event()
        def cancelled(pkg, stage, cancel, progress):
            self.extract(pkg, stage, cancel, progress)
            cancel.set()
        self.installer.extract = cancelled
        with self.assertRaises(update.UpdateCancelled):
            self.installer.install(self.preview(), cancel=event)
        self.assertFalse((self.parent / base.AREA).exists())
        self.assertFalse((self.parent / 'eboot.bin').exists())
        self.assert_originals()

    def test_cancellation_during_package_inspection(self):
        event = threading.Event(); event.set()
        with self.assertRaises(update.UpdateCancelled):
            self.installer.preview(self.pkg, cancel=event)
        self.assert_originals()

    def test_failure_halfway_through_publication_rolls_back_new_roots(self):
        rename = base._rename
        calls = []
        def fail(source, target):
            calls.append(target)
            if len(calls) == 2:
                raise OSError('synthetic full disk')
            rename(source, target)
        with patch.object(base, '_rename', side_effect=fail):
            with self.assertRaises(OSError):
                self.installer.install(self.preview())
        self.assertFalse((self.parent / 'data').exists())
        self.assertFalse((self.parent / base.AREA).exists())
        self.assert_originals()

    def test_concurrent_file_is_preserved_without_overwrite(self):
        rename = base._rename
        def race(source, target):
            if source.name == 'eboot.bin' and not target.exists():
                target.write_bytes(b'concurrent writer')
            rename(source, target)
        with patch.object(base, '_rename', side_effect=race):
            with self.assertRaises(OSError):
                self.installer.install(self.preview())
        self.assertEqual((self.parent / 'eboot.bin').read_bytes(), b'concurrent writer')
        self.assertFalse((self.parent / 'data').exists())
        self.assert_originals()

    def test_crash_journal_recovers_only_published_owned_roots(self):
        root = self.parent / base.AREA
        payload = root / 'payload'; payload.mkdir(parents=True)
        self.extract(self.pkg, payload, threading.Event(), lambda text: None)
        roots = [{'name': p.name, 'identity': base._identity(p)} for p in payload.iterdir()]
        update._journal(root, {'schema': 1, 'destination': str(self.parent), 'state': 'publishing', 'roots': roots})
        base._rename(payload / 'data', self.parent / 'data')
        self.preview()
        self.assertFalse((self.parent / 'data').exists())
        self.assertFalse(root.exists())
        self.assert_originals()

    def test_malformed_existing_area_is_preserved(self):
        root = self.parent / base.AREA; root.mkdir()
        (root / 'keep').write_bytes(b'foreign')
        with self.assertRaisesRegex(update.UpdateError, 'Preserve esta pasta'):
            self.preview()
        self.assertEqual((root / 'keep').read_bytes(), b'foreign')

    def test_all_new_user_messages_translate_without_changing_paths(self):
        from port.launcher.knack2.ui.i18n import translate
        source = f'Já existe no destino: {self.parent / "data"}. Nenhum arquivo foi sobrescrito.'
        for language in ('en-US', 'ru-RU'):
            result = translate(language, source)
            self.assertNotEqual(source, result)
            self.assertIn(str(self.parent / 'data'), result)


if __name__ == '__main__':
    unittest.main()
