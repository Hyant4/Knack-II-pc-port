// SPDX-License-Identifier: GPL-2.0-or-later
// The frame analyzer's buffer dump (vk_frame_capture.cpp). A binding whose bytes the host cannot
// read (data null: a constant-ring copy of the flattened user data carries guest address 0,
// issue #174) is skipped instead of read through the null pointer. No game or Vulkan device.
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "video_core/renderer_vulkan/vk_frame_capture.h"

int main() {
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() /
        ("knack-frame-capture-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
#ifdef _WIN32
    _putenv_s("TLG_CAPTURE_GPU_FLIP", "1");
    _putenv_s("TLG_CAPTURE_DIR", directory.string().c_str());
#else
    setenv("TLG_CAPTURE_GPU_FLIP", "1", 1);
    setenv("TLG_CAPTURE_DIR", directory.string().c_str(), 1);
#endif
    Vulkan::FrameCapture::Request();
    Vulkan::FrameCapture::OnFlip(0x1000);
    assert(Vulkan::FrameCapture::Active() && !Vulkan::FrameCapture::IsRecording());
    Vulkan::FrameCapture::OnGpuFlip(0x1000);
    assert(Vulkan::FrameCapture::IsRecording());
    // No draw to a registered display buffer: the stream flip must still finish the trace.
    Vulkan::FrameCapture::BeginPass(nullptr, 0, nullptr, "offscreen-only");
    Vulkan::FrameCapture::Draw(0x1234, 0x5678, 3, 1);
    // A buffer with host bytes is dumped as before.
    std::array<float, 16> words{};
    Vulkan::FrameCapture::Buffer(0x1234, 0, 0x1000, words.data(), sizeof(words));
    // Flattened user data in the constant ring: host copy only, guest address 0, and here
    // nothing readable at all. Must not dereference `data`.
    Vulkan::FrameCapture::Buffer(0x1234, 1, 0, nullptr, 64);
    Vulkan::FrameCapture::OnGpuFlip(0x2000);
    assert(!Vulkan::FrameCapture::Active());
    unsigned files = 0;
    for (const auto& item : fs::directory_iterator(directory)) {
        std::ifstream input(item.path());
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        assert(text.find("presented buffer 0x2000") != std::string::npos);
        assert(text.find("PASS draws 1") != std::string::npos);
        assert(text.find("offscreen-only") != std::string::npos);
        input.close();
        fs::remove(item.path());
        ++files;
    }
    assert(files == 1);
    fs::remove(directory);
    std::puts("frame-capture-test ok");
    return 0;
}
