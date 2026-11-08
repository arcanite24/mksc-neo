#ifndef MKSC_PORTABLE_TRACK_TILESET_ASSET_H
#define MKSC_PORTABLE_TRACK_TILESET_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "track_asset.h"

enum
{
    PORTABLE_TRACK_TILESET_BLOCK_BYTES = 0x1000u,
    PORTABLE_TRACK_TILESET_BLOCK_COUNT = 4u,
    PORTABLE_TRACK_TILESET_TILE_BYTES =
        PORTABLE_TRACK_TILESET_BLOCK_BYTES * PORTABLE_TRACK_TILESET_BLOCK_COUNT,
    PORTABLE_TRACK_TILESET_COMMON_SOURCE_BYTES = 0x800u,
    PORTABLE_TRACK_TILESET_COMMON_SOURCE_TILE = 0xe0u,
    PORTABLE_TRACK_TILESET_PALETTE_COUNT = 48u,
};

typedef enum
{
    PORTABLE_TRACK_TILESET_OK = 0,
    PORTABLE_TRACK_TILESET_INVALID_ARGUMENT,
    PORTABLE_TRACK_TILESET_OUT_OF_BOUNDS,
    PORTABLE_TRACK_TILESET_CAPACITY,
    PORTABLE_TRACK_TILESET_FORMAT,
    PORTABLE_TRACK_TILESET_UNSUPPORTED,
} PortableTrackTilesetStatus;

typedef struct
{
    uint8_t *tiles;
    size_t tile_capacity;
    uint16_t *palette;
    size_t palette_capacity;
} PortableTrackTilesetBuffers;

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    uint32_t table_offset;
    uint32_t palette_offset;
    uint8_t flags;
    uint8_t block_count;
} PortableTrackTilesetView;

/* Decode the source split tileset selected by the imported course blob. */
PortableTrackTilesetStatus portable_track_tileset_asset_build(
    const PortableTrackAsset *asset,
    const PortableTrackTilesetBuffers *buffers,
    PortableTrackTilesetView *out_view);

/* Load the shared _08076C1C block into the source VRAM tile window at
 * 0x0600B800 (tile IDs 0xe0..0xff). */
PortableTrackTilesetStatus portable_track_tileset_asset_load_common(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t source_offset,
    uint8_t *tiles,
    size_t tile_capacity);

#endif
