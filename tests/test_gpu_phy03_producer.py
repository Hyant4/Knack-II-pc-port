"""Production GNM writer and bounded caller sampling on CPU; no game or GPU.

Uses real packet types, Windows unwind metadata, logging macro and safe reads.
Does not prove a complete guest unwind or causality of a runtime candidate link.
"""

import copy
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get("MSYS2_ROOT") or Path(os.environ.get("LOCALAPPDATA", "")) /
                 "knack2-toolchain/msys64") / "clang64"
COMPILER = TOOLCHAIN / "bin/clang++.exe"
SPEC = importlib.util.spec_from_file_location("producer_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)


@unittest.skipUnless(COMPILER.is_file(), "existing Windows Clang toolchain unavailable")
class ProducerNativeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="relic-producer-")
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        renderer = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h").read_text(encoding="utf-8")
        gnm = (ROOT / "port/gpu/shadps4/core/libraries/gnmdriver/gnmdriver.cpp").read_text(encoding="utf-8")
        types = header[header.index("struct RelicCommandSample {"):header.index("class GraphicsPipeline;")]
        pod = renderer[renderer.index("struct DrawTraceRecord {"):renderer.index("struct Epoch {")]
        helpers = renderer[renderer.index("bool Rasterizer::RelicProducerTraceActive()"):renderer.index("bool Rasterizer::RelicSubmissionTraceActive()")]
        helpers += renderer[renderer.index("void Rasterizer::TraceRelicPm4("):renderer.index("\nnamespace {", renderer.index("void Rasterizer::TraceRelicPm4("))]
        writer = gnm[gnm.index("s32 PS4_SYSV_ABI sceGnmDrawIndexOffset("):gnm.index("s32 PS4_SYSV_ABI sceGnmDrawIndirect(")]
        nop_start = gnm.index("template <u32 data_block_size>")
        nop = gnm[nop_start:gnm.index("\n}", nop_start)+2]
        source = r'''
#define NOMINMAX
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include "video_core/amdgpu/regs.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include <windows.h>
#include "common/logging/log.h"
namespace Common::Log {
unsigned calls{};
[[gnu::noinline]] bool ShouldLog(Level) { return std::getenv("RELIC_TEST_LOG") != nullptr; }
[[gnu::noinline]] void Write(std::string_view,Level,const char*,int,const char*,std::string&& message) {
    if (message != "called") std::abort();
    ++calls;
}
}
namespace Vulkan {
''' + types + r'''
namespace GpuPhy03 {
''' + pod + r'''
struct State {
    bool producer_enabled{true}, pm4_enabled{true}, list_enabled{false}; std::atomic<bool> active{false};
    std::mutex mutex; u64 producer_sequence{};
    std::shared_ptr<DrawTraceWindow> trace_window = std::make_shared<DrawTraceWindow>(true);
};
State& Get() { static State s; return s; }
}
struct Rasterizer {
    AmdGpu::Regs regs{};
    const AmdGpu::Regs& Regs() const { return regs; }
    bool RelicProducerTraceActive() const;
    bool RelicPm4TraceActive() const;
    void TraceRelicProducer(const u32*,u64);
    void TraceRelicPm4(u32,u32,std::span<const u32>,u32,u64,u64,u64,u32,u32,bool=false,u32=0);
};
''' + helpers + r'''
}
Vulkan::Rasterizer rasterizer;
namespace Core {
struct Memory {
    static inline bool available = true;
    static Memory* Instance() { static Memory m; return &m; }
    Vulkan::Rasterizer* GetRasterizer() { return available ? &rasterizer : nullptr; }
};
}
namespace Libraries::GnmDriver {
using namespace AmdGpu;
constexpr s32 ORBIS_OK = 0;
bool neo{};
[[gnu::noinline]] bool sceKernelIsNeoMode() { return neo; }
''' + nop + '\n' + writer + r'''
}
// Unregistered guest-style frames: host metadata must unwind only as far as
// the precise ABI return PC, then the bounded guest frame walker takes over.
extern "C" s32 PS4_SYSV_ABI producer_guest_entry(u32*,u32,u32,u32,u32);
extern "C" void producer_guest_middle_return();
extern "C" void producer_guest_parent_return();
__asm__(R"(
.text
.p2align 4
.globl producer_guest_entry
producer_guest_entry:
    push %rbp
    mov %rsp,%rbp
    sub $32,%rsp
    call producer_guest_middle
.globl producer_guest_parent_return
producer_guest_parent_return:
    add $32,%rsp
    pop %rbp
    ret
.p2align 4
producer_guest_middle:
    push %rbp
    mov %rsp,%rbp
    sub $32,%rsp
    call _ZN9Libraries9GnmDriver21sceGnmDrawIndexOffsetEPjjjjj
.globl producer_guest_middle_return
producer_guest_middle_return:
    add $32,%rsp
    pop %rbp
    ret
)");
int main(int argc,char** argv) {
    if (argc != 2) return 10;
    using namespace Vulkan;
    const std::string mode = argv[1];
    auto& state = GpuPhy03::Get();
    const std::array<u32,5> packet{0xc0033500,60,4500,60,0};
    if (mode.starts_with("list-")) {
        auto* image = static_cast<u8*>(VirtualAlloc(nullptr,0xc34000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if (!image) return 30;
        const u64 base = u64(image);
        const u8 a[]{0x4c,0x89,0x85,0x40,0xfe,0xff,0xff}, b[]{0x48,0x89,0x9d,0x68,0xfe,0xff,0xff};
        const u8 c[]{0x8b,0x43,0x40}, d[]{0x4a,0x8b,0x44,0x36,0xcc};
        std::memcpy(image+0xc33bbb,a,sizeof(a)); std::memcpy(image+0xbe56f5,b,sizeof(b));
        std::memcpy(image+0xbe5684,c,sizeof(c)); std::memcpy(image+0xbe579a,d,sizeof(d));
        alignas(16) std::array<u8,0x800> stack{};
        alignas(16) std::array<u8,0x48> header{};
        std::array<u8,4*0x58> entries{};
        std::array<u64,3> materials{0x12340000,0,0x12350000};
        auto put = [](void* destination,auto value) { std::memcpy(destination,&value,sizeof(value)); };
        put(header.data(),u64(entries.data())); put(header.data()+0x40,u32(4)); put(header.data()+0x44,u32(4));
        put(entries.data()+0x58+0x20,u64(&materials[0]));
        put(entries.data()+2*0x58+0x20,u64(&materials[1]));
        put(entries.data()+3*0x58+0x20,mode=="list-partial" ? 1ull : u64(&materials[2]));
        RelicProducerSample r; r.caller_allocation_base=base; r.caller=base+0xbb05f1;
        r.host_unwind=1; r.frame_count=4;
        const u64 returns[]{0xbf5834,0xbe5989,0xc348c5,0x9bc831};
        for (u32 i=0;i<4;++i) r.frames[i]={u64(stack.data()+0x100+i*0x100),base+returns[i],base};
        const u64 builder=r.frames[2].frame,owner=r.frames[3].frame;
        put(reinterpret_cast<void*>(owner-0x1c0),u64(header.data()));
        put(reinterpret_cast<void*>(builder-0x198),u64(header.data()));
        put(reinterpret_cast<void*>(builder-0x164),u32(2));
        put(reinterpret_cast<void*>(builder-0x1b0),u64(0x54+0x58));
        if (mode=="list-signature") image[0xc33bbb]^=1;
        if (mode=="list-path") ++r.frames[0].caller;
        if (mode=="list-slots") put(reinterpret_cast<void*>(builder-0x198),1ull);
        if (mode=="list-invalid") put(header.data()+0x40,u32(5));
        if (mode=="list-unreadable-header") {
            put(reinterpret_cast<void*>(owner-0x1c0),1ull);
            put(reinterpret_cast<void*>(builder-0x198),1ull);
        }
        const auto entry_before=entries; const auto material_before=materials; const auto stack_before=stack;
        SampleRelicListHeader(r);
        const u32 expected=mode=="list-signature"?2:mode=="list-path"?0:mode=="list-slots"?6:
            mode=="list-invalid"?4:mode=="list-unreadable-header"?3:1;
        if (r.list.status!=expected || stack!=stack_before || entries!=entry_before || materials!=material_before) return 31;
        if (expected!=1) { VirtualFree(image,0,MEM_RELEASE); return 0; }
        auto& q=r.list;
        if (q.owner_frame!=owner || q.builder_frame!=builder || q.list!=u64(header.data()) ||
            q.entries!=u64(entries.data()) || q.count!=4 || q.capacity!=4 || q.batch_count!=2) return 32;
        auto storage = std::make_unique<GpuPhy03::Pm4TraceWindow>();
        auto& window = *storage;
        if (window.ReserveListScan(q)!=1 || window.ReserveListScan(q)!=2 || window.list_key_used!=1 ||
            window.list_scan_bytes!=4*96) return 33;
        if (mode=="list-budget") {
            window.list_key_used=0; window.list_scan_bytes=0; q.count=4096;
            for (u32 i=0;i<2;++i) { q.list+=8; if (window.ReserveListScan(q)!=1) return 34; }
            q.list+=8; if (window.ReserveListScan(q)!=3 || window.list_scan_bytes!=2*4096*96) return 35;
            q.count=4097; if (window.ReserveListScan(q)!=4) return 36;
            window.list_key_used=0; window.list_scan_bytes=0; q.count=0;
            for (u32 i=0;i<32;++i) { q.list+=8; if (window.ReserveListScan(q)!=1) return 37; }
            q.list+=8; if (window.ReserveListScan(q)!=3) return 38;
        } else {
            if (mode=="list-changed") put(header.data()+0x40,u32(3));
            if (mode=="list-reread-failed") q.list=1;
            if (mode=="list-entries-failed") q.entries=1;
            ScanRelicListMaterials(q);
            const u32 scan=mode=="list-partial"?6:mode=="list-changed"?7:
                mode=="list-reread-failed"?8:mode=="list-entries-failed"?5:1;
            if (q.scan_status!=scan || (scan!=5 && (q.scanned!=4 || q.null_records!=1 || q.null_materials!=1 ||
                q.readable_materials!=(scan==6?1u:2u) || q.unreadable_materials!=(scan==6?1u:0u)))) return 39;
            if (entries!=entry_before || materials!=material_before || stack!=stack_before) return 40;
        }
        VirtualFree(image,0,MEM_RELEASE); return 0;
    }
    if (mode.starts_with("writer-")) {
        using namespace Libraries::GnmDriver;
        _putenv_s("TLG_RELIC_PRODUCER_TRACE", mode == "writer-disabled" ? "0" : "1");
        if (mode == "writer-logging") _putenv_s("RELIC_TEST_LOG", "1");
        state.active = mode != "writer-inactive";
        Core::Memory::available = mode != "writer-uninitialized";
        const bool collect = state.active && Core::Memory::available && mode != "writer-disabled";
        std::array<u32,11> words;
        for (bool neo_value : {false,true}) {
            neo = neo_value;
            words.fill(0xdeadbeef);
            if (producer_guest_entry(words.data()+1,9,4500,60,0xe0000001) != 0) return 11;
            const std::array<u32,11> expected{0xdeadbeef,0xc0033501,60,4500,60,
                neo ? 0xe0000000u : 0u,0xc0021000,0,0xdeadbeef,0xdeadbeef,0xdeadbeef};
            if (words != expected) return 12;
        }
        auto saved_window = state.trace_window;
        auto& window = *saved_window->pm4;
        if (window.producer_used != (collect ? 2u : 0u)) return 13;
        if (collect && (window.producers[0].command != u64(words.data()+1) ||
            window.producers[1].packet[4] != 0xe0000000u || !window.producers[0].frame_count ||
            window.producers[0].thread != GetCurrentThreadId())) return 14;
        if (collect) {
            const auto& sample = window.producers[0];
            if (sample.host_unwind != 1 || sample.host_steps != 2 ||
                sample.caller != u64(&producer_guest_middle_return) ||
                sample.frames[0].caller != u64(&producer_guest_parent_return) ||
                !sample.caller_allocation_base || sample.frames[0].frame != sample.guest_frame) return 16;
            // The former [frame,frame+8] interpretation must fail for this real logging layout.
            // The executable disassembly is also retained by the Python test below.
            std::printf("caller=%llx parent=%llx steps=%u frames=%u stop=%u\n",
                sample.caller,sample.frames[0].caller,sample.host_steps,sample.frame_count,sample.stop);
            state.trace_window = std::make_shared<GpuPhy03::DrawTraceWindow>(true);
            rasterizer.TraceRelicPm4(1,0x35,packet,5,u64(packet.data()),5,17,0,0);
            producer_guest_entry(words.data()+1,9,4500,60,0);
            const auto& next = *state.trace_window->pm4;
            if (next.records[0].producer_serial != 3 || next.producers[0].serial != 4 ||
                window.producer_used != 2) return 17;
        }
        const auto before = words;
        if (sceGnmDrawIndexOffset(nullptr,9,4500,60,0) != -1 ||
            sceGnmDrawIndexOffset(words.data()+1,8,4500,60,0) != -1 || words != before ||
            window.producer_used != (collect ? 2u : 0u)) return 15;
        if (mode == "writer-logging" && Common::Log::calls != 5) return 18;
        state.active = false; rasterizer.TraceRelicProducer(reinterpret_cast<const u32*>(1),1);
        state.active = true; state.producer_enabled = false;
        rasterizer.TraceRelicProducer(reinterpret_cast<const u32*>(1),1);
        return 0;
    }
    state.active = true;
    if (mode == "wrong-caller") {
        rasterizer.TraceRelicProducer(packet.data(), u64(&producer_guest_parent_return));
        const auto& sample = state.trace_window->pm4->producers[0];
        return sample.stop == 6 && sample.host_unwind != 1 && sample.host_steps <= 4 &&
            !sample.frame_count && !sample.guest_frame && !sample.guest_stack && sample.packet == packet ? 0 : 23;
    }
    alignas(16) std::array<std::array<u64,2>,10> frames{};
    for (size_t i=0;i<frames.size();++i) frames[i] = {
        i+1 == frames.size() ? 0 : u64(frames[i+1].data()), u64(&producer_guest_parent_return)};
    if (mode == "end") frames[2][0] = 0;
    if (mode == "cycle") frames[2][0] = u64(frames[0].data());
    if (mode == "span") frames[2][0] = u64(frames[0].data())+(2ull<<20);
    if (mode == "bad-return") frames[0][1] = 0x1000;
    u64 start = mode == "unreadable" ? 0x1000 : u64(frames[0].data());
    if (mode == "unaligned") ++start;
    const auto before = frames;
    RelicProducerSample sample; sample.guest_frame = start; sample.guest_stack = start & ~15ull;
    WalkRelicGuestFrames(sample);
    const u32 expected_stop = mode == "end" ? 1 : mode == "unreadable" ? 2 : mode == "limit" ? 5 : mode == "bad-return" ? 7 : 3;
    const u32 expected_frames = mode == "unreadable" || mode == "unaligned" || mode == "bad-return" ? 0 : mode == "limit" ? 8 : 3;
    if (sample.stop != expected_stop || sample.frame_count != expected_frames || frames != before ||
        state.trace_window->pm4->producer_used) return 20;
    rasterizer.TraceRelicProducer(packet.data(),0); // expected caller is deliberately invalid
    const auto& failed = state.trace_window->pm4->producers[0];
    return failed.stop == 6 && failed.host_unwind == 6 && !failed.frame_count &&
        !failed.guest_frame && !failed.guest_stack ? 0 : 21;
}
'''
        cpp = folder / "producer.cpp"
        cpp.write_text(source, encoding="utf-8")
        cls.binary = folder / "producer.exe"
        cls.cpp = cpp
        cls.environment = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "ISS_"))}
        cls.environment["PATH"] = str(TOOLCHAIN / "bin") + os.pathsep + cls.environment.get("PATH", "")
        command = [str(COMPILER), "-std=c++23", "-O2", "-g", "-DNDEBUG", "-fno-omit-frame-pointer", "-DFMT_HEADER_ONLY",
                   "-include", str(ROOT / "port/gpu/shim/tlg_win_compat.h"),
                   "-I"+str(ROOT / "port/gpu/shim"), "-I"+str(ROOT / "port/gpu/shadps4"),
                   "-I"+str(TOOLCHAIN / "include"), str(cpp),
                   str(ROOT / "port/gpu/shadps4/video_core/amdgpu/regs.cpp"), "-o", str(cls.binary), "-lws2_32"]
        result = subprocess.run(command, env=cls.environment, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_writer_preserves_packets_returns_and_inactive_paths(self):
        for mode in ("writer-enabled", "writer-logging", "writer-disabled", "writer-inactive", "writer-uninitialized"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], env=self.environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_stack_limits_failed_reads_and_shared_order(self):
        for mode in ("end", "cycle", "span", "limit", "unreadable", "unaligned", "bad-return", "wrong-caller"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], env=self.environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_list_layout_guards_safe_reads_materials_and_budgets(self):
        for mode in ("list-complete", "list-partial", "list-signature", "list-path", "list-slots",
                     "list-invalid", "list-unreadable-header", "list-budget", "list-changed",
                     "list-reread-failed", "list-entries-failed"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], env=self.environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_real_logging_layout_and_native_unwind_evidence(self):
        outputs = {}
        for mode in ("writer-enabled", "writer-logging"):
            result = subprocess.run([str(self.binary), mode], env=self.environment,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn("steps=2", result.stdout)
            outputs[mode] = result.stdout
        llvm = TOOLCHAIN / "bin/llvm-objdump.exe"
        symbols = ["_ZN9Libraries9GnmDriver21sceGnmDrawIndexOffsetEPjjjjj",
                   "_ZN6Vulkan10Rasterizer18TraceRelicProducerEPKjy"]
        disassembly = subprocess.run([str(llvm), "-d", "--no-show-raw-insn", "--x86-asm-syntax=intel",
                                     "--disassemble-symbols="+",".join(symbols), str(self.binary)],
                                    env=self.environment, capture_output=True, text=True, timeout=10)
        self.assertEqual(disassembly.returncode, 0, disassembly.stderr)
        self.assertIn("RtlVirtualUnwind", disassembly.stdout)
        self.assertIn("RtlCaptureContext", disassembly.stdout)
        # Optional local evidence; default test runs remain temporary and self-contained.
        if folder := os.environ.get("RELIC_TEST_ARTIFACT_DIR"):
            evidence = Path(folder)
            evidence.mkdir(exist_ok=False)
            (evidence / "producer.cpp").write_text(self.cpp.read_text(encoding="utf-8"), encoding="utf-8")
            (evidence / "producer.asm").write_text(disassembly.stdout, encoding="utf-8")
            (evidence / "native-unwind.json").write_text(json.dumps({"outputs": outputs,
                "binary_sha256": hashlib.sha256(self.binary.read_bytes()).hexdigest(),
                "scope": "real_writer_logging_macro_and_windows_unwind_with_guest_style_assembly_frames"}, indent=2))


class ProducerJoinTests(unittest.TestCase):
    def writer(self, serial=1, command=4096, count=60):
        return dict(order=0, serial=serial, command=command, thread=7,
                    packet=[0xc0033500,count,4500,count,0], stop=1,
                    frames=[dict(frame=8192,caller=0xbb05f1,caller_allocation_base=0)])

    def trace(self, writers=(), consumers=(), index=0, dropped=0):
        return dict(observation_index=index, present_index=100+index,
                    producer_samples=dict(integrity="passed", records=list(writers), dropped=dropped),
                    producer_consumers=list(consumers))

    def consumer(self, serial=2, command=4096, count=60):
        return dict(serial=serial, command=command, packet=[0xc0033500,count,4500,count,0],
                    submission=7, instances=64, programs=[0]*6, primitive=4, color_mask=31)

    def test_cross_interval_link_uses_serial_not_array_or_frame_order(self):
        result = analyzer.compare_producer_samples([
            self.trace(consumers=[self.consumer()], index=1), self.trace(writers=[self.writer()])])
        self.assertEqual(result["candidate_matches"], 1)
        self.assertEqual(result["rows"][0]["writer"]["observation_index"], 0)
        self.assertEqual(result["classification"], "candidate_links_not_buffer_generation_proof")

    def test_latest_writer_reuse_and_mismatch_never_fall_back_to_old_match(self):
        trace = self.trace(writers=[self.writer(),self.writer(serial=3,count=61)],
                           consumers=[self.consumer(),self.consumer(serial=4),self.consumer(serial=5,count=61)])
        result = analyzer.compare_producer_samples([trace])
        self.assertEqual(result["candidate_matches"], 2)
        self.assertEqual(result["packet_mismatches"], 1)
        self.assertEqual(result["rows"][1]["writer"]["serial"], 3)

    def test_missing_writer_and_observed_reexecution_are_explicit(self):
        trace = self.trace(writers=[self.writer(serial=3)],
                           consumers=[self.consumer(),self.consumer(serial=4),self.consumer(serial=5)])
        result = analyzer.compare_producer_samples([trace])
        self.assertEqual(result["missing_writers"], 1)
        self.assertEqual(result["rows"][2]["observed_uses"], 2)

    def test_duplicates_and_truncation_cannot_claim_complete_coverage(self):
        bad = self.trace(writers=[self.writer()],consumers=[self.consumer(serial=1)])
        self.assertEqual(analyzer.compare_producer_samples([bad])["integrity"], "failed")
        partial = self.trace(writers=[self.writer()],consumers=[self.consumer()],dropped=1)
        self.assertTrue(analyzer.compare_producer_samples([partial])["coverage_limited"])
        self.assertEqual(analyzer.compare_producer_samples([])["integrity"], "not_collected")

    def test_validator_rejects_wrong_opcode_duplicate_and_bad_stack(self):
        data = dict(contract="gnm_index_offset_producers_v1", coverage="successful_sceGnmDrawIndexOffset_only",
                    association="collector_serial_address_packet_candidate_only",
                    stack="bounded_frame_pointer_links_not_complete_unwind", status="complete", dropped=0,
                    records=[self.writer()])
        self.assertEqual(analyzer.validate_producer_samples(data)["integrity"], "passed")
        for what in ("packet", "stack", "duplicate"):
            broken = copy.deepcopy(data)
            if what == "packet": broken["records"][0]["packet"][0] ^= 0x4000
            elif what == "stack": broken["records"][0]["frames"][0]["frame"] += 1
            else:
                broken["records"].append(copy.deepcopy(broken["records"][0]))
                broken["records"][1]["order"] = 1
            with self.assertRaises(analyzer.CaptureError):
                analyzer.validate_producer_samples(broken)

    def test_v2_host_match_failure_and_legacy_quality_are_separate_from_packet_integrity(self):
        record = self.writer()
        record.update(caller=0xbb05f1, caller_allocation_base=0x1000, guest_frame=8192,
                      guest_stack=8160, host_unwind=1, host_steps=2)
        record["frames"][0]["caller_allocation_base"] = 0x1000
        data = dict(contract="gnm_index_offset_producers_v2", coverage="successful_sceGnmDrawIndexOffset_only",
                    association="collector_serial_address_packet_candidate_only",
                    stack="windows_metadata_to_import_caller_then_guest_frame_links", status="complete",
                    dropped=0, records=[record])
        validated = analyzer.validate_producer_samples(data)
        self.assertEqual(validated["host_matches"], 1)
        self.assertEqual(validated["host_failures"], 0)
        for field in ("host_steps", "caller_allocation_base", "guest_stack"):
            bad = copy.deepcopy(data); bad["records"][0][field] = 0
            with self.assertRaises(analyzer.CaptureError):
                analyzer.validate_producer_samples(bad)
        failed = copy.deepcopy(data)
        failed["records"][0].update(host_unwind=2, frames=[], guest_frame=0, guest_stack=0, stop=6)
        self.assertEqual(analyzer.validate_producer_samples(failed)["host_failures"], 1)
        failed["records"][0]["frames"] = record["frames"]
        with self.assertRaises(analyzer.CaptureError):
            analyzer.validate_producer_samples(failed)
        legacy = dict(data, contract="gnm_index_offset_producers_v1",
                      stack="bounded_frame_pointer_links_not_complete_unwind", records=[self.writer()])
        self.assertEqual(analyzer.validate_producer_samples(legacy)["stack_quality"], "legacy_host_start_unverified")


if __name__ == "__main__":
    unittest.main()
