#!/usr/bin/env python3
"""Run Knack II with screen-region visual draw identification enabled.

Create visual.request containing x,y,width,height to override the armed ROI for one capture.
Plain text such as "visual" keeps the ROI passed on the command line.
"""

import argparse
import importlib.util
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("knack2_runner", ROOT / "run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
base_environment = runner.runtime_environment
options = None
last_trigger = None


def diagnostic_environment(*args, **kwargs):
    global last_trigger
    env = base_environment(*args, **kwargs)
    capture_dir = kwargs.get("capture_dir")
    if capture_dir is None:
        raise ValueError("visual capture requires --capture-dir")
    capture_dir = Path(capture_dir).resolve()
    last_trigger = capture_dir / "visual.request"
    env.update(
        TLG_KNACK_VIS_TRIGGER=str(last_trigger),
        TLG_KNACK_VIS_ROI=options.roi,
        TLG_KNACK_VIS_PRIM=str(options.primitive),
        TLG_KNACK_VIS_RTS=str(options.render_targets),
        TLG_KNACK_VIS_FRAMES=str(options.frames),
        TLG_KNACK_VIS_FIRST_DRAW=str(options.first_draw),
        TLG_KNACK_VIS_MAX_DRAWS=str(options.max_draws),
        TLG_FRAME_RECORD_TRIGGER=str(capture_dir / "frame_record.request"),
        TLG_FRAME_RECORD_VERIFY_UPLOAD="1",
        TLG_FRAME_DUMP_COUNT="2",
    )
    if options.pad_replay is not None:
        env["TLG_PAD_REPLAY"] = str(options.pad_replay.resolve())
    if options.small_sync:
        env["TLG_KNACK_SMALL_SYNC"] = "1"
    return env


def main(argv=None):
    global options
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--roi", required=True, help="render-target ROI: x,y,width,height")
    parser.add_argument("--primitive", type=int, default=4)
    parser.add_argument("--render-targets", type=int, default=5)
    parser.add_argument("--frames", type=int, default=1)
    parser.add_argument("--first-draw", type=int, default=0)
    parser.add_argument("--max-draws", type=int, default=1024)
    parser.add_argument("--pad-replay", type=Path)
    parser.add_argument("--small-sync", action="store_true")
    options, remaining = parser.parse_known_args(argv)
    if not re.fullmatch(r"-?\d+,-?\d+,\d+,\d+", options.roi):
        parser.error("--roi must be x,y,width,height")
    width, height = map(int, options.roi.split(",")[2:])
    if width <= 0 or height <= 0:
        parser.error("ROI width and height must be positive")
    if not 1 <= options.frames <= 4:
        parser.error("--frames must be 1..4")
    if options.first_draw < 0 or not 1 <= options.max_draws <= 2048:
        parser.error("draw range must be non-negative and at most 2048 draws")
    if options.pad_replay is not None and not options.pad_replay.is_file():
        parser.error("--pad-replay must name an existing replay file")

    runner.runtime_environment = diagnostic_environment
    result = runner.main(remaining)
    if last_trigger is not None and last_trigger.exists():
        print(
            f"KNACK-VIS: Capture failed reason=trigger_not_consumed path={last_trigger}",
            file=sys.stderr,
        )
    return result


if __name__ == "__main__":
    raise SystemExit(main())
