# SPDX-License-Identifier: GPL-2.0-or-later
"""Build labels must follow the deployed package, independent of saved settings."""
from pathlib import Path
import tempfile
import unittest

from port.launcher.knack2.paths import display_version


class BuildVersionTests(unittest.TestCase):
    def test_packaged_labels_and_source_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(display_version(root), 'dev')
            for label in ['0.17.7-rc4', '0.17.7-dev']:
                (root / 'VERSION').write_text(label + '\n', encoding='utf-8')
                self.assertEqual(display_version(root), label)

    def test_malformed_labels_do_not_reach_qml(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for data in [b'', b'pr19-8d7f434', b'one\ntwo', b'<b>version</b>', b'x' * 129, b'\xff']:
                (root / 'VERSION').write_bytes(data)
                self.assertEqual(display_version(root), 'dev')


if __name__ == '__main__':
    unittest.main()
