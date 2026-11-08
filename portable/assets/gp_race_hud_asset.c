#include "gp_race_hud_asset.h"

enum { HEADER_BYTES = 16, MAGIC = 0x31484747 };

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

int portable_gp_race_hud_asset_open(
    const PortableAssetSectionView *section,
    PortableGpRaceHudAssetView *out)
{
    const size_t map_bytes = PORTABLE_GP_RACE_HUD_RANK_MAP_COUNT *
        PORTABLE_GP_RACE_HUD_RANK_MAP_CELLS * 2u;
    const size_t expected = HEADER_BYTES + 8u +
        PORTABLE_GP_RACE_HUD_PORTRAIT_BYTES +
        PORTABLE_GP_RACE_HUD_COURSE_BYTES + map_bytes +
        PORTABLE_GP_RACE_HUD_RANK_COLOR_COUNT * 2u;
    if (section == NULL || out == NULL || section->bytes == NULL ||
        section->kind != PORTABLE_ASSET_SECTION_GP_RACE_HUD_BG0 ||
        (section->version < 2u || section->version > 5u) ||
        section->size != expected+(section->version>=3u?PORTABLE_GP_RACE_HUD_ANIMATION_BYTES:0u)+
            (section->version>=4u?PORTABLE_GP_RACE_HUD_FINISH_BYTES:0u)+
            (section->version>=5u?4u:0u))
        return 0;
    const uint8_t *p = section->bytes;
    if (le32(p) != MAGIC || le32(p + 4u) != section->content_id ||
        le32(p + 8u) != PORTABLE_GP_RACE_HUD_RANK_MAP_COUNT ||
        le32(p + 12u) != PORTABLE_GP_RACE_HUD_RANK_MAP_CELLS)
        return 0;
    PortableGpRaceHudAssetView view = {0};
    view.course_id = section->content_id;
    view.minimap_shift=21u;
    if(section->version>=5u){
        const uint32_t shift=le32(p+section->size-4u);
        if(shift!=20u&&shift!=21u)return 0;
        view.minimap_shift=(uint8_t)shift;
    }
    view.kind_pages = p + HEADER_BYTES;
    for (size_t index = 0u; index < 8u; ++index)
        if (view.kind_pages[index] >= 8u) return 0;
    view.portrait_tiles = view.kind_pages + 8u;
    view.course_tiles = view.portrait_tiles + PORTABLE_GP_RACE_HUD_PORTRAIT_BYTES;
    view.rank_maps = view.course_tiles + PORTABLE_GP_RACE_HUD_COURSE_BYTES;
    view.rank_colors = view.rank_maps + map_bytes;
    if(section->version>=3u){
        view.rank_transition_maps=p+expected;
        view.rank_flash_colors=view.rank_transition_maps+4u*52u*2u;
        view.rank_separator_tile=view.rank_flash_colors+16u;
        view.coin_hud_colors=view.rank_separator_tile+32u;
        view.rank_separator_colors=view.coin_hud_colors+18u;
    }
    if(section->version>=4u){
        view.finish_frames=p+expected+PORTABLE_GP_RACE_HUD_ANIMATION_BYTES;
        view.finish_portrait_patterns=view.finish_frames+8u*36u;
        for(unsigned i=0u;i<8u;++i){
            if(le32(view.finish_frames+i*36u)!=UINT32_C(0x00040004)||
               le32(view.finish_portrait_patterns+i*12u)!=UINT32_C(0x00020002))return 0;
        }
    }
    *out = view;
    return 1;
}
