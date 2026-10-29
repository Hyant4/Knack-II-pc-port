"""Run the production Stage body against Vulkan call spies, without a Vulkan device.

Real Vulkan/VMA types are used; allocation, scheduler and Vulkan dispatch are synthetic.
This proves CPU command ordering, restoration and cleanup, not GPU driver validation.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path(os.environ.get("MSYS2_ROOT") or Path(os.environ.get("LOCALAPPDATA", "")) / "knack2-toolchain/msys64") / "clang64"
COMPILER = TOOLCHAIN / "bin/clang++.exe"


@unittest.skipUnless(COMPILER.is_file(), "existing Windows Clang toolchain unavailable")
class StageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="gpu-phy03-stage-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        text = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp").read_text()
        production = text[text.index("namespace GpuPhy03 {"):text.index("\n};\nIdentity Identify") + 4]
        header = (ROOT / "port/gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h").read_text(encoding="utf-8")
        types = header[header.index("struct RelicCommandSample {"):header.index("class GraphicsPipeline;")]
        production = production.replace("namespace GpuPhy03 {", "namespace GpuPhy03 {\n" + types, 1)
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
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <fmt/format.h>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
using u8 = unsigned char; using u32 = unsigned int; using u64 = unsigned long long;
bool rendering = true, copy_inside_rendering = false, fail_allocate = false;
bool fail_mapping = false, fail_invalidate = false;
u32 allocations{}, destroys{}, copies{}, host_barriers{}, ends{};
unsigned char pixels[16]{};
extern "C" VkResult vmaCreateBuffer(VmaAllocator, const VkBufferCreateInfo* ci,
    const VmaAllocationCreateInfo*, VkBuffer* buffer, VmaAllocation* allocation, VmaAllocationInfo* info) {
    if (fail_allocate) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (ci->size != sizeof(pixels)) return VK_ERROR_OUT_OF_HOST_MEMORY;
    ++allocations;
    *buffer = reinterpret_cast<VkBuffer>(101);
    *allocation = reinterpret_cast<VmaAllocation>(102);
    info->pMappedData = fail_mapping ? nullptr : pixels;
    return VK_SUCCESS;
}
extern "C" void vmaDestroyBuffer(VmaAllocator, VkBuffer, VmaAllocation) { ++destroys; }
extern "C" VkResult vmaInvalidateAllocation(VmaAllocator, VmaAllocation, VkDeviceSize, VkDeviceSize) {
    return fail_invalidate ? VK_ERROR_MEMORY_MAP_FAILED : VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL Copy(VkCommandBuffer, VkImage, VkImageLayout layout, VkBuffer,
                               uint32_t count, const VkBufferImageCopy* regions) {
    ++copies;
    copy_inside_rendering |= rendering;
    if (layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL || count != 1 ||
        regions->bufferRowLength != 0 || regions->imageExtent.width != 2 ||
        regions->imageExtent.height != 2) std::abort();
}
VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer, const VkDependencyInfo* info) {
    if (info->bufferMemoryBarrierCount != 1) std::abort();
    const auto& b = *info->pBufferMemoryBarriers;
    if (b.srcStageMask != VK_PIPELINE_STAGE_2_COPY_BIT ||
        b.srcAccessMask != VK_ACCESS_2_TRANSFER_WRITE_BIT ||
        b.dstStageMask != VK_PIPELINE_STAGE_2_HOST_BIT || b.dstAccessMask != VK_ACCESS_2_HOST_READ_BIT)
        std::abort();
    ++host_barriers;
}
struct Scheduler {
    std::vector<std::function<void()>> callbacks;
    void EndRendering() { rendering = false; ++ends; }
    template<class F> void Record(F&& f) { f(vk::CommandBuffer{reinterpret_cast<VkCommandBuffer>(103)}); }
    u64 CurrentTick() { return 7; }
    template<class F> void DeferPriorityOperation(F&& f) { callbacks.emplace_back(std::forward<F>(f)); }
};
namespace VideoCore {
using ImageId = u32;
struct ImageState {
    vk::PipelineStageFlags2 pl_stage;
    vk::AccessFlags2 access_mask;
    vk::ImageLayout layout;
    bool operator==(const ImageState&) const = default;
};
struct Backing {
    struct { vk::ImageCreateInfo image_ci; } image;
    ImageState state;
    std::vector<ImageState> subresource_states;
};
struct Image {
    Backing storage{};
    Backing* backing = &storage;
    struct { u64 guest_address = 0x1000; u64 guest_size = 16; } info;
    vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor;
    bool scene_proxy{};
    u64 image_uid = 1, alias_generation = 2;
    vk::Image GetImage() const { return vk::Image{reinterpret_cast<VkImage>(104)}; }
};
}
struct Runtime {
    Scheduler& scheduler;
    bool pending{};
    u32 barriers{}, flushes{}, transits{};
    std::vector<VideoCore::ImageState> destinations;
    void FlushBarriers() {
        ++flushes;
        if (!pending) return; // actual Runtime has this early return
        ++barriers; pending = false; scheduler.EndRendering();
    }
    void Transit(VideoCore::Image* image, vk::ImageLayout layout, vk::PipelineStageFlags2 stage,
                 vk::AccessFlags2 access) {
        ++transits;
        VideoCore::ImageState next{stage, access, layout};
        pending |= !(image->backing->state == next);
        image->backing->state = next; destinations.push_back(next);
    }
};
struct Instance {
    VmaAllocator GetAllocator() const { return reinterpret_cast<VmaAllocator>(105); }
};
'''
        source += production + "\n}\n}\n"
        source += r'''
int main(int argc, char** argv) {
    if (argc != 3) return 10;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdCopyImageToBuffer = Copy;
    VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPipelineBarrier2 = Barrier;
    const std::string mode = argv[2];
    GpuPhy03::State state;
    state.dir = argv[1]; state.active = true;
    auto epoch = std::make_shared<GpuPhy03::Epoch>(); state.epochs.push_back(epoch);
    VideoCore::Image image;
    image.storage.image.image_ci = {
        .imageType = vk::ImageType::e2D, .format = vk::Format::eR8G8B8A8Unorm,
        .extent = {2, 2, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eTransferSrc};
    image.storage.state = mode == "empty-barriers"
        ? VideoCore::ImageState{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead,
                                vk::ImageLayout::eTransferSrcOptimal}
        : VideoCore::ImageState{vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                                vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal};
    const auto original = image.storage.state;
    fail_allocate = mode == "allocation-failure";
    fail_mapping = mode == "mapping-failure";
    fail_invalidate = mode == "invalidate-failure";
    if (mode == "budget-failure") state.outstanding = state.StagingBudget;
    if (mode == "undefined") image.storage.state.layout = vk::ImageLayout::eUndefined;
    Scheduler scheduler; Runtime runtime{scheduler}; Instance instance;
    bool staged = state.Stage(instance, scheduler, runtime, image, epoch, "gbuffer");
    if (mode == "budget-failure")
        return !staged && !copies && !allocations && !ends && state.outstanding == state.StagingBudget ? 0 : 11;
    if (mode == "undefined" || fail_allocate || fail_mapping)
        return !staged && !copies && !state.outstanding && destroys == allocations && !state.active ? 0 : 12;
    if (!staged || copy_inside_rendering || copies != 1 || !ends || host_barriers != 1 ||
        !(image.storage.state == original) || runtime.transits != 2 ||
        runtime.destinations[0].layout != vk::ImageLayout::eTransferSrcOptimal ||
        runtime.destinations[0].access_mask != vk::AccessFlagBits2::eTransferRead ||
        runtime.destinations[1] != original || state.outstanding != 16 || destroys ||
        scheduler.callbacks.size() != 1 || epoch->snapshots[0].saved) return 13;
    if (mode == "empty-barriers" && runtime.barriers != 0) return 14;
    scheduler.callbacks[0](); // simulated completed tick, never a Vulkan queue wait
    if (destroys != allocations || state.outstanding) return 15;
    if (fail_invalidate) return !state.active && !epoch->snapshots[0].saved ? 0 : 16;
    return epoch->snapshots[0].saved && std::filesystem::file_size(state.dir / "gbuffer_000.raw") == 16 ? 0 : 17;
}
'''
        cpp = cls.directory / "stage.cpp"
        cpp.write_text(source, encoding="utf-8")
        cls.executable = cls.directory / "stage.exe"
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
        directory = self.directory / mode
        directory.mkdir()
        result = subprocess.run([str(self.executable), str(directory), mode], env=self.environment,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_end_rendering_with_empty_barriers_and_noop_transitions(self):
        self.run_case("empty-barriers")

    def test_color_write_copy_restore_and_deferred_lifetime(self):
        self.run_case("normal")

    def test_failure_cleanup(self):
        for mode in ("allocation-failure", "mapping-failure", "invalidate-failure", "undefined", "budget-failure"):
            with self.subTest(mode=mode):
                self.run_case(mode)


if __name__ == "__main__":
    unittest.main()
