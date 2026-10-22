# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic unit tests for Knack II Launcher configuration and validation (Hotfix F1.1)."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

# Adjust path to find port/launcher and port/scripts
_ROOT = Path(__file__).resolve().parents[1]
_PORT_DIR = _ROOT / 'port'
_LAUNCHER_DIR = _PORT_DIR / 'launcher'
_SCRIPTS_DIR = _PORT_DIR / 'scripts'

for directory in (str(_ROOT), str(_LAUNCHER_DIR), str(_SCRIPTS_DIR)):
    if directory not in os.sys.path:
        os.sys.path.insert(0, directory)

from knack2.config import (
    ALLOWED_MANAGED_KEYS,
    default_app_data_dir,
    launcher_settings_path,
    load_knack2_ini,
    load_launcher_settings,
    save_launcher_settings,
    update_knack2_ini,
)
from knack2.validator import (
    KnackDumpError,
    KnackDumpNotFoundError,
    KnackHashMismatchError,
    KnackModuleError,
    KnackProfileError,
    KnackVersionMismatchError,
    digest_file,
    load_profile,
    validate_knack2_dump,
    validate_profile_schema,
)


class TestKnack2Config(unittest.TestCase):
    """Tests for launcher_settings.json and non-destructive knack2.ini updates."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix='knack2_cfg_test_')
        self.addCleanup(self.temp_dir.cleanup)
        self.root = Path(self.temp_dir.name)

    def test_default_app_data_dir_windows_and_linux(self) -> None:
        win_dir = default_app_data_dir({'APPDATA': str(self.root / 'AppData')}, nt=True)
        self.assertEqual(win_dir, self.root / 'AppData' / 'Knack2-PC')

        posix_dir = default_app_data_dir({'XDG_CONFIG_HOME': str(self.root / '.config')}, nt=False)
        self.assertEqual(posix_dir, self.root / '.config' / 'knack2-pc')

    def test_launcher_settings_roundtrip(self) -> None:
        settings_file = self.root / 'launcher_settings.json'
        self.assertEqual(load_launcher_settings(settings_file), {})

        test_data = {
            'game_dir': 'C:\\Games\\Knack2',
            'locale': 'pt_BR',
            'session_timeout': 3600,
        }
        saved_path = save_launcher_settings(test_data, path=settings_file)
        self.assertEqual(saved_path, settings_file)
        self.assertTrue(settings_file.is_file())

        loaded = load_launcher_settings(settings_file)
        self.assertEqual(loaded, test_data)

    def test_launcher_settings_corrupted_json(self) -> None:
        settings_file = self.root / 'launcher_settings.json'
        settings_file.write_text('{bad json: True,', encoding='utf-8')
        self.assertEqual(load_launcher_settings(settings_file), {})

    def test_launcher_settings_atomic_failure_cleanup(self) -> None:
        settings_file = self.root / 'launcher_settings.json'
        with patch('os.replace', side_effect=OSError('Access denied')):
            with self.assertRaises(OSError):
                save_launcher_settings({'key': 'val'}, path=settings_file)
        # Ensure no dangling tmp files
        tmp_files = list(self.root.glob('*.tmp*'))
        self.assertEqual(tmp_files, [])

    def test_load_knack2_ini(self) -> None:
        ini_file = self.root / 'knack2.ini'
        ini_file.write_text(
            '# Header\n'
            'display_mode=windowed\n'
            'vsync=1\n'
            '; semicolon comment\n'
            'ngs2_atrac9_hle=1\n',
            encoding='utf-8',
        )
        data = load_knack2_ini(ini_file)
        self.assertEqual(data.get('display_mode'), 'windowed')
        self.assertEqual(data.get('vsync'), '1')
        self.assertEqual(data.get('ngs2_atrac9_hle'), '1')

    def test_load_knack2_ini_missing(self) -> None:
        with self.assertRaises(FileNotFoundError):
            load_knack2_ini(self.root / 'nonexistent.ini')

    def test_update_knack2_ini_rejects_unauthorized_keys(self) -> None:
        ini_file = self.root / 'knack2.ini'
        ini_file.write_text('display_mode=windowed\nngs2_render_mix=1\n', encoding='utf-8')

        # Attempt to modify an unmanaged key directly via updates must fail
        with self.assertRaises(ValueError) as cm:
            update_knack2_ini(ini_file, {'ngs2_render_mix': '0'})
        self.assertIn('não autorizada', str(cm.exception))

        with self.assertRaises(ValueError) as cm:
            update_knack2_ini(ini_file, {'arbitrary_injection': 'malicious'})
        self.assertIn('não autorizada', str(cm.exception))

        # Ensure the file was left 100% untouched
        content = ini_file.read_text(encoding='utf-8')
        self.assertEqual(content, 'display_mode=windowed\nngs2_render_mix=1\n')

    def test_update_knack2_ini_rejects_control_characters_and_newlines(self) -> None:
        ini_file = self.root / 'knack2.ini'
        ini_file.write_text('display_mode=windowed\n', encoding='utf-8')

        # Injections via newlines (raw values checked before strip)
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode': 'fullscreen\nngs2_render_mix=0'})

        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode': 'fullscreen\r\ninjected=1'})

        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode': 'windowed\n'})

        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode': 'windowed\r'})

        # Control characters in values
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode': 'fullscreen\x00extra'})

        # Control characters / newlines in keys
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode\n': 'windowed'})

        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode\x1f': 'windowed'})

    def test_update_knack2_ini_rejects_semantically_invalid_values(self) -> None:
        ini_file = self.root / 'knack2.ini'
        ini_file.write_text('display_mode=windowed\nvsync=1\n', encoding='utf-8')

        # Invalid display mode
        for bad_mode in ('vr', 'window', '', 'custom'):
            with self.assertRaises(ValueError):
                update_knack2_ini(ini_file, {'display_mode': bad_mode})

        # Invalid booleans
        for bad_bool in ('2', 'maybe', 'not_a_bool', ''):
            with self.assertRaises(ValueError):
                update_knack2_ini(ini_file, {'vsync': bad_bool})

        # Invalid integer bounds
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'fps_limit': '-1'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'fps_limit': 'unlimited'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'rcas_strength': '101'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'rcas_strength': '-1'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'frames_queued': '0'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'frames_queued': '5'})

        # Invalid float bounds
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'mouse_sensitivity': '0.01'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'mouse_sensitivity': '15.0'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'mouse_smoothing': '0.99'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'mouse_accel': '2.5'})
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'mouse_deadzone': '0.6'})

        # Oversized key_map
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'key_map': 'A' * 1025})

        # Non-primitive type
        with self.assertRaises(ValueError):
            update_knack2_ini(ini_file, {'display_mode': {'nested': 'dict'}})

    def test_update_knack2_ini_preserves_crlf_and_lf(self) -> None:
        # 1. Test CRLF preservation
        crlf_file = self.root / 'knack2_crlf.ini'
        crlf_bytes = b'ngs2_audio_diag=1\r\ndisplay_mode=windowed\r\nvsync=1\r\n'
        crlf_file.write_bytes(crlf_bytes)

        update_knack2_ini(crlf_file, {'display_mode': 'borderless'})
        res_bytes = crlf_file.read_bytes()
        self.assertIn(b'\r\n', res_bytes)
        self.assertNotIn(b'(?<!\r)\n', [res_bytes])  # all newlines should be CRLF
        self.assertEqual(res_bytes, b'ngs2_audio_diag=1\r\ndisplay_mode=borderless\r\nvsync=1\r\n')

        # 2. Test LF preservation
        lf_file = self.root / 'knack2_lf.ini'
        lf_bytes = b'ngs2_audio_diag=1\ndisplay_mode=windowed\nvsync=1\n'
        lf_file.write_bytes(lf_bytes)

        update_knack2_ini(lf_file, {'display_mode': 'fullscreen'})
        res_lf_bytes = lf_file.read_bytes()
        self.assertNotIn(b'\r\n', res_lf_bytes)
        self.assertEqual(res_lf_bytes, b'ngs2_audio_diag=1\ndisplay_mode=fullscreen\nvsync=1\n')

    def test_update_knack2_ini_preserves_unknown_keys_and_comments(self) -> None:
        ini_file = self.root / 'knack2.ini'
        original_content = (
            '# Knack 2 Audio Configuration\n'
            'ngs2_atrac9_hle=1\n'
            'ngs2_render_mix=1\n'
            'ngs2_audio_diag=1\n'
            'ngs2_b2f2a_stable_cohort=0\n'
            'ngs2_b2f2c_zero_matrix_bypass=1\n'
            '\n'
            '# Video\n'
            'display_mode=windowed\n'
            'vsync=1\n'
            'fps_limit=60\n'
        )
        ini_file.write_text(original_content, encoding='utf-8')

        # Update authorized keys
        updates = {
            'display_mode': 'fullscreen',
            'vsync': '0',
            'fsr1': '1',
            'rcas': '1',
            'fps_limit': None,  # remove
        }
        update_knack2_ini(ini_file, updates)

        updated_text = ini_file.read_text(encoding='utf-8')
        lines = updated_text.splitlines()

        # Check preservation of comments and unmanaged NGS2 keys
        self.assertIn('# Knack 2 Audio Configuration', lines)
        self.assertIn('ngs2_atrac9_hle=1', lines)
        self.assertIn('ngs2_render_mix=1', lines)
        self.assertIn('ngs2_audio_diag=1', lines)
        self.assertIn('ngs2_b2f2a_stable_cohort=0', lines)
        self.assertIn('ngs2_b2f2c_zero_matrix_bypass=1', lines)
        self.assertIn('# Video', lines)

        # Check modifications
        self.assertIn('display_mode=fullscreen', lines)
        self.assertNotIn('display_mode=windowed', lines)
        self.assertIn('vsync=0', lines)
        self.assertNotIn('vsync=1', lines)
        self.assertNotIn('fps_limit=60', lines)
        self.assertIn('fsr1=1', lines)
        self.assertIn('rcas=1', lines)

    def test_update_knack2_ini_atomic_failure_leaves_file_intact(self) -> None:
        ini_file = self.root / 'knack2.ini'
        original = 'display_mode=windowed\nngs2_render_mix=1\n'
        ini_file.write_text(original, encoding='utf-8')

        with patch('os.replace', side_effect=OSError('Mock filesystem write error')):
            with self.assertRaises(OSError):
                update_knack2_ini(ini_file, {'display_mode': 'fullscreen'})

        # The original file must remain exactly as it was
        self.assertEqual(ini_file.read_text(encoding='utf-8'), original)
        # No dangling tmp files
        self.assertEqual(list(self.root.glob('*.tmp*')), [])


class TestKnack2Validator(unittest.TestCase):
    """Tests for CUSA08014 / 01.01 dump and profile validation."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory(prefix='knack2_val_test_')
        self.addCleanup(self.temp_dir.cleanup)
        self.game_dir = Path(self.temp_dir.name)

        # Build mock dump structure
        (self.game_dir / 'sce_sys').mkdir(parents=True)
        (self.game_dir / 'sce_module').mkdir(parents=True)

        self.eboot_data = b'Mock eboot binary content'
        self.game_elf_data = b'Mock game elf binary content'
        self.eboot_hash = hashlib.sha256(self.eboot_data).hexdigest()
        self.game_elf_hash = hashlib.sha256(self.game_elf_data).hexdigest()

        (self.game_dir / 'eboot.bin').write_bytes(self.eboot_data)
        (self.game_dir / 'game.elf').write_bytes(self.game_elf_data)

        # Dummy modules
        for mod in ('libc.prx', 'libSceFios2.prx', 'libSceJobManager.prx'):
            (self.game_dir / 'sce_module' / mod).write_bytes(b'PRX' + mod.encode())

        (self.game_dir / 'sce_sys' / 'param.sfo').write_bytes(b'SFO_DUMMY')

        # Synthetic valid profile
        self.mock_profile = {
            'title_id': 'CUSA08014',
            'version': '01.01',
            'executables': {
                'eboot.bin': self.eboot_hash,
                'game.elf': self.game_elf_hash,
            },
            'modules': {
                'eboot.bin': ['libc.prx', 'libSceFios2.prx'],
                'game.elf': ['libc.prx', 'libSceFios2.prx', 'libSceJobManager.prx'],
            },
        }

    def test_profile_schema_strict_validation(self) -> None:
        # Not a dict
        with self.assertRaises(KnackProfileError):
            validate_profile_schema('not a dict')

        # Missing title_id
        bad = dict(self.mock_profile)
        bad['title_id'] = ''
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        # Missing version
        bad = dict(self.mock_profile)
        bad['version'] = None
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        # Missing game.elf in executables
        bad = dict(self.mock_profile)
        bad['executables'] = {'eboot.bin': self.eboot_hash}
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        # Unsupported title_id or version
        bad = dict(self.mock_profile)
        bad['title_id'] = 'CUSA00223'
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        bad = dict(self.mock_profile)
        bad['version'] = '01.00'
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        # Non-string hash types (must raise KnackProfileError, NOT TypeError)
        for bad_hash in (12345, None, ['hash'], {'sha': '256'}, 3.14):
            bad = dict(self.mock_profile)
            bad['executables'] = {'eboot.bin': bad_hash, 'game.elf': self.game_elf_hash}
            with self.assertRaises(KnackProfileError):
                validate_profile_schema(bad)

        # Malformed hash (not 64 hex characters)
        bad = dict(self.mock_profile)
        bad['executables'] = {'eboot.bin': 'short_hash', 'game.elf': self.game_elf_hash}
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        bad['executables'] = {'eboot.bin': 'g' * 64, 'game.elf': self.game_elf_hash}
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        # Malformed modules structure
        bad = dict(self.mock_profile)
        bad['modules'] = 'not a dict'
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        bad = dict(self.mock_profile)
        bad['modules'] = {'eboot.bin': 'not a list', 'game.elf': ['libc.prx']}
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        bad = dict(self.mock_profile)
        bad['modules'] = {'eboot.bin': [12345], 'game.elf': ['libc.prx']}
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

        # Empty modules list
        bad = dict(self.mock_profile)
        bad['modules'] = {'eboot.bin': [], 'game.elf': ['libc.prx']}
        with self.assertRaises(KnackProfileError):
            validate_profile_schema(bad)

    def test_digest_file(self) -> None:
        f = self.game_dir / 'test.bin'
        f.write_bytes(b'Antigravity')
        expected = hashlib.sha256(b'Antigravity').hexdigest()
        self.assertEqual(digest_file(f), expected)

    @patch('prepare.load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01', 'TITLE': 'Knack II'})
    @patch('link_modules.module', return_value=None)
    def test_successful_validation(self, mock_mod, mock_sfo) -> None:
        result = validate_knack2_dump(self.game_dir, profile=self.mock_profile)
        self.assertTrue(result['valid'])
        self.assertEqual(result['title_id'], 'CUSA08014')
        self.assertEqual(result['version'], '01.01')
        self.assertEqual(result['app_title'], 'Knack II')
        self.assertIn('eboot.bin', result['executables'])
        self.assertIn('game.elf', result['executables'])

    def test_missing_game_dir(self) -> None:
        with self.assertRaises(KnackDumpNotFoundError):
            validate_knack2_dump(self.game_dir / 'missing_folder', profile=self.mock_profile)

    def test_missing_param_sfo(self) -> None:
        (self.game_dir / 'sce_sys' / 'param.sfo').unlink()
        with self.assertRaises(KnackDumpNotFoundError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    @patch('prepare.load_sfo', return_value={'TITLE_ID': 'CUSA00223', 'APP_VER': '01.00'})
    def test_wrong_title_id(self, mock_sfo) -> None:
        with self.assertRaises(KnackVersionMismatchError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    @patch('prepare.load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.00'})
    def test_wrong_app_version(self, mock_sfo) -> None:
        with self.assertRaises(KnackVersionMismatchError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    @patch('prepare.load_sfo', side_effect=ValueError('Corrupt SFO structure'))
    def test_corrupt_param_sfo(self, mock_sfo) -> None:
        with self.assertRaises(KnackVersionMismatchError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    @patch('prepare.load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01'})
    def test_missing_executable(self, mock_sfo) -> None:
        (self.game_dir / 'game.elf').unlink()
        with self.assertRaises(KnackDumpNotFoundError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    @patch('prepare.load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01'})
    def test_executable_hash_mismatch(self, mock_sfo) -> None:
        (self.game_dir / 'eboot.bin').write_bytes(b'Tampered content')
        with self.assertRaises(KnackHashMismatchError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    @patch('prepare.load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01'})
    def test_missing_module(self, mock_sfo) -> None:
        (self.game_dir / 'sce_module' / 'libSceJobManager.prx').unlink()
        with self.assertRaises(KnackModuleError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    @patch('prepare.load_sfo', return_value={'TITLE_ID': 'CUSA08014', 'APP_VER': '01.01'})
    @patch('link_modules.module', side_effect=ValueError('Invalid ELF tag'))
    def test_corrupt_module(self, mock_mod, mock_sfo) -> None:
        with self.assertRaises(KnackModuleError):
            validate_knack2_dump(self.game_dir, profile=self.mock_profile)

    def test_fail_closed_when_tools_unavailable(self) -> None:
        with patch('knack2.validator.prepare', None):
            with self.assertRaises(KnackDumpError) as cm:
                validate_knack2_dump(self.game_dir, profile=self.mock_profile)
            self.assertIn('prepare indisponível', str(cm.exception))

        with patch('knack2.validator.link_modules', None):
            with self.assertRaises(KnackModuleError) as cm:
                validate_knack2_dump(self.game_dir, profile=self.mock_profile)
            self.assertIn('link_modules indisponível', str(cm.exception))

    def test_official_profile_schema(self) -> None:
        """Verify the official CUSA08014-01.01.json schema without modifying it."""
        profile = load_profile(repo_root=_ROOT)
        self.assertEqual(profile.get('title_id'), 'CUSA08014')
        self.assertEqual(profile.get('version'), '01.01')
        self.assertIn('eboot.bin', profile.get('executables', {}))
        self.assertIn('game.elf', profile.get('executables', {}))
        self.assertIn('libc.prx', profile.get('modules', {}).get('eboot.bin', []))


if __name__ == '__main__':
    unittest.main()
