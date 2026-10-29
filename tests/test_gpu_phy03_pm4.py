"""CPU-only checks of production PM4 decode, packet hooks and CondExec block.

Runs selected ProcessGraphics blocks, not its coroutine scheduler or GPU queues.
Uses real register/packet types, production register application and trace helper.
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
class Pm4HookTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="relic-pm4-")
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        parser = (ROOT / "port/gpu/shadps4/video_core/amdgpu/liverpool.cpp").read_text(encoding="utf-8")
        renderer = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp").read_text(encoding="utf-8")
        pod = renderer[renderer.index("struct DrawTraceRecord {"):renderer.index("struct Epoch {")]
        header = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h").read_text(encoding="utf-8")
        pod = header[header.index("struct RelicCommandSample {"):header.index("class GraphicsPipeline;")] + pod
        helper = renderer[renderer.index("bool Rasterizer::RelicPm4TraceActive()"):renderer.index("bool Rasterizer::RelicSubmissionTraceActive()")]
        helper += renderer[renderer.index("void Rasterizer::TraceRelicPm4("):renderer.index("\nnamespace {", renderer.index("void Rasterizer::TraceRelicPm4("))]
        decode = parser[parser.index("u64 Liverpool::HashRegisterPacket("):parser.index("// tlg: TLG_DCB_STATS")]
        next_packet = parser[parser.index("static std::span<const u32> NextPacket("):parser.index("Liverpool::Liverpool()")]
        packet_hook = parser[parser.index("            const bool trace_pm4 ="):parser.index("            // DmaData to 0x3022C")]
        conditional = parser[parser.index("            case PM4ItOpcode::CondExec: {"):parser.index("            default:", parser.index("            case PM4ItOpcode::CondExec: {"))]
        source = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include "video_core/amdgpu/regs.h"
#include "video_core/amdgpu/pm4_cmds.h"
#undef ASSERT
#define ASSERT(x) do { if (!(x)) std::abort(); } while(false)
#undef LOG_ERROR
#undef LOG_WARNING
#define LOG_ERROR(...) do {} while(false)
#define LOG_WARNING(...) do {} while(false)
namespace GpuPhy03 {
''' + pod + r'''
struct State {
    bool pm4_enabled{true}, producer_enabled{false}; u64 producer_sequence{};
    std::atomic<bool> active{false}; std::mutex mutex;
    std::shared_ptr<DrawTraceWindow> trace_window = std::make_shared<DrawTraceWindow>(true);
};
State& Get() { static State s; return s; }
}
bool* change_condition_after_decision{};
struct Rasterizer {
    AmdGpu::Regs& regs;
    const AmdGpu::Regs& Regs() const { return regs; }
    bool RelicPm4TraceActive() const;
    void TraceRelicPm4(u32,u32,std::span<const u32>,u32,u64,u64,u64,u32,u32,bool=false,u32=0);
};
''' + helper + r'''
// Wrap the real collector only to change condition memory after its existing decision.
struct TraceSpy : Rasterizer {
    void TraceRelicPm4(u32 kind,u32 opcode,std::span<const u32> remaining,u32 words,u64 base,
                      u64 size,u64 submission,u32 before_count,u32 before_instances,
                      bool skipped=false,u32 skip_words=0) {
        Rasterizer::TraceRelicPm4(kind,opcode,remaining,words,base,size,submission,
                                 before_count,before_instances,skipped,skip_words);
        if (kind == 2 && change_condition_after_decision)
            *change_condition_after_decision = !*change_condition_after_decision;
    }
};
namespace AmdGpu {
struct Liverpool {
    Regs regs{}; RegDirty pipe_dirty; u64 gfx_reg_checksum{};
    TraceSpy spy{{regs}}, *rasterizer = &spy;
    static u64 HashRegisterPacket(u64,const u32*,u32);
    static void ApplyGraphicsRegisterPacket(Regs&,const PM4Header*,u64&,RegDirty*);
    void Run(std::span<const u32> dcb, bool trace_pm4_requested);
};
''' + decode + next_packet + r'''
void Liverpool::Run(std::span<const u32> dcb, bool trace_pm4_requested) {
    const auto base_addr = reinterpret_cast<uintptr_t>(dcb.data());
    const auto trace_buffer_words = dcb.size(); const u64 seq = 17;
    while (!dcb.empty()) {
        const auto* header = reinterpret_cast<const PM4Header*>(dcb.data());
        const u32 count = header->type3.NumWords();
        const PM4ItOpcode opcode = header->type3.opcode;
''' + packet_hook + r'''
        switch (opcode) {
''' + conditional + r'''
        default: break;
        }
        dcb = NextPacket(dcb, count+1);
    }
}
}
u32 Header(AmdGpu::PM4ItOpcode op,u32 words) { return 0xc0000000u | ((words-2)<<16) | (u32(op)<<8); }
int main(int argc,char** argv) {
    if (argc != 2) return 10;
    using Op = AmdGpu::PM4ItOpcode;
    const std::string mode = argv[1];
    alignas(4) bool condition = mode == "cond-keep";
    std::vector<u32> words;
    if (mode.starts_with("cond-")) {
        const u64 address = reinterpret_cast<uintptr_t>(&condition);
        words = {Header(Op::CondExec,4),u32(address),u32(address>>32),2,
                 Header(Op::NumInstances,2),13,Header(Op::Nop,2),0};
    } else if (mode == "num") words = {Header(Op::NumInstances,2),9};
    else if (mode == "indexed") words = {Header(Op::DrawIndex2,6),1000,0x1000,0,321,0};
    else if (mode == "offset") words = {Header(Op::DrawIndexOffset2,5),1000,78,5,0};
    else if (mode == "auto") words = {Header(Op::DrawIndexAuto,3),42,0};
    else if (mode == "register-writer") {
        const auto offset = offsetof(AmdGpu::Regs,num_instances)/4 - AmdGpu::Regs::UconfigRegWordOffset;
        words = {Header(Op::SetUconfigReg,3),u32(offset),23};
    } else if (mode == "prefix-limit") {
        words.assign(16,0); words[0] = Header(Op::Nop,16);
    } else return 11;
    u32 expected_count{}, expected_instances{}; u64 expected_checksum{};
    for (bool enabled : {false,true}) {
        AmdGpu::Liverpool engine;
        engine.regs.num_indices = 7; engine.regs.num_instances.num_instances = 3;
        auto& state = GpuPhy03::Get(); state.active = enabled;
        state.trace_window = std::make_shared<GpuPhy03::DrawTraceWindow>(true);
        condition = mode == "cond-keep";
        // Switching it after the trace must not change the parser's local skip decision.
        change_condition_after_decision = enabled && mode.starts_with("cond-") ? &condition : nullptr;
        engine.Run(words,enabled);
        auto& w = *state.trace_window->pm4;
        if (!enabled) {
            expected_count = engine.regs.num_indices; expected_instances = engine.regs.num_instances.num_instances;
            expected_checksum = engine.gfx_reg_checksum;
            if (w.used) return 12;
            continue;
        }
        if (engine.regs.num_indices != expected_count || engine.regs.num_instances.num_instances != expected_instances ||
            engine.gfx_reg_checksum != expected_checksum || w.dropped) return 13;
        if (mode == "cond-skip" && (expected_instances != 3 || w.opcode_counts[u32(Op::NumInstances)])) return 14;
        if (mode == "cond-keep" && (expected_instances != 13 || w.opcode_counts[u32(Op::NumInstances)] != 1)) return 15;
        if (mode.starts_with("cond-")) {
            if (w.records[1].kind != 2 || w.records[1].skipped != (mode == "cond-skip") ||
                w.records[1].skip_words != 2) return 16;
        } else if (mode == "prefix-limit") {
            // Unselected packet is counted, without reading or retaining its body.
            if (w.used || w.opcode_counts[u32(Op::Nop)] != 1) return 17;
            engine.regs.num_indices = 99;
            engine.spy.TraceRelicPm4(1,u32(Op::Nop),words,16,reinterpret_cast<uintptr_t>(words.data()),16,17,1,3);
            if (w.used != 1 || w.records[0].raw_words != 8) return 18;
        } else {
            if (w.used != 1 || w.records[0].before_indices != 7 || w.records[0].before_instances != 3 ||
                w.records[0].after_indices != expected_count || w.records[0].after_instances != expected_instances) return 19;
        }
    }
    return 0;
}
'''
        cpp = folder / "pm4.cpp"
        cpp.write_text(source, encoding="utf-8")
        cls.binary = folder / "pm4.exe"
        cls.environment = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "ISS_"))}
        cls.environment["PATH"] = str(TOOLCHAIN / "bin") + os.pathsep + cls.environment.get("PATH", "")
        result = subprocess.run([str(COMPILER), "-std=c++23", "-O1", "-DFMT_HEADER_ONLY",
                                 "-I"+str(ROOT / "port/gpu/shim"), "-I"+str(ROOT / "port/gpu/shadps4"),
                                 "-I"+str(TOOLCHAIN / "include"),
                                 str(cpp), str(ROOT / "port/gpu/shadps4/video_core/amdgpu/regs.cpp"),
                                 "-o", str(cls.binary), "-lws2_32"], env=cls.environment,
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_register_decode_and_conditional_decision_preserved_with_trace(self):
        for mode in ("num", "indexed", "offset", "auto", "register-writer", "cond-skip", "cond-keep", "prefix-limit"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], env=self.environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
