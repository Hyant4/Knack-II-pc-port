"""Synthetic regression tests; no game files are required."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('knack_run', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


def request(target='/app0/game.elf', args=()):
    def string(value):
        b = value.encode('utf-8')
        return struct.pack('<I', len(b)) + b
    return b'K2EXEC01' + string(target) + struct.pack('<I', len(args)) + b''.join(map(string, args))


class KnackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knack test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = json.loads((ROOT / 'profiles/CUSA08014-01.01.json').read_text())

    def parse(self, data):
        file = self.root / 'request.bin'
        file.write_bytes(data)
        return run.read_exec_request(file, self.profile)

    def test_load_exec_preserves_arguments(self):
        values = ['game.elf', 'argument with spaces', '', 'ação', '--option']
        self.assertEqual(self.parse(request(args=values)), ('game.elf', values))

    def test_load_exec_rejects_every_truncation(self):
        data = request(args=['one', 'two'])
        for size in range(len(data)):
            with self.subTest(size=size), self.assertRaises((ValueError, struct.error)):
                self.parse(data[:size])

    def test_load_exec_rejects_unlisted_path_and_trailer(self):
        for path in ['/app0/../game.elf', '/app0/other.elf', 'C:/game.elf', '/savedata0/game.elf']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.parse(request(path))
        with self.assertRaises(ValueError):
            self.parse(request() + b'extra')

    def test_request_limits(self):
        for data in [request(args=['x'] * 64), request(args=['x' * 4097]), request(args=['x\0y'])]:
            with self.assertRaises(ValueError):
                self.parse(data)

    def test_executable_stays_inside_dump(self):
        self.assertEqual(run.prepare.executable_path(self.root, 'game.elf'), self.root / 'game.elf')
        for target in ['../escape.elf', str(self.root.parent / 'escape.elf')]:
            with self.assertRaises(ValueError):
                run.prepare.executable_path(self.root, target)

    def test_foreign_game_overrides_are_isolated(self):
        with patch.dict(os.environ, {'ISS_FORCE_PATCHES': '1', 'TLG_PATCHES': 'unsafe', 'GOW3_FPS': '240'}):
            env = run.runtime_environment(self.root)
        self.assertNotIn('ISS_FORCE_PATCHES', env)
        self.assertNotIn('TLG_PATCHES', env)
        self.assertNotIn('GOW3_FPS', env)
        self.assertEqual(env['KNACK2_PROFILE'], 'CUSA08014-01.01')

    def test_supervisor_timeout_and_child_exit(self):
        command = [sys.executable, '-c', 'import sys; print("stage"); sys.exit(75)']
        log = self.root / 'child.log'
        self.assertEqual(run.run_child(command, os.environ.copy(), log, 5), 75)
        self.assertIn(b'stage', log.read_bytes())
        command = [sys.executable, '-c', 'import time; time.sleep(20)']
        self.assertEqual(run.run_child(command, os.environ.copy(), log, 0.1), 124)

    @unittest.skipUnless((ROOT / 'out/knack-loadexec-test.exe').is_file(), 'native harness has not been built')
    def test_native_loadexec_roundtrip_and_rejection(self):
        exe = ROOT / 'out/knack-loadexec-test.exe'
        path = self.root / 'request.bin'
        env = {**run.runtime_environment(self.root), 'KNACK2_EXEC_REQUEST': str(path)}
        values = ['game.elf', 'argument with spaces', '--option', '']
        result = subprocess.run([str(exe), '/app0/game.elf', *values], env=env, capture_output=True)
        self.assertEqual(result.returncode, 75, result.stderr)
        self.assertEqual(run.read_exec_request(path, self.profile), ('game.elf', values))
        path.unlink()
        result = subprocess.run([str(exe), '/app0/../escape.elf'], env=env, capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertFalse(path.exists())


if __name__ == '__main__':
    unittest.main()
