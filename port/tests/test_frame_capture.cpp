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

int main(int argc, char**) {
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() /
        ("knack-frame-capture-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
#ifdef _WIN32
    _putenv_s("TLG_CAPTURE_GPU_FLIP", "1");
    _putenv_s("TLG_CAPTURE_SUBMIT_BOUNDARY", "1");
    _putenv_s("TLG_CAPTURE_DIR", directory.string().c_str());
#else
    setenv("TLG_CAPTURE_GPU_FLIP", "1", 1);
    setenv("TLG_CAPTURE_SUBMIT_BOUNDARY", "1", 1);
    setenv("TLG_CAPTURE_DIR", directory.string().c_str(), 1);
#endif
    if (argc > 1) {
#ifdef _WIN32
        _putenv_s("TLG_KNACK_CAPTURE", "1");
#else
        setenv("TLG_KNACK_CAPTURE", "1", 1);
#endif
        using namespace Vulkan::KnackCapture;
        assert(!ImageBytes(0, 1080, 1920, 4));
        assert(!ImageBytes(1920, 1080, 1919, 4));
        assert(!ImageBytes(0xffffffff, 0xffffffff, 0xffffffff, 16));
        assert(!ImageBytes(4096, 4096, 8192, 16));
        assert(ImageBytes(1920, 1080, 1920, 8) == 16588800);
        Budget budget;
        for (unsigned i = 0; i < 5; ++i)
            assert(budget.Reserve(Role::GBuffer, i + 1, (i == 4 ? 16 : 12) * Budget::MiB));
        assert(!budget.Reserve(Role::GBuffer, 99, 1));
        for (unsigned i = 0; i < 4; ++i) {
            assert(budget.Reserve(Role::Intermediate, i + 1, 4 * Budget::MiB));
            assert(budget.Reserve(Role::Final, i + 1, 12 * Budget::MiB));
        }
        assert(budget.count == 13);
        assert(budget.bytes[0] + budget.bytes[1] + budget.bytes[2] == 128 * Budget::MiB);
        assert(!budget.Reserve(Role::Final, 88, 1));
        Budget duplicate;
        assert(duplicate.Reserve(Role::GBuffer, 42, 128));
        assert(!duplicate.Reserve(Role::GBuffer, 42, 128));

        using Vulkan::FrameCapture;
        if (argc > 2) {
            fs::create_directories(directory / ".requested");
            FrameCapture::Request(); FrameCapture::OnFlip(0x1000); FrameCapture::OnSubmit();
            assert(!FrameCapture::Active());
            fs::remove(directory / ".requested"); fs::remove(directory);
            std::puts("bounded capture restart guard ok");
            return 0;
        }
        FrameCapture::Request(); FrameCapture::OnFlip(0x1000);
        FrameCapture::OnGpuFlip(0x1000); // host/stream flips must not double-close SubmitDone capture
        assert(FrameCapture::Active() && !FrameCapture::IsRecording());
        FrameCapture::OnSubmit();
        assert(FrameCapture::IsRecording() && FrameCapture::ImageEpoch() == 1);
        assert(FrameCapture::BeginGBuffer()); assert(!FrameCapture::BeginGBuffer());
        assert(FrameCapture::ReserveImage(Role::GBuffer, 42, 128));
        assert(!FrameCapture::ReserveImage(Role::GBuffer, 42, 128));
        for (unsigned i = 0; i < 700; ++i) {
            FrameCapture::Dispatch(i, 1, 1, 1);
            FrameCapture::Note("test");
        }
        FrameCapture::ImageResult("saved synthetic.raw bytes=128");
        FrameCapture::OnSubmit();
        assert(!FrameCapture::Active());
        FrameCapture::Request(); FrameCapture::OnFlip(0x2000); FrameCapture::OnSubmit();
        assert(!FrameCapture::Active());
        unsigned reports = 0;
        for (const auto& item : fs::directory_iterator(directory)) {
            if (item.is_directory()) {
                assert(item.path().filename() == ".requested");
                fs::remove(item.path());
                continue;
            }
            std::ifstream input(item.path());
            const std::string text{std::istreambuf_iterator<char>(input), {}};
            assert(text.find("census_truncated=1") != std::string::npos);
            assert(text.find("image attempts=1 bytes=128") != std::string::npos);
            assert(text.find("saved synthetic.raw") != std::string::npos);
            assert(text.find("#512 COMPUTE") == std::string::npos);
            input.close(); fs::remove(item.path()); ++reports;
        }
        assert(reports == 1); fs::remove(directory);
        std::puts("bounded frame-capture-test ok");
        return 0;
    }
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
    Vulkan::FrameCapture::Request();
    Vulkan::FrameCapture::OnFlip(0x3000);
    Vulkan::FrameCapture::OnSubmit();
    assert(Vulkan::FrameCapture::IsRecording());
    Vulkan::FrameCapture::BeginPass(nullptr, 0, nullptr, "submit-boundary");
    Vulkan::FrameCapture::Draw(0x1234, 0x5678, 3, 1);
    Vulkan::FrameCapture::OnSubmit();
    assert(!Vulkan::FrameCapture::Active());
    for (const auto& item : fs::directory_iterator(directory)) fs::remove(item.path());
    fs::remove(directory);
    std::puts("frame-capture-test ok");
    return 0;
}
