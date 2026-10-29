/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic test suite for Knack 2 NGS2 HLE-2.2B Real PCM Mixer.
 * Covers 25 mandatory and adversarial requirements without game binaries, GPU, or audio hardware.
 */
#include "native_test_platform.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../port/src/runtime_tlg.c"

void runtime_restart(void) { assert(!"Unexpected restart in NGS2 test"); }
int64_t runtime_file_getdents(int fd,char *buffer,uint64_t size,int64_t *base) {
    (void)fd; (void)buffer; (void)size; (void)base; return -1;
}
void runtime_wait_begin(const char *kind,const void *object,const char *name,
                        const void *const *owner,uintptr_t guest_frame) {
    (void)kind; (void)object; (void)name; (void)owner; (void)guest_frame;
}
void runtime_wait_end(void) {}
const char *runtime_symbol(const char *name) { return name; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int runtime_memory_copy_snapshot(void *dst,uintptr_t address,uint64_t size) {
    if (!dst || !address || !size || size>SIZE_MAX) return 0;
    memcpy(dst,(const void *)address,(size_t)size); return 1;
}

/* Helper functions for constructing NGS2 structures and commands */
static void put_u16(void *data,size_t offset,uint16_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void put_u32(void *data,size_t offset,uint32_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}
static void put_uptr(void *data,size_t offset,uintptr_t value) {
    memcpy((unsigned char *)data+offset,&value,sizeof(value));
}

static void command(void *data,uint16_t size,uint32_t id) {
    memset(data,0,size); put_u16(data,0,size); put_u32(data,4,id);
}
static void system_option(unsigned char option[0x40]) {
    memset(option,0,0x40); put_u32(option,0,0x40); put_u32(option,0x20,256); put_u32(option,0x24,48000);
}
static void rack_option(unsigned char option[0xa8],uint32_t capacity,const char *name) {
    memset(option,0,0xa8); put_u32(option,0,0xa8); put_u32(option,0x20,capacity);
    snprintf((char *)option+8,16,"%s",name);
}
static void setup_waveform(Ngs2Handle *voice,uint32_t channels) {
    unsigned char cmd[0x28]; command(cmd,sizeof(cmd),UINT32_C(0x10000000));
    put_u32(cmd,8,0x40); put_u32(cmd,0x0c,channels); put_u32(cmd,0x10,48000);
    put_u32(cmd,0x14,UINT32_C(0xfe7a17f0));
    assert(!ngs2_voice_control(voice,cmd));
}
static void set_voice_state(Ngs2Handle *voice,uint32_t event) {
    unsigned char cmd[0x0c]; command(cmd,sizeof(cmd),6); put_u32(cmd,8,event);
    assert(!ngs2_voice_control(voice,cmd));
}
static void set_voice_matrix(Ngs2Handle *voice,const float *matrix,uint32_t count) {
    unsigned char cmd[0x18]; command(cmd,sizeof(cmd),1);
    put_u32(cmd,0x0c,count); put_uptr(cmd,0x10,(uintptr_t)matrix);
    assert(!ngs2_voice_control(voice,cmd));
}
static void push_synthetic_grain(Ngs2Voice *voice,int16_t sample_value) {
    Ngs2Atrac9Voice *state=voice->atrac9;
    if (!state) {
        state=calloc(1,sizeof(*state));
        state->generation=1;
        voice->atrac9=state;
    }
    assert(state->pcm_count<NGS2_ATRAC9_FIFO_CAPACITY);
    Ngs2PcmGrain *grain=&state->fifo[state->pcm_write];
    grain->generation=state->generation;
    grain->frames=256;
    grain->channels=voice->waveform.channels;
    for (uint32_t f=0;f<256;++f) {
        for (uint32_t c=0;c<grain->channels;++c) {
            grain->pcm[f*grain->channels+c]=sample_value;
        }
    }
    state->pcm_write=(state->pcm_write+1u)%NGS2_ATRAC9_FIFO_CAPACITY;
    ++state->pcm_count;
}

/* =========================================================================
 * MANDATORY TESTS (1 to 25)
 * ========================================================================= */

/* 1. Mixer desativado: silêncio byte a byte */
static void test_01_mixer_disabled_silence(void) {
    printf("Running Test 01: Mixer disabled byte-for-byte silence...\n");
    ngs2_atrac9_setting=0;
    ngs2_render_mix_setting=0;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    float buf0[256*8],buf1[256*8];
    memset(buf0,0xa5,sizeof(buf0));
    memset(buf1,0xa5,sizeof(buf1));
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!ngs2_render(sys,renders,2));

    const unsigned char *b0=(const unsigned char *)buf0;
    const unsigned char *b1=(const unsigned char *)buf1;
    for (size_t i=0;i<sizeof(buf0);++i) assert(b0[i]==0);
    for (size_t i=0;i<sizeof(buf1);++i) assert(b1[i]==0);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 2. ATRAC9 ativo com mixer desativado: silêncio e FIFO não consumida */
static void test_02_atrac9_active_mixer_disabled(void) {
    printf("Running Test 02: ATRAC9 active with mixer disabled...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=0;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4);
    set_voice_state(v0,0); /* PLAYING */

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(v0);
    push_synthetic_grain(voice,16384);
    assert(voice->atrac9->pcm_count==1);
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    memset(buf0,0x5a,sizeof(buf0));
    memset(buf1,0x5a,sizeof(buf1));
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!ngs2_render(sys,renders,2));

    /* Verify silence */
    for (size_t i=0;i<256*8;++i) {
        assert(buf0[i]==0.0f);
        assert(buf1[i]==0.0f);
    }

    /* Verify FIFO was NOT drained */
    pthread_mutex_lock(&ngs2_lock);
    voice=ngs2_voice_locked(v0);
    assert(voice->atrac9->pcm_count==1);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* Retry wrapper for unit tests to tolerate momentary microsecond lock collision with background worker */
static int test_render_retry(Ngs2Handle *sys, const Ngs2RenderBuffer *renders, uint32_t count) {
    for (int retry=0; retry<100; ++retry) {
        pthread_mutex_lock(&ngs2_lock);
        Ngs2System *s = ngs2_system_locked(sys);
        uint64_t r0 = s ? (s->main_renders + s->ambiguous_renders_ignored) : 0;
        pthread_mutex_unlock(&ngs2_lock);

        int rc = ngs2_render(sys, renders, count);
        if (rc != 0) return rc;

        pthread_mutex_lock(&ngs2_lock);
        s = ngs2_system_locked(sys);
        uint64_t r1 = s ? (s->main_renders + s->ambiguous_renders_ignored) : 0;
        pthread_mutex_unlock(&ngs2_lock);

        if (r1 > r0) return 0; /* Render successfully executed */
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
    return 0;
}

/* 3. Mixer ativo e matriz esparsa de quatro canais */
static void test_03_sparse_four_channel_matrix(void) {
    printf("Running Test 03: Sparse 4-channel matrix routing...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4);
    set_voice_state(v0,0); /* PLAYING */

    /* Destination-Major 4ch matrix: dst * 4 + src
     * Send src 0 -> FL (dst 0) with gain 1.0. All other 31 slots = 0.0 */
    float mat[32]; memset(mat,0,sizeof(mat));
    mat[0*4 + 0] = 1.0f; /* FL <- src0 */
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(v0);
    /* Push grain with sample 16384 (0.5f) */
    push_synthetic_grain(voice,16384);
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    memset(buf0,0,sizeof(buf0));
    memset(buf1,0,sizeof(buf1));
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!test_render_retry(sys,renders,2));

    /* Expected: FL = 0.5f * 1.0f * 0.25f (attenuation) = 0.125f
     * All other channels (1..7) must be exactly 0.0f */
    for (uint32_t f=0;f<256;++f) {
        float fl=buf0[f*8 + 0];
        if (fabsf(fl-0.125f)>=1e-5f) {
            printf("Test 03 FAIL: f=%u fl=%.6f\n", f, fl);
        }
        assert(fabsf(fl-0.125f)<1e-5f);
        for (uint32_t dst=1;dst<8;++dst) {
            assert(buf0[f*8 + dst]==0.0f);
        }
        for (uint32_t dst=0;dst<8;++dst) {
            assert(buf1[f*8 + dst]==0.0f);
        }
    }

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 4. Matriz de oito canais */
static void test_04_eight_channel_matrix(void) {
    printf("Running Test 04: 8-channel matrix identity routing...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,8);
    set_voice_state(v0,0);

    /* 8ch identity matrix: 64 floats */
    float mat[64]; memset(mat,0,sizeof(mat));
    for (uint32_t ch=0;ch<8;++ch) mat[ch*8+ch]=1.0f;
    set_voice_matrix(v0,mat,64);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(v0);
    Ngs2Atrac9Voice *state=voice->atrac9;
    if (!state) {
        state=calloc(1,sizeof(*state));
        state->generation=1;
        voice->atrac9=state;
    }
    Ngs2PcmGrain *grain=&state->fifo[state->pcm_write];
    grain->generation=state->generation;
    grain->frames=256; grain->channels=8;
    for (uint32_t f=0;f<256;++f) {
        for (uint32_t c=0;c<8;++c) {
            grain->pcm[f*8+c]=(int16_t)((c+1)*2048); /* 2048, 4096, 6144, ... */
        }
    }
    state->pcm_write=(state->pcm_write+1u)%NGS2_ATRAC9_FIFO_CAPACITY;
    ++state->pcm_count;
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    memset(buf0,0,sizeof(buf0));
    memset(buf1,0,sizeof(buf1));
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!test_render_retry(sys,renders,2));

    for (uint32_t f=0;f<256;++f) {
        for (uint32_t c=0;c<8;++c) {
            float expected=((float)((c+1)*2048)/32768.0f)*0.25f;
            assert(fabsf(buf0[f*8+c]-expected)<1e-5f);
        }
    }

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 5. Duas vozes simultâneas */
static void test_05_two_simultaneous_voices(void) {
    printf("Running Test 05: Two simultaneous voices mixed together...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL,*v1=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));
    assert(!ngs2_voice_handle(rack,1,&v1));

    setup_waveform(v0,4); set_voice_state(v0,0);
    setup_waveform(v1,4); set_voice_state(v1,0);

    /* Both voices route src 0 -> dst 0 (FL) with gain 1.0 */
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);
    set_voice_matrix(v1,mat,32);

    /* Voice 0 sample = 8192 (0.25f), Voice 1 sample = 16384 (0.5f) */
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),8192);
    push_synthetic_grain(ngs2_voice_locked(v1),16384);
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    memset(buf0,0,sizeof(buf0)); memset(buf1,0,sizeof(buf1));
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!test_render_retry(sys,renders,2));

    /* Expected FL = (0.25f + 0.5f) * 0.25f = 0.1875f */
    for (uint32_t f=0;f<256;++f) {
        assert(fabsf(buf0[f*8+0]-0.1875f)<1e-5f);
        for (uint32_t d=1;d<8;++d) assert(buf0[f*8+d]==0.0f);
    }

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 6. Acúmulo F32 correto e atenuação */
static void test_06_f32_accumulation_and_attenuation(void) {
    printf("Running Test 06: F32 conversion and -12dB attenuation precision...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat));
    mat[0] = 2.0f; /* gain = 2.0 */
    set_voice_matrix(v0,mat,32);

    /* Negative S16 sample: -16384 -> -0.5f
     * Expected = -0.5f * 2.0f * 0.25f = -0.25f */
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),-16384);
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!test_render_retry(sys,renders,2));

    for (uint32_t f=0;f<256;++f) {
        assert(fabsf(buf0[f*8+0] - (-0.25f))<1e-5f);
    }

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 7. NaN, Inf e clipping */
static void test_07_nan_inf_and_clipping(void) {
    printf("Running Test 07: NaN, Inf, and hard clipping in [-1.0, 1.0]...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat));
    mat[0] = (float)NAN;
    mat[1] = (float)INFINITY;
    mat[4] = 50.0f; /* Huge gain for dst 1 (FR) -> should clamp to 1.0 */
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),32767);
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!test_render_retry(sys,renders,2));

    for (uint32_t i=0;i<256*8;++i) {
        assert(isfinite(buf0[i]));
        assert(buf0[i]>=-1.0f && buf0[i]<=1.0f);
    }
    /* FL (with NaN/Inf in mat[0], mat[1]) must be 0.0f, not NaN */
    assert(buf0[0]==0.0f);
    /* FR (with gain 50.0f) must be clamped to 1.0f */
    assert(buf0[1]==1.0f);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 8. Descritor inválido */
static void test_08_invalid_descriptors(void) {
    printf("Running Test 08: Invalid descriptor validation...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40]; system_option(sysopt);
    Ngs2Handle *sys=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));

    float buf0[256*8];
    Ngs2RenderBuffer valid={buf0,sizeof(buf0),0x18,8};
    assert(ngs2_render(NULL,&valid,1)==NGS2_INVALID);
    assert(ngs2_render(sys,NULL,1)==NGS2_INVALID);

    Ngs2RenderBuffer invalid_buf={NULL,8192,0x18,8};
    assert(ngs2_render(sys,&invalid_buf,1)==NGS2_INVALID);

    Ngs2RenderBuffer oversized={buf0,NGS2_MAX_RENDER_BYTES+1,0x18,8};
    assert(ngs2_render(sys,&oversized,1)==NGS2_INVALID);

    /* Size 4096 (wrong size) with count > 1 -> buffers zeroed, return 0, no mix */
    float buf_half[1024]; memset(buf_half,0xa5,sizeof(buf_half));
    Ngs2RenderBuffer wrong_size[2]={
        {buf_half,sizeof(buf_half),0x18,8},
        {buf_half,sizeof(buf_half),0x18,8}
    };
    assert(!ngs2_render(sys,wrong_size,2));
    for (size_t i=0;i<sizeof(buf_half);++i) assert(((unsigned char *)buf_half)[i]==0);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 9. count=1 secundário: silêncio sem consumo */
static void test_09_count_one_ambiguous_path(void) {
    printf("Running Test 09: Ambiguous render path (count=1) silent without FIFO drain...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    pthread_mutex_unlock(&ngs2_lock);

    /* Call with count=1 */
    float buf0[256*8]; memset(buf0,0x55,sizeof(buf0));
    Ngs2RenderBuffer render_sec={buf0,sizeof(buf0),0x18,8};
    assert(!test_render_retry(sys,&render_sec,1));

    /* Buffer must be silent */
    for (uint32_t i=0;i<256*8;++i) assert(buf0[i]==0.0f);

    /* FIFO pcm_count MUST still be 1 (undrained) */
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(v0);
    assert(voice->atrac9->pcm_count==1);
    Ngs2System *sys_obj=ngs2_system_locked(sys);
    assert(sys_obj->ambiguous_renders_ignored==1);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 10. Múltiplos buffers: consumo único por voz */
static void test_10_multiple_buffers_single_consumption(void) {
    printf("Running Test 10: Multiple buffers drain exactly ONE grain per voice...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==3);
    pthread_mutex_unlock(&ngs2_lock);

    float b0[256*8],b1[256*8],b2[256*8],b3[256*8];
    Ngs2RenderBuffer renders[4]={
        {b0,sizeof(b0),0x18,8},
        {b1,sizeof(b1),0x18,8},
        {b2,sizeof(b2),0x18,8},
        {b3,sizeof(b3),0x18,8}
    };
    assert(!test_render_retry(sys,renders,4));

    /* Exactly ONE grain consumed: pcm_count becomes 2 */
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==2);
    pthread_mutex_unlock(&ngs2_lock);

    /* b0 has audio, b1..b3 are silent */
    assert(fabsf(b0[0]-0.125f)<1e-5f);
    assert(b1[0]==0.0f && b2[0]==0.0f && b3[0]==0.0f);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 11. FIFO vazia e cheia */
static void test_11_fifo_empty_and_full(void) {
    printf("Running Test 11: Empty FIFO underrun and full FIFO operation...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0);
    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };

    /* Render with EMPTY FIFO: produces silence, non-blocking, counts underrun */
    assert(!test_render_retry(sys,renders,2));
    assert(buf0[0]==0.0f);
    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *s_obj=ngs2_system_locked(sys);
    assert(s_obj->underruns==1);

    /* Fill FIFO completely (8 grains) */
    Ngs2Voice *voice=ngs2_voice_locked(v0);
    for (int i=0;i<8;++i) push_synthetic_grain(voice,8000);
    assert(voice->atrac9->pcm_count==8);
    pthread_mutex_unlock(&ngs2_lock);

    /* Render with FULL FIFO: drains 1 grain */
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);
    assert(!test_render_retry(sys,renders,2));
    assert(buf0[0]!=0.0f);
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==7);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 12. Pause e Resume */
static void test_12_pause_and_resume(void) {
    printf("Running Test 12: Pause and Resume FIFO behavior...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0); /* PLAYING */
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    pthread_mutex_unlock(&ngs2_lock);

    /* Pause the voice */
    set_voice_state(v0,4); /* PAUSE */

    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };
    assert(!test_render_retry(sys,renders,2));
    assert(buf0[0]==0.0f); /* Silence */
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==1); /* FIFO untouched */
    pthread_mutex_unlock(&ngs2_lock);

    /* Resume the voice */
    set_voice_state(v0,5); /* RESUME */
    assert(!test_render_retry(sys,renders,2));
    assert(fabsf(buf0[0]-0.125f)<1e-5f); /* Audio produced */
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==0); /* Drained */
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cond_start;
    pthread_cond_t cond_done;
    int worker_action; /* 1: Stop, 2: RackDestroy, 3: SystemDestroy, 4: GenChange,
                        * 5: ConcurrentRender, 6: PublicationContention, 7: MutateDescriptor */
    unsigned delay_before_action_ms;
    int hook_reached;
    int worker_finished;
    int test_done;
    void *target_handle;
    const Ngs2RenderBuffer *concurrent_renders;
    uint32_t concurrent_render_count;
    int concurrent_render_rc;
    void *replacement_buffer;
    float *shared_probe;
    float shared_marker;
    int shared_preserved;
} TestConcurrentContext;

static void *test_concurrent_worker_thread(void *arg) {
    TestConcurrentContext *ctx=(TestConcurrentContext *)arg;
    pthread_mutex_lock(&ctx->mtx);
    while (!ctx->hook_reached && !ctx->test_done) {
        pthread_cond_wait(&ctx->cond_start,&ctx->mtx);
    }
    if (ctx->test_done) {
        pthread_mutex_unlock(&ctx->mtx);
        return NULL;
    }
    pthread_mutex_unlock(&ctx->mtx);

    if (ctx->delay_before_action_ms) {
#ifdef _WIN32
        Sleep(ctx->delay_before_action_ms);
#else
        usleep(ctx->delay_before_action_ms*1000u);
#endif
    }

    /* Execute the concurrent action while the main thread is inside the mix window */
    switch (ctx->worker_action) {
    case 1: /* Stop voice */
        set_voice_state((Ngs2Handle *)ctx->target_handle, 2);
        break;
    case 2: /* Destroy rack */
        ngs2_rack_destroy((Ngs2Handle *)ctx->target_handle, NULL);
        break;
    case 3: /* Destroy system */
        ngs2_system_destroy((Ngs2Handle *)ctx->target_handle, NULL);
        break;
    case 4: { /* Change generation */
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *v = ngs2_voice_locked((Ngs2Handle *)ctx->target_handle);
        if (v && v->atrac9) ++v->atrac9->generation;
        pthread_mutex_unlock(&ngs2_lock);
        break;
    }
    case 5: /* Concurrent primary render */
        ctx->concurrent_render_rc = ngs2_render((Ngs2Handle *)ctx->target_handle,
                                                ctx->concurrent_renders,
                                                ctx->concurrent_render_count);
        break;
    case 6: /* Hold ngs2_lock across main-render publication */
        pthread_mutex_lock(&ngs2_lock);
        pthread_mutex_lock(&ctx->mtx);
        ctx->worker_finished=1;
        pthread_cond_signal(&ctx->cond_done);
        pthread_mutex_unlock(&ctx->mtx);
#ifdef _WIN32
        Sleep(50);
#else
        usleep(50000);
#endif
        pthread_mutex_unlock(&ngs2_lock);
        return NULL;
    case 7: /* Mutate caller-owned descriptor after render snapshot */
        ((Ngs2RenderBuffer *)ctx->target_handle)[0].buffer=ctx->replacement_buffer;
        break;
    }

    pthread_mutex_lock(&ctx->mtx);
    ctx->worker_finished = 1;
    pthread_cond_signal(&ctx->cond_done);
    pthread_mutex_unlock(&ctx->mtx);
    return NULL;
}

static void test_concurrent_mix_hook(void *arg) {
    TestConcurrentContext *ctx=(TestConcurrentContext *)arg;
    pthread_mutex_lock(&ctx->mtx);
    if (ctx->shared_probe) *ctx->shared_probe=ctx->shared_marker;
    ctx->hook_reached = 1;
    pthread_cond_signal(&ctx->cond_start);
    while (!ctx->worker_finished && !ctx->test_done) {
        pthread_cond_wait(&ctx->cond_done,&ctx->mtx);
    }
    if (ctx->shared_probe) ctx->shared_preserved=(*ctx->shared_probe==ctx->shared_marker);
    pthread_mutex_unlock(&ctx->mtx);
}

/* 13. Stop durante mixagem (Concorrência real com pthread) */
static void test_13_stop_during_mixing(void) {
    printf("Running Test 13: Stop during mixing (Real pthread concurrency)...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    for (int iter=0;iter<5;++iter) {
        unsigned char sysopt[0x40],rackopt[0xa8];
        system_option(sysopt); rack_option(rackopt,2,"Sampler");
        Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&sys));
        assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
        assert(!ngs2_voice_handle(rack,0,&v0));

        setup_waveform(v0,4); set_voice_state(v0,0);
        float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
        set_voice_matrix(v0,mat,32);

        pthread_mutex_lock(&ngs2_lock);
        push_synthetic_grain(ngs2_voice_locked(v0),16384);
        pthread_mutex_unlock(&ngs2_lock);

        TestConcurrentContext ctx;
        memset(&ctx,0,sizeof(ctx));
        pthread_mutex_init(&ctx.mtx,NULL);
        pthread_cond_init(&ctx.cond_start,NULL);
        pthread_cond_init(&ctx.cond_done,NULL);
        ctx.worker_action=1; /* Stop voice */
        ctx.target_handle=v0;

        ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

        pthread_t th;
        assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));

        float buf0[256*8],buf1[256*8];
        Ngs2RenderBuffer renders[2]={
            {buf0,sizeof(buf0),0x18,8},
            {buf1,sizeof(buf1),0x18,8}
        };
        assert(!ngs2_render(sys,renders,2));

        pthread_mutex_lock(&ctx.mtx);
        int reached=ctx.hook_reached;
        ctx.test_done=1;
        pthread_cond_signal(&ctx.cond_start);
        pthread_cond_signal(&ctx.cond_done);
        pthread_mutex_unlock(&ctx.mtx);

        assert(!pthread_join(th,NULL));
        ngs2_set_render_mix_hook(NULL,NULL);

        if (!reached) {
            --iter;
            assert(!ngs2_system_destroy(sys,NULL));
            pthread_mutex_destroy(&ctx.mtx);
            pthread_cond_destroy(&ctx.cond_start);
            pthread_cond_destroy(&ctx.cond_done);
            continue;
        }

        /* Publication must have aborted cleanly: output is silence */
        assert(buf0[0]==0.0f);

        pthread_mutex_lock(&ngs2_lock);
        Ngs2System *s_obj=ngs2_system_locked(sys);
        assert(s_obj->render_cancellations>=1);
        pthread_mutex_unlock(&ngs2_lock);

        assert(!ngs2_system_destroy(sys,NULL));
        pthread_mutex_destroy(&ctx.mtx);
        pthread_cond_destroy(&ctx.cond_start);
        pthread_cond_destroy(&ctx.cond_done);
    }
    puts("  -> PASS (5 iterations clean)");
}

/* 14. RackDestroy durante mixagem (Concorrência real com pthread) */
static void test_14_rack_destroy_quiescence(void) {
    printf("Running Test 14: RackDestroy during mixing (Real pthread concurrency)...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    for (int iter=0;iter<5;++iter) {
        unsigned char sysopt[0x40],rackopt[0xa8];
        system_option(sysopt); rack_option(rackopt,2,"Sampler");
        Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&sys));
        assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
        assert(!ngs2_voice_handle(rack,0,&v0));

        setup_waveform(v0,4); set_voice_state(v0,0);
        pthread_mutex_lock(&ngs2_lock);
        push_synthetic_grain(ngs2_voice_locked(v0),16384);
        pthread_mutex_unlock(&ngs2_lock);

        TestConcurrentContext ctx;
        memset(&ctx,0,sizeof(ctx));
        pthread_mutex_init(&ctx.mtx,NULL);
        pthread_cond_init(&ctx.cond_start,NULL);
        pthread_cond_init(&ctx.cond_done,NULL);
        ctx.worker_action=2; /* Destroy rack */
        ctx.target_handle=rack;

        ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

        pthread_t th;
        assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));

        float buf0[256*8],buf1[256*8];
        Ngs2RenderBuffer renders[2]={
            {buf0,sizeof(buf0),0x18,8},
            {buf1,sizeof(buf1),0x18,8}
        };
        assert(!ngs2_render(sys,renders,2));

        pthread_mutex_lock(&ctx.mtx);
        int reached=ctx.hook_reached;
        ctx.test_done=1;
        pthread_cond_signal(&ctx.cond_start);
        pthread_cond_signal(&ctx.cond_done);
        pthread_mutex_unlock(&ctx.mtx);

        assert(!pthread_join(th,NULL));
        ngs2_set_render_mix_hook(NULL,NULL);

        if (!reached) {
            --iter;
            assert(!ngs2_system_destroy(sys,NULL));
            pthread_mutex_destroy(&ctx.mtx);
            pthread_cond_destroy(&ctx.cond_start);
            pthread_cond_destroy(&ctx.cond_done);
            continue;
        }

        /* Rack was destroyed during mix: publication safely aborted with silence */
        assert(buf0[0]==0.0f);

        assert(!ngs2_system_destroy(sys,NULL));
        pthread_mutex_destroy(&ctx.mtx);
        pthread_cond_destroy(&ctx.cond_start);
        pthread_cond_destroy(&ctx.cond_done);
    }
    puts("  -> PASS (5 iterations clean)");
}

/* 15. SystemDestroy durante mixagem (Concorrência real com pthread) */
static void test_15_system_destroy_quiescence(void) {
    printf("Running Test 15: SystemDestroy during mixing (Real pthread concurrency)...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    for (int iter=0;iter<5;++iter) {
        unsigned char sysopt[0x40],rackopt[0xa8];
        system_option(sysopt); rack_option(rackopt,2,"Sampler");
        Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&sys));
        assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
        assert(!ngs2_voice_handle(rack,0,&v0));

        setup_waveform(v0,4); set_voice_state(v0,0);
        pthread_mutex_lock(&ngs2_lock);
        push_synthetic_grain(ngs2_voice_locked(v0),16384);
        pthread_mutex_unlock(&ngs2_lock);

        TestConcurrentContext ctx;
        memset(&ctx,0,sizeof(ctx));
        pthread_mutex_init(&ctx.mtx,NULL);
        pthread_cond_init(&ctx.cond_start,NULL);
        pthread_cond_init(&ctx.cond_done,NULL);
        ctx.worker_action=3; /* Destroy system */
        ctx.target_handle=sys;

        ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

        pthread_t th;
        assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));

        float buf0[256*8],buf1[256*8];
        Ngs2RenderBuffer renders[2]={
            {buf0,sizeof(buf0),0x18,8},
            {buf1,sizeof(buf1),0x18,8}
        };
        /* SystemDestroy happens before publication; render detects freed system safely */
        (void)ngs2_render(sys,renders,2);

        pthread_mutex_lock(&ctx.mtx);
        int reached=ctx.hook_reached;
        ctx.test_done=1;
        pthread_cond_signal(&ctx.cond_start);
        pthread_cond_signal(&ctx.cond_done);
        pthread_mutex_unlock(&ctx.mtx);

        assert(!pthread_join(th,NULL));
        ngs2_set_render_mix_hook(NULL,NULL);

        if (!reached) {
            --iter;
            assert(!ngs2_system_destroy(sys,NULL));
            pthread_mutex_destroy(&ctx.mtx);
            pthread_cond_destroy(&ctx.cond_start);
            pthread_cond_destroy(&ctx.cond_done);
            continue;
        }

        pthread_mutex_destroy(&ctx.mtx);
        pthread_cond_destroy(&ctx.cond_start);
        pthread_cond_destroy(&ctx.cond_done);
    }
    puts("  -> PASS (5 iterations clean)");
}

/* 16. Mudança de geração durante a mixagem (Concorrência real com pthread) */
static void test_16_generation_change_invalidation(void) {
    printf("Running Test 16: Generation change during mixing (Real pthread concurrency)...\n");
    fflush(stdout);
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    for (int iter=0;iter<5;++iter) {
        unsigned char sysopt[0x40],rackopt[0xa8];
        system_option(sysopt); rack_option(rackopt,2,"Sampler");
        Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&sys));
        assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
        assert(!ngs2_voice_handle(rack,0,&v0));

        setup_waveform(v0,4); set_voice_state(v0,0);
        float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
        set_voice_matrix(v0,mat,32);

        pthread_mutex_lock(&ngs2_lock);
        push_synthetic_grain(ngs2_voice_locked(v0),16384);
        pthread_mutex_unlock(&ngs2_lock);

        TestConcurrentContext ctx;
        memset(&ctx,0,sizeof(ctx));
        pthread_mutex_init(&ctx.mtx,NULL);
        pthread_cond_init(&ctx.cond_start,NULL);
        pthread_cond_init(&ctx.cond_done,NULL);
        ctx.worker_action=4; /* Increment generation */
        ctx.target_handle=v0;

        ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

        pthread_t th;
        assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));

        float buf0[256*8],buf1[256*8];
        Ngs2RenderBuffer renders[2]={
            {buf0,sizeof(buf0),0x18,8},
            {buf1,sizeof(buf1),0x18,8}
        };
        assert(!ngs2_render(sys,renders,2));

        pthread_mutex_lock(&ctx.mtx);
        int reached=ctx.hook_reached;
        ctx.test_done=1;
        pthread_cond_signal(&ctx.cond_start);
        pthread_cond_signal(&ctx.cond_done);
        pthread_mutex_unlock(&ctx.mtx);

        assert(!pthread_join(th,NULL));
        ngs2_set_render_mix_hook(NULL,NULL);

        if (!reached) {
            --iter;
            assert(!ngs2_system_destroy(sys,NULL));
            pthread_mutex_destroy(&ctx.mtx);
            pthread_cond_destroy(&ctx.cond_start);
            pthread_cond_destroy(&ctx.cond_done);
            continue;
        }

        /* Generation mismatch cancels publication and leaves output as silence */
        assert(buf0[0]==0.0f);

        pthread_mutex_lock(&ngs2_lock);
        Ngs2System *s_obj=ngs2_system_locked(sys);
        assert(s_obj->render_cancellations>=1);
        pthread_mutex_unlock(&ngs2_lock);

        assert(!ngs2_system_destroy(sys,NULL));
        pthread_mutex_destroy(&ctx.mtx);
        pthread_cond_destroy(&ctx.cond_start);
        pthread_cond_destroy(&ctx.cond_done);
    }
    puts("  -> PASS (5 iterations clean)");
}

typedef struct {
    pthread_mutex_t started_mtx;
    pthread_cond_t started_cond;
    int holding_lock;
} ContentionThreadArg;

static void *contention_holder_thread(void *arg) {
    ContentionThreadArg *c=(ContentionThreadArg *)arg;
    pthread_mutex_lock(&ngs2_lock);
    pthread_mutex_lock(&c->started_mtx);
    c->holding_lock=1;
    pthread_cond_signal(&c->started_cond);
    pthread_mutex_unlock(&c->started_mtx);

    /* Sleep while holding ngs2_lock to test that render does not block */
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif

    pthread_mutex_unlock(&ngs2_lock);
    return NULL;
}

/* 17. Contenção real do mutex e medição de duração */
static void test_17_trylock_contention(void) {
    printf("Running Test 17: Mutex contention with real duration measurement...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    pthread_mutex_unlock(&ngs2_lock);

    ContentionThreadArg carg;
    memset(&carg,0,sizeof(carg));
    pthread_mutex_init(&carg.started_mtx,NULL);
    pthread_cond_init(&carg.started_cond,NULL);

    pthread_t cth;
    assert(!pthread_create(&cth,NULL,contention_holder_thread,&carg));

    pthread_mutex_lock(&carg.started_mtx);
    while (!carg.holding_lock) {
        pthread_cond_wait(&carg.started_cond,&carg.started_mtx);
    }
    pthread_mutex_unlock(&carg.started_mtx);

    float buf0[256*8],buf1[256*8];
    memset(buf0,0xaa,sizeof(buf0));
    memset(buf1,0xbb,sizeof(buf1));
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };

    /* ngs2_lock is held by cth for 50ms. Measure render duration: */
    uint64_t t0=ngs2_now_us();
    assert(!ngs2_render(sys,renders,2));
    uint64_t t1=ngs2_now_us();
    uint64_t elapsed_us=t1-t0;

    printf("  Contention render latency: %llu us (must be < 10000 us)\n",(unsigned long long)elapsed_us);
    assert(elapsed_us<10000); /* Proves render did NOT block on the 50ms lock */
    for (size_t i=0;i<sizeof(buf0);++i) assert(((unsigned char *)buf0)[i]==0xaa);
    for (size_t i=0;i<sizeof(buf1);++i) assert(((unsigned char *)buf1)[i]==0xbb);

    assert(!pthread_join(cth,NULL));
    pthread_mutex_destroy(&carg.started_mtx);
    pthread_cond_destroy(&carg.started_cond);

    /* Verify grain was NOT drained during contention */
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==1);
    pthread_mutex_unlock(&ngs2_lock);

    /* Now that lock is free, render succeeds and drains */
    assert(!test_render_retry(sys,renders,2));
    assert(fabsf(buf0[0]-0.125f)<1e-5f);
    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 18. Concorrência de render principal e teste com 33, 64 e 128 vozes */
static void test_18_more_than_16_voices_fairness(void) {
    printf("Running Test 18: Concurrent primary renders and 33, 64, 128 voice safe-fail...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    /* Part A: 24 active voices within real-time capacity (<= 32) */
    {
        unsigned char sysopt[0x40],rackopt[0xa8];
        system_option(sysopt); rack_option(rackopt,24,"Sampler");
        Ngs2Handle *sys=NULL,*rack=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&sys));
        assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));

        float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
        for (uint32_t i=0;i<24;++i) {
            Ngs2Handle *vh=NULL;
            assert(!ngs2_voice_handle(rack,i,&vh));
            setup_waveform(vh,4);
            set_voice_state(vh,0);
            set_voice_matrix(vh,mat,32);

            pthread_mutex_lock(&ngs2_lock);
            push_synthetic_grain(ngs2_voice_locked(vh),1024);
            pthread_mutex_unlock(&ngs2_lock);
        }

        float buf0[256*8],buf1[256*8];
        Ngs2RenderBuffer renders[2]={
            {buf0,sizeof(buf0),0x18,8},
            {buf1,sizeof(buf1),0x18,8}
        };
        assert(!test_render_retry(sys,renders,2));

        pthread_mutex_lock(&ngs2_lock);
        Ngs2System *s_obj=ngs2_system_locked(sys);
        assert(s_obj->voices_mixed==24);
        assert(s_obj->grains_consumed==24);
        pthread_mutex_unlock(&ngs2_lock);
        assert(!ngs2_system_destroy(sys,NULL));
    }

    /* Part B: Two concurrent primary renders: zero duplicate consumption */
    {
        unsigned char sysopt[0x40],rackopt[0xa8];
        system_option(sysopt); rack_option(rackopt,2,"Sampler");
        Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&sys));
        assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
        assert(!ngs2_voice_handle(rack,0,&v0));

        setup_waveform(v0,4); set_voice_state(v0,0);
        float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
        set_voice_matrix(v0,mat,32);

        pthread_mutex_lock(&ngs2_lock);
        push_synthetic_grain(ngs2_voice_locked(v0),16384);
        pthread_mutex_unlock(&ngs2_lock);

        float rA_buf[256*8],rB_buf[256*8];
        Ngs2RenderBuffer rendersA[2]={{rA_buf,sizeof(rA_buf),0x18,8},{rB_buf,sizeof(rB_buf),0x18,8}};
        Ngs2RenderBuffer rendersB[2]={{rB_buf,sizeof(rB_buf),0x18,8},{rA_buf,sizeof(rA_buf),0x18,8}};

        TestConcurrentContext ctx;
        memset(&ctx,0,sizeof(ctx));
        pthread_mutex_init(&ctx.mtx,NULL);
        pthread_cond_init(&ctx.cond_start,NULL);
        pthread_cond_init(&ctx.cond_done,NULL);
        ctx.worker_action=5; /* Concurrent render */
        ctx.delay_before_action_ms=25; /* Exceeds removed 15 ms timestamp window. */
        ctx.target_handle=sys;
        ctx.concurrent_renders=rendersB;
        ctx.concurrent_render_count=2;

        ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

        pthread_t th;
        assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));

        assert(!ngs2_render(sys,rendersA,2));

        pthread_mutex_lock(&ctx.mtx);
        ctx.test_done=1;
        pthread_cond_signal(&ctx.cond_start);
        pthread_cond_signal(&ctx.cond_done);
        pthread_mutex_unlock(&ctx.mtx);

        assert(!pthread_join(th,NULL));
        ngs2_set_render_mix_hook(NULL,NULL);

        /* Grains must be consumed exactly ONCE across the two concurrent calls */
        pthread_mutex_lock(&ngs2_lock);
        Ngs2System *s_obj=ngs2_system_locked(sys);
        assert(s_obj->grains_consumed==1);
        assert(ngs2_voice_locked(v0)->atrac9->pcm_count==0);
        pthread_mutex_unlock(&ngs2_lock);

        assert(!ngs2_system_destroy(sys,NULL));
        pthread_mutex_destroy(&ctx.mtx);
        pthread_cond_destroy(&ctx.cond_start);
        pthread_cond_destroy(&ctx.cond_done);
    }

    /* Part C: 33, 64, 128 voices must all contribute and advance once. */
    uint32_t counts[3]={33,64,128};
    for (int c=0;c<3;++c) {
        uint32_t num_voices=counts[c];
        unsigned char sysopt[0x40],rackopt[0xa8];
        system_option(sysopt); rack_option(rackopt,num_voices,"LargeRack");
        Ngs2Handle *sys=NULL,*rack=NULL;
        assert(!ngs2_system_create(sysopt,NULL,&sys));
        assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));

        float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
        for (uint32_t i=0;i<num_voices;++i) {
            Ngs2Handle *vh=NULL;
            assert(!ngs2_voice_handle(rack,i,&vh));
            setup_waveform(vh,4);
            set_voice_state(vh,0);
            set_voice_matrix(vh,mat,32);

            pthread_mutex_lock(&ngs2_lock);
            push_synthetic_grain(ngs2_voice_locked(vh),2048);
            pthread_mutex_unlock(&ngs2_lock);
        }

        float buf0[256*8],buf1[256*8];
        Ngs2RenderBuffer renders[2]={
            {buf0,sizeof(buf0),0x18,8},
            {buf1,sizeof(buf1),0x18,8}
        };

        int non_silent_committed=0;
        for (int attempt=0;attempt<20;++attempt) {
            assert(!ngs2_render(sys,renders,2));
            pthread_mutex_lock(&ngs2_lock);
            Ngs2System *s_obj=ngs2_system_locked(sys);
            if (s_obj->grains_consumed==num_voices) {
                assert(s_obj->render_limit_exceeded>=1);
                assert(s_obj->virtualized_grains==0);
                Ngs2Rack *rack_obj=ngs2_rack_locked(rack);
                assert(rack_obj!=NULL);
                for (uint32_t i=0;i<num_voices;++i) {
                    assert(rack_obj->voices[i].atrac9!=NULL);
                    assert(rack_obj->voices[i].atrac9->pcm_count==0);
                }
                non_silent_committed=1;
            }
            pthread_mutex_unlock(&ngs2_lock);
            if (non_silent_committed) {
                assert(buf0[0]>0.0f);
                break;
            }
#ifdef _WIN32
            Sleep(2);
#else
            usleep(2000);
#endif
        }
        assert(non_silent_committed);
        assert(!ngs2_system_destroy(sys,NULL));
    }
    puts("  -> PASS (Concurrent renders & full 33/64/128 source mixing verified)");
}

/* 19. Continuidade PCM ATRAC9 preservada */
static void test_19_atrac9_pcm_continuity(void) {
    printf("Running Test 19: Consecutive grain continuity across render calls...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(v0);
    for (int i=1;i<=4;++i) {
        push_synthetic_grain(voice,(int16_t)(i*4096));
    }
    assert(voice->atrac9->pcm_count==4);
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={
        {buf0,sizeof(buf0),0x18,8},
        {buf1,sizeof(buf1),0x18,8}
    };

    /* Render 4 consecutive quanta and check continuity */
    for (int i=1;i<=4;++i) {
        /* main_renders can advance before a publication rollback. As in test 22,
         * wait for FIFO consumption instead of counting an attempted render. */
        int consumed=0;
        for (int retry=0;retry<100 && !consumed;++retry) {
            pthread_mutex_lock(&ngs2_lock);
            uint32_t before=ngs2_voice_locked(v0)->atrac9->pcm_count;
            pthread_mutex_unlock(&ngs2_lock);
            assert(!ngs2_render(sys,renders,2));
            pthread_mutex_lock(&ngs2_lock);
            uint32_t after=ngs2_voice_locked(v0)->atrac9->pcm_count;
            pthread_mutex_unlock(&ngs2_lock);
            if (after!=before) {
                assert(after+1==before);
                consumed=1;
            } else {
                for (size_t sample=0;sample<256*8;++sample)
                    assert(buf0[sample]==0.0f && buf1[sample]==0.0f);
                Sleep(1);
            }
        }
        assert(consumed);
        float expected=((float)(i*4096)/32768.0f)*0.25f;
        assert(fabsf(buf0[0]-expected)<1e-5f);
    }

    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==0);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 20. Ausência de regressão no HLE-1 */
static void test_20_hle1_non_regression(void) {
    printf("Running Test 20: HLE-1 contracts non-regression...\n");
    ngs2_atrac9_setting=0;
    ngs2_render_mix_setting=0;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,4,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,1,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));

    unsigned char state_buf[0x10]; memset(state_buf,0xa5,sizeof(state_buf));
    assert(!ngs2_voice_state(v0,state_buf,0x10));
    for (size_t i=0;i<0x10;++i) assert(state_buf[i]==0);

    float pcm[256*8]; memset(pcm,0xa5,sizeof(pcm));
    Ngs2RenderBuffer r={pcm,sizeof(pcm),0,1};
    assert(!ngs2_render(sys,&r,1));
    for (size_t i=0;i<sizeof(pcm);++i) assert(((unsigned char *)pcm)[i]==0);

    Ngs2RenderBuffer null_r={NULL,sizeof(pcm),0,1};
    assert(!ngs2_render(sys,&null_r,1));

    assert(!ngs2_rack_destroy(rack,NULL));
    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 21. Publication contention preserves FIFO and releases render exclusion. */
static void test_21_publication_contention_cleanup(void) {
    printf("Running Test 21: Publication contention transaction and gate cleanup...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,0x1000,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));
    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);

    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    pthread_mutex_unlock(&ngs2_lock);

    TestConcurrentContext ctx;
    memset(&ctx,0,sizeof(ctx));
    pthread_mutex_init(&ctx.mtx,NULL);
    pthread_cond_init(&ctx.cond_start,NULL);
    pthread_cond_init(&ctx.cond_done,NULL);
    ctx.worker_action=6;
    ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

    pthread_t th;
    assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));
    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={{buf0,sizeof(buf0),0x18,8},{buf1,sizeof(buf1),0x18,8}};
    assert(!ngs2_render(sys,renders,2));
    assert(!pthread_join(th,NULL));
    ngs2_set_render_mix_hook(NULL,NULL);

    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==1);
    pthread_mutex_unlock(&ngs2_lock);
    assert(atomic_load_explicit(&ngs2_render_owner,memory_order_acquire)==0);
    assert(buf0[0]==0.0f);

    assert(!test_render_retry(sys,renders,2));
    assert(fabsf(buf0[0]-0.125f)<1e-5f);
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==0);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    pthread_mutex_destroy(&ctx.mtx);
    pthread_cond_destroy(&ctx.cond_start);
    pthread_cond_destroy(&ctx.cond_done);
    puts("  -> PASS");
}

/* 22. Stale FIFO heads remain untouched until an explicitly valid transaction. */
static void test_22_stale_grain_transaction(void) {
    printf("Running Test 22: Stale grain transactional preservation...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,0x1000,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));
    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);

    uint32_t saved_read;
    pthread_mutex_lock(&ngs2_lock);
    Ngs2Voice *voice=ngs2_voice_locked(v0);
    push_synthetic_grain(voice,16384);
    saved_read=voice->atrac9->pcm_read;
    voice->atrac9->fifo[saved_read].generation=voice->atrac9->generation+1;
    pthread_mutex_unlock(&ngs2_lock);

    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={{buf0,sizeof(buf0),0x18,8},{buf1,sizeof(buf1),0x18,8}};
    assert(!ngs2_render(sys,renders,2));
    assert(buf0[0]==0.0f);
    assert(atomic_load_explicit(&ngs2_render_owner,memory_order_acquire)==0);

    pthread_mutex_lock(&ngs2_lock);
    voice=ngs2_voice_locked(v0);
    assert(voice->atrac9->pcm_count==1);
    assert(voice->atrac9->pcm_read==saved_read);
    voice->atrac9->fifo[saved_read].generation=voice->atrac9->generation;
    pthread_mutex_unlock(&ngs2_lock);

    /* main_renders advances before the publication trylock. Wait for actual
     * FIFO consumption, so a permitted rollback cannot masquerade as a commit. */
    int consumed=0;
    for(unsigned retry=0;retry<100 && !consumed;++retry) {
        assert(!ngs2_render(sys,renders,2));
        pthread_mutex_lock(&ngs2_lock);
        consumed=ngs2_voice_locked(v0)->atrac9->pcm_count==0;
        pthread_mutex_unlock(&ngs2_lock);
        if(!consumed)Sleep(1);
    }
    assert(consumed);
    assert(fabsf(buf0[0]-0.125f)<1e-5f);
    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 23. Output destination is snapshotted before unlocked mixing. */
static void test_23_output_descriptor_snapshot(void) {
    printf("Running Test 23: Output descriptor snapshot under mutation...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,0x1000,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));
    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    pthread_mutex_unlock(&ngs2_lock);

    float original[256*8],secondary[256*8],replacement[256*8];
    for (size_t i=0;i<256*8;++i) replacement[i]=42.0f;
    Ngs2RenderBuffer renders[2]={{original,sizeof(original),0x18,8},{secondary,sizeof(secondary),0x18,8}};

    TestConcurrentContext ctx;
    memset(&ctx,0,sizeof(ctx));
    pthread_mutex_init(&ctx.mtx,NULL);
    pthread_cond_init(&ctx.cond_start,NULL);
    pthread_cond_init(&ctx.cond_done,NULL);
    ctx.worker_action=7;
    ctx.target_handle=renders;
    ctx.replacement_buffer=replacement;
    ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

    pthread_t th;
    assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));
    assert(!ngs2_render(sys,renders,2));
    assert(!pthread_join(th,NULL));
    ngs2_set_render_mix_hook(NULL,NULL);

    assert(fabsf(original[0]-0.125f)<1e-5f);
    for (size_t i=0;i<256*8;++i) assert(replacement[i]==42.0f);
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==0);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    pthread_mutex_destroy(&ctx.mtx);
    pthread_cond_destroy(&ctx.cond_start);
    pthread_cond_destroy(&ctx.cond_done);
    puts("  -> PASS");
}

/* 24. Output-only racks do not create sampler underrun telemetry. */
static void test_24_output_racks_no_underruns(void) {
    printf("Running Test 24: Master and PreMaster underrun exclusion...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],sampler_opt[0xa8],premaster_opt[0xa8],master_opt[0xa8];
    system_option(sysopt);
    rack_option(sampler_opt,1,"Sampler");
    rack_option(premaster_opt,1,"PreMaster");
    rack_option(master_opt,1,"Master");
    Ngs2Handle *sys=NULL,*sampler=NULL,*premaster=NULL,*master=NULL;
    Ngs2Handle *sampler_voice=NULL,*premaster_voice=NULL,*master_voice=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,0x1000,sampler_opt,NULL,&sampler));
    assert(!ngs2_rack_create(sys,0x2000,premaster_opt,NULL,&premaster));
    assert(!ngs2_rack_create(sys,0x3000,master_opt,NULL,&master));
    assert(!ngs2_voice_handle(sampler,0,&sampler_voice));
    assert(!ngs2_voice_handle(premaster,0,&premaster_voice));
    assert(!ngs2_voice_handle(master,0,&master_voice));
    setup_waveform(sampler_voice,4); set_voice_state(sampler_voice,0);
    setup_waveform(premaster_voice,4); set_voice_state(premaster_voice,0);
    setup_waveform(master_voice,4); set_voice_state(master_voice,0);

    float buf0[256*8],buf1[256*8];
    Ngs2RenderBuffer renders[2]={{buf0,sizeof(buf0),0x18,8},{buf1,sizeof(buf1),0x18,8}};
    assert(!ngs2_render(sys,renders,2));

    pthread_mutex_lock(&ngs2_lock);
    Ngs2System *system=ngs2_system_locked(sys);
    assert(system->underruns==1);
    assert(ngs2_voice_locked(sampler_voice)->atrac9->underruns==1);
    assert(ngs2_voice_locked(premaster_voice)->atrac9->underruns==0);
    assert(ngs2_voice_locked(master_voice)->atrac9->underruns==0);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    puts("  -> PASS");
}

/* 25. A render that loses the gate cannot clear a buffer owned by another render. */
static void test_25_shared_buffer_gate_exclusion(void) {
    printf("Running Test 25: Shared-buffer exclusion on render-gate failure...\n");
    ngs2_atrac9_setting=1;
    ngs2_render_mix_setting=1;

    unsigned char sysopt[0x40],rackopt[0xa8];
    system_option(sysopt); rack_option(rackopt,2,"Sampler");
    Ngs2Handle *sys=NULL,*rack=NULL,*v0=NULL;
    assert(!ngs2_system_create(sysopt,NULL,&sys));
    assert(!ngs2_rack_create(sys,0x1000,rackopt,NULL,&rack));
    assert(!ngs2_voice_handle(rack,0,&v0));
    setup_waveform(v0,4); set_voice_state(v0,0);
    float mat[32]; memset(mat,0,sizeof(mat)); mat[0]=1.0f;
    set_voice_matrix(v0,mat,32);
    pthread_mutex_lock(&ngs2_lock);
    push_synthetic_grain(ngs2_voice_locked(v0),16384);
    pthread_mutex_unlock(&ngs2_lock);

    float shared[256*8],secondary[256*8];
    Ngs2RenderBuffer first[2]={{shared,sizeof(shared),0x18,8},{secondary,sizeof(secondary),0x18,8}};
    Ngs2RenderBuffer second[2]={{shared,sizeof(shared),0x18,8},{secondary,sizeof(secondary),0x18,8}};

    TestConcurrentContext ctx;
    memset(&ctx,0,sizeof(ctx));
    pthread_mutex_init(&ctx.mtx,NULL);
    pthread_cond_init(&ctx.cond_start,NULL);
    pthread_cond_init(&ctx.cond_done,NULL);
    ctx.worker_action=5;
    ctx.target_handle=sys;
    ctx.concurrent_renders=second;
    ctx.concurrent_render_count=2;
    ctx.shared_probe=&shared[0];
    ctx.shared_marker=0.75f;
    ngs2_set_render_mix_hook(test_concurrent_mix_hook,&ctx);

    pthread_t th;
    assert(!pthread_create(&th,NULL,test_concurrent_worker_thread,&ctx));
    assert(!ngs2_render(sys,first,2));
    assert(!pthread_join(th,NULL));
    ngs2_set_render_mix_hook(NULL,NULL);

    assert(ctx.concurrent_render_rc==0);
    assert(ctx.shared_preserved);
    assert(fabsf(shared[0]-0.125f)<1e-5f);
    assert(atomic_load_explicit(&ngs2_render_owner,memory_order_acquire)==0);
    pthread_mutex_lock(&ngs2_lock);
    assert(ngs2_system_locked(sys)->ambiguous_renders_ignored>=1);
    assert(ngs2_system_locked(sys)->grains_consumed==1);
    assert(ngs2_voice_locked(v0)->atrac9->pcm_count==0);
    pthread_mutex_unlock(&ngs2_lock);

    assert(!ngs2_system_destroy(sys,NULL));
    pthread_mutex_destroy(&ctx.mtx);
    pthread_cond_destroy(&ctx.cond_start);
    pthread_cond_destroy(&ctx.cond_done);
    puts("  -> PASS");
}

int main(void) {
#ifdef _WIN32
    /* Keep failed assertions visible in captured logs without a modal dialog. */
    _set_error_mode(_OUT_TO_STDERR);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    puts("=== Running Knack 2 NGS2 HLE-2.2B Mixer Test Suite ===");
    test_01_mixer_disabled_silence();
    test_02_atrac9_active_mixer_disabled();
    test_03_sparse_four_channel_matrix();
    test_04_eight_channel_matrix();
    test_05_two_simultaneous_voices();
    test_06_f32_accumulation_and_attenuation();
    test_07_nan_inf_and_clipping();
    test_08_invalid_descriptors();
    test_09_count_one_ambiguous_path();
    test_10_multiple_buffers_single_consumption();
    test_11_fifo_empty_and_full();
    test_12_pause_and_resume();
    test_13_stop_during_mixing();
    test_14_rack_destroy_quiescence();
    test_15_system_destroy_quiescence();
    test_16_generation_change_invalidation();
    test_17_trylock_contention();
    test_18_more_than_16_voices_fairness();
    test_19_atrac9_pcm_continuity();
    test_20_hle1_non_regression();
    test_21_publication_contention_cleanup();
    test_22_stale_grain_transaction();
    test_23_output_descriptor_snapshot();
    test_24_output_racks_no_underruns();
    test_25_shared_buffer_gate_exclusion();
    puts("=== ALL 25 MANDATORY TESTS PASSED CLEANLY! ===");
    return 0;
}
