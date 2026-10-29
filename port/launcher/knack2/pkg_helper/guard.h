// SPDX-License-Identifier: GPL-2.0-or-later
// Bounds and destination checks for the pinned PKGInstall extraction adapter.
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

inline void require(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}

inline void check_range(uint64_t offset, uint64_t size, uint64_t limit) {
    require(offset <= limit && size <= limit - offset, "PKG/PFS range out of bounds");
}

inline void check_component(const std::string& name) {
    require(!name.empty() && name != "." && name != ".." && name.size() < 512,
            "Invalid PFS filename");
    require(name.back() != '.' && name.back() != ' ', "Ambiguous filename");
    for (unsigned char c : name)
        require(c >= 32 && std::string("\\/:<>\"|?*").find(c) == std::string::npos,
                "Unsafe PFS filename");
    std::string stem = name.substr(0, name.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return std::toupper(c); });
    require(stem != "CON" && stem != "PRN" && stem != "AUX" && stem != "NUL", "Reserved filename");
    require(!(stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
              stem[3] >= '1' && stem[3] <= '9'), "Reserved device filename");
}

inline void check_destination(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(root);
    require(!relative.empty() && !relative.is_absolute(), "Destination outside staging");
    auto current = root;
    for (const auto& part : relative) {
        check_component(part.string());
        current /= part;
        require(!std::filesystem::is_symlink(current), "Symlink in destination");
#ifdef _WIN32
        const auto attr = GetFileAttributesW(current.c_str());
        require(attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_REPARSE_POINT),
                "Reparse point in destination");
#endif
    }
}

template<typename T>
inline void read_dirent(T& out, const std::vector<char>& data, size_t offset) {
    check_range(offset, 16, data.size());
    out = {};
    std::memcpy(&out, data.data() + offset, 16);
    if (out.ino == 0) return;
    require(out.namelen >= 0 && out.namelen <= 512 && out.entsize >= 16 + out.namelen,
            "Invalid PFS directory entry");
    check_range(offset, out.entsize, data.size());
    std::memcpy(out.name, data.data() + offset + 16, out.namelen);
}
