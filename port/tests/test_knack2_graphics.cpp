// SPDX-License-Identifier: GPL-2.0-or-later
// CPU-only proof of window sizing, FSR selection, and real native INI persistence.
#include "tlg_graphics.h"
#include "tlg_settings.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

int main(int argc, char** argv) {
    namespace G = TlgGraphics;
    CHECK(argc == 2);
    CHECK(G::FitImage(1920, 1080, 3440, 1440).width == 2560);
    CHECK(G::FitImage(1920, 1080, 3440, 1440).height == 1440);
    CHECK(G::FitImage(1920, 1080, 2560, 1080).width == 1920);
    CHECK(G::FitImage(1920, 1080, 2560, 1080).height == 1080);
    CHECK(G::FitImage(1920, 1080, 1280, 1024).height == 720);
    CHECK(G::FitImage(1920, 1080, 1080, 1920).height == 607);
    CHECK(G::FitImage(1920, 1080, 3840, 2160).width == 3840);
    CHECK(G::FitImage(1920, 1080, 0, 0).width == 0);
    CHECK(G::FitImage(-1, 1080, 100, 100).height == 0);
    CHECK(G::FitImage(1920, 1080, 1, 1).width == 1);
    // Arbitrary resizes, tiny screens and large values must stay inside bounds.
    for (int w : {1, 320, 1280, 1366, 1920, 3440, 7680, 100000}) {
        for (int h : {1, 200, 720, 768, 1080, 1440, 4320, 100000}) {
            auto s = G::FitImage(1920, 1080, w, h);
            CHECK(s.width > 0 && s.width <= w && s.height > 0 && s.height <= h);
            CHECK(std::abs(int64_t(s.width) * 1080 - int64_t(s.height) * 1920) <= 1920);
        }
    }
    for (bool rcas : {false, true}) {
        CHECK(G::FsrPassRuns(true, rcas, 1920, 1080, 2560, 1440));
        CHECK(G::FsrPassRuns(true, rcas, 1919, 1080, 1920, 1080));
        CHECK(G::FsrPassRuns(true, rcas, 1920, 1079, 1920, 1080));
        CHECK(!G::FsrPassRuns(true, rcas, 1920, 1080, 1280, 720));
        CHECK(!G::FsrPassRuns(true, rcas, 1920, 1080, 2560, 720));
        CHECK(!G::FsrPassRuns(true, rcas, 0, 0, 1920, 1080));
        CHECK(!G::FsrPassRuns(true, rcas, 1920, 1080, 0, 0));
    }
    CHECK(G::FsrPassRuns(false, true, 1920, 1080, 1920, 1080));
    CHECK(!G::FsrPassRuns(true, false, 1920, 1080, 1920, 1080));
    CHECK(!G::FsrPassRuns(false, true, 1920, 1080, 2560, 1440));
    CHECK(!G::FsrPassRuns(false, false, 1920, 1080, 2560, 1440));
    CHECK(G::ParseWindowResolution("untrusted") == 0);

    _putenv_s("TLG_CONFIG", argv[1]);
    auto& settings = TlgSettings::Get();
    CHECK(settings.window_resolution == 0);
    CHECK(settings.internal_resolution == 0);
    CHECK(G::ParseInternalResolution("7680x4320") == 0);
    for (std::size_t i = 0; i < G::InternalResolutionKeys.size(); ++i) {
        const auto key = G::InternalResolutionKeys[i];
        CHECK(G::ParseInternalResolution(key) == int(i));
        { std::ofstream out(argv[1]); out << "internal_resolution=" << key << "\nngs2_render_mix=1\n"; }
        TlgSettings::Load();
        CHECK(settings.internal_resolution == int(i));
        CHECK(settings.startup_internal_resolution == int(i));
        TlgSettings::Save();
        std::ifstream in(argv[1]);
        std::ostringstream data;
        data << in.rdbuf();
        CHECK(data.str().find("internal_resolution=" + std::string(key) + "\n") != std::string::npos);
    }
    _putenv_s("TLG_INTERNAL_RESOLUTION", "1600x900");
    TlgSettings::Load();
    CHECK(settings.internal_resolution == 2);
    _putenv_s("TLG_INTERNAL_RESOLUTION", "invalid");
    TlgSettings::Load();
    CHECK(settings.internal_resolution == 0);
    _putenv_s("TLG_INTERNAL_RESOLUTION", "");
    for (std::size_t i = 0; i < G::WindowResolutionKeys.size(); ++i) {
        auto key = G::WindowResolutionKeys[i];
        CHECK(G::ParseWindowResolution(key) == int(i));
        { std::ofstream out(argv[1]); out << "window_resolution=" << key << "\nfsr1=1\nrcas=0\n"; }
        TlgSettings::Load();
        CHECK(settings.window_resolution == int(i));
        CHECK(settings.fsr1 && !settings.rcas);
        CHECK(settings.gpu_readbacks == 2 && settings.render_resolution == 0);
        TlgSettings::Save();
        std::ifstream in(argv[1]);
        std::ostringstream data;
        data << in.rdbuf();
        CHECK(data.str().find("window_resolution=" + std::string(key) + "\n") != std::string::npos);
    }
    { std::ofstream out(argv[1]); out << "window_resolution=invalid\n"; }
    TlgSettings::Load();
    CHECK(settings.window_resolution == 0);
    std::puts("window sizing, FSR selection, native INI: OK");
}
