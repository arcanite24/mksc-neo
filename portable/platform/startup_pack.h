#ifndef PORTABLE_STARTUP_PACK_H
#define PORTABLE_STARTUP_PACK_H
#include "asset_pack.h"
/* Probe retains the verified bytes; take transfers them exactly once. This
 * avoids reading/hashing the remembered pack twice on ordinary startup. */
int portable_startup_pack_probe(const char *path, int hidden, uint32_t *version);
int portable_startup_pack_take(const char *path, int hidden, uint8_t **bytes,
                               size_t *size, PortableAssetPackView *view);
void portable_startup_pack_discard(void);
#endif
