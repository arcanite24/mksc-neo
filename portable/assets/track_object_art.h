#ifndef MKSC_PORTABLE_TRACK_OBJECT_ART_H
#define MKSC_PORTABLE_TRACK_OBJECT_ART_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_TRACK_OBJECT_ART_DESCRIPTOR_OFFSET = 0x0efe88u,
    PORTABLE_TRACK_OBJECT_ART_CELL_OFFSET = 0x07ba38u,
    PORTABLE_TRACK_OBJECT_ART_PALETTE_OFFSET = 0x0af2c0u,
    PORTABLE_TRACK_OBJECT_ART_PRIMARY_STREAM_OFFSET = 0x0af320u,
    PORTABLE_TRACK_OBJECT_ART_SECONDARY_STREAM_OFFSET = 0x0c2fa8u,
    PORTABLE_TRACK_OBJECT_ART_PRIMARY_TILE_BYTES = 0x0c00u,
    PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BYTES = 0x1000u,
    PORTABLE_TRACK_OBJECT_ART_SECONDARY_TILE_BASE = 0x1000u,
    PORTABLE_TRACK_OBJECT_ART_TILE_BYTES = 0x2000u,
    PORTABLE_TRACK_OBJECT_ART_PALETTE_COUNT = 32u,
};

typedef enum
{
    PORTABLE_TRACK_OBJECT_ART_OK = 0,
    PORTABLE_TRACK_OBJECT_ART_INVALID_ARGUMENT,
    PORTABLE_TRACK_OBJECT_ART_OUT_OF_BOUNDS,
    PORTABLE_TRACK_OBJECT_ART_CAPACITY,
    PORTABLE_TRACK_OBJECT_ART_FORMAT,
} PortableTrackObjectArtStatus;

typedef struct
{
    uint8_t *tiles;
    size_t tile_capacity;
    uint16_t *palette;
    size_t palette_capacity;
} PortableTrackObjectArtBuffers;

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint8_t *cell_data;
    size_t cell_data_bytes;
    uint32_t descriptor_offset;
    uint32_t cell_offset;
    uint32_t primary_stream_offset;
    uint32_t secondary_stream_offset;
    uint32_t primary_decoded_bytes;
    uint32_t secondary_decoded_bytes;
    uint32_t level0_cell_offset;
    uint32_t level1_cell_offset;
    uint32_t level2_cell_offset;
    uint32_t level3_cell_offset;
    uint8_t level0_shift;
    uint8_t level1_shift;
    uint8_t level2_shift;
    uint8_t level3_shift;
} PortableTrackObjectArtView;

/* Recover the source generic obstacle's OAM descriptor, cell data, graphics,
 * and the two object palette banks from caller-owned ROM bytes. */
PortableTrackObjectArtStatus portable_track_object_art_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableTrackObjectArtBuffers *buffers,
    PortableTrackObjectArtView *out_view);

#endif
