"""F3B.1 supervisor contracts; no guest or real saves are executed."""
import contextlib
import importlib.util
import io
import itertools
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch
from port.launcher.knack2.languages import BY_CODE, REGION_LANGUAGES
from port.launcher.knack2.identity import runtime_names

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('unlimited_supervisor', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


class UnlimitedTests(unittest.TestCase):
    def exercise_loop(self, timeout, *, times, coop=False, expire=False, language=None, branded=False, persistent=False):
        with tempfile.TemporaryDirectory(prefix='knack-unlimited ') as temporary:
            root = Path(temporary)
            port = root / 'port'
            (port / 'out').mkdir(parents=True)
            runtime_name = runtime_names()[0 if branded else 1]
            runtime = port / 'out' / runtime_name
            runtime.write_bytes(b'never executed')
            runtime.chmod(0o700)
            out = root / 'new-output'
            user = root / 'persistent-profile' if persistent else out / 'user'
            if persistent:
                user.mkdir()
                (user / 'save.sentinel').write_bytes(b'original progress')
            calls = []
            title='CUSA07670' if language is not None and language not in REGION_LANGUAGES['CUSA08014'] else 'CUSA08014'
            profile=run.load_profile(ROOT/'profiles'/f'{title}-01.01.json')
            if language is not None:
                assets = root / 'dump/data/lang' / BY_CODE[language]['asset']
                assets.mkdir(parents=True)
                (assets / 'synthetic.btext').write_bytes(b'synthetic locale')

            def child(command, env, log, seconds):
                calls.append((command, dict(env), seconds))
                self.assertEqual(Path(command[0]).name, runtime_name)
                self.assertEqual(env['TLG_APP_NAME'], 'Knack II')
                self.assertEqual(env['KNACK2_PROFILE'],title+'-01.01')
                self.assertEqual(env['TLG_APP_ID'], 'KnackII.PCPort.Game')
                self.assertEqual(command[command.index('--user') + 1], str(user))
                if persistent:
                    self.assertEqual((user / 'save.sentinel').read_bytes(), b'original progress')
                    with self.assertRaises(ValueError):
                        with run.user_data_lock(user):
                            self.fail('profile lock not held across LoadExec')
                self.assertEqual(env['TLG_CONFIG'], str(out / 'knack2.ini'))
                self.assertNotIn('TLG_GPU_USER_DIR', env)
                if language is not None:
                    self.assertEqual(env['TLG_LANGUAGE'], str(BY_CODE[language]['id']))
                else:
                    self.assertNotIn('TLG_LANGUAGE', env)
                if coop:
                    self.assertEqual(env['TLG_LOCAL_USERS'], '2')
                    self.assertEqual(env['TLG_LOCAL_P2_USER_ID'], '4099')
                else:
                    self.assertNotIn('TLG_LOCAL_USERS', env)
                    self.assertNotIn('TLG_LOCAL_P2_USER_ID', env)
                if len(calls) == 1:
                    target, argument = b'/app0/game.elf', b'from title'
                    Path(env['KNACK2_EXEC_REQUEST']).write_bytes(
                        b'K2EXEC01' + struct.pack('<I', len(target)) + target +
                        struct.pack('<II', 1, len(argument)) + argument)
                    return 75
                self.assertIn('from title', command)
                return 0

            options = ['--game-dir', str(root / 'dump'), '--out', str(out)]
            if persistent:
                options += ['--user-data', str(user)]
            if timeout is not None:
                options += ['--timeout', str(timeout)]
            if coop:
                options += ['--local-users', '2', '--p2-user-id', '4099']
            if language is not None:
                options += ['--language', language]
            with patch.object(run, 'PORT', port), patch.object(run, 'validate_game', return_value={
                    'executable_layout': run.validate_executable_layout(run.profile_id(profile)),
                    'game_elf_sha256': '0' * 64}), \
                 patch.object(run, 'load_profile', return_value=profile), \
                 patch.object(run, 'prepare_image', side_effect=lambda g, p, name, o: o / name), \
                 patch.object(run.subprocess, 'run'), patch.object(run, 'run_child', side_effect=child), \
                 patch.object(run.time, 'monotonic', side_effect=times), \
                 patch.dict(os.environ, {'TLG_LOCAL_USERS': '2', 'TLG_LOCAL_P2_USER_ID': '999',
                                         'TLG_GPU_USER_DIR': 'habitual saves', 'TLG_LANGUAGE': '999',
                                         'TLG_APP_NAME': 'The Last Guardian', 'TLG_APP_ID': 'infamous'}), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run.main(options), 124 if expire else 0)
            report = json.loads(next(out.glob('session-*/session.json')).read_text())
            self.assertEqual(len(report['stages']), 1 if expire else 2)
            self.assertEqual(report['language'], language)
            self.assertEqual(report['user_data'], str(user))
            if persistent:
                with run.user_data_lock(user):
                    pass  # Ownership released after the whole session.
            return calls

    def test_fixed_profiles_preserve_progress_across_loadexec(self):
        for coop in (False, True):
            self.exercise_loop(0, times=itertools.count(), coop=coop, persistent=True)

    def test_unlimited_survives_loadexec_and_arbitrary_elapsed_time(self):
        for coop in (False, True):
            with self.subTest(coop=coop):
                calls = self.exercise_loop(0, times=itertools.count(10**12, 10**12), coop=coop)
                self.assertEqual(len(calls), 2)
                for command, env, seconds in calls:
                    self.assertEqual(command[command.index('--timeout') + 1], '0')
                    self.assertIsNone(seconds)  # No hidden +5-second supervisor wait.

    def test_all_languages_survive_unlimited_coop_loadexec(self):
        for language in BY_CODE:
            with self.subTest(language=language):
                calls = self.exercise_loop(0, times=itertools.count(10**12, 10**12), coop=True, language=language)
                self.assertEqual(len(calls), 2)
                self.assertTrue(all(seconds is None for _, _, seconds in calls))

    def test_branded_runtime_is_selected_across_loadexec(self):
        calls = self.exercise_loop(0, times=itertools.count(10**12, 10**12), coop=True,
                                   language='pt-BR', branded=True)
        self.assertEqual(len(calls), 2)

    def test_explicit_positive_timeout_preserves_shared_deadline(self):
        calls = self.exercise_loop(60, times=iter([100, 110, 120, 130]))
        self.assertEqual([c[c.index('--timeout') + 1] for c, _, _ in calls], ['50', '30'])
        self.assertEqual([seconds for _, _, seconds in calls], [55, 35])

    def test_diagnostic_default_remains_180_seconds(self):
        calls = self.exercise_loop(None, times=iter([100, 110, 120, 130]))
        self.assertEqual([c[c.index('--timeout') + 1] for c, _, _ in calls], ['170', '150'])
        self.assertEqual([seconds for _, _, seconds in calls], [175, 155])

    def test_positive_deadline_can_still_expire_during_loadexec(self):
        calls = self.exercise_loop(60, times=iter([100, 110, 170]), expire=True)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][2], 55)

    def test_unlimited_wait_has_no_deadline_or_kill(self):
        process = Mock()
        process.wait.return_value = 75
        with tempfile.TemporaryDirectory() as temporary, patch.object(run.subprocess, 'Popen', return_value=process):
            self.assertEqual(run.run_child(['synthetic'], {}, Path(temporary) / 'child.log', None), 75)
        process.wait.assert_called_once_with(timeout=None)
        process.kill.assert_not_called()

    def test_explicit_timeout_still_kills_and_reaps_child(self):
        process = Mock()
        process.wait.side_effect = [subprocess.TimeoutExpired('synthetic', .1), 0]
        with tempfile.TemporaryDirectory() as temporary, patch.object(run.subprocess, 'Popen', return_value=process):
            self.assertEqual(run.run_child(['synthetic'], {}, Path(temporary) / 'child.log', .1), 124)
        process.kill.assert_called_once_with()
        self.assertEqual(process.wait.call_args_list[0].kwargs, {'timeout': .1})
        self.assertEqual(process.wait.call_count, 2)


if __name__ == '__main__':
    unittest.main()
