# SPDX-License-Identifier: GPL-2.0-or-later
"""Lazy Qt entrypoint with a clear dependency error and finite smoke mode."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import sys


def create_window(backend=None):
    from PySide6.QtCore import QUrl
    from PySide6.QtGui import QFont, QFontDatabase, QGuiApplication, QIcon
    from PySide6.QtQml import QQmlApplicationEngine
    from .backend import Backend
    from ..identity import set_launcher_app_id, identity_assets

    set_launcher_app_id()

    # Windows' offscreen platform does not enumerate system fonts. Register the
    # installed files explicitly; no font is copied or downloaded.
    if os.name == 'nt':
        fonts = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts'
        for filename in ('segoeui.ttf', 'seguisb.ttf', 'consola.ttf'):
            path = fonts / filename
            if path.is_file():
                QFontDatabase.addApplicationFont(str(path))
        QGuiApplication.instance().setFont(QFont('Segoe UI', 10))
    backend = backend if backend is not None else Backend()
    icon = identity_assets() / 'Knack.ico'
    QGuiApplication.instance().setWindowIcon(QIcon(str(icon)) if icon.is_file() else QIcon())
    engine = QQmlApplicationEngine()
    from .i18n import Translator
    translator = Translator(backend.uiLanguage)
    app = QGuiApplication.instance()
    app.installTranslator(translator)
    # Keep translator alive for the engine; translations update existing bindings.
    engine._knack_translator = translator
    def retranslate():
        translator.locale = backend.uiLanguage
        engine.retranslate()
    backend.uiLanguageChanged.connect(retranslate)
    engine.destroyed.connect(lambda: app.removeTranslator(translator))
    engine.setInitialProperties({'backend': backend})
    engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name('Main.qml'))))
    if not engine.rootObjects():
        backend.shutdown()
        raise RuntimeError('Não foi possível carregar a interface QML do Knack II.')
    return engine, engine.rootObjects()[0], backend


def main(argv=None):
    parser = argparse.ArgumentParser(description='Knack II — launcher F3B. Nenhum início automático.')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--demo', action='store_true', help='Demonstração F3A, sem processos reais.')
    mode.add_argument('--real', action='store_true', help='Supervisor oficial (padrão); requer clicar em Jogar.')
    parser.add_argument('--data-dir', type=Path, help='Dados persistentes; padrão Windows: %LOCALAPPDATA%/KnackII-PC.')
    parser.add_argument('--offscreen', action='store_true', help='Renderização Qt sem janela visível.')
    parser.add_argument('--quit-after-ms', type=int, default=0, help='Fechar após N ms, para smoke finito.')
    parser.add_argument('--screenshot', type=Path, help='Salvar captura PNG da interface antes de fechar.')
    args = parser.parse_args(argv)
    if args.quit_after_ms < 0:
        parser.error('--quit-after-ms deve ser >= 0')
    if args.screenshot and not args.quit_after_ms:
        parser.error('--screenshot requer --quit-after-ms')
    if args.offscreen:
        os.environ['QT_QPA_PLATFORM'] = 'offscreen'
        os.environ.setdefault('QT_QUICK_BACKEND', 'software')
    try:
        from PySide6.QtCore import QTimer
        from PySide6.QtGui import QGuiApplication
        from PySide6.QtQuickControls2 import QQuickStyle
    except ImportError:
        print('Qt/PySide6 indisponível. Instale PySide6-Essentials>=6.7 no ambiente Python do projeto. '
              'Nenhum jogo foi iniciado.', file=sys.stderr)
        return 2
    QQuickStyle.setStyle('Basic')
    app = QGuiApplication.instance() or QGuiApplication([sys.argv[0]])
    app.setApplicationName('Knack II PC — Demonstração' if args.demo else 'Knack II PC')
    app.setOrganizationName('Knack2-PC')
    app.setQuitOnLastWindowClosed(True)
    try:
        from .backend import Backend
        from .real import RealBackend
        backend = Backend() if args.demo else RealBackend(data_dir=args.data_dir)
        engine, window, backend = create_window(backend)
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 3
    app.aboutToQuit.connect(backend.shutdown)
    capture_failed = []

    def finish():
        if args.screenshot and not window.grabWindow().save(str(args.screenshot)):
            print('Falha ao salvar screenshot.', file=sys.stderr)
            capture_failed.append(True)
        window.close()

    if args.quit_after_ms:
        QTimer.singleShot(args.quit_after_ms, finish)
    try:
        code = app.exec()
    finally:
        backend.shutdown()
        del engine
    return 4 if capture_failed else code
