// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_vulkan/vk_temporal_upscaler.h"
#include <cassert>
#include <cstdio>
int main() {
    using Upscaler = Vulkan::TemporalUpscaler;
    using Target = Upscaler::NativeAaTarget;
    const auto image = [](uintptr_t value) { return vk::Image{reinterpret_cast<VkImage>(value)}; };
    const auto usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    for (const auto size : {std::array<u32,2>{1920,1080}, {2560,1440}, {3840,2160}}) {
        std::array<Target,3> good{};
        for (unsigned i=0;i<3;++i)
            good[i] = {image(i+1), vk::Format::eB10G11R11UfloatPack32,
                       size[0],size[1],1,1,1,1,usage,0x10000000ull+i*0x4000000ull,
                       u64(size[0])*size[1]*4};
        assert(Upscaler::NativeAaCompatible(good,size[0],size[1]));
        for (unsigned i=0;i<3;++i) {
            auto bad=good; bad[i].image=nullptr; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].format=vk::Format::eR16G16B16A16Sfloat;
            assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; --bad[i].width; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; --bad[i].height; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].depth=2; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].layers=2; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].samples=4; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].levels=2; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].usage={}; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].guest_size=0; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[i].guest_address=UINT64_MAX; assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
        }
        for (unsigned i=0;i<3;++i) for (unsigned j=i+1;j<3;++j) {
            auto bad=good; bad[j].image=bad[i].image;
            assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
            bad=good; bad[j].guest_address=bad[i].guest_address+4;
            assert(!Upscaler::NativeAaCompatible(bad,size[0],size[1]));
        }
        assert(!Upscaler::NativeAaCompatible(good,0,size[1]));
    }
    std::puts("native AA copy: native sizes, format, aliases, samples, bounds and usage OK");
}
