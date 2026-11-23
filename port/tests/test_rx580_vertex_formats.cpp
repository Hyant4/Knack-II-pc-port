// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "common/logging/log.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/rx580_vertex_formats.h"
#include "video_core/texture_cache/barrier_merge.h"

extern "C" { uint32_t runtime_disabled_optimizations = 0; }
void assert_fail_impl() { std::abort(); }
[[noreturn]] void unreachable_impl() { std::abort(); }
namespace Common::Log {
bool ShouldLog(Level) { return false; }
void Write(std::string_view, Level, const char*, int, const char*, std::string&&) {}
}

int main(int argc, char** argv) {
    const bool expected_preset = argc == 2 && std::strcmp(argv[1], "1") == 0;
    assert(VideoCore::Rx580PresetEnabled() == expected_preset);
    using namespace Shader::Gcn;
    std::array<u32, 4> code{0xC0000000u | (2u << 22) | (8u << 15) | (1u << 9) | (1u << 8),
                            0xE8000000u | (3u << 16), 2u << 16, 0xBE802000u};
    AmdGpu::Buffer buffer{};
    buffer.base_address = 0x12340;
    buffer.stride = 8;
    buffer.num_records = 64;
    buffer.data_format = u32(AmdGpu::DataFormat::Format16_16_16_16);
    buffer.num_format = u32(AmdGpu::NumberFormat::Float);
    std::array<u32, 4> ud{};
    const auto* code_pointer = code.data();
    const auto* buffer_pointer = &buffer;
    std::memcpy(ud.data(), &code_pointer, sizeof(code_pointer));
    std::memcpy(ud.data() + 2, &buffer_pointer, sizeof(buffer_pointer));
    Shader::Info info;
    info.has_fetch_shader = true;
    info.fetch_shader_sgpr_base = 0;
    info.user_data = ud;
    info.flattened_ud_buf.assign(ud.begin(), ud.end());
    FetchShaderSnapshot snapshot;
    snapshot.Capture(info);
    assert(snapshot.fetch && snapshot.fetch->attributes.size() == 1);
    const auto initial_ud = ud;
    const auto initial_flat = info.flattened_ud_buf;
    const auto initial_program = info.pgm_base;
    std::array<vk::Format, 32> expected{};
    const auto live_format = [&](std::size_t index) {
        const auto sharp = snapshot.fetch->attributes[index].GetSharp(info);
        return Vulkan::LiverpoolToVK::SurfaceFormat(sharp.GetDataFmt(), sharp.GetNumberFmt());
    };
    expected[0] = live_format(0);
    assert(expected[0] == vk::Format::eR16G16B16A16Sfloat);
    assert(Vulkan::Rx580PreparedFormatsMatch(true, false, expected, 1, live_format));

    // The real external V# changes. Registers, ordinary flat data and program do not.
    buffer.data_format = u32(AmdGpu::DataFormat::Format32_32_32_32);
    buffer.stride = 16;
    assert(ud == initial_ud && info.flattened_ud_buf == initial_flat);
    assert(info.pgm_base == initial_program);
    assert(live_format(0) == vk::Format::eR32G32B32A32Sfloat);
    assert(!Vulkan::Rx580PreparedFormatsMatch(true, false, expected, 1, live_format));
    assert(Vulkan::Rx580PreparedFormatsMatch(VideoCore::Rx580PresetEnabled(), false,
                                            expected, 1, live_format) == !expected_preset);
    expected[0] = live_format(0); // The regular key-refresh path chooses the current format.
    assert(Vulkan::Rx580PreparedFormatsMatch(true, false, expected, 1, live_format));

    // Changing an address or stride alone must not reject a format-compatible pipeline.
    buffer.base_address = 0x56780;
    buffer.stride = 32;
    assert(Vulkan::Rx580PreparedFormatsMatch(true, false, expected, 1, live_format));
    const auto no_guest_read = [](std::size_t) -> vk::Format { std::abort(); };
    assert(Vulkan::Rx580PreparedFormatsMatch(false, false, expected, 100, no_guest_read));
    assert(Vulkan::Rx580PreparedFormatsMatch(true, true, expected, 100, no_guest_read));
    assert(!Vulkan::Rx580PreparedFormatsMatch(true, false, expected, 33, no_guest_read));
    assert(!Vulkan::Rx580PreparedFormatsMatch(true, false, expected, 0, no_guest_read));
    expected.fill(vk::Format::eUndefined);
    assert(Vulkan::Rx580PreparedFormatsMatch(true, false, expected, 0, no_guest_read));
    std::puts("RX580 static formats: stale rejected, refreshed accepted, other paths do not read V#.");
}
