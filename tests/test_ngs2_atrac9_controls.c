/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent Synthetic Test Suite for NGS2 ATRAC9 Trimming & Repeat Controls.
 *
 * Implements strict acceptance contracts per Tech Lead Sol directive (REVISAO_E_PACOTE_TESTES.md):
 *  1. finite_trimming: 1 superframe (1024 capacity), skip 256 priming, 500 valid samples.
 *     Verifies exact sample indices, no priming/cauda leak, zero-padding in last quantum, natural FINISHED.
 *  2. cross_boundary_trimming: non-256 boundary crossing frame & superframe (skip 300, count 800 across 2 superframes).
 *     Verifies trimming state spans across frames without resetting; tested on 4ch & 8ch.
 *  3. consecutive_finite_blocks: Block A (skip 256, count 300) + Block B (skip 256, count 400).
 *     Sub-quanta packed seamlessly without artificial zeros between A and B. Total 700 samples.
 *  4. finite_repeats: num_repeats = 1 (1 additional repeat = 2 passes of 500 samples = 1000 total).
 *     Decoder reinitializes on repeat pass; completion occurs naturally after last pass.
 *  5. infinite_loop: num_repeats = UINT32_MAX. Renders 3 full passes without turning into silence;
 *     never finishes spontaneously; halts on Cmd 6 Action 3.
 *  6. intro_then_loop: Block 1 finite intro (400 samples) followed by Block 2 infinite loop (500 samples).
 *     Verifies seamless transition across block boundaries and loop continuity.
 *  7. pause_resume_under_repeat: PAUSE during repeat freezes playback (ACTIVE=1, PLAYING=0, silent render,
 *     FIFO preserved). RESUME restores exact continuation. Cancellation during decode isolates Voice B.
 *  8. boundary_and_rejection_limits: 32-bit wrap overflow, range > capacity, truncated compressed data.
 *     Must reject or fault safely without producing false FINISHED.
 *
 * Two explicit execution modes:
 *  - Diagnostic mode (default): confirms exact RED predicates on missing contracts.
 *  - Positive contract mode (--assert-positive-contract): requires all assertions to succeed (exit 0).
 */

#include "native_test_platform.h"
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RUNTIME_TLG_SOURCE
#define RUNTIME_TLG_SOURCE "../port/src/runtime_tlg.c"
#endif

#include RUNTIME_TLG_SOURCE

/* Proven bit masks from game.elf instructions (0x00b6f008..0x00b6f045) */
#define SCE_NGS2_STATE_ACTIVE    0x00000001u
#define SCE_NGS2_STATE_PLAYING   0x00000002u
#define SCE_NGS2_STATE_FINISHED  0x00000020u

/* Mock ATRAC9 decoder state for deterministic sample tracking */
struct RuntimeAtrac9 {
    unsigned channels;
    unsigned frame;
    uintptr_t last_input_base;
    uint16_t tag;
};

static pthread_mutex_t mock_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mock_cond = PTHREAD_COND_INITIALIZER;
static int decode_block;
static int snapshot_block,snapshot_entered;
static unsigned mock_live,mock_peak;
static int decode_entered __attribute__((unused));
static unsigned atrac9_init_calls __attribute__((unused)) = 0;
static unsigned atrac9_decode_calls __attribute__((unused)) = 0;

RuntimeAtrac9 *runtime_atrac9_create(void) {
    RuntimeAtrac9 *d=calloc(1,sizeof(*d));
    pthread_mutex_lock(&mock_lock); if (d) ++mock_live;
    if (mock_live>mock_peak) mock_peak=mock_live;
    pthread_mutex_unlock(&mock_lock); return d;
}

void runtime_atrac9_release(RuntimeAtrac9 *decoder) {
    pthread_mutex_lock(&mock_lock); assert(mock_live); --mock_live;
    pthread_mutex_unlock(&mock_lock); free(decoder);
}

int runtime_atrac9_init(RuntimeAtrac9 *decoder, const unsigned char config[4], Atrac9CodecInfo *info) {
    ++atrac9_init_calls;
    assert(decoder);
    memset(info, 0, sizeof(*info));
    if (!memcmp(config, "\xfe\x7a\x17\xf0", 4)) {
        decoder->channels = 4;
        info->channels = 4;
        info->superframeSize = 768;
    } else {
        assert(!memcmp(config, "\xfe\x78\x29\xf0", 4));
        decoder->channels = 8;
        info->channels = 8;
        info->superframeSize = 1344;
    }
    info->samplingRate = 48000;
    info->framesInSuperframe = 4;
    info->frameSamples = 256;
    decoder->frame = 0;
    decoder->last_input_base = 0;
    return 0;
}

int runtime_atrac9_decode(RuntimeAtrac9 *decoder, const unsigned char *input, int16_t *pcm, int *used) {
    ++atrac9_decode_calls;
    pthread_mutex_lock(&mock_lock);
    decode_entered = 1;
    pthread_cond_broadcast(&mock_cond);
    while (decode_block) pthread_cond_wait(&mock_cond, &mock_lock);
    pthread_mutex_unlock(&mock_lock);

    if (decoder->channels == 8) {
        static const int padded_used[4] = {300, 300, 289, 280};
        *used = padded_used[decoder->frame % 4u];
    } else {
        *used = (decoder->frame % 4u == 0) ? 195 : 191;
    }

    /* Tag belongs to decoder instance, not every compressed subframe.
     * Restarting an actual pass must reinitialize the decoder. */
    if (!decoder->frame) {
        decoder->tag=(uint16_t)(input[0]|(input[1]<<8));
        if (!decoder->tag) decoder->tag=1000;
    }
    uint16_t tag=decoder->tag;

    /* Deterministic sample generation:
     * val = tag + (sample_index_in_segment * 4) + channel
     * All values are strictly positive non-zero and easily distinguishable.
     */
    for (unsigned i = 0; i < 256u; ++i) {
        uint32_t sample_idx = decoder->frame * 256u + i;
        for (unsigned ch = 0; ch < decoder->channels; ++ch) {
            int16_t val = (int16_t)(tag + (sample_idx * 4u) + ch);
            pcm[i * decoder->channels + ch] = val;
        }
    }
    ++decoder->frame;
    return 0;
}

int runtime_memory_copy_snapshot(void *dst, uintptr_t address, uint64_t size) {
    pthread_mutex_lock(&mock_lock); snapshot_entered=1; pthread_cond_broadcast(&mock_cond);
    while (snapshot_block) pthread_cond_wait(&mock_cond,&mock_lock);
    pthread_mutex_unlock(&mock_lock);
    if (!dst || !address || !size || size > SIZE_MAX) return 0;
    memcpy(dst, (const void *)address, (size_t)size);
    return 1;
}

void runtime_restart(void) { assert(!"Unexpected restart in NGS2 test"); }
int64_t runtime_file_getdents(int fd, char *buffer, uint64_t size, int64_t *base) {
    (void)fd; (void)buffer; (void)size; (void)base; return -1;
}
void runtime_wait_begin(const char *kind, const void *object, const char *name,
                        const void *const *owner, uintptr_t guest_frame) {
    (void)kind; (void)object; (void)name; (void)owner; (void)guest_frame;
}
void runtime_wait_end(void) {}
const char *runtime_symbol(const char *name) { return name; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(table[i].name, name)) return (uintptr_t)table[i].function;
    }
    return 0;
}

/* Command and option builder helpers */
static void put_u16(void *data, size_t offset, uint16_t value) {
    memcpy((char *)data + offset, &value, sizeof(value));
}
static void put_u32(void *data, size_t offset, uint32_t value) {
    memcpy((char *)data + offset, &value, sizeof(value));
}
static void put_uptr(void *data, size_t offset, uintptr_t value) {
    memcpy((char *)data + offset, &value, sizeof(value));
}
static void command(void *data, uint16_t size, uint32_t id) {
    memset(data, 0, size);
    put_u16(data, 0, size);
    put_u32(data, 4, id);
}
static void system_option(unsigned char sys[0x40]) {
    memset(sys, 0, 0x40);
    put_u32(sys, 0, 0x40);
    put_u32(sys, 0x20, 256);
    put_u32(sys, 0x24, 48000);
}
static void rack_option(unsigned char rack[0xa8], uint32_t capacity, const char *name) {
    memset(rack, 0, 0xa8);
    put_u32(rack, 0, 0xa8);
    put_u32(rack, 0x20, capacity);
    snprintf((char *)rack + 8, 16, "%s", name);
}
static void setup_waveform(Ngs2Handle *voice, uint32_t channels, uint32_t codec_config) {
    unsigned char cmd[0x28];
    command(cmd, sizeof(cmd), UINT32_C(0x10000000));
    put_u32(cmd, 8, NGS2_WAVEFORM_ATRAC9);
    put_u32(cmd, 0x0c, channels);
    put_u32(cmd, 0x10, 48000);
    put_u32(cmd, 0x14, codec_config);
    assert(!ngs2_voice_control(voice, cmd));
}
static void set_voice_state(Ngs2Handle *voice, uint32_t event) {
    unsigned char cmd[0x0c];
    command(cmd, sizeof(cmd), 6);
    put_u32(cmd, 8, event);
    assert(!ngs2_voice_control(voice, cmd));
}
static void set_voice_matrix_identity(Ngs2Handle *voice, uint32_t channels) {
    /* Destination-major routing matrix with gain 4.0 (0.25 master attenuation gives 1.0) */
    float mat[64];
    memset(mat, 0, sizeof(mat));
    for (uint32_t ch = 0; ch < channels && ch < 8; ++ch) {
        mat[ch * channels + ch] = 4.0f;
    }
    unsigned char cmd[0x18];
    command(cmd, sizeof(cmd), 1);
    put_u32(cmd, 0x0c, channels * channels);
    put_uptr(cmd, 0x10, (uintptr_t)mat);
    assert(!ngs2_voice_control(voice, cmd));
}

static void enqueue_segment_with_controls(Ngs2Handle *voice, const void *bytes, uint32_t size,
                                          uint32_t num_repeats, uint32_t num_skip_samples, uint32_t num_samples) {
    unsigned char descriptor[0x20], cmd[0x20];
    memset(descriptor, 0, sizeof(descriptor));
    put_u32(descriptor, 0x00, 0);
    put_u32(descriptor, 0x04, size);
    put_u32(descriptor, 0x08, num_repeats);
    put_u32(descriptor, 0x0C, num_skip_samples);
    put_u32(descriptor, 0x10, num_samples);
    put_u32(descriptor, 0x14, 0);
    put_uptr(descriptor, 0x18, 0);

    command(cmd, sizeof(cmd), UINT32_C(0x10000001));
    put_uptr(cmd, 8, (uintptr_t)bytes);
    put_u32(cmd, 16, 1);
    put_u32(cmd, 20, 1);
    put_uptr(cmd, 24, (uintptr_t)descriptor);
    assert(!ngs2_voice_control(voice, cmd));
}

static void enqueue_null_command(Ngs2Handle *voice) {
    unsigned char cmd[0x20];
    command(cmd, sizeof(cmd), UINT32_C(0x10000001));
    assert(!ngs2_voice_control(voice, cmd));
}

/* Retry wrapper for non-blocking trylock mixer */
static int test_render_retry(Ngs2Handle *sys, const Ngs2RenderBuffer *renders, uint32_t count) {
    /* Wait for producer progress only when a playing voice has pending work
     * but no published grain. A missing loop implementation still returns
     * silence after its work is exhausted, and is detected by the PCM oracle. */
    pthread_mutex_lock(&ngs2_lock);
    for (;;) {
        Ngs2System *system=ngs2_system_locked(sys); int waiting=0;
        assert(system);
        for (Ngs2Rack *r=system->racks;r;r=r->next) for (uint32_t i=0;i<r->capacity;++i) {
            Ngs2Voice *v=&r->voices[i]; Ngs2Atrac9Voice *st=v->atrac9;
            if (v->internal_state==NGS2_VOICE_PLAYING && st && !st->faulted &&
                !st->closing && !st->pcm_count && (st->active_valid || st->segment_count ||
                st->decode_inflight || st->segment_reservations || st->snapshot_inflight)) waiting=1;
        }
        if (!waiting) break;
        pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    }
    pthread_mutex_unlock(&ngs2_lock);
    for (int retry = 0; retry < 100; ++retry) {
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
        if (r1 > r0) return 0;
        Sleep(1);
    }
    return -1;
}

/* Assertion macros for conformance vs contract gates */
#define CHECK_CONFORMING(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CONFORMANCE_FAILED case=%s line=%d: %s\n", case_name, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

#define CHECK_GATE(expr, predicate_str) do { \
    if (assert_positive_contract) { \
        if (!(expr)) { \
            fprintf(stderr, "CONTROLS_CONTRACT_FAILED case=%s line=%d: %s\n", case_name, __LINE__, predicate_str); \
            return 1; \
        } \
    } else { \
        if (!(expr)) { \
            printf("CONTROLS_RED_CONFIRMED case=%s line=%d predicate=%s\n", case_name, __LINE__, predicate_str); \
            return 0; /* Expected RED confirmed; post-RED checks halted */ \
        } \
    } \
} while (0)

typedef struct {
    Ngs2Handle *sys;
    Ngs2Handle *sampler_rack;
    Ngs2Handle *voice_a;
    Ngs2Handle *voice_b;
    float master[256 * 8], aux[256 * 8];
    Ngs2RenderBuffer outputs[2];
} ControlsFixture;

static void open_fixture(ControlsFixture *f, uint32_t channels_a, uint32_t channels_b) {
    memset(f, 0, sizeof(*f));
    unsigned char so[0x40], ro[0xa8];
    system_option(so);
    assert(!ngs2_system_create(so, NULL, &f->sys));

    rack_option(ro, 4, "Sampler");
    assert(!ngs2_rack_create(f->sys, 0x1000, ro, NULL, &f->sampler_rack));
    assert(!ngs2_voice_handle(f->sampler_rack, 0, &f->voice_a));
    assert(!ngs2_voice_handle(f->sampler_rack, 1, &f->voice_b));

    uint32_t cfg_a = (channels_a == 8) ? UINT32_C(0xfe7829f0) : UINT32_C(0xfe7a17f0);
    uint32_t cfg_b = (channels_b == 8) ? UINT32_C(0xfe7829f0) : UINT32_C(0xfe7a17f0);
    setup_waveform(f->voice_a, channels_a, cfg_a);
    setup_waveform(f->voice_b, channels_b, cfg_b);

    set_voice_matrix_identity(f->voice_a, channels_a);
    set_voice_matrix_identity(f->voice_b, channels_b);

    f->outputs[0] = (Ngs2RenderBuffer){f->master, sizeof(f->master), 0x18, 8};
    f->outputs[1] = (Ngs2RenderBuffer){f->aux, sizeof(f->aux), 0x18, 8};
}

static void close_fixture(ControlsFixture *f) {
    if (f->sys) assert(!ngs2_system_destroy(f->sys, NULL));
}


/* Stateless oracle: direct indexing into declared spans. It does not reproduce
 * worker scheduling, decoding, queues, completion or cancellation logic. */
typedef struct { uint16_t tag; uint32_t skip,count,repeats; } ExpectedSpan;
static int pcm_matches(const float *pcm,uint32_t channels,const ExpectedSpan *spans,
                       unsigned count,uint64_t first,int check_unused) {
    for (unsigned i=0;i<256;++i) {
        uint64_t p=first+i; const ExpectedSpan *span=NULL;
        for (unsigned n=0;n<count;++n) {
            uint64_t length=spans[n].repeats==UINT32_MAX ? UINT64_MAX :
                            (uint64_t)spans[n].count*((uint64_t)spans[n].repeats+1u);
            if (p<length) { span=&spans[n]; break; }
            p-=length;
        }
        for (uint32_t ch=0;ch<(check_unused ? 8u : channels);++ch) {
            int16_t expected=span && ch<channels ?
                (int16_t)(span->tag+4u*(span->skip+(uint32_t)(p%span->count))+ch) : 0;
            int16_t actual=(int16_t)roundf(pcm[i*8+ch]*32768.0f);
            if (actual!=expected) {
                fprintf(stderr,"PCM_MISMATCH sample=%llu ch=%u expected=%d actual=%d\n",
                        (unsigned long long)(first+i),ch,expected,actual); return 0;
            }
        }
    }
    return 1;
}
static uint32_t control_flags(Ngs2Handle *handle) {
    uint8_t q[0x30]; uint32_t flags; assert(!ngs2_voice_state(handle,q,sizeof(q)));
    memcpy(&flags,q,sizeof(flags)); return flags;
}
static Ngs2Voice *control_voice(Ngs2Handle *handle) {
    pthread_mutex_lock(&ngs2_lock); Ngs2Voice *v=ngs2_voice_locked(handle);
    assert(v); pthread_mutex_unlock(&ngs2_lock); return v;
}
static void tag_bytes(unsigned char *bytes,size_t size,uint16_t tag) {
    memset(bytes,0,size); memcpy(bytes,&tag,sizeof(tag));
}
static int test_case_1_finite_trimming(int assert_positive_contract) {
    const char *case_name="finite_trimming"; ControlsFixture f; open_fixture(&f,4,4);
    unsigned char data[768]; tag_bytes(data,sizeof(data),1000);
    ExpectedSpan expected={1000,256,500,0};
    set_voice_state(f.voice_a,0); enqueue_segment_with_controls(f.voice_a,data,sizeof(data),0,256,500);
    enqueue_null_command(f.voice_a);
    for (unsigned q=0;q<2;++q) {
        assert(!test_render_retry(f.sys,f.outputs,2));
        CHECK_GATE(pcm_matches(f.master,4,&expected,1,q*256u,1),"trim_pcm_exact");
    }
    CHECK_GATE(control_flags(f.voice_a)==0x20,"finite_finished_after_publication");
    close_fixture(&f); printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
static int test_case_2_cross_boundary_trimming(int assert_positive_contract) {
    const char *case_name="cross_boundary_trimming";
    for (unsigned channels=4;channels<=8;channels+=4) {
        ControlsFixture f; open_fixture(&f,channels,4);
        unsigned char data[2688]; uint32_t bytes=channels==4 ? 1536u : 2688u;
        tag_bytes(data,bytes,2000); ExpectedSpan expected={2000,300,800,0};
        set_voice_state(f.voice_a,0); enqueue_segment_with_controls(f.voice_a,data,bytes,0,300,800);
        enqueue_null_command(f.voice_a);
        for (unsigned q=0;q<4;++q) {
            assert(!test_render_retry(f.sys,f.outputs,2));
            CHECK_GATE(pcm_matches(f.master,channels,&expected,1,q*256u,1),"cross_boundary_pcm_exact");
        }
        CHECK_GATE(control_flags(f.voice_a)==0x20,"cross_boundary_finished"); close_fixture(&f);
    }
    printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
static int test_case_3_consecutive_finite_blocks(int assert_positive_contract) {
    const char *case_name="consecutive_finite_blocks"; ControlsFixture f; open_fixture(&f,4,4);
    unsigned char a[768],b[768]; tag_bytes(a,sizeof(a),1000); tag_bytes(b,sizeof(b),5000);
    ExpectedSpan expected[]={{1000,256,300,0},{5000,256,400,0}};
    set_voice_state(f.voice_a,0); enqueue_segment_with_controls(f.voice_a,a,sizeof(a),0,256,300);
    enqueue_segment_with_controls(f.voice_a,b,sizeof(b),0,256,400); enqueue_null_command(f.voice_a);
    for (unsigned q=0;q<3;++q) {
        assert(!test_render_retry(f.sys,f.outputs,2));
        CHECK_GATE(pcm_matches(f.master,4,expected,2,q*256u,1),"block_chain_pcm_exact");
    }
    CHECK_GATE(control_flags(f.voice_a)==0x20,"block_chain_finished");
    close_fixture(&f); printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
static int test_case_4_finite_repeats(int assert_positive_contract) {
    const char *case_name="finite_repeats";
    for (unsigned repeats=1;repeats<=2;++repeats) {
        ControlsFixture f; open_fixture(&f,4,4); unsigned char data[768]; tag_bytes(data,sizeof(data),1000);
        ExpectedSpan expected={1000,256,500,repeats};
        set_voice_state(f.voice_a,0); enqueue_segment_with_controls(f.voice_a,data,sizeof(data),repeats,256,500);
        enqueue_null_command(f.voice_a);
        unsigned quanta=(500u*(repeats+1u)+255u)/256u;
        for (unsigned q=0;q<quanta;++q) {
            assert(!test_render_retry(f.sys,f.outputs,2));
            CHECK_GATE(pcm_matches(f.master,4,&expected,1,q*256u,1),"repeat_pcm_exact_without_interpass_padding");
        }
        CHECK_GATE(control_flags(f.voice_a)==0x20,"finite_repeat_finished"); close_fixture(&f);
    }
    printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
static int test_case_5_infinite_loop(int assert_positive_contract) {
    const char *case_name="infinite_loop"; ControlsFixture f; open_fixture(&f,4,4);
    unsigned char data[768]; tag_bytes(data,sizeof(data),1000); ExpectedSpan expected={1000,256,500,UINT32_MAX};
    set_voice_state(f.voice_a,0); enqueue_segment_with_controls(f.voice_a,data,sizeof(data),UINT32_MAX,256,500);
    enqueue_null_command(f.voice_a);
    for (unsigned q=0;q<12;++q) {
        assert(!test_render_retry(f.sys,f.outputs,2));
        CHECK_GATE(pcm_matches(f.master,4,&expected,1,q*256u,1),"infinite_loop_pcm_exact");
    }
    CHECK_GATE(control_flags(f.voice_a)==3,"infinite_loop_remains_active");
    pthread_mutex_lock(&ngs2_lock); int held=ngs2_compressed_bytes==sizeof(data);
    pthread_mutex_unlock(&ngs2_lock); CHECK_GATE(held,"loop_retains_one_snapshot");
    pthread_mutex_lock(&mock_lock); int bounded=mock_peak<=3; pthread_mutex_unlock(&mock_lock);
    CHECK_GATE(bounded,"decoder_live_count_bounded");
    set_voice_state(f.voice_a,3); CHECK_GATE(control_flags(f.voice_a)==0,"loop_cancelled");
    close_fixture(&f); printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
static int test_case_6_intro_then_loop(int assert_positive_contract) {
    const char *case_name="intro_then_loop"; ControlsFixture f; open_fixture(&f,4,4);
    unsigned char a[768],b[768]; tag_bytes(a,sizeof(a),1000); tag_bytes(b,sizeof(b),6000);
    ExpectedSpan expected[]={{1000,256,400,0},{6000,256,500,UINT32_MAX}};
    set_voice_state(f.voice_a,0); enqueue_segment_with_controls(f.voice_a,a,sizeof(a),0,256,400);
    enqueue_segment_with_controls(f.voice_a,b,sizeof(b),UINT32_MAX,256,500); enqueue_null_command(f.voice_a);
    for (unsigned q=0;q<8;++q) {
        assert(!test_render_retry(f.sys,f.outputs,2));
        CHECK_GATE(pcm_matches(f.master,4,expected,2,q*256u,1),"intro_loop_pcm_exact");
    }
    CHECK_GATE(control_flags(f.voice_a)==3,"intro_loop_remains_active");
    set_voice_state(f.voice_a,3); close_fixture(&f);
    printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
static int test_case_7_pause_resume_under_repeat(int assert_positive_contract) {
    const char *case_name="pause_resume_under_repeat"; ControlsFixture f; open_fixture(&f,4,4);
    unsigned char a[768],b[768]; tag_bytes(a,sizeof(a),1000); tag_bytes(b,sizeof(b),8000);
    /* Give B a disjoint synthetic master lane to measure A's exact continuation. */
    float matrix[32]={0}; matrix[16]=4.0f; unsigned char cmd[0x18]; command(cmd,sizeof(cmd),1);
    put_u32(cmd,12,32); put_uptr(cmd,16,(uintptr_t)matrix); assert(!ngs2_voice_control(f.voice_b,cmd));
    ExpectedSpan expected={1000,256,500,UINT32_MAX};
    set_voice_state(f.voice_a,0); set_voice_state(f.voice_b,0);
    enqueue_segment_with_controls(f.voice_a,a,sizeof(a),UINT32_MAX,256,500);
    enqueue_segment_with_controls(f.voice_b,b,sizeof(b),UINT32_MAX,256,500);
    Ngs2Voice *va=control_voice(f.voice_a),*vb=control_voice(f.voice_b);
    assert(!test_render_retry(f.sys,f.outputs,2)); assert(!test_render_retry(f.sys,f.outputs,2));
    set_voice_state(f.voice_a,4);
    pthread_mutex_lock(&ngs2_lock);
    while (va->atrac9->decode_inflight) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    uint64_t gen=va->atrac9->generation; uint32_t pcm=va->atrac9->pcm_count,rd=va->atrac9->pcm_read;
    pthread_mutex_unlock(&ngs2_lock);
    CHECK_GATE(control_flags(f.voice_a)==1 && control_flags(f.voice_b)==3,"pause_flags_coherent");
    assert(!test_render_retry(f.sys,f.outputs,2));
    for (unsigned i=0;i<256;++i) for (unsigned ch=0;ch<4;++ch) CHECK_GATE(f.master[i*8+ch]==0,"paused_pcm_silent");
    pthread_mutex_lock(&ngs2_lock);
    int unchanged=va->atrac9->generation==gen && va->atrac9->pcm_count==pcm && va->atrac9->pcm_read==rd;
    pthread_mutex_unlock(&ngs2_lock); CHECK_GATE(unchanged,"paused_fifo_preserved");
    set_voice_state(f.voice_a,5); assert(!test_render_retry(f.sys,f.outputs,2));
    CHECK_GATE(pcm_matches(f.master,4,&expected,1,512,0),"repeat_paused_resumed_exact");
    /* Quiesce both voices, then force A's new generation into a real decode barrier. */
    set_voice_state(f.voice_a,4); set_voice_state(f.voice_b,4);
    pthread_mutex_lock(&ngs2_lock);
    while (va->atrac9->decode_inflight || vb->atrac9->decode_inflight) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    uint64_t gen_b=vb->atrac9->generation; uint32_t pcm_b=vb->atrac9->pcm_count;
    pthread_mutex_unlock(&ngs2_lock);
    set_voice_state(f.voice_a,3); enqueue_segment_with_controls(f.voice_a,a,sizeof(a),UINT32_MAX,256,500);
    pthread_mutex_lock(&mock_lock); decode_block=1; decode_entered=0; pthread_mutex_unlock(&mock_lock);
    set_voice_state(f.voice_a,0);
    pthread_mutex_lock(&mock_lock); while (!decode_entered) pthread_cond_wait(&mock_cond,&mock_lock); pthread_mutex_unlock(&mock_lock);
    pthread_mutex_lock(&ngs2_lock); gen=va->atrac9->generation; pthread_mutex_unlock(&ngs2_lock);
    set_voice_state(f.voice_b,5); set_voice_state(f.voice_a,3);
    pthread_mutex_lock(&mock_lock); decode_block=0; pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
    pthread_mutex_lock(&ngs2_lock);
    while (va->atrac9->decode_inflight) pthread_cond_wait(&ngs2_cond,&ngs2_lock);
    int cancelled=va->atrac9->generation>gen && !va->atrac9->pcm_count;
    int isolated=vb->atrac9->generation==gen_b && vb->atrac9->pcm_count>=pcm_b;
    pthread_mutex_unlock(&ngs2_lock);
    CHECK_GATE(cancelled && isolated,"decode_cancel_generation_isolated");
    assert(!test_render_retry(f.sys,f.outputs,2));
    CHECK_GATE(f.master[0]==0 && f.master[4]>0,"cancelled_a_silent_b_pcm_present");
    close_fixture(&f); printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
static int test_case_8_boundary_and_rejection_limits(int assert_positive_contract) {
    const char *case_name="boundary_and_rejection_limits"; ControlsFixture f; open_fixture(&f,4,4);
    unsigned char data[768]={0}; const uint32_t skips[]={0xffffff00u,256,256};
    const uint32_t counts[]={0x200,1000,500},bytes[]={768,768,500};
    for (unsigned n=0;n<3;++n) {
        set_voice_state(f.voice_a,2); set_voice_state(f.voice_a,0);
        Ngs2Voice *v=control_voice(f.voice_a);
        pthread_mutex_lock(&ngs2_lock); uint64_t errors=v->atrac9->decode_errors; pthread_mutex_unlock(&ngs2_lock);
        enqueue_segment_with_controls(f.voice_a,data,bytes[n],0,skips[n],counts[n]); enqueue_null_command(f.voice_a);
        pthread_mutex_lock(&ngs2_lock);
        while (!v->atrac9->faulted && (v->atrac9->active_valid || v->atrac9->segment_count || v->atrac9->decode_inflight))
            pthread_cond_wait(&ngs2_cond,&ngs2_lock);
        int rejected=v->atrac9->faulted && v->atrac9->decode_errors>errors && !v->atrac9->pcm_count;
        pthread_mutex_unlock(&ngs2_lock);
        CHECK_GATE(rejected,"bounds_validated_cleanly");
        CHECK_GATE((control_flags(f.voice_a)&0x20)==0,"invalid_input_not_finished");
    }
    close_fixture(&f); printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}
typedef struct { Ngs2Handle *voice; unsigned char *data; } CopyParam;
static void *controlled_copy(void *arg) {
    CopyParam *p=arg; enqueue_segment_with_controls(p->voice,p->data,768,0,256,500); return NULL;
}
static int test_case_9_cancelled_snapshot_reuse(int assert_positive_contract) {
    const char *case_name="cancelled_snapshot_reuse"; ControlsFixture f; open_fixture(&f,4,4);
    unsigned char data[768]; tag_bytes(data,sizeof(data),1000); ExpectedSpan expected={1000,256,500,0};
    set_voice_state(f.voice_a,0);
    pthread_mutex_lock(&mock_lock); snapshot_block=1; snapshot_entered=0; pthread_mutex_unlock(&mock_lock);
    CopyParam param={f.voice_a,data}; pthread_t thread; assert(!pthread_create(&thread,NULL,controlled_copy,&param));
    pthread_mutex_lock(&mock_lock); while (!snapshot_entered) pthread_cond_wait(&mock_cond,&mock_lock); pthread_mutex_unlock(&mock_lock);
    enqueue_null_command(f.voice_a); set_voice_state(f.voice_a,3); set_voice_state(f.voice_a,0);
    pthread_mutex_lock(&mock_lock); snapshot_block=0; pthread_cond_broadcast(&mock_cond); pthread_mutex_unlock(&mock_lock);
    assert(!pthread_join(thread,NULL));
    CHECK_GATE(control_flags(f.voice_a)==3,"new_generation_not_finished_by_old_copy");
    enqueue_segment_with_controls(f.voice_a,data,sizeof(data),0,256,500); enqueue_null_command(f.voice_a);
    for (unsigned q=0;q<2;++q) {
        assert(!test_render_retry(f.sys,f.outputs,2));
        CHECK_GATE(pcm_matches(f.master,4,&expected,1,q*256u,1),"snapshot_reuse_trim_pcm_exact");
    }
    CHECK_GATE(control_flags(f.voice_a)==0x20,"snapshot_reuse_finished");
    close_fixture(&f); printf("PASS_CONTROLS_CASE %s\n",case_name); return 0;
}

/* =========================================================================
 * Main entry point
 * ========================================================================= */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <case_name> [--assert-positive-contract]\n", argv[0]);
        return 2;
    }

    const char *case_name = argv[1];
    int assert_positive_contract = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--assert-positive-contract")) {
            assert_positive_contract = 1;
        }
    }

    /* Standard NGS2 HLE settings for mixer & ATRAC9 */
    ngs2_atrac9_setting = 1;
    ngs2_render_mix_setting = 1;
    ngs2_b2f2a_setting = 0;
    ngs2_b2f2c_setting = 0;

    if (!strcmp(case_name, "cancelled_snapshot_reuse")) return test_case_9_cancelled_snapshot_reuse(assert_positive_contract);
    if (!strcmp(case_name, "finite_trimming")) {
        return test_case_1_finite_trimming(assert_positive_contract);
    } else if (!strcmp(case_name, "cross_boundary_trimming")) {
        return test_case_2_cross_boundary_trimming(assert_positive_contract);
    } else if (!strcmp(case_name, "consecutive_finite_blocks")) {
        return test_case_3_consecutive_finite_blocks(assert_positive_contract);
    } else if (!strcmp(case_name, "finite_repeats")) {
        return test_case_4_finite_repeats(assert_positive_contract);
    } else if (!strcmp(case_name, "infinite_loop")) {
        return test_case_5_infinite_loop(assert_positive_contract);
    } else if (!strcmp(case_name, "intro_then_loop")) {
        return test_case_6_intro_then_loop(assert_positive_contract);
    } else if (!strcmp(case_name, "pause_resume_under_repeat")) {
        return test_case_7_pause_resume_under_repeat(assert_positive_contract);
    } else if (!strcmp(case_name, "boundary_and_rejection_limits")) {
        return test_case_8_boundary_and_rejection_limits(assert_positive_contract);
    } else {
        fprintf(stderr, "Unknown test case: %s\n", case_name);
        return 2;
    }
}
