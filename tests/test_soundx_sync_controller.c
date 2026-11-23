/* Local-image integration experiment. No game bytes are distributed here.
 * The runner authenticates the image before passing it on stdin. Only the
 * controller/parameter-setter pages are executable; instructions stay unchanged.
 * Clock-query results and the bridge from factors to NGS2 commands are fixtures,
 * not the complete guest query/producer. This is not a cutscene reproduction.
 */
#define main prior_mixer_main
#include "test_ngs2_mixer.c"
#undef main
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define IMAGE_SIZE 28377232u
#define CONTROL_OFFSET 0x5ce5e0u
#define RECORD_SIZE 0x150u
#define RECORD_COUNT 10u
#define DT (256.0 / 48000.0)

typedef void (ABI *Controller)(void *, float);
typedef int (ABI *ClockQuery)(void *, uint32_t, float *, float *);
typedef void (ABI *ParameterSetter)(void *, uint32_t, float);
static unsigned char records[RECORD_SIZE * RECORD_COUNT];
static unsigned char selector[0x24], other_selector[0x24], context[16];
static Controller controller;
static ParameterSetter set_parameter;
static float query_position, query_auxiliary;
static int query_result = 1;
static unsigned queries;
static uint64_t cookie = UINT64_C(0xc12e59b703a6d841);

static float read_float(const void *data, size_t offset) {
    float value; memcpy(&value, (const unsigned char *)data + offset, 4); return value;
}
static void write_float(void *data, size_t offset, float value) {
    memcpy((unsigned char *)data + offset, &value, 4);
}
static int ABI query_clock(void *object, uint32_t mask, float *position, float *auxiliary) {
    assert(object == context && mask == (1u << 21));
    ++queries; *position = query_position; *auxiliary = query_auxiliary; return query_result;
}
static ClockQuery query_table[] = {query_clock};

static void reset_controller(void) {
    memset(context, 0, sizeof(context)); put_uptr(context, 0, (uintptr_t)query_table);
    put_u32(context, 8, 1u << 21);
    memset(records, 0, sizeof(records));
    selector[0x23] = 21; other_selector[0x23] = 22;
    for (unsigned i = 0; i < RECORD_COUNT; ++i) {
        unsigned char *record = records + i * RECORD_SIZE;
        put_u32(record, 0x1c, 1);
        put_uptr(record, 0x10, (uintptr_t)(i ? other_selector : selector));
        write_float(record, 0x100, 1); write_float(record, 0x108, 1);
    }
    queries = 0; query_result = 1; query_auxiliary = 3;
}

static void step_controller(float reference, float position) {
    unsigned before = queries; query_position = position; controller(context, reference);
    assert(queries == before + 1);
    for (unsigned i = 1; i < RECORD_COUNT; ++i) {
        assert(read_float(records + i * RECORD_SIZE, 0x100) == 1);
        assert(read_float(records + i * RECORD_SIZE, 0x108) == 1);
    }
}

static void load_controller(void) {
    unsigned char *image = native_test_allocate_code(IMAGE_SIZE); assert(image);
    assert(fread(image, 1, IMAGE_SIZE, stdin) == IMAGE_SIZE);
    assert(getchar() == EOF);
    /* All writes are to test-owned data slots in a private memory copy. */
    put_uptr(image, 0x1247b18, (uintptr_t)&cookie);
    put_uptr(image, 0x16ef710, (uintptr_t)records);
    assert(native_test_executable(image + 0x5ce000, 4096));
    assert(native_test_executable(image + 0xb47000, 4096));
    controller = (Controller)(uintptr_t)(image + CONTROL_OFFSET);
    set_parameter = (ParameterSetter)(uintptr_t)(image + 0xb47aa0);
    /* Allocation is released explicitly after all calls. */
}

static float pitch_parameter(float requested) {
    unsigned char parameters[0x100] = {0};
    /* The original SetPitch dispatcher selects parameter 0x10; its original
     * metadata specifies float at +0x68, range [0,4]. Execute the leaf setter. */
    set_parameter(parameters, 0x10, requested);
    return read_float(parameters, 0x68);
}

static void pitch_parameter_cases(void) {
    const float requests[] = {0, .5f, 1, 2, 4, 5, -1};
    const float expected[] = {0, .5f, 1, 2, 4, 4, 0};
    for (unsigned i = 0; i < sizeof(requests)/sizeof(*requests); ++i) {
        float stored = pitch_parameter(requests[i]); assert(stored == expected[i]);
        printf("PARAMETER requested=%.9g stored=%.9g\n", requests[i], stored);
    }
}

static void decision_cases(void) {
    const struct {const char *name; float position, auxiliary, gain, speed;} cases[] = {
        {"aligned", 10, 3, 1, 1},
        {"late_small", 9.5f, 3, 1, 1},
        {"late_large", 9, 3, 0, 4},
        {"early_small", 10.5f, 3, 1, 1},
        {"early_large", 11, 3, 0, 0},
        {"auxiliary_gate", 9, 1, 1, 1},
    };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(*cases); ++i) {
        reset_controller(); query_auxiliary = cases[i].auxiliary;
        step_controller(10, cases[i].position);
        float gain = read_float(records, 0x108), speed = read_float(records, 0x100);
        assert(gain == cases[i].gain && speed == cases[i].speed);
        printf("CASE name=%s gain=%.9g speed=%.9g muted=%u\n", cases[i].name, gain, speed, context[12]);
    }
    reset_controller(); step_controller(10, 9);
    step_controller(10, 9.9f);
    assert(context[12] == 1 && read_float(records, 0x108) == 0);
    step_controller(10, 9.98f);
    assert(!context[12] && read_float(records, 0x108) == 1 && read_float(records, 0x100) == 1);
    puts("CASE name=external_realign gain=1 speed=1 muted=0");
}

/* Independent, rate-responsive clock fixture, not a replacement mixer. It
 * demonstrates what the actual controller does when its factors affect time. */
static void responsive_clock(double initial_error) {
    reset_controller(); double position = 10 + initial_error;
    unsigned recovered = 0, muted = 0;
    for (unsigned tick = 0; tick < 240; ++tick) {
        double reference = 10 + tick * DT;
        step_controller((float)reference, (float)position);
        float gain = read_float(records, 0x108), speed = read_float(records, 0x100);
        if (!gain) ++muted;
        if (muted && gain && !recovered) recovered = tick;
        position += speed * DT;
    }
    double final_error = position - (10 + 240 * DT);
    assert(muted && recovered && fabs(final_error) < .05);
    printf("LOOP backend=responsive_clock initial_error=%.3f ticks=240 muted=%u recovered_tick=%u final_error=%.9f\n",
           initial_error, muted, recovered, final_error);
}

static uint64_t consumed(Ngs2Handle *voice) {
    unsigned char state[0x30]; uint64_t clock;
    assert(!ngs2_voice_state(voice, state, sizeof(state))); memcpy(&clock, state + 0x10, 8); return clock;
}

static int32_t bridge_factors(Ngs2Handle *voice) {
    float matrix[8] = {read_float(records, 0x108)}; set_voice_matrix(voice, matrix, 8);
    unsigned char cmd[0x10]; command(cmd, sizeof(cmd), UINT32_C(0x10000005));
    write_float(cmd, 8, pitch_parameter(read_float(records, 0x100)));
    return ngs2_voice_control(voice, cmd);
}

/* Recovery acceptance gate. The fixture
 * it supplies a full FIFO so acceleration cannot fail merely because this
 * fixture feeds only one grain per output quantum. No clock is forced back
 * into alignment and no gain is synthesized outside the real controller. */
static int hle_recovery_acceptance(double initial_error) {
    reset_controller();
    unsigned char so[0x40], ro[0xa8]; system_option(so); rack_option(ro, 1, "Sampler");
    Ngs2Handle *system = NULL, *rack = NULL, *handle = NULL;
    assert(!ngs2_system_create(so, NULL, &system));
    assert(!ngs2_rack_create(system, 0x1000, ro, NULL, &rack));
    assert(!ngs2_voice_handle(rack, 0, &handle)); setup_waveform(handle, 1); set_voice_state(handle, 0);
    float main_pcm[256*8], auxiliary_pcm[256*8];
    Ngs2RenderBuffer buffers[] = {{main_pcm, sizeof(main_pcm), 0x18, 8},
                                {auxiliary_pcm, sizeof(auxiliary_pcm), 0x18, 8}};
    unsigned muted = 0, recovered = 0, rejected = 0, nonzero = 0;
    for (unsigned tick = 0; tick < 240; ++tick) {
        double reference = 10 + tick * DT;
        double position = 10 + initial_error + consumed(handle) / 48000.0;
        step_controller((float)reference, (float)position);
        float gain = read_float(records, 0x108);
        if (!gain) ++muted;
        if (muted && gain && !recovered) recovered = tick;
        if (bridge_factors(handle)) ++rejected;
        pthread_mutex_lock(&ngs2_lock);
        Ngs2Voice *voice = ngs2_voice_locked(handle);
        while (!voice->atrac9 || voice->atrac9->pcm_count < NGS2_ATRAC9_FIFO_CAPACITY)
            push_synthetic_grain(voice, 12000);
        pthread_mutex_unlock(&ngs2_lock);
        /* A host trylock miss is not a committed simulated time step. Retry
         * unchanged inputs; zero speed cannot use sample progress as a probe. */
        int committed = 0;
        for (unsigned retry = 0; retry < 100; ++retry) {
            Ngs2System *live = ngs2_system_locked(system);
            uint64_t before = live->trylock_contentions + atomic_load(&ngs2_global_trylock_contentions);
            assert(!ngs2_render(system, buffers, 2));
            if (live->trylock_contentions + atomic_load(&ngs2_global_trylock_contentions) == before) {
                committed = 1; break;
            }
            Sleep(1);
        }
        assert(committed);
        for (unsigned i = 0; i < 256*8; ++i) nonzero += main_pcm[i] != 0;
    }
    double error = initial_error + consumed(handle) / 48000.0 - 240 * DT;
    int ok = muted && recovered && !rejected && nonzero && fabs(error) < .05;
    printf("ACCEPTANCE initial_error=%.3f recovered_tick=%u rejected=%u nonzero=%u final_error=%.9f result=%s\n",
           initial_error, recovered, rejected, nonzero, error, ok ? "PASS" : "FAIL");
    assert(!ngs2_system_destroy(system, NULL)); return ok;
}

int main(int argc, char **argv) {
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "--require-recovery")));
    if (!__builtin_cpu_supports("avx")) { fputs("SKIP: host AVX is required\n", stderr); return 77; }
#ifdef _WIN32
    assert(_setmode(_fileno(stdin), _O_BINARY) != -1);
#endif
    load_controller();
    ngs2_atrac9_setting = 1; ngs2_render_mix_setting = 1;
    ngs2_b2f2a_setting = 0; ngs2_b2f2c_setting = 1;
    atomic_store(&ngs2_audio_diag_setting, 0);
    pitch_parameter_cases(); decision_cases(); responsive_clock(-1); responsive_clock(1);
    int ok = 1;
    {
        int late_ok = hle_recovery_acceptance(-1);
        int early_ok = hle_recovery_acceptance(1);
        ok = late_ok && early_ok;
    }
    assert(native_test_release_code((unsigned char *)(uintptr_t)controller - CONTROL_OFFSET, IMAGE_SIZE));
    if (!ok) return 2;
    puts("PASS: original controller decisions and HLE recovery acceptance");
    return 0;
}
