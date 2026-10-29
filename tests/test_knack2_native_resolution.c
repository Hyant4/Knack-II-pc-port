/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "knack_native_resolution.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(int argc,char** argv) {
    const size_t size=0xc93000;
    unsigned char *image=calloc(1,size), *before=malloc(size);
    CHECK(image && before);
    CHECK(argc==3);
    CHECK(knack_native_profile("CUSA08014","01.00")==knack_native_profile("CUSA08014","01.01"));
    CHECK(!knack_native_profile("CUSA07670","01.00"));
    CHECK(!knack_native_profile("CUSA08014","01.02"));
    CHECK(!knack_native_profile("other","01.01"));
    for (unsigned region=0;region<3;++region) for (unsigned mode=0;mode<2;++mode) {
        const KnackNativeSites *sites=knack_native_profile(region==1?"CUSA07670":"CUSA08014",
                                                          region==2?"01.00":"01.01");
        const unsigned char width[]={0x3d,0x80,0x07,0,0,0x7f,0x0b};
        const unsigned char height[]={0x3d,0x39,0x04,0,0,0x7c,0x1d};
        const uint32_t original[]={3840,2160,3840,2160};
        const uint64_t pools[]={UINT64_C(0x13400000),UINT64_C(0x87600000)};
        memset(image,0xa5,size);
        memcpy(image+sites->width_limit,width,sizeof(width));
        memcpy(image+sites->height_limit,height,sizeof(height));
        memcpy(image+sites->preset_load,sites->load_signature,8);
        memcpy(image+sites->preset,original,sizeof(original));
        memcpy(image+sites->pool_load,sites->pool_signature,8);
        memcpy(image+sites->pool_defaults,pools,sizeof(pools));
        memcpy(before,image,size);
        const uint32_t w=mode?3840:2560, h=mode?2160:1440;
        CHECK(knack_native_direct_mb(w,h)==strtoul(argv[mode+1],NULL,10));
        CHECK(!knack_native_apply(image,size,sites,2560,1080));
        CHECK(!memcmp(image,before,size));
        image[sites->preset_load]^=1;
        memcpy(before,image,size);
        image[sites->pool_defaults]^=1;
        memcpy(before,image,size);
        CHECK(!knack_native_apply(image,size,sites,w,h));
        CHECK(!memcmp(image,before,size));
        image[sites->pool_defaults]^=1;
        memcpy(before,image,size);
        CHECK(!knack_native_apply(image,size,sites,w,h));
        CHECK(!memcmp(image,before,size));
        image[sites->preset_load]^=1;
        memcpy(before,image,size);
        CHECK(!knack_native_apply(image,sites->preset+15,sites,w,h));
        CHECK(!memcmp(image,before,size));
        CHECK(knack_native_apply(image,size,sites,w,h));
        uint32_t actual[4], bound;
        memcpy(actual,image+sites->preset,sizeof(actual));
        CHECK(actual[0]==w && actual[1]==h && actual[2]==w && actual[3]==h);
        memcpy(&bound,image+sites->width_limit+1,4); CHECK(bound==w);
        memcpy(&bound,image+sites->height_limit+1,4); CHECK(bound==h+1);
        CHECK(image[sites->width_limit+5]==0x7f && image[sites->height_limit+5]==0x7c);
        CHECK(!memcmp(image+sites->preset_load,sites->load_signature,8));
        uint64_t pool;
        memcpy(&pool,image+sites->pool_defaults+8,8);
        CHECK(pool==pools[1]+(uint64_t)knack_native_extra_mb(w,h)*1024*1024);
        CHECK(pool+UINT64_C(0x3b00000)<UINT64_C(0x100000000));
        /* Only the two dimension immediates and the preset may change. */
        memcpy(image+sites->width_limit+1,before+sites->width_limit+1,4);
        memcpy(image+sites->height_limit+1,before+sites->height_limit+1,4);
        memcpy(image+sites->preset,before+sites->preset,sizeof(original));
        memcpy(image+sites->pool_defaults+8,before+sites->pool_defaults+8,8);
        CHECK(!memcmp(image,before,size));
    }
    free(before); free(image);
    puts("Native framebuffer presets: three profiles, bounds and atomic rejection PASS");
    return 0;
}
