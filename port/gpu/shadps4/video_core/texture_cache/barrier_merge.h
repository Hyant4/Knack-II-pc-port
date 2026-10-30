// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vulkan/vulkan.hpp>

namespace VideoCore {

// Adapted from HayatoG/iss_pc #232/#278 (cde38af). Explicit opt-in only.
inline bool IsBarrierMergeEnabled(const char* value) {
    return value && value[0] == '1' && value[1] == '\0';
}

// Supplied only by an explicit RX 580 preset snapshot, never by GPU detection.
inline bool Rx580PresetEnabled() {
    static const bool enabled = IsBarrierMergeEnabled(std::getenv("TLG_RX580_PRESET"));
    return enabled;
}

// Independent budgets for generation and recording; no unbounded per-frame trace.
class BarrierTraceBudget {
public:
    std::uint32_t Claim(std::size_t count) {
        if (count < 256) {
            return 0;
        }
        auto seen = used.load(std::memory_order_relaxed);
        while (seen < 16) {
            if (used.compare_exchange_weak(seen, seen + 1, std::memory_order_relaxed)) {
                return seen + 1;
            }
        }
        return 0;
    }

private:
    std::atomic<std::uint32_t> used{0};
};

// Only the new suffix from one GetBarriers call may be coalesced. This preserves
// ordering and dependencies of earlier accesses. Extended/sentinel ranges stay separate.
template <typename Container>
void MergeAdjacentBarriers(Container& barriers, std::size_t first) {
    if (first >= barriers.size() || barriers.size() - first < 2) {
        return;
    }
    const auto merge_pass = [&](bool layers) {
        std::size_t out = first;
        for (std::size_t i = first + 1; i < barriers.size(); ++i) {
            auto& prev = barriers[out];
            const auto cur = barriers[i];
            auto& pr = prev.subresourceRange;
            const auto& cr = cur.subresourceRange;
            const bool explicit_ranges = pr.levelCount && cr.levelCount && pr.layerCount &&
                cr.layerCount && pr.levelCount != VK_REMAINING_MIP_LEVELS &&
                cr.levelCount != VK_REMAINING_MIP_LEVELS &&
                pr.layerCount != VK_REMAINING_ARRAY_LAYERS &&
                cr.layerCount != VK_REMAINING_ARRAY_LAYERS;
            const bool same_transition =
                !prev.pNext && !cur.pNext && explicit_ranges &&
                prev.image == cur.image && pr.aspectMask == cr.aspectMask &&
                prev.srcStageMask == cur.srcStageMask && prev.srcAccessMask == cur.srcAccessMask &&
                prev.dstStageMask == cur.dstStageMask && prev.dstAccessMask == cur.dstAccessMask &&
                prev.oldLayout == cur.oldLayout && prev.newLayout == cur.newLayout &&
                prev.srcQueueFamilyIndex == cur.srcQueueFamilyIndex &&
                prev.dstQueueFamilyIndex == cur.dstQueueFamilyIndex;
            const bool adjacent =
                layers ? (pr.baseMipLevel == cr.baseMipLevel && pr.levelCount == cr.levelCount &&
                          std::uint64_t{pr.baseArrayLayer} + pr.layerCount == cr.baseArrayLayer)
                       : (pr.baseArrayLayer == cr.baseArrayLayer && pr.layerCount == cr.layerCount &&
                          std::uint64_t{pr.baseMipLevel} + pr.levelCount == cr.baseMipLevel);
            const auto combined = layers ? std::uint64_t{pr.layerCount} + cr.layerCount
                                         : std::uint64_t{pr.levelCount} + cr.levelCount;
            if (same_transition && adjacent && combined < UINT32_MAX) {
                if (layers) {
                    pr.layerCount = static_cast<std::uint32_t>(combined);
                } else {
                    pr.levelCount = static_cast<std::uint32_t>(combined);
                }
                continue;
            }
            barriers[++out] = cur;
        }
        barriers.erase(barriers.begin() + (out + 1), barriers.end());
    };
    merge_pass(true);
    merge_pass(false);
}

} // namespace VideoCore
