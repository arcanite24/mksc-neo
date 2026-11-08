#include "mesh_item_inventory.h"
#include "gp_item_inventory.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); return 1; } } while (0)

static PortableMeshItemRules authored_rules(void)
{
    PortableMeshItemRules rules = {.profile_id = 71, .revision = 3};
#define AUTHORED_MINIMUM(name, minimum, maximum) rules.name = minimum;
    PORTABLE_MESH_ITEM_FIELDS(AUTHORED_MINIMUM)
#undef AUTHORED_MINIMUM
    for (unsigned i = 0; i < 10; ++i)
        rules.roulette.preview[i] = (uint16_t)(0x1000u + i);
    for (unsigned i = 0; i < 27; ++i)
        rules.roulette.timeline[i] = 2;
    for (unsigned rank = 0; rank < 8; ++rank)
    {
        rules.player_weights[rank][0] = (PortableGpItemWeight){0x1000, 2};
        rules.player_weights[rank][1] = (PortableGpItemWeight){0x1001, 3};
        rules.player_weights[rank][2] = (PortableGpItemWeight){0x1002, 5};
        rules.cpu_weights[rank][0] = (PortableGpItemWeight){(uint16_t)(0x1004u + rank), 1};
    }
    return rules;
}

static uint32_t next_random(uint32_t seed)
{
    /* Independent wide-arithmetic oracle for the shared source LCG. */
    return (uint32_t)((uint64_t)seed * UINT64_C(1103515245) + 12345u);
}

static int roulette_timing(void)
{
    const PortableMeshItemRules rules = authored_rules();
    CHECK(portable_mesh_item_rules_valid(&rules));
    PortableMeshItemInventory owner;
    CHECK(portable_mesh_item_inventory_init(&owner, 2, 1, UINT32_C(0x12345678)) == PORTABLE_MESH_ITEM_OK);
    PortableMeshItemStep step;
    CHECK(portable_mesh_item_inventory_collect(&owner, &rules, 0, 1, 15, &step) == PORTABLE_MESH_ITEM_OK);
    CHECK(owner.racers[0].held_item == 0 && owner.seed == UINT32_C(0x12345678));
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_STALE);
    CHECK(portable_mesh_item_inventory_collect(&owner, &rules, 1, 1, 9, &step) == PORTABLE_MESH_ITEM_OK);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(owner.racers[1].cpu_choice_delay == 30u);
    PortableMeshItemInput inputs[2] = {
        {.rank = 3, .eligible = 1, .strength_threshold = 3, .item_pressed = 1},
        {.rank = 2, .eligible = 1, .strength_threshold = 3, .cpu_use = 1}};
    uint16_t hud = 999;
    for (unsigned tick = 1; tick <= 6; ++tick)
    {
        CHECK(portable_mesh_item_inventory_tick(&owner, &rules, inputs, 2, &step) == PORTABLE_MESH_ITEM_OK);
        CHECK(step.next.seed == owner.seed && step.use_count == 0);
        CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
        CHECK(owner.racers[0].active && owner.racers[0].timeline_index == (int32_t)(tick / 2u));
        CHECK(owner.racers[1].cpu_choice_delay == 30u - tick);
        CHECK(portable_mesh_item_inventory_hud(&owner, 0, &hud) == PORTABLE_MESH_ITEM_OK);
        if (tick == 1)
            CHECK(hud == rules.roulette.preview[5]);
    }
    /* The early press and continued hold cannot stop or consume the prize.
     * Release at7 and press at8 coincides with a genuine settlement callback. */
    inputs[0].item_pressed = 0;
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, inputs, 2, &step) == PORTABLE_MESH_ITEM_OK);
    CHECK(step.next.seed == owner.seed);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    inputs[0].item_pressed = 1;
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, inputs, 2, &step) == PORTABLE_MESH_ITEM_OK);
    uint32_t expected_seed = next_random(UINT32_C(0x12345678));
    const unsigned sample = (expected_seed & UINT32_C(0x7fffffff)) >> 16u;
    const unsigned draw = sample % 10u;
    const uint16_t expected_item = draw < 2u ? 0x1000u : draw < 5u ? 0x1001u : 0x1002u;
    CHECK(owner.seed == UINT32_C(0x12345678) && owner.racers[0].active);
    CHECK(step.next.seed == expected_seed && step.next.racers[0].held_item == expected_item &&
          !step.next.racers[0].active && step.use_count == 0);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    for (unsigned tick = 9; tick <= 30; ++tick)
    {
        CHECK(portable_mesh_item_inventory_tick(&owner, &rules, inputs, 2, &step) == PORTABLE_MESH_ITEM_OK);
        CHECK(step.next.seed == expected_seed && step.next.racers[1].active);
        CHECK(step.use_count == 0);
        CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    }
    CHECK(owner.racers[1].cpu_choice_delay == 0);
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, inputs, 2, &step) == PORTABLE_MESH_ITEM_OK);
    expected_seed = next_random(expected_seed);
    CHECK(step.next.seed == expected_seed && step.next.racers[1].held_item == 0x1005u &&
          step.use_count == 0);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, inputs, 2, &step) == PORTABLE_MESH_ITEM_OK);
    CHECK(step.use_count == 1 && step.uses[0].racer == 1 && step.uses[0].item == 0x1005u &&
          step.next.seed == expected_seed);
    return 0;
}

static int use_and_refusal(void)
{
    const PortableMeshItemRules rules = authored_rules();
    PortableMeshItemInventory owner;
    CHECK(portable_mesh_item_inventory_init(&owner, 1, 1, 5) == PORTABLE_MESH_ITEM_OK);
    owner.racers[0].held_item = 0x1008u;
    PortableMeshItemInput input = {.rank = 1, .eligible = 1, .item_pressed = 1};
    PortableMeshItemStep step;
    unsigned char saved[sizeof owner];
    memcpy(saved, &owner, sizeof owner);
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_OK);
    CHECK(step.use_count == 1 && step.uses[0].successor == 0x100fu &&
          step.uses[0].inventory_action == PORTABLE_GP_ITEM_INVENTORY_SUCCESSOR);
    CHECK(memcmp(saved, &owner, sizeof owner) == 0); /* Discard on world failure. */
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 2) == PORTABLE_MESH_ITEM_INVALID);
    CHECK(memcmp(saved, &owner, sizeof owner) == 0);
    PortableMeshItemStep corrupted = step;
    corrupted.uses[0].successor = 0x1009;
    CHECK(portable_mesh_item_inventory_commit(&owner, &corrupted, 1) == PORTABLE_MESH_ITEM_INVALID);
    CHECK(memcmp(saved, &owner, sizeof owner) == 0);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(owner.racers[0].held_item == 0x1008u && owner.seed == 5);
    const uint16_t sequence[] = {0x100fu, 0x1007u, 0u};
    for (unsigned i = 0; i < 3; ++i)
    {
        input.item_pressed = 0;
        CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_OK);
        CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
        input.item_pressed = 1;
        CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_OK);
        CHECK(step.use_count == 1);
        CHECK(portable_mesh_item_inventory_commit(&owner, &step, 1) == PORTABLE_MESH_ITEM_OK);
        CHECK(owner.racers[0].held_item == sequence[i] && owner.racers[0].settled_item == sequence[i] &&
              owner.racers[0].preview_item == 0 && owner.seed == 5);
    }
    const uint16_t clear_items[] = {0x1001, 0x1003, 0x1006};
    for (unsigned i = 0; i < 3; ++i)
    {
        owner.racers[0].held_item = clear_items[i];
        owner.racers[0].previous_item_pressed = 0;
        CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_OK);
        CHECK(step.use_count == 1 && step.uses[0].inventory_action == PORTABLE_GP_ITEM_INVENTORY_CLEAR &&
              step.uses[0].inert == (clear_items[i] == 0x1006));
        CHECK(portable_mesh_item_inventory_commit(&owner, &step, 1) == PORTABLE_MESH_ITEM_OK);
        CHECK(owner.racers[0].held_item == 0 && owner.seed == 5);
    }
    owner.racers[0].held_item = 0x1009;
    owner.racers[0].previous_item_pressed = 0;
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_OK);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 1) == PORTABLE_MESH_ITEM_OK);
    CHECK(owner.racers[0].held_item == 0x100e);
    for (unsigned press = 0; press < 3; ++press)
    {
        owner.racers[0].previous_item_pressed = 0;
        CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_OK);
        CHECK(step.uses[0].inventory_action == PORTABLE_GP_ITEM_INVENTORY_KEEP);
        CHECK(portable_mesh_item_inventory_commit(&owner, &step, 1) == PORTABLE_MESH_ITEM_OK);
        CHECK(owner.racers[0].held_item == 0x100e && owner.seed == 5);
    }
    return 0;
}

static int threshold_and_invalid(void)
{
    PortableMeshItemRules rules = authored_rules();
    PortableMeshItemInventory owner;
    CHECK(portable_mesh_item_inventory_init(&owner, 1, 0, 19) == PORTABLE_MESH_ITEM_OK);
    rules.cpu_weights[7][0] = (PortableGpItemWeight){0x1001, 1};
    PortableMeshItemStep step;
    CHECK(portable_mesh_item_inventory_collect(&owner, &rules, 0, 1, 0, &step) == PORTABLE_MESH_ITEM_OK);
    CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    PortableMeshItemInput input = {.rank = 8, .strength_threshold = 0};
    for (unsigned tick = 0; tick < 31; ++tick)
    {
        CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_OK);
        CHECK(portable_mesh_item_inventory_commit(&owner, &step, 0) == PORTABLE_MESH_ITEM_OK);
    }
    CHECK(owner.seed == next_random(19) && owner.racers[0].held_item == PORTABLE_GP_ITEM_FALLBACK);
    unsigned char before[sizeof step];
    memset(&step, 0xa5, sizeof step);
    memcpy(before, &step, sizeof step);
    input.rank = 0;
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_INVALID);
    CHECK(memcmp(before, &step, sizeof step) == 0);
    input.rank = 1;
    rules.roulette.timeline[26] = 0;
    CHECK(portable_mesh_item_inventory_tick(&owner, &rules, &input, 1, &step) == PORTABLE_MESH_ITEM_INVALID);
    CHECK(memcmp(before, &step, sizeof step) == 0);
    CHECK(portable_mesh_item_inventory_collect(&owner, &rules, 0, 1, 0, &step) == PORTABLE_MESH_ITEM_INVALID);
    CHECK(memcmp(before, &step, sizeof step) == 0);
    uint16_t hud = 123;
    CHECK(portable_mesh_item_inventory_hud(&owner, 1, &hud) == PORTABLE_MESH_ITEM_INVALID && hud == 123);
    rules = authored_rules();
    CHECK(portable_mesh_item_inventory_collect(&owner, &rules, 0, 0, 0, &step) == PORTABLE_MESH_ITEM_INELIGIBLE);
    CHECK(memcmp(before, &step, sizeof step) == 0);
    CHECK(portable_mesh_item_inventory_init(&owner, 1, 2, 0) == PORTABLE_MESH_ITEM_INVALID);
    return 0;
}

int main(void)
{
    CHECK(roulette_timing() == 0);
    CHECK(use_and_refusal() == 0);
    CHECK(threshold_and_invalid() == 0);
    return 0;
}
