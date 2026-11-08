#include "mkds_rules.h"
#include "mkds_course.h"
#include "mkds_items.h"
#include "mesh_item_rules.h"

#include <string.h>

/* The asset kind of GP scoring (PORTABLE_ASSET_SECTION_GP_SCORING); the
 * importer stays independent of the asset reader. */
#define MKDS_GP_SCORING_KIND 59u

/* ItemSlotList.dat: a table count, then tables of (places, items) bytes and
 * places*items weights stored item by item; each place's weights total 200.
 * Tables 0/1 are the Grand Prix odds of players and CPUs, 2/3 VS, then online
 * and missions; 7/8 and 9/10 are balloon battle and Shine Runners, players then
 * CPUs, over three standings (leading, middle, trailing). Columns follow the
 * game's item order. */
enum
{
    MKDS_SLOT_ITEMS = 18,
    MKDS_SLOT_PLACES = 8,
    MKDS_SLOT_BATTLE_LEVELS = 3,
    MKDS_SLOT_GP_PLAYER = 0,
    MKDS_SLOT_GP_CPU = 1,
    MKDS_SLOT_VS_PLAYER = 2,
    MKDS_SLOT_VS_CPU = 3,
    MKDS_SLOT_BATTLE_PLAYER = 7,
    MKDS_SLOT_BATTLE_CPU = 8
};
static const uint16_t engine_items[MKDS_SLOT_ITEMS] = {
    PORTABLE_MESH_ITEM_GREEN_SHELL,      PORTABLE_MESH_ITEM_RED_SHELL,
    PORTABLE_MESH_ITEM_SINGLE_BANANA,    PORTABLE_MESH_ITEM_FAKE_BOX,
    PORTABLE_MESH_ITEM_MUSHROOM,         PORTABLE_MESH_ITEM_TRIPLE_MUSHROOM,
    PORTABLE_MESH_ITEM_BOMB,             PORTABLE_MESH_ITEM_SPINY_SHELL,
    PORTABLE_MESH_ITEM_LIGHTNING,        PORTABLE_MESH_ITEM_TRIPLE_GREEN,
    PORTABLE_MESH_ITEM_TRIPLE_BANANAS,   PORTABLE_MESH_ITEM_TRIPLE_RED,
    PORTABLE_MESH_ITEM_STAR,             PORTABLE_MESH_ITEM_GOLDEN_MUSHROOM,
    PORTABLE_MESH_ITEM_BULLET,           PORTABLE_MESH_ITEM_BLOOPER,
    PORTABLE_MESH_ITEM_BOO,              0u, /* unused column */
};

/* The weights of table `wanted` (places rows), item-major. */
static const uint8_t *slot_table(RiCtx *ctx, RiData file, unsigned wanted, unsigned want_places)
{
    size_t at = 1u;
    if (file.size < 1u || file.data[0] <= wanted)
        ri_fail(ctx, "MainRace.carc/Item/ItemSlotList.dat lacks odds table %u", wanted);
    for (unsigned table = 0u;; ++table)
    {
        if (at + 2u > file.size)
            ri_fail(ctx, "MainRace.carc/Item/ItemSlotList.dat table %u is truncated", table);
        const unsigned places = file.data[at], items = file.data[at + 1u];
        if (at + 2u + (size_t)places * items > file.size)
            ri_fail(ctx, "MainRace.carc/Item/ItemSlotList.dat table %u is truncated", table);
        if (table == wanted)
        {
            if (places != want_places || items != MKDS_SLOT_ITEMS)
                ri_fail(ctx, "MainRace.carc/Item/ItemSlotList.dat table %u is %ux%u, not %u places x 18 items",
                        table, places, items, want_places);
            return file.data + at + 2u;
        }
        at += 2u + (size_t)places * items;
    }
}

/* One MIR1 rank row from a place of a slot table: its nonzero odds in item order. */
static void slot_row(RiCtx *ctx, const uint8_t *table, unsigned places, unsigned place, MkdsItemOdds row)
{
    unsigned written = 0u;
    memset(row, 0, sizeof(MkdsItemOdds));
    for (unsigned item = 0u; item < MKDS_SLOT_ITEMS; ++item)
    {
        const uint8_t weight = table[item * places + place];
        if (!weight || !engine_items[item])
            continue;
        if (written == PORTABLE_MESH_ITEM_WEIGHT_CAPACITY)
            ri_fail(ctx, "MainRace.carc/Item/ItemSlotList.dat place %u has too many items", place + 1u);
        row[written++] = engine_items[item] | (uint32_t)weight << 16;
    }
}

void mkds_item_odds_section(RiCtx *ctx, uint32_t id, MkdsItemOdds player[PORTABLE_MESH_ITEM_RANKS],
                            MkdsItemOdds cpu[PORTABLE_MESH_ITEM_RANKS])
{
    enum
    {
        HEADER = 8 + PORTABLE_MESH_ITEM_SCALAR_WORDS + 10 + 27
    };
    RiBuf *profile = ri_buf(ctx);
    for (size_t index = 0; index < HEADER; ++index)
        ri_u32(ctx, profile, index == 3 ? id : mkds_items[index]);
    for (unsigned side = 0u; side < 2u; ++side)
        for (unsigned rank = 0u; rank < PORTABLE_MESH_ITEM_RANKS; ++rank)
        {
            const uint32_t *row = side ? cpu[rank] : player[rank];
            if (!row[0])
                ri_fail(ctx, "item rules %08X give rank %u no %s odds", (unsigned)id, rank + 1u,
                        side ? "CPU" : "player");
            for (unsigned slot = 0u; slot < PORTABLE_MESH_ITEM_WEIGHT_CAPACITY; ++slot)
                ri_u32(ctx, profile, row[slot]);
        }
    ri_section(ctx, PORTABLE_MESH_ITEM_RULES_KIND, id, 1, profile);
}

static NdsFs main_race(RiCtx *ctx, const NdsFs *rom)
{
    return nds_narc(ctx, nds_file(ctx, rom, "data/MainRace.carc"), "data/MainRace.carc");
}

/* Battle tables have three standings; ranks map leader, middle, last. */
static unsigned battle_level(unsigned rank)
{
    return rank == 0u ? 0u : rank < 3u ? 1u : 2u;
}

void mkds_items_sections(RiCtx *ctx, const NdsFs *rom)
{
    const NdsFs race = main_race(ctx, rom);
    const RiData slots = nds_file(ctx, &race, "Item/ItemSlotList.dat");
    static const struct
    {
        uint32_t id;
        uint8_t player, cpu, places;
    } profiles[] = {
        {MKDS_ITEMS_ID, MKDS_SLOT_GP_PLAYER, MKDS_SLOT_GP_CPU, MKDS_SLOT_PLACES},
        {MKDS_ITEMS_VS_ID, MKDS_SLOT_VS_PLAYER, MKDS_SLOT_VS_CPU, MKDS_SLOT_PLACES},
        {MKDS_ITEMS_BATTLE_ID, MKDS_SLOT_BATTLE_PLAYER, MKDS_SLOT_BATTLE_CPU, MKDS_SLOT_BATTLE_LEVELS},
    };
    for (size_t p = 0; p < sizeof(profiles) / sizeof(profiles[0]); ++p)
    {
        MkdsItemOdds player[PORTABLE_MESH_ITEM_RANKS], cpu[PORTABLE_MESH_ITEM_RANKS];
        const unsigned places = profiles[p].places;
        const uint8_t *player_table = slot_table(ctx, slots, profiles[p].player, places);
        const uint8_t *cpu_table = slot_table(ctx, slots, profiles[p].cpu, places);
        for (unsigned rank = 0u; rank < PORTABLE_MESH_ITEM_RANKS; ++rank)
        {
            const unsigned place = places == MKDS_SLOT_PLACES ? rank : battle_level(rank);
            slot_row(ctx, player_table, places, place, player[rank]);
            slot_row(ctx, cpu_table, places, place, cpu[rank]);
        }
        mkds_item_odds_section(ctx, profiles[p].id, player, cpu);
    }
}

/* rankpoint.rpt: "NKPT", its size, then 8-byte point rows; the first is the
 * Grand Prix's eight racers (later rows are VS with fewer racers). */
void mkds_cup_scoring_sections(RiCtx *ctx, const NdsFs *rom, unsigned cups)
{
    const NdsFs race = main_race(ctx, rom);
    const RiData points = nds_file(ctx, &race, "rankpoint.rpt");
    if (points.size < 16u || memcmp(points.data, "NKPT", 4) != 0 || ri_le32(points.data + 4) != points.size)
        ri_fail(ctx, "MainRace.carc/rankpoint.rpt is not an NKPT points table");
    for (unsigned place = 0u; place < 8u; ++place)
        if (points.data[8u + place] > 63u)
            ri_fail(ctx, "MainRace.carc/rankpoint.rpt awards %u points for place %u", points.data[8u + place],
                    place + 1u);
    for (unsigned cup = 0u; cup < cups; ++cup)
    {
        RiBuf *scoring = ri_buf(ctx);
        ri_put(ctx, scoring, "GPT1", 4);
        ri_u32(ctx, scoring, 8u);
        ri_put(ctx, scoring, points.data + 8u, 8u);
        ri_u32(ctx, scoring, 3u); /* Retry credits, as the base game. */
        ri_section(ctx, MKDS_GP_SCORING_KIND, MKDS_CUP_ID(cup), 2, scoring);
    }
}
