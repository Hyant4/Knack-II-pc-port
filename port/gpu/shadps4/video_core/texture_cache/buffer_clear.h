// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <vector>
#include "common/types.h"

namespace VideoCore {

// A confirmed uniform raw-buffer fill can initialize an image created after the dispatch.
// Any intervening write invalidates the proof, including partial overlapping writes.
class BufferClears {
public:
    using Pattern = std::array<u32, 4>;

    void Forget(VAddr address, u64 size) {
        std::erase_if(entries, [&](const Entry& e) {
            return size && (address >= e.address ? address - e.address < e.size
                                                : e.address - address < size);
        });
    }

    void Record(VAddr address, u64 size, const Pattern& pattern) {
        Forget(address, size);
        if (size) entries.push_back({address, size, pattern});
    }

    std::optional<Pattern> Take(VAddr address, u64 size) {
        const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) {
            return e.address == address && e.size == size;
        });
        if (it == entries.end()) return std::nullopt;
        const auto pattern = it->pattern;
        entries.erase(it);
        return pattern;
    }

    static bool Repeats64(const Pattern& p) {
        return p[0] == p[2] && p[1] == p[3];
    }

private:
    struct Entry { VAddr address; u64 size; Pattern pattern; };
    std::vector<Entry> entries;
};
} // namespace VideoCore
