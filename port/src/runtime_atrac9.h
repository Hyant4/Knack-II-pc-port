#ifndef TLG_RUNTIME_ATRAC9_H
#define TLG_RUNTIME_ATRAC9_H

#include <stdint.h>
#include "third_party/LibAtrac9/C/src/libatrac9.h"

typedef struct RuntimeAtrac9 RuntimeAtrac9;

/* Each decoder has one owner. The owner must prevent new calls before release. Calls already
 * executing on the same decoder are serialized by the adapter. The process-wide Init/Decode
 * gate is initialized before the first handle is exposed. */
RuntimeAtrac9 *runtime_atrac9_create(void);
void runtime_atrac9_release(RuntimeAtrac9 *decoder);
int runtime_atrac9_init(RuntimeAtrac9 *decoder, const unsigned char config[4], Atrac9CodecInfo *info);
int runtime_atrac9_decode(RuntimeAtrac9 *decoder, const unsigned char *input, int16_t *pcm,
                          int *bytes_used);

#endif
