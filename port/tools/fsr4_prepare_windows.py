#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepare FSR4 v07 Windows assets without a GLSL round trip.

Keep the original postpass and add a bounds guard to the original pass-11 SPIR-V.
This preserves the shader body, descriptor ABI and signed integer dot products.
Requires spirv-dis, spirv-as and spirv-val on PATH; installs nothing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def guard_assembly(text):
    text = text.replace('\r\n', '\n')
    if any(op in text for op in ('OpControlBarrier', 'OpMemoryBarrier', 'OpGroupNonUniform',
                                 'OpTypePointer Workgroup', '%tlg_fsr4_guard_')):
        raise ValueError('Pass 11 has workgroup/subgroup dependencies or is already guarded')
    def require(pattern):
        match = re.search(pattern, text)
        if not match:
            raise ValueError('Unsupported pass-11 SPIR-V layout')
        return match[1]
    uint = require(r'(%\S+) = OpTypeInt 32 0\b')
    vector = require(r'(%\S+) = OpTypeVector ' + re.escape(uint) + r' 3\b')
    boolean = require(r'(%\S+) = OpTypeBool\b')
    gid = require(r'OpDecorate (%\S+) BuiltIn GlobalInvocationId\b')
    constants = dict(re.findall(r'(%\S+) = OpConstant ' + re.escape(uint) + r' (\d+)\b', text))
    vectors = {m[0]: m[1:] for m in re.findall(
        r'(%\S+) = OpConstantComposite ' + re.escape(vector) + r' (%\S+) (%\S+) (%\S+)', text)}
    bounds = set()
    for operand in re.findall(r'OpULessThan %\S+ %\S+ (%\S+)', text):
        if operand in vectors:
            ids = vectors[operand]
            values = tuple(int(constants.get(i, '-1')) for i in ids)
            if values[0] > 0 and values[1] > 0 and values[2] == 32:
                bounds.add(ids)
    if len(bounds) != 1:
        raise ValueError('Pass 11 must have one unambiguous input bounds signature')
    width, height, _ = next(iter(bounds))
    main = require(r'OpEntryPoint GLCompute (%\S+) "fsr4_model_v07_i8_pass11"')
    function = re.search(re.escape(main) + r' = OpFunction\b', text)
    if not function:
        raise ValueError('Missing pass-11 entry function')
    start = function.start()
    entry = re.search(r'(?m)^\s*(%\S+) = OpLabel\n((?:\s*%\S+ = OpVariable[^\n]+\n)*)', text[start:])
    if not entry:
        raise ValueError('Unsupported pass-11 entry block')
    original_label, variables = entry[1], entry[2]
    # Keep function variables in the first block and retain every original label
    # and phi predecessor. No instruction in the original shader body is changed.
    p = '%tlg_fsr4_guard_'
    guard = f'''{p}entry = OpLabel
{variables}{p}gid = OpLoad {vector} {gid}
{p}x = OpCompositeExtract {uint} {p}gid 0
{p}y = OpCompositeExtract {uint} {p}gid 1
{p}x_in = OpULessThan {boolean} {p}x {width}
{p}y_in = OpULessThan {boolean} {p}y {height}
{p}in = OpLogicalAnd {boolean} {p}x_in {p}y_in
OpSelectionMerge {p}merge None
OpBranchConditional {p}in {p}merge {p}out
{p}out = OpLabel
OpReturn
{p}merge = OpLabel
OpBranch {original_label}
{original_label} = OpLabel
'''
    begin, end = start + entry.start(), start + entry.end()
    return text[:begin] + '\n' + guard + text[end:], (int(constants[width]), int(constants[height]))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(source, destination):
    source, destination = source.resolve(), destination.resolve()
    if source == destination:
        raise ValueError('Output must not overwrite the original assets directory')
    tools = {name: shutil.which(name) for name in ('spirv-dis', 'spirv-as', 'spirv-val')}
    if not all(tools.values()):
        raise FileNotFoundError('spirv-dis, spirv-as and spirv-val are required on PATH')
    passes = sorted(source.glob('fsr4_model_v07_i8_*_pass11.spv'))
    if not passes:
        raise ValueError('No v07 pass-11 assets found')
    records = []
    # Validate the entire set before publishing any prepared file.
    with tempfile.TemporaryDirectory(prefix='knack-fsr4-prepare-') as temporary:
        scratch = Path(temporary)
        for path in passes:
            result = subprocess.run([tools['spirv-dis'], str(path)], check=True, capture_output=True,
                                    text=True, encoding='utf-8', timeout=30)
            assembly, bounds = guard_assembly(result.stdout)
            text = scratch / (path.stem + '.spvasm')
            text.write_text(assembly, encoding='utf-8')
            guarded = scratch / path.name
            subprocess.run([tools['spirv-as'], '--target-env', 'vulkan1.3', str(text), '-o', str(guarded)],
                           check=True, capture_output=True, timeout=30)
            post = source / path.name.replace('_pass11.spv', '_post.spv')
            if not post.is_file():
                raise FileNotFoundError(post)
            shutil.copy2(post, scratch / post.name)
            for original, prepared in ((path, guarded), (post, scratch / post.name)):
                subprocess.run([tools['spirv-val'], '--target-env', 'vulkan1.3', str(prepared)],
                               check=True, capture_output=True, timeout=30)
                records.append({'file': original.name, 'source_sha256': sha(original),
                                'prepared_sha256': sha(prepared),
                                'input_bounds': bounds if original == path else None})
        # Some tiers share a model but have additional postpass variants.
        recorded = {record['file'] for record in records}
        for post in sorted(source.glob('fsr4_model_v07_i8_*_post.spv')):
            if post.name in recorded:
                continue
            shutil.copy2(post, scratch / post.name)
            subprocess.run([tools['spirv-val'], '--target-env', 'vulkan1.3', str(scratch / post.name)],
                           check=True, capture_output=True, timeout=30)
            records.append({'file': post.name, 'source_sha256': sha(post),
                            'prepared_sha256': sha(scratch / post.name), 'input_bounds': None})
        destination.mkdir(parents=True, exist_ok=True)
        for record in records:
            # A running context already owns its shader code; future contexts must
            # see a complete file even if preparation overlaps a model load.
            with tempfile.NamedTemporaryFile(dir=destination, prefix='.fsr4-', delete=False) as pending:
                replacement = Path(pending.name)
            try:
                shutil.copy2(scratch / record['file'], replacement)
                os.replace(replacement, destination / record['file'])
            finally:
                replacement.unlink(missing_ok=True)
        (destination / 'KNACK2-FSR4-PREPARE.json').write_text(json.dumps({
            'policy': 'windows-original-spirv-with-pass11-bounds-guard', 'files': records}, indent=2),
            encoding='utf-8')
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args(argv)
    records = prepare(args.assets, args.output or args.assets / 'opt')
    print(f'FSR 4 Windows: {len(records)} validated assets prepared; original shader bodies preserved.')


if __name__ == '__main__':
    main()
