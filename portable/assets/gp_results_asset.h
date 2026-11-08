#ifndef PORTABLE_GP_RESULTS_ASSET_H
#define PORTABLE_GP_RESULTS_ASSET_H

#include <stddef.h>
#include <stdint.h>
#include "native_image.h"
#include "menu_palette.h"

#define PORTABLE_GP_RESULTS_TILE_BYTES 0x4000u
#define PORTABLE_GP_RESULTS_PALETTE_COUNT 256u
#define PORTABLE_GP_RESULTS_DIGIT_COUNT 20u
#define PORTABLE_GP_RESULTS_HEADER_COUNT 2u
#define PORTABLE_GP_RESULTS_HEADER_BYTES 16u
#define PORTABLE_GP_RESULTS_ROW_COUNT 4u
#define PORTABLE_GP_RESULTS_ROW_BYTES 32u
/* Revision 7: DB7C8's L1..L5 and total rows for boards of more than three laps. */
#define PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT 6u
#define PORTABLE_GP_RESULTS_MAP_COUNT 1024u
#define PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT 640u
#define PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT 384u
#define PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT 20u
#define PORTABLE_GP_RESULTS_RECORD_PATTERN_CELLS 9u
#define PORTABLE_GP_RESULTS_OBJECT_TILE_BYTES 0x800u
#define PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT 10u
#define PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT 16u
#define PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT 64u
#define PORTABLE_GP_RESULTS_RANK_COUNT 8u
#define PORTABLE_GP_RESULTS_RANK_TILE_BYTES 0x160u
#define PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT 16u
/* Temporary cell decoder shared with the next-course actor renderer. */
#define PORTABLE_GP_RESULTS_LAKITU_ART_BYTES 0x300u
#define PORTABLE_GP_RESULTS_LAKITU_FRAME_BYTES 28u

typedef struct {
    uint32_t roster_driver_id;
    const PortableNativeImage *portrait,*name;
} PortableGpResultsRecordArt;

typedef struct
{
    const uint8_t *tiles;
    size_t tile_bytes;
    const uint16_t *palette;
    size_t palette_count;
    const uint16_t *digit_tiles;
    size_t digit_count;
    const uint8_t *header_patterns;
    size_t header_count;
    const uint8_t *row_patterns;
    const uint16_t *row_pattern_bytes;
    size_t row_count;
    const uint16_t *background_map;
    size_t background_map_count;
    const uint8_t *followup_tiles;
    size_t followup_tile_bytes;
    const uint16_t *followup_palette;
    size_t followup_palette_count;
    const uint16_t *followup_map;
    size_t followup_map_count;
    const uint16_t *scroll_map;
    size_t scroll_map_count;
    const uint16_t *record_patterns;
    size_t record_pattern_count;
    const uint8_t *object_tiles;
    size_t object_tile_bytes;
    const uint16_t *object_palette;
    size_t object_palette_count;
    const uint16_t *menu_palette;
    size_t menu_palette_count;
    const uint16_t *menu_pulse_base_palette;
    size_t menu_pulse_base_palette_count;
    const uint16_t *menu_pulse_factors;
    size_t menu_pulse_factor_count;
    const uint8_t *rank_tiles;
    size_t rank_tile_bytes;
    const uint16_t *rank_palette;
    size_t rank_palette_count;
    const uint8_t *compact_row_patterns;
    const uint16_t *compact_row_pattern_bytes;
    size_t compact_row_count;
    /* sub_8020770's 9B4CC time board map (Quick Run and VS results). */
    const uint16_t *time_board_map;
    size_t time_board_map_count;
    const uint8_t *lakitu_art;
    size_t lakitu_art_count;
    const uint16_t *lakitu_palette;
    /* Optional borrowed native-roster art, independent fallback for each part. */
    const uint8_t *menu_option_codes; /* Five option-to-action codes; NULL in v7. */
    const uint8_t *record_identities; /* Eight class-to-record-art identities; NULL in v7. */
    PortableGpResultsRecordArt record_art[6];
} PortableGpResultsAssetView;

static inline PortableMenuPaletteProfile portable_gp_results_menu_profile(const PortableGpResultsAssetView *asset)
{
    if(!asset||asset->menu_palette_count!=16u||asset->menu_pulse_base_palette_count!=16u||
       asset->menu_pulse_factor_count!=64u)return (PortableMenuPaletteProfile){0};
    return (PortableMenuPaletteProfile){asset->menu_pulse_base_palette,asset->menu_palette,
        asset->menu_pulse_factors};
}

#endif
