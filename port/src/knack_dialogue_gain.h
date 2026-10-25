/* SPDX-License-Identifier: GPL-2.0-or-later
 * CUSA08014 01.01: configuration category 2 is Voice (0=SE, 1=BGM).
 * Scale the two native category evaluations, not codecs, speaker channels or
 * NGS2 clocks. The group setter's cap is raised only for category-2 calls from
 * these exact evaluators; every other caller retains its original 1.0 cap.
 * Stubs preserve GPRs, RFLAGS and the original scalar AVX load semantics. */
#ifndef KNACK_DIALOGUE_GAIN_H
#define KNACK_DIALOGUE_GAIN_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#define KNACK_DIALOGUE_GAIN 1.50f
#define KNACK_DIALOGUE_SITES 3u
#define KNACK_DIALOGUE_STUB_STRIDE 128u

static const uint64_t dialogue_sites[KNACK_DIALOGUE_SITES]={0x5bfd6f,0x5c052f,0xb2832e};
static const uint8_t dialogue_lengths[KNACK_DIALOGUE_SITES]={7,6,8};
static const unsigned char dialogue_signatures[KNACK_DIALOGUE_SITES][8]={
    {0xc4,0xc1,0x7a,0x10,0x54,0x8f,0x68},
    {0xc5,0xfa,0x10,0x54,0x8b,0x68},
    {0xc5,0xfa,0x10,0x0d,0xe2,0xe8,0x1b,0x00}
};

static inline int dialogue_profile_matches(const char *serial,const char *version) {
    return serial && version && !strcmp(serial,"CUSA08014") && !strcmp(version,"01.01");
}
static inline int dialogue_signature_matches(const unsigned char *bytes,size_t length,unsigned site) {
    return site<KNACK_DIALOGUE_SITES && bytes && length>=dialogue_lengths[site] &&
           !memcmp(bytes,dialogue_signatures[site],dialogue_lengths[site]);
}
static inline int dialogue_image_matches(const unsigned char *bytes,uint64_t length) {
    static const unsigned char prologue[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
                                         0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x28};
    static const unsigned char calls[2][5]={{0xe8,0x02,0x84,0x56,0x00},
                                          {0xe8,0x47,0x7c,0x56,0x00}};
    if (!bytes || length<UINT64_C(0xb28336)) return 0;
    for (unsigned i=0;i<KNACK_DIALOGUE_SITES;++i)
        if (!dialogue_signature_matches(bytes+dialogue_sites[i],
                                        (size_t)(length-dialogue_sites[i]),i)) return 0;
    /* The clamp stub reads the setter's saved R15/R12/RBX and return address.
     * Validate that stack layout and both return-site identities as well. */
    return !memcmp(bytes+0xb281c0,prologue,sizeof(prologue)) &&
           !memcmp(bytes+0x5bfdb9,calls[0],5) && !memcmp(bytes+0x5c0574,calls[1],5);
}
static inline int dialogue_jump_bytes(uintptr_t site,uintptr_t target,unsigned length,
                                      unsigned char out[8]) {
    if (!out || length<5 || length>8 || site>UINTPTR_MAX-5u) return 0;
    uintptr_t next=site+5u;
    int64_t delta;
    if (target>=next) {
        if (target-next>INT32_MAX) return 0;
        delta=(int64_t)(target-next);
    } else {
        if (next-target>(uint64_t)INT32_MAX+1u) return 0;
        delta=-(int64_t)(next-target);
    }
    int32_t relative=(int32_t)delta;
    memset(out,0x90,length); out[0]=0xe9; memcpy(out+1,&relative,4); return 1;
}
static inline size_t dialogue_load_stub(unsigned char *out,size_t capacity,unsigned site,
                                         uintptr_t return_address) {
    if (!out || site>1 || capacity<(size_t)dialogue_lengths[site]+33u) return 0;
    size_t n=dialogue_lengths[site];
    static const unsigned char suffix[]={
        0x9c,                         /* pushfq */
        0x83,0xf9,0x02,               /* cmp ecx,2 */
        0x75,0x08,                    /* jne restore */
        0xc5,0xea,0x59,0x15,0x0f,0,0,0, /* vmulss xmm2,xmm2,[rip+15] */
        0x9d,                         /* popfq */
        0xff,0x25,0,0,0,0             /* jmp [rip] */
    };
    memcpy(out,dialogue_signatures[site],n); memcpy(out+n,suffix,sizeof(suffix));
    memcpy(out+n+21,&return_address,8);
    float gain=KNACK_DIALOGUE_GAIN; memcpy(out+n+29,&gain,4); return n+33u;
}
static inline size_t dialogue_clamp_stub(unsigned char *out,size_t capacity,
                                          uintptr_t return_address,
                                          uintptr_t evaluator_return,uintptr_t update_return) {
    if (!out || capacity<110) return 0;
    static const unsigned char code[]={
        0xc5,0xfa,0x10,0x0d,0x5e,0,0,0, /* vmovss xmm1,[rip+94] (1.0) */
        0x9c,0x50,0x52,               /* pushfq; push rax; push rdx */
        0x48,0x8b,0x45,0x08,          /* mov rax,[rbp+8] */
        0x48,0x3b,0x05,0x40,0,0,0,    /* cmp rax,[rip+64] (evaluator return) */
        0x74,0x13,                    /* je evaluator */
        0x48,0x3b,0x05,0x3f,0,0,0,    /* cmp rax,[rip+63] (update return) */
        0x75,0x24,                    /* jne restore */
        0x48,0x8b,0x45,0xd8,          /* update: mov rax,[rbp-40] (root RBX) */
        0x48,0x8b,0x55,0xe0,          /* mov rdx,[rbp-32] (offset R12) */
        0xeb,0x08,
        0x48,0x8b,0x45,0xf8,          /* evaluator: mov rax,[rbp-8] (root R15) */
        0x48,0x8b,0x55,0xd8,          /* mov rdx,[rbp-40] (offset RBX) */
        0x48,0x8b,0x00,               /* mov rax,[rax] (group entries) */
        0x80,0x7c,0x10,0x10,0x02,     /* cmp byte ptr [rax+rdx+16],2 */
        0x75,0x08,
        0xc5,0xfa,0x10,0x0d,0x25,0,0,0, /* vmovss xmm1,[rip+37] (1.5) */
        0x5a,0x58,0x9d,               /* restore rdx,rax,RFLAGS */
        0xff,0x25,0,0,0,0             /* jmp [rip] */
    };
    _Static_assert(sizeof(code)==78,"dialogue clamp code layout");
    memcpy(out,code,sizeof(code));
    memcpy(out+78,&return_address,8); memcpy(out+86,&evaluator_return,8);
    memcpy(out+94,&update_return,8);
    float one=1.f,gain=KNACK_DIALOGUE_GAIN;
    memcpy(out+102,&one,4); memcpy(out+106,&gain,4); return 110;
}
#endif
