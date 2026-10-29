/* Execute the generated native stubs; no game or audio device is started. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "../port/src/knack_dialogue_gain.h"

typedef float (__attribute__((sysv_abi)) *Probe)(void *,unsigned);
static unsigned char *page;
static size_t cursor;
static void bytes(const unsigned char *p,size_t n) { memcpy(page+cursor,p,n);cursor+=n; }
static void hex1(unsigned char x) { bytes(&x,1); }
static void u64(uint64_t x) { bytes((const unsigned char *)&x,8); }
static void store_rax(size_t at) {
    static const unsigned char op[]={0x48,0x89,0x05};bytes(op,3);
    int32_t d=(int32_t)(at-(cursor+4));bytes((const unsigned char *)&d,4);
}
static void store_rdx(size_t at) {
    static const unsigned char op[]={0x48,0x89,0x15};bytes(op,3);
    int32_t d=(int32_t)(at-(cursor+4));bytes((const unsigned char *)&d,4);
}
static void indirect_jump(uintptr_t target) {
    static const unsigned char j[]={0xff,0x25,0,0,0,0};bytes(j,6);u64(target);
}
static Probe make_probe(int clamp,unsigned site,uintptr_t caller_a,uintptr_t caller_b) {
#ifdef _WIN32
    page=VirtualAlloc(NULL,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);assert(page);
#else
    page=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(page!=MAP_FAILED);
#endif
    cursor=0;
    if (clamp) {
        static const unsigned char p[]={0x55,0x48,0x89,0xfd};bytes(p,sizeof(p)); /* saved RBP; frame=RDI */
    } else {
        static const unsigned char p[]={0x53,0x41,0x57,0x49,0x89,0xff,0x48,0x89,0xfb,0x89,0xf1};
        bytes(p,sizeof(p)); /* save RBX/R15; both roots=RDI; category=ESI */
    }
    static const unsigned char flags[]={0x31,0xc0,0x39,0xc0,0x9c,0x58};bytes(flags,sizeof(flags));
    store_rax(0x380); /* expected flags before the generated stub */
    hex1(0x48);hex1(0xb8);u64(UINT64_C(0x1122334455667788));
    hex1(0x48);hex1(0xba);u64(UINT64_C(0x778899aabbccddee));
    indirect_jump((uintptr_t)(page+0x180));assert(cursor<0x80);
    cursor=0x80;store_rax(0x388);store_rdx(0x390);
    hex1(0x9c);hex1(0x58);store_rax(0x398);
    static const unsigned char xmm0[]={0xc5,0xf8,0x28};bytes(xmm0,3);hex1(clamp?0xc1:0xc2);
    if (clamp) hex1(0x5d);else {hex1(0x41);hex1(0x5f);hex1(0x5b);}
    hex1(0xc3);
    size_t n=clamp ? dialogue_clamp_stub(page+0x180,128,(uintptr_t)(page+0x80),caller_a,caller_b) :
                    dialogue_load_stub(page+0x180,128,site,(uintptr_t)(page+0x80));
    assert(n && n<=128);
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(),page,4096);
#else
    __builtin___clear_cache((char *)page,(char *)page+4096);
#endif
    return (Probe)(uintptr_t)page;
}
static void preserved(void) {
    uint64_t before,after,a,d;
    memcpy(&before,page+0x380,8);memcpy(&after,page+0x398,8);
    memcpy(&a,page+0x388,8);memcpy(&d,page+0x390,8);
    assert(before==after && a==UINT64_C(0x1122334455667788) && d==UINT64_C(0x778899aabbccddee));
}
static void release_probe(void) {
#ifdef _WIN32
    assert(VirtualFree(page,0,MEM_RELEASE));
#else
    assert(!munmap(page,4096));
#endif
}
static void test_categories(void) {
    unsigned char root[160]={0};
    const float input[]={0.f,0.1f,0.5f,0.9f,1.f};
    for (unsigned site=0;site<2;++site) {
        Probe fn=make_probe(0,site,0,0);
        for (unsigned v=0;v<sizeof(input)/sizeof(*input);++v) {
            for (unsigned category=0;category<8;++category) {
                memcpy(root+0x68+category*4,input+v,4);
                float actual=fn(root,category),expected=input[v]*(category==2 ? 1.5f : 1.f);
                assert(fabsf(actual-expected)<1e-6f);preserved();
                float unchanged;memcpy(&unchanged,root+0x68+category*4,4);assert(unchanged==input[v]);
            }
        }
        release_probe();
    }
    puts("PASS: both evaluators, Voice +50%, SE/BGM/other categories unchanged, silence and config storage preserved");
}
static void test_clamp(void) {
    uintptr_t a=UINT64_C(0x10105bfdbe),b=UINT64_C(0x10105c0579);
    Probe fn=make_probe(1,0,a,b);
    unsigned char entries[0x160]={0},root[128]={0},frame_space[128]={0};
    unsigned char *frame=frame_space+64;uintptr_t base=(uintptr_t)entries;
    memcpy(root,&base,8);uintptr_t root_address=(uintptr_t)root,offset=0x58;
    for (unsigned caller=0;caller<3;++caller) {
        uintptr_t return_site=caller==0 ? a : caller==1 ? b : a+1;
        memcpy(frame+8,&return_site,8);
        for (int category=-1;category<8;++category) {
            memset(frame-40,0,40);
            if (!caller) {memcpy(frame-8,&root_address,8);memcpy(frame-40,&offset,8);}
            if (caller==1) {memcpy(frame-40,&root_address,8);memcpy(frame-32,&offset,8);}
            entries[offset+0x10]=(unsigned char)category;
            float cap=fn(frame,0);
            assert(cap==((caller<2 && category==2) ? 1.5f : 1.f));preserved();
            /* End-to-end native clamp semantics: every normalized Voice level
             * receives 1.5x, including maximum; other sources remain identical. */
            for (unsigned n=0;n<=100;++n) {
                float value=n/100.f;
                float scaled=value*((caller<2 && category==2)?1.5f:1.f);
                float clamped=fminf(cap,fmaxf(0.f,scaled));
                assert(fabsf(clamped-scaled)<1e-6f);
            }
        }
    }
    release_probe();puts("PASS: category-specific clamp, both known callers, unrelated callers, RFLAGS and scratch registers");
}
static void test_bounds(void) {
    assert(dialogue_profile_matches("CUSA08014","01.01"));
    assert(dialogue_profile_matches("CUSA08014","01.00"));
    assert(!dialogue_profile_matches("CUSA07670","01.00"));
    assert(!dialogue_profile_matches("CUSA08014","01.02"));
    assert(!dialogue_profile_matches("CUSA00223","01.01"));
    assert(!dialogue_profile_matches(NULL,"01.01"));
    unsigned char out[128];memset(out,0xa5,sizeof(out));
    assert(!dialogue_load_stub(out,39,0,0));assert(out[0]==0xa5);
    assert(!dialogue_load_stub(out,128,2,0));assert(!dialogue_clamp_stub(out,109,0,0,0));
    assert(!dialogue_jump_bytes(100,200,4,out));assert(!dialogue_jump_bytes(UINTPTR_MAX,200,6,out));
    assert(!dialogue_jump_bytes(0,UINTPTR_MAX,7,out));
    assert(dialogue_jump_bytes(100,200,7,out));int32_t d;memcpy(&d,out+1,4);assert(d==95 && out[0]==0xe9 && out[5]==0x90 && out[6]==0x90);
    assert(dialogue_jump_bytes(200,100,7,out));memcpy(&d,out+1,4);assert(d==-105);
    for (unsigned s=0;s<3;++s) {
        assert(dialogue_signature_matches(dialogue_signatures[s],8,s));
        for (unsigned i=0;i<dialogue_lengths[s];++i) {
            unsigned char copy[8];memcpy(copy,dialogue_signatures[s],8);copy[i]^=1;
            assert(!dialogue_signature_matches(copy,8,s));
        }
    }
    puts("PASS: profile/signature/range/capacity rejection before modification");
}
static void test_image(const char *path) {
    FILE *fp=fopen(path,"rb");assert(fp);assert(!fseek(fp,0,SEEK_END));long n=ftell(fp);assert(n>0);rewind(fp);
    unsigned char *image=malloc((size_t)n);assert(image);assert(fread(image,1,(size_t)n,fp)==(size_t)n);fclose(fp);
    assert(dialogue_image_matches(image,(uint64_t)n));
    assert(!dialogue_image_matches(image,0xb28335));
    uint64_t locations[]={0x5bfd6f,0x5c052f,0xb2832e,0xb281c0,0x5bfdb9,0x5c0574};
    for (unsigned i=0;i<sizeof(locations)/sizeof(*locations);++i) {
        image[locations[i]]^=1;assert(!dialogue_image_matches(image,(uint64_t)n));image[locations[i]]^=1;
    }
    free(image);puts("PASS: real prepared image signatures and frame layout; corrupted sites rejected");
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    test_bounds();test_categories();test_clamp();if (argc==2)test_image(argv[1]);
    puts("PASS: dialogue gain contracts");return 0;
}
