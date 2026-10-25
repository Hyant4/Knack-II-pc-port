"""Exercise the production on-demand startup with a cache-opening test double."""
import json
import os
from pathlib import Path
import subprocess
import sys

AREA = Path(__file__).resolve().parents[2]
source = (AREA / 'port/gpu/shadps4/video_core/renderer_vulkan/vk_pipeline_serialization.cpp').read_text(encoding='utf-8')
start = source.index('void PipelineCache::StartOnDemand()')
opening = source.index('{', start)
level = 0
for end in range(opening, len(source)):
    if source[end] == '{': level += 1
    elif source[end] == '}':
        level -= 1
        if not level: break
method = source[start:end + 1]
text = r'''
#include <cstdio>
#include <cstdlib>
#include <map>
namespace Storage {
struct DataBase {
    static DataBase& Instance() { static DataBase db; return db; }
    void FinishPreload() { finished = true; }
    bool finished = false;
};
}
struct PipelineCache {
    bool cached = true, lazy_programs = false, checked = false;
    std::map<int, int> next_permutation;
    bool CheckCacheProfile() { checked = true; return cached; }
    void StartOnDemand();
};
@METHOD@
int main() {
    _putenv_s("TLG_LAZY_PROGRAMS", "");
    PipelineCache normal;
    normal.StartOnDemand();
    if (!normal.checked || !normal.lazy_programs || !Storage::DataBase::Instance().finished) return 1;
    _putenv_s("TLG_LAZY_PROGRAMS", "0");
    PipelineCache disabled;
    disabled.StartOnDemand();
    if (disabled.lazy_programs) return 2;
    _putenv_s("TLG_LAZY_PROGRAMS", "");
    PipelineCache empty;
    empty.cached = false;
    empty.StartOnDemand();
    if (empty.lazy_programs) return 3;
    std::puts("3 on-demand cache startup cases passed");
}
'''.replace('@METHOD@', method)
cpp = AREA / 'tests/pipeline_cache/startup_probe.cpp'
cpp.write_text(text, encoding='utf-8')
tools = Path.home() / 'AppData/Local/knack2-toolchain/msys64/clang64/bin'
env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ.get('PATH', ''))
exe = cpp.with_suffix('.exe')
subprocess.run([str(tools / 'clang++.exe'), '-std=c++23', str(cpp), '-o', str(exe)], env=env, check=True)
result = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
label = sys.argv[1] if len(sys.argv) > 1 else 'startup'
(AREA / 'tests/pipeline_cache' / (label + '.json')).write_text(json.dumps({'exit_code': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}, indent=2), encoding='utf-8')
print(json.dumps({'exit_code': result.returncode, 'stdout': result.stdout}), flush=True)
raise SystemExit(result.returncode)
