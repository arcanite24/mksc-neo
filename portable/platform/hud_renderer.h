#ifndef MKSC_PORTABLE_HUD_RENDERER_H
#define MKSC_PORTABLE_HUD_RENDERER_H

#include <stdint.h>

#include "hud_asset.h"
#include "../app/gp_item_hud.h"
#include "gp_race_hud_asset.h"
#include "minimap_asset.h"
#include "software_frame.h"
#include "native_image.h"
#include "../core/gp_finish_portraits.h"

enum
{
    PORTABLE_HUD_RENDER_OK = 0,
    PORTABLE_HUD_RENDER_INVALID_ARGUMENT = 1,
};

typedef struct
{
    uint8_t lap;
    uint8_t lap_count;
    uint8_t position;
    uint8_t checkpoint;
    uint32_t elapsed_frames;
    uint16_t heading;
    uint8_t show_time_trial_items;
    uint8_t mushrooms_remaining;
    uint8_t show_gp_item;
    uint8_t show_battle; /* balloons show only as kart sprites, as in the ROM */
    uint16_t gp_item;
    /* Winning GP result +2..+9 clears the digits; +10 begins lap splits. */
    uint8_t result_clock_mode;
    uint16_t result_lap_centiseconds;
    uint32_t source_clock_centiseconds;
    uint8_t source_clock_override;
} PortableHudState;

typedef struct
{
    uint8_t physical_page;
    uint16_t palette_selector;
} PortableHudGpItemGraphic;

/* Resolve the exact sub_804CD98 item page and palette mapping. */
int portable_hud_gp_item_graphic(const PortableHudAssetView *asset,uint16_t item,
                                 PortableHudGpItemGraphic *out);

/* Draw source-positioned HUD labels. Kind11 remains a legacy asset input;
 * its old minimap interpretation is not rendered. */
int portable_hud_render(const PortableMinimapAssetView *minimap,
                        const PortableHudState *state,
                        PortableSoftwareFrame *frame);

/* Draw the source tile/palette HUD when the exact local assets are available. */
int portable_hud_render_with_asset(const PortableMinimapAssetView *minimap,
                                   const PortableHudAssetView *hud_asset,
                                   const PortableHudState *state,
                                   PortableSoftwareFrame *frame);
int portable_hud_render_without_minimap_with_asset(
    const PortableMinimapAssetView *minimap,
    const PortableHudAssetView *hud_asset,
    const PortableHudState *state,
    PortableSoftwareFrame *frame);

/* Source speedometer: immutable art and source Q15 sine samples. */
int portable_hud_render_speedometer(const PortableNativeImage *dial,
    const PortableNativeImage *needle,int32_t speed_q16,const int32_t *sine,
    size_t sine_count,PortableSoftwareFrame *frame);
int portable_hud_render_course_map(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,PortableSoftwareFrame *frame);

/* A top_four_classes entry whose portrait the caller draws itself. */
#define PORTABLE_HUD_PORTRAIT_OVERRIDDEN 0xffu
/* Top-left of the 16x16 portrait for standings row rank (0..3), native pixels. */
#define PORTABLE_HUD_PORTRAIT_X 8
#define PORTABLE_HUD_PORTRAIT_Y(rank) ((3+(int)(rank)*3)*8)
/* Draw the source GP BG0 ranking strip and course picture from live rank. */
int portable_hud_render_gp_race_bg0(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    uint8_t player_rank,
    const uint8_t top_four_classes[4],
    uint32_t result_tick,
    PortableSoftwareFrame *frame);

/* Current standings and coin count; reads only the presentation snapshot. */
/* Coin OBJ remains visible when the lap board clears the BG HUD map. */
/* Immutable live owner output; data stays in the fixed-tick session. */
int portable_hud_render_gp_race_owned(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,uint8_t rank,const uint8_t classes[4],
    uint32_t ticks,const uint16_t rank_cells[52],const uint16_t rank_colors[4],
    const uint16_t coin_cells[3],const uint16_t coin_colors[3],
    const uint16_t separator_colors[2],PortableSoftwareFrame *frame);

int portable_hud_render_gp_coins(const PortableHudAssetView *hud,uint8_t coins,
    PortableSoftwareFrame *frame);

/* 1D3D0 draws rivals, then the selected blinking border at highest priority. */
/* Quick2009C uses the same imported cells, but never draws selected last. */
int portable_hud_render_quick_finish_portraits(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,const PortableGpFinishPortraits *state,
    const PortableNativeImage *const own_portraits[8],PortableSoftwareFrame *frame);
int portable_hud_render_gp_finish_portraits(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,const PortableGpFinishPortraits *state,
    const PortableNativeImage *const own_portraits[8],uint32_t clock,
    PortableSoftwareFrame *frame);

/* EDAC restores three HUD bands; transparency is an explicit scratch marker,
 * since ordinary native HUD pixels carry RGB without an alpha byte. */
int portable_hud_render_entry_bands(const PortableSoftwareFrame *hud,uint8_t progress,
    uint32_t transparent,PortableSoftwareFrame *frame);

int portable_hud_render_gp_race(
    const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud,
    uint8_t player_rank, const uint8_t top_four_classes[4], uint8_t coins,
    uint32_t ticks, PortableSoftwareFrame *frame);

/* Source BG0 course picture enters from the right at +1339..+1342. */
int portable_hud_render_gp_course_map_entrance(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    uint32_t intro_frame,
    PortableSoftwareFrame *frame);

int portable_hud_render_gp_top_entrance(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    const uint8_t *top_tiles,
    uint32_t intro_frame,
    PortableSoftwareFrame *frame);

int portable_hud_render_gp_live_entrance(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    const uint8_t *top_tiles,
    const uint8_t *badge_tiles,
    uint8_t player_rank,
    const uint8_t top_four_classes[4],
    uint32_t intro_frame,
    uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame);

/* Immutable preceding HUD map/glyph/colors; no animation advances here. */
int portable_hud_render_gp_live_latched(
    const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud,
    const uint8_t *top_tiles, const uint8_t badge_tiles[352],
    const uint16_t rank_cells[52], const uint16_t rank_colors[4],
    const uint8_t separator_tile[32],
    uint32_t intro_frame, uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame);

/* Coin map and bank-B colors from the preceding source upload. */
int portable_hud_render_gp_live_coin_latched(
    const PortableGpRaceHudAssetView *gp,const PortableHudAssetView *hud,
    const uint8_t *top_tiles,const uint8_t badge_tiles[352],
    const uint16_t rank_cells[52],const uint16_t rank_colors[4],
    const uint8_t separator_tile[32],
    const uint16_t coin_cells[3],const uint16_t coin_colors[3],
    uint32_t intro_frame,uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame);

/* Bounded2749 live HUD continuation; original wrapper remains2629. */
int portable_hud_render_gp_live_coin_hit_continuation(
    const PortableGpRaceHudAssetView *gp,const PortableHudAssetView *hud,
    const uint8_t *top_tiles,const uint8_t badge_tiles[352],
    const uint16_t rank_cells[52],const uint16_t rank_colors[4],
    const uint8_t separator_tile[32],
    const uint16_t coin_cells[3],const uint16_t coin_colors[3],
    uint32_t intro_frame,uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame);

/* Preceding804E4CC map/glyph, using the displayed native rank palette. */
int portable_hud_render_gp_turn_cue(const PortableHudAssetView *hud,
    const uint16_t cells[12],const uint8_t tiles[384],
    const uint16_t rank_colors[4],PortableSoftwareFrame *frame);

#endif
