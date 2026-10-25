#!/usr/bin/env python3
"""Run Knack II with the two-frame geometry/visibility recorder enabled.

Pass the normal run.py arguments, including --capture-dir. During gameplay, create
frame_record.request in that directory to record two consecutive command streams.
Create capture.request at the same time to save the corresponding presented frames.
"""

import importlib.util
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("knack2_runner", ROOT / "run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
base_environment = runner.runtime_environment


def diagnostic_environment(*args, **kwargs):
    env = base_environment(*args, **kwargs)
    capture_dir = kwargs.get("capture_dir")
    if capture_dir is None:
        raise ValueError("geometry capture requires --capture-dir")
    capture_dir = Path(capture_dir).resolve()
    env["TLG_FRAME_RECORD_TRIGGER"] = str(capture_dir / "frame_record.request")
    env["TLG_FRAME_RECORD_VERIFY_UPLOAD"] = "1"
    env["TLG_FRAME_DUMP_COUNT"] = "2"
    return env


runner.runtime_environment = diagnostic_environment
sys.exit(runner.main())
