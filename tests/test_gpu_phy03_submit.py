"""CPU spies for the real SubmitGfx/CopyCmdBuffers and sampling helpers.

Does not run the coroutine scheduler, GPU, guest or concurrent producer threads.
"""

import copy
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get("MSYS2_ROOT") or Path(os.environ.get("LOCALAPPDATA", "")) /
                 "knack2-toolchain/msys64") / "clang64"
COMPILER = TOOLCHAIN / "bin/clang++.exe"
SPEC = importlib.util.spec_from_file_location("submit_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)


@unittest.skipUnless(COMPILER.is_file(), "existing Windows Clang toolchain unavailable")
class SubmitRoutingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="relic-submit-")
        cls.addClassCleanup(cls.temp.cleanup)
        folder = Path(cls.temp.name)
        renderer = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp").read_text(encoding="utf-8")
        parser = (ROOT / "port/gpu/shadps4/video_core/amdgpu/liverpool.cpp").read_text(encoding="utf-8")
        header = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h").read_text(encoding="utf-8")
        types = header[header.index("struct RelicCommandSample {"):header.index("class GraphicsPipeline;")]
        pod = renderer[renderer.index("struct DrawTraceRecord {"):renderer.index("struct Epoch {")]
        helper = renderer[renderer.index("bool Rasterizer::RelicSubmissionTraceActive()"):renderer.index("void Rasterizer::TraceRelicPm4(")]
        submit = parser[parser.index("Liverpool::CmdBuffer Liverpool::CopyCmdBuffers("):parser.index("void Liverpool::SubmitAsc(")]
        source = r'''
#define XXH_INLINE_ALL
#include <xxhash.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <span>
#include <string>
#include <tuple>
#include <vector>
using u32 = unsigned int; using u64 = unsigned long long;
#define ASSERT_MSG(x,...) do { if (!(x)) std::abort(); } while(false)
struct QueueMutex { static inline bool held{}; void lock() { held=true; } void unlock() { held=false; } };
struct SubmitMutex { static inline bool held{}; void lock() { held=true; } void unlock() { held=false; } };
struct StateMutex { void lock() { if (QueueMutex::held || SubmitMutex::held) std::abort(); } void unlock() {} };
bool mutate_in_build{};
namespace EmulatorSettingsImpl { bool Flag(const char*,bool) { return true; } }
struct Settings { bool copy{}; bool IsCopyGpuBuffers() const { return copy; } } EmulatorSettings;
namespace Vulkan {
''' + types + r'''
namespace GpuPhy03 {
''' + pod + r'''
struct State { bool submission_enabled{true}; std::atomic<bool> active{false}; StateMutex mutex;
    std::shared_ptr<DrawTraceWindow> trace_window = std::make_shared<DrawTraceWindow>(true); };
State& Get() { static State state; return state; }
}
struct Prep {
    std::vector<u64> enqueued;
    std::unique_ptr<int> Build(std::span<const u32> dcb) {
        if (mutate_in_build && !dcb.empty()) const_cast<u32*>(dcb.data())[0] ^= 1;
        return std::make_unique<int>(1);
    }
    void Enqueue(u64 seq,std::unique_ptr<int>) { enqueued.push_back(seq); }
};
class Rasterizer {
public:
    Prep prep;
    Prep& GetDrawPreparation() { return prep; }
    bool RelicSubmissionTraceActive() const;
    static RelicCommandSample SampleRelicCommands(std::span<const u32>);
    void RecordRelicSubmission(const RelicSubmissionSample&);
    void TraceRelicConsumption(u32,std::span<const u32>,std::span<const u32>,u64);
};
''' + helper + r'''
}
namespace AmdGpu {
class Liverpool {
public:
    static constexpr u64 NoSeq = ~0ull; static constexpr u32 GfxQueueId = 0;
    struct Queue { QueueMutex m_access; std::vector<u32> dcb_buffer,ccb_buffer;
        u32 dcb_buffer_offset{},ccb_buffer_offset{}; std::queue<u64> submits; };
    std::array<Queue,1> mapped_queues;
    Vulkan::Rasterizer* rasterizer{}; u64 gfx_submit_seq{};
    SubmitMutex submit_mutex; std::condition_variable submit_cv; u32 num_submits{}; bool work_retired{true};
    struct Task { u64 handle; };
    std::vector<std::tuple<std::span<const u32>,std::span<const u32>,u64>> handed;
    using CmdBuffer = std::pair<std::span<const u32>,std::span<const u32>>;
    Liverpool() { mapped_queues[0].dcb_buffer.reserve(1024); mapped_queues[0].ccb_buffer.reserve(1024); }
    Task ProcessGraphics(std::span<const u32> dcb,std::span<const u32> ccb,u64 seq) {
        handed.emplace_back(dcb,ccb,seq); return {seq}; // no real coroutine
    }
    CmdBuffer CopyCmdBuffers(std::span<const u32>,std::span<const u32>);
    void SubmitGfx(std::span<const u32>,std::span<const u32>);
};
''' + submit + r'''
}
int main(int argc,char** argv) {
    if (argc != 2) return 10;
    const std::string mode = argv[1];
    if (mode == "budget") {
        std::vector<u32> big(262145,7);
        auto over = Vulkan::Rasterizer::SampleRelicCommands(big);
        if (over.status != 2 || over.hash || over.words != big.size()) return 11;
        auto limit = Vulkan::Rasterizer::SampleRelicCommands(std::span<const u32>(big).first(262144));
        if (limit.status != 1 || limit.words != 262144) return 12;
        auto empty = Vulkan::Rasterizer::SampleRelicCommands({});
        if (empty.status != 1 || empty.words) return 13;
        return 0;
    }
    EmulatorSettings.copy = mode == "copy";
    mutate_in_build = mode == "mutate";
    std::array<u32,4> dcb{1,2,3,4},ccb{5,6,7,8};
    for (bool enabled : {false,true}) {
        dcb = {1,2,3,4}; ccb = {5,6,7,8};
        Vulkan::Rasterizer rasterizer; AmdGpu::Liverpool engine; engine.rasterizer = &rasterizer;
        auto& state = Vulkan::GpuPhy03::Get(); state.active = enabled;
        state.trace_window = std::make_shared<Vulkan::GpuPhy03::DrawTraceWindow>(true);
        engine.SubmitGfx(dcb,ccb); engine.SubmitGfx(dcb,ccb);
        if (engine.num_submits != 2 || engine.work_retired || engine.gfx_submit_seq != 2 ||
            engine.mapped_queues[0].submits.size() != 2 || rasterizer.prep.enqueued != std::vector<u64>{0,1}) return 14;
        auto& window = *state.trace_window->pm4;
        if (!enabled) { if (window.submission_used) return 15; continue; }
        if (window.submission_used != 2 || window.submission_dropped) return 16;
        const auto& first = window.submissions[0];
        if (first.submission != 0 || first.kind != 1 || first.copy_mode != (EmulatorSettings.copy ? 2 : 1)) return 17;
        if (first.samples[0].hash != first.samples[2].hash || first.samples[1].hash != first.samples[3].hash) return 18;
        const auto [actual_dcb,actual_ccb,seq] = engine.handed[0];
        if (actual_dcb.data() == dcb.data() ? EmulatorSettings.copy : !EmulatorSettings.copy) return 19;
        rasterizer.TraceRelicConsumption(2,actual_dcb,actual_ccb,seq);
        if (window.submission_used != 3 || window.submissions[2].kind != 2) return 20;
        const auto& at_parser = window.submissions[2].samples[0];
        if (at_parser.address != first.samples[2].address || at_parser.words != first.samples[2].words) return 21;
        // Two synthetic Build mutations restored the byte. Force a fresh mutation before another parser sample.
        const_cast<u32*>(actual_dcb.data())[0] ^= 2;
        rasterizer.TraceRelicConsumption(2,actual_dcb,actual_ccb,seq);
        if (window.submissions[3].samples[0].hash == at_parser.hash) return 22;
    }
    return 0;
}
'''
        path = folder / "submit.cpp"
        path.write_text(source, encoding="utf-8")
        cls.binary = folder / "submit.exe"
        cls.environment = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "ISS_"))}
        cls.environment["PATH"] = str(TOOLCHAIN / "bin") + os.pathsep + cls.environment.get("PATH", "")
        result = subprocess.run([str(COMPILER), "-std=c++23", "-O1", "-I"+str(TOOLCHAIN / "include"),
                                 str(path), "-o", str(cls.binary)], env=cls.environment,
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_copy_policy_queue_order_and_sampling_budget(self):
        for mode in ("borrowed", "copy", "mutate", "budget"):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], env=self.environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class SubmissionJoinTests(unittest.TestCase):
    def fixture(self, kind, seq=7):
        sample = {"address": 0x4000, "words": 4, "hash": "0000000000001234", "status": 1}
        empty = {"address": 0, "words": 0, "hash": "0000000000005678", "status": 1}
        unset = {"address": 0, "words": 0, "hash": "0"*16, "status": 0}
        return {"order": 0, "submission": seq, "kind": kind, "copy_mode": 1 if kind == 1 else 0,
                "samples": [sample, empty, copy.deepcopy(sample) if kind == 1 else unset,
                            copy.deepcopy(empty) if kind == 1 else copy.deepcopy(unset)]}

    def window(self, record, observation):
        data = {"contract": "command_submission_samples_v1", "hash_algorithm": "xxh3_64",
                "sample_limit_words": 262144, "atomicity": "not_proven",
                "association": "submission_id_across_prepare_intervals", "status": "complete",
                "dropped": 0, "records": [record]}
        return {"observation_index": observation, "present_index": observation+100,
                "submission_samples": analyzer.validate_submission_samples(data)}

    def test_join_across_frames_detects_change_and_missing_or_ambiguous_identity(self):
        submit, parser = self.fixture(1), self.fixture(2)
        result = analyzer.compare_submission_samples([self.window(parser, 0), self.window(submit, 1)])
        row = result["results"][0]
        self.assertEqual(row["association"], "matched_submission_id")
        self.assertEqual(row["buffers"][0]["ready_to_parser"], "sample_hash_equal")
        parser["samples"][0]["hash"] = "0000000000009999"
        result = analyzer.compare_submission_samples([self.window(submit, 0), self.window(parser, 1)])
        self.assertEqual(result["results"][0]["buffers"][0]["ready_to_parser"], "sample_hash_changed")
        result = analyzer.compare_submission_samples([self.window(parser, 0)])
        self.assertEqual(result["results"][0]["association"], "missing_submission_or_parser_sample")
        result = analyzer.compare_submission_samples([self.window(submit, 0), self.window(submit, 1)])
        self.assertEqual(result["results"][0]["association"], "ambiguous_submission_identity")
        result = analyzer.compare_submission_samples([self.window(self.fixture(1, 2**64-1), 0)])
        self.assertEqual(result["results"][0]["association"], "ambiguous_submission_identity")
