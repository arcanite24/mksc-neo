#ifndef PORTABLE_ITEM_ROULETTE_H
#define PORTABLE_ITEM_ROULETTE_H

#include <stdint.h>
#include "battle_ai_rules.h"

/* Imported roulette presentation and contact/status data. */
typedef struct {
    uint16_t preview[10], timeline[27], gap_upper[8];
    uint32_t contact_weight[8][2];
    int8_t lightning_slots[14];
} PortableGpRouletteRules;

/* 44028 consumes value/duration halfwords; normalized content has no pointers. */
typedef struct
{
    uint16_t item;
    uint16_t weight;
} PortableGpItemWeight;

#define PORTABLE_GP_PLAYER_ITEM_TABLE_COUNT 11u
#define PORTABLE_GP_PLAYER_ITEM_TABLE_CAPACITY 16u
typedef struct {
    /* IDs1/2: leader coin-flip tables;3..11: unsigned progress-gap rows. */
    PortableGpItemWeight table[PORTABLE_GP_PLAYER_ITEM_TABLE_COUNT][PORTABLE_GP_PLAYER_ITEM_TABLE_CAPACITY];
} PortableGpPlayerItemRules;

#define PORTABLE_BATTLE_ITEM_TABLE_COUNT 3u
#define PORTABLE_BATTLE_ITEM_TABLE_CAPACITY 10u

typedef struct
{
    /* 43BB0: zero balloons, one balloon, at least two balloons. */
    PortableGpItemWeight table[PORTABLE_BATTLE_ITEM_TABLE_COUNT][PORTABLE_BATTLE_ITEM_TABLE_CAPACITY];
    uint16_t box_hidden_ticks; /* 439FC: wait after shattering, before regrowth. */
    PortableBattleCombatRules combat; /* v16 native item intent; zero disables it. */
    PortableBattleAiRules ai; /* v15 native arena navigation; older packs disable it. */
    uint8_t elimination_ticks; /* B020/AFA4: F2C50 frame duration, then bomb role. */
} PortableBattleItemRules;

#endif
