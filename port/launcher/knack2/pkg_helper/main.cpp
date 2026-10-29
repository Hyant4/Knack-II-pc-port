// SPDX-License-Identifier: GPL-2.0-or-later
// Headless adapter: accepts only a fresh staging directory, never a game installation.
#include <filesystem>
#include <iostream>
#include <string>
#include "pkg.h"
#include "guard.h"

int run(const std::filesystem::path& input, const std::filesystem::path& output,
        bool base = false, bool plan = false) {
    try {
        require(std::filesystem::is_directory(output) && std::filesystem::is_empty(output) &&
                !std::filesystem::is_symlink(output), "Staging directory must be empty");
        auto root = std::filesystem::canonical(output);
        auto package = std::filesystem::canonical(input);
        require(!package.lexically_relative(root).empty() &&
                package.lexically_relative(root).begin()->string() == "..", "PKG must be outside staging");
        PKG pkg;
        std::string error;
        require(pkg.Open(package, error), error.empty() ? "Cannot inspect PKG" : error.c_str());
        const bool patch = pkg.GetPkgFlags().find("PATCH") != std::string::npos;
        require(base ? !patch : patch, base ? "Base mode does not accept update PKGs" :
                                              "Only update PKGs are accepted");
        require(pkg.Extract(package, root, error), error.empty() ? "Cannot extract PKG" : error.c_str());
        if (plan) {
            std::cout << "KNACK_PKG_PLAN_V1\n";
            return 0;
        }
        const auto count = pkg.GetNumberOfFiles();
        for (unsigned i = 0; i < count; ++i) {
            pkg.ExtractFiles(i);
            if (base && ((i + 1) % 32 == 0 || i + 1 == count))
                std::cout << "KNACK_PKG_PROGRESS_V1 " << i + 1 << ' ' << count << std::endl;
        }
        std::cout << "KNACK_PKG_COMPLETE_V1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PKG extraction failed: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    const bool base = argc == 4 && (std::filesystem::path(argv[1]) == "--base" ||
                                    std::filesystem::path(argv[1]) == "--base-plan");
    if (argc != 3 && !base) {
        std::cerr << "Usage: KnackPkg [--base|--base-plan] game.pkg EMPTY_STAGING_DIRECTORY\n";
        return 2;
    }
    return run(argv[base ? 2 : 1], argv[base ? 3 : 2], base,
               base && std::filesystem::path(argv[1]) == "--base-plan");
}
