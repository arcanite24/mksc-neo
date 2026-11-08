#ifndef PORTABLE_MKDS_RULES_H
#define PORTABLE_MKDS_RULES_H

/* Race rules Mario Kart DS keeps as data in MainRace.carc: the per-place item
 * odds (Item/ItemSlotList.dat) and the Grand Prix points (rankpoint.rpt). */

#include "nds_fs.h"
#include "mesh_item_rules.h"

/* One rank's odds: up to 16 `engine item | weight << 16` words, zero padded. */
typedef uint32_t MkdsItemOdds[PORTABLE_MESH_ITEM_WEIGHT_CAPACITY];
/* A MIR1 item rules section: the default scalars (mesh-items.json) with these
 * per-rank player and CPU odds. Shared by the DS and N64 importers. */
void mkds_item_odds_section(RiCtx *ctx, uint32_t id, MkdsItemOdds player[PORTABLE_MESH_ITEM_RANKS],
                            MkdsItemOdds cpu[PORTABLE_MESH_ITEM_RANKS]);
/* The DS item rules from ItemSlotList.dat: Grand Prix (MKDS_ITEMS_ID), VS
 * (MKDS_ITEMS_VS_ID) and balloon battle (MKDS_ITEMS_BATTLE_ID). */
void mkds_items_sections(RiCtx *ctx, const NdsFs *rom);
/* One GP scoring section per cup (content ID = its cup ID) holding the
 * eight-racer row of rankpoint.rpt. */
void mkds_cup_scoring_sections(RiCtx *ctx, const NdsFs *rom, unsigned cups);

#endif
