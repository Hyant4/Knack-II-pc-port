// SPDX-License-Identifier: GPL-2.0-or-later
// tlg: frame analyzer. Creating the file named by TLG_CAPTURE_TRIGGER records the next full
// frame: every render pass (targets, depth, draw count, shaders, sampled textures) and compute
// dispatch, in order, plus the buffer that was presented. Input for placing a temporal
// upscaler (scene color, depth, motion vectors, UI) in the game's frame.

#pragma once

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include "common/types.h"
#include "video_core/renderer_vulkan/knack_capture_budget.h"

namespace VideoCore {
struct ImageInfo;
}

namespace Vulkan {

class FrameCapture {
public:
    /// VideoOut thread, per flip: frame boundary, presented buffer, trigger file check.
    static void OnFlip(VAddr presented_address);
    /// Optional stream-ordered boundary for games that copy to the display instead of drawing.
    static void OnGpuFlip(VAddr address, bool submit_boundary = false);
    /// Opt-in SubmitDone boundary for profiles whose VideoOut flips run on a host thread.
    static void OnSubmit();

    /// VideoOut: a display buffer the game registered. A pass drawing into one starts a frame
    /// in the GPU command stream (the flip itself runs a frame behind).
    static void AddDisplayBuffer(VAddr address);
    static bool IsDisplayBuffer(VAddr address);

    /// GPU thread: whether passes are being looked at (armed or recording; cheap).
    static bool Active() {
        return state.load(std::memory_order_relaxed) != Idle;
    }
    /// GPU thread: whether a frame is being recorded right now.
    static bool IsRecording() {
        return state.load(std::memory_order_relaxed) == Recording;
    }
    /// GPU thread, at every draw/dispatch: starts or ends a recording at frame boundaries.
    static void Poll();

    /// `state`: the pass's census line (vk_pass_census.h), kept with the first pipeline that
    /// opened the pass.
    static void BeginPass(const VideoCore::ImageInfo* const* colors, u32 num_colors,
                          const VideoCore::ImageInfo* depth, std::string census);
    static void Draw(u64 vs_hash, u64 ps_hash, u32 num_indices, u32 num_instances);
    static void Dispatch(u64 cs_hash, u32 x, u32 y, u32 z);
    static void Sampled(const VideoCore::ImageInfo& info, bool storage);
    /// Contents of a bound buffer (first 1 KiB), kept for small passes and a pass's first draw.
    static void Buffer(u64 stage_hash, u32 slot, VAddr address, const void* data, u64 size);
    static void Note(const char* text);

    /// In-game menu: records the next full frame (as the trigger file does).
    static void Request();
    /// What the last request did, for the menu: empty, "Capturing...", the file or an error.
    static std::string LastResult();

    /// Opt-in issue #12 capture: one request per session, GPU-thread-owned budget.
    static bool BoundedImages();
    static u64 ImageEpoch();
    static bool BeginGBuffer();
    static bool HasGBuffer();
    static bool ReserveImage(KnackCapture::Role role, VAddr address, u64 bytes);
    static void ImageResult(const std::string& text);

private:
    enum : u32 { Idle, Armed, Recording };
    static inline std::atomic<u32> state{Idle};
    static inline std::atomic<u64> flips{0};
    static inline std::atomic<VAddr> last_presented{0};
    static inline std::atomic<bool> requested{false};
};

} // namespace Vulkan
