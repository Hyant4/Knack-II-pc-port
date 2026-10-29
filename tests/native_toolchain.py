"""Select an installed host C compiler for synthetic native contract tests."""
import os
from pathlib import Path
import shutil


def c_compiler():
    if os.name == 'nt':
        root = Path(os.environ.get('MSYS2_ROOT') or
                    Path.home() / 'AppData/Local/knack2-toolchain/msys64')
        compiler = root / 'clang64/bin/clang.exe'
    else:
        selected = os.environ.get('CC')
        found = shutil.which(selected) if selected else (shutil.which('clang') or shutil.which('cc'))
        if not found:
            raise FileNotFoundError('An installed host C compiler is required for native tests.')
        compiler = Path(found)
    if not compiler.is_file():
        raise FileNotFoundError(f'Native test compiler not found: {compiler}')
    return compiler
