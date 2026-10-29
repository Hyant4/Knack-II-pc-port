"""Native encrypted synthetic PKG extraction. Requires the isolated helper selftest build."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import hashlib
import shutil

from tests.test_knack2_pkg_update import sfo_bytes
from port.launcher.knack2 import pkg_update, validator

NATIVE = os.environ.get('KNACK_PKG_NATIVE_DIR')


@unittest.skipUnless(NATIVE, 'Native helper selftest build not selected')
class NativePkgTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='knack-native-pkg-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.stage = self.root / 'stage'
        self.stage.mkdir()
        self.sfo = self.root / 'metadata'
        self.sfo.write_bytes(sfo_bytes({'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01',
            'CATEGORY': 'gp', 'CONTENT_ID': 'UP9000-CUSA08014_00-KNACK20000000000'}))
        self.pkg = self.root / 'synthetic.pkg'

    def generate(self, name='hello.txt'):
        subprocess.run([str(Path(NATIVE) / 'KnackPkgFixture.exe'), str(self.sfo), str(self.pkg), name],
                       check=True, timeout=10, capture_output=True)

    def test_offsets_above_4gib_preserve_all_bits_without_large_file(self):
        result = subprocess.run([str(Path(NATIVE) / 'KnackPkgFixture.exe'), '--seek64', str(self.root / 'empty')],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual((self.root / 'empty').stat().st_size, 0)

    def extract(self):
        return subprocess.run([str(Path(NATIVE) / 'KnackPkg.exe'), str(self.pkg), str(self.stage)],
                              capture_output=True, timeout=10)

    def test_encrypted_synthetic_update_extracts_exact_bytes(self):
        self.generate()
        result = self.extract()
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual((self.stage / 'hello.txt').read_bytes(), b'Synthetic PKG payload')
        self.assertEqual((self.stage / 'sce_sys/param.sfo').read_bytes(), self.sfo.read_bytes())
        self.assertEqual((self.stage / 'sce_sys/npbind.dat').read_bytes(), bytes((i * 7) & 255 for i in range(532)))
        self.assertIn(b'KNACK_PKG_COMPLETE_V1', result.stdout)

    def test_base_mode_extracts_and_default_update_mode_still_rejects_base(self):
        self.sfo.write_bytes(sfo_bytes({'TITLE_ID': 'CUSA08014', 'APP_VER': '01.00',
            'CATEGORY': 'gd', 'CONTENT_ID': 'UP9000-CUSA08014_00-KNACK20000000000'}))
        subprocess.run([str(Path(NATIVE) / 'KnackPkgFixture.exe'), str(self.sfo), str(self.pkg),
                        'hello.txt', '--base'], check=True, timeout=10, capture_output=True)
        rejected = self.extract()
        self.assertEqual(rejected.returncode, 1)
        self.assertIn(b'Only update PKGs', rejected.stderr)
        result = subprocess.run([str(Path(NATIVE) / 'KnackPkg.exe'), '--base', str(self.pkg), str(self.stage)],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual((self.stage / 'hello.txt').read_bytes(), b'Synthetic PKG payload')

    def test_base_mode_rejects_update_and_plan_does_not_extract_payload(self):
        self.generate()
        result = subprocess.run([str(Path(NATIVE) / 'KnackPkg.exe'), '--base', str(self.pkg), str(self.stage)],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.sfo.write_bytes(sfo_bytes({'TITLE_ID': 'CUSA08014', 'APP_VER': '01.00',
            'CATEGORY': 'gd', 'CONTENT_ID': 'UP9000-CUSA08014_00-KNACK20000000000'}))
        subprocess.run([str(Path(NATIVE) / 'KnackPkgFixture.exe'), str(self.sfo), str(self.pkg),
                        'hello.txt', '--base'], check=True, timeout=10, capture_output=True)
        result = subprocess.run([str(Path(NATIVE) / 'KnackPkg.exe'), '--base-plan', str(self.pkg), str(self.stage)],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertIn(b'KNACK_PKG_PLAN_V1', result.stdout)
        self.assertFalse((self.stage / 'hello.txt').exists())

    def test_native_base_extractor_publishes_beside_pkg_and_preserves_original(self):
        from port.launcher.knack2.pkg_base import BaseExtractor
        self.sfo.write_bytes(sfo_bytes({'TITLE_ID': 'CUSA08014', 'APP_VER': '01.00',
            'CATEGORY': 'gd', 'CONTENT_ID': 'UP9000-CUSA08014_00-KNACK20000000000'}))
        subprocess.run([str(Path(NATIVE) / 'KnackPkgFixture.exe'), str(self.sfo), str(self.pkg),
                        'hello.txt', '--base'], check=True, timeout=10, capture_output=True)
        before = hashlib.sha256(self.pkg.read_bytes()).hexdigest()
        helper_root = self.root / 'launcher'
        target = helper_root / 'port/out/knack-pkg-helper'; target.mkdir(parents=True)
        for source in Path(NATIVE).iterdir():
            if source.suffix.lower() == '.dll' or source.name == 'KnackPkg.exe':
                shutil.copy2(source, target / source.name)
        def validate(path):
            self.assertEqual((path / 'hello.txt').read_bytes(), b'Synthetic PKG payload')
            return {'title_id': 'CUSA08014', 'version': '01.00'}
        extractor = BaseExtractor(validate=validate)
        with patch.object(pkg_update, 'project_root', return_value=helper_root):
            result = extractor.install(extractor.preview(self.pkg))
        self.assertEqual(result['destination'], str(self.root))
        self.assertEqual((self.root / 'hello.txt').read_bytes(), b'Synthetic PKG payload')
        self.assertEqual(hashlib.sha256(self.pkg.read_bytes()).hexdigest(), before)

    def test_traversal_never_writes_outside_staging(self):
        self.generate('../escape.txt')
        result = self.extract()
        self.assertEqual(result.returncode, 1, result.stderr.decode(errors='replace'))
        self.assertFalse((self.root / 'escape.txt').exists())
        self.assertIn(b'Unsafe PFS filename', result.stderr)

    def test_truncated_package_reports_failure(self):
        self.generate()
        self.pkg.write_bytes(self.pkg.read_bytes()[:-1])
        result = self.extract()
        self.assertEqual(result.returncode, 1)
        self.assertFalse((self.stage / 'hello.txt').exists())

    def test_nonempty_destination_is_never_overwritten(self):
        self.generate()
        (self.stage / 'hello.txt').write_bytes(b'original')
        result = self.extract()
        self.assertEqual(result.returncode, 1)
        self.assertEqual((self.stage / 'hello.txt').read_bytes(), b'original')

    def test_real_helper_through_installer_and_backup_restore(self):
        self.generate()
        game = self.root / 'game'
        (game / 'sce_sys').mkdir(parents=True)
        values = validator.prepare.load_sfo(self.sfo)
        (game / 'sce_sys/param.sfo').write_bytes(sfo_bytes({**values, 'APP_VER': '01.00', 'CATEGORY': 'gd'}))
        profile = {'title_id': 'CUSA08014', 'version': '01.01', 'executables': {}, 'modules': {}}
        for name in ('eboot.bin', 'game.elf'):
            content = ('invented ' + name).encode()
            (game / name).write_bytes(content)
            profile['executables'][name] = hashlib.sha256(content).hexdigest()
            profile['modules'][name] = ['libc.prx']
        (game / 'sce_module').mkdir()
        (game / 'sce_module/libc.prx').write_bytes(b'synthetic module')
        before = {p.relative_to(game).as_posix(): p.read_bytes() for p in game.rglob('*') if p.is_file()}
        helper_root = self.root / 'launcher'
        target = helper_root / 'port/out/knack-pkg-helper'
        target.mkdir(parents=True)
        for source in Path(NATIVE).iterdir():
            if source.suffix.lower() == '.dll' or source.name == 'KnackPkg.exe':
                shutil.copy2(source, target / source.name)
        installer = pkg_update.UpdateInstaller(validate=lambda path, **kwargs:
            validator.validate_knack2_dump(path, profile=profile, **kwargs))
        with patch.object(pkg_update, 'project_root', return_value=helper_root), \
             patch.object(validator.link_modules, 'module'):
            result = installer.install(game, installer.preview(game, self.pkg))
        self.assertTrue(result['valid'])
        self.assertEqual((game / 'hello.txt').read_bytes(), b'Synthetic PKG payload')
        installer.restore(game)
        after = {p.relative_to(game).as_posix(): p.read_bytes() for p in game.rglob('*') if p.is_file()}
        self.assertEqual(after, before)


if __name__ == '__main__':
    unittest.main()
