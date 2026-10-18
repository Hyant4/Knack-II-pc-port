"""Verify bounded CPAK/SXDF extraction without proprietary data."""
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from scripts.extract_soundx_atrac9_fixture import fnv1a64

ROOT = Path(__file__).resolve().parents[1]
EXTRACTOR = ROOT / 'scripts/extract_soundx_atrac9_fixture.py'


def synthetic_archive(path: Path, superframes: int = 1, truncate_payload: bool = False) -> bytes:
    member_offset = 0x300
    payload = bytes((index * 17 + 3) & 0xFF for index in range(768 * superframes))
    member_size = 0x110 + len(payload) - (1 if truncate_payload else 0)
    archive = bytearray(member_offset + member_size)
    archive[:4] = b'CPAK'
    struct.pack_into('<I', archive, 4, 1)
    record = 0x14
    struct.pack_into('<QQ', archive, record, member_offset, member_size)
    archive[record + 0x14:record + 0x14 + len(b'test.bsxd')] = b'test.bsxd'

    archive[member_offset:member_offset + 4] = b'SXDF'
    wave = member_offset + 0x40
    archive[wave:wave + 4] = b'WAVE'
    struct.pack_into('<I', archive, wave + 12, 1)
    struct.pack_into('<I', archive, wave + 16, 4)
    entry = wave + 20
    struct.pack_into('<I', archive, entry, 1)
    archive[entry + 4:entry + 6] = bytes((0x42, 4))
    struct.pack_into('<I', archive, entry + 8, 48_000)
    struct.pack_into('<I', archive, entry + 0x20, len(payload))
    archive[entry + 0x40:entry + 0x44] = bytes.fromhex('F0177AFE')
    data = member_offset + 0x100
    archive[data:data + 4] = b'DATA'
    struct.pack_into('<I', archive, data + 4, len(payload) + 8)
    available = len(payload) - (1 if truncate_payload else 0)
    archive[data + 16:data + 16 + available] = payload[:available]
    path.write_bytes(archive)
    return payload


class SoundXFixtureExtractorTests(unittest.TestCase):
    def test_extracts_exact_superframe_after_validating_metadata(self):
        with tempfile.TemporaryDirectory(prefix='knack soundx extractor ') as temporary:
            root = Path(temporary)
            archive = root / 'test.apak'
            output = root / 'fixture.at9'
            payload = synthetic_archive(archive)
            command = [
                str(ROOT / '.venv/Scripts/python.exe'), str(EXTRACTOR),
                '--archive', str(archive), '--member', 'test.bsxd',
                '--output', str(output), '--segment-size', '768',
                '--expected-fingerprint', f'{fnv1a64(payload[:32]):016x}',
            ]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_bytes(), payload)
            self.assertIn('payload_offset=0x410', result.stdout)
            self.assertIn('superframes_extracted=1', result.stdout)
            self.assertIn('fixture_size=768', result.stdout)

    def test_extracts_multiple_complete_superframes(self):
        with tempfile.TemporaryDirectory(prefix='knack soundx extractor multi ') as temporary:
            root = Path(temporary)
            archive = root / 'test.apak'
            output = root / 'fixture.at9'
            payload = synthetic_archive(archive, superframes=2)
            command = [
                str(ROOT / '.venv/Scripts/python.exe'), str(EXTRACTOR),
                '--archive', str(archive), '--member', 'test.bsxd',
                '--output', str(output), '--segment-size', '1536',
                '--superframes', '2',
                '--expected-fingerprint', f'{fnv1a64(payload[:32]):016x}',
            ]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_bytes(), payload)
            self.assertIn('superframes_extracted=2', result.stdout)
            self.assertIn('fixture_size=1536', result.stdout)

    def test_rejects_payload_outside_member(self):
        with tempfile.TemporaryDirectory(prefix='knack soundx extractor bad ') as temporary:
            root = Path(temporary)
            archive = root / 'bad.apak'
            output = root / 'fixture.at9'
            payload = synthetic_archive(archive, truncate_payload=True)
            command = [
                str(ROOT / '.venv/Scripts/python.exe'), str(EXTRACTOR),
                '--archive', str(archive), '--member', 'test.bsxd',
                '--output', str(output), '--segment-size', '768',
                '--expected-fingerprint', f'{fnv1a64(payload[:32]):016x}',
            ]
            result = subprocess.run(command, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn('ATRAC9 payload range exceeds container', result.stderr)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
