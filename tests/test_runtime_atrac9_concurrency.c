/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime_atrac9.h"

typedef struct {
    RuntimeAtrac9 *decoder;
    const unsigned char *encoded;
    size_t encoded_size;
    uint64_t hash;
} DecodeWorker;

static uint64_t hash_samples(uint64_t hash,const int16_t *pcm,size_t count) {
    for (size_t i=0;i<count;++i) {
        uint16_t sample=(uint16_t)pcm[i];
        hash=(hash^(unsigned char)sample)*UINT64_C(1099511628211);
        hash=(hash^(unsigned char)(sample>>8))*UINT64_C(1099511628211);
    }
    return hash;
}
static void *decode_worker(void *opaque) {
    DecodeWorker *worker=opaque;
    int16_t pcm[256*8]; worker->hash=UINT64_C(1469598103934665603);
    for (size_t superframe=0;superframe<worker->encoded_size/768;++superframe) {
        size_t offset=superframe*768;
        for (int frame=0;frame<4;++frame) {
            int used=0;
            assert(!runtime_atrac9_decode(worker->decoder,worker->encoded+offset,pcm,&used));
            assert(used>0 && (size_t)used<=768-(offset%768));
            offset+=(size_t)used;
            worker->hash=hash_samples(worker->hash,pcm,256*4);
        }
        assert(offset==(superframe+1)*768);
    }
    return NULL;
}
static void *init_worker(void *opaque) {
    RuntimeAtrac9 *decoder=opaque; Atrac9CodecInfo info;
    const unsigned char configs[2][4]={{0xfe,0x7a,0x17,0xf0},{0xfe,0x78,0x29,0xf0}};
    for (int i=0;i<10000;++i) assert(!runtime_atrac9_init(decoder,configs[i&1],&info));
    return NULL;
}
static unsigned char *read_fixture(const char *path,size_t *size) {
    FILE *file=fopen(path,"rb"); assert(file); assert(!fseek(file,0,SEEK_END));
    long length=ftell(file); assert(length==6144); assert(!fseek(file,0,SEEK_SET));
    unsigned char *data=malloc((size_t)length); assert(data);
    assert(fread(data,1,(size_t)length,file)==(size_t)length); assert(!fclose(file));
    *size=(size_t)length; return data;
}
static RuntimeAtrac9 *initialized(void) {
    RuntimeAtrac9 *decoder=runtime_atrac9_create(); assert(decoder); Atrac9CodecInfo info;
    const unsigned char config[4]={0xfe,0x7a,0x17,0xf0};
    assert(!runtime_atrac9_init(decoder,config,&info));
    assert(info.channels==4 && info.samplingRate==48000 && info.superframeSize==768);
    return decoder;
}

int main(int argc,char **argv) {
    assert(argc==2); size_t size=0; unsigned char *fixture=read_fixture(argv[1],&size);
    DecodeWorker left={initialized(),fixture,size,0},right={initialized(),fixture,size,0};
    RuntimeAtrac9 *initializer=runtime_atrac9_create(); assert(initializer);
    pthread_t threads[3];
    assert(!pthread_create(&threads[0],NULL,decode_worker,&left));
    assert(!pthread_create(&threads[1],NULL,decode_worker,&right));
    assert(!pthread_create(&threads[2],NULL,init_worker,initializer));
    for (size_t i=0;i<3;++i) assert(!pthread_join(threads[i],NULL));
    assert(left.hash==UINT64_C(0xa79abed856a64db5));
    assert(right.hash==left.hash);
    runtime_atrac9_release(initializer); runtime_atrac9_release(right.decoder); runtime_atrac9_release(left.decoder);
    free(fixture);
    printf("PASS: real ATRAC9 concurrent init/decode hash=%016" PRIx64 "\n",left.hash);
    return 0;
}
