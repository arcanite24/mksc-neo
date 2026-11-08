#include "gp_rank_hud.h"

#include <stddef.h>
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static int valid_rank(uint8_t rank, const uint8_t classes[4])
{
    if(rank >= 8u || classes == NULL) return 0;
    for(unsigned i = 0; i < 4u; ++i)
        if(classes[i] >= 8u) return 0;
    return 1;
}

static void overlay(PortableGpRankHud *s, const uint8_t *map)
{
    /* F6F4 leaves zero source cells untouched, preserving portraits. */
    for(unsigned i = 0; i < PORTABLE_GP_RANK_HUD_CELLS; ++i){
        const uint16_t cell = le16(map + i * 2u);
        if(cell != 0u) s->pending_cells[i] = cell;
    }
}

static void portraits(PortableGpRankHud *s, const uint8_t pages[8],
                      const uint8_t classes[4])
{
    for(unsigned rank = 0; rank < 4u; ++rank){
        const uint8_t kind = classes[rank];
        if(s->portrait_cache[rank] == kind) continue;
        const uint16_t first = (uint16_t)(0xd080u + pages[kind] * 4u);
        for(unsigned y = 0; y < 2u; ++y)
            for(unsigned x = 0; x < 2u; ++x)
                s->pending_cells[(1u + rank * 3u + y) * 4u + 1u + x] =
                    (uint16_t)(first + y * 2u + x);
        s->portrait_cache[rank] = kind;
    }
}

static void colors(uint16_t out[4], const uint8_t *bytes)
{
    for(unsigned i = 0; i < 4u; ++i) out[i] = le16(bytes + i * 2u);
}

int portable_gp_rank_hud_init(PortableGpRankHud *s,
    const uint8_t stable_maps[5 * 52 * 2], const uint8_t kind_pages[8],
    const uint8_t rank_colors[8 * 8], uint8_t rank,
    const uint8_t classes[4])
{
    if(s == NULL || stable_maps == NULL || kind_pages == NULL ||
       rank_colors == NULL || !valid_rank(rank, classes)) return 0;
    memset(s, 0, sizeof *s);
    memset(s->portrait_cache, 0xff, sizeof s->portrait_cache);
    overlay(s, stable_maps + (rank < 4u ? rank : 4u) * 104u);
    portraits(s, kind_pages, classes);
    colors(s->pending_colors, rank_colors + (rank == 6u ? 5u : rank) * 8u);
    memcpy(s->displayed_cells, s->pending_cells, sizeof s->pending_cells);
    memcpy(s->displayed_colors, s->pending_colors, sizeof s->pending_colors);
    s->pending_rank = s->displayed_rank = rank;
    s->badge_delay = -1;
    s->initialized = 1u;
    return 1;
}

int portable_gp_rank_hud_step(PortableGpRankHud *s,
    const uint8_t stable_maps[5 * 52 * 2],
    const uint8_t transition_maps[4 * 52 * 2],
    const uint8_t kind_pages[8], const uint8_t rank_colors[8 * 8],
    const uint8_t flash_colors[16], uint8_t rank,
    const uint8_t classes[4], uint32_t centiseconds, uint8_t clock_flags)
{
    if(s == NULL || !s->initialized || stable_maps == NULL ||
       transition_maps == NULL || kind_pages == NULL || rank_colors == NULL ||
       flash_colors == NULL || !valid_rank(rank, classes)) return 0;
    memcpy(s->displayed_cells, s->pending_cells, sizeof s->pending_cells);
    memcpy(s->displayed_colors, s->pending_colors, sizeof s->pending_colors);
    s->displayed_rank = s->pending_rank;
    portraits(s, kind_pages, classes);
    const uint8_t changed = (uint8_t)(s->pending_rank != rank);
    const uint8_t *map = NULL;
    if(changed){
        s->badge_delay = 6;
        s->color_timer = 32;
        map = rank < 4u ? transition_maps + rank * 104u : stable_maps + 416u;
    }
    if(centiseconds == 0u) s->badge_delay = 0;
    if(s->badge_delay > 0){
        if(clock_flags & 4u) --s->badge_delay;
    }else if(s->badge_delay == 0){
        --s->badge_delay;
        map = stable_maps + (rank < 4u ? rank : 4u) * 104u;
    }
    if(map != NULL) overlay(s, map);
    s->pending_rank = rank;
    if(s->color_timer > 0){
        --s->color_timer;
        colors(s->pending_colors, s->color_timer > 0 ?
            flash_colors + ((s->color_timer & 4) ? 0u : 8u) :
            rank_colors + (rank == 6u ? 5u : rank) * 8u);
    }
    return 1;
}

void portable_gp_rank_hud_separator_step(PortableGpRankHud *s,uint32_t clock,const uint8_t bytes[8])
{
    if(!s||!bytes)return;
    memcpy(s->displayed_separator_colors,s->pending_separator_colors,sizeof s->pending_separator_colors);
    if((clock&3u)==0u){
        const unsigned first=(clock&4u)?0u:4u;
        for(unsigned i=0u;i<2u;++i)s->pending_separator_colors[i]=le16(bytes+first+i*2u);
    }
}
