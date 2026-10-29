#!/usr/bin/env python3
"""Summarize KNACK-VIS screen-region query results."""

import argparse
import collections
import re
import sys


FRAME = re.compile(r"KNACK-VIS: Results collected frame=(\d+) queries=(\d+)")
DRAW = re.compile(
    r"KNACK-VIS: draw=(\d+) pipe=([0-9a-f]+) vs=([0-9a-f]+) gs=([0-9a-f]+) "
    r"ps=([0-9a-f]+) prim=(\d+) rts=(\d+) idx=(\d+) inst=(\d+) samples=(\d+)"
)
STATE = re.compile(
    r"KNACK-VIS: (Armed|Trigger detected|Capture started|Matching draws found|"
    r"Queries submitted|Results collected|Capture completed|Capture failed)\b(.*)",
    re.IGNORECASE,
)


class ReportError(ValueError):
    pass


def analyze(path):
    frames = collections.OrderedDict()
    expected = {}
    states = []
    current = None
    with open(path, encoding="utf-8", errors="replace") as source:
        for line in source:
            if state := STATE.search(line):
                item = (state.group(1).lower(), state.group(2).strip())
                if item[0] == "capture started":
                    armed = next((entry for entry in reversed(states) if entry[0] == "armed"), None)
                    trigger = next(
                        (entry for entry in reversed(states) if entry[0] == "trigger detected"),
                        None,
                    )
                    states = [entry for entry in (armed, trigger) if entry is not None] + [item]
                    frames.clear()
                    expected.clear()
                    current = None
                else:
                    states.append(item)
            if match := FRAME.search(line):
                current = int(match.group(1))
                frames[current] = []
                expected[current] = int(match.group(2))
                continue
            if current is None or not (match := DRAW.search(line)):
                continue
            values = match.groups()
            frames[current].append(
                {
                    "draw": int(values[0]),
                    "pipe": values[1].lower(),
                    "vs": values[2].lower(),
                    "gs": values[3].lower(),
                    "ps": values[4].lower(),
                    "prim": int(values[5]),
                    "rts": int(values[6]),
                    "indices": int(values[7]),
                    "instances": int(values[8]),
                    "samples": int(values[9]),
                }
            )

    failures = [detail for name, detail in states if name == "capture failed"]
    if failures:
        raise ReportError("runtime reported Capture failed " + failures[-1])
    if not frames:
        names = ", ".join(name for name, _ in states) or "none"
        raise ReportError(f"no KNACK-VIS result frames; states={names}")

    output = []
    groups = collections.Counter()
    for frame, draws in frames.items():
        if len(draws) != expected[frame]:
            raise ReportError(
                f"frame {frame} expected {expected[frame]} query results, got {len(draws)}"
            )
        visible = [draw for draw in draws if draw["samples"]]
        output.append(f"frame={frame} queried={len(draws)} visible={len(visible)}")
        for draw in visible:
            output.append(
                "  draw={draw} pipe={pipe} vs={vs} gs={gs} ps={ps} "
                "prim={prim} rts={rts} idx={indices} inst={instances} samples={samples}".format(
                    **draw
                )
            )
            groups[(draw["pipe"], draw["vs"], draw["gs"], draw["ps"])] += 1

    output.append("visible pipeline groups:")
    for (pipe, vs, gs, ps), count in groups.most_common():
        output.append(f"  count={count} pipe={pipe} vs={vs} gs={gs} ps={ps}")
    if not groups:
        output.append("  none")
    return "\n".join(output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    args = parser.parse_args(argv)
    try:
        print(analyze(args.log))
    except (OSError, ReportError) as error:
        print(f"ERROR KNACK-VIS: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
