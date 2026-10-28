"""Language discovery, environment validation and local catalog contracts."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from port.launcher.knack2.languages import discover_languages, language_environment, BY_CODE

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('language_run', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


class LanguageTests(unittest.TestCase):
    def test_discovery_requires_actual_installed_assets(self):
        with tempfile.TemporaryDirectory() as temporary:
            game = Path(temporary)
            self.assertEqual(discover_languages(game), [])
            for code, entry in BY_CODE.items():
                directory = game / 'data/lang' / entry['asset']
                directory.mkdir(parents=True)
                self.assertNotIn(code, discover_languages(game))
                (directory / 'synthetic.btext').write_bytes(b'not game data')
            self.assertEqual(discover_languages(game), list(BY_CODE))
            unknown = game / 'data/lang/unknown'
            unknown.mkdir()
            (unknown / 'fixture').write_bytes(b'x')
            self.assertEqual(discover_languages(game), list(BY_CODE))

    def test_explicit_orbis_ids_and_inherited_environment_filter(self):
        for code, value in [('pt-BR', 17), ('en-US', 1), ('es-419', 20), ('fr-FR', 2)]:
            self.assertEqual(language_environment(code), {'TLG_LANGUAGE': str(value)})
            with patch.dict(os.environ, {'TLG_LANGUAGE': '999', 'ISS_LANGUAGE': '999'}):
                self.assertEqual(run.runtime_environment(ROOT, language=code)['TLG_LANGUAGE'], str(value))
                self.assertNotIn('TLG_LANGUAGE', run.runtime_environment(ROOT))
                self.assertNotIn('ISS_LANGUAGE', run.runtime_environment(ROOT, language=code))

    def test_invalid_or_uninstalled_language_fails_before_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / 'output'
            with patch.object(run, 'validate_game') as validate, patch.object(run, 'prepare_image') as prepare:
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    run.main(['--game-dir', str(root), '--out', str(out), '--language', 'unsupported'])
                validate.assert_not_called()
                with self.assertRaises(ValueError):
                    run.main(['--game-dir', str(root), '--out', str(out), '--language', 'fr-FR'])
                prepare.assert_not_called()
                self.assertFalse(out.exists())
            with self.assertRaises(ValueError):
                run.runtime_environment(out, language='bad', capture_dir=root / 'frames')
            self.assertFalse((root / 'frames').exists())

    def test_english_preparation_diagnostics(self):
        from port.launcher.knack2.ui.i18n import translate
        self.assertEqual(translate('en-US', 'Preparação'), 'Preparation')
        self.assertEqual(translate('en-US', 'Não iniciou: nenhuma instalação válida foi confirmada.'),
                         'Did not start: no valid installation has been confirmed.')
        self.assertEqual(translate('en-US', 'ISA: rewrite table unavailable (iced_x86 is not installed)'),
                         'ISA: rewrite table unavailable (iced_x86 is not installed)')

    def test_all_catalogs_cover_translated_qml_bindings(self):
        import json
        import re
        from port.launcher.knack2.ui.i18n import MESSAGES, translate
        used = set()
        for name in ['Main.qml', 'LogPanel.qml', 'OptionSwitch.qml']:
            text = (ROOT / 'port/launcher/knack2/ui' / name).read_text(encoding='utf-8')
            for match in re.finditer(r'qsTr\(("(?:[^"\\]|\\.)*")\)', text):
                used.add(json.loads(match.group(1)))
        self.assertTrue(used)
        self.assertFalse(used - MESSAGES.keys())
        for source in used:
            self.assertEqual(len(MESSAGES[source]), 3)
            self.assertTrue(all(MESSAGES[source]))
            self.assertEqual(translate('pt-BR', source), source)
        # Runtime diagnostic strings are intentionally not rewritten.
        self.assertEqual(translate('fr-FR', 'external error 75 at 0x1234'), 'external error 75 at 0x1234')


if __name__ == '__main__':
    unittest.main()
