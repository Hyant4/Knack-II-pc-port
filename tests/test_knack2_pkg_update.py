"""Synthetic update, rollback and crash recovery proofs; no game or real package."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import threading
import unittest
from unittest.mock import patch

from port.launcher.knack2 import pkg_update as update, validator


def sfo_bytes(values):
    keys, values_blob, entries = bytearray(), bytearray(), bytearray()
    for key, value in values.items():
        encoded = value.encode() + b'\0'
        entries.extend(struct.pack('<HHIII', len(keys), 0x204, len(encoded), len(encoded), len(values_blob)))
        keys.extend(key.encode() + b'\0')
        values_blob.extend(encoded)
    start = 20 + len(entries)
    return struct.pack('<4sIIII', b'\0PSF', 0x101, start, start + len(keys), len(values)) + entries + keys + values_blob


def pkg_bytes(values):
    sfo = sfo_bytes(values)
    header = bytearray(0x1020)
    header[:4] = b'\x7fCNT'
    struct.pack_into('>I', header, 0x10, 1)
    struct.pack_into('>I', header, 0x18, 0x1000)
    content = values['CONTENT_ID'].encode()
    header[0x40:0x40 + len(content)] = content
    struct.pack_into('>Q', header, 0x430, len(header) + len(sfo))
    struct.pack_into('>I', header, 0x43c, 0x10000)
    struct.pack_into('>6I', header, 0x1000, 0x1000, 0, 0, 0, len(header), len(sfo))
    return bytes(header) + sfo


class UpdateTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='knack-pkg-tests-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.game = self.root / 'game with arbitrary name'
        self.game.mkdir()
        self.values = {'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01', 'CATEGORY': 'gp',
                       'TITLE': 'Synthetic Knack II', 'CONTENT_ID': 'UP9000-CUSA08014_00-KNACK20000000000'}
        self.payload = {'sce_sys/param.sfo': sfo_bytes(self.values), 'game.elf': b'new synthetic code',
                        'data/new/file.txt': b'new asset', 'data/changed.txt': b'new asset version'}
        old_values = {**self.values, 'APP_VER': '01.00', 'CATEGORY': 'gd'}
        self.write('sce_sys/param.sfo', sfo_bytes(old_values))
        for name, content in {'eboot.bin': b'synthetic boot', 'game.elf': b'old synthetic code',
                              'data/changed.txt': b'old asset', 'data/untouched.txt': b'keep',
                              'data/lang/ena/synthetic.txt': b'english'}.items():
            self.write(name, content)
        self.profile = copy.deepcopy(validator.load_profile())
        self.profile['game_elf_variants'] = []
        self.profile['executables'] = {
            'eboot.bin': validator.digest_file(self.game / 'eboot.bin'),
            'game.elf': hashlib.sha256(self.payload['game.elf']).hexdigest()}
        for name in {m for items in self.profile['modules'].values() for m in items}:
            self.write('sce_module/' + name, b'synthetic module')
        self.module = patch.object(validator.link_modules, 'module')
        self.module.start()
        self.addCleanup(self.module.stop)
        self.pkg = self.root / 'update.pkg'
        self.pkg.write_bytes(pkg_bytes(self.values))
        self.installer = update.UpdateInstaller(extract=self.extract, validate=self.validate)
        self.initial = self.snapshot()

    def write(self, name, value):
        path = self.game / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)

    def extract(self, _pkg, destination, cancel, progress):
        for name, content in self.payload.items():
            path = destination / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)

    def validate(self, game, **kwargs):
        return validator.validate_knack2_dump(game, profile=self.profile, **kwargs)

    def snapshot(self):
        return {p.relative_to(self.game).as_posix(): p.read_bytes()
                for p in self.game.rglob('*') if p.is_file()}

    def install(self, **kwargs):
        return self.installer.install(self.game, self.installer.preview(self.game, self.pkg), **kwargs)

    def test_success_backs_up_only_replaced_files_and_overlay_keeps_base_modules(self):
        with patch.object(update.shutil, 'copytree', side_effect=AssertionError('No base copy')):
            result = self.install()
        self.assertTrue(result['valid'])
        root = update.transaction_root(self.game)
        backup = root / 'backup'
        actual = {p.relative_to(backup).as_posix(): p.read_bytes() for p in backup.rglob('*') if p.is_file()}
        self.assertEqual(actual, {n: self.initial[n] for n in self.payload if n in self.initial})
        self.assertFalse((backup / 'eboot.bin').exists())
        self.assertEqual((self.game / 'data/untouched.txt').read_bytes(), b'keep')
        self.assertFalse((root / 'stage').exists())
        self.assertFalse(update.pending_update(self.game))
        self.installer.restore(self.game)
        self.assertEqual(self.snapshot(), self.initial)
        self.assertFalse((self.game / 'data/new').exists())

    def test_region_mismatch_blocks_before_extraction(self):
        regions = {'CUSA08014': 'UP9000', 'CUSA07670': 'EP9000'}
        for game_title, pkg_title in [('CUSA08014', 'CUSA07670'), ('CUSA07670', 'CUSA08014')]:
            with self.subTest(game=game_title, update=pkg_title):
                game_values = {**self.values, 'TITLE_ID': game_title, 'APP_VER': '01.00', 'CATEGORY': 'gd',
                    'CONTENT_ID': f'{regions[game_title]}-{game_title}_00-KNACK20000000000'}
                (self.game / 'sce_sys/param.sfo').write_bytes(sfo_bytes(game_values))
                before = self.snapshot()
                self.pkg.write_bytes(pkg_bytes({**self.values, 'TITLE_ID': pkg_title,
                    'CONTENT_ID': f'{regions[pkg_title]}-{pkg_title}_00-KNACK20000000000'}))
                with patch.object(self.installer, 'extract') as extract, self.assertRaisesRegex(update.UpdateError, 'CUSA'):
                    self.install()
                extract.assert_not_called()
                self.assertEqual(self.snapshot(), before)

    def test_unknown_hash_preserves_installation(self):
        self.payload['game.elf'] = b'unknown bytes'
        with self.assertRaises(validator.KnackHashMismatchError):
            self.install()
        self.assertEqual(self.snapshot(), self.initial)
        self.assertEqual(update.read_journal(self.game)['state'], 'rolled_back')

    def test_incomplete_payload_still_requires_all_modules(self):
        (self.game / 'sce_module/libc.prx').unlink()
        before = self.snapshot()
        with self.assertRaises(validator.KnackModuleError):
            self.install()
        self.assertEqual(self.snapshot(), before)

    def test_failure_mid_apply_restores_originals_and_removes_new_files(self):
        replace = os.replace
        fired = False
        def fail_once(source, destination):
            nonlocal fired
            if Path(destination) == self.game / 'game.elf' and not fired:
                fired = True
                raise OSError('synthetic write failure')
            return replace(source, destination)
        with patch.object(update.os, 'replace', side_effect=fail_once), self.assertRaises(OSError):
            self.install()
        self.assertTrue(fired)
        self.assertEqual(self.snapshot(), self.initial)

    def test_final_validation_failure_rolls_back(self):
        def validate(game, **kwargs):
            if not kwargs:
                raise update.UpdateError('synthetic final failure')
            return self.validate(game, **kwargs)
        self.installer.validate = validate
        with self.assertRaisesRegex(update.UpdateError, 'final'):
            self.install()
        self.assertEqual(self.snapshot(), self.initial)

    def test_cancel_during_apply_rolls_back(self):
        cancel = threading.Event()
        replace = os.replace
        def apply_then_cancel(source, destination):
            value = replace(source, destination)
            if Path(destination) == self.game / 'data/changed.txt':
                cancel.set()
            return value
        with patch.object(update.os, 'replace', side_effect=apply_then_cancel), self.assertRaises(update.UpdateCancelled):
            self.install(cancel=cancel)
        self.assertEqual(self.snapshot(), self.initial)

    def test_insufficient_space_blocks_extractor(self):
        with patch.object(update.shutil, 'disk_usage', return_value=shutil._ntuple_diskusage(1, 1, 0)), \
             patch.object(self.installer, 'extract') as extract, self.assertRaisesRegex(update.UpdateError, 'Espaço'):
            self.install()
        extract.assert_not_called()
        self.assertEqual(self.snapshot(), self.initial)

    def test_package_replaced_after_preview_rejected(self):
        info = self.installer.preview(self.game, self.pkg)
        self.pkg.write_bytes(pkg_bytes({**self.values, 'TITLE': 'changed'}))
        with self.assertRaisesRegex(update.UpdateError, 'mudou'):
            self.installer.install(self.game, info)
        self.assertEqual(self.snapshot(), self.initial)

    def test_interrupted_apply_is_blocked_and_recoverable(self):
        self.install()
        record = update.read_journal(self.game)
        record['state'] = 'applying'
        update._journal(update.transaction_root(self.game), record)
        with self.assertRaisesRegex(update.UpdateError, 'interrompida'):
            update.assert_playable(self.game)
        self.installer.restore(self.game)
        self.assertEqual(self.snapshot(), self.initial)

    def test_failed_restore_retains_journal_and_backup(self):
        self.install()
        self.write('game.elf', b'user changed this after update')
        with self.assertRaisesRegex(update.UpdateError, 'alterado'):
            self.installer.restore(self.game)
        self.assertEqual((self.game / 'game.elf').read_bytes(), b'user changed this after update')
        self.assertTrue(update.pending_update(self.game))
        self.assertTrue((update.transaction_root(self.game) / 'backup/game.elf').exists())

    def test_previous_backup_archived_on_retry_without_overwrite(self):
        self.install()
        self.installer.restore(self.game)
        self.install()
        self.assertEqual(len(list(self.game.parent.glob('.knack-update-*-history-*'))), 1)

    def test_metadata_mismatch_after_extraction_rejected(self):
        self.payload['sce_sys/param.sfo'] = sfo_bytes({**self.values, 'TITLE_ID': 'CUSA07670'})
        with self.assertRaisesRegex(update.UpdateError, 'Metadados'):
            self.install()
        self.assertEqual(self.snapshot(), self.initial)

    def test_extract_failure_preserves_game_and_retry_works(self):
        extract = self.installer.extract
        self.installer.extract = lambda *args: (_ for _ in ()).throw(OSError('disk write'))
        with self.assertRaises(OSError):
            self.install()
        self.assertEqual(self.snapshot(), self.initial)
        self.installer.extract = extract
        self.install()

    def test_game_lock_excludes_other_installer(self):
        before = set(self.game.parent.iterdir())
        with update.game_lock(self.game):
            with self.assertRaisesRegex(update.UpdateError, 'em uso'):
                self.install()
        self.assertEqual(set(self.game.parent.iterdir()), before)

    def test_same_or_older_version_rejected(self):
        self.write('sce_sys/param.sfo', sfo_bytes(self.values))
        with self.assertRaisesRegex(update.UpdateError, 'já instalado'):
            self.installer.preview(self.game, self.pkg)

    def test_pkg_metadata_bounds_and_type(self):
        original = self.pkg.read_bytes()
        for blob in (b'not pkg', original[:-1], b'\0' * len(original),
                     pkg_bytes({**self.values, 'CATEGORY': 'gd'})):
            with self.subTest(size=len(blob)), self.assertRaises((update.UpdateError, ValueError)):
                self.pkg.write_bytes(blob)
                update.inspect_pkg(self.pkg)

    def test_path_traversal_windows_aliases_and_links_rejected(self):
        for name in ('../outside', '/absolute', 'C:/outside', 'x\\y', 'a:stream',
                     'CON', 'file.', 'x/../y', 'a//b', 'dir/NUL.txt'):
            with self.subTest(name=name), self.assertRaises(update.UpdateError):
                update.safe_path(self.game, name)
        outside = self.root / 'outside'
        outside.mkdir()
        try:
            (self.game / 'linked').symlink_to(outside, target_is_directory=True)
        except OSError:
            return  # Windows may deny unprivileged symlink creation.
        with self.assertRaises(update.UpdateError):
            update.safe_path(self.game, 'linked/file')


if __name__ == '__main__':
    unittest.main()
