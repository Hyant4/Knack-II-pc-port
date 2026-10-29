// SPDX-License-Identifier: GPL-2.0-or-later
// Regression cases adapted from HayatoG/iss_pc #278, with range/extension guards.

#include <cassert>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>
#include <boost/container/small_vector.hpp>
#include "video_core/texture_cache/barrier_merge.h"

using VideoCore::MergeAdjacentBarriers;

static vk::ImageMemoryBarrier2 MakeBarrier(std::uint32_t mip, std::uint32_t layer) {
    vk::ImageMemoryBarrier2 b{};
    b.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
    b.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
    b.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
    b.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    b.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = vk::Image{reinterpret_cast<VkImage>(std::uintptr_t{1})};
    b.subresourceRange = {vk::ImageAspectFlagBits::eColor, mip, 1, layer, 1};
    return b;
}

static auto Expand(const auto& barriers) {
    std::vector<vk::ImageMemoryBarrier2> expanded;
    for (const auto& b : barriers) {
        const auto& r = b.subresourceRange;
        for (std::uint32_t m = 0; m < r.levelCount; ++m) {
            for (std::uint32_t l = 0; l < r.layerCount; ++l) {
                auto unit = b;
                unit.subresourceRange = {r.aspectMask, r.baseMipLevel + m, 1,
                                         r.baseArrayLayer + l, 1};
                expanded.push_back(unit);
            }
        }
    }
    return expanded;
}

int main() {
    const char* disabled_values[] = {nullptr, "", "0", "auto", "true", "10", "1 ", " 1"};
    for (const char* value : disabled_values) {
        assert(!VideoCore::IsBarrierMergeEnabled(value));
    }
    assert(VideoCore::IsBarrierMergeEnabled("1"));

    std::vector<vk::ImageMemoryBarrier2> empty;
    MergeAdjacentBarriers(empty, 0);
    MergeAdjacentBarriers(empty, std::numeric_limits<std::size_t>::max());
    assert(empty.empty());

    // Match the container used by Image::Barriers, including growth beyond inline storage.
    boost::container::small_vector<vk::ImageMemoryBarrier2, 32> barriers{MakeBarrier(9, 9)};
    const auto prefix = barriers[0];
    for (std::uint32_t mip = 0; mip < 6; ++mip) {
        for (std::uint32_t layer = 0; layer < 1536; ++layer) {
            barriers.push_back(MakeBarrier(mip, layer));
        }
    }
    const auto before = Expand(barriers);
    MergeAdjacentBarriers(barriers, 1);
    assert(barriers.size() == 2);
    assert(barriers[0] == prefix);
    assert(barriers[1].subresourceRange.levelCount == 6);
    assert(barriers[1].subresourceRange.layerCount == 1536);
    assert(Expand(barriers) == before);

    const auto separate = [](auto mutate) {
        auto a = MakeBarrier(0, 0), b = MakeBarrier(0, 1);
        mutate(a, b);
        std::vector pair{a, b};
        MergeAdjacentBarriers(pair, 0);
        assert(pair == (std::vector{a, b}));
    };
    separate([](auto&, auto& b) { b.image = vk::Image{}; });
    separate([](auto&, auto& b) { b.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth; });
    separate([](auto&, auto& b) { b.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader; });
    separate([](auto&, auto& b) { b.srcAccessMask = vk::AccessFlagBits2::eShaderWrite; });
    separate([](auto&, auto& b) { b.dstStageMask = vk::PipelineStageFlagBits2::eComputeShader; });
    separate([](auto&, auto& b) { b.dstAccessMask = vk::AccessFlagBits2::eShaderWrite; });
    separate([](auto&, auto& b) { b.oldLayout = vk::ImageLayout::eGeneral; });
    separate([](auto&, auto& b) { b.newLayout = vk::ImageLayout::eGeneral; });
    separate([](auto&, auto& b) { b.srcQueueFamilyIndex = 3; });
    separate([](auto&, auto& b) { b.dstQueueFamilyIndex = 4; });
    separate([](auto&, auto& b) { b.subresourceRange.baseArrayLayer = 2; });
    separate([](auto&, auto& b) { b.subresourceRange.baseMipLevel = 2; });
    separate([](auto& a, auto&) { a.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS; });
    separate([](auto&, auto& b) { b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS; });
    separate([](auto& a, auto&) { a.subresourceRange.layerCount = 0; });
    separate([](auto& a, auto& b) { a.pNext = b.pNext = &a; });
    separate([](auto& a, auto& b) {
        a.subresourceRange.baseArrayLayer = UINT32_MAX;
        b.subresourceRange.baseArrayLayer = 0;
    });
    std::vector sentinel{MakeBarrier(0, 0), MakeBarrier(0, 1)};
    const auto original = sentinel;
    MergeAdjacentBarriers(sentinel, sentinel.size());
    assert(sentinel == original);

    // Expanding merged ranges must recover the exact dependency stream, including holes
    // and mixed prior layouts. This catches accidental cross-range or cross-state merging.
    std::mt19937 random{232278};
    for (unsigned trial = 0; trial < 200; ++trial) {
        std::vector<vk::ImageMemoryBarrier2> stream;
        for (unsigned mip = 0; mip < 6; ++mip) {
            for (unsigned layer = 0; layer < 16; ++layer) {
                if (random() % 5 == 0) continue;
                auto b = MakeBarrier(mip, layer);
                if (random() % 3 == 0) b.oldLayout = vk::ImageLayout::eGeneral;
                stream.push_back(b);
            }
        }
        const auto expected = Expand(stream);
        MergeAdjacentBarriers(stream, 0);
        assert(Expand(stream) == expected);
    }

    VideoCore::BarrierTraceBudget budget;
    for (unsigned i = 0; i < 100; ++i) assert(budget.Claim(255) == 0);
    for (unsigned i = 1; i <= 16; ++i) assert(budget.Claim(9216) == i);
    for (unsigned i = 0; i < 100; ++i) assert(budget.Claim(9216) == 0);
}
