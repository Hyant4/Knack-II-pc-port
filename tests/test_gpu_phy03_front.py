"""Compile production Draw routing and front-census helper against CPU-only spies.

Checks branch coverage and unchanged routing with tracing off/on. No GPU, guest
memory access or game execution is exercised by these tests.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get("MSYS2_ROOT") or Path(os.environ.get("LOCALAPPDATA", "")) /
                 "knack2-toolchain/msys64") / "clang64"
COMPILER = TOOLCHAIN / "bin/clang++.exe"


@unittest.skipUnless(COMPILER.is_file(), "existing Windows Clang toolchain unavailable")
class FrontRoutingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="relic-front-")
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        text = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp").read_text(encoding="utf-8")
        pod = text[text.index("struct DrawTraceRecord {"):text.index("struct Epoch {")]
        header = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h").read_text(encoding="utf-8")
        pod = header[header.index("struct RelicCommandSample {"):header.index("class GraphicsPipeline;")] + pod
        helper = text[text.index("void TraceFront("):text.index("void TraceDraw(")]
        draw = text[text.index("void Rasterizer::Draw(bool"):text.index("void Rasterizer::DiagPassBoundary(")]
        source = r'''
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
using u32 = unsigned int; using u64 = unsigned long long;
namespace AmdGpu {
enum class PrimitiveType : u32 { TriangleList, AdjTriangleList, AdjTriangleStrip };
struct Program { u64 address{}; template <class T> T Address() const { return T(address); } };
struct VgtNumInstances { u32 num_instances{7}; u32 NumInstances() const { return num_instances ? num_instances : 1; } };
struct Regs {
    std::array<Program, 6> programs{{{0x100}, {0x200}, {0x300}, {0x400}, {0x500}, {0x600}}};
    u32 stage_enable{19}, color_control{0x10}, color_target_mask{0xfffff};
    PrimitiveType primitive_type{PrimitiveType::TriangleList};
    u32 num_indices{180}; VgtNumInstances num_instances;
    const Program* ProgramForStage(u32 i) const { return &programs[i]; }
};
}
namespace Shader { enum class SwStage : u32 { Vertex, Fragment }; }
struct Stage { u64 pgm_hash{}; };
struct GraphicsPipeline {
    struct Key { AmdGpu::PrimitiveType prim_type{AmdGpu::PrimitiveType::TriangleList}; } key;
    Stage vs{0x6e383eb8}, ps{0xb1f064c3};
    const Key& GetGraphicsKey() const { return key; }
    std::array<const Stage*, 2> GetStages() const { return {&vs, &ps}; }
};
namespace GpuPhy03 {
''' + pod + r'''
struct State { std::atomic<bool> active{false}; bool trace_enabled{true};
    std::mutex mutex; std::shared_ptr<DrawTraceWindow> trace_window = std::make_shared<DrawTraceWindow>(); };
State& Get() { static State state; return state; }
bool TraceEnabled() { return Get().active && Get().trace_enabled; }
''' + helper + r'''
}
struct PreparedDraw { static constexpr u32 Ready = 1; std::atomic<u32> state{Ready}; u64 reg_checksum{}; };
namespace TlgStats { std::atomic<u64> draws{}; }
namespace TlgToggle { enum Toggle { PipelinedMemoryWrites }; bool disabled{}; bool Disabled(Toggle) { return disabled; } }
namespace DrawPipe { constexpr int ReasonDraw = 1; }
namespace FrameCapture { void Poll() {} }
#define RENDERER_TRACE
struct Rasterizer {
    bool pipelined{}, filter_pass{true}, filter_result{true};
    unsigned recorded{}, queued{}, specials{}, filtered{}, lookups{};
    GraphicsPipeline pipeline;
    AmdGpu::Regs regs;
    struct Scheduler { void PopPendingOperations() {} } scheduler;
    struct Prep { void Count(bool) {} } prep, *draw_prep = &prep;
    struct Liverpool { u64 gfx_reg_checksum{}; } live, *liverpool = &live;
    struct Cache {
        Rasterizer* self; bool missing{};
        const GraphicsPipeline* GetGraphicsPipeline(int, const PreparedDraw*) {
            ++self->lookups; return missing ? nullptr : &self->pipeline;
        }
        const PreparedDraw* UsedPrepared() { return nullptr; }
    } pipeline_cache{this};
    bool UseDrawPipe() const { return pipelined; }
    bool FilterDrawPasses() const { return filter_pass; }
    bool FilterDraw() { ++filtered; return filter_result; }
    const AmdGpu::Regs& Regs() const { return regs; }
    void DrainDrawPipe(int) {}
    void PostDraw(const GraphicsPipeline* p, const PreparedDraw*, bool, u32) {
        if (p) ++queued; else ++specials;
    }
    void DrawRecord(const GraphicsPipeline*, const PreparedDraw*, bool, u32) { ++recorded; }
    void Draw(bool, u32, const PreparedDraw*);
};
''' + draw + r'''
int main(int argc, char** argv) {
    if (argc != 2) return 10;
    const std::string mode = argv[1];
    if (mode == "adjacency") _putenv_s("TLG_SKIP_ADJACENCY", "1");
    std::array<u32, 5> routing{};
    for (bool enabled : {false, true}) {
        auto& state = GpuPhy03::Get(); state.active = enabled;
        state.trace_window = std::make_shared<GpuPhy03::DrawTraceWindow>();
        Rasterizer r; u32 expected = 5;
        if (mode == "filter") { r.filter_result = false; expected = 2; }
        if (mode == "missing") { r.pipeline_cache.missing = true; expected = 3; }
        if (mode == "adjacency") { r.pipeline.key.prim_type = AmdGpu::PrimitiveType::AdjTriangleList; expected = 4; }
        if (mode == "queued") r.pipelined = true;
        if (mode == "special") { r.pipelined = true; r.filter_pass = false; expected = 6; }
        if (mode == "special-inline") {
            r.pipelined = true; r.filter_pass = false; r.filter_result = false;
            TlgToggle::disabled = true; expected = 2;
        }
        if (mode == "zero") r.regs.num_instances.num_instances = 0;
        PreparedDraw prepared;
        r.Draw(true, 0, &prepared);
        const std::array<u32, 5> actual{r.recorded,r.queued,r.specials,r.filtered,r.lookups};
        auto& w = *state.trace_window;
        if (!enabled) { routing = actual; if (w.front_used) return 11; continue; }
        if (actual != routing || w.front_used != 2 || w.front_dropped) return 12;
        const auto& entry = w.front[0]; const auto& terminal = w.front[1];
        if (entry.key.outcome != 1 || terminal.key.outcome != expected) return 13;
        if (entry.calls != 1 || entry.indices != 180 || entry.instances != (mode == "zero" ? 1 : 7)) return 14;
        if (entry.key.programs[0] != 0x100 || entry.key.programs[5] != 0x600 || entry.key.stages != 19) return 15;
        if (entry.key.vs_hash || entry.key.ps_hash) return 16;
        if ((expected == 4 || expected == 5) &&
            (terminal.key.vs_hash != 0x6e383eb8 || terminal.key.ps_hash != 0xb1f064c3)) return 17;
        if (entry.raw_zero_instances != (mode == "zero" ? 1 : 0)) return 18;
        if (mode == "queued" && (r.queued != 1 || r.recorded)) return 19;
        if (mode == "special" && (r.specials != 1 || r.lookups)) return 20;
        if ((mode == "direct" || mode == "zero") && r.recorded != 1) return 21;
    }
    return 0;
}
'''
        path = folder / "front.cpp"
        path.write_text(source, encoding="utf-8")
        cls.binary = folder / "front.exe"
        cls.environment = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "ISS_"))}
        cls.environment["PATH"] = str(TOOLCHAIN / "bin") + os.pathsep + cls.environment.get("PATH", "")
        result = subprocess.run([str(COMPILER), "-std=c++20", "-O0", "-pthread", str(path), "-o", str(cls.binary)],
                                env=cls.environment, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_actual_draw_routes_identically_with_trace_off_and_on(self):
        for mode in ("direct", "filter", "missing", "adjacency", "queued", "special", "special-inline", "zero"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], env=self.environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
