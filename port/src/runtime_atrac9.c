/* Shared LibAtrac9 synchronization. Init rewrites process-global decode tables, so it excludes
 * every Decode. Independent decoders may Decode concurrently after initialization. */
#include "runtime_atrac9.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RuntimeAtrac9 {
    pthread_mutex_t lock;
    void *handle;
};

static pthread_rwlock_t atrac9_gate;
static pthread_once_t atrac9_gate_once=PTHREAD_ONCE_INIT;
static int atrac9_gate_error;

static void runtime_atrac9_init_gate(void) {
    atrac9_gate_error=pthread_rwlock_init(&atrac9_gate,NULL);
}

static int runtime_atrac9_gate_ready(void) {
    int error=pthread_once(&atrac9_gate_once,runtime_atrac9_init_gate);
    return error ? error : atrac9_gate_error;
}

RuntimeAtrac9 *runtime_atrac9_create(void) {
    if (runtime_atrac9_gate_ready()) return NULL;
    RuntimeAtrac9 *decoder=calloc(1,sizeof(*decoder));
    if (!decoder) return NULL;
    if (pthread_mutex_init(&decoder->lock,NULL)) { free(decoder); return NULL; }
    decoder->handle=Atrac9GetHandle();
    if (!decoder->handle) { pthread_mutex_destroy(&decoder->lock); free(decoder); return NULL; }
    return decoder;
}

void runtime_atrac9_release(RuntimeAtrac9 *decoder) {
    if (!decoder) return;
    pthread_mutex_lock(&decoder->lock);
    Atrac9ReleaseHandle(decoder->handle);
    decoder->handle=NULL;
    pthread_mutex_unlock(&decoder->lock);
    pthread_mutex_destroy(&decoder->lock);
    free(decoder);
}

int runtime_atrac9_init(RuntimeAtrac9 *decoder, const unsigned char config[4], Atrac9CodecInfo *info) {
    if (!decoder || !config) return (int)UINT32_C(0x81000000);
    unsigned char mutable_config[4];
    memcpy(mutable_config,config,sizeof(mutable_config));
    pthread_mutex_lock(&decoder->lock);
    if (pthread_rwlock_wrlock(&atrac9_gate)) {
        pthread_mutex_unlock(&decoder->lock); return (int)UINT32_C(0x81000000);
    }
    int error=Atrac9InitDecoder(decoder->handle,mutable_config);
    if (!error && info) (void)Atrac9GetCodecInfo(decoder->handle,info);
    pthread_rwlock_unlock(&atrac9_gate);
    pthread_mutex_unlock(&decoder->lock);
    return error;
}

int runtime_atrac9_decode(RuntimeAtrac9 *decoder, const unsigned char *input, int16_t *pcm,
                          int *bytes_used) {
    if (!decoder || !input || !pcm || !bytes_used) return (int)UINT32_C(0x80000000);
    pthread_mutex_lock(&decoder->lock);
    if (pthread_rwlock_rdlock(&atrac9_gate)) {
        pthread_mutex_unlock(&decoder->lock); return (int)UINT32_C(0x80000000);
    }
    int error=Atrac9Decode(decoder->handle,input,pcm,bytes_used);
    pthread_rwlock_unlock(&atrac9_gate);
    pthread_mutex_unlock(&decoder->lock);
    return error;
}
