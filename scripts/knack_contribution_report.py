#!/usr/bin/env python3
"""Summarize KNACK-PX per-draw occlusion results from a game.elf log."""

import argparse
import collections
import re
import sys


FRAME = re.compile(r"KNACK-PX: (?:Results collected )?frame=(\d+) queries=(\d+)")
DRAW = re.compile(
    r"KNACK-PX: draw=(\d+) mode=([OR]) ps=([0-9a-fA-F]+) inst=(\d+) samples=(\d+)"
)
STATE = re.compile(
    r"KNACK-PX: (Armed|Trigger detected|Capture started|Matching draws found|"
    r"Queries submitted|Results collected|Capture completed|Capture failed)\b(.*)",
    re.IGNORECASE,
)
STATE_NAMES = {
    "armed": "Armed",
    "trigger detected": "Trigger detected",
    "capture started": "Capture started",
    "matching draws found": "Matching draws found",
    "queries submitted": "Queries submitted",
    "results collected": "Results collected",
    "capture completed": "Capture completed",
    "capture failed": "Capture failed",
}


class ReportError(ValueError):
    pass


def classify(original, relaxed):
    if original > 0:
        return "after_depth"
    if relaxed is None:
        return "zero_unclassified"
    if relaxed > 0:
        return "depth_stencil_cull"
    return "gs_clip_or_discard"


def analyze(path):
    frames = collections.OrderedDict()
    expected_queries = {}
    states = []
    current = None
    with open(path, encoding="utf-8", errors="replace") as source:
        for line in source:
            if state := STATE.search(line):
                item = (STATE_NAMES[state.group(1).lower()], state.group(2).strip())
                if item[0] == "Capture started":
                    # A probe may be rearmed after an explicit failed window. Report only the
                    # latest attempt, while retaining its Armed/Trigger context.
                    armed = next((entry for entry in reversed(states) if entry[0] == "Armed"), None)
                    trigger = next(
                        (entry for entry in reversed(states) if entry[0] == "Trigger detected"),
                        None,
                    )
                    states = [entry for entry in (armed, trigger) if entry is not None] + [item]
                    frames.clear()
                    expected_queries.clear()
                    current = None
                else:
                    states.append(item)
            if match := FRAME.search(line):
                current = int(match.group(1))
                frames[current] = {}
                expected_queries[current] = int(match.group(2))
                continue
            if current is None or not (match := DRAW.search(line)):
                continue
            draw, mode, ps, instances, samples = match.groups()
            key = (int(draw), ps.lower(), int(instances))
            frames[current].setdefault(key, {})[mode] = int(samples)

    state_names = [name for name, _ in states]
    failures = [detail for name, detail in states if name == "Capture failed"]
    if failures:
        raise ReportError("runtime reported Capture failed " + failures[-1])
    if not frames:
        state_text = ", ".join(state_names) if state_names else "none"
        if state_names == ["Armed"]:
            hint = "trigger was never detected on render thread"
        elif "Capture started" in state_names:
            hint = "capture started but query results were never collected"
        else:
            hint = "capture did not reach result collection"
        raise ReportError(f"no KNACK-PX result frames; states={state_text}; {hint}")

    relaxed = any("relaxed=1" in detail for name, detail in states if name == "Armed")
    total_draws = 0
    for frame, draws in frames.items():
        result_lines = sum(len(result) for result in draws.values())
        if result_lines != expected_queries[frame]:
            raise ReportError(
                f"frame {frame} expected {expected_queries[frame]} query results, got {result_lines}"
            )
        for key, result in draws.items():
            if "O" not in result or relaxed and "R" not in result:
                raise ReportError(f"frame {frame} draw {key[0]} has incomplete O/R result pair")
        total_draws += len(draws)
    if total_draws == 0:
        raise ReportError("KNACK-PX collected frames but found no matching draws")

    output = []
    totals = collections.defaultdict(collections.Counter)
    for frame, draws in frames.items():
        classes = collections.Counter()
        samples = collections.Counter()
        for (draw, ps, instances), result in sorted(draws.items()):
            original = result.get("O", 0)
            relaxed = result.get("R")
            kind = classify(original, relaxed)
            classes[kind] += 1
            samples["O"] += original
            samples["R"] += relaxed or 0
            totals[ps][kind] += 1
            totals[ps]["O"] += original
            totals[ps]["R"] += relaxed or 0
        output.append(
            f"frame={frame} draws={len(draws)} O={samples['O']} R={samples['R']} "
            + " ".join(f"{key}={value}" for key, value in sorted(classes.items()))
        )

    output.append("per shader:")
    for ps, values in sorted(totals.items()):
        output.append(
            f"  ps={ps} O={values['O']} R={values['R']} "
            f"after_depth={values['after_depth']} "
            f"depth_stencil_cull={values['depth_stencil_cull']} "
            f"gs_clip_or_discard={values['gs_clip_or_discard']}"
        )
    return "\n".join(output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    args = parser.parse_args(argv)

    try:
        print(analyze(args.log))
    except (OSError, ReportError) as error:
        print(f"ERROR KNACK-PX: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
