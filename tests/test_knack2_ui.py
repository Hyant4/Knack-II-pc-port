# SPDX-License-Identifier: GPL-2.0-or-later
"""F3A proofs: synthetic runners, private files, bounded UI, no core execution."""
from __future__ import annotations

import importlib.util
import hashlib
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QUICK_BACKEND', 'software')

from port.launcher.knack2.runner import RunnerState
from port.launcher.knack2.ui.demo import SimulatedRunner

HAS_QT = importlib.util.find_spec('PySide6') is not None
if HAS_QT:
    from PySide6.QtCore import QObject, QPointF, QThread, QTimer, Qt, qInstallMessageHandler
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQuickControls2 import QQuickStyle
    from PySide6.QtTest import QTest
    from port.launcher.knack2.ui.app import create_window
    from port.launcher.knack2.ui.backend import Backend, MAX_LINE, MAX_PENDING, MAX_ROWS
    from port.launcher.knack2 import config, validator


class DependencyTests(unittest.TestCase):
    def test_missing_qt_exits_without_launching_game(self):
        # -S removes site-packages, reproducing a Python without Qt.
        result = subprocess.run([sys.executable, '-S', '-m', 'port.launcher.knack2', '--demo'],
                                cwd=ROOT, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn('Qt/PySide6 indispon', result.stderr)
        self.assertNotIn('Traceback', result.stderr)

    def test_no_real_execution_cli(self):
        result = subprocess.run([sys.executable, '-m', 'port.launcher.knack2', '--real',
                                 '--offscreen', '--quit-after-ms', '100'],
                                cwd=ROOT, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0 if HAS_QT else 2)


class DemoRunnerTests(unittest.TestCase):
    def test_finite_repeatable_session_and_worker_callbacks(self):
        states, callback_threads, guest = [], [], []
        def on_state(state):
            states.append(state)
            callback_threads.append(threading.get_ident())
        runner = SimulatedRunner(on_state_change=on_state, on_log_line=lambda line: None,
                                 on_guest_log_line=guest.append, interval=0.005)
        self.addCleanup(runner.close)
        with patch('subprocess.Popen', side_effect=AssertionError('No processes allowed')):
            runner.start()
            runner._thread.join(2)
            self.assertFalse(runner._thread.is_alive())
            self.assertEqual(states, [RunnerState.STARTING, RunnerState.RUNNING,
                                     RunnerState.STOPPING, RunnerState.FINISHED])
            self.assertEqual(len(guest), 30)
            self.assertTrue(all(t != threading.get_ident() for t in callback_threads))
            first = runner.session_info.session_id
            runner.start()
            runner._thread.join(2)
            self.assertNotEqual(first, runner.session_info.session_id)
            self.assertEqual(len(runner.get_log_tail_lines()), 30)


@unittest.skipUnless(HAS_QT, 'PySide6 indisponível; testes gráficos ignorados explicitamente.')
class QtUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        QQuickStyle.setStyle('Basic')
        cls.app = QGuiApplication.instance() or QGuiApplication(['knack2-f3a-tests'])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.messages = []
        self.previous_handler = qInstallMessageHandler(lambda kind, context, message: self.messages.append(message))
        self.process_guard = patch('subprocess.Popen', side_effect=AssertionError('F3A cannot launch processes'))
        self.process_guard.start()
        self.backend = Backend(runner_factory=lambda **kwargs: SimulatedRunner(interval=0.01, **kwargs))
        self.engine, self.window, _ = create_window(self.backend)
        self.pump(80)

    def tearDown(self):
        self.window.close()
        self.backend.shutdown()
        self.engine.deleteLater()
        self.pump(30)
        self.process_guard.stop()
        qInstallMessageHandler(self.previous_handler)
        failures = [m for m in self.messages if any(s in m for s in
                    ('ReferenceError', 'TypeError', 'binding loop', 'Binding loop', 'failed to load', 'Error:'))]
        self.assertEqual(failures, [])

    def test_header_uses_the_packaged_build_label(self):
        self.engine.rootContext().setContextProperty('launcherVersion', 'pr19-abcdef0')
        self.pump()
        self.assertEqual(self.item('launcherVersionLabel').property('text'), 'pr19-abcdef0')

    def pump(self, milliseconds=20):
        QTest.qWait(milliseconds)

    def until(self, predicate, timeout=2500):
        deadline = time.monotonic() + timeout / 1000
        while not predicate() and time.monotonic() < deadline:
            self.pump(10)
        self.assertTrue(predicate(), 'Condition did not arrive within finite deadline')

    def item(self, name):
        item = self.window.findChild(QObject, name)
        if item is None:
            # Repeater delegates belong to the visual tree, not always the QObject tree.
            pending = [self.window.contentItem()]
            while pending:
                candidate = pending.pop()
                if candidate.objectName() == name:
                    item = candidate
                    break
                pending.extend(candidate.childItems())
        self.assertIsNotNone(item, name)
        return item

    def click(self, name):
        item = self.item(name)
        # Settings now have categories and a clipped viewport. Exercise actual
        # visible coordinates, as a user does after scrolling to a control.
        parent = item.parentItem()
        while parent is not None:
            if parent.property('contentY') is not None:
                content = parent.property('contentItem')
                position = item.mapToItem(content, QPointF(0, 0))
                bottom = position.y() + item.height()
                if position.y() < parent.property('contentY') or bottom > parent.property('contentY') + parent.height():
                    parent.setProperty('contentY', max(0, min(position.y() - 12,
                        parent.property('contentHeight') - parent.height())))
                    self.pump(20)
                break
            parent = parent.parentItem()
        point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2)).toPoint()
        QTest.mouseClick(self.window, Qt.LeftButton, Qt.NoModifier, point)
        self.pump(20)

    def test_qml_load_and_real_button_states(self):
        self.assertTrue(self.window.isVisible())
        self.assertEqual(self.window.title(), 'Knack II PC — Demonstração')
        self.assertTrue(self.item('playButton').property('enabled'))
        self.assertFalse(self.item('stopButton').property('enabled'))
        self.click('playButton')
        self.until(lambda: self.backend.state == 'RUNNING')
        self.assertFalse(self.item('playButton').property('enabled'))
        self.assertTrue(self.item('stopButton').property('enabled'))
        self.click('stopButton')
        self.until(lambda: self.backend.state == 'FINISHED')
        self.assertTrue(self.item('playButton').property('enabled'))

    def test_launcher_language_switch_retranslates_without_changing_game_language(self):
        self.window.resize(980, 700)
        for button, locale, title, settings in (
                ('uiLanguageEN', 'en-US', 'Knack II PC — Demo', 'Settings'),
                ('uiLanguageRU', 'ru-RU', 'Knack II PC — Демонстрация', 'Настройки'),
                ('uiLanguagePT', 'pt-BR', 'Knack II PC — Demonstração', 'Configurações')):
            self.click(button)
            self.assertEqual(self.backend.uiLanguage, locale)
            self.assertEqual(self.backend._language, 'pt-BR')
            self.assertEqual(self.window.title(), title)
            self.assertEqual(self.item('navPage1').property('text'), settings)
            self.assertTrue(self.item(button).property('checked'))
            self.click(button)
            self.assertTrue(self.item(button).property('checked'))
        self.backend.setUiLanguage('ru-RU')
        self.backend.setLanguage('fr-FR')
        self.assertEqual(self.backend.uiLanguage, 'ru-RU')
        self.assertEqual(self.backend._language, 'fr-FR')
        self.backend.setUiLanguage('invalid')
        self.assertEqual(self.backend.uiLanguage, 'ru-RU')

    def test_native_controller_guide_front_assets_and_button_actions(self):
        self.window.setProperty('page', 1)
        self.window.setProperty('settingsTab', 2)
        self.pump(70)
        expected = [('controller-ps-photo.png', 'ps'),
                    ('controller-xbox-one-frontal.png', 'xbox'),
                    ('controller-nintendo-front-clean.png', 'nintendo')]
        guide = self.item('controllerGuide')
        for index, (filename, family) in enumerate(expected):
            with self.subTest(controller=family):
                self.item('guideFamily').setProperty('currentIndex', index)
                self.pump(80)
                self.assertEqual(guide.property('family'), family)
                self.assertTrue(self.item('controllerPhoto').property('source').toLocalFile().endswith(filename))
                for control, title in [('square', 'Soco'), ('cross', 'Pular'),
                                       ('circle', 'Chute'), ('triangle', 'Ação especial')]:
                    self.click('guideControl_' + control)
                    self.assertEqual(self.item('guideFunction').property('text'), title)
                self.click('guideShoulder_l1')
                self.assertEqual(self.item('guideFunction').property('text'), 'Defender')
        self.assertEqual(self.item('guideAllCommands').property('count'), 16)
        self.click('guideTab1')
        self.assertEqual(guide.property('guidePage'), 1)
        self.click('guideTab2')
        self.assertEqual(guide.property('guidePage'), 2)
        self.click('guideTab0')
        self.assertEqual(guide.property('guidePage'), 0)

    def test_new_categories_keep_video_and_mouse_bindings(self):
        self.window.setProperty('page', 1)
        self.click('settingsTab1')
        self.assertEqual(self.window.property('settingsTab'), 1)
        self.backend.setSetting('display_mode', 'borderless')
        self.pump(20)
        self.assertFalse(self.item('windowResolution').property('enabled'))
        self.backend.setSetting('display_mode', 'windowed')
        self.pump(20)
        self.assertTrue(self.item('windowResolution').property('enabled'))
        self.click('settingsTab2')
        self.click('mouseEnabled')
        self.assertEqual(self.backend.settings['mouse_enabled'], '1')
        self.backend.saveSettings()
        self.assertEqual(config.load_knack2_ini(self.backend.workspace / 'knack2.ini')['mouse_enabled'], '1')

    def test_folder_url_path_and_invalid_installation_are_rendered(self):
        with tempfile.TemporaryDirectory(prefix='Knack II espaço ç ') as path:
            from PySide6.QtCore import QUrl
            self.backend.selectFolder(QUrl.fromLocalFile(path).toString())
            self.assertEqual(self.backend.gameDir, path)
            self.assertEqual(self.item('gamePath').property('text'), path)
            self.backend.validateFolder()
            self.until(lambda: not self.backend.validating)
            self.assertEqual(self.backend.validationState, 'invalid')
            self.assertIn('param.sfo', self.item('validationMessage').property('text'))
            self.assertTrue(self.backend.canPlay)  # Demonstration needs no dump.

    def test_empty_folder_validation(self):
        self.backend.validateFolder()
        self.assertEqual(self.backend.validationState, 'invalid')
        self.assertIn('Selecione', self.backend.validationText)

    def test_slow_validation_does_not_block_gui_and_prevents_selection_races(self):
        gate = threading.Event()
        self.addCleanup(gate.set)
        worker_ids = []
        def validate(path):
            worker_ids.append(threading.get_ident())
            gate.wait(1)
            return {'app_title': 'Knack II sintético', 'title_id': 'CUSA08014', 'version': '01.01'}
        self.backend._validate_dump = validate
        self.backend.selectFolder('synthetic')
        self.backend.validateFolder()
        heartbeat = []
        QTimer.singleShot(10, lambda: heartbeat.append(True))
        self.pump(40)
        self.assertTrue(heartbeat)
        self.assertTrue(self.backend.validating)
        self.backend.selectFolder('other')
        self.assertEqual(self.backend.gameDir, 'synthetic')
        self.assertFalse(self.item('gamePath').property('enabled'))
        gate.set()
        self.until(lambda: not self.backend.validating)
        self.assertEqual(self.backend.validationState, 'valid')
        self.assertNotEqual(worker_ids[0], threading.get_ident())

    def test_settings_widgets_and_private_f1_roundtrip(self):
        self.window.setProperty('page', 1)
        self.window.setProperty('settingsTab', 1)
        self.pump(60)
        for name in ('displayMode', 'vsync', 'fpsLimit', 'fsr1', 'rcas', 'rcasStrength'):
            self.assertTrue(self.item(name).property('visible'), name)
        self.click('fsr1')
        self.assertEqual(self.backend.settings['fsr1'], '1')
        self.backend.setSetting('display_mode', 'borderless')
        self.backend.setSetting('rcas', True)
        self.backend.setSetting('rcas_strength', 73)
        self.backend.saveSettings()
        written = config.load_knack2_ini(self.backend.workspace / 'knack2.ini')
        self.assertEqual(written['rcas_strength'], '73')
        self.assertEqual(written['display_mode'], 'borderless')
        self.assertEqual(self.item('displayMode').property('currentIndex'), 1)
        self.assertTrue(self.item('rcasStrength').property('enabled'))
        self.assertEqual(self.item('rcasStrength').property('value'), 73)
        self.window.setProperty('settingsTab', 2)
        self.pump(30)
        self.assertTrue(self.item('mouseEnabled').property('visible'))
        self.assertEqual(config.load_launcher_settings(self.backend.workspace / 'launcher_settings.json')['mode'], 'demo')
        workspace = self.backend.workspace
        self.backend.shutdown()
        self.assertFalse(workspace.exists())

    def test_settings_reject_bad_values_and_protected_keys(self):
        old = self.backend.settings
        for key, value in [('rcas_strength', 101), ('vsync', '1\ninjected=1'), ('ngs2_atrac9_hle', 0)]:
            self.backend.setSetting(key, value)
            self.assertTrue(self.backend.notificationError)
            self.assertEqual(self.backend.settings, old)

    def test_rcas_slider_zero_fifty_hundred_save_and_navigation(self):
        self.window.resize(1180, 1060)
        self.window.setProperty('page', 1)
        self.window.setProperty('settingsTab', 1)
        self.backend.setSetting('rcas', True)
        self.pump(40)
        slider = self.item('rcasStrength')
        slider.forceActiveFocus()
        for expected, key in [(0, Qt.Key_Left), (50, None), (100, Qt.Key_Right)]:
            with self.subTest(percent=expected):
                if key is None:
                    self.click('rcasStrength')
                else:
                    slider.forceActiveFocus()
                    for _ in range(50):
                        QTest.keyClick(self.window, key)
                self.pump(20)
                self.assertEqual(slider.property('value'), expected)
                self.assertEqual(self.backend.settings['rcas_strength'], str(expected))
                self.assertEqual(self.item('rcasPercent').property('text'), f'{expected}%')
                self.assertFalse(self.backend.notificationError, self.backend.notification)
                self.click('saveDemoSettings')
                self.assertIn('Configurações salvas', self.backend.notification)
                written = config.load_knack2_ini(self.backend.workspace / 'knack2.ini')
                self.assertEqual(written['rcas_strength'], str(expected))
                for page in (0, 2, 1):
                    self.click(f'navPage{page}')
                    self.assertEqual(self.window.property('page'), page)
                    self.assertFalse(self.backend.notificationError)
                    self.assertNotIn('Valor inteiro inválido', self.backend.notification)

    def test_rcas_float_boundary_normalizes_before_strict_f1(self):
        for value, expected in [(0.0, 0), (49.6, 50), (100.0, 100), (-1.0, 0), (101.0, 100)]:
            with self.subTest(value=value), patch.object(config, 'sanitize_and_validate_key_value',
                    wraps=config.sanitize_and_validate_key_value) as validate:
                self.backend.setRcasStrength(value)
                argument = validate.call_args.args[1]
                self.assertIs(type(argument), int)
                self.assertEqual(argument, expected)
                self.assertEqual(self.backend.settings['rcas_strength'], str(expected))
        for value in (float('nan'), float('inf'), float('-inf')):
            old = self.backend.settings
            self.backend.setRcasStrength(value)
            self.assertTrue(self.backend.notificationError)
            self.assertEqual(self.backend.settings, old)
        # Generic F1 input remains strict; only the Slider boundary converts.
        with self.assertRaises(ValueError):
            config.sanitize_and_validate_key_value('rcas_strength', 100.0)

    def test_valid_save_clears_old_error_across_pages_and_notification_can_dismiss(self):
        self.backend.setSetting('rcas_strength', '100.0')
        self.assertTrue(self.backend.notificationError)
        self.assertIn('100.0', self.backend.notification)
        self.backend.setRcasStrength(100.0)
        self.backend.saveSettings()
        self.assertFalse(self.backend.notificationError)
        self.assertIn('Configurações salvas', self.backend.notification)
        for page in (1, 0, 2, 1):
            self.click(f'navPage{page}')
            self.assertNotIn('Valor inteiro inválido', self.item('notificationText').property('text'))
            self.assertFalse(self.backend.notificationError)
        # Error history stays available only in the explicit diagnostics dialog.
        self.assertIn('100.0', self.backend.errorText)
        self.click('dismissNotification')
        self.assertFalse(self.item('notificationBanner').property('visible'))
        for page in (0, 2, 1):
            self.click(f'navPage{page}')
            self.assertFalse(self.item('notificationBanner').property('visible'))

    def test_installation_version_and_hash_validation_ignore_folder_names(self):
        def sfo_bytes(version):
            keys, values, entries = b'', b'', b''
            for key, value in {'TITLE_ID': 'CUSA08014', 'APP_VER': version, 'TITLE': 'Knack II synthetic'}.items():
                data = value.encode('utf-8') + b'\0'
                entries += struct.pack('<HHIII', len(keys), 0x0204, len(data), len(data), len(values))
                keys += key.encode('ascii') + b'\0'
                values += data
            start = 20 + len(entries)
            return struct.pack('<4sIIII', b'\0PSF', 0x101, start, start + len(keys), 3) + entries + keys + values

        executables = {'eboot.bin': b'synthetic eboot', 'game.elf': b'synthetic game'}
        profile = {'title_id': 'CUSA08014', 'version': '01.01',
                   'executables': {name: hashlib.sha256(data).hexdigest() for name, data in executables.items()},
                   'modules': {name: ['synthetic.prx'] for name in executables}}
        self.backend._validate_dump = lambda path: validator.validate_knack2_dump(path, profile=profile)
        with tempfile.TemporaryDirectory(prefix='knack2-f3a-version-') as directory:
            for folder, metadata, tamper, state, message in [
                ('Named_01.00', '01.01', False, 'valid', 'Instalação válida'),
                ('Named_01.01', '01.00', False, 'invalid', 'obtido: CUSA08014 01.00'),
                ('Claimed_01.01', '01.01', True, 'invalid', 'SHA-256')]:
                with self.subTest(folder=folder, metadata=metadata, tamper=tamper):
                    game = Path(directory) / folder
                    (game / 'sce_sys').mkdir(parents=True)
                    (game / 'sce_module').mkdir()
                    (game / 'sce_sys' / 'param.sfo').write_bytes(sfo_bytes(metadata))
                    for name, data in executables.items():
                        (game / name).write_bytes(data + (b'tampered' if tamper else b''))
                    (game / 'sce_module' / 'synthetic.prx').write_bytes(b'synthetic module')
                    # Only ELF module parsing is mocked. SFO parsing and SHA-256 are real.
                    with patch.object(validator.link_modules, 'module', return_value=None):
                        self.backend.selectFolder(str(game))
                        self.backend.validateFolder()
                        self.until(lambda: not self.backend.validating)
                    self.assertEqual(self.backend.validationState, state)
                    self.assertIn(message, self.backend.validationText)

    def test_worker_callbacks_arrive_on_qt_thread(self):
        seen_threads = []
        self.backend.changed.connect(lambda: seen_threads.append(QThread.currentThread()))
        self.backend.play()
        self.until(lambda: self.backend.state == 'FINISHED')
        self.assertTrue(seen_threads)
        self.assertTrue(all(t == self.app.thread() for t in seen_threads))
        self.assertTrue(self.backend.guest_model.lines)

    def test_failure_then_new_session_has_clean_logs_and_unique_id(self):
        self.backend.play()
        self.until(lambda: self.backend.state == 'RUNNING')
        self.backend.simulateError()
        self.until(lambda: self.backend.state == 'FAILED')
        self.pump(60)
        first = self.backend.sessionId
        self.assertIn('Erro simulado', self.backend.errorText)
        self.window.setProperty('page', 2)
        self.pump(30)
        self.click('showErrors')
        self.assertTrue(self.item('errorsDialog').property('visible'))
        self.item('errorsDialog').setProperty('visible', False)
        self.backend.post_log('guest', 'old-session-canary')
        self.backend._drain()
        self.backend.play()
        self.until(lambda: self.backend.state == 'FINISHED')
        self.assertNotEqual(first, self.backend.sessionId)
        self.assertNotIn('old-session-canary', self.backend.guest_model.lines)

    def test_double_start_and_immediate_stop_do_not_restore_starting_state(self):
        with patch.object(self.backend._runner, 'start', wraps=self.backend._runner.start) as start:
            self.backend.play()
            self.backend.play()
            self.assertEqual(start.call_count, 1)
            self.backend.stop()
            states = []
            self.backend.changed.connect(lambda: states.append(self.backend.state))
            self.until(lambda: self.backend.state == 'FINISHED')
            self.assertNotIn('STARTING', states)
            self.assertNotIn('RUNNING', states)

    def test_flood_is_bounded_and_gui_heartbeat_survives(self):
        worker = threading.Thread(target=lambda: [self.backend.post_log('guest', 'x' * 4096) for _ in range(8000)])
        worker.start(); worker.join(2)
        self.assertFalse(worker.is_alive())
        self.assertLessEqual(len(self.backend._pending), MAX_PENDING)
        self.assertGreater(self.backend._dropped, 0)
        heartbeat = []
        QTimer.singleShot(10, lambda: heartbeat.append(True))
        self.until(lambda: not self.backend._pending)
        self.assertTrue(heartbeat)
        self.assertEqual(self.backend.guest_model.rowCount(), MAX_ROWS)
        self.assertTrue(all(len(line) <= MAX_LINE for line in self.backend.guest_model.lines))

    def test_log_sources_clipboard_and_plain_text(self):
        self.backend.post_log('supervisor', 'supervisor-only')
        self.backend.post_log('guest', '<b>guest-only</b>')
        self.backend._drain()
        self.assertEqual(self.backend.supervisor_model.lines, ['supervisor-only'])
        self.assertEqual(self.backend.guest_model.lines, ['<b>guest-only</b>'])
        self.backend.copyLogs()
        self.assertIn('Supervisor\nsupervisor-only', self.app.clipboard().text())
        self.assertIn('Guest (sintético)\n<b>guest-only</b>', self.app.clipboard().text())

    def test_close_during_start_and_reopen(self):
        self.backend.play()
        self.window.close()
        self.pump(40)
        self.assertFalse(self.backend._runner._thread.is_alive())
        self.assertFalse(self.backend.workspace.exists())
        other_engine, other_window, other = create_window()
        try:
            self.pump(40)
            self.assertTrue(other_window.isVisible())
            self.assertEqual(other.state, 'IDLE')
        finally:
            other_window.close()
            other.shutdown()
            other_engine.deleteLater()

    def test_close_during_validation_ignores_late_result(self):
        gate = threading.Event()
        self.addCleanup(gate.set)
        def validate(path):
            gate.wait(1)
            raise ValueError('late')
        self.backend._validate_dump = validate
        self.backend.selectFolder('synthetic')
        self.backend.validateFolder()
        self.window.close()
        gate.set()
        self.backend._validation_thread.join(1)
        self.assertTrue(self.backend._validation_results.empty())
        self.assertFalse(self.backend._timer.isActive())

    def test_screenshot_all_pages_and_minimum_size(self):
        with tempfile.TemporaryDirectory(prefix='knack2-f3a-images-') as directory:
            for page in range(3):
                self.window.setProperty('page', page)
                self.pump(100)
                image = self.window.grabWindow()
                self.assertFalse(image.isNull())
                self.assertTrue(image.save(str(Path(directory) / f'page-{page}.png')))
            self.window.resize(980, 700)
            self.window.setProperty('page', 0)
            self.pump(60)
            self.assertTrue(self.item('playButton').property('visible'))


if __name__ == '__main__':
    unittest.main()
