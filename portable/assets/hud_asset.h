#ifndef MKSC_PORTABLE_HUD_ASSET_H
#define MKSC_PORTABLE_HUD_ASSET_H

#include "asset_pack.h"
#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_HUD_ASSET_OK = 0,
    PORTABLE_HUD_ASSET_INVALID_ARGUMENT = 1,
    PORTABLE_HUD_ASSET_OUT_OF_BOUNDS = 2,
    PORTABLE_HUD_ASSET_CAPACITY = 3,
    PORTABLE_HUD_CHAR_BYTES = 0x1000,
    PORTABLE_HUD_PALETTE_COUNT = 96,
    PORTABLE_HUD_DIGIT_COUNT = 20,
    PORTABLE_HUD_CLOCK_STEP_COUNT = 60,
    PORTABLE_HUD_LEFT_CELL_COUNT = 6,
    PORTABLE_HUD_SLASH_CELL_COUNT = 2,
    PORTABLE_HUD_RIGHT_CELL_COUNT = 6,
    /* sub_804CD98 selects source icon DMA pages 0..13. Logical pages 15 and
       16 reuse physical pages 11 and 10 with different palette selectors. */
    PORTABLE_HUD_ITEM_PAGE_COUNT = 14,
    PORTABLE_HUD_ITEM_PAGE_BYTES = 0x80,
    PORTABLE_HUD_ITEM_TILE_BYTES =
        PORTABLE_HUD_ITEM_PAGE_COUNT * PORTABLE_HUD_ITEM_PAGE_BYTES,
    PORTABLE_HUD_ITEM_CELL_COUNT = 16,
    PORTABLE_HUD_ITEM_PALETTE_COUNT = 17,
};

typedef struct
{
    uint8_t *tiles;
    size_t tile_capacity;
    uint16_t *palette;
    size_t palette_capacity;
    uint16_t *digit_tiles;
    size_t digit_capacity;
    uint8_t *clock_steps;
    size_t clock_capacity;
    uint16_t *left_cells;
    size_t left_capacity;
    uint16_t *slash_cells;
    size_t slash_capacity;
    uint16_t *right_cells;
    size_t right_capacity;
    uint8_t *item_tiles;
    size_t item_tile_capacity;
    uint16_t *item_cells;
    size_t item_cell_capacity;
    uint16_t *item_palette_selectors;
    size_t item_palette_capacity;
} PortableHudAssetBuffers;

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint16_t *digit_tiles;
    size_t digit_count;
    const uint8_t *clock_steps;
    size_t clock_step_count;
    const uint16_t *left_cells;
    size_t left_cell_count;
    const uint16_t *slash_cells;
    size_t slash_cell_count;
    const uint16_t *right_cells;
    size_t right_cell_count;
    const uint8_t *item_tiles;
    size_t item_tile_bytes;
    const uint16_t *item_cells;
    size_t item_cell_count;
    const uint16_t *item_palette_selectors;
    size_t item_palette_count;
    uint32_t tile_source_offset;
    uint32_t palette_source_offset;
    uint32_t digit_source_offset;
    uint32_t clock_source_offset;
    uint32_t left_source_offset;
    uint32_t slash_source_offset;
    uint32_t right_source_offset;
    uint32_t item_tile_source_offset;
    uint32_t item_map_source_offset;
} PortableHudAssetView;

/* Import the exact HUD character, palette and map snippets from the ROM. */
int portable_hud_asset_build(const uint8_t *data,
                             size_t data_size,
                             uint32_t tile_source_offset,
                             uint32_t palette_source_offset,
                             uint32_t digit_source_offset,
                             uint32_t clock_source_offset,
                             uint32_t left_source_offset,
                             uint32_t slash_source_offset,
                             uint32_t right_source_offset,
                             uint32_t item_tile_source_offset,
                             uint32_t item_map_source_offset,
                             uint32_t item_palette_source_offset,
                             const PortableHudAssetBuffers *buffers,
                             PortableHudAssetView *out);

/* Decode normalized HUD1/HUD2 into caller buffers; returns 1 on success.
 * View publication is transactional; scratch buffers may change on failure. */
int portable_hud_asset_open(const PortableAssetSectionView *,const PortableHudAssetBuffers *,PortableHudAssetView *);

#endif
