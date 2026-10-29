// SPDX-License-Identifier: GPL-2.0-or-later
// tlg: graphics options of the in-game menu (Insert / R3+L2), kept in tlg.ini. The live ones
// apply on the next frame; the startup ones (patches, GPU readback modes) are read by run.py and
// apply through "Apply and restart". The launcher reads and writes the same keys.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace TlgGraphics {

enum DisplayMode : int { Windowed = 0, Borderless = 1, Fullscreen = 2, DisplayModeCount };
inline constexpr std::array<std::string_view, DisplayModeCount> DisplayModeKeys = {
    "windowed", "borderless", "fullscreen"};
inline constexpr std::array<const char*, DisplayModeCount> DisplayModeNames = {
    "Windowed", "Borderless", "Fullscreen"};

/// Startup window size only. Fullscreen uses the desktop; guest render targets stay unchanged.
inline constexpr std::array<std::string_view, 12> WindowResolutionKeys = {
    "auto", "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160",
    "2560x1080", "3440x1440", "3840x1600", "5120x1440", "5120x2160", "7680x2160"};
struct ImageSize {
    int width, height;
};
/// Knack startup size. QHD/4K use native game targets; lower sizes use scene proxies.
inline constexpr std::array<std::string_view, 6> InternalResolutionKeys = {
    "auto", "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160"};
inline constexpr std::array<ImageSize, 6> InternalSizes = {{{0, 0}, {1280, 720},
    {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}}};
inline constexpr std::array<ImageSize, 12> WindowSizes = {{{0, 0}, {1280, 720}, {1600, 900},
    {1920, 1080}, {2560, 1440}, {3840, 2160}, {2560, 1080}, {3440, 1440}, {3840, 1600},
    {5120, 1440}, {5120, 2160}, {7680, 2160}}};
inline constexpr float NativeAspect = 16.0f / 9.0f;
inline constexpr std::array<std::string_view, 4> AspectKeys = {"auto", "16:9", "21:9", "32:9"};
inline bool KnackProfile(const char* value) {
    return value && (std::string_view(value)=="CUSA08014-01.00" ||
                     std::string_view(value)=="CUSA08014-01.01" ||
                     std::string_view(value)=="CUSA07670-01.01");
}
inline float RequestedAspect(int mode, int desktop_width, int desktop_height) {
    float value = mode==2 ? 21.0f/9.0f : mode==3 ? 32.0f/9.0f : NativeAspect;
    if (mode==0 && desktop_width>0 && desktop_height>0)
        value=float(desktop_width)/float(desktop_height);
    return std::isfinite(value) && value>NativeAspect && value<=32.0f/9.0f+0.0001f
        ? value : NativeAspect;
}

/// NV12 conversion and final UI composition are separate draws. Only conversion
/// needs its original horizontal projection restored; the final UI already
/// compensates presentation and places the video inside its original 16:9 frame.
enum class KnackVideoPass { None, Conversion, Composition };
struct KnackVideoFraming {
    bool active = true; // startup and blank/loading frames retain the original 16:9 canvas
    bool Advance(bool video_composed, bool scene_camera_seen) {
        if (video_composed) active=true;
        else if (scene_camera_seen) active=false;
        return active;
    }
};
struct KnackVideoImage {
    uint64_t uid{}, address{}, generation{};
    uint32_t width{}, height{};
    bool Matches(uint64_t current_uid, uint64_t current_address, uint64_t current_generation,
                 uint32_t current_width, uint32_t current_height) const {
        // Alias copies inherit a globally unique write generation. A recycled
        // address with another write cannot masquerade as this video texture.
        return uid && current_uid && generation && address==current_address &&
               generation==current_generation && width==current_width && height==current_height;
    }
};
inline KnackVideoPass KnackVideoDraw(bool knack, uint64_t vertex, uint64_t fragment,
                                    bool depth, bool clip_disabled, bool indirect,
                                    unsigned indices, unsigned instances, bool video_sampled) {
    if (!knack || vertex!=UINT64_C(0xa4d9b6a0) || depth || clip_disabled ||
        (!indirect && ((indices!=4 && indices!=6) || instances!=1))) return KnackVideoPass::None;
    if (fragment==UINT64_C(0x6e46c324)) return KnackVideoPass::Conversion;
    if (fragment==UINT64_C(0xc437421c) && video_sampled) return KnackVideoPass::Composition;
    return KnackVideoPass::None;
}
inline float KnackVideoConversionScale(float aspect) {
    return std::isfinite(aspect) && aspect>NativeAspect+0.001f && aspect<=32.0f/9.0f+0.0001f
        ? aspect/NativeAspect : 1.0f;
}
inline std::array<int,2> KnackVideoContentBounds(int width, float aspect) {
    if (width<=0) return {0,0};
    const float scale=KnackVideoConversionScale(aspect);
    if (scale==1.0f) return {0,width};
    const float margin=float(width)*0.5f*(1.0f-1.0f/scale);
    // Cover fractional boundary pixels too: otherwise the old background leaves
    // a one-pixel colored seam when the presenter filters the display image.
    return {int(std::ceil(margin)),width-int(std::ceil(margin))};
}
/// Largest source-shaped image inside the bounds, using integer arithmetic to avoid overflow.
inline ImageSize FitImage(int width, int height, int bounds_width, int bounds_height) {
    if (width <= 0 || height <= 0 || bounds_width <= 0 || bounds_height <= 0) return {0, 0};
    if (int64_t(bounds_width) * height <= int64_t(bounds_height) * width) {
        return {bounds_width, std::max(1, int(int64_t(bounds_width) * height / width))};
    }
    return {std::max(1, int(int64_t(bounds_height) * width / height)), bounds_height};
}

/// Guest scene targets remain native: camera projection encodes the wider scene anamorphically.
/// Presentation expands it back to its validated aspect; the game retains ownership of UI projection.
inline ImageSize FitPresentation(int width, int height, int bounds_width, int bounds_height,
                                 float active_aspect) {
    if (width<=0 || height<=0) return {0,0};
    if (std::isfinite(active_aspect) && active_aspect>NativeAspect+0.001f &&
        active_aspect<=32.0f/9.0f+0.0001f && height>0 && height<=32768)
        width=std::max(1,int(std::lround(float(height)*active_aspect)));
    return FitImage(width,height,bounds_width,bounds_height);
}

/// Present rate limits; 0 = unlimited.
inline constexpr std::array<int, 5> FpsLimits = {30, 60, 120, 240, 0};

/// Game patch suffixes ("Resolution Patch - <name>"); "native" is no resolution patch.
inline constexpr std::array<std::string_view, 6> Resolutions = {"native", "480p", "720p",
                                                                "1440p", "1800p", "4K"};
/// Engine frame rate ceilings: 60 is the game's own, 120 and 240 are frame rate patches.
inline constexpr std::array<int, 3> EngineFps = {60, 120, 240};

template <typename T, std::size_t N>
constexpr int IndexOf(const std::array<T, N>& values, const T& value, int fallback) {
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == value) return int(i);
    }
    return fallback;
}

inline int ParseWindowResolution(std::string_view text) {
    return IndexOf(WindowResolutionKeys, text, 0);
}

inline int ParseInternalResolution(std::string_view text) {
    return IndexOf(InternalResolutionKeys, text, 0);
}

/// Case-insensitive: the launchers wrote "Borderless"/"Fullscreen" and every such tlg.ini
/// opened the game windowed.
inline int ParseDisplayMode(std::string_view text) {
    for (std::size_t i = 0; i < DisplayModeKeys.size(); ++i) {
        const std::string_view key = DisplayModeKeys[i];
        if (key.size() == text.size() &&
            std::equal(key.begin(), key.end(), text.begin(), [](char a, char b) {
                return a == (b >= 'A' && b <= 'Z' ? char(b - 'A' + 'a') : b);
            })) {
            return int(i);
        }
    }
    return Windowed;
}
/// A listed limit, else unlimited.
inline int ParseFpsLimit(int value) {
    return IndexOf(FpsLimits, value, -1) >= 0 ? value : 0;
}
/// Index into Resolutions; unknown text is native.
inline int ParseResolution(std::string_view text) {
    return IndexOf(Resolutions, text, 0);
}
/// A listed ceiling, else 120 (the default patch).
inline int ParseEngineFps(int value) {
    return IndexOf(EngineFps, value, -1) >= 0 ? value : 120;
}

/// Frames the GPU command thread may queue ahead of the GPU; 0 = unbounded.
inline constexpr std::array<int, 3> FramesQueued = {1, 2, 0};
inline int ParseFramesQueued(int value) {
    return IndexOf(FramesQueued, value, -1) >= 0 ? value : 1;
}
/// RCAS strength in percent; invalid values fall back to 75.
inline int ParseRcasStrength(int value) {
    return value >= 0 && value <= 100 ? value : 60;
}
/// FSR RCAS attenuation for a strength: 0 is the sharpest, 2 the softest.
inline float RcasAttenuation(int strength) {
    return 2.0f * (1.0f - float(ParseRcasStrength(strength)) / 100.0f);
}
/// Whether the FSR pass runs: FSR 1 when the window is larger than the game's image, RCAS
/// alone when they are the same size. A smaller window is left to the regular scaling.
inline bool FsrPassRuns(bool fsr1, bool rcas, int in_w, int in_h, int out_w, int out_h) {
    if (in_w <= 0 || in_h <= 0 || out_w <= 0 || out_h <= 0) return false;
    const bool upscale = in_w <= out_w && in_h <= out_h && (in_w < out_w || in_h < out_h);
    const bool same = in_w == out_w && in_h == out_h;
    return (fsr1 && upscale) || (rcas && same);
}

/// What the present limit really allows: the engine ceiling caps it.
inline int EffectiveFps(int limit, int engine_fps) {
    return limit == 0 || limit > engine_fps ? engine_fps : limit;
}

/// The present thread's frame limit: never above the engine rate the patches applied, since the
/// engine steps a fixed time per frame and faster flips speed the game up. 0 stays vblank paced.
inline int PresentLimit(int limit, int engine_fps) {
    return limit == 0 ? 0 : EffectiveFps(limit, engine_fps);
}

} // namespace TlgGraphics
