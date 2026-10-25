"""Replay real cache records through the extracted production loader and comparator.

The source loader, deserializer and comparator are used verbatim. Host services
use test doubles: disk access, Vulkan module creation, executable walker allocation,
the fixture's binary version, and logging/assertion reporting. Assertions still fail.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import sys

AREA = Path(__file__).resolve().parents[2]
SOURCE = AREA
TOOLS = Path.home() / 'AppData/Local/knack2-toolchain/msys64/clang64/bin'
path = SOURCE / 'port/gpu/shadps4/video_core/renderer_vulkan/vk_pipeline_serialization.cpp'
source = path.read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    level = 0
    for i in range(opening, len(source)):
        if source[i] == '{': level += 1
        elif source[i] == '}':
            level -= 1
            if not level: return source[start:i + 1]
    raise ValueError(signature)

methods = source[source.index('void Info::Serialize('):source.rindex('} // namespace Shader')]
loader = function('bool PipelineCache::LoadPipelineStage(').replace('PipelineCache::', 'ProbeCache::').replace('Storage::DataBase::Instance().Load', 'probe_storage.Load')
generated = (AREA / 'tests/pipeline_cache/cache_probe.cpp.in').read_text(encoding='utf-8')
generated = generated.replace('@SHADER_SERIALIZATION@', methods).replace('@LOAD_SHADER_META@', function('bool LoadShaderMeta(')).replace('@LOAD_PIPELINE_STAGE@', loader)
cpp = AREA / 'tests/pipeline_cache/cache_probe.cpp'
cpp.write_text(generated, encoding='utf-8')
entries = json.loads(Path(os.environ.get('KNACK_CACHE_COMPILE_COMMANDS', AREA / 'port/out/release/compile_commands.json')).read_text())
command = next(x['command'] for x in entries if x['file'].endswith('vk_pipeline_serialization.cpp'))
includes = re.findall(r'(?<!\S)-I([^\s]+)', command)
system = re.findall(r'-isystem\s+([^\s]+)', command)
exe = AREA / 'tests/pipeline_cache/cache_probe.exe'
args = [str(TOOLS / 'clang++.exe'), '-std=c++23', '-O0', '-g', '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN', '-DNDEBUG', '-ffunction-sections', '-fdata-sections',
        *['-I' + x for x in includes], *sum([['-isystem', x] for x in system], []),
        '-include', str(SOURCE / 'port/gpu/shim/tlg_win_compat.h'), str(cpp), '-o', str(exe), '-Wl,--gc-sections', '-lfmt']
env = dict(os.environ, PATH=str(TOOLS) + os.pathsep + os.environ.get('PATH', ''))
build = subprocess.run(args, env=env, capture_output=True, text=True)
(AREA / 'tests/pipeline_cache/probe-build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
if build.returncode:
    print((build.stdout + build.stderr)[-6500:])
    raise SystemExit(build.returncode)
fixture = '--synthetic'
run = subprocess.run([str(exe), fixture], env=env, capture_output=True, text=True)
label = next((x for x in sys.argv[1:] if not x.startswith('--')), 'synthetic')
(AREA / 'tests/pipeline_cache' / (label + '.json')).write_text(json.dumps({'exit_code': run.returncode, 'stdout': run.stdout, 'stderr': run.stderr}, indent=2), encoding='utf-8')
print(run.stdout + run.stderr)
raise SystemExit(run.returncode)
