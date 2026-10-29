// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic constants and native TAA reprojection math. No game or Vulkan device.
#include "video_core/renderer_vulkan/knack_temporal.h"
#include <cstdio>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
    namespace K = Vulkan::KnackTemporal;
    using Size = Vulkan::SceneResolution::Size;
    // Auto preserves the release's preset dimensions; explicit raster size is exact,
    // independent of provider, preset and presentation size. No FSR downscale request.
    for (int preset = 0; preset < 4; ++preset) {
        const auto off = K::PlanResolution({0, 0}, false, preset);
        CHECK(off.raster == Size{} && off.temporal == Size{});
        const auto automatic = K::PlanResolution({0, 0}, true, preset);
        CHECK(automatic.raster == Vulkan::SceneResolution::ForPreset(preset));
        CHECK(automatic.temporal == Size{});
        for (auto requested : {Size{1280, 720}, Size{1600, 900}, Size{1920, 1080},
                               Size{2560, 1440}, Size{3840, 2160}}) {
            for (bool active : {false, true}) {
                const auto plan = K::PlanResolution(requested, active, preset);
                CHECK(plan.raster == requested);
                CHECK(plan.temporal.width >= plan.raster.width);
                CHECK(plan.temporal.height >= plan.raster.height);
                CHECK(plan.temporal.width >= 1920 && plan.temporal.height >= 1080);
                CHECK(Vulkan::SceneResolution::Unpack(Vulkan::SceneResolution::Pack(plan.raster)) == requested);
            }
        }
    }
    std::array<float, 108> data{};
    for (unsigned i = 0; i < 4; ++i) data[16 + i * 5] = data[48 + i * 5] = 1;
    data[28] = -10; data[29] = -20; data[30] = -30;
    data[60] = 10; data[61] = 20; data[62] = 30;
    data[32] = 1.358f; data[37] = 2.41421f;
    data[42] = -6000.f / 5996.f; data[43] = -1;
    data[46] = -24000.f / 5996.f;
    data[84] = -data[42]; data[85] = data[46]; data[86] = 4; data[87] = 6000;
    data[88] = 1920; data[89] = 1080;
    data[40] = -2 * .25f / 1920; data[41] = -2 * -.125f / 1080;
    K::Camera camera;
    CHECK(K::ParseCamera(data, camera));
    CHECK(camera.size[0] == 1920 && camera.size[1] == 1080);
    CHECK(std::abs(camera.jitter[0] - .25f) < 1e-5f);
    CHECK(std::abs(camera.jitter[1] + .125f) < 1e-5f);
    CHECK(camera.near_plane == 4 && camera.far_plane == 6000);
    CHECK(camera.projection[2] > 1 && camera.projection[3] < 0);
    CHECK(camera.view[11] == 30 && camera.inverse[10] == -1);
    const auto original = data;
    for (unsigned slot : {32u, 43u, 63u, 86u, 88u}) {
        data = original; data[slot] = std::numeric_limits<float>::quiet_NaN();
        CHECK(!K::ParseCamera(data, camera));
    }
    data = original; data[60] += 1;
    CHECK(!K::ParseCamera(data, camera));
    data = original; data[43] = 1;
    CHECK(!K::ParseCamera(data, camera));
    CHECK(!K::ParseCamera(std::span<const float>(original.data(), 107), camera));
    const auto stationary = K::MotionPixels({.25f, -.125f}, {.25f, -.125f}, {1, 1});
    CHECK(stationary[0] == 0 && stationary[1] == 0);
    const auto moving = K::MotionPixels({4.25f, -2.125f}, {.25f, -.125f}, {.5f, .5f});
    CHECK(moving[0] == -2 && moving[1] == -1);
    std::puts("Knack camera contract and native motion conversion: OK");
}
