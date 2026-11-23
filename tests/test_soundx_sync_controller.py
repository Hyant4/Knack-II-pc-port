"""Opt-in local-image integration experiment; distributes no proprietary bytes.

Run with KNACK_SYNC_TEST_IMAGE pointing at an existing prepared CUSA08014 01.01
boot-linked.bin. Default discovery skips guest execution without that opt-in.
The authenticated image is read only, never prepared or modified by this test.
The gate requires recovery without external alignment, using a full synthetic
FIFO. It still uses controlled query inputs and a partial command bridge, so a
pass does not certify that the reported cutscene occurrence is fixed.
"""
import hashlib
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest

try:
    from tests.native_toolchain import c_compiler
except ModuleNotFoundError:
    from native_toolchain import c_compiler

ROOT = Path(__file__).resolve().parents[1]
IMAGE_SHA256 = '0c51f1e2c825ac785809e06ec4c8d1ad39ebb9a49129294ff4096c43267a4bc9'


def authenticated_image(path):
    # Import only the existing read-only parser, never preparation or linking.
    import sys
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    from scripts.inspect_soundx_ngs2 import load_probe_image
    image = load_probe_image(path)['image']
    if len(image) != 28377232 or hashlib.sha256(image).hexdigest() != IMAGE_SHA256:
        raise ValueError('Unrecognized image: no guest code will be executed')
    return image


class SoundxSyncController(unittest.TestCase):
    def test_unknown_image_is_rejected_before_guest_execution(self):
        with tempfile.TemporaryDirectory(prefix='knack-sync-invalid-') as tmp:
            path = Path(tmp) / 'synthetic.bin'
            # Structurally readable G3 container, deliberately unauthenticated.
            data = (struct.pack('<8s6Q', b'G3PROBE\0', 4, 0, 0, 0, 0, 0)
                    + bytes(40) + struct.pack('<2Q', 0, 0) + b'FAKE')
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, 'no guest code will be executed'):
                authenticated_image(path)
            self.assertEqual(path.read_bytes(), data)

    @unittest.skipUnless(os.environ.get('KNACK_SYNC_TEST_IMAGE'),
                         'local guest integration requires KNACK_SYNC_TEST_IMAGE')
    def test_original_controller_and_current_hle_clock_loop(self):
        path = Path(os.environ['KNACK_SYNC_TEST_IMAGE'])
        original_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        image = authenticated_image(path)
        compiler = c_compiler()
        env = {k: v for k, v in os.environ.items() if not k.startswith(('TLG_', 'SDL_'))}
        env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
        try:
            with tempfile.TemporaryDirectory(prefix='knack-sync-controller-') as tmp:
                tmp = Path(tmp)
                config = tmp / 'settings.ini'
                config.write_text('', encoding='ascii')
                env['TLG_CONFIG'] = str(config)
                binary = tmp / 'sync-controller.exe'
                build = subprocess.run([str(compiler), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wframe-larger-than=65536', '-I' + str(ROOT / 'port/src'),
                    '-I' + str(ROOT / 'port'), str(ROOT / 'tests/test_soundx_sync_controller.c'),
                    '-pthread', '-lm', '-o', str(binary)], env=env, capture_output=True, timeout=60)
                self.assertEqual(build.returncode, 0, (build.stdout + build.stderr).decode(errors='replace'))
                require_recovery = os.environ.get('KNACK_SYNC_REQUIRE_RECOVERY') == '1'
                args = [str(binary)] + (['--require-recovery'] if require_recovery else [])
                run = subprocess.run(args, input=image, env=env, capture_output=True, timeout=30)
                output = (run.stdout + run.stderr).decode(errors='replace')
                # Preserve intentional RED evidence as well as successful runs.
                if os.environ.get('KNACK_SYNC_TEST_LOG'):
                    Path(os.environ['KNACK_SYNC_TEST_LOG']).write_text(output, encoding='utf-8')
                if run.returncode == 77:
                    self.skipTest(output)
                self.assertEqual(run.returncode, 0, output)
                self.assertIn('PASS: original controller decisions', output)
                self.assertEqual(output.count('CASE name='), 7)
                self.assertEqual(output.count('PARAMETER requested='), 7)
                self.assertEqual(output.count('LOOP backend=responsive_clock '), 2)
                self.assertEqual(output.count('ACCEPTANCE initial_error='), 2)
                self.assertNotIn('result=FAIL', output)
        finally:
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), original_hash,
                             'The supplied image changed during the experiment')


if __name__ == '__main__':
    unittest.main()
