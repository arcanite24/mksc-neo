#include "presentation_pack.h"

#include <string.h>

#define ROAD_MAGIC UINT32_C(0x31444152)
#define MINIMAP_MAGIC UINT32_C(0x314e494d)
#define LIGHTNING_EFFECT_MAGIC UINT32_C(0x3146584c)
#define GP_FINISH_BANNER_MAGIC UINT32_C(0x31424647)
#define GP_RESULTS_MAGIC UINT32_C(0x31525047)
#define GP_STANDINGS_MAGIC UINT32_C(0x31534750)
#define SCENERY_MAGIC UINT32_C(0x314e4353)
#define GP_MINIMAP_MARKER_MAGIC UINT32_C(0x314b4d47)

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static int find(const PortableAssetPackView *p, uint32_t k, uint32_t id,
                PortableAssetSectionView *s)
{
    return portable_asset_pack_find_section(p, k, id, s) == PORTABLE_ASSET_PACK_OK;
}
static int read16s(const uint8_t **p, const uint8_t *end, uint16_t *out, size_t n)
{
    if ((size_t)(end - *p) < n * 2u)
        return 0;
    for (size_t i = 0; i < n; ++i)
        out[i] = u16(*p + i * 2u);
    *p += n * 2u;
    return 1;
}

static int road(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                PortableTrackTilesetView *v)
{
    const uint8_t *p = s->bytes, *end = p + s->size;
    uint32_t tn, pn;
    if (s->version != 1 || s->size < 24 || u32(p) != ROAD_MAGIC)
        return 0;
    tn = u32(p + 4);
    pn = u32(p + 8);
    if (tn > sizeof st->road_tiles || pn > 64 || s->size != 24u + tn + pn * 2u)
        return 0;
    memcpy(st->road_tiles, p + 24, tn);
    p += 24 + tn;
    if (!read16s(&p, end, st->road_palette, pn) || p != end)
        return 0;
    *v = (PortableTrackTilesetView){st->road_tiles,
                                    tn,
                                    st->road_palette,
                                    pn,
                                    0,
                                    0,
                                    (uint8_t)u32(s->bytes + 12),
                                    (uint8_t)u32(s->bytes + 16)};
    return 1;
}

static int sky(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
               PortableSkyAssetView *v)
{
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    uint32_t tn, pn, fn, mn, bn;
    const PortableSkyMotionRules *motion_rules = NULL;
    if (portable_sky_asset_open_motion(s, &st->sky_motion_rules, &motion_rules) !=
        PORTABLE_SKY_ASSET_OK)
        return 0;
    tn = u32(b + 4);
    pn = u32(b + 8);
    fn = u32(b + 12);
    mn = u32(b + 16);
    bn = u32(b + 20);
    if (tn > sizeof st->sky_tiles || pn > 64 || fn > sizeof st->sky_front ||
        mn > sizeof st->sky_mid || bn > sizeof st->sky_back ||
        s->size != 64u + tn + pn * 2u + fn + mn + bn + u32(b + 60u))
        return 0;
    p = b + 64;
    memcpy(st->sky_tiles, p, tn);
    p += tn;
    if (!read16s(&p, end, st->sky_palette, pn))
        return 0;
    memcpy(st->sky_front, p, fn);
    p += fn;
    memcpy(st->sky_mid, p, mn);
    p += mn;
    memcpy(st->sky_back, p, bn);
    p += bn;
    if ((size_t)(end - p) != u32(b + 60u))
        return 0;
    *v = (PortableSkyAssetView){
        st->sky_tiles,         tn,          st->sky_palette,      pn,
        st->sky_front,         fn,          st->sky_mid,          mn,
        st->sky_back,          bn,          u32(b + 24),          u32(b + 28),
        u32(b + 32),           u32(b + 36), (uint8_t)u32(b + 40), (uint8_t)u32(b + 44),
        (uint16_t)u32(b + 48), 0,           u32(b + 56),          u32(b + 52),
        motion_rules};
    return 1;
}

static int character(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                     PortableCharacterAssetView *v)
{
    return portable_character_asset_open_packed(s, &st->character_storage, v) ==
           PORTABLE_CHARACTER_ASSET_OK;
}


static int lightning_effect(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                            PortableLightningEffectAssetView *v)
{
    const uint8_t *b = s->bytes;
    uint32_t tn, fn, tile;
    if (s->version != 1 || s->size < 16u || u32(b) != LIGHTNING_EFFECT_MAGIC)
        return 0;
    tn = u32(b + 4);
    fn = u32(b + 8);
    tile = u32(b + 12);
    if (tn != sizeof st->lightning_effect_tiles || fn != PORTABLE_LIGHTNING_EFFECT_FRAME_COUNT ||
        tile != 0x1a0u || s->size != 16u + tn)
        return 0;
    memcpy(st->lightning_effect_tiles, b + 16u, tn);
    *v = (PortableLightningEffectAssetView){st->lightning_effect_tiles, tn, fn, (uint16_t)tile};
    return 1;
}

static int item_box(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                    PortableItemBoxAssetView *v)
{
    return portable_item_box_asset_open(s, st->item_box_tiles, sizeof st->item_box_tiles,
                                        st->item_box_palette, PORTABLE_ITEM_BOX_PALETTE_COUNT, v);
}

static int scenery(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                   PortableCourseSceneryView *v)
{
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    const uint32_t count = s->size >= 16u ? u32(b + 4u) : 0u;
    const uint32_t tn = s->size >= 16u ? u32(b + 8u) : 0u;
    const uint32_t pn = s->size >= 16u ? u32(b + 12u) : 0u;
    if (s->version == 2u || s->version == 3u || s->version == 4u)
    {
        const size_t stride = s->version == 4u ? 20u : s->version == 3u ? 16u : 12u;
        if (s->size < 16u || u32(b) != SCENERY_MAGIC || pn != 0u ||
            count > PORTABLE_COURSE_SCENERY_MAX_OBJECTS || tn > PORTABLE_COURSE_SCENERY_MAX_ART ||
            s->size < 16u + (size_t)count * stride)
            return 0;
        p = b + 16u;
        for (uint32_t i = 0u; i < count; ++i, p += stride)
        {
            if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u || p[9] >= tn ||
                (s->version >= 3u && (u32(p + 12u) >> 16u) == 0xa0u && (uint32_t)p[9] + 1u >= tn) ||
                (s->version >= 3u && (u32(p + 12u) >> 16u) == 0xa1u && (uint32_t)p[9] + 3u >= tn) ||
                (s->version >= 3u && u32(p + 12u) == 0x860000u && (uint32_t)p[9] + 1u >= tn) ||
                (s->version == 2u && u16(p + 10u) != 0u) ||
                (s->version >= 3u && (u16(p + 10u) == 0u || u16(p + 10u) > 256u)) ||
                (s->version == 4u && (p[16] >= 64u || p[17] || u16(p + 18u))))
                return 0;
            st->scenery_objects[i] =
                (PortableCourseSceneryObject){.position_x_q16 = (int32_t)u32(p),
                                              .position_y_q16 = (int32_t)u32(p + 4u),
                                              .source_parameter = p[8],
                                              .art_index = p[9],
                                              .affine_q8 = s->version >= 3u ? u16(p + 10u) : 256u,
                                              .source_code = s->version >= 3u ? u32(p + 12u) : 0u};
        }
        for (uint32_t i = 0u; i < tn; ++i)
        {
            for (unsigned lod = 0u; lod < 4u; ++lod)
            {
                if ((size_t)(end - p) < 8u)
                    return 0;
                const uint32_t shift = u32(p), size = u32(p + 4u);
                p += 8u;
                PortableNativeImage *image = &st->scenery_art[i].lod[lod];
                if (shift > 3u || size > (size_t)(end - p) ||
                    !portable_native_image_open(p, size, image) || image->width > 128u ||
                    image->height > 128u || image->anchor_x < -128 || image->anchor_x > 128 ||
                    image->anchor_y < -128 || image->anchor_y > 128)
                    return 0;
                st->scenery_art[i].distance_shift[lod] = (uint8_t)shift;
                p += size;
            }
        }
        if (p != end)
            return 0;
        *v = (PortableCourseSceneryView){.objects = st->scenery_objects,
                                         .object_count = count,
                                         .native_art = st->scenery_art,
                                         .native_art_count = tn};
        return 1;
    }
    if (s->version != 1u || s->size < 16u || u32(b) != SCENERY_MAGIC ||
        count > PORTABLE_COURSE_SCENERY_MAX_OBJECTS ||
        (tn != PORTABLE_COURSE_SCENERY_LOD16_TILE_BYTES &&
         tn != PORTABLE_COURSE_SCENERY_TILE_BYTES - PORTABLE_COURSE_SCENERY_LOD8_TILE_BYTES &&
         tn != PORTABLE_COURSE_SCENERY_TILE_BYTES &&
         tn != PORTABLE_COURSE_SCENERY_ALL_TILE_BYTES) ||
        pn != PORTABLE_COURSE_SCENERY_PALETTE_COUNT ||
        s->size != 16u + (size_t)count * 4u + tn + pn * 2u)
        return 0;
    p = b + 16u;
    for (uint32_t i = 0u; i < count; ++i, p += 4u)
    {
        if (p[0] != 2u)
            return 0;
        st->scenery_objects[i] = (PortableCourseSceneryObject){
            .position_x_q16 = (int32_t)(((uint32_t)p[1] * 8u + 4u) << 16u),
            .position_y_q16 = (int32_t)(((uint32_t)p[2] * 8u + 4u) << 16u),
            .source_parameter = p[3]};
    }
    memcpy(st->scenery_tiles, p, tn);
    p += tn;
    if (!read16s(&p, end, st->scenery_palette, pn) || p != end)
        return 0;
    const size_t base = tn == PORTABLE_COURSE_SCENERY_ALL_TILE_BYTES
                            ? PORTABLE_COURSE_SCENERY_LOD64_TILE_BYTES
                            : 0u;
    *v = (PortableCourseSceneryView){
        st->scenery_objects,
        count,
        st->scenery_tiles + base +
            (tn >= PORTABLE_COURSE_SCENERY_LOD32_TILE_BYTES +
                         PORTABLE_COURSE_SCENERY_LOD16_TILE_BYTES
                 ? PORTABLE_COURSE_SCENERY_LOD32_TILE_BYTES
                 : 0u),
        st->scenery_palette,
        tn >= PORTABLE_COURSE_SCENERY_LOD32_TILE_BYTES + PORTABLE_COURSE_SCENERY_LOD16_TILE_BYTES
            ? st->scenery_tiles + base
            : NULL,
        tn >= PORTABLE_COURSE_SCENERY_TILE_BYTES
            ? st->scenery_tiles + base + PORTABLE_COURSE_SCENERY_LOD32_TILE_BYTES +
                  PORTABLE_COURSE_SCENERY_LOD16_TILE_BYTES
            : NULL,
        base ? st->scenery_tiles : NULL,
        NULL,
        NULL,
        0u};
    return 1;
}

static int gp_minimap_markers(const PortableAssetSectionView *s,
                              PortablePresentationPackStorage *st,
                              PortableGpMinimapMarkerAssetView *v)
{
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    const size_t palette_count = s->version == 1u ? 16u : PORTABLE_GP_MINIMAP_MARKER_PALETTE_COUNT;
    if ((s->version != 1u && s->version != 2u) ||
        s->size != 16u + PORTABLE_GP_MINIMAP_MARKER_TILE_BYTES + palette_count * 2u ||
        u32(b) != GP_MINIMAP_MARKER_MAGIC || u32(b + 4u) != PORTABLE_GP_MINIMAP_MARKER_TILE_BYTES ||
        u32(b + 8u) != palette_count || u32(b + 12u) != 0x118u)
        return 0;
    p = b + 16u;
    memcpy(st->gp_minimap_marker_tiles, p, PORTABLE_GP_MINIMAP_MARKER_TILE_BYTES);
    p += PORTABLE_GP_MINIMAP_MARKER_TILE_BYTES;
    if (!read16s(&p, end, st->gp_minimap_marker_palette, palette_count) || p != end)
        return 0;
    *v = (PortableGpMinimapMarkerAssetView){
        st->gp_minimap_marker_tiles, st->gp_minimap_marker_palette,
        s->version == 2u ? st->gp_minimap_marker_palette + 16u : st->gp_minimap_marker_palette};
    return 1;
}

static int gp_finish_banner(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                            PortableGpFinishBannerAssetView *v)
{
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    uint32_t tn, pn, cn, tile, x, y;
    if (s->version != 1 || s->size < 32u || u32(b) != GP_FINISH_BANNER_MAGIC)
        return 0;
    tn = u32(b + 4);
    pn = u32(b + 8);
    cn = u32(b + 12);
    tile = u32(b + 16);
    x = u32(b + 20);
    y = u32(b + 24);
    if (tn != sizeof st->gp_finish_banner_tiles || pn != PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT ||
        cn != sizeof st->gp_finish_banner_cells || tile != 0xa0u || x != 120u || y != 63u ||
        s->size != 32u + tn + pn * 2u + cn)
        return 0;
    p = b + 32u;
    memcpy(st->gp_finish_banner_tiles, p, tn);
    p += tn;
    if (!read16s(&p, end, st->gp_finish_banner_palette, pn))
        return 0;
    memcpy(st->gp_finish_banner_cells, p, cn);
    p += cn;
    if (p != end)
        return 0;
    *v = (PortableGpFinishBannerAssetView){st->gp_finish_banner_tiles,
                                           tn,
                                           st->gp_finish_banner_palette,
                                           pn,
                                           st->gp_finish_banner_cells,
                                           cn,
                                           (uint16_t)tile,
                                           (uint16_t)x,
                                           (uint16_t)y};
    return 1;
}

static int gp_results(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                      PortableGpResultsAssetView *v)
{
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    const uint32_t counts[] = {PORTABLE_GP_RESULTS_TILE_BYTES,
                               PORTABLE_GP_RESULTS_PALETTE_COUNT,
                               PORTABLE_GP_RESULTS_DIGIT_COUNT,
                               PORTABLE_GP_RESULTS_HEADER_COUNT,
                               PORTABLE_GP_RESULTS_ROW_COUNT,
                               PORTABLE_GP_RESULTS_MAP_COUNT,
                               PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT,
                               PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT,
                               PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT,
                               PORTABLE_GP_RESULTS_OBJECT_TILE_BYTES,
                               PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT,
                               PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,
                               PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,
                               PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT,
                               PORTABLE_GP_RESULTS_RANK_COUNT};
    const size_t expected =
        64u + sizeof st->gp_results_tiles + sizeof st->gp_results_palette +
        sizeof st->gp_results_digits + sizeof st->gp_results_headers +
        sizeof st->gp_results_row_bytes + sizeof st->gp_results_rows +
        sizeof st->gp_results_background_map + sizeof st->gp_results_followup_tiles +
        sizeof st->gp_results_followup_palette + sizeof st->gp_results_followup_map +
        sizeof st->gp_results_scroll_map + sizeof st->gp_results_record_patterns +
        sizeof st->gp_results_object_tiles + sizeof st->gp_results_object_palette +
        sizeof st->gp_results_menu_palette + sizeof st->gp_results_menu_pulse_base_palette +
        sizeof st->gp_results_menu_pulse_factors + sizeof st->gp_results_rank_tiles +
        sizeof st->gp_results_rank_palette + sizeof st->gp_results_compact_row_bytes +
        sizeof st->gp_results_compact_rows + sizeof st->gp_results_time_board_map;
    if ((s->version != 7u && s->version != 8u) ||
        s->size != expected + (s->version == 8u ? 13u : 0u) || u32(b) != GP_RESULTS_MAGIC)
        return 0;
    for (size_t i = 0u; i < sizeof counts / sizeof counts[0]; ++i)
        if (u32(b + 4u + i * 4u) != counts[i])
            return 0;
    p = b + 64u;
    memcpy(st->gp_results_tiles, p, sizeof st->gp_results_tiles);
    p += sizeof st->gp_results_tiles;
    if (!read16s(&p, end, st->gp_results_palette, PORTABLE_GP_RESULTS_PALETTE_COUNT) ||
        !read16s(&p, end, st->gp_results_digits, PORTABLE_GP_RESULTS_DIGIT_COUNT))
        return 0;
    memcpy(st->gp_results_headers, p, sizeof st->gp_results_headers);
    p += sizeof st->gp_results_headers;
    if (!read16s(&p, end, st->gp_results_row_bytes, PORTABLE_GP_RESULTS_ROW_COUNT))
        return 0;
    for (size_t i = 0u; i < PORTABLE_GP_RESULTS_ROW_COUNT; ++i)
        if (st->gp_results_row_bytes[i] > PORTABLE_GP_RESULTS_ROW_BYTES ||
            st->gp_results_row_bytes[i] < 4u)
            return 0;
    memcpy(st->gp_results_rows, p, sizeof st->gp_results_rows);
    p += sizeof st->gp_results_rows;
    if (!read16s(&p, end, st->gp_results_background_map, PORTABLE_GP_RESULTS_MAP_COUNT))
        return 0;
    memcpy(st->gp_results_followup_tiles, p, sizeof st->gp_results_followup_tiles);
    p += sizeof st->gp_results_followup_tiles;
    if (!read16s(&p, end, st->gp_results_followup_palette, PORTABLE_GP_RESULTS_PALETTE_COUNT) ||
        !read16s(&p, end, st->gp_results_followup_map, PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT) ||
        !read16s(&p, end, st->gp_results_scroll_map, PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT) ||
        !read16s(&p, end, st->gp_results_record_patterns,
                 PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT *
                     PORTABLE_GP_RESULTS_RECORD_PATTERN_CELLS))
        return 0;
    memcpy(st->gp_results_object_tiles, p, sizeof st->gp_results_object_tiles);
    p += sizeof st->gp_results_object_tiles;
    if (!read16s(&p, end, st->gp_results_object_palette,
                 PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT) ||
        !read16s(&p, end, st->gp_results_menu_palette, PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT) ||
        !read16s(&p, end, st->gp_results_menu_pulse_base_palette,
                 PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT) ||
        !read16s(&p, end, st->gp_results_menu_pulse_factors,
                 PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT))
        return 0;
    memcpy(st->gp_results_rank_tiles, p, sizeof st->gp_results_rank_tiles);
    p += sizeof st->gp_results_rank_tiles;
    if (!read16s(&p, end, st->gp_results_rank_palette, PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT) ||
        !read16s(&p, end, st->gp_results_compact_row_bytes, PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT))
        return 0;
    for (size_t i = 0u; i < PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT; ++i)
        if (st->gp_results_compact_row_bytes[i] > PORTABLE_GP_RESULTS_ROW_BYTES ||
            st->gp_results_compact_row_bytes[i] < 4u)
            return 0;
    memcpy(st->gp_results_compact_rows, p, sizeof st->gp_results_compact_rows);
    p += sizeof st->gp_results_compact_rows;
    if (!read16s(&p, end, st->gp_results_time_board_map, PORTABLE_GP_RESULTS_MAP_COUNT))
        return 0;
    if (s->version == 8u)
    {
        for (unsigned i = 0u; i < 8u; ++i)
        {
            if (p[i] > 7u)
                return 0;
            st->gp_results_record_identities[i] = p[i];
        }
        p += 8u;
        for (unsigned i = 0u; i < 5u; ++i)
        {
            if (p[i] > 4u)
                return 0;
            st->gp_results_menu_option_codes[i] = p[i];
        }
        p += 5u;
    }
    if (p != end)
        return 0;
    *v = (PortableGpResultsAssetView){
        .tiles = st->gp_results_tiles,
        .tile_bytes = sizeof st->gp_results_tiles,
        .palette = st->gp_results_palette,
        .palette_count = PORTABLE_GP_RESULTS_PALETTE_COUNT,
        .digit_tiles = st->gp_results_digits,
        .digit_count = PORTABLE_GP_RESULTS_DIGIT_COUNT,
        .header_patterns = st->gp_results_headers,
        .header_count = PORTABLE_GP_RESULTS_HEADER_COUNT,
        .row_patterns = st->gp_results_rows,
        .row_pattern_bytes = st->gp_results_row_bytes,
        .row_count = PORTABLE_GP_RESULTS_ROW_COUNT,
        .background_map = st->gp_results_background_map,
        .background_map_count = PORTABLE_GP_RESULTS_MAP_COUNT,
        .followup_tiles = st->gp_results_followup_tiles,
        .followup_tile_bytes = sizeof st->gp_results_followup_tiles,
        .followup_palette = st->gp_results_followup_palette,
        .followup_palette_count = PORTABLE_GP_RESULTS_PALETTE_COUNT,
        .followup_map = st->gp_results_followup_map,
        .followup_map_count = PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT,
        .scroll_map = st->gp_results_scroll_map,
        .scroll_map_count = PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT,
        .record_patterns = st->gp_results_record_patterns,
        .record_pattern_count = PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT,
        .object_tiles = st->gp_results_object_tiles,
        .object_tile_bytes = sizeof st->gp_results_object_tiles,
        .object_palette = st->gp_results_object_palette,
        .object_palette_count = PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT,
        .menu_palette = st->gp_results_menu_palette,
        .menu_palette_count = PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,
        .menu_pulse_base_palette = st->gp_results_menu_pulse_base_palette,
        .menu_pulse_base_palette_count = PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT,
        .menu_pulse_factors = st->gp_results_menu_pulse_factors,
        .menu_pulse_factor_count = PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT,
        .rank_tiles = st->gp_results_rank_tiles,
        .rank_tile_bytes = sizeof st->gp_results_rank_tiles,
        .rank_palette = st->gp_results_rank_palette,
        .rank_palette_count = PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT,
        .compact_row_patterns = st->gp_results_compact_rows,
        .compact_row_pattern_bytes = st->gp_results_compact_row_bytes,
        .compact_row_count = PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT,
        .time_board_map = st->gp_results_time_board_map,
        .time_board_map_count = PORTABLE_GP_RESULTS_MAP_COUNT,
        .record_identities = s->version == 8u ? st->gp_results_record_identities : NULL,
        .menu_option_codes = s->version == 8u ? st->gp_results_menu_option_codes : NULL};
    return 1;
}

static int gp_standings(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                        PortableGpStandingsAssetView *v)
{
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    uint32_t tn, pn, mn, dc, cc;
    if (s->version != 1u || s->size < 32u || u32(b) != GP_STANDINGS_MAGIC)
        return 0;
    tn = u32(b + 4);
    pn = u32(b + 8);
    mn = u32(b + 12);
    dc = u32(b + 16);
    cc = u32(b + 20);
    if (tn != sizeof st->gp_standings_tiles || pn != PORTABLE_GP_STANDINGS_PALETTE_COUNT ||
        mn != PORTABLE_GP_STANDINGS_MAP_COUNT || dc != PORTABLE_GP_STANDINGS_DRIVER_COUNT ||
        cc != PORTABLE_GP_STANDINGS_DRIVER_CELLS ||
        s->size !=
            32u + tn + pn * 2u + mn * 2u + dc * cc * 2u + PORTABLE_GP_STANDINGS_FOOTER_CELLS * 2u)
        return 0;
    p = b + 32u;
    memcpy(st->gp_standings_tiles, p, tn);
    p += tn;
    if (!read16s(&p, end, st->gp_standings_palette, pn) ||
        !read16s(&p, end, st->gp_standings_background_map, mn) ||
        !read16s(&p, end, st->gp_standings_driver_cells, dc * cc) ||
        !read16s(&p, end, st->gp_standings_footer_cells, PORTABLE_GP_STANDINGS_FOOTER_CELLS) ||
        p != end)
        return 0;
    *v = (PortableGpStandingsAssetView){st->gp_standings_tiles,
                                        tn,
                                        st->gp_standings_palette,
                                        pn,
                                        st->gp_standings_background_map,
                                        mn,
                                        st->gp_standings_driver_cells,
                                        dc,
                                        cc,
                                        st->gp_standings_footer_cells,
                                        PORTABLE_GP_STANDINGS_FOOTER_CELLS,
                                        u32(b + 24),
                                        (uint8_t)u32(b + 28),
                                        {NULL},
                                        {NULL}};
    return 1;
}

static int minimap(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
                   PortableMinimapAssetView *v)
{
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    uint32_t cn, tn;
    if (s->version != 1 || s->size < 24 || u32(b) != MINIMAP_MAGIC)
        return 0;
    cn = u32(b + 4);
    tn = u32(b + 16);
    if (cn > PORTABLE_MINIMAP_CELL_COUNT || tn > sizeof st->minimap_tiles ||
        s->size != 24u + cn * 2u + tn)
        return 0;
    p = b + 24;
    if (!read16s(&p, end, st->minimap_cells, cn))
        return 0;
    memcpy(st->minimap_tiles, p, tn);
    p += tn;
    if (p != end)
        return 0;
    *v = (PortableMinimapAssetView){st->minimap_cells, cn, u32(b + 8), u32(b + 12), 0,
                                    st->minimap_tiles, tn, 0};
    return 1;
}

static int hud(const PortableAssetSectionView *s, PortablePresentationPackStorage *st,
               PortableHudAssetView *v)
{
    const PortableHudAssetBuffers buffers = {st->hud_tiles,
                                             sizeof st->hud_tiles,
                                             st->hud_palette,
                                             PORTABLE_HUD_PALETTE_COUNT,
                                             st->hud_digits,
                                             PORTABLE_HUD_DIGIT_COUNT,
                                             st->hud_clock,
                                             PORTABLE_HUD_CLOCK_STEP_COUNT,
                                             st->hud_left,
                                             PORTABLE_HUD_LEFT_CELL_COUNT,
                                             st->hud_slash,
                                             PORTABLE_HUD_SLASH_CELL_COUNT,
                                             st->hud_right,
                                             PORTABLE_HUD_RIGHT_CELL_COUNT,
                                             st->hud_item_tiles,
                                             sizeof st->hud_item_tiles,
                                             st->hud_item_cells,
                                             PORTABLE_HUD_ITEM_CELL_COUNT,
                                             st->hud_item_palette_selectors,
                                             PORTABLE_HUD_ITEM_PALETTE_COUNT};
    return portable_hud_asset_open(s, &buffers, v);
}

static int actor_art(const PortableAssetSectionView *s, PortableCourseSceneryArt *art,
                     uint32_t magic, unsigned frames, unsigned max_extent)
{
    if (s->version != 1u || s->size < 8u || u32(s->bytes) != magic || u32(s->bytes + 4u) != frames)
        return 0;
    const uint8_t *p = s->bytes + 8u, *end = s->bytes + s->size;
    for (unsigned frame = 0u; frame < frames; ++frame)
    {
        for (unsigned lod = 0u; lod < 4u; ++lod)
        {
            if ((size_t)(end - p) < 8u)
                return 0;
            const uint32_t shift = u32(p), size = u32(p + 4u);
            p += 8u;
            PortableNativeImage *image = &art[frame].lod[lod];
            if (shift > 3u || size > (size_t)(end - p) ||
                !portable_native_image_open(p, size, image) || image->width > max_extent ||
                image->height > max_extent || image->anchor_x < -64 || image->anchor_x > 64 ||
                image->anchor_y < -64 || image->anchor_y > 64)
                return 0;
            art[frame].distance_shift[lod] = (uint8_t)shift;
            p += size;
        }
    }
    return p == end;
}

/* These effects may be absent in older packs; present payloads still need to
 * match their simulation scripts before the presentation view is published. */
static PortableRecoveredPackStatus load_optional_effect_art(const PortableAssetPackView *pack,
                                                            PortablePresentationPackStorage *st,
                                                            PortablePresentationPackView *out)
{
    PortableAssetSectionView f;
    if (find(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 2u, &f) &&
        !portable_race_particle_asset_open(
            &f, &st->mushroom_burst, PORTABLE_MUSHROOM_BURST_FRAME_COUNT,
            PORTABLE_MUSHROOM_BURST_CELL_BYTES, &out->mushroom_burst))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 3u, &f) &&
        !portable_race_particle_asset_open(&f, &st->coin_pickup, PORTABLE_COIN_PICKUP_FRAME_COUNT,
                                           PORTABLE_COIN_PICKUP_CELL_BYTES, &out->coin_pickup))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 5u, &f) &&
        !portable_race_particle_asset_open(&f, &st->lava_smoke, PORTABLE_LAVA_SMOKE_FRAME_COUNT,
                                           PORTABLE_LAVA_SMOKE_CELL_BYTES, &out->lava_smoke))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    /* Keeping an older pack remains playable; only absent splash art is optional. */
    const PortableAssetPackStatus splash_status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 6u, &f);
    if (splash_status != PORTABLE_ASSET_PACK_NOT_FOUND &&
        (splash_status != PORTABLE_ASSET_PACK_OK ||
         !portable_race_particle_asset_open(&f, &st->entry_splash,
                                            PORTABLE_ENTRY_SPLASH_FRAME_COUNT,
                                            PORTABLE_ENTRY_SPLASH_CELL_BYTES, &out->entry_splash)))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    for (unsigned effect = 0u; effect < 2u; ++effect)
    {
        const PortableAssetPackStatus status = portable_asset_pack_find_section(
            pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 7u + effect, &f);
        if (status != PORTABLE_ASSET_PACK_NOT_FOUND &&
            (status != PORTABLE_ASSET_PACK_OK ||
             !portable_race_particle_asset_open(
                 &f, effect ? &st->recovery_drip : &st->recovery_thaw,
                 effect ? PORTABLE_RECOVERY_DRIP_FRAME_COUNT : PORTABLE_RECOVERY_THAW_FRAME_COUNT,
                 effect ? PORTABLE_RECOVERY_DRIP_CELL_BYTES : PORTABLE_RECOVERY_THAW_CELL_BYTES,
                 effect ? &out->recovery_drip : &out->recovery_thaw)))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    }
    for (unsigned effect = 0u; effect < 5u; ++effect)
    {
        const PortableAssetPackStatus status = portable_asset_pack_find_section(
            pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 9u + effect, &f);
        if (status != PORTABLE_ASSET_PACK_NOT_FOUND &&
            (status != PORTABLE_ASSET_PACK_OK ||
             !portable_race_particle_asset_open(&f, &st->surface_trail_art[effect],
                                                effect == 4u ? 1u : 3u, effect == 4u ? 8u : 24u,
                                                &out->surface_trail_art[effect])))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    }
    for (unsigned effect = 0u; effect < 3u; ++effect)
    {
        const PortableAssetPackStatus status = portable_asset_pack_find_section(
            pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 14u + effect, &f);
        if (status != PORTABLE_ASSET_PACK_NOT_FOUND &&
            (status != PORTABLE_ASSET_PACK_OK ||
             !portable_race_particle_asset_open(&f, &st->coin_effect_art[effect], 0u, 0u,
                                                &out->coin_effect_art[effect])))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    }
    if (out->coin_effect_rules)
    {
        const PortableRaceParticleAssetView *scripts[4] = {
            &out->coin_pickup, &out->coin_effect_art[0], &out->coin_effect_art[1],
            &out->coin_effect_art[2]};
        for (unsigned script = 0u; script < 4u; ++script)
        {
            unsigned ticks = 0u;
            for (size_t frame = 0u; frame < scripts[script]->frame_count; ++frame)
                ticks += scripts[script]->frames[frame].duration + 1u;
            if (ticks != out->coin_effect_rules->script_ticks[script])
                return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        }
    }
    for (unsigned effect = 0u; effect < 6u; ++effect)
    {
        const PortableAssetPackStatus status = portable_asset_pack_find_section(
            pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 17u + effect, &f);
        if (status != PORTABLE_ASSET_PACK_NOT_FOUND &&
            (status != PORTABLE_ASSET_PACK_OK ||
             !portable_race_particle_asset_open(&f, &st->impact_effect_art[effect], 0u, 0u,
                                                &out->impact_effect_art[effect])))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        if (out->impact_effect_rules)
        {
            unsigned ticks = 0u;
            for (size_t row = 0u; row < out->impact_effect_art[effect].frame_count; ++row)
                ticks += out->impact_effect_art[effect].frames[row].duration + 1u;
            if (ticks != out->impact_effect_rules->script_ticks[effect])
                return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        }
    }
    if (find(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 4u, &f) &&
        (f.version != 1u || !portable_native_image_open(f.bytes, f.size, &out->battle_balloon) ||
         out->battle_balloon.width > 64u || out->battle_balloon.height > 64u ||
         out->battle_balloon.anchor_x < -64 || out->battle_balloon.anchor_x > 64 ||
         out->battle_balloon.anchor_y < -64 || out->battle_balloon.anchor_y > 64))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS, 9u, &f) &&
        portable_character_asset_open_borrowed(&f, st->battle_bomb_palette, 256u,
                                               &out->battle_bomb) != PORTABLE_CHARACTER_ASSET_OK)
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    return PORTABLE_RECOVERED_PACK_OK;
}

/* Actor placement counts decide which art is required for this course. */
static PortableRecoveredPackStatus load_course_actor_art(const PortableAssetPackView *pack,
                                                         const PortableContentSelection *selection,
                                                         const PortableRecoveredRaceView *race,
                                                         PortablePresentationPackStorage *st,
                                                         PortablePresentationPackView *out)
{
    if (race->lava_bubble_count != 0u)
    {
        PortableAssetSectionView bubbles;
        if (!find(pack, PORTABLE_ASSET_SECTION_LAVA_BUBBLE_ART, selection->course.id, &bubbles))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&bubbles, st->lava_bubble_art, 0x3141424cu, 2u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->lava_bubble_art = st->lava_bubble_art;
    }
    if (race->thwomp_count != 0u)
    {
        PortableAssetSectionView thwomps;
        if (!find(pack, PORTABLE_ASSET_SECTION_THWOMP_ART, selection->course.id, &thwomps))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&thwomps, st->thwomp_art, 0x31415754u, 1u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->thwomp_art = st->thwomp_art;
    }
    if (race->crab_count != 0u)
    {
        PortableAssetSectionView crabs;
        if (!find(pack, PORTABLE_ASSET_SECTION_CRAB_ART, selection->course.id, &crabs))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&crabs, st->crab_art, 0x31415243u, 4u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->crab_art = st->crab_art;
    }
    if (race->seagull_count != 0u)
    {
        PortableAssetSectionView seagulls;
        if (!find(pack, PORTABLE_ASSET_SECTION_SEAGULL_ART, selection->course.id, &seagulls))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&seagulls, st->seagull_art, 0x31414753u, 3u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->seagull_art = st->seagull_art;
    }
    if (race->piranha_count != 0u)
    {
        PortableAssetSectionView piranhas;
        if (!find(pack, PORTABLE_ASSET_SECTION_PIRANHA_ART, selection->course.id, &piranhas))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&piranhas, st->piranha_art, 0x31415250u, 5u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->piranha_art = st->piranha_art;
    }
    if (race->snowman_count != 0u)
    {
        PortableAssetSectionView snowmen;
        if (!find(pack, PORTABLE_ASSET_SECTION_SNOWMAN_ART, selection->course.id, &snowmen))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&snowmen, st->snowman_art, 0x31414e53u, 2u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->snowman_art = st->snowman_art;
    }
    if (race->mouse_count != 0u)
    {
        PortableAssetSectionView mice;
        if (!find(pack, PORTABLE_ASSET_SECTION_MOUSE_ART, selection->course.id, &mice))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&mice, st->mouse_art, 0x3141534du, 2u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->mouse_art = st->mouse_art;
    }
    if (race->thundercloud_count != 0u)
    {
        PortableAssetSectionView thunderclouds;
        if (!find(pack, PORTABLE_ASSET_SECTION_THUNDERCLOUD_ART, selection->course.id,
                  &thunderclouds))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&thunderclouds, st->thundercloud_art, 0x31414354u,
                       PORTABLE_THUNDERCLOUD_ART_COUNT, 128u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->thundercloud_art = st->thundercloud_art;
    }
    if (race->falling_star_count != 0u)
    {
        PortableAssetSectionView falling_stars;
        if (!find(pack, PORTABLE_ASSET_SECTION_FALLING_STAR_ART, selection->course.id,
                  &falling_stars))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&falling_stars, st->falling_star_art, 0x31415253u, 5u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->falling_star_art = st->falling_star_art;
    }
    if (race->penguin_count != 0u)
    {
        PortableAssetSectionView penguins;
        if (!find(pack, PORTABLE_ASSET_SECTION_PENGUIN_ART, selection->course.id, &penguins))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&penguins, st->penguin_art, 0x31414750u, 6u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->penguin_art = st->penguin_art;
    }
    if (race->boo_count != 0u)
    {
        PortableAssetSectionView boos;
        if (!find(pack, PORTABLE_ASSET_SECTION_BOO_ART, selection->course.id, &boos))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&boos, st->boo_art, 0x31414f42u, 2u, 64u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->boo_art = st->boo_art;
    }
    if (race->cannonball_count != 0u)
    {
        PortableAssetSectionView cannonballs;
        if (!find(pack, PORTABLE_ASSET_SECTION_CANNONBALL_ART, selection->course.id, &cannonballs))
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (!actor_art(&cannonballs, st->cannonball_art, 0x31414243u, 7u, 128u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->cannonball_art = st->cannonball_art;
    }
    return PORTABLE_RECOVERED_PACK_OK;
}

PortableRecoveredPackStatus portable_presentation_pack_open(
    const PortableAssetPackView *pack, const PortableContentSelection *selection,
    const PortableRecoveredPackStorage *rs, const PortableRecoveredRaceView *race,
    PortablePresentationPackStorage *st, PortablePresentationPackView *out)
{
    PortableAssetSectionView road_section, sky_section, character_section, minimap_section,
        hud_section, effect_section, lightning_section, banner_section, results_section,
        standings_section, box_section, scenery_section, marker_section, hud_bg_section;
    if (!pack || !selection || !rs || !race || !st || !out)
        return PORTABLE_RECOVERED_PACK_INVALID_ARGUMENT;
    memset(st, 0, sizeof *st);
    memset(out, 0, sizeof *out);
    out->ambient_rules = race->ambient_rules;
    out->ambient_seed = race->ambient_seed;
    out->view_rules = race->view_rules;
    out->finish_rules = race->finish_rules;
    out->weather_profile = race->weather_profile;
    out->recovery_fx_rules = race->recovery_fx_rules;
    out->surface_trail_rules = race->surface_trail_rules;
    out->coin_effect_rules = race->coin_effect_rules;
    out->impact_effect_rules = race->impact_effect_rules;
    out->finish_lakitu_rules = race->finish_lakitu_rules;
    out->time_trial_record_defaults = race->time_trial_record_defaults;
    PortableAssetSectionView projection_section;
    const PortableAssetPackStatus projection_status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_DRIVER_PROJECTION_RULES, 1u, &projection_section);
    if (projection_status != PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        if (projection_status != PORTABLE_ASSET_PACK_OK ||
            !portable_driver_projection_asset_open(&projection_section, &st->driver_projection))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->driver_projection = &st->driver_projection;
    }

    if (!find(pack, PORTABLE_ASSET_SECTION_ROAD_GRAPHICS, selection->course.road, &road_section) ||
        !find(pack, PORTABLE_ASSET_SECTION_SKY_GRAPHICS, selection->course.sky, &sky_section) ||
        !find(pack, PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS, selection->character.graphics,
              &character_section) ||
        !find(pack, PORTABLE_ASSET_SECTION_MINIMAP_GRAPHICS, selection->course.minimap,
              &minimap_section) ||
        !find(pack, PORTABLE_ASSET_SECTION_HUD_GRAPHICS, selection->course.hud, &hud_section) ||
        !find(pack, PORTABLE_ASSET_SECTION_RACE_EFFECT_GRAPHICS, 1u, &effect_section))
        return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
    if (!road(&road_section, st, &out->tileset) || !sky(&sky_section, st, &out->sky) ||
        !character(&character_section, st, &out->character) ||
        !minimap(&minimap_section, st, &out->minimap) || !hud(&hud_section, st, &out->hud) ||
        !portable_race_particle_asset_open(
            &effect_section, &st->charged_exit, PORTABLE_DRIFT_SPARK_FRAME_COUNT,
            PORTABLE_DRIFT_SPARK_CELL_BYTES, &out->charged_exit_particle))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableRecoveredPackStatus status = load_optional_effect_art(pack, st, out);
    if (status != PORTABLE_RECOVERED_PACK_OK)
        return status;
    if (find(pack, PORTABLE_ASSET_SECTION_LIGHTNING_EFFECT_GRAPHICS, 1u, &lightning_section) &&
        !lightning_effect(&lightning_section, st, &out->lightning_effect))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_GP_FINISH_BANNER_GRAPHICS, 1u, &banner_section) &&
        !gp_finish_banner(&banner_section, st, &out->gp_finish_banner))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_GP_RESULTS_GRAPHICS, 1u, &results_section) &&
        !gp_results(&results_section, st, &out->gp_results))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_GP_STANDINGS_GRAPHICS, selection->course.id,
             &standings_section) &&
        !gp_standings(&standings_section, st, &out->gp_standings))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_ITEM_BOX_GRAPHICS, 1u, &box_section) &&
        !item_box(&box_section, st, &out->item_box))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_COURSE_SCENERY, selection->course.id, &scenery_section) &&
        !scenery(&scenery_section, st, &out->scenery))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    status = load_course_actor_art(pack, selection, race, st, out);
    if (status != PORTABLE_RECOVERED_PACK_OK)
        return status;
    if (find(pack, PORTABLE_ASSET_SECTION_GP_MINIMAP_MARKERS, 1u, &marker_section) &&
        !gp_minimap_markers(&marker_section, st, &out->gp_minimap_markers))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_GP_RACE_HUD_BG0, selection->course.id, &hud_bg_section) &&
        !portable_gp_race_hud_asset_open(&hud_bg_section, &out->gp_race_hud))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (find(pack, PORTABLE_ASSET_SECTION_GP_OPENING_TITLE, selection->course.id,
             &hud_bg_section) &&
        hud_bg_section.version >= 27u &&
        !portable_gp_opening_asset_open(&hud_bg_section, &out->gp_opening))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    st->geometry_data[3] = 5;
    st->geometry_data[4] = 0;
    for (size_t point = 0; point < race->rules.route_count; ++point)
    {
        if ((race->rules.route[point].x & 7) || (race->rules.route[point].y & 7))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        w16(st->geometry_data + 5 + point * 8, (uint16_t)(race->rules.route[point].x / 8));
        w16(st->geometry_data + 7 + point * 8, (uint16_t)(race->rules.route[point].y / 8));
    }
    out->geometry = (PortableTrackGeometryView){
        st->geometry_data, sizeof st->geometry_data, (uint16_t)race->rules.route_count, 0, 5, 32};
    out->world_map = (PortableRoadMapView){rs->terrain_cells,
                                           rs->terrain.width * rs->terrain.height,
                                           rs->terrain.width,
                                           rs->terrain.height,
                                           0,
                                           0};
    return PORTABLE_RECOVERED_PACK_OK;
}
