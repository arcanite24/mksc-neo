#ifndef PORTABLE_GP_ITEM_INVENTORY_H
#define PORTABLE_GP_ITEM_INVENTORY_H

#include <stddef.h>
#include <stdint.h>
#include "../core/item_roulette.h"

enum
{
    PORTABLE_GP_ITEM_NONE = 0,
    PORTABLE_GP_ITEM_ROULETTE = 1,
    PORTABLE_GP_ITEM_BOX_KIND = 0x8800,
    PORTABLE_GP_ITEM_FALLBACK = 0x1007,
    PORTABLE_GP_ITEM_OPPONENT_DELAY = 30,
    PORTABLE_GP_ITEM_PREVIEW_COUNT = 10,
    PORTABLE_GP_ITEM_TIMELINE_COUNT = 27
};

typedef enum
{
    PORTABLE_GP_ITEM_OK = 0,
    PORTABLE_GP_ITEM_INVALID_ARGUMENT,
    PORTABLE_GP_ITEM_INELIGIBLE,
    PORTABLE_GP_ITEM_ACTIVE
} PortableGpItemStatus;

typedef enum
{
    PORTABLE_GP_ITEM_INVENTORY_KEEP = 0,
    PORTABLE_GP_ITEM_INVENTORY_CLEAR,
    PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR
} PortableGpItemInventoryAction;

typedef struct
{
    uint32_t primary_callback;
    uint32_t secondary_callback;
    uint16_t successor_item;
    PortableGpItemInventoryAction inventory_action;
} PortableGpItemUseDispatch;

typedef struct
{
    const PortableGpRouletteRules *rules; /* Borrowed content, never serialized. */
    uint16_t held_item_17a;
    uint16_t preview_item;
    uint16_t settled_item;
    int32_t roulette_index_70;
    int32_t delay_74;
    int32_t timeline_index_78;
    uint8_t player_owned;
    uint8_t active;
} PortableGpItemInventory;

uint16_t portable_gp_item_random(uint32_t *state);
uint8_t portable_gp_item_strength(uint16_t item);
uint16_t portable_gp_item_select_weighted(
    const PortableGpItemWeight *table, size_t count, int32_t threshold,
    uint32_t *random_state);
/* Leader selection consumes an extra RNG coin flip. Missing rules reject. */
uint16_t portable_gp_item_select_player(const PortableGpPlayerItemRules *rules,
    const PortableGpRouletteRules *roulette, uint8_t is_leader,uint32_t progress_gap,int32_t threshold,uint32_t *random_state);
/* 43BB0/44028 Battle selection consumes one sample, regardless of rank. */
uint16_t portable_gp_item_select_battle(
    const PortableBattleItemRules *rules, uint8_t balloons,
    int32_t strength_threshold, uint32_t *random_state);
int portable_gp_item_use_dispatch(uint16_t item,
                                  PortableGpItemUseDispatch *out);
PortableGpItemStatus portable_gp_item_begin(
    PortableGpItemInventory *inventory, uint16_t item_box_kind,
    uint32_t driver_flags, uint8_t player_owned, uint16_t roulette_seed);
PortableGpItemStatus portable_gp_item_tick(
    PortableGpItemInventory *inventory, uint8_t stop_pressed,
    uint16_t selected_item);
uint16_t portable_gp_item_hud_value(
    const PortableGpItemInventory *inventory);
const char *portable_gp_item_status_name(PortableGpItemStatus status);

/* CD28 spatial volume from wrapped whole-world distance. */
uint16_t portable_gp_item_spatial_volume(int32_t x,int32_t y,int32_t camera_x,int32_t camera_y);

#endif
