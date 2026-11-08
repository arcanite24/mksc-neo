#include "recovered_pack.h"
#include "engine_sound_asset.h"

#include <string.h>
#include <stdlib.h>

#define RULES_MAGIC UINT32_C(0x314c5552)
#define ROUTE_MAGIC UINT32_C(0x31545552)
#define WORLD_MAGIC UINT32_C(0x31444c57)
#define MATERIAL_MAGIC UINT32_C(0x3154414d)
#define MOTION_MAGIC UINT32_C(0x31544f4d)
#define COLLISION_MAGIC UINT32_C(0x314c4f43)
#define COURSE_OBJECT_MAGIC UINT32_C(0x314a424f)
#define COURSE_COIN_MAGIC UINT32_C(0x314e4943)
#define GP_GRID_MAGIC UINT32_C(0x31475047)

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t i32(const uint8_t *p)
{
    const uint32_t value = u32(p);
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

static int section(const PortableAssetPackView *pack, uint32_t kind, uint32_t content,
                   PortableAssetSectionView *out)
{
    return portable_asset_pack_find_section(pack, kind, content, out) == PORTABLE_ASSET_PACK_OK;
}

static int copy_i16(const uint8_t **cursor, const uint8_t *end, int16_t *out, size_t count)
{
    if ((size_t)(end - *cursor) < count * 2u)
        return 0;
    for (size_t i = 0; i < count; ++i)
        out[i] = (int16_t)u16(*cursor + i * 2u);
    *cursor += count * 2u;
    return 1;
}

static int copy_i32(const uint8_t **cursor, const uint8_t *end, int32_t *out, size_t count)
{
    if ((size_t)(end - *cursor) < count * 4u)
        return 0;
    for (size_t i = 0; i < count; ++i)
        out[i] = (int32_t)u32(*cursor + i * 4u);
    *cursor += count * 4u;
    return 1;
}

static int parse_rules(const PortableAssetSectionView *s, PortableRaceRulesView *rules)
{
    const uint8_t *p = s->bytes;
    if (!((s->version == 1u && s->size == 68u) || (s->version == 2u && s->size == 72u)) ||
        u32(p) != RULES_MAGIC)
        return 0;
    rules->compatibility.course_id = u32(p + 4);
    rules->compatibility.course_revision = u32(p + 8);
    rules->compatibility.rules_id = u32(p + 12);
    rules->compatibility.rules_revision = u32(p + 16);
    rules->compatibility.handling_profile_id = u32(p + 20);
    rules->compatibility.handling_revision = u32(p + 24);
    rules->compatibility.character_class = u32(p + 28);
    rules->compatibility.state_schema_version = u32(p + 32);
    rules->start_x_q16 = (int32_t)u32(p + 36);
    rules->start_y_q16 = (int32_t)u32(p + 40);
    rules->start_heading = u16(p + 44);
    rules->start_route_index = u16(p + 46);
    rules->start_checkpoint_raw = (uint8_t)u16(p + 66);
    rules->progress_kind = (PortableRaceProgressKind)u32(p + 48);
    rules->finish_kind = (PortableFinishGateKind)u32(p + 52);
    rules->finish_y_q16 = (int32_t)u32(p + 56);
    rules->lap_count = u16(p + 60);
    rules->countdown_ticks = u16(p + 62);
    rules->rescue_penalty_ticks = u16(p + 64);
    if (u16(p + 66) > UINT8_MAX)
        return 0;
    if (s->version == 1u)
        rules->item_strength_threshold = 0u; /* v1 has no item odds contract. */
    else
    {
        /* v2 appends u8 threshold 1..8 and three zero bytes. */
        if (p[68] == 0u || p[68] > 8u || p[69] || p[70] || p[71])
            return 0;
        rules->item_strength_threshold = p[68];
    }
    return 1;
}

static int parse_route(const PortableAssetSectionView *s, PortableRecoveredPackStorage *st,
                       PortableRaceRulesView *rules)
{
    const uint32_t count = s->size >= 8u ? u32(s->bytes + 4) : 0u;
    const size_t stride = s->version == 1u ? 8u : 12u;
    const uint32_t rows = s->version >= 3u && s->size >= 16u ? u32(s->bytes + 8u) : 1u;
    const uint32_t table_count = s->version >= 3u && s->size >= 16u ? u32(s->bytes + 12u) : 0u;
    const size_t header = s->version >= 3u ? 16u : 8u;
    if ((s->version != 1u && s->version != 2u && s->version != 3u && s->version != 4u) ||
        rows == 0u || rows > 3u || table_count > 40u || (s->version >= 3u && table_count != 40u) ||
        s->size != header + (size_t)count * rows * stride + table_count +
                       (s->version >= 4u ? 4u + (size_t)rows * 8u : 0u) ||
        u32(s->bytes) != ROUTE_MAGIC || count == 0u || count > PORTABLE_RECOVERED_ROUTE_CAPACITY)
        return 0;
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t *record = s->bytes + header + ((size_t)row * count + i) * stride;
            const uint32_t ai_mode = s->version != 1u ? u32(record + 8u) : 0u;
            if (ai_mode > UINT8_MAX)
                return 0;
            PortableRacePoint *point =
                row == 0u ? &st->route[i] : &st->gp_route_rows[((size_t)row - 1u) * count + i];
            point->x = (int32_t)u32(record);
            point->y = (int32_t)u32(record + 4u);
            st->route_ai_modes[(size_t)row * count + i] = (uint8_t)ai_mode;
        }
    if (table_count != 0u)
    {
        const uint8_t *table = s->bytes + header + (size_t)count * rows * stride;
        for (uint32_t i = 0u; i < table_count; ++i)
        {
            if (table[i] >= rows)
                return 0;
            st->gp_target_table[i] = table[i];
        }
        rules->gp_route_rows = st->gp_route_rows;
        rules->gp_route_row_count = rows - 1u;
        rules->gp_target_table = st->gp_target_table;
        rules->gp_target_table_count = table_count;
    }
    if (s->version >= 4u)
    {
        const uint8_t *tail = s->bytes + header + (size_t)count * rows * stride + table_count;
        const uint32_t mask = u32(tail);
        if (mask & ~((1u << rows) - 1u))
            return 0;
        for (uint32_t row = 0u; row < rows; ++row)
        {
            st->gp_sentinel_targets[row].x = (int32_t)u32(tail + 4u + row * 8u);
            st->gp_sentinel_targets[row].y = (int32_t)u32(tail + 8u + row * 8u);
            if (!(mask & (1u << row)) &&
                (st->gp_sentinel_targets[row].x != 0 || st->gp_sentinel_targets[row].y != 0))
                return 0;
        }
        rules->gp_sentinel_targets = st->gp_sentinel_targets;
        rules->gp_sentinel_target_mask = (uint8_t)mask;
    }
    rules->route = st->route;
    rules->route_count = count;
    rules->ai_modes = st->route_ai_modes;
    rules->ai_mode_count = (size_t)count * rows;
    return rules->start_route_index < count;
}

static int parse_world(const PortableAssetSectionView *s, PortableRecoveredPackStorage *st)
{
    const uint8_t *p = s->bytes;
    uint32_t rw, rh, tw, th, rn, tn;
    if (s->version != 1u || s->size < 28u || u32(p) != WORLD_MAGIC)
        return 0;
    rw = u32(p + 4);
    rh = u32(p + 8);
    tw = u32(p + 12);
    th = u32(p + 16);
    rn = u32(p + 20);
    tn = u32(p + 24);
    if (rw == 0u || rh == 0u || tw == 0u || th == 0u || rn != rw * rh || tn != tw * th ||
        rn > sizeof st->raster_cells || tn > sizeof st->terrain_cells || s->size != 28u + rn + tn)
        return 0;
    memcpy(st->raster_cells, p + 28, rn);
    memcpy(st->terrain_cells, p + 28 + rn, tn);
    st->raster = (PortableTrackRaster){st->raster_cells, rn, rw, rh, 0u, 0u};
    st->terrain = (PortableTerrainView){st->terrain_cells, tw, tw, th};
    st->initial_terrain_cells = p + 28u + rn;
    return 1;
}

static int parse_banana_rules(const PortableAssetPackView *pack, PortableBananaRules *out)
{
    PortableAssetSectionView s;
    const PortableAssetPackStatus status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_BANANA_RULES, 1u, &s);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        *out = (PortableBananaRules){0};
        return 1;
    }
    if (status != PORTABLE_ASSET_PACK_OK || s.version != 1u || s.size != 44u ||
        u32(s.bytes) != 0x314e4142u || u32(s.bytes + 4u) != 9u)
        return 0;
    const PortableBananaRules next = {u32(s.bytes + 8u),  u32(s.bytes + 12u), u32(s.bytes + 16u),
                                      u32(s.bytes + 20u), u32(s.bytes + 24u), u32(s.bytes + 28u),
                                      u32(s.bytes + 32u), u32(s.bytes + 36u), u32(s.bytes + 40u)};
    if (!portable_banana_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}

static int parse_red_shell_rules(const PortableAssetPackView *pack, PortableRedShellRules *out)
{
    PortableAssetSectionView s;
    const PortableAssetPackStatus status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_RED_SHELL_RULES, 1u, &s);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        *out = (PortableRedShellRules){0};
        return 1;
    }
    if (status != PORTABLE_ASSET_PACK_OK || s.version != 1u || s.size != 72u ||
        u32(s.bytes) != 0x31485352u || u32(s.bytes + 4u) != 16u)
        return 0;
    const PortableRedShellRules next = {
        u32(s.bytes + 8u),  u32(s.bytes + 12u), u32(s.bytes + 16u), u32(s.bytes + 20u),
        u32(s.bytes + 24u), u32(s.bytes + 28u), u32(s.bytes + 32u), u32(s.bytes + 36u),
        u32(s.bytes + 40u), u32(s.bytes + 44u), u32(s.bytes + 48u), u32(s.bytes + 52u),
        u32(s.bytes + 56u), u32(s.bytes + 60u), u32(s.bytes + 64u), u32(s.bytes + 68u)};
    if (!portable_red_shell_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}

static int parse_shell_orbit_rules(const PortableAssetPackView *pack, PortableShellOrbitRules *out)
{
    PortableAssetSectionView s;
    const PortableAssetPackStatus status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_SHELL_ORBIT_RULES, 1u, &s);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        *out = (PortableShellOrbitRules){0};
        return 1;
    }
    if (status != PORTABLE_ASSET_PACK_OK || s.version != 1u || s.size != 20u ||
        u32(s.bytes) != 0x3142524fu || u32(s.bytes + 4u) != 3u)
        return 0;
    const PortableShellOrbitRules next = {u32(s.bytes + 8u), u32(s.bytes + 12u),
                                          u32(s.bytes + 16u)};
    if (!portable_shell_orbit_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}

static int parse_green_shell_rules(const PortableAssetPackView *pack, PortableGreenShellRules *out)
{
    PortableAssetSectionView source;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_GREEN_SHELL_RULES, 1u, &source);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        *out = (PortableGreenShellRules){0};
        return 1;
    }
    if (status != PORTABLE_ASSET_PACK_OK || source.version != 1u || source.size != 40u ||
        u32(source.bytes) != 0x31485347u || u32(source.bytes + 4u) != 8u)
        return 0;
    for (unsigned i = 0u; i < 6u; ++i)
        if (u32(source.bytes + 8u + i * 4u) > 65535u)
            return 0;
    PortableGreenShellRules next = {
        (uint16_t)u32(source.bytes + 8u),  (uint16_t)u32(source.bytes + 12u),
        (uint16_t)u32(source.bytes + 16u), (uint16_t)u32(source.bytes + 20u),
        (uint16_t)u32(source.bytes + 24u), (uint16_t)u32(source.bytes + 28u),
        u32(source.bytes + 32u),           u32(source.bytes + 36u)};
    if (!portable_green_shell_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}

static int parse_boo_rules(const PortableAssetPackView *pack, PortableBooRules *out)
{
    PortableAssetSectionView source;
    const PortableAssetPackStatus status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_BOO_RULES, 1u, &source);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        *out = (PortableBooRules){0};
        return 1;
    }
    if (status != PORTABLE_ASSET_PACK_OK || source.version != 1u || source.size != 36u ||
        u32(source.bytes) != UINT32_C(0x314f4f42) || u32(source.bytes + 4u) != 7u)
        return 0;
    for (unsigned i = 0u; i < 5u; ++i)
        if (u32(source.bytes + 8u + i * 4u) > 32767u)
            return 0;
    PortableBooRules next = {(uint16_t)u32(source.bytes + 8u),
                             (uint16_t)u32(source.bytes + 12u),
                             (uint16_t)u32(source.bytes + 16u),
                             (uint16_t)u32(source.bytes + 20u),
                             (uint16_t)u32(source.bytes + 24u),
                             u32(source.bytes + 28u),
                             u32(source.bytes + 32u)};
    if (!portable_boo_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}

static int parse_item_status_rules(const PortableAssetPackView *pack, PortableItemStatusRules *out)
{
    PortableAssetSectionView source;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_ITEM_STATUS_RULES, 1u, &source);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        *out = (PortableItemStatusRules){0};
        return 1;
    }
    if (status != PORTABLE_ASSET_PACK_OK || source.version != 1u || source.size != 20u ||
        u32(source.bytes) != UINT32_C(0x31544153) || u32(source.bytes + 4u) != 3u)
        return 0;
    for (unsigned i = 0u; i < 3u; ++i)
        if (u32(source.bytes + 8u + i * 4u) > 32767u)
            return 0;
    PortableItemStatusRules next = {(uint16_t)u32(source.bytes + 8u),
                                    (uint16_t)u32(source.bytes + 12u),
                                    (uint16_t)u32(source.bytes + 16u)};
    if (!portable_item_status_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}

static int parse_mushroom_rules(const PortableAssetPackView *pack, PortableMushroomRules *out)
{
    PortableMushroomRules next = {0};
    PortableAssetSectionView source;
    const PortableAssetPackStatus status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_MUSHROOM_RULES, 1u, &source);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        *out = next;
        return 1;
    }
    if (status != PORTABLE_ASSET_PACK_OK || source.version != 1u || source.size != 28u ||
        u32(source.bytes) != UINT32_C(0x3153554d) || u32(source.bytes + 4u) != 5u)
        return 0;
    for (unsigned field = 0u; field < 4u; ++field)
        if (u32(source.bytes + 8u + field * 4u) > 65535u)
            return 0;
    next.boost_ticks = (uint16_t)u32(source.bytes + 8u);
    next.single_actor_ticks = (uint16_t)u32(source.bytes + 12u);
    next.golden_actor_ticks = (uint16_t)u32(source.bytes + 16u);
    next.acceleration_units = (uint16_t)u32(source.bytes + 20u);
    next.max_speed_q16 = u32(source.bytes + 24u);
    if (!portable_mushroom_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}

/* 2E98 starts with three credits; 2C54 decrements only when nonzero.
 * 1E7C8 routes exhausted credits to state32 (Game Over). */
static int gp_scoring_section(const PortableAssetPackView *pack, uint32_t id, int fallback,
                              PortableGpScoringRules *out)
{
    if (pack == NULL || out == NULL)
        return 0;
    PortableGpScoringRules next = {0};
    next.initial_retry_credits = 3u;
    PortableAssetSectionView source;
    const PortableAssetPackStatus status =
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_GP_SCORING, id, &source);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        if (fallback)
            *out = next;
        return fallback;
    }
    if (status != PORTABLE_ASSET_PACK_OK ||
        !((source.version == 1u && source.size == 16u) ||
          (source.version == 2u && source.size == 20u) ||
          (source.version == 3u && source.size == 112u)) ||
        u32(source.bytes) != UINT32_C(0x31545047) || u32(source.bytes + 4u) != 8u)
        return 0;
    for (unsigned rank = 0u; rank < 8u; ++rank)
    {
        if (source.bytes[8u + rank] > 63u)
            return 0;
        next.finish_points[rank] = source.bytes[8u + rank];
    }
    if (source.version >= 2u)
    {
        const uint32_t credits = u32(source.bytes + 16u);
        if (credits > 3u)
            return 0;
        next.initial_retry_credits = (uint8_t)credits;
    }
    if (source.version == 3u)
    {
        size_t offset = 20u;
        for (unsigned row = 0u; row < 3u; ++row)
            for (unsigned rank = 0u; rank < 4u; ++rank, offset += 4u)
                next.quality.rank_bonus[row][rank] = i32(source.bytes + offset);
        for (unsigned index = 0u; index < 3u; ++index, offset += 4u)
            next.quality.speed_target[index] = i32(source.bytes + offset);
        for (unsigned index = 0u; index < 8u; ++index, offset += 4u)
            next.quality.driver_bonus[index] = i32(source.bytes + offset);
        /* Bound modded inputs so the deterministic signed score cannot overflow. */
        for (unsigned row = 0u; row < 3u; ++row)
        {
            if (next.quality.speed_target[row] < 0 || next.quality.speed_target[row] > 10000)
                return 0;
            for (unsigned rank = 0u; rank < 4u; ++rank)
                if (next.quality.rank_bonus[row][rank] < -10000 ||
                    next.quality.rank_bonus[row][rank] > 10000)
                    return 0;
        }
        for (unsigned index = 0u; index < 8u; ++index)
            if (next.quality.driver_bonus[index] < -10000 ||
                next.quality.driver_bonus[index] > 10000)
                return 0;
        next.has_quality_rules = 1u;
    }
    *out = next;
    return 1;
}
int portable_recovered_gp_scoring(const PortableAssetPackView *pack, PortableGpScoringRules *out)
{
    return gp_scoring_section(pack, 1u, 1, out);
}
int portable_recovered_cup_scoring(const PortableAssetPackView *pack, uint32_t cup_id,
                                   PortableGpScoringRules *out)
{
    return cup_id != 1u && gp_scoring_section(pack, cup_id, 0, out);
}
/* RLT1 owns the source preview/timeline, gap boundaries and status tables. */
static int parse_roulette_rules(const PortableAssetPackView *pack,
                                PortableRecoveredPackStorage *storage)
{
    PortableAssetSectionView section;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS, 12u, &section);
    storage->roulette_rules_valid = 0u;
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
        return 1;
    if (status != PORTABLE_ASSET_PACK_OK || section.version != 1u || section.size != 176u ||
        section.bytes == NULL || u32(section.bytes) != UINT32_C(0x31544c52) ||
        u32(section.bytes + 4u) != 10u)
        return 0;
    PortableGpRouletteRules *rules = &storage->roulette_rules;
    for (unsigned i = 0u; i < 10u; ++i)
    {
        rules->preview[i] = u16(section.bytes + 8u + i * 2u);
        if (rules->preview[i] < 0x1000u || rules->preview[i] > 0x100fu)
            return 0;
    }
    for (unsigned i = 0u; i < 27u; ++i)
    {
        rules->timeline[i] = u16(section.bytes + 28u + i * 2u);
        if (rules->timeline[i] == 0u || rules->timeline[i] > 32767u)
            return 0;
    }
    for (unsigned i = 0u; i < 8u; ++i)
    {
        rules->gap_upper[i] = u16(section.bytes + 82u + i * 2u);
        if (i != 0u && rules->gap_upper[i] <= rules->gap_upper[i - 1u])
            return 0;
    }
    for (unsigned row = 0u; row < 8u; ++row)
        for (unsigned size = 0u; size < 2u; ++size)
        {
            const uint32_t weight = u32(section.bytes + 98u + (row * 2u + size) * 4u);
            if (weight == 0u || weight > 65535u)
                return 0;
            rules->contact_weight[row][size] = weight;
        }
    for (unsigned i = 0u; i < 14u; ++i)
    {
        const uint8_t frame = section.bytes[162u + i];
        if (frame != 255u && frame > 3u)
            return 0;
        rules->lightning_slots[i] = frame == 255u ? -1 : (int8_t)frame;
    }
    storage->roulette_rules_valid = 1u;
    return 1;
}

/* 43BB0's global player odds. An entirely absent bank retains pre-bank pack
 * compatibility; a partial bank is malformed and must never silently mix data. */
static int parse_player_item_odds(const PortableAssetPackView *pack,
                                  PortableRecoveredPackStorage *storage)
{
    unsigned found = 0u;
    for (unsigned id = 1u; id <= PORTABLE_GP_PLAYER_ITEM_TABLE_COUNT; ++id)
    {
        PortableAssetSectionView source;
        const PortableAssetPackStatus status = portable_asset_pack_find_section(
            pack, PORTABLE_ASSET_SECTION_GP_PLAYER_ITEM_ODDS, id, &source);
        if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
            continue;
        if (status != PORTABLE_ASSET_PACK_OK || source.version != 1u || source.size != 72u ||
            u32(source.bytes) != UINT32_C(0x31575047) || u32(source.bytes + 4u) != 16u)
            return 0;
        uint32_t total = 0u;
        int terminated = 0;
        for (unsigned row = 0u; row < PORTABLE_GP_PLAYER_ITEM_TABLE_CAPACITY; ++row)
        {
            const uint16_t item = u16(source.bytes + 8u + row * 4u),
                           weight = u16(source.bytes + 10u + row * 4u);
            if (terminated)
            {
                if (item != 0u || weight != 0u)
                    return 0;
            }
            else if (item == 0u)
            {
                if (weight != 255u || total == 0u)
                    return 0;
                terminated = 1;
            }
            else
            {
                if (item < 0x1000u || item > 0x100cu || weight == 0u || total + weight > 32767u)
                    return 0;
                total += weight;
            }
            storage->gp_player_items.table[id - 1u][row] = (PortableGpItemWeight){item, weight};
        }
        if (!terminated)
            return 0;
        ++found;
    }
    if (found != 0u && found != PORTABLE_GP_PLAYER_ITEM_TABLE_COUNT)
        return 0;
    storage->gp_player_items_valid = (uint8_t)(found != 0u);
    return 1;
}

/* MAT1 grows by appending one bounded tail per schema revision. The layout
 * names wire fields without changing the historical validation/write order. */
typedef struct
{
    const uint8_t *flags;
    const uint8_t *ramp_speed_limit, *wall_replacement_tile, *wall_contact_song;
    const uint8_t *protected_hop, *cpu_items;
    const uint8_t *banana_release, *green_release, *red_release, *orbit_delay;
    const uint8_t *mushroom_voices, *spatial_pitch;
    const uint8_t *battle_items, *elimination, *battle_ai, *combat;
} MaterialLayout;

static int material_take(const uint8_t **cursor, const uint8_t *end, size_t size,
                         const uint8_t **field)
{
    if ((size_t)(end - *cursor) < size)
        return 0;
    *field = *cursor;
    *cursor += size;
    return 1;
}

static int material_layout(const PortableAssetSectionView *section, uint32_t count,
                           MaterialLayout *layout)
{
    if (section->version < 1u || section->version > 16u || count != 256u)
        return 0;
    const uint8_t *cursor = section->bytes + 8u;
    const uint8_t *end = section->bytes + section->size;
    *layout = (MaterialLayout){0};
    if (!material_take(&cursor, end, 256u, &layout->flags))
        return 0;
    if (section->version >= 2u && !material_take(&cursor, end, 4u, &layout->ramp_speed_limit))
        return 0;
    if (section->version >= 3u && !material_take(&cursor, end, 4u, &layout->wall_replacement_tile))
        return 0;
    if (section->version >= 4u && !material_take(&cursor, end, 4u, &layout->wall_contact_song))
        return 0;
    if (section->version >= 5u && !material_take(&cursor, end, 12u, &layout->protected_hop))
        return 0;
    if (section->version >= 6u && !material_take(&cursor, end, 1152u, &layout->cpu_items))
        return 0;
    if (section->version >= 7u && !material_take(&cursor, end, 8u, &layout->banana_release))
        return 0;
    if (section->version >= 8u && !material_take(&cursor, end, 8u, &layout->green_release))
        return 0;
    if (section->version >= 9u && !material_take(&cursor, end, 8u, &layout->red_release))
        return 0;
    if (section->version >= 10u && !material_take(&cursor, end, 6u, &layout->orbit_delay))
        return 0;
    if (section->version >= 11u && !material_take(&cursor, end, 32u, &layout->mushroom_voices))
        return 0;
    if (section->version >= 12u && !material_take(&cursor, end, 2178u, &layout->spatial_pitch))
        return 0;
    if (section->version >= 13u && !material_take(&cursor, end, 124u, &layout->battle_items))
        return 0;
    if (section->version >= 14u && !material_take(&cursor, end, 4u, &layout->elimination))
        return 0;
    if (section->version >= 15u && !material_take(&cursor, end, 24u, &layout->battle_ai))
        return 0;
    if (section->version >= 16u && !material_take(&cursor, end, 12u, &layout->combat))
        return 0;
    return cursor == end;
}

static int decode_material_battle_rules(const MaterialLayout *layout,
                                        PortableRecoveredPackStorage *st)
{
    st->battle_items_valid = 0u;
    st->battle_items.elimination_ticks = 0u;
    st->battle_items.ai = (PortableBattleAiRules){0};
    st->battle_items.combat = (PortableBattleCombatRules){0};
    if (layout->combat != NULL)
    {
        uint16_t values[3];
        for (size_t i = 0u; i < 3u; ++i)
        {
            const uint32_t value = u32(layout->combat + i * 4u);
            if (value > UINT16_MAX)
                return 0;
            values[i] = (uint16_t)value;
        }
        st->battle_items.combat = (PortableBattleCombatRules){values[0], values[1], values[2]};
        if (!portable_battle_combat_rules_valid(&st->battle_items.combat))
            return 0;
    }
    if (layout->battle_ai != NULL)
    {
        uint16_t values[6];
        for (size_t i = 0u; i < 6u; ++i)
        {
            const uint32_t value = u32(layout->battle_ai + i * 4u);
            if (value > UINT16_MAX)
                return 0;
            values[i] = (uint16_t)value;
        }
        st->battle_items.ai = (PortableBattleAiRules){values[0], values[1], values[2],
                                                      values[3], values[4], values[5]};
        if (!portable_battle_ai_rules_valid(&st->battle_items.ai))
            return 0;
    }
    if (layout->elimination != NULL)
    {
        const uint32_t ticks = u32(layout->elimination);
        if (ticks == 0u || ticks > 127u)
            return 0;
        st->battle_items.elimination_ticks = (uint8_t)ticks;
    }
    if (layout->battle_items != NULL)
    {
        const uint32_t hidden = u32(layout->battle_items + 120u);
        if (hidden == 0u || hidden > 3600u)
            return 0;
        st->battle_items.box_hidden_ticks = (uint16_t)hidden;
        for (size_t row = 0u; row < PORTABLE_BATTLE_ITEM_TABLE_COUNT; ++row)
        {
            uint32_t total = 0u;
            uint8_t terminated = 0u;
            for (size_t column = 0u; column < PORTABLE_BATTLE_ITEM_TABLE_CAPACITY; ++column)
            {
                const uint8_t *entry = layout->battle_items + (row * 10u + column) * 4u;
                const uint16_t item = u16(entry), weight = u16(entry + 2u);
                if (terminated)
                {
                    if (item != 0u || weight != 0u)
                        return 0;
                }
                else if (item == 0u)
                {
                    if (weight != 255u)
                        return 0;
                    terminated = 1u;
                }
                else
                {
                    if (item < 0x1000u || item > 0x100cu)
                        return 0;
                    total += weight;
                }
                st->battle_items.table[row][column] = (PortableGpItemWeight){item, weight};
            }
            if (!terminated || total == 0u || total > INT16_MAX)
                return 0;
        }
        st->battle_items_valid = 1u;
    }
    return 1;
}

static int decode_material_voices(const MaterialLayout *layout, PortableRecoveredPackStorage *st)
{
    memset(st->mushroom_voice_songs, 0, sizeof st->mushroom_voice_songs);
    if (layout->mushroom_voices != NULL)
        for (size_t driver = 0u; driver < 8u; ++driver)
            for (size_t variant = 0u; variant < 2u; ++variant)
            {
                const uint16_t voice = u16(layout->mushroom_voices + (driver * 2u + variant) * 2u);
                if (voice == 0u)
                    return 0;
                st->mushroom_voice_songs[driver][variant] = voice;
            }
    return 1;
}

int portable_recovered_mushroom_voices(const PortableAssetPackView *pack, uint16_t out[8][2])
{
    /* A composed view (base pack plus mods) may exceed one pack's 4096 sections. */
    if (!pack || !out)
        return 0;
    uint16_t result[8][2] = {{0}};
    int found = 0;
    for (uint32_t index = 0; index < pack->section_count; ++index)
    {
        PortableAssetSectionView s;
        if (portable_asset_pack_section_at(pack, index, &s) != PORTABLE_ASSET_PACK_OK)
            return 0;
        if (s.kind != PORTABLE_ASSET_SECTION_MATERIAL_FLAGS)
            continue;
        MaterialLayout layout;
        if (!s.bytes || s.size < 8u || u32(s.bytes) != MATERIAL_MAGIC ||
            !material_layout(&s, u32(s.bytes + 4u), &layout))
            return 0;
        if (!layout.mushroom_voices)
            continue;
        uint16_t candidate[8][2];
        for (unsigned driver = 0; driver < 8; ++driver)
            for (unsigned variant = 0; variant < 2; ++variant)
            {
                candidate[driver][variant] =
                    u16(layout.mushroom_voices + (driver * 2u + variant) * 2u);
                if (!candidate[driver][variant])
                    return 0;
            }
        if (found && memcmp(result, candidate, sizeof result))
            return 0;
        memcpy(result, candidate, sizeof result);
        found = 1;
    }
    if (!found)
        return 0;
    memcpy(out, result, sizeof result);
    return 1;
}

static int decode_material_protected_hop(const MaterialLayout *layout,
                                         PortableRecoveredPackStorage *st)
{
    if (layout->protected_hop != NULL)
    {
        for (size_t i = 0u; i < 6u; ++i)
        {
            st->protected_hop_thresholds[i] = u16(layout->protected_hop + i * 2u);
            if (st->protected_hop_thresholds[i] > 2016u)
                return 0;
        }
        st->material.protected_hop_thresholds = st->protected_hop_thresholds;
        st->material.protected_hop_threshold_count = 6u;
    }
    return 1;
}

static int material_release_choices_valid(const uint8_t choices[8])
{
    for (size_t i = 0u; i < 8u; ++i)
        if (choices[i] > 3u)
            return 0;
    return 1;
}

static int decode_material_cpu_items(const MaterialLayout *layout, PortableRecoveredPackStorage *st)
{
    st->gp_cpu_item_engine_count = 0u;
    for (size_t engine = 0u; engine < 3u; ++engine)
    {
        st->gp_cpu_items[engine].banana_release_valid = 0u;
        st->gp_cpu_items[engine].green_release_valid = 0u;
        st->gp_cpu_items[engine].red_release_valid = 0u;
        st->gp_cpu_items[engine].shell_orbit_delay_valid = 0u;
    }
    if (layout->cpu_items == NULL)
        return 1;
    for (size_t engine = 0u; engine < 3u; ++engine)
        for (size_t variant = 0u; variant < 3u; ++variant)
            for (size_t driver = 0u; driver < 8u; ++driver)
                for (size_t rank = 0u; rank < 8u; ++rank)
                {
                    const size_t index = ((engine * 3u + variant) * 8u + driver) * 8u + rank;
                    const uint16_t item = u16(layout->cpu_items + index * 2u);
                    if (item != 0u && (item < 0x1000u || item > 0x100cu))
                        return 0;
                    st->gp_cpu_items[engine].item[variant][driver][rank] = item;
                }
    st->gp_cpu_item_engine_count = 3u;
    if (layout->orbit_delay != NULL)
        for (size_t engine = 0u; engine < 3u; ++engine)
        {
            memcpy(st->gp_cpu_items[engine].shell_orbit_delay, layout->orbit_delay, 6u);
            st->gp_cpu_items[engine].shell_orbit_delay_valid = 1u;
        }
    if (layout->red_release != NULL)
    {
        if (!material_release_choices_valid(layout->red_release))
            return 0;
        for (size_t engine = 0u; engine < 3u; ++engine)
        {
            memcpy(st->gp_cpu_items[engine].red_release, layout->red_release, 8u);
            st->gp_cpu_items[engine].red_release_valid = 1u;
        }
    }
    if (layout->green_release != NULL)
    {
        if (!material_release_choices_valid(layout->green_release))
            return 0;
        for (size_t engine = 0u; engine < 3u; ++engine)
        {
            memcpy(st->gp_cpu_items[engine].green_release, layout->green_release, 8u);
            st->gp_cpu_items[engine].green_release_valid = 1u;
        }
    }
    if (layout->banana_release != NULL)
    {
        if (!material_release_choices_valid(layout->banana_release))
            return 0;
        for (size_t engine = 0u; engine < 3u; ++engine)
        {
            memcpy(st->gp_cpu_items[engine].banana_release, layout->banana_release, 8u);
            st->gp_cpu_items[engine].banana_release_valid = 1u;
        }
    }
    return 1;
}

static int parse_material(const PortableAssetSectionView *s, PortableRecoveredPackStorage *st)
{
    const uint32_t count = s->size >= 8u ? u32(s->bytes + 4u) : 0u;
    MaterialLayout layout;
    if (!material_layout(s, count, &layout) || u32(s->bytes) != MATERIAL_MAGIC ||
        (layout.ramp_speed_limit && u32(layout.ramp_speed_limit) > UINT32_C(0x7fff0000)) ||
        (layout.wall_replacement_tile && u32(layout.wall_replacement_tile) > 255u) ||
        (layout.wall_contact_song && u32(layout.wall_contact_song) > UINT16_MAX))
        return 0;

    /* Later revisions were historically checked first. Preserve even partial
     * writes on rejection; callers must not consume a failed decode. */
    if (!decode_material_battle_rules(&layout, st))
        return 0;
    if (layout.spatial_pitch != NULL)
        for (size_t i = 0u; i < 33u * 33u; ++i)
        {
            const uint16_t bits = u16(layout.spatial_pitch + i * 2u);
            st->spatial_pitch_table[i] =
                (int16_t)(bits <= INT16_MAX ? (int32_t)bits : (int32_t)bits - 65536);
        }
    if (!decode_material_voices(&layout, st))
        return 0;
    memcpy(st->material_flags, layout.flags, count);
    st->material = (PortableTrackMaterialView){.flags = st->material_flags, .count = count};
    if (!decode_material_protected_hop(&layout, st))
        return 0;
    return decode_material_cpu_items(&layout, st);
}

static int parse_motion(const PortableAssetSectionView *s, PortableRecoveredPackStorage *st)
{
    static const uint32_t expected[7] = {1024, 64, 8, 8, 8, 16, 10};
    const uint8_t *p = s->bytes, *end = s->bytes + s->size;
    if ((s->version < 1u || s->version > 8u) || s->size < 36u || u32(p) != MOTION_MAGIC)
        return 0;
    for (size_t i = 0; i < 7; ++i)
    {
        const uint32_t value = u32(p + 4 + i * 4);
        if (i == 5u)
        {
            if (value != (s->version == 1u ? 16u : 256u))
                return 0;
        }
        else if (value != expected[i])
            return 0;
    }
    st->steering_response_count = u32(p + 32);
    if (st->steering_response_count != 4u && st->steering_response_count != 16u)
        return 0;
    p += 36;
    if (!(copy_i32(&p, end, st->sine, 1024) && copy_i16(&p, end, st->auto_acceleration, 64) &&
          copy_i16(&p, end, st->brake, 8) && copy_i16(&p, end, st->brake_accelerate, 8) &&
          copy_i16(&p, end, st->forward_response, 8) &&
          copy_i16(&p, end, &st->local[0][0][0], s->version == 1u ? 16u : 256u) &&
          copy_i16(&p, end, st->steering_delta, 10) &&
          copy_i16(&p, end, st->steering_response, st->steering_response_count)))
        return 0;
    st->track_angle_lut_count = 0u;
    if (s->version >= 3u)
    {
        if (!copy_i16(&p, end, (int16_t *)st->track_angle_lut, 0x4000u))
            return 0;
        st->track_angle_lut_count = 0x4000u;
    }
    st->angle_target_table_count = 0u;
    if (s->version >= 4u)
    {
        if ((size_t)(end - p) < sizeof st->angle_target_table)
            return 0;
        memcpy(st->angle_target_table, p, sizeof st->angle_target_table);
        p += sizeof st->angle_target_table;
        st->angle_target_table_count = sizeof st->angle_target_table;
    }
    st->time_trial_base_speed_q16 = 0;
    if (s->version >= 5u)
    {
        if (!copy_i32(&p, end, &st->time_trial_base_speed_q16, 1u) ||
            !copy_i16(&p, end, st->time_trial_forward_limits, 16u) ||
            !copy_i16(&p, end, st->forward_target_response, 8u) ||
            st->time_trial_base_speed_q16 <= 0 ||
            st->time_trial_base_speed_q16 > INT32_C(0x07e00000))
            return 0;
        for (size_t i = 0u; i < 16u; ++i)
            if (i < 10u ? st->time_trial_forward_limits[i] != -1
                        : st->time_trial_forward_limits[i] < 0)
                return 0;
        for (size_t i = 0u; i < 8u; ++i)
            if (st->forward_target_response[i] > 0)
                return 0;
    }
    st->gp_engine_count = 0u;
    if (s->version >= 6u)
    {
        for (size_t engine = 0u; engine < 3u; ++engine)
        {
            PortableGpEngineProfile *profile = &st->gp_engines[engine];
            if ((size_t)(end - p) < sizeof profile->angle_targets)
                return 0;
            memcpy(profile->angle_targets, p, sizeof profile->angle_targets);
            p += sizeof profile->angle_targets;
            memcpy(profile->cpu.acceleration, profile->angle_targets, 128u);
            memcpy(profile->cpu.target_speeds, profile->angle_targets + 128u, 160u);
            memcpy(profile->cpu.heading_steps, profile->angle_targets + 288u, 40u);
            if (!copy_i32(&p, end, &profile->player_base_speed_q16, 1u) ||
                !copy_i16(&p, end, profile->player_forward_limits, 16u) ||
                profile->player_base_speed_q16 <= 0 ||
                profile->player_base_speed_q16 > INT32_C(0x07e00000))
                return 0;
            for (size_t i = 0u; i < 16u; ++i)
                if (i < 10u ? profile->player_forward_limits[i] != -1
                            : profile->player_forward_limits[i] < 0)
                    return 0;
        }
        st->gp_engine_count = 3u;
    }
    if (s->version >= 7u)
    {
        if (!copy_i16(&p, end, st->source_auto_deceleration, 8u))
            return 0;
        for (unsigned i = 0u; i < 8u; ++i)
            if (st->source_auto_deceleration[i] > 0)
                return 0;
    }
    st->source_auto_deceleration_count = s->version >= 7u ? 8u : 0u;
    st->driver_response_rules_valid = 0u;
    if (s->version >= 8u)
    {
        if (!copy_i16(&p, end, &st->driver_response_rules.rows[0][0], 64u) ||
            !copy_i32(&p, end, st->driver_response_rules.selector_rows, 16u) ||
            !copy_i32(&p, end, st->driver_response_rules.character_offsets, 8u) ||
            !portable_driver_response_rules_valid(&st->driver_response_rules))
            return 0;
        if ((size_t)(end - p) < 64u)
            return 0;
        memcpy(st->driver_roster_rules.rival_classes, p, 64u);
        p += 64u;
        for (unsigned row = 0u; row < 8u; ++row)
        {
            unsigned seen = 0u;
            for (unsigned i = 0u; i < 8u; ++i)
            {
                const uint8_t c = st->driver_roster_rules.rival_classes[row][i];
                if (c > 7u || (seen & (1u << c)))
                    return 0;
                seen |= 1u << c;
            }
        }
        if ((size_t)(end - p) < 16u)
            return 0;
        for (unsigned i = 0u; i < 8u; ++i)
            st->driver_roster_rules.contact_flags[i] = u16(p + i * 2u);
        p += 16u;
        if (!copy_i32(&p, end, st->driver_roster_rules.recovery_acceleration, 8u))
            return 0;
        for (unsigned i = 0u; i < 8u; ++i)
            if (st->driver_roster_rules.recovery_acceleration[i] > 0 ||
                st->driver_roster_rules.recovery_acceleration[i] < -32768)
                return 0;
        if ((size_t)(end - p) < 60u)
            return 0;
        memcpy(st->driver_roster_rules.pacing_progress, p, 36u);
        p += 36u;
        memcpy(st->driver_roster_rules.pacing_route, p, 12u);
        p += 12u;
        memcpy(st->driver_roster_rules.pacing_last_place, p, 12u);
        p += 12u;
        if ((size_t)(end - p) < 16u)
            return 0;
        memcpy(st->driver_roster_rules.contact_script, p, 16u);
        p += 16u;
        for (unsigned i = 0u; i < 8u; ++i)
            if (st->driver_roster_rules.contact_script[i][1] > 127u)
                return 0;
        if (st->driver_roster_rules.contact_script[0][1] == 0u ||
            st->driver_roster_rules.contact_script[6][1] != 0u ||
            st->driver_roster_rules.contact_script[7][1] == 0u)
            return 0;
        st->driver_response_rules_valid = 1u;
    }
    if (p != end)
        return 0;
    if (s->version == 1u)
        for (size_t profile = 0u; profile < 2u; ++profile)
            for (size_t class_index = 0u; class_index < 8u; ++class_index)
                if (profile != 0u || class_index != 0u)
                    memcpy(st->local[profile][class_index], st->local[0][0],
                           sizeof st->local[0][0]);
    return 1;
}


/* Reuse the complete MOT1 decoder: source revisions have variable-width
 * preceding tables, so the rival tail must not acquire a second offset parser.
 * The large decode workspace is transient heap storage, never a stack frame. */
int portable_recovered_pack_rival_driver_ids(const PortableAssetPackView *pack,
                                            uint32_t selected_id, uint32_t ids[8])
{
    PortableCharacterContent selected;
    PortableAssetSectionView motion;
    if (!pack || !ids || portable_content_registry_character(pack, selected_id, &selected) !=
                             PORTABLE_CONTENT_OK || selected.character_class >= 8u ||
        portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_MOTION_TABLES,
                                         selected.motion, &motion) != PORTABLE_ASSET_PACK_OK)
        return 0;
    PortableRecoveredPackStorage *storage = calloc(1, sizeof *storage);
    if (!storage)
        return 0;
    uint32_t resolved[8] = {selected_id};
    unsigned count = 1u;
    int valid = parse_motion(&motion, storage) && storage->driver_response_rules_valid;
    for (unsigned i = 8u; valid && i != 0u; --i)
    {
        const uint8_t donor = storage->driver_roster_rules.rival_classes[selected.character_class][i - 1u];
        if (donor == selected.character_class)
            continue;
        PortableCharacterContent rival;
        if (count >= 8u || portable_content_registry_find_character_class(pack, donor, &rival) !=
                              PORTABLE_CONTENT_OK)
            valid = 0;
        else
            resolved[count++] = rival.id;
    }
    free(storage);
    if (!valid || count != 8u)
        return 0;
    memcpy(ids, resolved, sizeof resolved);
    return 1;
}

static int parse_collision(const PortableAssetSectionView *s, PortableRecoveredPackStorage *st)
{
    static const uint32_t expected[7] = {8, 8, 4, 4, 4, 4, 8};
    const uint8_t *p = s->bytes, *end = s->bytes + s->size;
    if (s->version != 1u || s->size < 32u || u32(p) != COLLISION_MAGIC)
        return 0;
    for (size_t i = 0; i < 7; ++i)
        if (u32(p + 4 + i * 4) != expected[i])
            return 0;
    p += 32;
    if (!(copy_i32(&p, end, st->terrain_x, 8) && copy_i32(&p, end, st->terrain_y, 8) &&
          copy_i16(&p, end, st->vector_x, 4) && copy_i16(&p, end, st->vector_y, 4) &&
          copy_i16(&p, end, st->resultant_x, 4) && copy_i16(&p, end, st->resultant_y, 4) &&
          copy_i32(&p, end, st->normal_response, 8) && p == end))
        return 0;
    st->terrain_effect = (PortableTerrainEffectTableView){st->terrain_x, 8, st->terrain_y, 8};
    st->normal_response_view = (PortableSourceNormalResponseTableView){st->normal_response, 8};
    return 1;
}

static int parse_course_objects(const PortableAssetSectionView *s, PortableRecoveredPackStorage *st,
                                PortableRecoveredRaceView *race)
{
    const uint32_t count = s->size >= 8u ? u32(s->bytes + 4u) : 0u;
    if (s->version != 1u || s->size != 8u + (size_t)count * 12u ||
        u32(s->bytes) != COURSE_OBJECT_MAGIC || count > PORTABLE_RECOVERED_COURSE_OBJECT_CAPACITY)
        return 0;
    for (uint32_t index = 0u; index < count; ++index)
    {
        const uint8_t *record = s->bytes + 8u + (size_t)index * 12u;
        PortableRecoveredCourseObject *object = &st->course_objects[index];
        object->position_x_q16 = (int32_t)u32(record);
        object->position_y_q16 = (int32_t)u32(record + 4u);
        object->kind = u16(record + 8u);
        object->source_parameter = record[10u];
        object->collision_half_extent = record[11u];
        if (object->kind != UINT16_C(0x8800) || object->collision_half_extent != 0x1eu)
            return 0;
    }
    race->course_objects = st->course_objects;
    race->course_object_count = count;
    return 1;
}

static int parse_course_coins(const PortableAssetSectionView *section,
                              PortableRecoveredPackStorage *storage,
                              PortableRecoveredRaceView *race)
{
    const uint32_t count = section->size >= 8u ? u32(section->bytes + 4u) : 0u;
    if (section->version != 1u || section->size != 8u + (size_t)count * 4u ||
        u32(section->bytes) != COURSE_COIN_MAGIC || count > PORTABLE_RECOVERED_COIN_CAPACITY)
        return 0;
    for (uint32_t index = 0u; index < count; ++index)
    {
        const uint8_t *record = section->bytes + 8u + index * 4u;
        PortableRecoveredCoin *coin = &storage->coins[index];
        if (record[0] != UINT8_C(0x80))
            return 0;
        /* loadtrack_loadCoinLocations passes x<<3, y<<3 to sub_8032798;
         * retain that tile origin for the source's track-cell contact. */
        coin->position_x_q16 = (int32_t)((uint32_t)record[1] * 8u) << 16u;
        coin->position_y_q16 = (int32_t)((uint32_t)record[2] * 8u) << 16u;
        coin->source_parameter = record[3];
    }
    race->coins = storage->coins;
    race->coin_count = count;
    return 1;
}

static int parse_gp_grid(const PortableAssetSectionView *section,
                         PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    const size_t count =
        race->rules.progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1 ? 4u : 8u;
    if (section->version != 1u || section->size != 8u + count * 12u ||
        u32(section->bytes) != GP_GRID_MAGIC || u32(section->bytes + 4u) != count)
        return 0;
    for (size_t rank = 0u; rank < count; ++rank)
    {
        const uint8_t *row = section->bytes + 8u + rank * 12u;
        const uint32_t heading = u32(row + 8u);
        if (heading > UINT16_MAX)
            return 0;
        storage->gp_grid[rank] =
            (PortableGpGridRow){(int32_t)u32(row), (int32_t)u32(row + 4u), heading};
    }
    race->gp_grid = storage->gp_grid;
    race->gp_grid_count = count;
    return 1;
}

static int parse_lava_bubbles(const PortableAssetSectionView *section,
                              PortableRecoveredPackStorage *storage,
                              PortableRecoveredRaceView *race)
{
    if ((section->version != 1u && section->version != 2u) || section->size < 8u ||
        u32(section->bytes) != 0x3142564cu)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_LAVA_BUBBLE_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0u; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            (section->version == 1u && p[11] != 0u) ||
            !portable_lava_bubble_init(&storage->lava_bubbles[i], (int32_t)u32(p),
                                       (int32_t)u32(p + 4u), p[10], u16(p + 8u)))
            return 0;
        storage->lava_bubbles[i].placement_order = section->version == 2u ? p[11] : (uint8_t)i;
    }
    race->lava_bubbles = storage->lava_bubbles;
    race->lava_bubble_count = count;
    return 1;
}

static int parse_thwomps(const PortableAssetSectionView *section,
                         PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if ((section->version != 1u && section->version != 2u) || section->size < 8u ||
        u32(section->bytes) != 0x31505754u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_THWOMP_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0u; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            (section->version == 1u && p[11] != 0u) ||
            !portable_thwomp_init(&storage->thwomps[i], (int32_t)u32(p), (int32_t)u32(p + 4u),
                                  p[10], u16(p + 8u)))
            return 0;
        storage->thwomps[i].placement_order = section->version == 2u ? p[11] : (uint8_t)i;
    }
    race->thwomps = storage->thwomps;
    race->thwomp_count = count;
    return 1;
}

static int parse_crabs(const PortableAssetSectionView *section,
                       PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x31425243u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_CRAB_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            !portable_crab_init(&storage->crabs[i], (int32_t)u32(p), (int32_t)u32(p + 4u), p[10],
                                u16(p + 8u)))
            return 0;
        storage->crabs[i].placement_order = p[11];
    }
    race->crabs = storage->crabs;
    race->crab_count = count;
    return 1;
}
static int parse_seagulls(const PortableAssetSectionView *section,
                          PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x314c4753u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_SEAGULL_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            !portable_seagull_init(&storage->seagulls[i], (int32_t)u32(p), (int32_t)u32(p + 4u),
                                   p[10], u16(p + 8u)))
            return 0;
        storage->seagulls[i].placement_order = p[11];
    }
    race->seagulls = storage->seagulls;
    race->seagull_count = count;
    return 1;
}
static int parse_piranhas(const PortableAssetSectionView *section,
                          PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x314e5250u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_PIRANHA_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            !portable_piranha_init(&storage->piranhas[i], (int32_t)u32(p), (int32_t)u32(p + 4u),
                                   p[10], u16(p + 8u)))
            return 0;
        storage->piranhas[i].placement_order = p[11];
    }
    race->piranhas = storage->piranhas;
    race->piranha_count = count;
    return 1;
}
static int parse_snowmen(const PortableAssetSectionView *section,
                         PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x314d4e53u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_SNOWMAN_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            !portable_snowman_init(&storage->snowmen[i], (int32_t)u32(p), (int32_t)u32(p + 4u),
                                   p[10], u16(p + 8u)))
            return 0;
        storage->snowmen[i].placement_order = p[11];
    }
    race->snowmen = storage->snowmen;
    race->snowman_count = count;
    return 1;
}
static int parse_mice(const PortableAssetSectionView *section,
                      PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x3153554du)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_MOUSE_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u || p[11] >= 64u ||
            !portable_mouse_init(&storage->mice[i], (int32_t)u32(p), (int32_t)u32(p + 4u), p[10],
                                 u16(p + 8u)))
            return 0;
        storage->mice[i].placement_order = p[11];
    }
    race->mice = storage->mice;
    race->mouse_count = count;
    return 1;
}
static int parse_thunderclouds(const PortableAssetSectionView *section,
                               PortableRecoveredPackStorage *storage,
                               PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x31444354u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_THUNDERCLOUD_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u || p[11] >= 64u ||
            !portable_thundercloud_init(&storage->thunderclouds[i], (int32_t)u32(p),
                                        (int32_t)u32(p + 4u), p[10], u16(p + 8u)))
            return 0;
        storage->thunderclouds[i].placement_order = p[11];
    }
    race->thunderclouds = storage->thunderclouds;
    race->thundercloud_count = count;
    return 1;
}
static int parse_falling_stars(const PortableAssetSectionView *section,
                               PortableRecoveredPackStorage *storage,
                               PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x31525453u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_FALLING_STAR_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u || p[11] >= 64u ||
            !portable_falling_star_init(&storage->falling_stars[i], (int32_t)u32(p),
                                        (int32_t)u32(p + 4u), p[10], u16(p + 8u)))
            return 0;
        storage->falling_stars[i].placement_order = p[11];
    }
    race->falling_stars = storage->falling_stars;
    race->falling_star_count = count;
    return 1;
}
static int parse_penguins(const PortableAssetSectionView *section,
                          PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x314e4750u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_PENGUIN_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            !portable_penguin_init(&storage->penguins[i], (int32_t)u32(p), (int32_t)u32(p + 4u),
                                   p[10], u16(p + 8u)))
            return 0;
        storage->penguins[i].placement_order = p[11];
    }
    race->penguins = storage->penguins;
    race->penguin_count = count;
    return 1;
}
static int parse_boos(const PortableAssetSectionView *section,
                      PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x314f4f42u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_BOO_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            !portable_boo_init(&storage->boos[i], (int32_t)u32(p), (int32_t)u32(p + 4u), p[10],
                               u16(p + 8u)))
            return 0;
        storage->boos[i].placement_order = p[11];
    }
    race->boos = storage->boos;
    race->boo_count = count;
    return 1;
}
static int parse_cannonballs(const PortableAssetSectionView *section,
                             PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    if (section->version != 1u || section->size < 8u || u32(section->bytes) != 0x314c4243u)
        return 0;
    const uint32_t count = u32(section->bytes + 4u);
    if (count > PORTABLE_CANNONBALL_CAPACITY || section->size != 8u + (size_t)count * 12u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = section->bytes + 8u + i * 12u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u ||
            !portable_cannonball_init(&storage->cannonballs[i], (int32_t)u32(p),
                                      (int32_t)u32(p + 4u), p[10], u16(p + 8u)))
            return 0;
        storage->cannonballs[i].placement_order = p[11];
    }
    race->cannonballs = storage->cannonballs;
    race->cannonball_count = count;
    return 1;
}

/* SCN1/v4 is shared data for the common static-object owner and its art. */
static int parse_course_props(const PortableAssetSectionView *s,
                              PortableRecoveredPackStorage *storage,
                              PortableRecoveredRaceView *race)
{
    if (s->version < 3u)
        return 1; /* legacy presentation-only sections */
    if (s->version != 4u || s->size < 16u || u32(s->bytes) != 0x314e4353u ||
        u32(s->bytes + 12u) != 0u)
        return 0;
    const uint32_t count = u32(s->bytes + 4u), art_count = u32(s->bytes + 8u);
    if (count > PORTABLE_COURSE_PROP_CAPACITY || art_count > 64u ||
        s->size < 16u + (size_t)count * 20u)
        return 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *p = s->bytes + 16u + i * 20u;
        if (u32(p) > 2048u * 65536u || u32(p + 4u) > 2048u * 65536u || p[9] >= art_count ||
            ((u32(p + 12u) >> 16u) == 0xa0u && (uint32_t)p[9] + 1u >= art_count) ||
            ((u32(p + 12u) >> 16u) == 0xa1u && (uint32_t)p[9] + 3u >= art_count) ||
            (u32(p + 12u) == 0x860000u && (uint32_t)p[9] + 1u >= art_count) || p[16] >= 64u ||
            p[17] || u16(p + 18u) ||
            !portable_course_prop_init(&storage->course_props[i], (int32_t)u32(p),
                                       (int32_t)u32(p + 4u), u32(p + 12u), u16(p + 10u), p[9]))
            return 0;
        storage->course_props[i].checkpoint = p[8];
        storage->course_props[i].placement_order = p[16];
    }
    race->course_props = storage->course_props;
    race->course_prop_count = count;
    return 1;
}

static int parse_driver_art(const PortableAssetPackView *pack,
                            PortableRecoveredPackStorage *storage, PortableRecoveredRaceView *race)
{
    PortableAssetSectionView section_view;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_DRIVER_ART_RULES, 1u, &section_view);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
        return 1;
    if (status != PORTABLE_ASSET_PACK_OK || section_view.version != 1u ||
        section_view.size != 56u || u32(section_view.bytes) != UINT32_C(0x31524144) ||
        u32(section_view.bytes + 4u) != 16u)
        return 0;
    for (unsigned index = 0u; index < 16u; ++index)
    {
        const uint16_t span = u16(section_view.bytes + 8u + index * 2u);
        const uint8_t mask = section_view.bytes[40u + index];
        if (span > 2047u || (mask != 3u && mask != 7u))
            return 0;
        storage->art_motion_rules.speed_spans[index] = span;
        storage->art_motion_rules.refresh_masks[index] = mask;
    }
    race->art_motion_rules = &storage->art_motion_rules;
    return 1;
}

static int parse_driver_affine(const PortableAssetPackView *pack,
                               PortableRecoveredPackStorage *storage,
                               PortableRecoveredRaceView *race)
{
    PortableAssetSectionView section_view;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_DRIVER_AFFINE_RULES, 1u, &section_view);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
        return 1;
    if (status != PORTABLE_ASSET_PACK_OK || section_view.version != 1u || section_view.size < 8u ||
        u32(section_view.bytes) != UINT32_C(0x31524641) || u32(section_view.bytes + 4u) != 5u)
        return 0;
    static const uint8_t modes[5] = {1u, 4u, 5u, 6u, 7u};
    const uint8_t *cursor = section_view.bytes + 8u, *end = section_view.bytes + section_view.size;
    for (size_t table = 0u; table < 5u; ++table)
    {
        if ((size_t)(end - cursor) < 8u)
            return 0;
        const uint32_t mode = u32(cursor), count = u32(cursor + 4u);
        cursor += 8u;
        if (mode != modes[table] || count == 0u || count > 32u ||
            (size_t)(end - cursor) < count * 6u)
            return 0;
        storage->affine_rules.tables[table] = (PortableDriverAffineTable){
            .mode = (uint8_t)mode, .count = (uint8_t)count, .rows = storage->affine_rows[table]};
        for (size_t row = 0u; row < count; ++row, cursor += 6u)
        {
            const uint16_t x = u16(cursor), y = u16(cursor + 2u);
            if (x == 0u || y == 0u || cursor[4] == 0u || cursor[4] > 127u || cursor[5] != 0u)
                return 0;
            storage->affine_rows[table][row] = (PortableDriverAffineRow){
                .inverse_x_q8 = x, .inverse_y_q8 = y, .duration = cursor[4]};
        }
    }
    if (cursor != end)
        return 0;
    race->affine_rules = &storage->affine_rules;
    return 1;
}

/* Optional profiles are decoded before required course sections. Keep this order so
 * malformed packs report the same first failure and leave the same partial view. */
static PortableRecoveredPackStatus load_race_profiles(const PortableAssetPackView *pack,
                                                      const PortableContentSelection *selection,
                                                      PortableRecoveredPackStorage *st,
                                                      PortableRecoveredRaceView *out)
{
    PortableAssetSectionView targets;
    const PortableAssetPackStatus target_status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_TIME_TRIAL_RECORD_DEFAULTS, selection->course.id, &targets);
    if (target_status != PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        if (target_status != PORTABLE_ASSET_PACK_OK || targets.version != 1u ||
            targets.size != 32u || !targets.bytes || u32(targets.bytes) != UINT32_C(0x31525454) ||
            u32(targets.bytes + 4u) != 6u)
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        for (unsigned row = 0u; row < 6u; ++row)
        {
            const uint8_t *record = targets.bytes + 8u + row * 4u;
            if (record[1] != 0u)
                return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
            st->time_trial_record_defaults.rows[row].character_class = record[0];
            st->time_trial_record_defaults.rows[row].centiseconds = u16(record + 2u);
        }
        if (!portable_time_trial_record_defaults_valid(&st->time_trial_record_defaults))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->time_trial_record_defaults = &st->time_trial_record_defaults;
    }
    if (!portable_impact_effect_asset_load(pack, &st->impact_effect_storage,
                                           &out->impact_effect_rules))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!portable_coin_effect_asset_load(pack, &st->coin_effect_storage, &out->coin_effect_rules))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!portable_surface_trail_asset_load(pack, &st->trail_storage, &out->surface_trail_rules))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!portable_recovery_fx_asset_load(pack, &st->recovery_fx_storage, &out->recovery_fx_rules) ||
        !portable_recovery_fx_ice_gate(pack, selection->course.id, &out->icy_recovery))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!portable_weather_asset_load(pack, selection->course.id, &st->weather_storage,
                                     &out->weather_profile))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!portable_finish_asset_load(pack, &st->finish_storage, &out->finish_rules))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!portable_character_view_asset_load(pack, &st->view_rules, &out->view_rules))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!portable_ambient_asset_load(pack, selection, &st->ambient_storage, &out->ambient_rules,
                                     &out->ambient_seed))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView sky_section;
    const PortableAssetPackStatus sky_status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_SKY_GRAPHICS, selection->course.sky, &sky_section);
    if (sky_status == PORTABLE_ASSET_PACK_OK)
    {
        if (portable_sky_asset_open_motion(&sky_section, &st->sky_motion_rules,
                                           &out->sky_motion_rules) != PORTABLE_SKY_ASSET_OK)
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    }
    else if (sky_status != PORTABLE_ASSET_PACK_NOT_FOUND)
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!parse_driver_art(pack, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (!parse_driver_affine(pack, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView hazard_sound;
    const PortableAssetPackStatus hazard_status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_HAZARD_SOUND_RULES, 1u, &hazard_sound);
    st->hazard_sound_rules_valid = 0u;
    if (hazard_status == PORTABLE_ASSET_PACK_OK)
    {
        if (!portable_hazard_sound_rules_load(pack, &st->hazard_sound_rules))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        st->hazard_sound_rules_valid = 1u;
    }
    else if (hazard_status != PORTABLE_ASSET_PACK_NOT_FOUND)
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView engine_sound;
    const PortableAssetPackStatus engine_status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_ENGINE_SOUND_RULES, 1u, &engine_sound);
    if (engine_status != PORTABLE_ASSET_PACK_NOT_FOUND)
    {
        if (engine_status != PORTABLE_ASSET_PACK_OK ||
            !portable_engine_sound_rules_load(pack, &st->engine_sound_rules))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        st->engine_sound_rules_valid = 1u;
    }
    if (!parse_shell_orbit_rules(pack, &st->shell_orbit_rules) ||
        !parse_banana_rules(pack, &st->banana_rules) ||
        !parse_red_shell_rules(pack, &st->red_shell_rules) ||
        !parse_green_shell_rules(pack, &st->green_shell_rules) ||
        !parse_boo_rules(pack, &st->boo_rules) ||
        !parse_item_status_rules(pack, &st->item_status_rules) ||
        !parse_mushroom_rules(pack, &st->mushroom_rules) ||
        !portable_recovered_gp_scoring(pack, &st->gp_scoring) ||
        !parse_player_item_odds(pack, st) || !parse_roulette_rules(pack, st))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    return PORTABLE_RECOVERED_PACK_OK;
}

static PortableRecoveredPackStatus load_course_actors(const PortableAssetPackView *pack,
                                                      const PortableContentSelection *selection,
                                                      PortableRecoveredPackStorage *st,
                                                      PortableRecoveredRaceView *out)
{
    PortableAssetSectionView props;
    if (section(pack, PORTABLE_ASSET_SECTION_COURSE_SCENERY, selection->course.id, &props) &&
        !parse_course_props(&props, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView bubbles;
    if (section(pack, PORTABLE_ASSET_SECTION_LAVA_BUBBLES, selection->course.id, &bubbles) &&
        !parse_lava_bubbles(&bubbles, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView thwomps;
    if (section(pack, PORTABLE_ASSET_SECTION_THWOMPS, selection->course.id, &thwomps) &&
        !parse_thwomps(&thwomps, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView crabs;
    if (section(pack, PORTABLE_ASSET_SECTION_CRABS, selection->course.id, &crabs) &&
        !parse_crabs(&crabs, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView seagulls;
    if (section(pack, PORTABLE_ASSET_SECTION_SEAGULLS, selection->course.id, &seagulls) &&
        !parse_seagulls(&seagulls, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView piranhas;
    if (section(pack, PORTABLE_ASSET_SECTION_PIRANHAS, selection->course.id, &piranhas) &&
        !parse_piranhas(&piranhas, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView snowmen;
    if (section(pack, PORTABLE_ASSET_SECTION_SNOWMEN, selection->course.id, &snowmen) &&
        !parse_snowmen(&snowmen, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView falling_stars;
    if (section(pack, PORTABLE_ASSET_SECTION_FALLING_STARS, selection->course.id, &falling_stars) &&
        !parse_falling_stars(&falling_stars, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView thunderclouds;
    if (section(pack, PORTABLE_ASSET_SECTION_THUNDERCLOUDS, selection->course.id, &thunderclouds) &&
        !parse_thunderclouds(&thunderclouds, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView mice;
    if (section(pack, PORTABLE_ASSET_SECTION_MICE, selection->course.id, &mice) &&
        !parse_mice(&mice, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView penguins;
    if (section(pack, PORTABLE_ASSET_SECTION_PENGUINS, selection->course.id, &penguins) &&
        !parse_penguins(&penguins, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView boos;
    if (section(pack, PORTABLE_ASSET_SECTION_BOOS, selection->course.id, &boos) &&
        !parse_boos(&boos, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    PortableAssetSectionView cannonballs;
    if (section(pack, PORTABLE_ASSET_SECTION_CANNONBALLS, selection->course.id, &cannonballs) &&
        !parse_cannonballs(&cannonballs, st, out))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    return PORTABLE_RECOVERED_PACK_OK;
}

PortableRecoveredPackStatus
portable_recovered_pack_open_race(const PortableAssetPackView *pack,
                                  const PortableContentSelection *selection,
                                  PortableRecoveredPackStorage *st, PortableRecoveredRaceView *out)
{
    PortableAssetSectionView rules_section, route_section, world_section, material_section,
        motion_section, collision_section, objects_section;
    PortableAssetSectionView coins_section = {0}, grid_section = {0};
    int have_coins, have_grid;
    if (pack == NULL || selection == NULL || st == NULL || out == NULL)
        return PORTABLE_RECOVERED_PACK_INVALID_ARGUMENT;
    memset(st, 0, sizeof *st);
    memset(out, 0, sizeof *out);
    /* Profile selection belongs to the effective pack owner. Original never
     * inspects optional modern data, including overrides it does not use. */
    if (pack->driving_profile > 2u)
        return PORTABLE_RECOVERED_PACK_UNSUPPORTED_CONTENT;
    if (pack->driving_profile != 0u)
    {
        PortableAssetSectionView modern;
        const PortableAssetPackStatus found = portable_asset_pack_find_section(
            pack, PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES, 1u, &modern);
        if (found == PORTABLE_ASSET_PACK_NOT_FOUND)
            return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
        if (found != PORTABLE_ASSET_PACK_OK ||
            !portable_modern_drift_asset_open(&modern, &st->modern_drift_rules) ||
            (pack->driving_profile == 2u && st->modern_drift_rules.air.launch_mask == 0u))
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        out->modern_drift_rules = &st->modern_drift_rules;
    }
    PortableRecoveredPackStatus status = load_race_profiles(pack, selection, st, out);
    if (status != PORTABLE_RECOVERED_PACK_OK)
        return status;
    if (!section(pack, PORTABLE_ASSET_SECTION_RACE_RULES, selection->course.rules,
                 &rules_section) ||
        !section(pack, PORTABLE_ASSET_SECTION_ROUTE, selection->course.route, &route_section) ||
        !section(pack, PORTABLE_ASSET_SECTION_WORLD_MAP, selection->course.world, &world_section) ||
        !section(pack, PORTABLE_ASSET_SECTION_MATERIAL_FLAGS, selection->course.material,
                 &material_section) ||
        !section(pack, PORTABLE_ASSET_SECTION_MOTION_TABLES, selection->character.motion,
                 &motion_section) ||
        !section(pack, PORTABLE_ASSET_SECTION_COLLISION_TABLES, selection->course.collision,
                 &collision_section) ||
        !section(pack, PORTABLE_ASSET_SECTION_COURSE_OBJECTS, selection->course.id,
                 &objects_section))
        return PORTABLE_RECOVERED_PACK_MISSING_SECTION;
    status = load_course_actors(pack, selection, st, out);
    if (status != PORTABLE_RECOVERED_PACK_OK)
        return status;
    have_coins =
        section(pack, PORTABLE_ASSET_SECTION_COURSE_COINS, selection->course.id, &coins_section);
    have_grid = section(pack, PORTABLE_ASSET_SECTION_GP_GRID, selection->course.id, &grid_section);
    if (!parse_rules(&rules_section, &out->rules) ||
        !parse_route(&route_section, st, &out->rules) || !parse_world(&world_section, st) ||
        !parse_material(&material_section, st) || !parse_motion(&motion_section, st) ||
        !parse_collision(&collision_section, st) ||
        !parse_course_objects(&objects_section, st, out) ||
        (have_coins && !parse_course_coins(&coins_section, st, out)) ||
        (have_grid && !parse_gp_grid(&grid_section, st, out)))
        return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
    if (out->rules.compatibility.course_id != selection->course.id ||
        out->rules.compatibility.course_revision != selection->course.revision ||
        out->rules.compatibility.handling_profile_id != 2u ||
        out->rules.compatibility.character_class >= PORTABLE_SOURCE_DRIVER_CLASS_COUNT)
        return PORTABLE_RECOVERED_PACK_UNSUPPORTED_CONTENT;
    if (out->modern_drift_rules != NULL)
    {
        out->rules.compatibility.rules_id = out->modern_drift_rules->profile_id;
        out->rules.compatibility.rules_revision = out->modern_drift_rules->revision;
    }
    /* The v1 course payload was captured with one driver. Driver identity is
     * owned by the selected character manifest, while the source motion asset
     * already contains all eight class rows. */
    out->rules.roster_rules = st->driver_response_rules_valid ? &st->driver_roster_rules : NULL;
    out->rules.source_auto_deceleration_table =
        st->source_auto_deceleration_count ? st->source_auto_deceleration : NULL;
    out->rules.source_auto_deceleration_count = st->source_auto_deceleration_count;
    out->rules.compatibility.character_class = selection->character.character_class;
    {
        PortableCharacterContent stock;
        out->rules.compatibility.roster_driver_id =
            portable_content_registry_find_character_class(
                pack, selection->character.character_class, &stock) == PORTABLE_CONTENT_OK &&
                    stock.id != selection->character.id &&
                    selection->character.id <= PORTABLE_DRIVER_ROSTER_MAX_ID
                ? selection->character.id
                : 0u;
        if (portable_content_registry_voice_driver(pack, selection->character.id, &stock) ==
                PORTABLE_CONTENT_OK &&
            stock.character_class < PORTABLE_SOURCE_DRIVER_CLASS_COUNT &&
            stock.character_class != selection->character.character_class)
            out->voice_override = (uint8_t)(stock.character_class + 1u);
    }
    memcpy(out->rules.compatibility.deterministic_digest, pack->deterministic_digest,
           sizeof out->rules.compatibility.deterministic_digest);
    if (out->modern_drift_rules != NULL)
    {
        /* Selection belongs to replay/ghost identity even if a mod gives its
         * profile the stock rule ID and supplies identical sections in both
         * selections. Keep the imported pack bytes/digest untouched. */
        static const uint8_t domain[] = "MKSC-MODERN-DRIFT-1";
        const uint8_t *parts[] = {domain,pack->deterministic_digest};
        const size_t sizes[] = {sizeof domain-1u,sizeof pack->deterministic_digest};
        portable_asset_pack_sha256(parts,sizes,2u,out->rules.compatibility.deterministic_digest);
    }
    out->dependencies = (PortableDriverPilotDependencies){
        .affine_rules = out->affine_rules,
        .art_motion_rules = out->art_motion_rules,
        .modern_drift_rules = out->modern_drift_rules,
        .auto_acceleration_table = st->auto_acceleration,
        .auto_acceleration_count = 64u,
        .source_auto_deceleration_table =
            st->source_auto_deceleration_count ? st->source_auto_deceleration : NULL,
        .source_auto_deceleration_count = st->source_auto_deceleration_count,
        .brake_table = st->brake,
        .brake_table_count = 8u,
        .brake_accelerate_table = st->brake_accelerate,
        .brake_accelerate_table_count = 8u,
        .forward_response_curve = st->forward_response,
        .forward_response_count = 8u,
        .forward_local_table = st->local[1][out->rules.compatibility.character_class],
        .forward_local_count = 16u,
        .response_rules = st->driver_response_rules_valid ? &st->driver_response_rules : NULL,
        .steering_delta_table = st->steering_delta,
        .steering_delta_count = 10u,
        .steering_response_data = st->steering_response,
        .steering_response_data_count = st->steering_response_count,
        .sine_table = st->sine,
        .sine_count = 1024u,
        .raster = &st->raster,
        .track_material = &st->material,
        .terrain = &st->terrain,
        .terrain_effect_tables = &st->terrain_effect,
        .vector_alternate_x_components = st->vector_x,
        .vector_alternate_x_count = 4u,
        .vector_alternate_y_components = st->vector_y,
        .vector_alternate_y_count = 4u,
        .normal_resultant_x_table = st->resultant_x,
        .normal_resultant_x_count = 4u,
        .normal_resultant_y_table = st->resultant_y,
        .normal_resultant_y_count = 4u,
        .normal_response_table = &st->normal_response_view,
    };
    out->dependencies.impact_effect_rules = out->impact_effect_rules;
    out->dependencies.mushroom_rules =
        portable_mushroom_rules_valid(&st->mushroom_rules) ? &st->mushroom_rules : NULL;
    out->dependencies.item_status_rules =
        portable_item_status_rules_valid(&st->item_status_rules) ? &st->item_status_rules : NULL;
    out->dependencies.boo_rules = portable_boo_rules_valid(&st->boo_rules) ? &st->boo_rules : NULL;
    out->dependencies.green_shell_rules =
        portable_green_shell_rules_valid(&st->green_shell_rules) ? &st->green_shell_rules : NULL;
    out->dependencies.shell_orbit_rules =
        portable_shell_orbit_rules_valid(&st->shell_orbit_rules) ? &st->shell_orbit_rules : NULL;
    out->dependencies.red_shell_rules =
        portable_red_shell_rules_valid(&st->red_shell_rules) ? &st->red_shell_rules : NULL;
    out->dependencies.banana_rules =
        portable_banana_rules_valid(&st->banana_rules) ? &st->banana_rules : NULL;
    out->dependencies.ramp_speed_limit_q16 =
        material_section.version >= 2u ? (int32_t)u32(material_section.bytes + 264u) : 0;
    out->dependencies.owned_terrain_cells = st->terrain_cells;
    out->dependencies.initial_terrain_cells = st->initial_terrain_cells;
    out->dependencies.owned_terrain_capacity = st->terrain.width * st->terrain.height;
    out->dependencies.wall_replacement_tile =
        material_section.version >= 3u ? (uint8_t)u32(material_section.bytes + 268u) : 0u;
    out->dependencies.wall_contact_song =
        material_section.version >= 4u ? (uint16_t)u32(material_section.bytes + 272u) : 0u;
    if (material_section.version >= 12u)
    {
        out->dependencies.spatial_pitch_table = st->spatial_pitch_table;
        out->dependencies.spatial_pitch_count = 33u * 33u;
    }
    memcpy(out->dependencies.mushroom_voice_songs,
           st->mushroom_voice_songs[out->voice_override != 0u
                                        ? out->voice_override - 1u
                                        : out->rules.compatibility.character_class],
           sizeof out->dependencies.mushroom_voice_songs);
    /* C344 initializes normal-size cue58 bit10; GP publishes its live size. */
    out->initial_driver.item_cue_flags_58 = UINT16_C(0x10);
    out->battle_items = st->battle_items_valid ? &st->battle_items : NULL;
    out->gp_scoring = &st->gp_scoring;
    out->hazard_sound_rules = st->hazard_sound_rules_valid ? &st->hazard_sound_rules : NULL;
    out->engine_sound_rules = st->engine_sound_rules_valid ? &st->engine_sound_rules : NULL;
    out->roulette_rules = st->roulette_rules_valid ? &st->roulette_rules : NULL;
    out->gp_player_items = st->gp_player_items_valid ? &st->gp_player_items : NULL;
    out->gp_cpu_items = st->gp_cpu_item_engine_count ? st->gp_cpu_items : NULL;
    out->gp_cpu_item_engine_count = st->gp_cpu_item_engine_count;
    out->gp_engines = st->gp_engine_count ? st->gp_engines : NULL;
    out->gp_engine_count = st->gp_engine_count;
    out->gp_forward_local_table = st->local[0][out->rules.compatibility.character_class];
    out->dependencies.source_track_route = st->route;
    out->dependencies.source_track_route_count = out->rules.route_count;
    out->dependencies.source_track_angle_lut =
        st->track_angle_lut_count != 0u ? st->track_angle_lut : NULL;
    out->dependencies.source_track_angle_lut_count = st->track_angle_lut_count;
    out->dependencies.source_angle_target_table =
        st->angle_target_table_count != 0u ? st->angle_target_table : NULL;
    out->dependencies.source_angle_target_table_count = st->angle_target_table_count;
    if (st->angle_target_table_count >= 0x148u)
    {
        memcpy(st->cpu_tuning.acceleration, st->angle_target_table, 0x80u);
        memcpy(st->cpu_tuning.target_speeds, st->angle_target_table + 0x80u, 0xa0u);
        memcpy(st->cpu_tuning.heading_steps, st->angle_target_table + 0x120u, 0x28u);
        out->rules.cpu_tuning = &st->cpu_tuning;
    }
    out->gp_forward_local_count = 16u;
    out->initial_driver.input.speed_parameter = 5;
    if (motion_section.version >= 5u)
    {
        out->initial_driver.preserve_source_base_speed = 1u;
        out->initial_driver.input.base_speed_q16 = st->time_trial_base_speed_q16;
        out->dependencies.forward_limit_thresholds = st->time_trial_forward_limits;
        out->dependencies.forward_limit_threshold_count = 16u;
        out->dependencies.forward_target_response_table = st->forward_target_response;
        out->dependencies.forward_target_response_count = 8u;
    }
    out->initial_driver.auto_acceleration.character_class =
        (uint8_t)selection->character.character_class;
    out->initial_driver.source_update_order = 1u;
    PortableAssetSectionView recovery;
    if (portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_RECOVERY_MOTION, 1u,
                                         &recovery) == PORTABLE_ASSET_PACK_OK)
    {
        if ((recovery.version < 1u || recovery.version > 3u) || recovery.bytes == NULL ||
            recovery.size < 136u || (recovery.version == 1u && recovery.size != 136u) ||
            u32(recovery.bytes) != UINT32_C(0x31434b4c) || u32(recovery.bytes + 4u) != 16u)
            return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        for (size_t i = 0u; i < 32u; ++i)
        {
            const uint32_t delta = u32(recovery.bytes + 8u + i * 4u);
            if (delta > 64u && delta < UINT32_C(0xffffffc0))
                return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
        }
        out->recovery_bob_offsets = recovery.bytes + 8u;
        if (recovery.version >= 2u)
        {
            if (recovery.size < 152u)
                return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
            const uint8_t *tail = recovery.bytes + 136u;
            PortableGpFinishLakituRules *finish = &st->finish_lakitu_storage;
            finish->row_count = u16(tail + 4u);
            finish->entrance_ticks = u16(tail + 6u);
            finish->bob_ticks = u16(tail + 8u);
            finish->exit_ticks = u16(tail + 10u);
            if (u32(tail) != UINT32_C(0x314b4c46) || u32(tail + 12u) != 0u ||
                finish->row_count > PORTABLE_GP_FINISH_LAKITU_MAX_FRAMES ||
                recovery.size !=
                    152u + finish->row_count * 4u + (recovery.version >= 3u ? 24u : 0u))
                return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
            for (unsigned row = 0u; row < finish->row_count; ++row)
            {
                const uint8_t *frame = tail + 16u + row * 4u;
                if (frame[1] != 0u)
                    return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
                finish->frames[row].art_bank = frame[0];
                finish->frames[row].duration = u16(frame + 2u);
            }
            if (!portable_gp_finish_lakitu_rules_valid(finish))
                return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
            out->finish_lakitu_rules = finish;
            if (recovery.version >= 3u)
            {
                const uint8_t *intro = tail + 16u + finish->row_count * 4u;
                if (u32(intro) != UINT32_C(0x31434149) ||
                    u16(intro + 4u) != PORTABLE_ITEM_ART_INTRO_WAIT_COUNT || u16(intro + 6u) != 0u)
                    return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
                for (unsigned i = 0u; i < PORTABLE_ITEM_ART_INTRO_WAIT_COUNT; ++i)
                    st->item_art_intro_storage.waits[i] = u16(intro + 8u + i * 2u);
                if (!portable_item_art_intro_rules_valid(&st->item_art_intro_storage))
                    return PORTABLE_RECOVERED_PACK_BAD_PAYLOAD;
                out->item_art_intro_rules = &st->item_art_intro_storage;
            }
        }
    }
    out->initial_driver.pose.position_x = out->rules.start_x_q16;
    out->initial_driver.pose.position_y = out->rules.start_y_q16;
    out->initial_driver.motion.angle = (int16_t)out->rules.start_heading;
    return PORTABLE_RECOVERED_PACK_OK;
}

const char *portable_recovered_pack_status_name(PortableRecoveredPackStatus s)
{
    static const char *const n[] = {"ok", "invalid_argument", "missing_section", "bad_payload",
                                    "unsupported_content"};
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "invalid_status";
}
