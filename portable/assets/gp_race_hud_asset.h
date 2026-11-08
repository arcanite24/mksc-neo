#ifndef MKSC_PORTABLE_GP_RACE_HUD_ASSET_H
#define MKSC_PORTABLE_GP_RACE_HUD_ASSET_H

#include "asset_pack.h"

#include <stddef.h>
#include <stdint.h>

enum {
    PORTABLE_GP_RACE_HUD_PORTRAIT_BYTES = 0x400,
    PORTABLE_GP_RACE_HUD_COURSE_BYTES = 0x800,
    PORTABLE_GP_RACE_HUD_RANK_MAP_COUNT = 5,
    PORTABLE_GP_RACE_HUD_RANK_MAP_CELLS = 4 * 13,
    PORTABLE_GP_RACE_HUD_RANK_COLOR_COUNT = 8 * 4,
    PORTABLE_GP_RACE_HUD_ANIMATION_BYTES = 490,
    PORTABLE_GP_RACE_HUD_FINISH_BYTES = 384,
};

typedef struct {
    uint32_t course_id;
    uint8_t minimap_shift; /* Source32D0C,20 or21; legacy0 means21. */
    const uint8_t *kind_pages;
    const uint8_t *portrait_tiles;
    const uint8_t *course_tiles;
    const uint8_t *rank_maps;
    /* Four colors for each rank, decoded from the ROM palette table. */
    const uint8_t *rank_colors;
    /* Optional v3 source animation data; LE16 maps/colors, raw4bpp tile. */
    const uint8_t *rank_transition_maps;
    const uint8_t *rank_flash_colors;
    const uint8_t *rank_separator_tile;
    const uint8_t *coin_hud_colors;
    const uint8_t *rank_separator_colors;
    /* Optional v4 1D3D0 frame patterns (8x36) and portraits (8x12). */
    const uint8_t *finish_frames;
    const uint8_t *finish_portrait_patterns;
} PortableGpRaceHudAssetView;

int portable_gp_race_hud_asset_open(
    const PortableAssetSectionView *section,
    PortableGpRaceHudAssetView *out);

#endif
