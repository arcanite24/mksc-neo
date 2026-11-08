#include "mesh_item_boxes.h"
#include "mesh_item_fixture.h"
#include "mesh_item_rules_asset.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define Q 65536

typedef struct
{
    PortableCourse3DView course;
    uint8_t rows[3][32];
} BoxFixture;

static void fixture(BoxFixture *f, unsigned count)
{
    memset(f, 0, sizeof *f);
    f->course.id = 70001;
    f->course.table_count = 1;
    f->course.tables[0] =
        (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'), count, 32, f->rows[0]};
    for (unsigned i = 0; i < count; ++i)
    {
        test_mir_word(f->rows[i] + 12, Q / 2);
        test_mir_word(f->rows[i] + 20, 3);
    }
}

static void position(BoxFixture *f, unsigned box, int32_t x, int32_t y, int32_t z)
{
    const int32_t xyz[3] = {x, y, z};
    for (unsigned axis = 0; axis < 3; ++axis)
        test_mir_word(f->rows[box] + 4 * axis, (uint32_t)xyz[axis]);
}

static PortableMeshItemBoxSample path(int32_t from, int32_t to, int32_t z)
{
    const PortableMeshItemBoxSample sample = {.before_q16 = {from, 0, z},
                                              .after_q16 = {to, 0, z},
                                              .collision_radius_q16 = Q / 2,
                                              .eligible = 1};
    return sample;
}

static uint32_t random_next(uint32_t value)
{
    return (uint32_t)((uint64_t)value * UINT64_C(1103515245) + 12345u);
}

static uint16_t preview_seed(uint32_t next)
{
    return (uint16_t)(((next & UINT32_C(0x7fffffff)) >> 16u) & 15u);
}

static int init(PortableMeshItemBoxes *boxes, PortableMeshItemInventory *inventory,
                const BoxFixture *f, unsigned racers)
{
    return portable_mesh_item_boxes_init(boxes, &f->course, NULL, 0) == PORTABLE_MESH_ITEM_OK &&
           portable_mesh_item_inventory_init(inventory, racers, (1u << racers) - 1u, 0x12345678u) ==
               PORTABLE_MESH_ITEM_OK;
}

static int contact_and_cooldown(const PortableMeshItemRules *rules)
{
    BoxFixture f;
    fixture(&f, 2);
    position(&f, 1, 0, 0, 10 * Q); /* Stacked road's box must remain untouched. */
    PortableMeshItemBoxes boxes = {0};
    PortableMeshItemInventory inventory = {0};
    CHECK(init(&boxes, &inventory, &f, 1));
    PortableMeshItemBoxSample sample = path(-4 * Q, 4 * Q, 0);
    PortableMeshItemBoxEvents events = {0};
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, &sample, 1, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 1 && events.pickups[0].placement == 0 && events.pickups[0].racer == 0);
    CHECK(events.pickups[0].fraction_q32 == UINT64_C(1610612736)); /* entry at3/8 */
    const uint32_t seed = random_next(0x12345678u);
    CHECK(inventory.seed == seed && inventory.racers[0].active &&
          inventory.racers[0].roulette_index == preview_seed(seed));
    CHECK(boxes.cooldowns[0] == 3 && boxes.cooldowns[1] == 0);
    for (unsigned tick = 1; tick <= 3; ++tick)
    {
        CHECK(portable_mesh_item_inventory_init(&inventory, 1, 1, seed) == PORTABLE_MESH_ITEM_OK);
        CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, &sample, 1,
                                            &events, NULL, 0) == PORTABLE_MESH_ITEM_OK);
        CHECK(events.count == (tick == 3 ? 1 : 0));
        CHECK(boxes.cooldowns[0] == (tick == 3 ? 3 : 3 - tick));
        CHECK(inventory.seed == (tick == 3 ? random_next(seed) : seed));
    }
    /* A stationary center overlap is an entry at0; endpoint contact is inclusive. */
    CHECK(init(&boxes, &inventory, &f, 1));
    sample = path(-2 * Q, -Q, 0);
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, &sample, 1, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 1 && events.pickups[0].fraction_q32 == UINT64_C(4294967296));
    CHECK(init(&boxes, &inventory, &f, 1));
    sample = path(0, 0, 10 * Q);
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, &sample, 1, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 1 && events.pickups[0].placement == 1 &&
          events.pickups[0].fraction_q32 == 0 && boxes.cooldowns[0] == 0);
    return 0;
}

static int global_order(const PortableMeshItemRules *rules)
{
    BoxFixture f;
    fixture(&f, 3);
    position(&f, 0, 2 * Q, 0, 0);
    position(&f, 1, 0, 0, 0);
    position(&f, 2, -2 * Q, 0, 0);
    PortableMeshItemBoxes boxes = {0};
    PortableMeshItemInventory inventory = {0};
    CHECK(init(&boxes, &inventory, &f, 2));
    PortableMeshItemBoxSample samples[2] = {path(-4 * Q, 4 * Q, 0), path(2 * Q, 2 * Q, 0)};
    PortableMeshItemBoxEvents events = {0};
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, samples, 2, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 2 && events.pickups[0].racer == 1 && events.pickups[0].placement == 0 &&
          events.pickups[0].fraction_q32 == 0 && events.pickups[1].racer == 0 &&
          events.pickups[1].placement == 2 &&
          events.pickups[1].fraction_q32 == UINT64_C(536870912));
    uint32_t first = random_next(0x12345678u), second = random_next(first);
    CHECK(inventory.seed == second && inventory.racers[1].roulette_index == preview_seed(first) &&
          inventory.racers[0].roulette_index == preview_seed(second));
    CHECK(boxes.cooldowns[0] == 3 && boxes.cooldowns[1] == 0 && boxes.cooldowns[2] == 3);
    /* All contacts tie: racer0 reserves placement0; racer1 reselects placement1. */
    fixture(&f, 2);
    CHECK(init(&boxes, &inventory, &f, 2));
    samples[0] = samples[1] = path(0, 0, 0);
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, samples, 2, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 2 && events.pickups[0].racer == 0 && events.pickups[0].placement == 0 &&
          events.pickups[1].racer == 1 && events.pickups[1].placement == 1);
    /* One box has only one winner, and an ineligible/busy racer cannot reserve it. */
    fixture(&f, 1);
    CHECK(init(&boxes, &inventory, &f, 2));
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, samples, 2, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 1 && events.pickups[0].racer == 0 && !inventory.racers[1].held_item);
    CHECK(init(&boxes, &inventory, &f, 2));
    inventory.racers[0].held_item = 0x1007;
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, samples, 2, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 1 && events.pickups[0].racer == 1 && inventory.seed == first);
    CHECK(init(&boxes, &inventory, &f, 2));
    samples[0].eligible = samples[1].eligible = 0;
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, samples, 2, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(!events.count && !boxes.cooldowns[0] && inventory.seed == 0x12345678u);
    /* Full eight-racer capacity still admits exactly one winner for one box. */
    CHECK(init(&boxes, &inventory, &f, 8));
    PortableMeshItemBoxSample eight[8];
    for (unsigned i = 0; i < 8; ++i)
        eight[i] = path(0, 0, 0);
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, eight, 8, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 1 && events.pickups[0].racer == 0 && inventory.seed == first);
    for (unsigned i = 1; i < 8; ++i)
        CHECK(!inventory.racers[i].held_item);
    /* An absent ITEM table has a valid empty owner and consumes no random draw. */
    f.course.table_count = 0;
    CHECK(init(&boxes, &inventory, &f, 1));
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, eight, 1, &events,
                                        NULL, 0) == PORTABLE_MESH_ITEM_OK);
    CHECK(!boxes.count && !events.count && inventory.seed == 0x12345678u);
    return 0;
}

static int rollback(const PortableMeshItemRules *rules)
{
    BoxFixture f;
    fixture(&f, 3);
    PortableMeshItemBoxes boxes = {0};
    PortableMeshItemInventory inventory = {0};
    CHECK(init(&boxes, &inventory, &f, 2));
    boxes.cooldowns[1] = 2;
    PortableMeshItemBoxEvents events = {0};
    memset(&events, 0xa5, sizeof events);
    PortableMeshItemBoxSample samples[2] = {path(0, 0, 0), path(0, 0, 0)};
    const PortableMeshItemBoxes saved_boxes = boxes;
    const PortableMeshItemInventory saved_inventory = inventory;
    const PortableMeshItemBoxEvents saved_events = events;
    char error[256];
    for (unsigned failure = 0; failure < 5; ++failure)
    {
        if (failure == 0)
            test_mir_word(f.rows[2] + 12, 0); /* late malformed placement */
        if (failure == 1)
            test_mir_word(f.rows[2] + 20, 65536); /* cooldown cannot narrow */
        if (failure == 2)
            samples[1].after_q16[2] = INT32_MAX;
        if (failure == 3)
            samples[1].collision_radius_q16 = 0;
        if (failure == 4)
            boxes.cooldowns[1] = 4;
        const PortableMeshItemBoxes before_call = boxes;
        CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, samples, 2,
                                            &events, error,
                                            sizeof error) == PORTABLE_MESH_ITEM_INVALID);
        CHECK(error[0] && !memcmp(&boxes, &before_call, sizeof boxes) &&
              !memcmp(&inventory, &saved_inventory, sizeof inventory) &&
              !memcmp(&events, &saved_events, sizeof events));
        boxes = saved_boxes;
        test_mir_word(f.rows[2] + 12, Q / 2);
        test_mir_word(f.rows[2] + 20, 3);
        samples[1] = path(0, 0, 0);
    }
    /* Invalid initialization also leaves the previous owner untouched. */
    test_mir_word(f.rows[2] + 12, 0);
    CHECK(portable_mesh_item_boxes_init(&boxes, &f.course, error, sizeof error) ==
          PORTABLE_MESH_ITEM_INVALID);
    CHECK(!memcmp(&boxes, &saved_boxes, sizeof boxes));
    test_mir_word(f.rows[2] + 12, Q / 2);
    CHECK(portable_mesh_item_boxes_tick(&boxes, &inventory, &f.course, rules, samples, 2, &events,
                                        error, sizeof error) == PORTABLE_MESH_ITEM_OK);
    CHECK(events.count == 2 && boxes.cooldowns[1] == 1 && events.pickups[0].placement == 0 &&
          events.pickups[1].placement == 2);
    return 0;
}

int main(void)
{
    uint8_t bytes[PORTABLE_MESH_ITEM_RULES_BYTES];
    test_mir_payload(bytes);
    const PortableAssetSectionView section = {
        .kind = 103, .content_id = 41, .version = 1, .bytes = bytes, .size = sizeof bytes};
    PortableMeshItemRules rules = {0};
    CHECK(portable_mesh_item_rules_asset_open(&section, &rules));
    CHECK(!contact_and_cooldown(&rules));
    CHECK(!global_order(&rules));
    CHECK(!rollback(&rules));
    puts("portable mesh item boxes: PASS");
    return 0;
}
