#ifndef PORTABLE_GP_RANK_HUD_H
#define PORTABLE_GP_RANK_HUD_H

#include <stdint.h>

enum { PORTABLE_GP_RANK_HUD_CELLS = 52,
       PORTABLE_GP_RANK_GLYPH_BYTES = 352,
       PORTABLE_GP_RANK_HUD_ROM_BYTES = 4 * 52 * 2 + 8 * 352 + 16 + 32 };

typedef struct {
    uint16_t pending_cells[PORTABLE_GP_RANK_HUD_CELLS];
    uint16_t displayed_cells[PORTABLE_GP_RANK_HUD_CELLS];
    uint16_t pending_colors[4], displayed_colors[4];
    uint16_t pending_separator_colors[2],displayed_separator_colors[2];
    uint8_t portrait_cache[4];
    uint8_t pending_rank, displayed_rank;
    int32_t badge_delay;
    int16_t color_timer;
    uint8_t initialized;
} PortableGpRankHud;

/* Stable eight-racer launch HUD, before the first racing turn. ROM data
 * remains byte-addressed; no original pointers enter native state. */
int portable_gp_rank_hud_init(PortableGpRankHud *state,
    const uint8_t stable_maps[5 * 52 * 2], const uint8_t kind_pages[8],
    const uint8_t rank_colors[8 * 8], uint8_t rank,
    const uint8_t top_four_classes[4]);

/* One pre-motion HUD turn. Display latches first, then DBF4 portraits,
 * DF08 map delay/overlay and E144 glyph/color selection advance. */
int portable_gp_rank_hud_step(PortableGpRankHud *state,
    const uint8_t stable_maps[5 * 52 * 2],
    const uint8_t transition_maps[4 * 52 * 2],
    const uint8_t kind_pages[8], const uint8_t rank_colors[8 * 8],
    const uint8_t flash_colors[16], uint8_t rank,
    const uint8_t top_four_classes[4], uint32_t centiseconds,
    uint8_t clock_flags);

void portable_gp_rank_hud_separator_step(PortableGpRankHud *state,
    uint32_t clock,const uint8_t colors[8]);

#endif
