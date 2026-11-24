"""Unknown executable fingerprints are runnable only after structural checks."""
import copy
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import validator
from tests.test_knack2_regions import sfo_bytes

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('open_validation_supervisor', ROOT / 'run.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


def synthetic_elf(marker=b'test'):
    data = bytearray(4352)
    ident = b'\x7fELF\x02\x01\x01' + bytes(9)
    struct.pack_into('<16sHHIQQQIHHHHHH', data, 0, ident, 0xfe10, 62, 1,
                     0x400000, 64, 0, 0, 64, 56, 1, 0, 0, 0)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 4096, 0x400000, 0x400000, 256, 256, 4096)
    data[4096:] = marker.ljust(256, b'!')
    return bytes(data)


def synthetic_self():
    elf = synthetic_elf(b'SELF')
    data = bytearray(512)
    struct.pack_into('<4sBBBBIHHQHHI', data, 0, b'O\x15=\x1d', 0, 1, 1, 0x12,
                     0x101, 184, 0, len(data), 1, 0x22, 0)
    struct.pack_into('<QQQQ', data, 32, 0x800, 256, 256, 256)
    data[64:184] = elf[:120]
    data[256:] = elf[4096:]
    return bytes(data)


class OpenValidationTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix='knack-open-validation-')
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.game = self.root / 'game'
        (self.game / 'sce_sys').mkdir(parents=True)
        (self.game / 'sce_sys/param.sfo').write_bytes(sfo_bytes(
            'CUSA07670', CONTENT_ID='EP9000-CUSA07670_00-KNACK20000000000'))
        (self.game / 'sce_module').mkdir()
        for name in ('libc.prx', 'libSceFios2.prx', 'libSceJobManager.prx'):
            (self.game / 'sce_module' / name).write_bytes(b'synthetic module')
        (self.game / 'eboot.bin').write_bytes(synthetic_self())
        (self.game / 'game.elf').write_bytes(synthetic_elf())
        self.profile = copy.deepcopy(validator.load_profile(ROOT / 'profiles/CUSA07670-01.01.json'))

    def validate(self):
        with patch.object(validator.link_modules, 'module'):
            return validator.validate_knack2_dump(self.game, repo_root=ROOT, allow_unverified=True)

    def test_unknown_structurally_valid_executables_are_unverified_not_blocked(self):
        before = {name: (self.game / name).read_bytes() for name in ('eboot.bin', 'game.elf')}
        result = self.validate()
        self.assertTrue(result['valid'])
        self.assertFalse(result['verified'])
        self.assertEqual(result['unverified_executables'], ['eboot.bin', 'game.elf'])
        self.assertEqual(result['executable_layout'], validator.UNVERIFIED_LAYOUT)
        for name, data in before.items():
            self.assertEqual((self.game / name).read_bytes(), data)
            self.assertEqual(result['executable_hashes'][name], hashlib.sha256(data).hexdigest())

    def test_known_reference_hashes_keep_existing_status(self):
        for name in self.profile['executables']:
            self.profile['executables'][name] = validator.digest_file(self.game / name)
        with patch.object(validator.link_modules, 'module'):
            result = validator.validate_knack2_dump(self.game, profile=self.profile)
        self.assertTrue(result['verified'])
        self.assertEqual(result['unverified_executables'], [])
        self.assertEqual(result['executable_layout'], 'elf-0b7843d3')

    def test_unknown_eboot_with_known_game_uses_experimental_layout(self):
        self.profile['executables']['game.elf'] = validator.digest_file(self.game / 'game.elf')
        profiles = self.root / 'profiles'
        profiles.mkdir()
        (profiles / 'CUSA07670-01.01.json').write_text(json.dumps(self.profile), encoding='utf-8')
        with patch.object(validator.link_modules, 'module'):
            result = validator.validate_knack2_dump(
                self.game, repo_root=self.root, allow_unverified=True)
        self.assertEqual(result['unverified_executables'], ['eboot.bin'])
        self.assertEqual(result['executable_layout'], validator.UNVERIFIED_LAYOUT)

    def test_invalid_unknown_executable_still_fails(self):
        for data in (b'', b'not an executable', synthetic_elf()[:-2]):
            with self.subTest(data_length=len(data)):
                (self.game / 'eboot.bin').write_bytes(data)
                with self.assertRaises(validator.KnackExecutableError):
                    self.validate()

    def test_entry_outside_executable_segment_still_fails(self):
        data = bytearray(synthetic_elf())
        struct.pack_into('<Q', data, 24, 0x900000)
        (self.game / 'eboot.bin').write_bytes(data)
        with self.assertRaises(validator.KnackExecutableError):
            self.validate()

    def test_encrypted_or_missing_self_segment_still_fails(self):
        for flags in (0x802, 0x808, 0):
            with self.subTest(flags=flags):
                data = bytearray(synthetic_self())
                struct.pack_into('<Q', data, 32, flags)
                (self.game / 'eboot.bin').write_bytes(data)
                with self.assertRaises(validator.KnackExecutableError):
                    self.validate()

    def test_missing_or_invalid_module_still_fails(self):
        with patch.object(validator.link_modules, 'module', side_effect=ValueError('invalid module')):
            with self.assertRaises(validator.KnackModuleError):
                validator.validate_knack2_dump(self.game, repo_root=ROOT, allow_unverified=True)
        (self.game / 'sce_module/libc.prx').unlink()
        with self.assertRaises(validator.KnackModuleError):
            self.validate()

    def test_unrecognized_content_identity_still_fails(self):
        for title, version in (('CUSA99999', '01.01'), ('CUSA07670', '01.02')):
            with self.subTest(title=title, version=version):
                (self.game / 'sce_sys/param.sfo').write_bytes(sfo_bytes(title, version))
                with self.assertRaises(validator.KnackVersionMismatchError):
                    self.validate()

    def test_explicit_profile_rejects_unknown_hashes(self):
        with self.assertRaises(validator.KnackHashMismatchError):
            supervisor.validate_game(self.game, self.profile)
        with self.assertRaises(validator.KnackHashMismatchError):
            validator.validate_knack2_dump(self.game, profile=self.profile, allow_unverified=True)

    def test_session_records_unverified_status_without_running_guest(self):
        repo = self.root / 'repo'
        (repo / 'profiles').mkdir(parents=True)
        (repo / 'profiles/CUSA07670-01.01.json').write_text(json.dumps(self.profile))
        runtime = self.root / 'synthetic-runtime'
        runtime.write_bytes(b'not executed')
        runtime.chmod(0o700)
        out = self.root / 'out'
        output = io.StringIO()
        with patch.object(supervisor, 'ROOT', repo), \
             patch.object(supervisor.link_modules, 'module'), \
             patch.object(supervisor, 'prepare_image', side_effect=lambda g, p, n, o, **kw: o / n), \
             patch.object(supervisor.subprocess, 'run', return_value=subprocess.CompletedProcess(
                 [], 0, stdout='KNACK2_UNVERIFIED_DUMPS_V1=1\n')) as preparation, \
             patch.object(supervisor, 'run_child', return_value=0) as child, \
             contextlib.redirect_stdout(output):
            result = supervisor.main(['--game-dir', str(self.game), '--out', str(out),
                                      '--runtime', str(runtime)])
        self.assertEqual(result, 0)
        child.assert_called_once()
        self.assertEqual(preparation.call_count, 3)
        self.assertIn(validator.UNVERIFIED_WARNING, output.getvalue())
        report = json.loads(next(out.glob('session-*/session.json')).read_text())
        self.assertEqual(report['verification_status'], 'unverified')
        self.assertFalse(report['executable_layout_verified'])
        self.assertEqual(report['unverified_executables'], ['eboot.bin', 'game.elf'])
        self.assertEqual(report['executable_hashes']['eboot.bin'], validator.digest_file(self.game / 'eboot.bin'))

    def test_unknown_update_overlay_is_checked_without_replacing_base(self):
        for name in self.profile['executables']:
            self.profile['executables'][name] = validator.digest_file(self.game / name)
        original = (self.game / 'eboot.bin').read_bytes()
        overlay = self.root / 'overlay'
        (overlay / 'sce_sys').mkdir(parents=True)
        (overlay / 'sce_sys/param.sfo').write_bytes(sfo_bytes(
            'CUSA07670', CONTENT_ID='EP9000-CUSA07670_00-KNACK20000000000'))
        (overlay / 'eboot.bin').write_bytes(synthetic_elf(b'updated executable'))
        with self.assertRaises(validator.KnackHashMismatchError):
            validator.validate_knack2_dump(
                self.game, profile=self.profile, overlay_dir=overlay, allow_unverified=True)
        self.assertEqual((self.game / 'eboot.bin').read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
