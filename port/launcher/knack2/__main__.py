# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the isolated Knack II launcher; --demo preserves the F3A demonstration."""
from .ui.app import main

if __name__ == '__main__':
    raise SystemExit(main())
