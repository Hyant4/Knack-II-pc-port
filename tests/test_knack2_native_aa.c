/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "knack_native_aa.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    const size_t size=0x1dc000;
    unsigned char *image=malloc(size), *before=malloc(size);
    CHECK(image && before);
    CHECK(!knack_aa_profile(NULL,"01.01"));
    CHECK(knack_aa_profile("CUSA08014","01.00")==knack_aa_profile("CUSA08014","01.01"));
    CHECK(!knack_aa_profile("CUSA07670","01.00"));
    CHECK(!knack_aa_profile("CUSA08014","01.02"));
    CHECK(!knack_aa_profile("unknown","01.01"));
    for (unsigned region=0;region<3;++region) {
        const KnackAaSite *site=knack_aa_profile(region==1?"CUSA07670":"CUSA08014",
                                               region==2?"01.00":"01.01");
        memset(image,0xa5,size);
        memcpy(image+site->instruction-16,site->context,sizeof(site->context));
        for (unsigned i=0;i<sizeof(site->context);++i) {
            image[site->instruction-16+i]^=1;
            memcpy(before,image,size);
            CHECK(!knack_aa_apply(image,size,site));
            CHECK(!memcmp(image,before,size));
            image[site->instruction-16+i]^=1;
        }
        memcpy(before,image,size);
        CHECK(!knack_aa_apply(image,site->instruction+63,site));
        CHECK(!knack_aa_apply(image,size,NULL));
        CHECK(!knack_aa_apply(NULL,size,site));
        KnackAaSite overflow=*site; overflow.instruction=UINT64_MAX;
        CHECK(!knack_aa_apply(image,size,&overflow));
        CHECK(!memcmp(image,before,size));
        CHECK(knack_aa_apply(image,size,site));
        const unsigned char replacement[]={0xc5,0xe0,0x57,0xdb};
        CHECK(!memcmp(image+site->instruction,replacement,4));
        CHECK(!knack_aa_apply(image,size,site));
        memcpy(image+site->instruction,before+site->instruction,4);
        CHECK(!memcmp(image,before,size));
    }
    free(image); free(before);
    puts("native AA: both regions, 160 context mutations, bounds and write scope OK");
    return 0;
}
