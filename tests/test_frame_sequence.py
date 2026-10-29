"""Final-frame capture: production state/readback against CPU Vulkan/VMA spies."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get("MSYS2_ROOT") or Path(os.environ.get("LOCALAPPDATA", "")) / "knack2-toolchain/msys64") / "clang64"
COMPILER = TOOLCHAIN / "bin/clang++.exe"
SPEC = importlib.util.spec_from_file_location("sequence_analyzer", ROOT / "scripts/gpu_phy03_analyze.py")
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)


@unittest.skipUnless(COMPILER.is_file(), "existing Clang toolchain unavailable")
class SequenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="frame-sequence-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        text = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp").read_text()
        start = text.index("namespace FrameSequence {", text.index("// Final game frames"))
        end = text.index("} // namespace FrameSequence", start) + len("} // namespace FrameSequence")
        state = text[start:end]
        start = text.index("static void DumpRaw(")
        end = text.index("Frame* Presenter::PrepareFrame", start)
        readback = text[start:end]
        source = r'''
#define VK_ENABLE_BETA_EXTENSIONS
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_STRUCT_SETTERS
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_ASSERT_ON_RESULT
#include <vulkan/vulkan.hpp>
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>
#include <fmt/format.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <cstdlib>
#include <cstdio>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
using u8 = unsigned char; using u32 = unsigned int; using u64 = unsigned long long;
struct Frame { vk::Image image; };
bool fail_allocate{}, fail_mapping{}, fail_invalidate{};
u32 allocations{}, destroys{}, copies{}, host_barriers{};
u8 pixels[16] = {0, 16, 32, 255, 48, 64, 80, 255, 96, 112, 128, 255, 144, 160, 176, 255};
extern "C" VkResult vmaCreateBuffer(VmaAllocator, const VkBufferCreateInfo* ci,
    const VmaAllocationCreateInfo*, VkBuffer* buffer, VmaAllocation* allocation, VmaAllocationInfo* info) {
    if (fail_allocate) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (ci->size != 16 || ci->usage != VK_BUFFER_USAGE_TRANSFER_DST_BIT) std::abort();
    ++allocations; *buffer = reinterpret_cast<VkBuffer>(101); *allocation = reinterpret_cast<VmaAllocation>(102);
    info->pMappedData = fail_mapping ? nullptr : pixels;
    return VK_SUCCESS;
}
extern "C" void vmaDestroyBuffer(VmaAllocator, VkBuffer, VmaAllocation) { ++destroys; }
extern "C" VkResult vmaInvalidateAllocation(VmaAllocator, VmaAllocation, VkDeviceSize, VkDeviceSize) {
    return fail_invalidate ? VK_ERROR_MEMORY_MAP_FAILED : VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL Copy(VkCommandBuffer, VkImage, VkImageLayout layout, VkBuffer,
                               uint32_t count, const VkBufferImageCopy* region) {
    if (layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || count != 1 || region->bufferRowLength ||
        region->imageExtent.width != 2 || region->imageExtent.height != 2) std::abort();
    ++copies;
}
VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer, const VkDependencyInfo* info) {
    if (info->bufferMemoryBarrierCount != 1) std::abort();
    const auto& b = *info->pBufferMemoryBarriers;
    if (b.srcStageMask != VK_PIPELINE_STAGE_2_COPY_BIT || b.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
        b.dstStageMask != VK_PIPELINE_STAGE_2_HOST_BIT || b.dstAccessMask != VK_ACCESS_2_HOST_READ_BIT ||
        b.srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED || b.dstQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED)
        std::abort();
    ++host_barriers;
}
struct Instance { VmaAllocator GetAllocator() const { return reinterpret_cast<VmaAllocator>(1); } };
struct Scheduler {
    std::vector<std::function<void()>> callbacks;
    template<class F> void DeferPriorityOperation(F&& f) { callbacks.emplace_back(std::forward<F>(f)); }
};
'''
        source += state + "\n" + readback
        source += r'''
int main(int argc, char** argv) {
    if (argc != 3) return 10;
    const std::filesystem::path base = argv[1], output = base / "frames", trigger = base / "request";
    const std::string mode = argv[2];
    _putenv_s("TLG_FRAME_SEQUENCE_TRIGGER", trigger.string().c_str());
    _putenv_s("TLG_FRAME_SEQUENCE_DIR", output.string().c_str());
    _putenv_s("TLG_FRAME_SEQUENCE_COUNT", mode == "invalid-count" ? "17" : "16");
    if (mode == "existing") { std::filesystem::create_directory(output); std::ofstream(output / "sentinel") << "keep"; }
    std::ofstream(trigger) << "capture";
    FrameSequence::State state; state.Poll();
    if (mode == "existing") return state.error == "output_directory_not_fresh" &&
        std::filesystem::file_size(output / "sentinel") == 4 ? 0 : 11;
    if (mode == "invalid-count") return state.error == "invalid_frame_count" ? 0 : 12;
    if (!state.error.empty() || !state.requested) return 13;
    state.Poll(); // no rearming or truncation
    if (mode == "minimized") { state.Fail("window_minimized"); return !state.completed ? 0 : 14; }
    if (mode == "format") return !state.Begin(2, 2, vk::Format::eUndefined, 7, false) &&
        state.error == "unsupported_output_format" ? 0 : 15;
    if (mode == "extent") return !state.Begin(~0u, ~0u, vk::Format::eB8G8R8A8Unorm, 7, false) &&
        state.error == "invalid_output_extent" ? 0 : 16;
    if (mode == "budget") {
        const auto first = state.Begin(8192, 4096, vk::Format::eB8G8R8A8Unorm, 7, false);
        const auto second = state.Begin(2, 2, vk::Format::eB8G8R8A8Unorm, 8, false);
        if (!first || second || state.outstanding != state.Budget || state.error != "staging_budget_exceeded") return 17;
        state.Saved(first, false, 0);
        return state.outstanding == 0 ? 0 : 18;
    }
    if (mode == "registered-format") {
        Frame frame{vk::Image{reinterpret_cast<VkImage>(105)}};
        FrameSequence::RegisterFrame(&frame, vk::Format::eB8G8R8A8Unorm);
        if (FrameSequence::Format(&frame) != vk::Format::eB8G8R8A8Unorm) return 19;
        frame.image = vk::Image{reinterpret_cast<VkImage>(106)};
        if (FrameSequence::Format(&frame) != vk::Format::eUndefined) return 20;
        FrameSequence::RegisterFrame(&frame, vk::Format::eA2B10G10R10UnormPack32);
        return FrameSequence::Format(&frame) == vk::Format::eA2B10G10R10UnormPack32 ? 0 : 21;
    }
    fail_allocate = mode == "allocate"; fail_mapping = mode == "mapping";
    fail_invalidate = mode == "invalidate";
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdCopyImageToBuffer = Copy;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPipelineBarrier2 = Barrier;
    Instance instance; Scheduler scheduler;
    const auto cmdbuf = vk::CommandBuffer{reinterpret_cast<VkCommandBuffer>(103)};
    const auto image = vk::Image{reinterpret_cast<VkImage>(104)};
    const bool complete = mode == "complete" || mode == "reused" || mode == "saved-first";
    const u32 count = complete ? 16 : mode == "gap" ? 2 : 1;
    for (u32 i = 0; i < count; ++i) {
        auto e = state.Begin(2, 2, vk::Format::eB8G8R8A8Unorm, 7+i, mode == "reused" && i % 2);
        if (!e) return 22;
        auto file = output / fmt::format("output_{:03}.raw", i);
        if (mode == "write") file = output / "missing" / "output.raw";
        DumpRaw(instance, scheduler, cmdbuf, image, vk::ImageLayout::eTransferSrcOptimal, 2, 2, "output", i,
            file, [&state, e](bool success, u64 hash) { state.Saved(e, success, hash); });
        if (mode == "saved-first") {
            auto callback = std::move(scheduler.callbacks.back()); scheduler.callbacks.pop_back(); callback();
            if (e->published || state.completed) return 23;
        }
        state.Presented(e, mode != "present");
        if (mode == "gap" && !i) state.Presented({}, true);
    }
    if (!scheduler.callbacks.empty() && state.completed) return 24;
    for (auto it = scheduler.callbacks.rbegin(); it != scheduler.callbacks.rend(); ++it) (*it)();
    if (allocations != destroys || state.outstanding) return 25;
    if (complete) return state.completed && state.error.empty() && copies == 16 && host_barriers == 16 ? 0 : 26;
    return !state.error.empty() && !state.completed && !std::filesystem::exists(output / "frame_000.json") ? 0 : 27;
}
'''
        cpp = cls.directory / "sequence.cpp"
        cpp.write_text(source, encoding="utf-8")
        cls.executable = cls.directory / "sequence.exe"
        cls.environment = {k: v for k, v in os.environ.items() if not k.startswith(("TLG_", "ISS_"))}
        cls.environment["PATH"] = str(TOOLCHAIN / "bin") + os.pathsep + cls.environment.get("PATH", "")
        command = [str(COMPILER), "-std=c++23", "-DFMT_HEADER_ONLY",
                   "-I" + str(ROOT / "port/gpu/third_party/vulkan-headers/include"),
                   "-I" + str(TOOLCHAIN / "include/vma"), "-I" + str(TOOLCHAIN / "include"),
                   str(cpp), "-o", str(cls.executable)]
        result = subprocess.run(command, env=cls.environment, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_case(self, mode):
        base = self.directory / mode
        base.mkdir()
        result = subprocess.run([str(self.executable), str(base), mode], env=self.environment,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return base / "frames"

    def test_sixteen_final_frames_validate_and_preview_without_any_scene_metadata(self):
        for mode in ("complete", "reused", "saved-first"):
            with self.subTest(mode=mode):
                capture = self.run_case(mode)
                previews = self.directory / (mode + "-png")
                result = analyzer.validate(capture, previews=previews)
                self.assertEqual(result["frames"], 16)
                self.assertEqual(result["classification"], "final_frames_only_no_stage_localization")
                self.assertFalse(result["visibility_automatic"])
                self.assertEqual(sum(x["reused"] for x in result["results"]), 8 if mode == "reused" else 0)
                self.assertEqual(len(list(previews.glob("*.png"))), 16)

    def test_readback_and_presentation_failures_preserve_failure_and_release_staging(self):
        for mode in ("allocate", "mapping", "invalidate", "write", "present", "gap", "minimized", "budget"):
            with self.subTest(mode=mode):
                capture = self.run_case(mode)
                with self.assertRaisesRegex(analyzer.CaptureError, "incomplete_session"):
                    analyzer.validate(capture)

    def test_fresh_directory_valid_count_format_extent_and_backing_required(self):
        for mode in ("existing", "invalid-count", "format", "extent", "registered-format"):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_analyzer_rejects_missing_corrupt_nonconsecutive_or_mislabelled_data(self):
        # Distinct native invocations keep each malformed fixture independent.
        for kind in ("hash", "missing", "gap", "format", "count", "path", "boundary", "reuse", "labels"):
            with self.subTest(kind=kind):
                base = self.directory / ("mutate-" + kind)
                base.mkdir()
                process = subprocess.run([str(self.executable), str(base), "complete"], env=self.environment,
                                         capture_output=True, text=True, timeout=10)
                self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                capture = base / "frames"
                path = capture / "frame_001.json"
                frame = json.loads(path.read_text())
                session_path = capture / "session.json"
                session = json.loads(session_path.read_text())
                if kind == "hash": (capture / "output_001.raw").write_bytes(b"X" * 16)
                if kind == "missing": (capture / "output_001.raw").unlink()
                if kind == "gap": frame["present_index"] += 1
                if kind == "format": frame["format"] = "Undefined"
                if kind == "count": session["saved"] -= 1
                if kind == "path": frame["file"] = "../output.raw"
                if kind == "boundary": session["boundary"] = "unknown"
                if kind == "reuse": frame["reused"] = 1
                path.write_text(json.dumps(frame))
                session_path.write_text(json.dumps(session))
                previews = base / "previews"
                with self.assertRaises(analyzer.CaptureError):
                    analyzer.validate(capture, labels={} if kind == "labels" else None, previews=previews)
                self.assertFalse(previews.exists())

    def test_present_hook_uses_real_frame_after_blit_and_before_restore_with_existing_wait(self):
        source = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_presenter.cpp").read_text()
        present = source[source.index("void Presenter::Present(Frame*"):source.index("Frame* Presenter::GetRenderFrame")]
        capture = present.index("sequence_entry = sequence.Begin")
        self.assertLess(present.index("cmdbuf.blitImage(frame->image"), capture)
        self.assertLess(capture, present.index("const std::array post_barriers"))
        self.assertIn("FrameSequence::Format(frame)", present)
        self.assertIn("info.AddWait(frame->ready_semaphore, frame->ready_tick)", present)
        self.assertLess(present.index("scheduler.Flush(info)"), present.index("swapchain.Present()"))
        self.assertLess(present.index("swapchain.Present()"), present.index("FrameSequence::Get().Presented("))
        self.assertIn('FrameSequence::Get().Fail("window_minimized")', present)
        self.assertIn('FrameSequence::Get().Fail("swapchain_acquire_failed")', present)
        self.assertNotIn("GpuPhy03", source[source.index("// Final game frames"):source.index("static void DumpRaw")])


if __name__ == "__main__":
    unittest.main()
