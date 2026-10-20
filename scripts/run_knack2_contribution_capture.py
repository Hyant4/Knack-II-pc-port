#!/usr/bin/env python3
"""Run Knack II with the short per-draw contribution probe enabled.

Pass normal run.py arguments and --capture-dir. During gameplay, create
contribution.request and capture.request in that directory at the same time.
The runtime records 16 frames of occlusion results and presented pixels.
"""

import importlib.util
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("knack2_runner", ROOT / "run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
base_environment = runner.runtime_environment
last_trigger = None


def diagnostic_environment(*args, **kwargs):
    global last_trigger
    env = base_environment(*args, **kwargs)
    capture_dir = kwargs.get("capture_dir")
    if capture_dir is None:
        raise ValueError("contribution capture requires --capture-dir")
    capture_dir = Path(capture_dir).resolve()
    last_trigger = capture_dir / "contribution.request"
    env["TLG_KNACK_CONTRIB_TRIGGER"] = str(last_trigger)
    env["TLG_FRAME_DUMP_COUNT"] = "16"
    return env


def main(argv=None):
    runner.runtime_environment = diagnostic_environment
    result = runner.main(argv)
    if last_trigger is not None and last_trigger.exists():
        print(
            f"KNACK-PX: Capture failed reason=trigger_not_consumed path={last_trigger}",
            file=sys.stderr,
        )
    return result


if __name__ == "__main__":
    raise SystemExit(main())
