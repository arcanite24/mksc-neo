#include "../core/source_random.h"
#include "gp_item_inventory.h"

uint16_t portable_gp_item_select_player(const PortableGpPlayerItemRules *rules,
    const PortableGpRouletteRules *roulette, uint8_t is_leader,uint32_t progress_gap,int32_t threshold,uint32_t *random_state)
{
    if(random_state==NULL || rules==NULL || roulette==NULL)return 0u;
    size_t gap_index=0u;
    while(gap_index<8u && progress_gap>roulette->gap_upper[gap_index])++gap_index;
    const size_t index=is_leader?((portable_gp_item_random(random_state)&1u)?0u:1u):
        2u+gap_index;
    return portable_gp_item_select_weighted(rules->table[index],
        PORTABLE_GP_PLAYER_ITEM_TABLE_CAPACITY,threshold,random_state);
}

uint16_t portable_gp_item_select_battle(
    const PortableBattleItemRules *rules, uint8_t balloons,
    int32_t strength_threshold, uint32_t *random_state)
{
    if (rules == NULL) return 0u;
    return portable_gp_item_select_weighted(rules->table[balloons > 1u ? 2u : balloons],
        PORTABLE_BATTLE_ITEM_TABLE_CAPACITY, strength_threshold, random_state);
}

int portable_gp_item_use_dispatch(uint16_t item,
                                  PortableGpItemUseDispatch *out)
{
    PortableGpItemUseDispatch result = {
        UINT32_C(0x08048090), 0u, 0u,
        PORTABLE_GP_ITEM_INVENTORY_CLEAR
    };
    if (out == NULL || item == PORTABLE_GP_ITEM_NONE ||
        item == PORTABLE_GP_ITEM_ROULETTE)
        return 0;
    switch (item)
    {
        case 0x1000u: case 0x2000u:
            result.primary_callback = UINT32_C(0x08047368); break;
        case 0x1001u: case 0x2001u:
            result.primary_callback = UINT32_C(0x080473fc); break;
        case 0x1002u: case 0x2002u:
            result.primary_callback = UINT32_C(0x080474cc); break;
        case 0x1003u: case 0x2003u:
            result.primary_callback = UINT32_C(0x08047560);
            result.secondary_callback = UINT32_C(0x08043310); break;
        case 0x1004u: case 0x2004u:
            result.primary_callback = UINT32_C(0x08047630); break;
        case 0x1005u: case 0x2005u:
            break;
        case 0x1006u: case 0x2006u:
            result.primary_callback = UINT32_C(0x08048134); break;
        case 0x1007u: case 0x2007u:
            result.primary_callback = UINT32_C(0x08048294); break;
        case 0x1008u:
            result.primary_callback = UINT32_C(0x08048340);
            result.successor_item = UINT16_C(0x100f);
            result.inventory_action = PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR;
            break;
        case 0x1009u:
            result.primary_callback = UINT32_C(0x080482b8);
            result.successor_item = UINT16_C(0x100e);
            result.inventory_action = PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR;
            break;
        case 0x100au: case 0x200au:
            result.primary_callback = UINT32_C(0x080485dc);
            result.secondary_callback = UINT32_C(0x08043344); break;
        case 0x100bu:
            result.primary_callback = UINT32_C(0x08048694); break;
        case 0x100cu: case 0x200cu:
            result.primary_callback = UINT32_C(0x080483a0);
            result.secondary_callback = UINT32_C(0x08043378); break;
        case 0x100eu:
            result.primary_callback = UINT32_C(0x080482e0);
            result.inventory_action = PORTABLE_GP_ITEM_INVENTORY_KEEP;
            break;
        case 0x100fu:
            result.primary_callback = UINT32_C(0x08048370);
            result.successor_item = UINT16_C(0x1007);
            result.inventory_action = PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR;
            break;
        default:
            break;
    }
    *out = result;
    return 1;
}

uint16_t portable_gp_item_random(uint32_t *state)
{
    return (uint16_t)portable_source_random(state);
}

uint8_t portable_gp_item_strength(uint16_t item)
{
    switch (item)
    {
        case 0x1005u: case 0x1002u: case 0x1000u: case 0x1004u:
        case 0x2002u: case 0x2000u: case 0x2004u:
            return 1u;
        case 0x1001u: case 0x1003u: case 0x2001u: case 0x2003u:
            return 3u;
        default:
            return 0u;
    }
}

uint16_t portable_gp_item_select_weighted(
    const PortableGpItemWeight *table, size_t count, int32_t threshold,
    uint32_t *random_state)
{
    uint32_t total = 0u;
    int32_t remaining;
    uint16_t selected = 0u;

    if (table == NULL || count == 0u || random_state == NULL)
        return 0u;
    for (size_t index = 0u; index < count; ++index)
    {
        if (table[index].item == 0u)
            break;
        total = (uint16_t)(total + table[index].weight);
    }
    remaining = total == 0u ? 0 :
        (int32_t)(portable_gp_item_random(random_state) % total);
    for (size_t index = 0u; index < count; ++index)
    {
        if (table[index].item == 0u)
            break;
        remaining -= table[index].weight;
        selected = table[index].item;
        if (remaining < 0)
            break;
    }
    if ((int32_t)portable_gp_item_strength(selected) <= threshold)
        return selected;
    for (size_t index = 0u; index < count; ++index)
    {
        if (table[index].item == 0u)
            break;
        if ((int32_t)portable_gp_item_strength(table[index].item) <= threshold)
            return table[index].item;
    }
    return PORTABLE_GP_ITEM_FALLBACK;
}

PortableGpItemStatus portable_gp_item_begin(
    PortableGpItemInventory *inventory, uint16_t item_box_kind,
    uint32_t driver_flags, uint8_t player_owned, uint16_t roulette_seed)
{
    if (inventory == NULL || item_box_kind != PORTABLE_GP_ITEM_BOX_KIND)
        return PORTABLE_GP_ITEM_INVALID_ARGUMENT;
    if (inventory->held_item_17a != PORTABLE_GP_ITEM_NONE ||
        (driver_flags & UINT32_C(0x20)) != 0u)
        return PORTABLE_GP_ITEM_INELIGIBLE;
    inventory->held_item_17a = PORTABLE_GP_ITEM_ROULETTE;
    inventory->preview_item = 0u;
    inventory->settled_item = 0u;
    inventory->player_owned = (uint8_t)(player_owned != 0u);
    inventory->active = 1u;
    if (inventory->player_owned)
    {
        inventory->roulette_index_70 = roulette_seed & 0x0fu;
        inventory->delay_74 = 2;
        inventory->timeline_index_78 = 0;
    }
    else
    {
        inventory->roulette_index_70 = PORTABLE_GP_ITEM_OPPONENT_DELAY;
        inventory->delay_74 = 0;
        inventory->timeline_index_78 = 0;
    }
    return PORTABLE_GP_ITEM_OK;
}

PortableGpItemStatus portable_gp_item_tick(
    PortableGpItemInventory *inventory, uint8_t stop_pressed,
    uint16_t selected_item)
{
    if (inventory == NULL || (inventory->player_owned && inventory->rules==NULL))
        return PORTABLE_GP_ITEM_INVALID_ARGUMENT;
    if (!inventory->active ||
        inventory->held_item_17a != PORTABLE_GP_ITEM_ROULETTE)
        return PORTABLE_GP_ITEM_INELIGIBLE;
    if (!inventory->player_owned)
    {
        if (inventory->roulette_index_70 != 0)
        {
            --inventory->roulette_index_70;
            return PORTABLE_GP_ITEM_ACTIVE;
        }
    }
    else
    {
        const int32_t preview_index = inventory->roulette_index_70 %
                                      PORTABLE_GP_ITEM_PREVIEW_COUNT;
        inventory->preview_item = inventory->rules->preview[
            preview_index < 0 ? preview_index + PORTABLE_GP_ITEM_PREVIEW_COUNT
                              : preview_index];
        --inventory->delay_74;
        if (inventory->delay_74 > 0)
            return PORTABLE_GP_ITEM_ACTIVE;
        if (inventory->timeline_index_78 < PORTABLE_GP_ITEM_TIMELINE_COUNT &&
            !(stop_pressed && inventory->timeline_index_78 > 2))
        {
            const int32_t duration =
                inventory->rules->timeline[inventory->timeline_index_78++];
            ++inventory->roulette_index_70;
            inventory->delay_74 = duration;
            return PORTABLE_GP_ITEM_ACTIVE;
        }
    }
    if (selected_item == PORTABLE_GP_ITEM_ROULETTE ||
        (selected_item == 0u && inventory->player_owned))
        return PORTABLE_GP_ITEM_INVALID_ARGUMENT;
    inventory->held_item_17a = selected_item;
    inventory->settled_item = selected_item;
    inventory->active = 0u;
    return PORTABLE_GP_ITEM_OK;
}

uint16_t portable_gp_item_hud_value(
    const PortableGpItemInventory *inventory)
{
    if (inventory == NULL || inventory->held_item_17a == PORTABLE_GP_ITEM_NONE)
        return 0u;
    if (inventory->held_item_17a == PORTABLE_GP_ITEM_ROULETTE)
        return inventory->preview_item;
    return inventory->held_item_17a;
}

const char *portable_gp_item_status_name(PortableGpItemStatus status)
{
    static const char *const names[] = {
        "ok", "invalid_argument", "ineligible", "active"
    };
    return (unsigned)status < sizeof names / sizeof names[0]
        ? names[status] : "invalid_status";
}

static int32_t signed32_bits(uint32_t value)
{
    return value<=INT32_MAX?(int32_t)value:
        -INT32_C(1)-(int32_t)(UINT32_MAX-value);
}

/* CD28: wrapped position differences, whole-world truncation toward zero,
 * wrapped squares, signed range test, then a wrapped halfword result. */
uint16_t portable_gp_item_spatial_volume(int32_t x,int32_t y,int32_t camera_x,int32_t camera_y)
{
    const int32_t dx=signed32_bits((uint32_t)x-(uint32_t)camera_x)/65536;
    const int32_t dy=signed32_bits((uint32_t)y-(uint32_t)camera_y)/65536;
    const int32_t distance=signed32_bits((uint32_t)dx*(uint32_t)dx+
                                        (uint32_t)dy*(uint32_t)dy);
    return distance>INT32_C(0x3ffff)?0u:
        (uint16_t)(INT32_C(256)-distance/1024);
}
