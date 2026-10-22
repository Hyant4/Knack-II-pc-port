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
    def test_root_is_independent_of_worktree_and_players_are_separate(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {'LOCALAPPDATA': directory, 'XDG_DATA_HOME': directory}):
            expected = Path(directory) / ('KnackII-PC' if os.name == 'nt' else 'knack-ii-pc')
            self.assertEqual(application_data_root(), expected)
            self.assertNotEqual(profile_user(expected, 1), profile_user(expected, 2))

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
