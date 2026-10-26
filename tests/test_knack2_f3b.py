# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused F3B proofs. No dump, guest, Vulkan, controller campaign or real saves."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QUICK_BACKEND', 'software')
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from PySide6.QtCore import QObject, QTimer, QMetaObject, Q_ARG, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuickControls2 import QQuickStyle
from PySide6.QtTest import QTest
from port.launcher.knack2 import config
from port.launcher.knack2.runner import KnackRunner, RunnerState, SessionInfo
from port.launcher.knack2.ui.app import create_window
from port.launcher.knack2.ui.real import RealBackend, P2_USER_ID

spec = importlib.util.spec_from_file_location('f3b_supervisor', ROOT / 'run.py')
supervisor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(supervisor)


class FakeSession:
    """Expose the real command builder; never create a process."""
    def __init__(self, game, **kwargs):
        self.contract = KnackRunner(game, **kwargs)
        self.options = kwargs
        self.session_info = SessionInfo()
        self.state = RunnerState.IDLE
        self.error_message = ''
        self.exit_code = None
        self.pid = None
        self.fail_stop = False
        self.start_gate = None
        self.stop_gate = None
        self.closed = False

    @property
    def is_running(self):
        return self.state in (RunnerState.STARTING, RunnerState.RUNNING, RunnerState.STOPPING)

    def emit(self, state):
        self.state = state
        self.options['on_state_change'](state)

    def start(self):
        self.emit(RunnerState.STARTING)
        if self.start_gate is not None:
            self.start_gate.wait(3)
        out = self.contract.out_dir
        assert not out.exists() or self.contract.local_users == 1
        self.session_info = SessionInfo(session_id='session-fake', session_dir=out / 'session-fake',
                                        current_executable='game.elf')
        self.options['on_log_line']('supervisor only')
        self.options['on_guest_log_line']('guest only')
        self.emit(RunnerState.RUNNING)

    def stop(self):
        self.emit(RunnerState.STOPPING)
        if self.stop_gate is not None:
            self.stop_gate.wait(3)
        if self.fail_stop:
            raise RuntimeError('controlled stop failed; process remains alive')
        self.exit_code = 130
        self.emit(RunnerState.FAILED)
        return True

    def close(self):
        if self.is_running:
            raise RuntimeError('must stop first')
        self.closed = True


class F3BTests(unittest.TestCase):
    def test_launcher_language_persists_without_dump_or_game_language_change(self):
        path = self.root / 'data' / 'launcher_settings.json'
        config.save_launcher_settings({'language': 'fr-FR', 'custom': 'preserve'}, path=path)
        self.backend.setUiLanguage('ru-RU')
        saved = config.load_launcher_settings(path)
        self.assertEqual(saved['ui_language'], 'ru-RU')
        self.assertEqual(saved['language'], 'fr-FR')
        self.assertEqual(saved['custom'], 'preserve')
        restored = RealBackend(data_dir=path.parent)
        try:
            self.assertEqual(restored.uiLanguage, 'ru-RU')
            self.assertEqual(restored._language, 'fr-FR')
            restored.saveSettings()
            self.assertEqual(config.load_launcher_settings(path)['ui_language'], 'ru-RU')
        finally:
            restored.shutdown()

    @classmethod
    def setUpClass(cls):
        QQuickStyle.setStyle('Basic')
        cls.app = QGuiApplication.instance() or QGuiApplication(['f3b-tests'])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='knack-f3b ')
        self.root = Path(self.temp.name)
        # A fresh checkout has no native runtime. The synthetic session never executes it.
        runtime = self.root / 'synthetic-runtime.exe'
        runtime.write_bytes(b'test fixture, never executed')
        self.runtime_path_patch = patch('port.launcher.knack2.ui.real.runtime_path', return_value=runtime)
        self.runtime_path_patch.start()
        self.addCleanup(self.runtime_path_patch.stop)
        self.sessions = []
        self.messages = []
        self.previous_handler = qInstallMessageHandler(lambda kind, ctx, msg: self.messages.append(msg))
        self.guard = patch('subprocess.Popen', side_effect=AssertionError('No game or subprocess allowed'))
        self.guard.start()
        def factory(game, **kwargs):
            session = FakeSession(game, **kwargs)
            self.sessions.append(session)
            return session
        self.backend = RealBackend(data_dir=self.root / 'data', session_factory=factory,
                                   validate_dump=lambda path: {'app_title': 'Synthetic Knack II',
                                   'title_id': 'CUSA08014', 'version': '01.01',
                                   'languages': ['pt-BR', 'en-US', 'es-419', 'fr-FR']})
        self.engine, self.window, _ = create_window(self.backend)
        self.pump()

    def tearDown(self):
        for session in self.sessions:
            if isinstance(session, FakeSession):
                session.fail_stop = False
                for gate in (session.start_gate, session.stop_gate):
                    if gate is not None:
                        gate.set()
        if self.backend.canStop:
            self.backend.stop()
            self.until(lambda: not self.backend._working() and not self.backend.canStop)
        self.window.close()
        self.backend.shutdown()
        self.engine.deleteLater()
        self.pump()
        self.guard.stop()
        qInstallMessageHandler(self.previous_handler)
        failures = [m for m in self.messages if any(s in m for s in ('ReferenceError', 'TypeError', 'Binding loop', 'binding loop', 'failed to load'))]
        self.assertEqual(failures, [])
        self.temp.cleanup()

    def pump(self):
        QTest.qWait(60)

    def until(self, predicate, seconds=5):
        deadline = time.monotonic() + seconds
        while not predicate() and time.monotonic() < deadline:
            QTest.qWait(10)
        self.assertTrue(predicate(), f'finite condition failed: state={self.backend.state}')

    def validate(self):
        self.backend.selectFolder(str(self.root / 'dump'))
        self.backend.validateFolder()
        self.until(lambda: not self.backend.validating)
        self.assertTrue(self.backend.canPlay)

    def launch(self):
        self.backend.play()
        self.until(lambda: self.backend.state == 'RUNNING' and not self.backend._working())
        self.pump()
        return self.sessions[-1]

    def finish(self):
        self.backend.stop()
        self.until(lambda: self.backend.state == 'FINISHED' and not self.backend._working())

    def test_default_validation_gate_p1_command_settings_and_logs(self):
        self.assertEqual(self.backend.players, 1)
        self.assertFalse(self.backend.canPlay)
        self.assertEqual(self.sessions, [])
        self.backend.play()
        self.assertEqual(self.sessions, [])
        self.validate()
        self.backend.setSetting('display_mode', 'borderless')
        self.backend.setRcasStrength(100.0)
        session = self.launch()
        command = session.contract.build_command()
        self.assertEqual(Path(command[1]), ROOT / 'run.py')
        self.assertNotIn('--local-users', command)
        self.assertEqual(command[command.index('--timeout') + 1], '0')
        self.assertNotIn('--p2-user-id', command)
        self.assertEqual(session.contract.user_data, self.root / 'data/profiles/shared/user')
        settings = config.load_knack2_ini(session.contract.config_file)
        self.assertEqual(settings['display_mode'], 'borderless')
        self.assertEqual(settings['rcas_strength'], '100')
        self.assertEqual(settings['shader_preload'], '1')
        self.assertIn('supervisor only', self.backend.supervisor_model.lines)
        self.assertNotIn('guest only', self.backend.supervisor_model.lines)
        self.assertIn('guest only', self.backend.guest_model.lines)
        self.assertIn('supervisor only', self.backend._log_path.read_text(encoding='utf-8'))
        self.assertFalse(self.backend.editable)
        self.finish()
        self.assertTrue(session.closed)

    def test_gpu_precision_ui_persistence_and_child_environment(self):
        self.window.setProperty('page', 1)
        self.pump()
        mode = self.window.findChild(QObject, 'gpuReadbacks')
        eager = self.window.findChild(QObject, 'eagerReadback')
        self.assertIsNotNone(mode)
        self.assertIsNotNone(eager)
        self.assertEqual(mode.property('currentIndex'), 2)
        self.assertFalse(eager.property('checked'))
        for value in (0, 1, 2):
            mode.setProperty('currentIndex', value)
            self.assertTrue(QMetaObject.invokeMethod(mode, 'activated', Q_ARG(int, value)))
            self.pump()
            self.assertEqual(self.backend.settings['gpu_readbacks'], str(value))
            self.assertEqual(eager.property('enabled'), value == 2)
        eager.setProperty('checked', True)
        self.assertTrue(QMetaObject.invokeMethod(eager, 'toggled'))
        self.pump()
        self.assertEqual(self.backend.settings['eager_readback'], '1')
        self.backend.saveSettings()
        restored = RealBackend(data_dir=self.backend.workspace)
        try:
            self.assertEqual(restored.settings['gpu_readbacks'], '2')
            self.assertEqual(restored.settings['eager_readback'], '1')
        finally:
            restored.shutdown()
        self.validate()
        session = self.launch()
        env = supervisor.runtime_environment(self.root / 'supervisor', config_file=session.contract.config_file)
        self.assertEqual(env['TLG_READBACKS'], '2')
        self.assertEqual(env['TLG_EAGER_READBACK'], '1')
        self.finish()

    def test_gtx1060_preset_card_preserves_other_settings_and_persists_readbacks(self):
        self.window.setProperty('page', 1)
        self.window.setProperty('settingsTab', 1)
        self.backend.applyGpuPreset('conservative')
        self.backend.setSetting('gpu_readbacks', '0')
        self.backend.setSetting('eager_readback', '1')
        self.backend.setSetting('window_resolution', '3440x1440')
        self.backend.setSetting('shader_preload', '0')
        before = dict(self.backend.settings)
        self.pump()
        pending = [self.window.contentItem()]
        card = None
        while pending:
            item = pending.pop()
            if item.objectName() == 'gpuPreset_gtx_1060_black_texture':
                card = item
                break
            pending.extend(item.childItems())
        self.assertIsNotNone(card)
        profile = next(p for p in self.backend.gpuPresets if p['id'] == 'gtx_1060_black_texture')
        self.assertEqual(profile['label'], 'GTX 1060 black texture')
        self.assertTrue(profile['experimental'])
        self.assertIn('Correção ainda não confirmada', profile['when'])
        self.assertTrue(QMetaObject.invokeMethod(card, 'clicked'))
        self.pump()
        expected = {**before, 'gpu_readbacks': '2', 'eager_readback': '0'}
        self.assertEqual(self.backend.settings, expected)
        self.assertEqual(self.backend.gpuPresetId, 'conservative')
        self.assertIn('Salve as configurações e reinicie', self.backend.notification)
        self.assertEqual(self.window.findChild(QObject, 'gpuReadbacks').property('currentIndex'), 2)
        self.assertFalse(self.window.findChild(QObject, 'eagerReadback').property('checked'))
        self.backend.saveSettings()
        restored = RealBackend(data_dir=self.backend.workspace)
        try:
            self.assertEqual(restored.settings, expected)
        finally:
            restored.shutdown()
        self.validate()
        session = self.launch()
        self.assertFalse(card.property('enabled'))
        self.backend.applyGpuPreset('gtx_1060_black_texture')
        self.assertEqual(self.backend.settings, expected)
        env = supervisor.runtime_environment(self.root / 'supervisor', config_file=session.contract.config_file)
        self.assertEqual(env['TLG_READBACKS'], '2')
        self.assertEqual(env['TLG_EAGER_READBACK'], '0')
        self.finish()
        self.backend.setSetting('gpu_readbacks', '0')
        self.backend.setSetting('eager_readback', '1')
        self.assertEqual(self.backend.settings, before)

    def test_guided_gpu_presets_apply_restore_and_preserve_other_preferences(self):
        def card(name):
            pending = [self.window.contentItem()]
            while pending:
                item = pending.pop()
                if item.objectName() == name:
                    return item
                pending.extend(item.childItems())
            self.fail(f'Profile card missing: {name}')

        self.window.setProperty('page', 1)
        self.window.setProperty('settingsTab', 1)
        self.pump()
        self.assertFalse(self.window.property('gpuAdvanced'))
        self.assertFalse(self.window.findChild(QObject, 'gpuDiagnosticBp06').property('visible'))
        self.assertEqual(self.backend.gpuPresetId, 'default')
        self.backend.setSetting('window_resolution', '3440x1440')
        self.backend.setSetting('internal_resolution', '2560x1440')
        self.backend.setSetting('fps_limit', '30')
        other = {key: value for key, value in self.backend.settings.items()
                 if key not in config.GPU_DIAGNOSTIC_ENV}
        self.assertTrue(QMetaObject.invokeMethod(card('gpuPreset_amd_compat'), 'clicked'))
        self.pump()
        self.assertEqual(self.backend.gpuPresetId, 'amd_compat')
        self.assertIn('AMD: falha ao iniciar', self.window.findChild(QObject, 'gpuPresetCurrent').property('text'))
        self.assertEqual(self.backend.settings['pipeline_binaries'], '0')
        self.assertEqual(self.backend.settings['bp06'], '0')
        self.assertEqual(self.backend.settings['compute_async_build'], 'auto')
        toggle = self.window.findChild(QObject, 'gpuAdvancedToggle')
        toggle.setProperty('checked', True)
        self.assertTrue(QMetaObject.invokeMethod(toggle, 'toggled'))
        self.pump()
        self.assertTrue(self.window.findChild(QObject, 'gpuDiagnosticBp06').property('visible'))
        self.backend.setSetting('compute_async_build', '1')
        self.pump()
        self.assertEqual(self.backend.gpuPresetId, 'custom')
        self.assertIn('Personalizado', self.window.findChild(QObject, 'gpuPresetCurrent').property('text'))
        self.assertTrue(QMetaObject.invokeMethod(card('gpuPreset_conservative'), 'clicked'))
        self.pump()
        self.assertEqual(self.backend.gpuPresetId, 'conservative')
        self.backend.saveSettings()
        restored = RealBackend(data_dir=self.backend.workspace)
        try:
            self.assertEqual(restored.gpuPresetId, 'conservative')
        finally:
            restored.shutdown()
        before_invalid = dict(self.backend.settings)
        self.backend.applyGpuPreset('unknown')
        self.assertEqual(self.backend.settings, before_invalid)
        self.validate()
        session = self.launch()
        self.backend.applyGpuPreset('default')
        self.assertEqual(self.backend.gpuPresetId, 'conservative')
        for profile in ('default', 'amd_compat', 'conservative'):
            self.assertFalse(card('gpuPreset_' + profile).property('enabled'))
        env = supervisor.runtime_environment(self.root / 'supervisor', config_file=session.contract.config_file)
        self.assertTrue(all(env[variable] == '0' for variable in config.GPU_DIAGNOSTIC_ENV.values()))
        self.finish()
        self.assertTrue(QMetaObject.invokeMethod(card('gpuPreset_default'), 'clicked'))
        self.pump()
        self.assertEqual(self.backend.gpuPresetId, 'default')
        self.assertEqual({key: value for key, value in self.backend.settings.items()
                          if key not in config.GPU_DIAGNOSTIC_ENV}, other)
        self.backend.saveSettings()
        stored = config.load_knack2_ini(self.backend.workspace / 'knack2.ini')
        self.assertFalse(set(stored) & set(config.GPU_DIAGNOSTIC_ENV))

    def test_gpu_diagnostic_widgets_persistence_and_session_snapshot(self):
        self.window.setProperty('page', 1)
        self.window.setProperty('settingsTab', 1)
        self.window.setProperty('gpuAdvanced', True)
        self.pump()
        controls = {'pipeline_binaries': 'gpuDiagnosticPipelineBinaries',
                    'bp06': 'gpuDiagnosticBp06', 'compute_async_build': 'gpuDiagnosticComputeAsync'}
        for key, name in controls.items():
            item = self.window.findChild(QObject, name)
            self.assertIsNotNone(item)
            self.assertEqual(item.property('currentIndex'), 0)
            for index, value in enumerate(('auto', '0', '1')):
                item.setProperty('currentIndex', index)
                self.assertTrue(QMetaObject.invokeMethod(item, 'activated', Q_ARG(int, index)))
                self.pump()
                self.assertEqual(self.backend.settings[key], value)
            self.backend.setSetting(key, '0')
        self.backend.saveSettings()
        restored = RealBackend(data_dir=self.backend.workspace)
        try:
            for key in controls:
                self.assertEqual(restored.settings[key], '0')
        finally:
            restored.shutdown()
        self.validate()
        session = self.launch()
        for name in controls.values():
            self.assertFalse(self.window.findChild(QObject, name).property('enabled'))
        snapshot = session.contract.config_file.read_bytes()
        env = supervisor.runtime_environment(self.root / 'supervisor', config_file=session.contract.config_file)
        for key, variable in config.GPU_DIAGNOSTIC_ENV.items():
            self.assertEqual(env[variable], '0')
            self.backend.setSetting(key, 'auto')
        self.assertEqual(session.contract.config_file.read_bytes(), snapshot)
        self.finish()
        next_session = self.launch()
        env = supervisor.runtime_environment(self.root / 'supervisor', config_file=next_session.contract.config_file)
        for key, variable in config.GPU_DIAGNOSTIC_ENV.items():
            self.assertNotIn(variable, env)
            self.assertNotIn(key, config.load_knack2_ini(next_session.contract.config_file))
        self.finish()

    def test_gpu_diagnostic_labels_follow_launcher_language(self):
        from port.launcher.knack2.ui.i18n import translate
        self.validate()
        labels = {'en-US': 'Default', 'es-419': 'Predeterminado', 'fr-FR': 'Par défaut'}
        self.window.setProperty('page', 1)
        self.window.setProperty('settingsTab', 1)
        for locale, expected in labels.items():
            self.backend.setLanguage(locale)
            self.pump()
            self.assertEqual(self.window.findChild(QObject, 'gpuDiagnosticBp06').property('currentText'),
                             expected)
            for source in ('Diagnóstico da GPU', 'Cache binário de pipelines',
                           'Reconvergência de shaders (BP06)', 'Compilação compute em segundo plano'):
                self.assertNotEqual(translate(locale, source), source)
            for profile in self.backend.gpuPresets:
                self.assertNotIn(profile['label'], ('Padrão do port', 'AMD: falha ao iniciar',
                                                    'Inicialização conservadora'))

    def test_p2_automatic_4099_and_fresh_out_each_launch(self):
        self.validate()
        self.backend.setPlayers(2)
        self.assertTrue(self.backend.canPlay)
        first = self.launch().contract
        self.assertEqual(first.second_user, 4099)
        command = first.build_command()
        self.assertEqual(command[command.index('--local-users') + 1], '2')
        self.assertEqual(command[command.index('--p2-user-id') + 1], '4099')
        self.assertEqual(command[command.index('--timeout') + 1], '0')
        self.assertFalse(first.out_dir.exists())  # INI/logs do not poison P2's fresh output.
        self.finish()
        first.out_dir.mkdir()
        sentinel = first.out_dir / 'save.sentinel'
        sentinel.write_bytes(b'preserve')
        second = self.launch().contract
        self.assertEqual(second.second_user, 4099)
        self.assertEqual(first.user_data, second.user_data)
        self.assertEqual(first.user_data, self.root / 'data/profiles/shared/user')
        self.assertNotEqual(first.out_dir, second.out_dir)
        self.assertEqual(sentinel.read_bytes(), b'preserve')
        self.assertFalse(second.out_dir.exists())
        self.finish()

    def test_european_validation_updates_identity_languages_and_fallback(self):
        languages = ['fr-FR', 'pt-PT', 'en-GB', 'es-ES', 'de-DE', 'it-IT', 'nl-NL',
                     'ru-RU', 'fi-FI', 'sv-SE', 'da-DK', 'nb-NO', 'pl-PL', 'ar-SA']
        self.backend._validate_dump = lambda path: {
            'app_title': 'Synthetic European Knack II', 'title_id': 'CUSA07670',
            'version': '01.01', 'languages': languages}
        self.validate()
        self.assertEqual(self.backend.installationId, 'CUSA07670')
        self.assertEqual(self.backend.installationVersion, '01.01')
        self.assertEqual(self.backend.uiLanguage, 'pt-PT')
        self.assertEqual([entry['code'] for entry in self.backend.languageOptions], languages)
        self.backend.setLanguage('en-GB')
        self.pump()
        self.assertEqual(self.window.findChild(QObject, 'playButton').property('text'), 'Play')
        self.backend.setLanguage('de-DE')
        self.pump()
        self.assertEqual(self.window.findChild(QObject, 'playButton').property('text'), 'Play')
        session = self.launch().contract
        self.assertEqual(session.language, 'de-DE')
        self.assertEqual(session.user_data, self.root / 'data/profiles/shared/user')
        self.finish()
        self.backend.selectFolder(str(self.root / 'another-dump'))
        self.assertEqual(self.backend.installationId, '')
        self.assertFalse(self.backend.canPlay)
        self.assertEqual(self.sessions[-1].contract.language, 'de-DE')

    def test_region_switch_replaces_language_choices_and_clears_validation(self):
        from port.launcher.knack2.languages import REGION_LANGUAGES
        self.assertEqual(self.backend.languageOptions,[])
        combo=self.window.findChild(QObject,'languageSelect')
        self.assertFalse(combo.property('enabled'))
        for title in ('CUSA07670','CUSA08014','CUSA07670'):
            codes=list(REGION_LANGUAGES[title])
            self.backend._validate_dump=lambda path,t=title,c=codes: {
                'app_title':'Synthetic Knack II','title_id':t,'version':'01.01','languages':c}
            self.validate()
            self.assertEqual(self.backend.installationId,title)
            self.assertEqual([entry['code'] for entry in self.backend.languageOptions],codes)
            for code in codes:
                self.backend.setLanguage(code)
                self.assertEqual(self.backend.uiLanguage,code)
            self.backend.selectFolder(str(self.root/'changed-dump'))
            self.assertEqual(self.backend.languageOptions,[])
            self.assertFalse(self.backend.canPlay)
        self.backend._validate_dump=lambda path: (_ for _ in ()).throw(ValueError('bad fixture'))
        self.backend.validateFolder()
        self.until(lambda:not self.backend.validating)
        self.assertEqual(self.backend.languageOptions,[])
        self.assertFalse(self.backend.canPlay)

    def test_language_selector_and_audio_profile_reach_official_command(self):
        self.validate()
        self.window.setProperty('page', 1)
        self.backend.setPlayers(2)
        self.pump()
        combo = self.window.findChild(QObject, 'languageSelect')
        self.assertEqual(combo.property('count'), 4)
        expected = [('pt-BR', 'Jogar'), ('en-US', 'Play'), ('es-419', 'Jugar'), ('fr-FR', 'Jouer')]
        outputs = []
        for index, (code, play_label) in enumerate(expected):
            combo.setProperty('currentIndex', index)
            combo.activated.emit(index)
            self.pump()
            self.assertEqual(self.backend.uiLanguage, code)
            self.assertEqual(self.window.findChild(QObject, 'playButton').property('text'), play_label)
            session = self.launch().contract
            command = session.build_command()
            self.assertEqual(Path(command[1]), ROOT / 'run.py')
            self.assertEqual(command[command.index('--language') + 1], code)
            self.assertEqual(command[command.index('--p2-user-id') + 1], '4099')
            self.assertEqual(command[command.index('--timeout') + 1], '0')
            audio = config.load_knack2_ini(session.config_file)
            for name in ('ngs2_atrac9_hle', 'ngs2_render_mix', 'ngs2_audio_diag', 'ngs2_b2f2c_zero_matrix_bypass'):
                self.assertEqual(audio[name], '1')
            self.assertEqual(audio['ngs2_b2f2a_stable_cohort'], '0')
            self.assertFalse(session.out_dir.exists())
            outputs.append(session.out_dir)
            self.finish()
        self.assertEqual(len(set(outputs)), 4)
        self.backend.saveSettings()
        restored = RealBackend(data_dir=self.backend.workspace)
        try:
            self.assertEqual(restored.uiLanguage, 'fr-FR')
            self.assertEqual(restored.players, 1)
        finally:
            restored.shutdown()

    def test_validated_dump_restricts_locale_and_rejects_missing_assets(self):
        result = {'app_title': 'Synthetic Knack II', 'title_id': 'CUSA08014', 'version': '01.01', 'languages': ['en-US']}
        self.backend._validate_dump = lambda path: result
        self.validate()
        self.assertEqual(self.backend.uiLanguage, 'en-US')
        self.assertEqual([entry['code'] for entry in self.backend.languageOptions], ['en-US'])
        self.backend.setLanguage('pt-BR')
        self.assertEqual(self.backend.uiLanguage, 'en-US')
        self.assertIn('unavailable', self.backend.notification)
        result['languages'] = []
        self.backend.selectFolder(str(self.root / 'other'))
        self.backend.validateFolder()
        self.until(lambda: not self.backend.validating)
        self.assertFalse(self.backend.canPlay)
        self.assertEqual(self.backend.validationState, 'invalid')

    def test_double_click_and_immediate_stop_stay_reserved_and_responsive(self):
        self.validate()
        gate = threading.Event()
        original = self.backend._session_factory
        def factory(*args, **kwargs):
            session = original(*args, **kwargs)
            session.start_gate = gate
            return session
        self.backend._session_factory = factory
        self.backend.play()
        self.backend.play()
        self.assertEqual(len(self.sessions), 1)
        self.backend.stop()
        ticks = []
        QTimer.singleShot(10, lambda: ticks.append(True))
        self.pump()
        self.assertEqual(self.backend.state, 'STOPPING')
        self.assertTrue(ticks)
        gate.set()
        self.until(lambda: self.backend.state == 'FINISHED')
        self.assertTrue(self.sessions[0].closed)

    def test_close_defers_for_stop_and_failed_stop_keeps_ownership_for_retry(self):
        self.validate()
        session = self.launch()
        session.fail_stop = True
        self.assertFalse(self.backend.requestClose())
        # Worker completion precedes consumption of its result by the Qt timer.
        self.until(lambda: not self.backend._working() and
                   'controlled stop failed' in self.backend.notification)
        self.assertEqual(self.backend.state, 'STOPPING')
        self.assertFalse(self.backend.canPlay)
        self.assertTrue(self.backend.canStop)
        self.assertFalse(session.closed)
        self.assertTrue(self.window.isVisible())
        self.assertIn('controlled stop failed', self.backend.notification)
        session.fail_stop = False
        gate = session.stop_gate = threading.Event()
        self.assertFalse(self.backend.requestClose())
        self.pump()
        self.assertTrue(self.window.isVisible())
        gate.set()
        self.until(lambda: not self.window.isVisible())
        self.assertTrue(session.closed)

    def test_saved_settings_restore_but_never_restore_p2_or_trust_dump(self):
        self.validate()
        self.backend.setPlayers(2)
        self.backend.setSetting('vsync', False)
        self.backend.saveSettings()
        restored = RealBackend(data_dir=self.backend.workspace)
        try:
            self.assertEqual(restored.players, 1)
            self.assertFalse(hasattr(restored, 'setP2UserId'))
            self.assertEqual(restored.settings['vsync'], '0')
            self.assertEqual(restored.gameDir, self.backend.gameDir)
            self.assertFalse(restored.canPlay)
        finally:
            restored.shutdown()

    def test_data_overlap_fails_before_writes(self):
        self.validate()
        self.assertTrue((self.backend.workspace / 'launcher_settings.json').is_file())
        self.backend.workspace = Path(self.backend.gameDir)
        self.backend.play()
        self.assertEqual(self.sessions, [])
        self.assertFalse(self.backend.workspace.exists())
        self.assertIn('sobrepõe', self.backend.notification)

    def test_selected_folder_is_remembered_without_saving_other_edits(self):
        self.backend._settings['vsync'] = '0'  # Unsaved unrelated edit must stay unsaved.
        self.backend.selectFolder(str(self.root / 'dump'))
        restored = RealBackend(data_dir=self.backend.workspace)
        try:
            self.assertEqual(restored.gameDir, str(self.root / 'dump'))
            self.assertEqual(restored.settings['vsync'], '1')
            self.assertEqual(restored.players, 1)
            self.assertFalse(restored.canPlay)
            self.assertEqual(restored.validationState, 'empty')
        finally:
            restored.shutdown()

    def test_stale_callbacks_cannot_overwrite_new_session(self):
        self.validate()
        first = self.launch()
        self.finish()
        second = self.launch()
        first.error_message = 'stale failure'
        first.emit(RunnerState.FAILED)
        first.options['on_log_line']('stale log')
        self.pump()
        self.assertEqual(self.backend.state, 'RUNNING')
        self.assertNotIn('stale failure', self.backend.notification)
        self.assertIs(self.backend._runner, second)
        self.assertNotIn('stale log', self.backend.supervisor_model.lines)
        self.assertNotIn('stale log', self.backend._log_path.read_text())
        self.finish()

    def test_qml_player_widgets_and_actual_python_supervisor_error(self):
        self.validate()
        self.window.setProperty('page', 1)
        self.pump()
        players = self.window.findChild(QObject, 'playerCount')
        players.setProperty('currentIndex', 1)
        players.activated.emit(1)
        self.assertEqual(self.backend.players, 2)
        self.assertIsNone(self.window.findChild(QObject, 'p2UserId'))
        self.assertIsNone(self.window.findChild(QObject, 'sessionTimeout'))
        self.assertFalse(hasattr(self.backend, 'setTimeoutSeconds'))
        self.assertTrue(self.backend.canPlay)
        self.guard.stop()  # Only this finite, source-controlled Python fixture may run.
        script = self.root / 'fake supervisor.py'
        script.write_text('import sys\nfrom pathlib import Path\n'
                          'out=Path(sys.argv[sys.argv.index("--out")+1]); session=out/"session-123"\n'
                          'session.mkdir(parents=True)\n'
                          'for name in ("00-eboot.bin.log", "01-game.elf.log"):\n'
                          ' log=session/name; log.write_text("guest from "+name+"\\n")\n'
                          ' print("Starting "+name[3:-4]+"; log: "+str(log), flush=True)\n'
                          'print("real supervisor error", flush=True); sys.exit(7)\n')
        def factory(game, **kwargs):
            return KnackRunner(game, run_py_path=script, **kwargs)
        self.backend._session_factory = factory
        self.backend.play()
        self.until(lambda: self.backend.state == 'FAILED' and not self.backend._working())
        self.assertEqual(self.backend._runner.exit_code, 7)
        self.assertIn('7', self.backend.notification)
        self.assertIn('real supervisor error', self.backend.supervisor_model.lines)
        self.assertIn('real supervisor error', self.backend._log_path.read_text())
        self.until(lambda: all('guest from ' + name in self.backend.guest_model.lines
                              for name in ('00-eboot.bin.log', '01-game.elf.log')))
        self.assertIn('guest from 00-eboot.bin.log', self.backend.guest_model.lines)
        self.assertIn('guest from 01-game.elf.log', self.backend.guest_model.lines)
        self.assertEqual(self.backend._runner.session_info.current_executable, 'game.elf')
        self.assertEqual(len(self.backend._runner.session_info.known_log_paths), 2)
        self.assertTrue(self.backend._runner.session_info.session_dir.is_relative_to(self.backend._out))


class SupervisorConfigTests(unittest.TestCase):
    def test_config_validation_has_no_effects_and_cli_p2_contract_remains(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            out = root / 'fresh'
            with patch.object(supervisor, 'prepare_image') as prepare, contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    supervisor.main(['--game-dir', str(root), '--out', str(out), '--config', str(root / 'missing.ini')])
            prepare.assert_not_called()
            self.assertFalse(out.exists())
        self.assertEqual(P2_USER_ID, supervisor.p2_user_id('0004099'))

    def test_config_and_filtered_coop_environment_survive_actual_loadexec_loop(self):
        with tempfile.TemporaryDirectory(prefix='f3b-supervisor ') as temporary:
            root = Path(temporary)
            port = root / 'port'
            (port / 'out').mkdir(parents=True)
            (port / 'out/tlg-probe.exe').write_bytes(b'never executed')
            ini = root / 'settings.ini'
            config.update_knack2_ini(ini, {'vsync': '1', 'rcas_strength': 100})
            calls = []
            def child(command, env, log, seconds):
                calls.append((command, env))
                self.assertEqual(env['TLG_CONFIG'], str(ini))
                self.assertNotIn('TLG_GPU_USER_DIR', env)
                self.assertNotIn('ISS_TEST', env)
                self.assertEqual(env['TLG_LOCAL_P2_USER_ID'], '4099')
                if len(calls) == 1:
                    target = b'/app0/game.elf'
                    Path(env['KNACK2_EXEC_REQUEST']).write_bytes(b'K2EXEC01' + struct.pack('<I', len(target)) + target + struct.pack('<I', 0))
                    return 75
                return 0
            with patch.object(supervisor, 'PORT', port), patch.object(supervisor, 'validate_game'), \
                 patch.object(supervisor, 'load_profile', return_value=supervisor.load_profile()), \
                 patch.object(supervisor, 'prepare_image', side_effect=lambda g, p, name, out: out / name), \
                 patch.object(supervisor.subprocess, 'run'), patch.object(supervisor, 'run_child', side_effect=child), \
                 patch.dict(os.environ, {'TLG_CONFIG': 'foreign', 'TLG_LOCAL_P2_USER_ID': '99',
                                         'TLG_GPU_USER_DIR': 'foreign saves', 'ISS_TEST': '1'}), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(supervisor.main(['--game-dir', str(root / 'dump'), '--out', str(root / 'new'),
                                                 '--local-users', '2', '--p2-user-id', '4099', '--config', str(ini)]), 0)
            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[0][1]['TLG_CONFIG'], calls[1][1]['TLG_CONFIG'])

    @unittest.skipUnless(os.name == 'nt', 'Windows process-tree cancellation')
    def test_ctrl_break_reaps_unlimited_child_via_official_run_child(self):
        # Tiny Python parent imports the official run_child and installs the same
        # SIGBREAK handler as run.py. Its child records readiness, then sleeps.
        with tempfile.TemporaryDirectory(prefix='f3b-stop ') as temporary:
            root = Path(temporary)
            child = root / 'child.py'
            marker = root / 'child.pid'
            child.write_text('import os,time,pathlib; pathlib.Path(' + repr(str(marker)) + ').write_text(str(os.getpid())); time.sleep(30)\n')
            parent = root / 'parent.py'
            parent.write_text('import importlib.util,signal,sys,os\nfrom pathlib import Path\n'
                              f'spec=importlib.util.spec_from_file_location("official", {str(ROOT / "run.py")!r})\n'
                              'run=importlib.util.module_from_spec(spec); spec.loader.exec_module(run)\n'
                              'signal.signal(signal.SIGBREAK, signal.default_int_handler)\n'
                              f'run.run_child([sys.executable, {str(child)!r}], os.environ.copy(), Path({str(root / "child.log")!r}), None)\n')
            runner = KnackRunner(root, run_py_path=parent, out_dir=root / 'out')
            try:
                runner.start()
                deadline = time.monotonic() + 5
                while not marker.exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertTrue(marker.exists())
                child_pid = int(marker.read_text())
                self.assertTrue(runner.stop(grace_period=2))
                # Query only; no broad process kills or unrelated PID ownership.
                import ctypes
                kernel = ctypes.WinDLL('kernel32', use_last_error=True)
                kernel.OpenProcess.restype = ctypes.c_void_p
                handle = kernel.OpenProcess(0x100000, False, child_pid)
                if handle:
                    try:
                        self.assertEqual(kernel.WaitForSingleObject(ctypes.c_void_p(handle), 1000), 0)
                    finally:
                        kernel.CloseHandle(ctypes.c_void_p(handle))
            finally:
                runner.close()


if __name__ == '__main__':
    unittest.main()
