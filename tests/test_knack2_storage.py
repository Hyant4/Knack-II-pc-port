"""Persistent profile checks with synthetic data only."""
import contextlib
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2.storage import application_data_root, profile_user, user_data_lock
from test_knack2_unlimited import run


class StorageTests(unittest.TestCase):
    def test_root_is_independent_of_worktree_and_modes_share_progress(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {'LOCALAPPDATA': directory, 'XDG_DATA_HOME': directory}):
            expected = Path(directory) / ('KnackII-PC' if os.name == 'nt' else 'knack-ii-pc')
            self.assertEqual(application_data_root(), expected)
            solo = profile_user(expected, 1)
            coop = profile_user(expected, 2)
            self.assertEqual(solo, coop)
            self.assertEqual(solo, expected / 'profiles/shared/user')
            slot = solo / 'savedata/1/CUSA08014/SAVEDATA00/progress.dat'
            slot.parent.mkdir(parents=True)
            slot.write_bytes(b'solo progress')
            self.assertEqual((coop / slot.relative_to(solo)).read_bytes(), b'solo progress')
            (coop / slot.relative_to(solo)).write_bytes(b'coop progress')
            self.assertEqual(slot.read_bytes(), b'coop progress')

    def test_old_saves_are_never_hidden_behind_a_new_empty_profile(self):
        for mode in ('single', 'coop'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                savedata = root / 'profiles' / mode / 'user/savedata/1/CUSA08014'
                savedata.mkdir(parents=True)
                original = savedata / 'progress.dat'
                original.write_bytes(b'legacy progress to preserve')
                for players in (1, 2):
                    with self.assertRaisesRegex(ValueError, 'saves antigos'):
                        profile_user(root, players)
                self.assertEqual(original.read_bytes(), b'legacy progress to preserve')
                self.assertFalse((root / 'profiles/shared').exists())

    def test_empty_shared_directory_does_not_hide_old_progress(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            legacy = root / 'profiles/coop/user/savedata/progress.dat'
            legacy.parent.mkdir(parents=True)
            legacy.write_bytes(b'coop progress')
            (root / 'profiles/shared/user').mkdir(parents=True)
            with self.assertRaisesRegex(ValueError, 'saves antigos'):
                profile_user(root, 1)
            self.assertEqual(legacy.read_bytes(), b'coop progress')

    def test_existing_shared_progress_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for mode, content in [('single', b'old solo'), ('coop', b'old coop'), ('shared', b'current progress')]:
                path = root / 'profiles' / mode / 'user/savedata/progress.dat'
                path.parent.mkdir(parents=True)
                path.write_bytes(content)
            for players in (1, 2):
                user = profile_user(root, players)
                self.assertEqual((user / 'savedata/progress.dat').read_bytes(), b'current progress')

    def test_empty_legacy_folders_and_invalid_players(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'profiles/coop/user/savedata').mkdir(parents=True)
            self.assertEqual(profile_user(root, 1), root / 'profiles/shared/user')
            for players in (0, 3, None, '2'):
                with self.subTest(players=players), self.assertRaises(ValueError):
                    profile_user(root, players)

    def test_solo_and_coop_cannot_own_the_shared_save_concurrently(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            solo, coop = profile_user(root, 1), profile_user(root, 2)
            with user_data_lock(solo):
                with self.assertRaises(ValueError):
                    with user_data_lock(coop):
                        self.fail('second mode acquired the save')
            with user_data_lock(coop):
                pass

    def test_lock_rejects_concurrent_use_then_releases_after_error(self):
        with tempfile.TemporaryDirectory() as directory:
            user = Path(directory) / 'user'
            with self.assertRaisesRegex(RuntimeError, 'synthetic failure'):
                with user_data_lock(user):
                    with self.assertRaises(ValueError):
                        with user_data_lock(user):
                            self.fail('second owner acquired profile')
                    raise RuntimeError('synthetic failure')
            with user_data_lock(user):
                pass

    def test_invalid_profile_rejected_before_preparation_or_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for profile in (root / 'dump/user', root / 'output/user', root):
                with self.subTest(profile=profile), patch.object(run, 'validate_game') as validate, \
                     contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    run.main(['--game-dir', str(root / 'dump'), '--out', str(root / 'output'),
                              '--user-data', str(profile)])
                validate.assert_not_called()
                self.assertEqual(list(root.iterdir()), [])
