"""Isolated capture launcher. Run gameplay only when the user requests a session.

Uses the existing runner and its environment filter. Selects an isolated executable
without replacing shared binaries. Requires fresh output; existing saves are never used.
With --display-only, use publish_capture_triggers() when the scene is ready.
It publishes closed files, so Windows consumers can remove markers immediately.
Pass observe_stages=True when that mode is enabled. Default capture: 16 frames.
"""

import argparse
import importlib.util
import os
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def publish_capture_triggers(capture_dir, observe_stages=False):
    """Publish final-frame markers without replacement; the pair is not a transaction.

    Each hard link becomes visible atomically. A consumer can immediately delete
    it because no writer handle remains open. Temporary files stay on the same
    filesystem. On partial failure, already published markers are not rolled back.
    """
    directory = Path(capture_dir).resolve()
    if not directory.is_dir():
        raise ValueError("capture directory must already exist")
    names = (["gpu_phy03.request"] if observe_stages else []) + ["frame_sequence.request"]
    targets = [directory / name for name in names]
    if any(p.exists() for p in targets) or any((directory / name).exists() for name in
                                             ("frame_sequence", "stage_observations", "gpu_phy03")):
        raise ValueError("capture already requested or started; use a fresh session")
    pending = []
    try:
        for target in targets:
            temporary = directory / ("." + target.name + "." + uuid.uuid4().hex + ".pending")
            with temporary.open("xb") as handle:
                pending.append((temporary, target))
                handle.write(b"capture\n")
        for temporary, target in pending:
            os.link(temporary, target)  # fails if another publisher already created target
    finally:
        for temporary, _ in pending:
            temporary.unlink(missing_ok=True)
    return targets


def diagnostic_environment(base, frames, capture_dir):
    capture_dir = Path(capture_dir).resolve()
    result = dict(base)
    result.update(TLG_GPU_PHY03_TRIGGER=str(capture_dir / "gpu_phy03.request"),
                  TLG_GPU_PHY03_DIR=str(capture_dir / "gpu_phy03"), TLG_GPU_PHY03_FRAMES=str(frames))
    return result


def sequence_environment(base, frames, capture_dir, observe_stages=False, relic_trace=False, pm4_trace=False, submit_trace=False, producer_trace=False, list_trace=False):
    """Capture final game frames without enabling shader/alias lineage diagnostics."""
    capture_dir = Path(capture_dir).resolve()
    result = {k: v for k, v in base.items() if not k.startswith("TLG_GPU_PHY03_")
              and k not in {"TLG_FRAME_DUMP_TRIGGER", "TLG_FRAME_DUMP_COUNT", "TLG_RELIC_TRACE", "TLG_RELIC_PM4_TRACE", "TLG_RELIC_SUBMIT_TRACE", "TLG_RELIC_PRODUCER_TRACE", "TLG_RELIC_LIST_TRACE"}}
    result.update(TLG_FRAME_SEQUENCE_TRIGGER=str(capture_dir / "frame_sequence.request"),
                  TLG_FRAME_SEQUENCE_DIR=str(capture_dir / "frame_sequence"),
                  TLG_FRAME_SEQUENCE_COUNT=str(frames))
    if observe_stages:
        result.update(TLG_GPU_PHY03_TRIGGER=str(capture_dir / "gpu_phy03.request"),
                      TLG_GPU_PHY03_DIR=str(capture_dir / "stage_observations"),
                      TLG_GPU_PHY03_FRAMES=str(frames), TLG_GPU_PHY03_OBSERVE="1",
                      TLG_FRAME_SEQUENCE_ID=uuid.uuid4().hex)
    if relic_trace:
        if not observe_stages:
            raise ValueError("relic trace requires stage observations")
        result["TLG_RELIC_TRACE"] = "1"
    if pm4_trace:
        if not relic_trace:
            raise ValueError("PM4 trace requires relic trace")
        result["TLG_RELIC_PM4_TRACE"] = "1"
    if submit_trace:
        if not pm4_trace:
            raise ValueError("submission trace requires PM4 trace")
        result["TLG_RELIC_SUBMIT_TRACE"] = "1"
    if producer_trace:
        if not pm4_trace:
            raise ValueError("producer trace requires PM4 trace")
        result["TLG_RELIC_PRODUCER_TRACE"] = "1"
    if list_trace:
        if not producer_trace:
            raise ValueError("list trace requires producer trace")
        result["TLG_RELIC_LIST_TRACE"] = "1"
    return result


def validate_paths(out, capture_dir, game, probe):
    out, capture_dir, game, probe = (Path(p).resolve() for p in (out, capture_dir, game, probe))
    if out.exists():
        raise ValueError("--out must name a new directory; existing saves are not reused")
    if not capture_dir.is_relative_to(out):
        raise ValueError("--capture-dir must stay inside --out")
    for protected in (ROOT / "out", ROOT / "port/user", game):
        if out.is_relative_to(protected) or protected.is_relative_to(out):
            raise ValueError("--out overlaps game or normal user data")
    if not probe.is_file() or probe.name != "tlg-probe.exe":
        raise ValueError("--phy03-probe must name the reviewed isolated tlg-probe.exe")
    if not game.is_dir():
        raise ValueError("--game-dir must name the existing local dump")
    return out, capture_dir, game, probe


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phy03-probe", type=Path, required=True)
    parser.add_argument("--phy03-frames", type=int, choices=range(1, 17), default=16)
    parser.add_argument("--display-only", action="store_true", help="capture final game images in any scene; no shader lineage")
    parser.add_argument("--observe-stages", action="store_true", help="with --display-only: independent G-buffer/display observations, no proven pixel lineage")
    parser.add_argument("--relic-trace", action="store_true", help="bounded draw emission metadata; requires --observe-stages")
    parser.add_argument("--pm4-trace", action="store_true", help="bounded executed PM4 metadata; requires --relic-trace")
    parser.add_argument("--submit-trace", action="store_true", help="bounded submission buffer samples; requires --pm4-trace")
    parser.add_argument("--producer-trace", action="store_true", help="bounded GNM writer callers; requires --pm4-trace")
    parser.add_argument("--list-trace", action="store_true", help="bounded supported-guest render-list samples; requires --producer-trace")
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--capture-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=180, help="finite total session seconds")
    args = parser.parse_args(argv)
    if args.observe_stages and not args.display_only:
        parser.error("--observe-stages requires --display-only")
    if args.relic_trace and not args.observe_stages:
        parser.error("--relic-trace requires --observe-stages")
    if args.pm4_trace and not args.relic_trace:
        parser.error("--pm4-trace requires --relic-trace")
    if args.submit_trace and not args.pm4_trace:
        parser.error("--submit-trace requires --pm4-trace")
    if args.producer_trace and not args.pm4_trace:
        parser.error("--producer-trace requires --pm4-trace")
    if args.list_trace and not args.producer_trace:
        parser.error("--list-trace requires --producer-trace")
    if not 1 <= args.timeout <= 600:
        parser.error("--timeout must be 1..600 seconds")
    try:
        out, capture, game, probe = validate_paths(args.out, args.capture_dir, args.game_dir, args.phy03_probe)
    except ValueError as error:
        parser.error(str(error))
    spec = importlib.util.spec_from_file_location("gpu_phy03_runner", ROOT / "run.py")
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    original_environment, original_child = runner.runtime_environment, runner.run_child

    def environment(*positional, **kwargs):
        base = original_environment(*positional, **kwargs)
        if args.display_only:
            return sequence_environment(base, args.phy03_frames, capture, args.observe_stages, args.relic_trace, args.pm4_trace, args.submit_trace, args.producer_trace, args.list_trace)
        return diagnostic_environment(base, args.phy03_frames, capture)

    def child(command, env, log, timeout):
        command = list(command)
        expected = {
            (runner.PORT / "out/tlg-probe.exe").resolve(),
            (runner.PORT / "out/knack2-runtime.exe").resolve(),
            runner.runtime_path(runner.PORT).resolve(),
        }
        if Path(command[0]).resolve() not in expected:
            raise ValueError("unexpected runner executable")
        command[0] = str(probe)
        return original_child(command, env, log, timeout)

    runner.runtime_environment, runner.run_child = environment, child
    try:
        return runner.main(["--game-dir", str(game), "--out", str(out), "--capture-dir", str(capture),
                            "--timeout", str(args.timeout)])
    finally:
        runner.runtime_environment, runner.run_child = original_environment, original_child


if __name__ == "__main__":
    raise SystemExit(main())
