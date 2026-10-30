// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <vulkan/vulkan.hpp>

namespace Vulkan {

// The caller supplies live formats lazily: the normal and dynamic-input paths
// must not read guest fetch descriptors as a side effect of this opt-in guard.
template <typename LiveFormat>
bool Rx580PreparedFormatsMatch(bool rx580_preset, bool dynamic_input,
                              std::span<const vk::Format> prepared_formats,
                              std::size_t attribute_count, LiveFormat&& live_format) {
    if (!rx580_preset || dynamic_input) {
        return true;
    }
    if (attribute_count > prepared_formats.size()) {
        return false;
    }
    for (std::size_t i = 0; i < attribute_count; ++i) {
        if (prepared_formats[i] != live_format(i)) {
            return false;
        }
    }
    for (std::size_t i = attribute_count; i < prepared_formats.size(); ++i) {
        if (prepared_formats[i] != vk::Format::eUndefined) {
            return false;
        }
    }
    return true;
}

} // namespace Vulkan
