/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native Knack II framebuffer presets, CUSA08014/CUSA07670 01.01 only.
 * Keep base-console ABI/memory layout. Raise the existing dimension bounds to
 * the selected preset; never modify the dump or bypass an unknown signature. */
#ifndef KNACK_NATIVE_RESOLUTION_H
#define KNACK_NATIVE_RESOLUTION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct KnackNativeSites {
    uint64_t width_limit, height_limit, preset_load, preset;
    unsigned char load_signature[8];
    uint64_t pool_load, pool_defaults;
    unsigned char pool_signature[8];
} KnackNativeSites;
static const KnackNativeSites knack_native_us = {
    0x14e467, 0x14e472, 0x158f75, 0xc92130,
    {0xc5,0xf8,0x10,0x05,0xb3,0x91,0xb3,0x00},
    0x14e348,0xc91f30,{0xc5,0xf8,0x10,0x05,0xe0,0x3b,0xb4,0x00}};
static const KnackNativeSites knack_native_eu = {
    0x14e417, 0x14e422, 0x158e55, 0xc91bf0,
    {0xc5,0xf8,0x10,0x05,0x93,0x8d,0xb3,0x00},
    0x14e2f8,0xc919f0,{0xc5,0xf8,0x10,0x05,0xf0,0x36,0xb4,0x00}};
static inline const KnackNativeSites* knack_native_profile(const char* serial, const char* version) {
    if (!serial || !version || strcmp(version,"01.01")) return NULL;
    if (!strcmp(serial,"CUSA08014")) return &knack_native_us;
    if (!strcmp(serial,"CUSA07670")) return &knack_native_eu;
    return NULL;
}
static inline int knack_native_size(uint32_t width, uint32_t height) {
    return (width==2560 && height==1440) || (width==3840 && height==2160);
}
/* Keep the graphics pool below 4 GiB, including the game's 59 MiB adjustment.
 * The direct-memory backing grows by the same amount; other pools are unchanged. */
static inline uint32_t knack_native_extra_mb(uint32_t width, uint32_t height) {
    if (width==2560 && height==1440) return 768;
    if (width==3840 && height==2160) return 1536;
    return 0;
}
static inline uint32_t knack_native_direct_mb(uint32_t width, uint32_t height) {
    return 5056+knack_native_extra_mb(width,height);
}
static inline int knack_native_range(uint64_t at, uint64_t count, uint64_t size) {
    return at<=size && count<=size-at;
}
static inline int knack_native_matches(const unsigned char* image, uint64_t size,
                                       const KnackNativeSites* sites) {
    static const unsigned char width[] = {0x3d,0x80,0x07,0x00,0x00,0x7f,0x0b};
    static const unsigned char height[] = {0x3d,0x39,0x04,0x00,0x00,0x7c,0x1d};
    static const uint32_t preset[] = {3840,2160,3840,2160};
    static const uint64_t pools[] = {UINT64_C(0x13400000),UINT64_C(0x87600000)};
    return image && sites &&
        knack_native_range(sites->width_limit,sizeof(width),size) &&
        knack_native_range(sites->height_limit,sizeof(height),size) &&
        knack_native_range(sites->preset_load,sizeof(sites->load_signature),size) &&
        knack_native_range(sites->preset,sizeof(preset),size) &&
        knack_native_range(sites->pool_load,8,size) &&
        knack_native_range(sites->pool_defaults,sizeof(pools),size) &&
        !memcmp(image+sites->width_limit,width,sizeof(width)) &&
        !memcmp(image+sites->height_limit,height,sizeof(height)) &&
        !memcmp(image+sites->preset_load,sites->load_signature,sizeof(sites->load_signature)) &&
        !memcmp(image+sites->preset,preset,sizeof(preset)) &&
        !memcmp(image+sites->pool_load,sites->pool_signature,8) &&
        !memcmp(image+sites->pool_defaults,pools,sizeof(pools));
}
static inline int knack_native_apply(unsigned char* image, uint64_t size,
                                     const KnackNativeSites* sites, uint32_t width, uint32_t height) {
    if (!knack_native_size(width,height) || !knack_native_matches(image,size,sites)) return 0;
    /* Preserve both conditional branches and the native 1080p fallback. Validate
     * all sites before writing; the height comparison uses an exclusive bound. */
    const uint32_t height_bound=height+1, preset[]={width,height,width,height};
    const uint64_t gpu_pool=UINT64_C(0x87600000)+(uint64_t)knack_native_extra_mb(width,height)*1024*1024;
    memcpy(image+sites->width_limit+1,&width,sizeof(width));
    memcpy(image+sites->height_limit+1,&height_bound,sizeof(height_bound));
    memcpy(image+sites->preset,preset,sizeof(preset));
    memcpy(image+sites->pool_defaults+8,&gpu_pool,sizeof(gpu_pool));
    return 1;
}
#endif
