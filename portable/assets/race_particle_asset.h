#ifndef MKSC_PORTABLE_RACE_PARTICLE_ASSET_H
#define MKSC_PORTABLE_RACE_PARTICLE_ASSET_H

#include "asset_pack.h"
#include <stddef.h>
#include <stdint.h>

#define PORTABLE_RACE_PARTICLE_FRAME_CAPACITY 32u
#define PORTABLE_DRIFT_SPARK_FRAME_COUNT 5u
#define PORTABLE_COIN_PICKUP_FRAME_COUNT 7u
#define PORTABLE_RECOVERY_THAW_FRAME_COUNT 3u
#define PORTABLE_RECOVERY_THAW_CELL_BYTES 174u
#define PORTABLE_RECOVERY_DRIP_FRAME_COUNT 1u
#define PORTABLE_RECOVERY_DRIP_CELL_BYTES 8u
#define PORTABLE_ENTRY_SPLASH_FRAME_COUNT 3u
#define PORTABLE_ENTRY_SPLASH_CELL_BYTES 24u
#define PORTABLE_LAVA_SMOKE_FRAME_COUNT 2u
#define PORTABLE_LAVA_SMOKE_CELL_BYTES 16u
#define PORTABLE_COIN_PICKUP_CELL_BYTES 50u
#define PORTABLE_MUSHROOM_BURST_FRAME_COUNT 12u
#define PORTABLE_MUSHROOM_BURST_CELL_BYTES 528u
#define PORTABLE_RACE_PARTICLE_TILE_BYTES 0x2000u
#define PORTABLE_RACE_PARTICLE_PALETTE_COUNT 80u
#define PORTABLE_DRIFT_SPARK_CELL_BYTES 160u

typedef struct
{
    uint32_t offset;
    uint16_t size;
    uint16_t duration;
} PortableRaceParticleFrame;

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint8_t *cells;
    size_t cell_bytes;
    const PortableRaceParticleFrame *frames;
    size_t frame_count;
    uint16_t tile_base;
    uint8_t palette_base;
} PortableRaceParticleAssetView;

/* Source 5B44C sequences share this bounded native storage. */
typedef struct
{
    uint8_t tiles[PORTABLE_RACE_PARTICLE_TILE_BYTES];
    uint16_t palette[PORTABLE_RACE_PARTICLE_PALETTE_COUNT];
    uint8_t cells[PORTABLE_MUSHROOM_BURST_CELL_BYTES];
    PortableRaceParticleFrame frames[PORTABLE_RACE_PARTICLE_FRAME_CAPACITY];
} PortableRaceParticleStorage;

/* Shared FXC1 decoder. Optional expected counts retain source-owner schema
 * checks; returns 1 on success. Failed decoding preserves out, not scratch. */
int portable_race_particle_asset_open(const PortableAssetSectionView *,
                                      PortableRaceParticleStorage *, uint32_t expected_frames,
                                      uint32_t expected_cells, PortableRaceParticleAssetView *out);

#endif
