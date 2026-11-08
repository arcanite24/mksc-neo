#include "mesh_item_inventory.h"
#include "gp_item_inventory.h"
#include <string.h>

static int held_valid(uint16_t item)
{
    return item <= PORTABLE_GP_ITEM_ROULETTE || portable_mesh_item_id_valid(item, 1);
}

static int owner_valid(const PortableMeshItemInventory *owner)
{
    if (!owner || !owner->count || owner->count > PORTABLE_MESH_ITEM_RACERS ||
        (owner->human_mask >> owner->count))
        return 0;
    for (unsigned i = 0; i < owner->count; ++i)
    {
        const PortableMeshItemRacer *racer = &owner->racers[i];
        if (!held_valid(racer->held_item) ||
            (racer->preview_item && !portable_mesh_item_id_valid(racer->preview_item, 1)) ||
            (racer->settled_item && !portable_mesh_item_id_valid(racer->settled_item, 1)) ||
            racer->active > 1u || racer->previous_item_pressed > 1u ||
            racer->active != (racer->held_item == PORTABLE_GP_ITEM_ROULETTE) ||
            racer->roulette_index < 0 || racer->roulette_index > 42 || racer->delay < 0 ||
            racer->delay > 32767 || racer->timeline_index < 0 || racer->timeline_index > 27 ||
            racer->cpu_choice_delay > PORTABLE_GP_ITEM_OPPONENT_DELAY)
            return 0;
    }
    return 1;
}

static PortableGpItemInventory borrowed(const PortableMeshItemInventory *owner, unsigned index,
                                         const PortableMeshItemRules *rules)
{
    const PortableMeshItemRacer *racer = &owner->racers[index];
    const uint8_t human = (owner->human_mask >> index) & 1u;
    const PortableGpItemInventory value = {
        .rules = rules ? &rules->roulette : NULL,
        .held_item_17a = racer->held_item, .preview_item = racer->preview_item,
        .settled_item = racer->settled_item,
        .roulette_index_70 = human ? racer->roulette_index : (int32_t)racer->cpu_choice_delay,
        .delay_74 = racer->delay, .timeline_index_78 = racer->timeline_index,
        .player_owned = human, .active = racer->active};
    return value;
}

static void retain(PortableMeshItemRacer *out, const PortableGpItemInventory *value)
{
    out->held_item = value->held_item_17a;
    out->preview_item = value->preview_item;
    out->settled_item = value->settled_item;
    if (value->player_owned)
        out->roulette_index = value->roulette_index_70;
    else
        out->cpu_choice_delay = (uint32_t)value->roulette_index_70;
    out->delay = value->delay_74;
    out->timeline_index = value->timeline_index_78;
    out->active = value->active;
}

static int same_owner(const PortableMeshItemInventory *a, const PortableMeshItemInventory *b)
{
    if (a->seed != b->seed || a->count != b->count || a->human_mask != b->human_mask)
        return 0;
    for (unsigned i = 0; i < PORTABLE_MESH_ITEM_RACERS; ++i)
    {
        const PortableMeshItemRacer *x = &a->racers[i], *y = &b->racers[i];
        if (x->held_item != y->held_item || x->preview_item != y->preview_item ||
            x->settled_item != y->settled_item || x->roulette_index != y->roulette_index ||
            x->delay != y->delay || x->timeline_index != y->timeline_index ||
            x->cpu_choice_delay != y->cpu_choice_delay || x->active != y->active ||
            x->previous_item_pressed != y->previous_item_pressed)
            return 0;
    }
    return 1;
}

static int use_request(unsigned racer, uint16_t item, PortableMeshItemUse *out)
{
    PortableGpItemUseDispatch logical;
    if (item >= PORTABLE_MESH_ITEM_FAKE_BOX && portable_mesh_item_id_valid(item, 0))
    {
        /* Items beyond the Super Circuit set have no source callback; one use
         * empties the slot (trailing bananas then drop one per press). */
        *out = (PortableMeshItemUse){.item = item, .racer = (uint8_t)racer,
            .inventory_action = (uint8_t)PORTABLE_GP_ITEM_INVENTORY_CLEAR};
        return 1;
    }
    if (!portable_mesh_item_id_valid(item, 1) || !portable_gp_item_use_dispatch(item, &logical))
        return 0;
    /* Callback addresses are deliberately neither executed nor retained. The
     * typed world decides how to realize the canonical item and may refuse. */
    *out = (PortableMeshItemUse){.item = item, .successor = logical.successor_item,
        .racer = (uint8_t)racer, .inventory_action = (uint8_t)logical.inventory_action,
        .inert = item == 0x1006u};
    return 1;
}

PortableMeshItemStatus portable_mesh_item_inventory_init(PortableMeshItemInventory *out,
    unsigned count, unsigned human_mask, uint32_t seed)
{
    if (!out || !count || count > PORTABLE_MESH_ITEM_RACERS || (human_mask >> count))
        return PORTABLE_MESH_ITEM_INVALID;
    const PortableMeshItemInventory value = {.seed = seed, .count = (uint8_t)count,
                                              .human_mask = (uint8_t)human_mask};
    *out = value;
    return PORTABLE_MESH_ITEM_OK;
}

PortableMeshItemStatus portable_mesh_item_inventory_collect(const PortableMeshItemInventory *owner,
    const PortableMeshItemRules *rules, unsigned racer, int eligible, uint16_t roulette_seed,
    PortableMeshItemStep *out)
{
    if (!owner_valid(owner) || !portable_mesh_item_rules_valid(rules) || !out || racer >= owner->count)
        return PORTABLE_MESH_ITEM_INVALID;
    if (!eligible)
        return PORTABLE_MESH_ITEM_INELIGIBLE;
    PortableGpItemInventory inventory = borrowed(owner, racer, rules);
    const PortableGpItemStatus status = portable_gp_item_begin(&inventory, PORTABLE_GP_ITEM_BOX_KIND,
        0u, inventory.player_owned, roulette_seed);
    if (status != PORTABLE_GP_ITEM_OK)
        return PORTABLE_MESH_ITEM_INELIGIBLE;
    PortableMeshItemStep step = {.before = *owner, .next = *owner};
    retain(&step.next.racers[racer], &inventory);
    *out = step;
    return PORTABLE_MESH_ITEM_OK;
}

PortableMeshItemStatus portable_mesh_item_inventory_tick(const PortableMeshItemInventory *owner,
    const PortableMeshItemRules *rules, const PortableMeshItemInput *inputs, size_t count,
    PortableMeshItemStep *out)
{
    if (!owner_valid(owner) || !portable_mesh_item_rules_valid(rules) || !inputs || !out ||
        count != owner->count)
        return PORTABLE_MESH_ITEM_INVALID;
    for (size_t i = 0; i < count; ++i)
        if (!inputs[i].rank || inputs[i].rank > 8u || inputs[i].item_pressed > 1u ||
            inputs[i].eligible > 1u || inputs[i].cpu_use > 1u)
            return PORTABLE_MESH_ITEM_INVALID;
    PortableMeshItemStep step = {.before = *owner, .next = *owner};
    for (unsigned i = 0; i < owner->count; ++i)
    {
        const PortableMeshItemInput *input = &inputs[i];
        PortableMeshItemRacer *racer = &step.next.racers[i];
        PortableGpItemInventory inventory = borrowed(owner, i, rules);
        const uint8_t pressed = input->item_pressed && !racer->previous_item_pressed;
        racer->previous_item_pressed = input->item_pressed;
        if (inventory.active)
        {
            /* Probe the borrowed owner without consuming RNG. Only a genuine
             * settlement callback samples rank odds; no preview draw occurs. */
            PortableGpItemInventory probe = inventory;
            const PortableGpItemStatus status = portable_gp_item_tick(&probe, pressed,
                                                                       PORTABLE_GP_ITEM_FALLBACK);
            if (status == PORTABLE_GP_ITEM_ACTIVE)
                inventory = probe;
            else if (status == PORTABLE_GP_ITEM_OK)
            {
                const PortableGpItemWeight *table = inventory.player_owned ?
                    rules->player_weights[input->rank - 1u] : rules->cpu_weights[input->rank - 1u];
                const uint16_t selected = portable_gp_item_select_weighted(table,
                    PORTABLE_MESH_ITEM_WEIGHT_CAPACITY, input->strength_threshold, &step.next.seed);
                if (portable_gp_item_tick(&inventory, pressed, selected) != PORTABLE_GP_ITEM_OK)
                    return PORTABLE_MESH_ITEM_INVALID;
            }
            else
                return PORTABLE_MESH_ITEM_INVALID;
            retain(racer, &inventory);
            continue;
        }
        const int wants_use = inventory.player_owned ? pressed : input->cpu_use;
        if (input->eligible && wants_use && inventory.held_item_17a != PORTABLE_GP_ITEM_NONE)
        {
            if (!use_request(i, inventory.held_item_17a, &step.uses[step.use_count]))
                return PORTABLE_MESH_ITEM_INVALID;
            ++step.use_count;
        }
    }
    *out = step;
    return PORTABLE_MESH_ITEM_OK;
}

PortableMeshItemStatus portable_mesh_item_inventory_hud(const PortableMeshItemInventory *owner,
    unsigned racer, uint16_t *out)
{
    if (!owner_valid(owner) || !out || racer >= owner->count)
        return PORTABLE_MESH_ITEM_INVALID;
    const PortableGpItemInventory inventory = borrowed(owner, racer, NULL);
    *out = portable_gp_item_hud_value(&inventory);
    return PORTABLE_MESH_ITEM_OK;
}

PortableMeshItemStatus portable_mesh_item_inventory_commit(PortableMeshItemInventory *owner,
    const PortableMeshItemStep *step, unsigned accepted_uses)
{
    if (!owner_valid(owner) || !step || !owner_valid(&step->before) || !owner_valid(&step->next) ||
        step->use_count > owner->count || (accepted_uses >> step->use_count) ||
        step->next.count != owner->count || step->next.human_mask != owner->human_mask)
        return PORTABLE_MESH_ITEM_INVALID;
    if (!same_owner(owner, &step->before))
        return PORTABLE_MESH_ITEM_STALE;
    PortableMeshItemInventory next = step->next;
    unsigned seen = 0;
    for (unsigned i = 0; i < step->use_count; ++i)
    {
        const PortableMeshItemUse *use = &step->uses[i];
        PortableMeshItemUse expected;
        if (use->racer >= owner->count || (seen & (1u << use->racer)) ||
            next.racers[use->racer].held_item != use->item ||
            !use_request(use->racer, use->item, &expected) ||
            expected.successor != use->successor || expected.inventory_action != use->inventory_action ||
            expected.inert != use->inert)
            return PORTABLE_MESH_ITEM_INVALID;
        seen |= 1u << use->racer;
        if (!(accepted_uses & (1u << i)))
            continue;
        PortableMeshItemRacer *racer = &next.racers[use->racer];
        if (use->inventory_action == PORTABLE_GP_ITEM_INVENTORY_CLEAR)
            racer->held_item = 0;
        else if (use->inventory_action == PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR)
            racer->held_item = use->successor;
        racer->preview_item = 0;
        racer->settled_item = racer->held_item;
        racer->active = 0;
    }
    *owner = next;
    return PORTABLE_MESH_ITEM_OK;
}
