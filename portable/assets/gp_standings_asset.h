#ifndef PORTABLE_GP_STANDINGS_ASSET_H
#define PORTABLE_GP_STANDINGS_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "native_image.h"

#define PORTABLE_GP_STANDINGS_TILE_BYTES 0x3000u
#define PORTABLE_GP_STANDINGS_PALETTE_COUNT 256u
#define PORTABLE_GP_STANDINGS_MAP_COUNT 1024u
#define PORTABLE_GP_STANDINGS_DRIVER_COUNT 8u
#define PORTABLE_GP_STANDINGS_DRIVER_CELLS 18u
#define PORTABLE_GP_STANDINGS_FOOTER_CELLS 9u

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint16_t *background_map;
    size_t background_map_count;
    const uint16_t *driver_cells;
    size_t driver_count;
    size_t driver_cell_count;
    const uint16_t *footer_cells;
    size_t footer_cell_count;
    uint32_t course_id;
    uint8_t source_header_index;
    /* Hosts may set a roster driver's own 16x16 portrait and board name (at
     * most 40x16) by racer index; they replace the class's portrait cells
     * (row cells 1..2) and name cells (4..8). */
    const PortableNativeImage *racer_portraits[8];
    const PortableNativeImage *racer_names[8];
} PortableGpStandingsAssetView;

#endif
