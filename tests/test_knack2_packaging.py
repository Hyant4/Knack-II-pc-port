"""Compiled entrypoint contracts without launching a guest or reading real saves."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2 import paths
from port.launcher.knack2.packaging import supervisor_entry
from port.launcher.knack2.packaging import gui_entry

ROOT = Path(__file__).resolve().parents[1]


class PackagingTests(unittest.TestCase):
    def test_linux_package_without_font_resources_fails_before_starting_child(self):
        import shutil
        from port.launcher.knack2 import validator
        for missing in ('DejaVuSans.ttf', 'LICENSE-DejaVu.txt'):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'profiles').mkdir()
                for name in validator.SUPPORTED_PROFILES:
                    shutil.copy2(ROOT / 'profiles' / (name + '.json'), root / 'profiles')
                fonts = root / 'port/gpu/third_party/fonts'
                fonts.mkdir(parents=True)
                for name in ('DejaVuSans.ttf', 'LICENSE-DejaVu.txt'):
                    if name != missing:
                        shutil.copy2(ROOT / 'port/gpu/third_party/fonts' / name, fonts / name)
                with patch.object(paths, 'project_root', return_value=root), \
                     patch.object(sys, 'platform', 'linux'), \
                     patch('port.launcher.knack2.runner.KnackRunner') as runner:
                    with self.assertRaisesRegex(FileNotFoundError, missing):
                        gui_entry.packaging_check(root / 'report.json')
                    runner.assert_not_called()

    def test_package_without_usa100_profile_fails_before_starting_child(self):
        import shutil
        from port.launcher.knack2 import validator
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'profiles').mkdir()
            for name in validator.SUPPORTED_PROFILES:
                if name != 'CUSA08014-01.00':
                    shutil.copy2(ROOT / 'profiles' / (name + '.json'), root / 'profiles')
            with patch.object(paths, 'project_root', return_value=root), \
                 patch('port.launcher.knack2.runner.KnackRunner') as runner:
                with self.assertRaises(validator.KnackProfileError):
                    gui_entry.packaging_check(root / 'report.json')
                runner.assert_not_called()

    def test_cpu_selftest_forwards_output_and_failure_status(self):
        import contextlib
        import io
        from types import SimpleNamespace
        class CapturedStream(io.StringIO):
            def reconfigure(self, **kwargs):
                pass
        for code, output, error in [(0,'CPU: complete\n',''),(7,'','native failure\n')]:
            out,err=CapturedStream(),CapturedStream()
            with self.subTest(code=code), \
                 patch.object(supervisor_entry.subprocess,'run',return_value=SimpleNamespace(
                     returncode=code,stdout=output,stderr=error)), \
                 contextlib.redirect_stdout(out),contextlib.redirect_stderr(err):
                self.assertEqual(supervisor_entry.main(['--selftest-cpu']),code)
            self.assertEqual(out.getvalue(),output)
            self.assertEqual(err.getvalue(),error)

    def test_actual_cli_entrypoint_accepts_help_and_literal_percent(self):
        result = subprocess.run([sys.executable, '-B', '-m', 'port.launcher.knack2', '--help'],
                                cwd=ROOT, capture_output=True, text=True, encoding='utf-8', timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('%LOCALAPPDATA%', result.stdout)

    def test_frozen_paths_find_repository_and_console_supervisor(self):
        for platform, suffix in (('win32', '.exe'), ('linux', '')):
            with self.subTest(platform=platform), patch.object(sys, 'platform', platform):
                self.check_frozen_paths(suffix)

    def check_frozen_paths(self, suffix):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'profiles').mkdir()
            (root / 'profiles/CUSA08014-01.01.json').write_text('{}')
            (root / 'run.py').write_text('# synthetic supervisor')
            helper = root / 'port/out/knack-launcher-supervisor' / ('KnackSupervisor' + suffix)
            with patch.object(sys, 'frozen', True, create=True), patch.object(sys, 'executable', str(root / ('Knack II' + suffix))):
                self.assertEqual(paths.project_root(), root)
                with self.assertRaises(FileNotFoundError):
                    paths.supervisor_executable()
                helper.parent.mkdir(parents=True)
                helper.write_bytes(b'synthetic, never executed')
                self.assertEqual(paths.supervisor_executable(), helper)
            with patch.object(sys, 'frozen', True, create=True), patch.object(sys, 'executable', str(helper)):
                self.assertEqual(paths.project_root(), root)

    def test_frozen_linux_children_restore_host_library_search_without_mutating_parent(self):
        values = {'LD_LIBRARY_PATH': '/tmp/bundle', 'LD_LIBRARY_PATH_ORIG': '/host/lib', 'SAFE': 'keep'}
        with patch.object(sys, 'platform', 'linux'), patch.object(sys, 'frozen', True, create=True):
            clean = paths.external_process_environment(values)
            self.assertEqual(clean, {'LD_LIBRARY_PATH': '/host/lib', 'SAFE': 'keep'})
            self.assertEqual(values['LD_LIBRARY_PATH'], '/tmp/bundle')
            self.assertEqual(paths.external_process_environment({'LD_LIBRARY_PATH': '/tmp/bundle'}), {})

    def test_source_process_environment_is_preserved(self):
        values = {'LD_LIBRARY_PATH': '/custom/native/lib'}
        with patch.object(sys, 'frozen', False, create=True):
            self.assertIs(paths.external_process_environment(values), values)
            self.assertIsNone(paths.external_process_environment())

    def test_official_cli_dispatch_and_unknown_script_rejection(self):
        with self.assertRaisesRegex(ValueError, 'Only the official'):
            supervisor_entry.main([str(ROOT / 'not-an-official-script.py')])
        with patch.object(sys, 'argv', ['selftest']), self.assertRaises(SystemExit) as exit_code:
            supervisor_entry.main([str(ROOT / 'run.py'), '--help'])
        self.assertEqual(exit_code.exception.code, 0)
