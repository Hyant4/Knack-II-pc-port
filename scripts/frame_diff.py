#!/usr/bin/env python3
"""Compares two frames written by TLG_FRAME_RECORD_TRIGGER (frame_record_0.txt, frame_record_1.txt).

In a static scene two consecutive frames should run the same commands with the same inputs;
what differs is where flickering effects come from. The command lists are aligned by queue,
kind and shaders (with permutations); for the commands present in both frames the inputs are
compared: indirect arguments, counts, buffer contents (g = the GPU's copy, c = guest memory),
textures, render targets and state. Changes shared by many commands (camera constants, TAA
history ping-pong) are listed once and weigh little. Groups of commands with the same shaders
come out most suspicious first.

usage: scripts/frame_diff.py frame_record_0.txt frame_record_1.txt [--top N] [--examples N]
"""
import argparse
import collections
import difflib
import re
import sys

STAGES = ("vs", "ps", "gs", "hs", "ds", "cs")
STATE_KEYS = {"vp", "scis", "dc", "st", "sref", "drc", "pc", "bc", "cm", "csm", "cc", "prim", "clr"}
IGNORED_KEYS = {"gpu"}  # notes of the recorder, not inputs
BUFFER_KEY = re.compile(r"^(?:(vs|ps|gs|hs|ds|cs)\.b\d+w?|vb\d+|ib)$")
TEXTURE_KEY = re.compile(r"^(vs|ps|gs|hs|ds|cs)\.t\d+w?$")
SAMPLER_KEY = re.compile(r"^(vs|ps|gs|hs|ds|cs)\.s\d+$")
TARGET_KEY = re.compile(r"^(rt\d+|z)$")

# Weight of a difference, by class (higher: more likely the cause of a flicker).
SCORES = {
    "missing": 100,       # the command runs in one frame only
    "indirect": 90,       # GPU-generated arguments (counts of particles, culled draws)
    "count": 80,          # vertex/instance counts, dispatch sizes
    "state": 50,          # blend/depth/stencil/viewport in a static scene
    "buffer_gpu": 60,     # contents the GPU wrote (simulation, culling results)
    "buffer_cpu": 40,     # contents the game wrote
    "buffer_addr": 35,    # another buffer bound
    "texture": 30,
    "target": 30,
    "sampler": 25,
    "user_data": 10,      # user data pointers (usually follow buffer changes)
    "memory_op": 5,       # DMA/fence/write packets (fence values change every frame)
}
COMMON_FACTOR = 0.1      # changes shared by many commands


def parse(path):
    commands = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.startswith("#") or line.startswith("# "):
                continue
            tokens = line.split(" ")
            fields = {}
            for token in tokens[1:]:
                key, sep, value = token.partition("=")
                if sep:
                    fields[key] = value
                elif token:
                    fields[token] = ""
            commands.append({"idx": int(tokens[0][1:]), "fields": fields, "line": line})
    return commands


def signature(cmd):
    f = cmd["fields"]
    shaders = tuple(f"{s}={f[s]}" for s in STAGES if s in f)
    label = (f.get("label", ""),) if f.get("kind") == "marker" else ()
    return (f.get("q", ""), f.get("kind", ""), f.get("op", "")) + shaders + label


def describe(sig):
    q, kind, op, *rest = sig
    return f"{q} {kind} ({op}) " + " ".join(rest)


def classify(key, old, new):
    """Class of a difference and the token used to find changes shared by many commands."""
    if key in ("args", "count", "args.cpu", "count.cpu", "max"):
        return "indirect", None
    if key in ("n", "inst", "groups", "ioff"):
        return "count", None
    if BUFFER_KEY.match(key):
        old_at, _, old_hash = old.partition(":")
        new_at, _, new_hash = new.partition(":")
        if old_at != new_at:
            return "buffer_addr", ("buffer_addr", key, old_at, new_at)
        kind = "buffer_gpu" if new_hash.startswith("g") or old_hash.startswith("g") else "buffer_cpu"
        return kind, ("buffer", new_at.split("+")[0])
    if key.endswith(".ud"):
        return "user_data", ("ud", key, old, new)
    if TEXTURE_KEY.match(key):
        return "texture", ("texture", old.split(":")[0], new.split(":")[0])
    if TARGET_KEY.match(key):
        return "target", ("target", old.split(":")[0], new.split(":")[0])
    if SAMPLER_KEY.match(key):
        return "sampler", ("sampler", old, new)
    if key in STATE_KEYS or re.match(r"^bl\d+$", key):
        return "state", ("state", key, old, new)
    return "memory_op", ("memory_op", key)


def diff_fields(a, b):
    keys = (set(a) | set(b)) - IGNORED_KEYS - {"q", "kind", "op", "label"} - set(STAGES)
    out = []
    for key in sorted(keys):
        old, new = a.get(key, "<none>"), b.get(key, "<none>")
        if old != new:
            out.append((key, old, new))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("frame0")
    parser.add_argument("frame1")
    parser.add_argument("--top", type=int, default=40, help="groups to print (default 40)")
    parser.add_argument("--examples", type=int, default=3, help="commands shown per group")
    parser.add_argument("--contains", help="compare only command lines containing this text")
    parser.add_argument("--common", type=float, default=0.25,
                        help="a change seen in this share of the compared commands (or in 16+ "
                             "of them) counts as frame-global (default 0.25)")
    args = parser.parse_args()

    frames = [parse(args.frame0), parse(args.frame1)]
    if args.contains:
        frames = [[cmd for cmd in frame if args.contains in cmd["line"]] for frame in frames]

    verified, mismatched, incomplete = 0, [], []
    for frame_index, frame in enumerate(frames):
        for cmd in frame:
            fields = cmd["fields"]
            for key, cpu in fields.items():
                if key.endswith(".cw"):
                    base = key[:-3]
                    gpu = fields.get(base + ".gw")
                    if gpu is None:
                        incomplete.append((frame_index, cmd["idx"], base))
                    else:
                        verified += 1
                        if cpu != gpu:
                            mismatched.append((frame_index, cmd["idx"], base))
                elif key.endswith(".gpu"):
                    base = key[:-4]
                    guest = fields.get(base, "").rpartition(":")[2]
                    if not guest.startswith("c") or not cpu.startswith("g"):
                        incomplete.append((frame_index, cmd["idx"], base))
                    else:
                        verified += 1
                        if guest[1:] != cpu[1:]:
                            mismatched.append((frame_index, cmd["idx"], base))

    sigs = [[signature(c) for c in frame] for frame in frames]
    matcher = difflib.SequenceMatcher(None, sigs[0], sigs[1], autojunk=False)
    pairs, missing = [], []
    for tag, i0, i1, j0, j1 in matcher.get_opcodes():
        if tag == "equal":
            pairs.extend(zip(range(i0, i1), range(j0, j1)))
            continue
        # Same signature in a replaced block: pair them in order (inputs changed in place).
        left = collections.defaultdict(list)
        for i in range(i0, i1):
            left[sigs[0][i]].append(i)
        right_unmatched = []
        for j in range(j0, j1):
            if left[sigs[1][j]]:
                pairs.append((left[sigs[1][j]].pop(0), j))
            else:
                right_unmatched.append(j)
        for i_list in left.values():
            missing.extend((0, i) for i in i_list)
        missing.extend((1, j) for j in right_unmatched)

    # Differences of the matched commands, and how many commands share each change.
    diffs = []
    shared = collections.Counter()
    for i, j in pairs:
        d = diff_fields(frames[0][i]["fields"], frames[1][j]["fields"])
        classified = [(key, old, new, *classify(key, old, new)) for key, old, new in d]
        diffs.append((i, j, classified))
        for token in {c[4] for c in classified if c[4] is not None}:
            shared[token] += 1
    threshold = max(16, args.common * max(1, len(pairs)))
    common = {token for token, n in shared.items() if n >= threshold}

    groups = {}
    def group(sig):
        return groups.setdefault(sig, {"score": 0, "changed": 0, "examples": [], "keys":
                                       collections.Counter(), "first": {}})
    for i, j, classified in diffs:
        if not classified:
            continue
        score = 0
        for key, old, new, cls, token in classified:
            weight = SCORES[cls] * (COMMON_FACTOR if token in common else 1.0)
            score = max(score, weight)
        g = group(sigs[0][i])
        g["score"] = max(g["score"], score)
        g["changed"] += 1
        for key, old, new, cls, token in classified:
            label = f"{cls}{' (frame-global)' if token in common else ''} {key}"
            g["keys"][label] += 1
            g["first"].setdefault(label, (old, new))
        if len(g["examples"]) < args.examples:
            g["examples"].append(f"#{frames[0][i]['idx']} -> #{frames[1][j]['idx']}")
    for frame, k in missing:
        g = group(sigs[frame][k])
        g["score"] = SCORES["missing"]
        g["changed"] += 1
        label = f"missing: only in frame {frame}"
        g["keys"][label] += 1
        g["first"].setdefault(label, ("", ""))
        if len(g["examples"]) < args.examples:
            g["examples"].append(f"frame {frame} #{frames[frame][k]['idx']}")

    total = collections.Counter(sigs[0][i] for i, _ in pairs)
    print(f"frame 0: {len(frames[0])} commands, frame 1: {len(frames[1])} commands, "
          f"{len(pairs)} matched, {len(missing)} in one frame only")
    if args.contains:
        print(f"filter: {args.contains}")
    if verified or incomplete:
        print(f"upload verification: {verified} compared, {len(mismatched)} mismatches, "
              f"{len(incomplete)} incomplete")
        for frame_index, command, key in mismatched[: args.examples]:
            print(f"  mismatch frame {frame_index} #{command} {key}")
        for frame_index, command, key in incomplete[: args.examples]:
            print(f"  incomplete frame {frame_index} #{command} {key}")

    sample_paths = collections.Counter()
    for frame in frames:
        for cmd in frame:
            fields = cmd["fields"]
            if "rs" in fields:
                attachments = tuple(sorted((key, value) for key, value in fields.items()
                                           if re.match(r"^(?:rt\d+s|zs)$", key)))
                sample_paths[(fields.get("rs"), fields.get("ds"), attachments)] += 1
    if sample_paths:
        print("sample paths:")
        for (raster, depth, attachments), count in sample_paths.most_common():
            detail = " ".join(f"{key}={value}" for key, value in attachments)
            print(f"  {count:5}x rs={raster} ds={depth} {detail}".rstrip())
    changed = sum(1 for _, _, c in diffs if c)
    print(f"{changed} matched commands have different inputs; {len(groups)} groups")
    if common:
        print("\nFrame-global changes (seen in many commands; weigh little):")
        for token in sorted(common, key=lambda t: -shared[t]):
            print(f"  {shared[token]:6} commands  {' '.join(str(x) for x in token)}")
    print()
    ranked = sorted(groups.items(), key=lambda kv: (-kv[1]["score"], -kv[1]["changed"]))
    for rank, (sig, g) in enumerate(ranked[: args.top], 1):
        of = total.get(sig, 0)
        print(f"[{rank}] score {g['score']:.0f}  {g['changed']} commands"
              f"{f' of {of} matched' if of else ''}: {describe(sig)}")
        for label, n in g["keys"].most_common():
            old, new = g["first"][label]
            change = f"  {old[:60]} -> {new[:60]}" if old or new else ""
            print(f"      {n:5}x {label}{change}")
        print(f"      e.g. {', '.join(g['examples'])}")
    if len(ranked) > args.top:
        print(f"\n... {len(ranked) - args.top} more groups (--top)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
