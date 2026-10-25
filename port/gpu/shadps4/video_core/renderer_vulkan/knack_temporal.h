// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <span>
#include <cstdint>
#include "video_core/renderer_vulkan/scene_resolution.h"

namespace Vulkan::KnackTemporal {
inline constexpr std::uint64_t TemporalShader = 0x5f9b889c;
inline constexpr std::uint64_t PresentShader = 0x446884a0;
struct ResolutionPlan {
    SceneResolution::Size raster;
    SceneResolution::Size temporal;
};
// Host-proxy planner (native QHD/4K bypass it). Manual size wins over the quality
// preset. FSR must never downscale:
// supersampling reconstructs at raster size, then resolves into native histories.
inline ResolutionPlan PlanResolution(SceneResolution::Size requested, bool temporal, int preset) {
    const auto raster = requested.width && requested.height ? requested :
        SceneResolution::ForPreset(temporal ? preset : 0);
    return {raster, {std::max(1920u, raster.width), std::max(1080u, raster.height)}};
}
struct Camera {
    std::array<float, 12> view{}, inverse{};
    std::array<float, 4> projection{};
    std::array<float, 2> jitter{};
    std::array<unsigned, 2> size{};
    float near_plane{}, far_plane{};
};

// CUSA08014/01.01 G-buffer VS scene constants: column-major VP, V, P, inverse V,
// inverse P, camera/depth parameters, native dimensions, previous VP (432 bytes).
inline bool ParseCamera(std::span<const float> data, Camera& out) {
    if (data.size() != 108) return false;
    for (float value : data) if (!std::isfinite(value)) return false;
    if (data[88] < 64 || data[88] > 8192 || data[89] < 64 || data[89] > 8192 ||
        std::floor(data[88]) != data[88] || std::floor(data[89]) != data[89] ||
        data[32] <= 0 || data[37] <= 0 || std::abs(data[43] + 1) > 1e-5f ||
        std::abs(data[31] - 1) > 1e-5f || std::abs(data[63] - 1) > 1e-5f ||
        data[86] <= 0 || data[87] <= data[86] ||
        std::abs(data[42] + data[84]) > 1e-4f || std::abs(data[46] - data[85]) > 1e-4f)
        return false;
    // Reject object/light constants sharing this size: view and inverse must agree.
    for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) {
        float product = 0;
        for (unsigned k = 0; k < 4; ++k) product += data[16 + k * 4 + r] * data[48 + c * 4 + k];
        if (std::abs(product - float(r == c)) > .03f) return false;
    }
    for (unsigned r = 0; r < 3; ++r) for (unsigned c = 0; c < 4; ++c) {
        out.view[r * 4 + c] = data[16 + c * 4 + r] * (r == 2 ? -1.f : 1.f);
        out.inverse[r * 4 + c] = data[48 + c * 4 + r] * (c == 2 ? -1.f : 1.f);
    }
    out.projection = {data[32], -data[37], -data[42], data[46]};
    out.size = {unsigned(data[88]), unsigned(data[89])};
    out.jitter = {-data[40] * data[88] * .5f, -data[41] * data[89] * .5f};
    out.near_plane = data[86];
    out.far_plane = data[87];
    return true;
}

// Native TAA disassembly: previous pixel = (x - (motion.x-jitter.x),
// y + (motion.y-jitter.y)). FSR needs previous minus current, in render pixels.
inline std::array<float, 2> MotionPixels(std::array<float, 2> native,
        std::array<float, 2> jitter, std::array<float, 2> scale) {
    return {(jitter[0] - native[0]) * scale[0], (native[1] - jitter[1]) * scale[1]};
}
} // namespace Vulkan::KnackTemporal
