/* SPDX-License-Identifier: GPL-2.0-or-later
 * AJM ATRAC9 regression contract. Input is a temporary, user-authorized fixture.
 */
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../port/src/runtime_ajm.c"

const char *runtime_symbol(const char *name) { return name; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int runtime_mp3_parse(const unsigned char *data,uint32_t size,int parse_ofl,int layer3_only,Mp3Frame *frame) {
    (void)data; (void)size; (void)parse_ofl; (void)layer3_only; (void)frame; return -1;
}
Mp3Decoder *runtime_mp3_open(void) { return NULL; }
void runtime_mp3_close(Mp3Decoder *decoder) { (void)decoder; }
void runtime_mp3_reset(Mp3Decoder *decoder) { (void)decoder; }
int runtime_mp3_decode(Mp3Decoder *decoder,const unsigned char *data,int size,float *pcm,int max_samples,int *channels) {
    (void)decoder; (void)data; (void)size; (void)pcm; (void)max_samples; (void)channels; return -1;
}

static uint64_t fnv1a64(const int16_t *pcm,size_t count) {
    uint64_t hash=UINT64_C(1469598103934665603);
    for (size_t i=0;i<count;++i) {
        uint16_t sample=(uint16_t)pcm[i];
        hash=(hash^(unsigned char)sample)*UINT64_C(1099511628211);
        hash=(hash^(unsigned char)(sample>>8))*UINT64_C(1099511628211);
    }
    return hash;
}

static unsigned char *read_fixture(const char *path,size_t *size) {
    FILE *file=fopen(path,"rb"); assert(file);
    assert(!fseek(file,0,SEEK_END));
    long length=ftell(file); assert(length>0 && !(length%768));
    assert(!fseek(file,0,SEEK_SET));
    unsigned char *data=malloc((size_t)length); assert(data);
    assert(fread(data,1,(size_t)length,file)==(size_t)length);
    assert(!fclose(file)); *size=(size_t)length; return data;
}

static void decoder_init(Instance *instance) {
    memset(instance,0,sizeof(*instance));
    instance->used=1; instance->codec=CODEC_AT9; instance->format=FORMAT_S16;
    const unsigned char config[4]={0xfe,0x7a,0x17,0xf0};
    memcpy(instance->config,config,sizeof(config)); at9_reset(instance);
    assert(instance->initialized);
    assert(instance->info.channels==4 && instance->info.samplingRate==48000);
    assert(instance->info.frameSamples==256 && instance->info.framesInSuperframe==4);
    assert(instance->info.superframeSize==768);
}

static void decode(Instance *instance,const unsigned char *encoded,size_t encoded_size,
                   int16_t *pcm,size_t pcm_samples) {
    ChunkBuffer chunk={0,(uint32_t)(pcm_samples*sizeof(*pcm)),pcm};
    Output output={&chunk,1,0,0};
    const unsigned char *input=encoded; uint64_t input_size=encoded_size;
    const size_t frames=encoded_size/768*4;
    for (size_t i=0;i<frames;++i) {
        uint32_t written=0; int32_t internal=0;
        assert(at9_frame(instance,&input,&input_size,&output,&written,&internal)==0);
        assert(internal==0 && written==256);
    }
    assert(input_size==0 && input==encoded+encoded_size);
    assert(output.current==1 && output.offset==0);
}

int main(int argc,char **argv) {
    assert(argc==2);
    size_t encoded_size=0; unsigned char *encoded=read_fixture(argv[1],&encoded_size);
    const size_t pcm_samples=encoded_size/768*4*256*4;
    int16_t *first=calloc(pcm_samples,sizeof(*first));
    int16_t *second=calloc(pcm_samples,sizeof(*second));
    assert(first && second);

    Instance left,right; decoder_init(&left); decoder_init(&right);
    decode(&left,encoded,encoded_size,first,pcm_samples);
    decode(&right,encoded,encoded_size,second,pcm_samples);
    assert(!memcmp(first,second,pcm_samples*sizeof(*first)));
    uint64_t hash=fnv1a64(first,pcm_samples);
    assert(encoded_size!=6144 || hash==UINT64_C(0xa79abed856a64db5));
    instance_release(&left); instance_release(&right);

    printf("PASS: AJM ATRAC9 bytes=%zu samples=%zu hash=%016" PRIx64 "\n",
           encoded_size,pcm_samples,hash);
    free(second); free(first); free(encoded); return 0;
}
