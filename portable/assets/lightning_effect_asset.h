#ifndef PORTABLE_LIGHTNING_EFFECT_ASSET_H
#define PORTABLE_LIGHTNING_EFFECT_ASSET_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_LIGHTNING_EFFECT_FRAME_COUNT 4u
#define PORTABLE_LIGHTNING_EFFECT_FRAME_BYTES 0x200u
#define PORTABLE_LIGHTNING_EFFECT_TILE_BYTES \
    (PORTABLE_LIGHTNING_EFFECT_FRAME_COUNT*PORTABLE_LIGHTNING_EFFECT_FRAME_BYTES)

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    size_t frame_count;
    uint16_t tile_base;
} PortableLightningEffectAssetView;

#endif
