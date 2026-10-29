"""Build and verify the isolated LibAtrac9 harness without game data."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get('MSYS2_ROOT') or Path.home() / 'AppData/Local/knack2-toolchain/msys64')
ATRAC9_SRC = ROOT / 'port/third_party/LibAtrac9/C/src'


class Atrac9HarnessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='knack atrac9 harness ')
        cls.output = Path(cls.temporary.name)
        cls.compiler = TOOLCHAIN / 'clang64/bin/clang.exe'
        if not cls.compiler.exists():
            raise unittest.SkipTest('MSYS2 CLANG64 is required for this native test')
        cls.env = dict(os.environ, PATH=str(cls.compiler.parent) + os.pathsep + os.environ.get('PATH', ''))

        harness_object = cls.output / 'harness.o'
        compile_harness = subprocess.run(
            [str(cls.compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
             '-I' + str(ATRAC9_SRC), '-c', str(ROOT / 'tests/atrac9_decode_harness.c'),
             '-o', str(harness_object)],
            env=cls.env, capture_output=True, text=True, timeout=60)
        if compile_harness.returncode:
            raise AssertionError(compile_harness.stdout + compile_harness.stderr)

        objects = [harness_object]
        for source in sorted(ATRAC9_SRC.glob('*.c')):
            target = cls.output / (source.stem + '.o')
            compiled = subprocess.run(
                [str(cls.compiler), '-std=c99', '-O2', '-w', '-c', str(source), '-o', str(target)],
                env=cls.env, capture_output=True, text=True, timeout=60)
            if compiled.returncode:
                raise AssertionError(compiled.stdout + compiled.stderr)
            objects.append(target)

        cls.executable = cls.output / 'atrac9-decode-harness.exe'
        linked = subprocess.run(
            [str(cls.compiler), *(str(item) for item in objects), '-lm', '-o', str(cls.executable)],
            env=cls.env, capture_output=True, text=True, timeout=60)
        if linked.returncode:
            raise AssertionError(linked.stdout + linked.stderr)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'temporary'):
            cls.temporary.cleanup()

    def run_harness(self, *arguments):
        return subprocess.run(
            [str(self.executable), *arguments], env=self.env,
            capture_output=True, text=True, timeout=20)

    def test_captured_four_channel_config(self):
        result = self.run_harness('--config', 'FE7A17F0')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = {
            'channels=4', 'channel_config_index=5', 'sample_rate=48000',
            'average_frame_bytes=192', 'frames_per_superframe=4', 'superframe_bytes=768',
            'frame_samples_per_channel=256', 'superframe_samples_per_channel=1024',
            'decode=not-run', 'reason=no-authorized-input-supplied',
        }
        self.assertTrue(expected.issubset(set(result.stdout.splitlines())), result.stdout)

    def test_captured_eight_channel_config(self):
        result = self.run_harness('--config', 'FE7829F0')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = {
            'channels=8', 'channel_config_index=4', 'sample_rate=48000',
            'average_frame_bytes=336', 'frames_per_superframe=4', 'superframe_bytes=1344',
            'frame_samples_per_channel=256', 'superframe_samples_per_channel=1024',
        }
        self.assertTrue(expected.issubset(set(result.stdout.splitlines())), result.stdout)

    def test_rejects_native_integer_byte_order(self):
        result = self.run_harness('--config', 'F0177AFE')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn('config_error=', result.stderr)

    def test_refuses_partial_superframe_before_decode(self):
        fixture = self.output / 'partial-not-a-fixture.at9'
        fixture.write_bytes(b'\0' * 767)
        result = self.run_harness('--config', 'FE7A17F0', '--input', str(fixture))
        self.assertEqual(result.returncode, 6, result.stdout + result.stderr)
        self.assertIn('partial_superframe_bytes=767 expected=768', result.stderr)
        self.assertIn('frames_decoded=0', result.stdout)

    def test_initializes_two_independent_handles_without_input(self):
        result = self.run_harness('--config', 'FE7A17F0', '--verify-second-handle')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('second_handle_initialized=1', result.stdout)
        self.assertIn('decode=not-run', result.stdout)

    def test_rejects_conflicting_reset_and_dual_handle_modes(self):
        result = self.run_harness(
            '--config', 'FE7A17F0', '--reset-each-superframe', '--verify-second-handle')
        self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertIn('usage:', result.stderr)


if __name__ == '__main__':
    unittest.main()
