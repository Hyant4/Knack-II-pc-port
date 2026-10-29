"""Inspect existing Knack frame census files; never start the game or modify inputs."""
import argparse
import json
import math
from pathlib import Path
import re


BUFFER = re.compile(
    r'buffer stage (\w+) slot (\d+) at (\w+) size (\d+):\n'
    r'((?:[ \t]*\[\s*\d+\][^\n]*(?:\n|$))+)')
TEMPORAL = '000000005f9b889c'


def halton(index, base):
    value, weight = 0.0, 1.0
    while index:
        weight /= base
        value += weight * (index % base)
        index //= base
    return value


def constants(match):
    return [float(value) for line in match[5].splitlines()
            for value in line.split(']', 1)[1].split()]


def inspect_capture(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    blocks = re.split(r'\n(?=#\d+ )', text)
    temporal = next((block for block in blocks
                     if f'cs {TEMPORAL}' in block.partition('\n')[0]), None)
    if temporal is None:
        raise ValueError(f'{path.name}: native temporal dispatch not found')
    buffer = next((match for match in BUFFER.finditer(temporal)
                   if match[1] == TEMPORAL and match[2] == '0' and match[4] == '32'), None)
    if buffer is None or len(values := constants(buffer)) != 8:
        raise ValueError(f'{path.name}: complete temporal constants not found')
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f'{path.name}: non-finite temporal constants')
    temporal_jitter = values[:2]
    # Native US/EU 01.01 code uses frame_counter % 17 and a separate amplitude.
    # This correlation assumes amplitude 1; an empty list does not prove bad input.
    phase_candidates = [phase for phase in range(17)
                        if max(abs(halton(phase, base) - .5 - temporal_jitter[i])
                               for i, base in enumerate((2, 3))) < 2e-5]
    temporal_images = re.findall(r'^  (samples|writes)\s+\S+ (\S+) (\d+)x(\d+)', temporal, re.M)
    if len(temporal_images) != 5:
        raise ValueError(f'{path.name}: expected three sampled images and two destinations')
    if [image[0] for image in temporal_images] != ['samples'] * 3 + ['writes'] * 2:
        raise ValueError(f'{path.name}: unexpected temporal image order')
    width, height = map(int, temporal_images[0][2:])
    if not width or not height:
        raise ValueError(f'{path.name}: invalid temporal dimensions')
    cameras = []
    for block in blocks:
        for match in BUFFER.finditer(block):
            if match[4] != '432':
                continue
            data = constants(match)
            if len(data) != 108 or not all(map(math.isfinite, data)) or abs(data[43] + 1) > 1e-5:
                continue
            w, h = data[88:90]
            if not (64 <= w <= 8192 and 64 <= h <= 8192):
                continue
            jitter = [-data[40] * w * .5, -data[41] * h * .5]
            # Compare projection offsets normalized to the temporal target size.
            scaled = [jitter[0] * width / w, jitter[1] * height / h]
            error = max(abs(scaled[i] - temporal_jitter[i]) for i in (0, 1))
            camera = dict(stage=match[1], size=[w, h], jitter=jitter,
                          normalized_error=error, matches=error < 2e-5)
            if camera not in cameras:
                cameras.append(camera)
    if not cameras:
        raise ValueError(f'{path.name}: complete perspective camera constants not found')
    return dict(capture=path.name, temporal_size=[width, height],
                temporal_jitter=temporal_jitter, temporal_images=temporal_images,
                halton_phases_at_unit_amplitude=phase_candidates,
                cameras=cameras)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path, required=True)
    args = parser.parse_args()
    files = sorted(args.capture_dir.glob('frame_*.txt'))
    if not files:
        parser.error('No frame_*.txt census files found')
    try:
        captures = [inspect_capture(path) for path in files]
    except (OSError, ValueError) as error:
        parser.error(str(error))
    # Discovery report only: matching constants do not prove coverage of all draws.
    print(json.dumps(dict(captures=captures), indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
