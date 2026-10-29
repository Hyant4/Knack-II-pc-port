/* F16C VCVTPH2PS/VCVTPS2PH rewrite (iss_pc #51: isa_emulate_f16c in src/isa_emulate.c, the F16C stubs
 * of src/isa_rewrite.c) against the real instructions. On a host with F16C the CPU is the oracle:
 * every half value through VCVTPH2PS, floats through VCVTPS2PH for every rounding immediate and
 * MXCSR (RC, DAZ, FTZ), and whole rewritten sites (register, memory, RIP-relative and red-zone
 * operands, XMM and YMM) run next to the original code with all GPRs, flags, YMM registers, memory
 * and the red zone compared. Without F16C only the recorded values below are checked. */
#define _GNU_SOURCE   /* MAP_ANONYMOUS under -std=c11 */
#include <assert.h>
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "isa_rewrite.h"

enum { PAGE = 4096, SITE_AT = 0x100, RIP_DATA = 0x800 };
static int native;                      /* host executes F16C */
static unsigned char *image, *stubs, *pristine;
static uint64_t rng = 0x9e3779b97f4a7c15ull;
static unsigned long long compared;

static uint64_t next(void) {             /* xorshift64* */
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return rng * 0x2545f4914f6cdd1dull;
}

/* Floats with structure: every exponent, NaN/Inf/zero/denormal, rounding ties and their
 * neighbours in the bits a half drops, and the half range (2^-25 .. 2^16) more often. */
static uint32_t test_float(void) {
    const uint32_t sign = (uint32_t)(next() & 1) << 31;
    uint32_t exponent, mantissa = (uint32_t)next() & 0x7fffff;
    switch (next() % 8) {
    case 0: exponent = (uint32_t)(next() % 256); break;
    case 1: exponent = 0; break;                                  /* zero and denormals */
    case 2: exponent = 255; break;                                /* Inf and NaN */
    default: exponent = 101 + (uint32_t)(next() % 43); break;    /* 2^-26 .. 2^16 */
    }
    switch (next() % 6) {
    case 0: mantissa &= ~0x1fffu; mantissa |= 0x1000; break;      /* tie at 13 dropped bits */
    case 1: mantissa |= 0x1fff; break;
    case 2: mantissa = (uint32_t)next() % 4 == 0 ? 0 : mantissa >> (next() % 24); break;
    default: break;
    }
    return sign | exponent << 23 | mantissa;
}

static uint32_t mxcsr_with(unsigned rc, int daz, int ftz) {
    return 0x1f80u | rc << 13 | (uint32_t)daz << 6 | (uint32_t)ftz << 15;
}

/* ---- the real instructions ---- */

#ifdef __F16C__
static void native_ph2ps(const uint16_t in[4], uint32_t out[4]) {
    __m128i h;
    uint16_t v[8] = {in[0], in[1], in[2], in[3], 0, 0, 0, 0};
    memcpy(&h, v, 16);
    __m128 r = _mm_cvtph_ps(h);
    memcpy(out, &r, 16);
}

#define PS2PH_CASE(n) case n: r = _mm_cvtps_ph(f, n); break;
static void native_ps2ph(const uint32_t in[4], unsigned imm, uint16_t out[4]) {
    __m128 f;
    __m128i r;
    memcpy(&f, in, 16);
    switch (imm & 15) {
    PS2PH_CASE(0) PS2PH_CASE(1) PS2PH_CASE(2) PS2PH_CASE(3) PS2PH_CASE(4) PS2PH_CASE(5) PS2PH_CASE(6) PS2PH_CASE(7)
    PS2PH_CASE(8) PS2PH_CASE(9) PS2PH_CASE(10) PS2PH_CASE(11) PS2PH_CASE(12) PS2PH_CASE(13) PS2PH_CASE(14)
    default: r = _mm_cvtps_ph(f, 15); break;
    }
    memcpy(out, &r, 8);
}
#endif

/* The model through isa_emulate_f16c with a register site: 4 lanes. */
static IsaSite f16c_site(uint8_t kind, int wide, uint8_t dst, uint8_t src, uint8_t imm) {
    IsaSite s;
    memset(&s, 0, sizeof s);
    s.kind = kind;
    s.width = wide ? 32 : 16;
    s.dst = dst;
    s.src1 = src;
    s.src2 = s.base = s.index = ISA_NO_REG;
    s.imm = imm;
    return s;
}

static void model_ps2ph(const uint32_t in[4], unsigned imm, uint16_t out[4]) {
    uint8_t ymm[64];
    memset(ymm, 0xee, sizeof ymm);
    memcpy(ymm + 32, in, 16);
    IsaSite s = f16c_site(ISA_VCVTPS2PH, 0, 0, 1, (uint8_t)imm);
    isa_emulate_f16c(&s, NULL, ymm);
    memcpy(out, ymm, 8);
    for (int i = 8; i < 32; i++) assert(ymm[i] == 0);
}

/* ---- 1: recorded results (Zen 3), checked on every host ---- */

static void test_recorded_results(void) {
    static const struct { uint16_t half; uint32_t single; } h[] = {
        {0x0000, 0x00000000}, {0x8000, 0x80000000}, {0x0001, 0x33800000}, {0x03ff, 0x387fc000},
        {0x0400, 0x38800000}, {0x3c00, 0x3f800000}, {0x7bff, 0x477fe000}, {0x7c00, 0x7f800000},
        {0xfc00, 0xff800000}, {0x7c01, 0x7fc02000}, {0x7e00, 0x7fc00000}, {0xfdff, 0xffffe000},
    };
    for (size_t i = 0; i < sizeof h / sizeof *h; i++) assert(isa_half_to_float(h[i].half) == h[i].single);
    static const struct { uint32_t single; uint16_t rn, rd, ru, rz; } f[] = {
        {0x00000001, 0x0000, 0x0000, 0x0001, 0x0000},   /* float denormal */
        {0x80000001, 0x8000, 0x8001, 0x8000, 0x8000},
        {0x33000000, 0x0000, 0x0000, 0x0001, 0x0000},   /* 2^-25: tie, to even */
        {0x33000001, 0x0001, 0x0000, 0x0001, 0x0000},
        {0x33c00000, 0x0002, 0x0001, 0x0002, 0x0001},   /* 1.5 * 2^-24: tie, to even */
        {0x387fc000, 0x03ff, 0x03ff, 0x03ff, 0x03ff},
        {0x387fe000, 0x0400, 0x03ff, 0x0400, 0x03ff},   /* denormal rounds up into the normals */
        {0x3f800000, 0x3c00, 0x3c00, 0x3c00, 0x3c00},
        {0x3f801000, 0x3c00, 0x3c00, 0x3c01, 0x3c00},   /* 1 + 2^-11: tie, to even */
        {0x3f803000, 0x3c02, 0x3c01, 0x3c02, 0x3c01},
        {0x477fe000, 0x7bff, 0x7bff, 0x7bff, 0x7bff},   /* 65504 */
        {0x477fefff, 0x7bff, 0x7bff, 0x7c00, 0x7bff},
        {0x477ff000, 0x7c00, 0x7bff, 0x7c00, 0x7bff},   /* 65520: overflows in nearest */
        {0xc77ff000, 0xfc00, 0xfc00, 0xfbff, 0xfbff},
        {0x7f7fffff, 0x7c00, 0x7bff, 0x7c00, 0x7bff},
        {0x7f800000, 0x7c00, 0x7c00, 0x7c00, 0x7c00},
        {0xff800001, 0xfe00, 0xfe00, 0xfe00, 0xfe00},   /* SNaN: quieted */
        {0x7fffffff, 0x7fff, 0x7fff, 0x7fff, 0x7fff},
    };
    for (size_t i = 0; i < sizeof f / sizeof *f; i++) {
        const uint16_t want[4] = {f[i].rn, f[i].rd, f[i].ru, f[i].rz};
        for (unsigned rc = 0; rc < 4; rc++) assert(isa_float_to_half(f[i].single, rc, 0) == want[rc]);
    }
    /* DAZ zeroes denormal inputs (Zen 3: VCVTPS2PH honours MXCSR.DAZ); VCVTPH2PS has no DAZ. */
    assert(isa_float_to_half(0x00000001, 2, 1) == 0x0000 && isa_float_to_half(0x80400000, 1, 1) == 0x8000);
}

/* ---- 2: the conversions against the CPU ---- */

static void test_ph2ps_every_half(void) {
#ifdef __F16C__
    if (!native) return;
    static const uint32_t csrs[] = {0x1f80, 0x9fc0, 0x7f80, 0xdfc0};   /* default, DAZ+FTZ, RZ, RU+DAZ+FTZ */
    const uint32_t saved = _mm_getcsr();
    for (size_t c = 0; c < sizeof csrs / sizeof *csrs; c++)
        for (uint32_t h = 0; h < 0x10000; h += 4) {
            const uint16_t in[4] = {(uint16_t)h, (uint16_t)(h + 1), (uint16_t)(h + 2), (uint16_t)(h + 3)};
            uint32_t want[4];
            uint8_t ymm[64];
            memset(ymm, 0xee, sizeof ymm);
            memcpy(ymm + 32, in, 8);
            IsaSite s = f16c_site(ISA_VCVTPH2PS, 0, 0, 1, 0);
            _mm_setcsr(csrs[c]);
            native_ph2ps(in, want);
            isa_emulate_f16c(&s, NULL, ymm);
            _mm_setcsr(saved);
            for (int i = 16; i < 32; i++) assert(ymm[i] == 0);
            if (memcmp(want, ymm, 16) != 0)
                for (int i = 0; i < 4; i++) {
                    uint32_t got;
                    memcpy(&got, ymm + 4 * i, 4);
                    if (got != want[i]) {
                        fprintf(stderr, "vcvtph2ps %04x (mxcsr %04x): cpu %08x, emulated %08x\n", in[i], csrs[c],
                                want[i], got);
                        abort();
                    }
                }
            compared += 4;
        }
#endif
}

#ifdef __F16C__
static void compare_ps2ph(const uint32_t in[4], unsigned imm, uint32_t csr) {
    const uint32_t saved = _mm_getcsr();
    uint16_t want[4], got[4];
    _mm_setcsr(csr);
    native_ps2ph(in, imm, want);
    model_ps2ph(in, imm, got);
    _mm_setcsr(saved);
    if (memcmp(want, got, sizeof want) != 0)
        for (int i = 0; i < 4; i++)
            if (want[i] != got[i]) {
                fprintf(stderr, "vcvtps2ph %08x imm %u mxcsr %04x: cpu %04x, emulated %04x\n", in[i], imm, csr, want[i],
                        got[i]);
                abort();
            }
    compared += 4;
}
#endif

static void test_ps2ph_against_cpu(void) {
#ifdef __F16C__
    if (!native) return;
    /* Every sign, exponent and pattern of the low 14 mantissa bits (round, sticky and the last kept
     * bit of a normal half), upper mantissa bits random, for the four immediate rounding modes. */
    for (unsigned imm = 0; imm < 4; imm++)
        for (uint32_t top = 0; top < 512; top++)
            for (uint32_t low = 0; low < 0x4000; low += 4) {
                uint32_t in[4];
                for (int i = 0; i < 4; i++)
                    in[i] = top << 23 | ((uint32_t)next() & 0x7fc000) | (low + (uint32_t)i);
                compare_ps2ph(in, imm, 0x1f80);
            }
    /* Every immediate (bit 3 masks precision only, bits 7:4 are ignored: 0..15 covers them), with
     * MXCSR's RC, DAZ and FTZ in every combination: imm8[2] selects MXCSR.RC. */
    for (unsigned imm = 0; imm < 16; imm++)
        for (unsigned rc = 0; rc < 4; rc++)
            for (int flags = 0; flags < 4; flags++)
                for (int n = 0; n < 40000; n++) {
                    uint32_t in[4] = {test_float(), test_float(), test_float(), test_float()};
                    compare_ps2ph(in, imm, mxcsr_with(rc, flags & 1, flags >> 1));
                }
    /* Fully random words. */
    for (int n = 0; n < 1000000; n++) {
        uint32_t in[4] = {(uint32_t)next(), (uint32_t)next(), (uint32_t)next(), (uint32_t)next()};
        compare_ps2ph(in, (unsigned)(next() % 16), mxcsr_with((unsigned)(next() % 4), (int)(next() & 1), 0));
    }
#endif
}

/* ---- 3: whole rewritten sites ---- */

/* Registers around a call (run_state below): gpr in x86 numbering (rsp not used), all 16 YMM, and
 * the callee's red zone, filled with 0x5a before the call. Same as tests/test_isa_sse4a.c. */
typedef struct {
    uint64_t gpr[16];
    uint64_t rflags, pad;
    uint8_t ymm[16][32];
    uint64_t red_zone[16];
} State;
_Static_assert(sizeof(State) == 784, "run_state offsets");
void ABI f16c_run_state(void *code, State *state);

#define RS_IRP16 ".irp i,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n"
__asm__(".text\n"
        ".globl f16c_run_state\n"
#ifndef _WIN32
        ".type f16c_run_state, @function\n"
#endif
        "f16c_run_state:\n"
        "push %rbx\n push %rbp\n push %r12\n push %r13\n push %r14\n push %r15\n"
        "push %rsi\n push %rdi\n"                     /* [rsp] = code, [rsp+8] = state */
        "movabs $0x5a5a5a5a5a5a5a5a, %rax\n"
        RS_IRP16 "mov %rax, -16-8*\\i(%rsp)\n .endr\n" /* callee red zone: [rsp-136, rsp-8) */
        RS_IRP16 "vmovdqu 144+32*\\i(%rsi), %ymm\\i\n .endr\n"
        "pushq 128(%rsi)\n popfq\n"
        "mov 0(%rsi), %rax\n mov 8(%rsi), %rcx\n mov 16(%rsi), %rdx\n mov 24(%rsi), %rbx\n"
        "mov 40(%rsi), %rbp\n mov 56(%rsi), %rdi\n mov 64(%rsi), %r8\n mov 72(%rsi), %r9\n"
        "mov 80(%rsi), %r10\n mov 88(%rsi), %r11\n mov 96(%rsi), %r12\n mov 104(%rsi), %r13\n"
        "mov 112(%rsi), %r14\n mov 120(%rsi), %r15\n mov 48(%rsi), %rsi\n"
        "call *(%rsp)\n"
        "pushfq\n"
        "cld\n" /* Keep captured guest flags, then restore the C ABI direction flag. */
        "xchg %rsi, 16(%rsp)\n"                       /* rsi = state, slot = the guest's rsi */
        "mov %rax, 0(%rsi)\n mov %rcx, 8(%rsi)\n mov %rdx, 16(%rsi)\n mov %rbx, 24(%rsi)\n"
        "mov %rbp, 40(%rsi)\n mov %rdi, 56(%rsi)\n mov %r8, 64(%rsi)\n mov %r9, 72(%rsi)\n"
        "mov %r10, 80(%rsi)\n mov %r11, 88(%rsi)\n mov %r12, 96(%rsi)\n mov %r13, 104(%rsi)\n"
        "mov %r14, 112(%rsi)\n mov %r15, 120(%rsi)\n"
        "mov 16(%rsp), %rax\n mov %rax, 48(%rsi)\n"
        "pop %rax\n mov %rax, 128(%rsi)\n"
        RS_IRP16 "vmovdqu %ymm\\i, 144+32*\\i(%rsi)\n .endr\n"
        RS_IRP16 "mov -16-8*\\i(%rsp), %rax\n mov %rax, 656+8*\\i(%rsi)\n .endr\n"
        "vzeroupper\n"
        "add $16, %rsp\n"
        "pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbp\n pop %rbx\n"
        "ret\n");

/* One operand form. reg: the XMM/YMM register operand (destination of VCVTPH2PS, source of
 * VCVTPS2PH). rm: a register number, or with mem a base register (BASE_RIP: RIP-relative, to
 * RIP_DATA in the page) and disp8. */
enum { BASE_RIP = -1 };
typedef struct { uint8_t kind; int wide, reg, rm, mem, base, disp; uint8_t imm; } Form;

static size_t encode(uint8_t *out, const Form *f) {
    size_t n = 0;
    const int b = f->mem ? (f->base == BASE_RIP ? 0 : f->base) : f->rm;
    out[n++] = 0xc4;
    out[n++] = (uint8_t)((f->reg >= 8 ? 0 : 0x80) | 0x40 | (b >= 8 ? 0 : 0x20) | (f->kind == ISA_VCVTPH2PS ? 2 : 3));
    out[n++] = (uint8_t)(0x79 | f->wide << 2);                       /* W0 vvvv=1111 L pp=66 */
    out[n++] = f->kind == ISA_VCVTPH2PS ? 0x13 : 0x1d;
    if (!f->mem) {
        out[n++] = (uint8_t)(0xc0 | (f->reg & 7) << 3 | (f->rm & 7));
    } else if (f->base == BASE_RIP) {
        out[n++] = (uint8_t)(0x05 | (f->reg & 7) << 3);
        const size_t end = n + 4 + (f->kind == ISA_VCVTPS2PH);
        const int32_t rel = (int32_t)(RIP_DATA - (SITE_AT + (int)end));
        memcpy(out + n, &rel, 4);
        n += 4;
    } else {
        const int disp8 = f->disp != 0 || (f->base & 7) == 5;   /* [rbp]/[r13] need a displacement */
        out[n++] = (uint8_t)((disp8 ? 0x40 : 0) | (f->reg & 7) << 3 | (f->base & 7));
        if ((f->base & 7) == 4) out[n++] = 0x24;
        if (disp8) out[n++] = (uint8_t)(int8_t)f->disp;
    }
    if (f->kind == ISA_VCVTPS2PH) out[n++] = f->imm;
    return n;
}

/* The record scripts/isa_rewrite.py writes for the form. */
static IsaSite record_of(const Form *f, const uint8_t *code, size_t n) {
    const int ph2ps = f->kind == ISA_VCVTPH2PS;
    IsaSite s = f16c_site(f->kind, f->wide, ph2ps ? (uint8_t)f->reg : (uint8_t)f->rm,
                          ph2ps ? (uint8_t)f->rm : (uint8_t)f->reg, f->imm);
    s.offset = SITE_AT;
    s.length = (uint8_t)n;
    s.mode = ISA_MODE_JMP;
    memcpy(s.original, code, n);
    if (f->mem) {
        s.mem_slot = ph2ps ? ISA_MEM_SRC : ISA_MEM_DST;
        if (ph2ps) s.src1 = ISA_NO_REG;
        else s.dst = ISA_NO_REG;
        s.scale = 1;
        if (f->base == BASE_RIP) {
            s.mem_flags = ISA_MEM_RIP_RELATIVE;
            s.disp = RIP_DATA;
        } else {
            s.base = (uint8_t)f->base;
            s.disp = f->disp;
        }
    }
    return s;
}

/* A YMM register full of conversion inputs: floats for VCVTPS2PH, halves (each a random bit
 * pattern, so NaN, Inf and denormals all appear) for VCVTPH2PS. */
static void fill_ymm(uint8_t ymm[32], uint8_t kind) {
    for (int j = 0; j < 8; j++) {
        uint32_t v = kind == ISA_VCVTPS2PH ? test_float() : (uint32_t)next();
        memcpy(ymm + 4 * j, &v, 4);
    }
}

static void random_state(State *s, uint8_t kind) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < 16; i++) s->gpr[i] = i == 4 ? 0 : next();
    s->rflags = 0x202 | (next() & 0x8d5) | (next() & 1 ? 0x400 : 0);   /* CF PF AF ZF SF OF, DF */
    for (int i = 0; i < 16; i++) fill_ymm(s->ymm[i], kind);
}

static unsigned sites_run, forms_memory, forms_wide;
static uint8_t data[64];                 /* the memory operand of base-register forms */

static void run_site(const Form *f, int rounds) {
    uint8_t code[32];
    size_t n = encode(code, f);
    assert(n >= 5);
    code[n] = 0xc3;
    memset(pristine, 0xcc, PAGE);
    memcpy(pristine + SITE_AT, code, n + 1);
    memcpy(image, pristine, PAGE);
    memset(stubs, 0, PAGE);

    static IsaSite s;   /* the stub keeps a pointer to it */
    static IsaTable t;
    static uint64_t hits;
    s = record_of(f, code, n);
    memset(&t, 0, sizeof t);
    hits = 0;
    t.sites = &s;
    t.hits = &hits;
    t.count = 1;
    t.image_size = PAGE;
    IsaReport r = isa_rewrite_apply(&t, image, stubs, CPU_SET_F16C);
    assert(r.rewritten == 1 && r.jmp == 1 && r.skipped == 0);
    const uint32_t saved = _mm_getcsr();
    for (int round = 0; round < rounds; round++) {
        State in, want, got;
        uint8_t init[64], want_mem[64];
        random_state(&in, f->kind);
        fill_ymm(init, f->kind);
        fill_ymm(init + 32, f->kind);
        if (f->mem && f->base != BASE_RIP && f->base != 4) in.gpr[f->base] = (uint64_t)(uintptr_t)data - (uint64_t)(int64_t)f->disp;
        /* imm8[2]: MXCSR.RC rounds; any MXCSR (RC, DAZ, FTZ) must give the CPU's result. */
        const uint32_t csr = mxcsr_with((unsigned)(next() % 4), (int)(next() & 1), (int)(next() & 1));
        memcpy(&want, &in, sizeof want);
        memcpy(&got, &in, sizeof got);
        memcpy(data, init, 64);
        memcpy(pristine + RIP_DATA, init, 64);
        memcpy(image + RIP_DATA, init, 64);
        if (native) {
            _mm_setcsr(csr);
            f16c_run_state(pristine + SITE_AT, &want);
            _mm_setcsr(saved);
        }
        memcpy(want_mem, f->base == BASE_RIP ? pristine + RIP_DATA : data, 64);
        memcpy(data, init, 64);
        _mm_setcsr(csr);
        f16c_run_state(image + SITE_AT, &got);
        _mm_setcsr(saved);
        const uint8_t *got_mem = f->base == BASE_RIP ? image + RIP_DATA : data;
        if (!native) continue;   /* without F16C only the stub's survival is checked */
        if (memcmp(&want, &got, sizeof want) != 0 || memcmp(want_mem, got_mem, 64) != 0) {
            fprintf(stderr, "%s form (wide %d, reg %d, rm %d, mem %d, base %d, disp %d, imm %u) mxcsr %04x: state differs\n",
                    isa_kind_name(f->kind), f->wide, f->reg, f->rm, f->mem, f->base, f->disp, f->imm, csr);
            for (int i = 0; i < 16; i++)
                if (want.gpr[i] != got.gpr[i]) fprintf(stderr, "  gpr%d %016llx %016llx\n", i,
                                                       (unsigned long long)want.gpr[i], (unsigned long long)got.gpr[i]);
            if (want.rflags != got.rflags) fprintf(stderr, "  rflags %llx %llx\n", (unsigned long long)want.rflags,
                                                   (unsigned long long)got.rflags);
            for (int i = 0; i < 16; i++)
                if (memcmp(want.ymm[i], got.ymm[i], 32)) fprintf(stderr, "  ymm%d\n", i);
            if (memcmp(want.red_zone, got.red_zone, sizeof want.red_zone)) fprintf(stderr, "  red zone\n");
            if (memcmp(want_mem, got_mem, 64)) fprintf(stderr, "  memory\n");
            abort();
        }
        compared++;
    }
    assert(hits == (uint64_t)rounds);
    sites_run++;
    forms_memory += f->mem != 0;
    forms_wide += f->wide;
}

static void test_sites(void) {
    static const uint8_t kinds[] = {ISA_VCVTPH2PS, ISA_VCVTPS2PH};
    for (int k = 0; k < 2; k++)
        for (int wide = 0; wide < 2; wide++) {
            /* Register forms: every register pair. */
            for (int reg = 0; reg < 16; reg++)
                for (int rm = 0; rm < 16; rm++) {
                    Form f = {kinds[k], wide, reg, rm, 0, 0, 0, (uint8_t)(next() % 16)};
                    run_site(&f, 6);
                }
            /* Memory forms: every base but rsp (below), positive and negative displacements. */
            for (int reg = 0; reg < 16; reg++)
                for (int base = 0; base < 16; base++) {
                    if (base == 4) continue;
                    /* The base register is set so that base + disp is data. */
                    Form f = {kinds[k], wide, reg, 0, 1, base, (int)(next() % 256) - 128, (uint8_t)(next() % 16)};
                    run_site(&f, 4);
                }
            /* The stack: rsp-based operands in the red zone (the stub's gpr[4] is the guest rsp), and
             * RIP-relative ones (the record keeps the image offset). */
            for (int reg = 0; reg < 16; reg += 5) {
                Form low = {kinds[k], wide, reg, 0, 1, 4, -128, (uint8_t)(next() % 16)};
                Form high = {kinds[k], wide, reg, 0, 1, 4, -16, (uint8_t)(next() % 16)};
                Form rip = {kinds[k], wide, reg, 0, 1, BASE_RIP, 0, (uint8_t)(next() % 16)};
                run_site(&low, 50);
                run_site(&high, 50);
                run_site(&rip, 50);
            }
        }
    /* The forms of the 13 inFAMOUS 1.07 sites (0x612da8-0x61300e): vcvtps2ph xmm0, xmm0, 8;
     * vcvtps2ph xmm1, xmm1, 8; vcvtph2ps xmm0, xmm0; vcvtph2ps xmm0, [rdi]. */
    static const Form game[] = {{ISA_VCVTPS2PH, 0, 0, 0, 0, 0, 0, 8}, {ISA_VCVTPS2PH, 0, 1, 1, 0, 0, 0, 8},
                                {ISA_VCVTPH2PS, 0, 0, 0, 0, 0, 0, 0}, {ISA_VCVTPH2PS, 0, 0, 0, 1, 7, 0, 0}};
    static const char *const bytes[] = {"\xc4\xe3\x79\x1d\xc0\x08", "\xc4\xe3\x79\x1d\xc9\x08", "\xc4\xe2\x79\x13\xc0",
                                        "\xc4\xe2\x79\x13\x07"};
    for (int i = 0; i < 4; i++) {
        uint8_t code[16];
        const size_t n = encode(code, &game[i]);
        assert(n == strlen(bytes[i]) && memcmp(code, bytes[i], n) == 0);
        run_site(&game[i], 20000);
    }
}

static void test_table_records(void) {
    assert(isa_kind_is_f16c(ISA_VCVTPH2PS) && isa_kind_is_f16c(ISA_VCVTPS2PH) && !isa_kind_is_f16c(ISA_EXTRQ));
    assert(!isa_kind_is_sse4a(ISA_VCVTPS2PH));
    IsaSite s = f16c_site(ISA_VCVTPH2PS, 0, 0, 0, 0);
    assert(isa_site_set(&s) == CPU_SET_F16C && !strcmp(isa_kind_name(ISA_VCVTPS2PH), "vcvtps2ph"));
    /* A host lacking only SSE4A leaves F16C sites alone; F16C sites never take the int3 path. */
    memset(pristine, 0xcc, PAGE);
    memcpy(pristine + SITE_AT, "\xc4\xe2\x79\x13\xc0\xc3", 6);   /* vcvtph2ps xmm0, xmm0; ret */
    memcpy(image, pristine, PAGE);
    s.offset = SITE_AT;
    s.length = 5;
    s.mode = ISA_MODE_JMP;
    memcpy(s.original, pristine + SITE_AT, 5);
    uint64_t hits = 0;
    IsaTable t = {.sites = &s, .hits = &hits, .count = 1, .image_size = PAGE};
    assert(isa_rewrite_apply(&t, image, stubs, CPU_SET_SSE4A).rewritten == 0 && memcmp(image, pristine, PAGE) == 0);
    IsaRegs regs;
    memset(&regs, 0, sizeof regs);
    uintptr_t rip = 0;
    s.mode = ISA_MODE_INT3;
    assert(!isa_rewrite_trap(&t, (uintptr_t)image + SITE_AT, &regs, NULL, &rip) && rip == 0);
    /* A record with an unsupported F16C encoding stops the rewrite only when F16C is rewritten. */
    s.mode = ISA_MODE_UNSUPPORTED;
    assert(isa_rewrite_apply(&t, image, stubs, CPU_SET_SSE4A).unsupported_needed == 0);
    assert(isa_rewrite_apply(&t, image, stubs, CPU_SET_F16C).unsupported_needed == 1);
}

int main(void) {
#ifdef _WIN32
    unsigned char *region = VirtualAlloc(NULL, 3 * PAGE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    assert(region != NULL);
#else
    unsigned char *region = mmap(NULL, 3 * PAGE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(region != MAP_FAILED);
#endif
    image = region;
    stubs = region + PAGE;
    pristine = region + 2 * PAGE;
    __builtin_cpu_init();
    native = __builtin_cpu_supports("f16c") && __builtin_cpu_supports("avx");
#ifndef __F16C__
    native = 0;
#endif
    test_recorded_results();
    test_ph2ps_every_half();
    test_ps2ph_against_cpu();
    test_table_records();
    if (__builtin_cpu_supports("avx")) test_sites();   /* the stubs need AVX (vmovdqu ymm), as the game does */
    printf("isa-f16c-test ok (%s: %llu values and site runs compared; %u sites, %u memory, %u ymm)\n",
           native ? "F16C host, CPU as oracle" : "no F16C, recorded results only", compared, sites_run, forms_memory,
           forms_wide);
    return 0;
}
