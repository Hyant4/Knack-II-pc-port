/* SPDX-License-Identifier: GPL-2.0-or-later
 * Knack II perspective builders: USA 01.00/01.01 and Europe 01.01.
 * Widen horizontal FOV, retain vertical FOV.
 * Set the gameplay camera's aspect source and four auxiliary projection constants
 * in the private image before guest execution. Never modify the original dump. */
#ifndef KNACK_ULTRAWIDE_PATCH_H
#define KNACK_ULTRAWIDE_PATCH_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define KNACK_WIDE_SITES 4u
#define KNACK_WIDE_NATIVE (16.0f / 9.0f)
typedef struct KnackWideSite {
    uint64_t constant, instruction;
    unsigned char signature[8];
} KnackWideSite;

static const KnackWideSite knack_wide_us[KNACK_WIDE_SITES] = {
    {0xcdeec4,0x1c32fb,{0xc5,0xe2,0x59,0x25,0xc1,0xbb,0xb1,0x00}},
    {0xcdeed4,0x1c37fe,{0xc5,0xe2,0x59,0x25,0xce,0xb6,0xb1,0x00}},
    {0xce11fc,0x43206e,{0xc5,0xfa,0x59,0x0d,0x86,0xf1,0x8a,0x00}},
    {0xce1f80,0x5b5f5b,{0xc5,0xfa,0x59,0x05,0x1d,0xc0,0x72,0x00}},
};
static const KnackWideSite knack_wide_eu[KNACK_WIDE_SITES] = {
    {0xcde994,0x1c269b,{0xc5,0xe2,0x59,0x25,0xf1,0xc2,0xb1,0x00}},
    {0xcde9a4,0x1c2b9e,{0xc5,0xe2,0x59,0x25,0xfe,0xbd,0xb1,0x00}},
    {0xce0cc8,0x4309fe,{0xc5,0xfa,0x59,0x0d,0xc2,0x02,0x8b,0x00}},
    {0xce1a4c,0x5b4b3b,{0xc5,0xfa,0x59,0x05,0x09,0xcf,0x72,0x00}},
};
/* The gameplay camera does not use the auxiliary builders above. Its constructor
 * divides the native framebuffer width by height and stores the result at +0x11c.
 * Retain the width conversion, division and store. Replace only the eight-byte
 * height conversion with width / requested_aspect; the following division then
 * seeds the real camera, including its CPU frustum, with the requested aspect.
 * Upper XMM lanes and instruction length are identical to the original sequence. */
typedef struct KnackWideCameraSite {
    uint64_t start;
} KnackWideCameraSite;
static const KnackWideCameraSite knack_wide_camera_us = {0x1e4799};
static const KnackWideCameraSite knack_wide_camera_eu = {0x1e3bb9};
static const unsigned char knack_wide_camera_signature[29] = {
    0xc5,0xfa,0x2a,0x80,0x0c,0x02,0x00,0x00, /* width conversion */
    0xc5,0xfa,0x2a,0x88,0x10,0x02,0x00,0x00, /* height conversion */
    0xc5,0xfa,0x5e,0xc1,                     /* width / height */
    0xc4,0xc1,0x7a,0x11,0x85,0x1c,0x01,0x00,0x00 /* aspect store */
};
static inline const KnackWideCameraSite* knack_wide_camera_site(const KnackWideSite* sites) {
    if (sites==knack_wide_us) return &knack_wide_camera_us;
    if (sites==knack_wide_eu) return &knack_wide_camera_eu;
    return NULL;
}
static inline const KnackWideSite* knack_wide_profile(const char* serial, const char* version) {
    if (!serial || !version) return NULL;
    /* USA 01.00 has the same allowlisted executable bytes as USA 01.01. */
    if (!strcmp(serial,"CUSA08014") && (!strcmp(version,"01.00") || !strcmp(version,"01.01")))
        return knack_wide_us;
    if (!strcmp(serial,"CUSA07670") && !strcmp(version,"01.01")) return knack_wide_eu;
    return NULL;
}
static inline int knack_wide_image_matches(const unsigned char* image, uint64_t size,
                                          const KnackWideSite* sites) {
    const KnackWideCameraSite* camera=knack_wide_camera_site(sites);
    if (!image || !sites || !camera || camera->start>size ||
        sizeof(knack_wide_camera_signature)>size-camera->start ||
        memcmp(image+camera->start,knack_wide_camera_signature,sizeof(knack_wide_camera_signature))) return 0;
    for (unsigned i=0;i<KNACK_WIDE_SITES;++i) {
        uint64_t at=sites[i].constant, code=sites[i].instruction;
        if (at>size || size-at<4 || code>size || size-code<8 ||
            memcmp(image+code,sites[i].signature,8)) return 0;
        uint32_t bits; memcpy(&bits,image+at,4);
        if (bits!=UINT32_C(0x3fe38e39)) return 0; /* exact original 16/9 float */
    }
    return 1;
}
static inline int knack_wide_apply(unsigned char* image, uint64_t size,
                                  const KnackWideSite* sites, float aspect) {
    if (!isfinite(aspect) || aspect<=KNACK_WIDE_NATIVE+0.001f ||
        aspect>32.0f/9.0f+0.0001f || !knack_wide_image_matches(image,size,sites)) return 0;
    /* Validate every constant and the complete camera sequence before any write. */
    const KnackWideCameraSite* camera=knack_wide_camera_site(sites);
    const uint64_t instruction=camera->start+8;
    const int64_t displacement=(int64_t)sites[0].constant-(int64_t)(instruction+8);
    if (displacement<INT32_MIN || displacement>INT32_MAX) return 0;
    unsigned char replacement[8]={0xc5,0xfa,0x5e,0x0d,0,0,0,0}; /* vdivss xmm1,xmm0,[aspect] */
    const int32_t relative=(int32_t)displacement;
    memcpy(replacement+4,&relative,4);
    for (unsigned i=0;i<KNACK_WIDE_SITES;++i) memcpy(image+sites[i].constant,&aspect,4);
    memcpy(image+instruction,replacement,sizeof(replacement));
    return KNACK_WIDE_SITES;
}
#endif
