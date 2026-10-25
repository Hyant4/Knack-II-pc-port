// SPDX-License-Identifier: GPL-2.0-or-later
/* Isolated LibAtrac9 probe. It never writes PCM or sends audio to the runtime. */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libatrac9.h"

enum { MAX_CHANNELS = 8, MAX_FRAME_SAMPLES = 256 };

typedef struct {
    const char *config_hex;
    const char *input_path;
    uint64_t max_superframes;
    int reset_each_superframe;
    int verify_second_handle;
} Options;

static void usage(const char *program) {
    fprintf(stderr,
            "usage: %s --config HEX8 [--input RAW_AT9] [--max-superframes N]\n"
            "       [--reset-each-superframe] [--verify-second-handle]\n"
            "  HEX8 is the four-byte ATRAC9 config in bitstream order (for example FE7A17F0).\n"
            "  Without --input, only decoder initialization and codec metadata are tested.\n",
            program);
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_config(const char *text, unsigned char config[4]) {
    if (!text || strlen(text) != 8) return -1;
    for (size_t i = 0; i < 4; ++i) {
        int high = hex_nibble(text[i * 2]);
        int low = hex_nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return -1;
        config[i] = (unsigned char)((high << 4) | low);
    }
    /* LibAtrac9 assumes a valid channel-config index before indexing its table. */
    if (config[0] != 0xfe || (config[1] & 1) != 0 || ((config[1] >> 1) & 7) > 5) return -1;
    return 0;
}

static int parse_positive_u64(const char *text, uint64_t *value) {
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || !text[0] || !end || *end || parsed == 0) return -1;
    *value = (uint64_t)parsed;
    return 0;
}

static int parse_options(int argc, char **argv, Options *options) {
    memset(options, 0, sizeof(*options));
    options->max_superframes = 1;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) options->config_hex = argv[++i];
        else if (!strcmp(argv[i], "--input") && i + 1 < argc) options->input_path = argv[++i];
        else if (!strcmp(argv[i], "--max-superframes") && i + 1 < argc) {
            if (parse_positive_u64(argv[++i], &options->max_superframes)) return -1;
        } else if (!strcmp(argv[i], "--reset-each-superframe")) {
            options->reset_each_superframe = 1;
        } else if (!strcmp(argv[i], "--verify-second-handle")) {
            options->verify_second_handle = 1;
        } else return -1;
    }
    if (options->reset_each_superframe && options->verify_second_handle) return -1;
    return options->config_hex ? 0 : -1;
}

static void print_info(const unsigned char config[4], const Atrac9CodecInfo *info) {
    int frame_bytes = info->framesInSuperframe > 0
                          ? info->superframeSize / info->framesInSuperframe
                          : 0;
    printf("config=%02X%02X%02X%02X\n", config[0], config[1], config[2], config[3]);
    printf("channels=%d\n", info->channels);
    printf("channel_config_index=%d\n", info->channelConfigIndex);
    printf("sample_rate=%d\n", info->samplingRate);
    printf("average_frame_bytes=%d\n", frame_bytes);
    printf("frames_per_superframe=%d\n", info->framesInSuperframe);
    printf("superframe_bytes=%d\n", info->superframeSize);
    printf("frame_samples_per_channel=%d\n", info->frameSamples);
    printf("superframe_samples_per_channel=%d\n",
           info->frameSamples * info->framesInSuperframe);
}

static int codec_info_valid(const Atrac9CodecInfo *info) {
    return info->channels >= 1 && info->channels <= MAX_CHANNELS &&
           info->frameSamples >= 1 && info->frameSamples <= MAX_FRAME_SAMPLES &&
           info->framesInSuperframe >= 1 &&
           info->superframeSize >= info->framesInSuperframe &&
           info->superframeSize % info->framesInSuperframe == 0;
}

static int codec_info_equal(const Atrac9CodecInfo *left, const Atrac9CodecInfo *right) {
    return left->channels == right->channels &&
           left->channelConfigIndex == right->channelConfigIndex &&
           left->samplingRate == right->samplingRate &&
           left->superframeSize == right->superframeSize &&
           left->framesInSuperframe == right->framesInSuperframe &&
           left->frameSamples == right->frameSamples;
}

static int create_decoder(const unsigned char config[4], const Atrac9CodecInfo *expected,
                          void **handle_out) {
    void *handle = Atrac9GetHandle();
    if (!handle) return -1;
    unsigned char mutable_config[4];
    memcpy(mutable_config, config, sizeof(mutable_config));
    int error = Atrac9InitDecoder(handle, mutable_config);
    Atrac9CodecInfo info;
    memset(&info, 0, sizeof(info));
    if (!error) error = Atrac9GetCodecInfo(handle, &info);
    if (error || !codec_info_valid(&info) || (expected && !codec_info_equal(&info, expected))) {
        Atrac9ReleaseHandle(handle);
        return error ? error : -1;
    }
    *handle_out = handle;
    return 0;
}

static uint64_t fnv1a64_pcm(uint64_t hash, const int16_t *pcm, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        uint16_t sample = (uint16_t)pcm[i];
        hash = (hash ^ (unsigned char)sample) * UINT64_C(1099511628211);
        hash = (hash ^ (unsigned char)(sample >> 8)) * UINT64_C(1099511628211);
    }
    return hash;
}

static int decode_file(void **handle_ptr, void *second_handle, const unsigned char config[4],
                       const Atrac9CodecInfo *info, const Options *options) {
    FILE *input = fopen(options->input_path, "rb");
    if (!input) {
        fprintf(stderr, "input_open_error=%s\n", strerror(errno));
        return 4;
    }

    const size_t superframe_size = (size_t)info->superframeSize;
    unsigned char *encoded = malloc(superframe_size);
    if (!encoded) {
        fclose(input);
        fputs("allocation_error=encoded_superframe\n", stderr);
        return 5;
    }

    int16_t pcm[MAX_FRAME_SAMPLES * MAX_CHANNELS];
    int16_t pcm_second[MAX_FRAME_SAMPLES * MAX_CHANNELS];
    int16_t previous_last[MAX_CHANNELS] = {0};
    int have_previous_superframe = 0;
    uint64_t superframes = 0;
    uint64_t frames = 0;
    uint64_t input_bytes_advanced = 0;
    uint64_t decoder_bytes_used = 0;
    uint64_t pcm_samples = 0;
    long double sum_squares = 0.0L;
    unsigned peak = 0;
    uint64_t pcm_hash = UINT64_C(1469598103934665603);
    int dual_handle_match = second_handle ? 1 : 0;
    int result = 0;

    while (superframes < options->max_superframes) {
        size_t got = fread(encoded, 1, superframe_size, input);
        if (got == 0 && feof(input)) break;
        if (got != superframe_size) {
            fprintf(stderr, "partial_superframe_bytes=%zu expected=%zu\n", got, superframe_size);
            result = 6;
            break;
        }

        if (options->reset_each_superframe && superframes > 0) {
            Atrac9ReleaseHandle(*handle_ptr);
            *handle_ptr = NULL;
            int reset_error = create_decoder(config, info, handle_ptr);
            if (reset_error) {
                fprintf(stderr, "decoder_reset_error=0x%08X superframe=%" PRIu64 "\n",
                        (unsigned)reset_error, superframes);
                result = 11;
                break;
            }
        }

        size_t offset = 0;
        for (int frame = 0; frame < info->framesInSuperframe; ++frame) {
            int used = 0;
            size_t remaining_before = superframe_size - offset;
            int error = Atrac9Decode(*handle_ptr, encoded + offset, pcm, &used);
            if (error) {
                fprintf(stderr, "decode_error=0x%08X superframe=%" PRIu64 " frame=%d\n",
                        (unsigned)error, superframes, frame);
                result = 7;
                goto done;
            }
            if (used <= 0 || (size_t)used > superframe_size - offset) {
                fprintf(stderr, "invalid_decoder_bytes_used=%d remaining=%zu\n",
                        used, superframe_size - offset);
                result = 8;
                goto done;
            }
            size_t count = (size_t)info->frameSamples * (size_t)info->channels;
            if (second_handle) {
                int used_second = 0;
                int second_error = Atrac9Decode(second_handle, encoded + offset,
                                                pcm_second, &used_second);
                if (second_error != error || used_second != used ||
                    memcmp(pcm_second, pcm, count * sizeof(*pcm))) {
                    fprintf(stderr, "second_handle_mismatch=1 superframe=%" PRIu64
                                    " frame=%d\n", superframes, frame);
                    dual_handle_match = 0;
                    result = 12;
                    goto done;
                }
            }

            printf("frame_%" PRIu64 "_decoder_return=0\n", frames);
            printf("frame_%" PRIu64 "_bytes_used=%d\n", frames, used);
            printf("frame_%" PRIu64 "_superframe=%" PRIu64 "\n", frames, superframes);
            printf("frame_%" PRIu64 "_superframe_offset_before=%zu\n", frames, offset);
            printf("frame_%" PRIu64 "_remaining_before=%zu\n", frames, remaining_before);
            offset += (size_t)used;
            printf("frame_%" PRIu64 "_stream_position=%" PRIu64 "\n", frames,
                   input_bytes_advanced + offset);
            printf("frame_%" PRIu64 "_bounds_ok=1\n", frames);
            decoder_bytes_used += (uint64_t)used;

            long double frame_sum_squares = 0.0L;
            unsigned frame_peak = 0;
            for (size_t i = 0; i < count; ++i) {
                int value = pcm[i];
                unsigned magnitude = (unsigned)(value < 0 ? -(int64_t)value : value);
                if (magnitude > peak) peak = magnitude;
                if (magnitude > frame_peak) frame_peak = magnitude;
                sum_squares += (long double)value * (long double)value;
                frame_sum_squares += (long double)value * (long double)value;
            }
            pcm_hash = fnv1a64_pcm(pcm_hash, pcm, count);
            printf("frame_%" PRIu64 "_pcm_samples_total=%zu\n", frames, count);
            printf("frame_%" PRIu64 "_pcm_samples_per_channel=%d\n", frames,
                   info->frameSamples);
            printf("frame_%" PRIu64 "_rms_s16=%.6f\n", frames,
                   sqrt((double)(frame_sum_squares / count)));
            printf("frame_%" PRIu64 "_peak_s16=%u\n", frames, frame_peak);
            printf("frame_%" PRIu64 "_s16_out_of_range=0\n", frames);

            if (frame == 0 && have_previous_superframe) {
                long double boundary_sum_squares = 0.0L;
                unsigned boundary_peak = 0;
                for (int channel = 0; channel < info->channels; ++channel) {
                    int delta = (int)pcm[channel] - (int)previous_last[channel];
                    unsigned magnitude = (unsigned)(delta < 0 ? -(int64_t)delta : delta);
                    if (magnitude > boundary_peak) boundary_peak = magnitude;
                    boundary_sum_squares += (long double)delta * (long double)delta;
                }
                printf("boundary_%" PRIu64 "_max_abs_delta=%u\n", superframes,
                       boundary_peak);
                printf("boundary_%" PRIu64 "_rms_delta=%.6f\n", superframes,
                       sqrt((double)(boundary_sum_squares / info->channels)));
            }
            for (int channel = 0; channel < info->channels; ++channel) {
                previous_last[channel] =
                    pcm[((size_t)info->frameSamples - 1) * (size_t)info->channels +
                        (size_t)channel];
            }
            pcm_samples += count;
            ++frames;
        }
        if (offset != superframe_size) {
            fprintf(stderr, "superframe_boundary_error=%zu expected=%zu\n",
                    offset, superframe_size);
            result = 13;
            break;
        }
        printf("superframe_%" PRIu64 "_decoder_bytes_used=%zu\n", superframes, offset);
        printf("superframe_%" PRIu64 "_boundary_ok=1\n", superframes);
        input_bytes_advanced += superframe_size;
        ++superframes;
        have_previous_superframe = 1;
    }

    if (!result && ferror(input)) {
        fputs("input_read_error=1\n", stderr);
        result = 9;
    } else if (!result && superframes == 0) {
        fprintf(stderr, "input_too_short=1 expected_at_least=%zu\n", superframe_size);
        result = 10;
    }

done:
    printf("decode=%s\n", result ? "error" : "ok");
    printf("superframes_decoded=%" PRIu64 "\n", superframes);
    printf("frames_decoded=%" PRIu64 "\n", frames);
    printf("input_bytes_advanced=%" PRIu64 "\n", input_bytes_advanced);
    printf("decoder_bytes_used=%" PRIu64 "\n", decoder_bytes_used);
    printf("pcm_samples_total=%" PRIu64 "\n", pcm_samples);
    printf("pcm_samples_per_channel=%" PRIu64 "\n",
           info->channels > 0 ? pcm_samples / (uint64_t)info->channels : 0);
    printf("rms_s16=%.6f\n", pcm_samples ? sqrt((double)(sum_squares / pcm_samples)) : 0.0);
    printf("rms_normalized=%.9f\n",
           pcm_samples ? sqrt((double)(sum_squares / pcm_samples)) / 32768.0 : 0.0);
    printf("peak_s16=%u\n", peak);
    printf("peak_normalized=%.9f\n", peak / 32768.0);
    printf("pcm_fnv1a64=%016" PRIx64 "\n", pcm_hash);
    printf("reset_each_superframe=%d\n", options->reset_each_superframe);
    if (second_handle) printf("dual_handle_match=%d\n", dual_handle_match);
    puts("s16_out_of_range=0");

    free(encoded);
    fclose(input);
    return result;
}

int main(int argc, char **argv) {
    Options options;
    if (parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return 2;
    }

    unsigned char config[4];
    if (parse_config(options.config_hex, config)) {
        fputs("config_error=expected_safe_ATRAC9_HEX8_in_bitstream_order\n", stderr);
        return 2;
    }

    void *handle = NULL;
    int error = create_decoder(config, NULL, &handle);
    if (error) {
        fprintf(stderr, "decoder_init_error=0x%08X\n", (unsigned)error);
        return 3;
    }

    Atrac9CodecInfo info;
    memset(&info, 0, sizeof(info));
    error = Atrac9GetCodecInfo(handle, &info);
    if (error || !codec_info_valid(&info)) {
        fprintf(stderr, "codec_info_error=0x%08X\n", (unsigned)error);
        Atrac9ReleaseHandle(handle);
        return 3;
    }

    print_info(config, &info);
    void *second_handle = NULL;
    if (options.verify_second_handle) {
        error = create_decoder(config, &info, &second_handle);
        if (error) {
            fprintf(stderr, "second_decoder_init_error=0x%08X\n", (unsigned)error);
            Atrac9ReleaseHandle(handle);
            return 3;
        }
        puts("second_handle_initialized=1");
    }
    int result = 0;
    if (options.input_path) {
        result = decode_file(&handle, second_handle, config, &info, &options);
    }
    else {
        puts("decode=not-run");
        puts("reason=no-authorized-input-supplied");
    }
    if (second_handle) Atrac9ReleaseHandle(second_handle);
    if (handle) Atrac9ReleaseHandle(handle);
    return result;
}
