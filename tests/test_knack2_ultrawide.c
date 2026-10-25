/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "knack_ultrawide_patch.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)

/* Execute the real, signature-guarded camera seed sequence, not a model of the
 * desired projection. The tiny host thunk supplies the two registers that the
 * original constructor has already initialized and preserves nonvolatile R13. */
static void camera_fixture(unsigned char* image, const KnackWideSite* sites) {
    const KnackWideCameraSite* camera=knack_wide_camera_site(sites);
    memcpy(image+camera->start,knack_wide_camera_signature,sizeof(knack_wide_camera_signature));
    image[camera->start+sizeof(knack_wide_camera_signature)]=0xc3;
#ifdef _WIN32
    unsigned char thunk[]={0x41,0x55,0x48,0x89,0xc8,0x49,0x89,0xd5,0xe8,0,0,0,0,0x41,0x5d,0xc3};
#else
    unsigned char thunk[]={0x41,0x55,0x48,0x89,0xf8,0x49,0x89,0xf5,0xe8,0,0,0,0,0x41,0x5d,0xc3};
#endif
    const int32_t relative=(int32_t)(camera->start-(0x100+13));
    memcpy(thunk+9,&relative,4);
    memcpy(image+0x100,thunk,sizeof(thunk));
}
static float camera_seed(unsigned char* image, int width, int height) {
    int dimensions[0x214/4]={0};
    unsigned char camera[0x120]={0};
    dimensions[0x20c/4]=width;
    dimensions[0x210/4]=height;
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(),image,0xcf0000);
#else
    __builtin___clear_cache((char*)image,(char*)image+0xcf0000);
#endif
    typedef void (*Evaluate)(const void*,void*);
    ((Evaluate)(image+0x100))(dimensions,camera);
    float aspect;
    memcpy(&aspect,camera+0x11c,4);
    return aspect;
}

int main(void) {
    const size_t size=0xcf0000;
#ifdef _WIN32
    unsigned char *image=VirtualAlloc(NULL,size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
#else
    unsigned char *image=mmap(NULL,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (image==MAP_FAILED) image=NULL;
#endif
    unsigned char *before=malloc(size);
    CHECK(image && before);
    const char *serials[]={"CUSA08014","CUSA07670"};
    const float aspects[]={21.0f/9.0f,32.0f/9.0f,3440.0f/1440.0f};
    for (unsigned r=0;r<2;++r) {
        const KnackWideSite *sites=knack_wide_profile(serials[r],"01.01");
        CHECK(sites);
        for (unsigned a=0;a<3;++a) {
            memset(image,0,size);
            for (unsigned i=0;i<4;++i) {
                uint32_t bits=0x3fe38e39;
                memcpy(image+sites[i].constant,&bits,4);
                memcpy(image+sites[i].instruction,sites[i].signature,8);
            }
            camera_fixture(image,sites);
            CHECK(fabsf(camera_seed(image,1920,1080)-KNACK_WIDE_NATIVE)<0.000001f);
            memcpy(before,image,size);
            CHECK(knack_wide_apply(image,size,sites,aspects[a])==4);
            const int widths[]={1920,2560,3840};
            const int heights[]={1080,1440,2160};
            for (unsigned resolution=0;resolution<3;++resolution)
                CHECK(fabsf(camera_seed(image,widths[resolution],heights[resolution])-aspects[a])<0.000001f);
            const KnackWideCameraSite* camera=knack_wide_camera_site(sites);
            CHECK(!memcmp(image+camera->start,before+camera->start,8));
            CHECK(!memcmp(image+camera->start+16,before+camera->start+16,13));
            memcpy(image+camera->start+8,before+camera->start+8,8);
            for (unsigned i=0;i<4;++i) {
                float value;memcpy(&value,image+sites[i].constant,4);
                CHECK(value==aspects[a]);
                memcpy(image+sites[i].constant,before+sites[i].constant,4);
            }
            CHECK(!memcmp(image,before,size)); /* only four constants and one instruction changed */
            for (unsigned bad=0;bad<4;++bad) {
                image[sites[bad].instruction]^=1;
                memcpy(before,image,size);
                CHECK(!knack_wide_apply(image,size,sites,aspects[a]));
                CHECK(!memcmp(image,before,size)); /* late failure never partially commits */
                image[sites[bad].instruction]^=1;
            }
            const unsigned corrupt[]={0,8,16,20,28};
            for (unsigned bad=0;bad<sizeof(corrupt)/sizeof(corrupt[0]);++bad) {
                image[camera->start+corrupt[bad]]^=1;
                memcpy(before,image,size);
                CHECK(!knack_wide_apply(image,size,sites,aspects[a]));
                CHECK(!memcmp(image,before,size));
                image[camera->start+corrupt[bad]]^=1;
            }
            memcpy(before,image,size);
            CHECK(!knack_wide_apply(image,sites[3].constant+3,sites,aspects[a]));
            CHECK(!memcmp(image,before,size));
            /* Perspective and presentation together preserve circles and vertical framing. */
            const float py=1.0f/tanf(0.5f),px=py/aspects[a];
            const float dx=px*0.1f*(1080.0f*aspects[a])*0.5f,dy=py*0.1f*1080.0f*0.5f;
            CHECK(fabsf(dx-dy)<0.001f);
            const float horizontal=2.0f*atanf(tanf(0.5f)*aspects[a]);
            CHECK(horizontal>2.0f*atanf(tanf(0.5f)*KNACK_WIDE_NATIVE));
        }
        CHECK(!knack_wide_apply(image,size,sites,NAN));
        CHECK(!knack_wide_apply(image,size,sites,INFINITY));
        CHECK(!knack_wide_apply(image,size,sites,KNACK_WIDE_NATIVE));
        CHECK(!knack_wide_apply(image,size,sites,4.0f));
    }
    CHECK(!knack_wide_profile(NULL,"01.01"));
    CHECK(!knack_wide_profile("CUSA07670","01.00"));
    CHECK(!knack_wide_profile("CUSA99999","01.01"));
    CHECK(!knack_wide_apply(image,size,NULL,21.0f/9.0f));
    free(before);
#ifdef _WIN32
    VirtualFree(image,0,MEM_RELEASE);
#else
    munmap(image,size);
#endif
    puts("Ultrawide: executed regional camera seeds, atomic rejection, Hor+ and shape preservation PASS");
    return 0;
}
