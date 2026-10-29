"""Error localization across PT/EN/RU, preserving technical evidence and UI state."""
import ast
import os
from pathlib import Path
from string import Formatter
import tempfile
import time
import unittest
import builtins
import contextlib
import io
import json
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QUICK_BACKEND', 'software')
from PySide6.QtCore import QObject
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtTest import QTest
from port.launcher.knack2 import validator
from port.launcher.knack2.ui.i18n import translate, translate_failure, _translate_catalog, MESSAGES
from port.launcher.knack2.ui.i18n_errors import ENTRIES, translate_error
from port.launcher.knack2.ui.real import RealBackend
from port.launcher.knack2.ui.app import create_window

ROOT = Path(__file__).resolve().parents[1]


class ErrorCatalogTests(unittest.TestCase):
    def test_bootstrap_failure_and_cli_validation_follow_saved_language_without_qt(self):
        from port.launcher.knack2.ui import app
        with tempfile.TemporaryDirectory(prefix='knack-bootstrap-i18n-') as directory:
            root = Path(directory)
            original_import = builtins.__import__
            def without_qt(name, *args, **kwargs):
                if name.startswith('PySide6'):
                    raise ImportError('synthetic unavailable Qt')
                return original_import(name, *args, **kwargs)
            for locale, unavailable, invalid in (
                    ('pt-BR', 'Qt/PySide6 indisponível.', '--quit-after-ms deve ser >= 0'),
                    ('en-US', 'Qt/PySide6 unavailable.', '--quit-after-ms must be >= 0'),
                    ('ru-RU', 'Qt/PySide6 недоступен.', '--quit-after-ms должен быть >= 0')):
                (root / 'launcher_settings.json').write_text(json.dumps({'ui_language': locale}), encoding='utf-8')
                error = io.StringIO()
                with contextlib.redirect_stderr(error), patch.object(builtins, '__import__', side_effect=without_qt):
                    self.assertEqual(app.main(['--data-dir', str(root)]), 2)
                self.assertIn(unavailable, error.getvalue())
                error = io.StringIO()
                with contextlib.redirect_stderr(error), self.assertRaises(SystemExit) as caught:
                    app.main(['--data-dir', str(root), '--quit-after-ms', '-1'])
                self.assertEqual(caught.exception.code, 2)
                self.assertIn(invalid, error.getvalue())

    def test_hash_reason_and_identifiers_survive_nested_translation(self):
        actual, expected = 'a' * 64, 'b' * 64
        error = validator.KnackHashMismatchError(
            f'eboot.bin: SHA-256 difere do dump suportado. Encontrado: {actual}. Esperado: {expected}.')
        source = validator.describe_validation_failure(error)
        for locale, heading, reason in (
                ('pt-BR', 'Falha de validação:', 'Um executável não corresponde'),
                ('en-US', 'Validation failed:', 'An executable does not match'),
                ('ru-RU', 'Ошибка проверки:', 'Исполняемый файл не соответствует')):
            with self.subTest(locale=locale):
                rendered = translate(locale, source)
                self.assertTrue(rendered.startswith(heading))
                self.assertIn(reason, rendered)
                for value in (actual, expected, 'eboot.bin', 'SHA-256'):
                    self.assertIn(value, rendered)

    def test_hash_variants_and_exact_profile_remain_visible(self):
        profile = validator.load_profile()
        actual = 'c' * 64
        with self.assertRaises(validator.KnackHashMismatchError) as caught:
            validator.executable_layout_for_hash(profile, actual)
        for locale in ('pt-BR', 'en-US', 'ru-RU'):
            rendered = translate_failure(locale, str(caught.exception))
            self.assertIn(actual, rendered)
            self.assertIn('CUSA08014-01.01', rendered)
            for value in [profile['executables']['game.elf'],
                          *(v['sha256'] for v in profile.get('game_elf_variants', []))]:
                self.assertIn(value, rendered)

    def test_paths_and_values_that_resemble_messages_are_opaque(self):
        path = r'C:\Jogos\Atualização cancelada.\game{original}.elf'
        source = 'Backup alterado: ' + path
        rendered = translate('ru-RU', source)
        self.assertEqual(rendered, 'Резервная копия изменена: ' + path)
        self.assertEqual(translate('en-US', 'Backup alterado: Atualização cancelada.'),
                         'Backup changed: Atualização cancelada.')

    def test_unknown_system_diagnostic_has_localized_heading_and_exact_detail(self):
        detail = "[WinError 5] Acesso negado: 'C:\\Jogos\\a{b}.elf'"
        for locale, heading in (('pt-BR', 'Detalhes técnicos:'), ('en-US', 'Technical details:'),
                                ('ru-RU', 'Технические подробности:')):
            rendered = translate_failure(locale, detail)
            self.assertIn(heading, rendered)
            self.assertTrue(rendered.endswith(detail))
        self.assertEqual(translate('ru-RU', detail), detail)  # Raw log translation is opt-in.

    def test_nested_native_error_preserves_line_endings(self):
        source = 'Falha ao extrair PKG: PKG extraction failed: Truncated PKG\r\n'
        self.assertEqual(translate('en-US', source),
                         'Could not extract PKG: PKG extraction failed: Truncated PKG\r\n')
        self.assertEqual(translate('ru-RU', source),
                         'Не удалось извлечь PKG: Ошибка извлечения PKG: PKG обрезан\r\n')

    def test_template_translations_preserve_every_argument(self):
        fields = lambda text: {name for _, name, _, _ in Formatter().parse(text) if name is not None}
        for source, translations in ENTRIES:
            for locale, target in translations.items():
                with self.subTest(source=source, locale=locale):
                    self.assertEqual(fields(source), fields(target))

    def test_current_application_exceptions_all_have_pt_en_ru_templates(self):
        def sample(node):
            if isinstance(node, ast.Constant) and isinstance(node.value, str):
                return node.value
            if isinstance(node, ast.JoinedStr):
                return ''.join(part.value if isinstance(part, ast.Constant) else 'OPAQUE_VALUE'
                               for part in node.values)
            if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
                left, right = sample(node.left), sample(node.right)
                if left is not None or right is not None:
                    return (left if left is not None else 'OPAQUE_VALUE') + (right if right is not None else 'OPAQUE_VALUE')
            return None

        for name in ('validator.py', 'pkg_update.py', 'pkg_base.py', 'config.py', 'runner.py', 'storage.py',
                     'paths.py', 'ui/backend.py', 'ui/real.py', 'ui/demo.py', 'ui/app.py'):
            tree = ast.parse((ROOT / 'port/launcher/knack2' / name).read_text(encoding='utf-8'))
            for node in ast.walk(tree):
                if not (isinstance(node, ast.Raise) and isinstance(node.exc, ast.Call) and node.exc.args):
                    continue
                text = sample(node.exc.args[0])
                if text is None:
                    continue  # Rethrown arbitrary external errors use the localized diagnostic heading.
                for locale in ('pt-BR', 'en-US', 'ru-RU'):
                    with self.subTest(file=name, line=node.lineno, locale=locale):
                        translated = _translate_catalog(locale, text)
                        if locale == 'pt-BR' and text in MESSAGES:
                            translated = text
                        if translated is None:
                            translated = translate_error(locale, text)
                        self.assertIsNotNone(translated, text)


class ErrorUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        QQuickStyle.setStyle('Basic')
        cls.app = QGuiApplication.instance() or QGuiApplication(['error-i18n-tests'])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='knack-errors-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.calls = []

        def validate(path):
            self.calls.append(path)
            raise validator.KnackHashMismatchError('game.elf: SHA-256 difere do dump suportado')

        self.backend = RealBackend(data_dir=self.root / 'data', validate_dump=validate)
        self.backend._game_dir = str(self.root / 'game')
        self.engine, self.window, _ = create_window(self.backend)
        self.window.setProperty('page', 3)
        self.addCleanup(self.cleanup_ui)

    def cleanup_ui(self):
        self.backend.updateManager.cancel()
        self.until(lambda: not self.backend.validating and not self.backend.updateManager.busy)
        self.window.close()
        self.backend.shutdown()
        self.engine.deleteLater()
        self.app.processEvents()

    def until(self, predicate):
        deadline = time.monotonic() + 3
        while not predicate() and time.monotonic() < deadline:
            QTest.qWait(10)
        self.assertTrue(predicate())

    def test_existing_validation_notification_and_history_retranslate_without_recheck(self):
        self.backend.validateFolder()
        self.until(lambda: not self.backend.validating)
        for locale, heading, reason in (
                ('en-US', 'Validation failed:', 'An executable does not match'),
                ('ru-RU', 'Ошибка проверки:', 'Исполняемый файл не соответствует'),
                ('pt-BR', 'Falha de validação:', 'Um executável não corresponde')):
            self.backend.setUiLanguage(locale)
            self.app.processEvents()
            for rendered in (self.backend.validationText, self.backend.notification, self.backend.errorText):
                self.assertIn(heading, rendered)
                self.assertIn(reason, rendered)
            message = self.window.findChild(QObject, 'validationMessage')
            self.assertIn(heading, message.property('text'))
            self.assertFalse(self.backend.canPlay)
        self.assertEqual(len(self.calls), 1)
        self.assertEqual(self.backend._language, 'pt-BR')

    def test_pkg_error_and_progress_retranslate_after_display(self):
        manager = self.backend.updateManager
        manager._events.put(('inspect', None,
            'Update incompatível: jogo CUSA08014; PKG CUSA07670. Use atualização do mesmo CUSA.'))
        manager._drain()
        for locale, expected in (('en-US', 'Incompatible update:'), ('ru-RU', 'Несовместимое обновление:'),
                                  ('pt-BR', 'Update incompatível:')):
            self.backend.setUiLanguage(locale)
            self.app.processEvents()
            self.assertIn(expected, manager.status)
            self.assertIn(expected, self.backend.notification)
            self.assertIn(expected, self.window.findChild(QObject, 'updateStatus').property('text'))
            self.assertIn('CUSA08014', manager.status)
            self.assertIn('CUSA07670', manager.status)
        manager._events.put(('progress', 'Validando instalação atualizada.', ''))
        manager._drain()
        self.backend.setUiLanguage('en-US')
        self.assertEqual(manager.status, 'Validating updated installation.')
        self.backend.setUiLanguage('ru-RU')
        self.assertEqual(manager.status, 'Проверка обновлённой установки.')

    def test_session_settings_and_save_errors_use_selected_language(self):
        examples = (
            ('Processo supervisor encerrou com código de saída 7', 'exited with code 7', 'завершился с кодом 7'),
            ("Valor booleano inválido para 'vsync': 'invalid' (esperado 0/1)", 'Invalid boolean', 'Недопустимое логическое'),
            ('Este perfil de saves já está em uso. Encerre a outra sessão.', 'save profile is already in use', 'профиль сохранений уже используется'),
            ('Runtime ausente: compile tlg-probe antes de jogar.', 'Runtime missing', 'Среда выполнения отсутствует'),
        )
        for source, english, russian in examples:
            self.backend._notice(source, True)
            self.backend.setUiLanguage('en-US')
            self.assertIn(english, self.backend.notification)
            self.backend.setUiLanguage('ru-RU')
            self.assertIn(russian, self.backend.notification)


if __name__ == '__main__':
    unittest.main()
