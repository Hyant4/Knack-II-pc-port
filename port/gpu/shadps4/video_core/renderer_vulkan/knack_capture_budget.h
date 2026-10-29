// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace Vulkan::KnackCapture {
enum class Role : unsigned { GBuffer, Intermediate, Final };

// Independent quotas reserve room for late composition even after early targets fill up.
struct Budget {
    static constexpr std::uint64_t MiB = 1024 * 1024;
    static constexpr std::array<std::uint64_t, 3> ByteLimits{64 * MiB, 16 * MiB, 48 * MiB};
    static constexpr std::array<unsigned, 3> ImageLimits{5, 4, 4};
    std::array<std::uint64_t, 3> bytes{};
    std::array<unsigned, 3> images{};
    struct Seen { std::uint64_t address; Role role; };
    std::array<Seen, 13> seen{};
    unsigned count{};

    bool Reserve(Role role, std::uint64_t address, std::uint64_t size) {
        const auto slot = static_cast<unsigned>(role);
        if (slot >= bytes.size() || !address || !size || count == seen.size() ||
            images[slot] == ImageLimits[slot] || size > ByteLimits[slot] - bytes[slot]) return false;
        for (unsigned i = 0; i < count; ++i)
            if (seen[i].address == address && seen[i].role == role) return false;
        seen[count++] = {address, role};
        bytes[slot] += size;
        ++images[slot];
        return true;
    }
};

inline std::optional<std::uint64_t> ImageBytes(std::uint32_t width, std::uint32_t height,
                                              std::uint32_t pitch, unsigned bytes_per_pixel) {
    if (!width || !height || width > 4096 || height > 4096 || pitch < width || pitch > 8192 ||
        (bytes_per_pixel != 1 && bytes_per_pixel != 2 && bytes_per_pixel != 4 &&
         bytes_per_pixel != 8 && bytes_per_pixel != 16)) return {};
    const auto bytes = std::uint64_t(pitch) * height * bytes_per_pixel;
    if (bytes > 64 * Budget::MiB) return {};
    return bytes;
}
} // namespace Vulkan::KnackCapture
