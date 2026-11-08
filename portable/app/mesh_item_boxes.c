#include "mesh_item_boxes.h"
#include "fixed_3d.h"
#include "mesh_query.h"
#include "source_random.h"
#include <stdio.h>

static PortableMeshItemStatus invalid(char *error, size_t capacity, uint32_t course,
                                      const char *field, uint32_t row)
{
    if (error && capacity)
        snprintf(error, capacity, "course %u item boxes: invalid %s row %u", course, field, row);
    return PORTABLE_MESH_ITEM_INVALID;
}

static int placement_valid(const PortableCourse3DView *course, uint32_t index,
                           PortableCourse3DItem *item)
{
    return portable_course_3d_item(course, index, item) &&
           item->radius_q16 <= PORTABLE_FIXED_3D_POSITION_LIMIT &&
           item->respawn_ticks <= UINT16_MAX;
}

static int position_valid(const int32_t point[3])
{
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (point[axis] < -PORTABLE_FIXED_3D_POSITION_LIMIT ||
            point[axis] > PORTABLE_FIXED_3D_POSITION_LIMIT)
            return 0;
    return 1;
}

/* Validate even unavailable placements before staging any RNG or cooldowns. */
static int placements_valid(const PortableCourse3DView *course, uint32_t *count, uint32_t *bad)
{
    if (course->table_count > PORTABLE_COURSE_3D_TABLE_LIMIT)
    {
        *bad = course->table_count;
        return 0;
    }
    const PortableCourse3DTable *table =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'));
    *count = table ? table->count : 0;
    if (*count > PORTABLE_MESH_ITEM_BOX_CAPACITY ||
        (table && (table->stride != 32u || (*count && !table->bytes))))
    {
        *bad = *count;
        return 0;
    }
    for (uint32_t i = 0; i < *count; ++i)
    {
        PortableCourse3DItem item = {0};
        if (!placement_valid(course, i, &item))
        {
            *bad = i;
            return 0;
        }
    }
    return 1;
}

PortableMeshItemStatus portable_mesh_item_boxes_init(PortableMeshItemBoxes *out,
                                                     const PortableCourse3DView *course,
                                                     char *error, size_t capacity)
{
    uint32_t count, bad = 0;
    if (!out || !course || !placements_valid(course, &count, &bad))
        return invalid(error, capacity, course ? course->id : 0, "placement", bad);
    const PortableMeshItemBoxes next = {.count = count};
    *out = next;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_ITEM_OK;
}

typedef struct
{
    uint64_t fraction;
    uint32_t placement;
    uint8_t available;
} BoxCandidate;

/* Called only with fully validated immutable data and bounded sample domains. */
static BoxCandidate earliest(const PortableMeshItemBoxes *boxes, const PortableCourse3DView *course,
                             const PortableMeshItemBoxSample *sample)
{
    BoxCandidate best = {0};
    for (uint32_t i = 0; i < boxes->count; ++i)
    {
        if (boxes->cooldowns[i])
            continue;
        PortableCourse3DItem item = {0};
        (void)portable_course_3d_item(course, i, &item);
        uint64_t fraction;
        const int32_t radius = item.radius_q16 + sample->collision_radius_q16;
        if (portable_mesh_query_segment_sphere(sample->before_q16, sample->after_q16,
                                               item.position_q16, radius,
                                               &fraction) == PORTABLE_MESH_QUERY_HIT &&
            (!best.available || fraction < best.fraction))
            best = (BoxCandidate){.fraction = fraction, .placement = i, .available = 1};
    }
    return best;
}

PortableMeshItemStatus
portable_mesh_item_boxes_tick(PortableMeshItemBoxes *boxes, PortableMeshItemInventory *inventory,
                              const PortableCourse3DView *course,
                              const PortableMeshItemRules *rules,
                              const PortableMeshItemBoxSample *samples, size_t count,
                              PortableMeshItemBoxEvents *events, char *error, size_t capacity)
{
    if (!boxes || !inventory || !course || !samples || !events ||
        !portable_mesh_item_rules_valid(rules) || !count || count > PORTABLE_MESH_ITEM_RACERS ||
        count != inventory->count)
        return invalid(error, capacity, course ? course->id : 0, "arguments", 0);
    uint32_t placed, bad = 0;
    if (!placements_valid(course, &placed, &bad) || boxes->count != placed)
        return invalid(error, capacity, course->id, "placement/count", bad);
    for (unsigned racer = 0; racer < count; ++racer)
    {
        const PortableMeshItemBoxSample *sample = &samples[racer];
        uint16_t hud;
        if (sample->eligible > 1u || sample->collision_radius_q16 <= 0 ||
            sample->collision_radius_q16 > PORTABLE_FIXED_3D_POSITION_LIMIT ||
            !position_valid(sample->before_q16) || !position_valid(sample->after_q16) ||
            portable_mesh_item_inventory_hud(inventory, racer, &hud) != PORTABLE_MESH_ITEM_OK)
            return invalid(error, capacity, course->id, "racer sample/inventory", racer);
    }
    PortableMeshItemBoxes next = *boxes;
    for (uint32_t i = 0; i < PORTABLE_MESH_ITEM_BOX_CAPACITY; ++i)
    {
        if (i >= placed)
        {
            if (next.cooldowns[i])
                return invalid(error, capacity, course->id, "unused cooldown", i);
            continue;
        }
        PortableCourse3DItem item = {0};
        (void)portable_course_3d_item(course, i, &item);
        if (next.cooldowns[i] > item.respawn_ticks)
            return invalid(error, capacity, course->id, "cooldown", i);
        if (next.cooldowns[i])
            --next.cooldowns[i];
    }
    PortableMeshItemInventory staged = *inventory;
    PortableMeshItemBoxEvents picked = {0};
    BoxCandidate candidates[PORTABLE_MESH_ITEM_RACERS] = {{0}};
    for (unsigned racer = 0; racer < count; ++racer)
        if (samples[racer].eligible && !staged.racers[racer].held_item)
            candidates[racer] = earliest(&next, course, &samples[racer]);
    for (;;)
    {
        unsigned winner = (unsigned)count;
        for (unsigned racer = 0; racer < count; ++racer)
            if (candidates[racer].available &&
                (winner == count || candidates[racer].fraction < candidates[winner].fraction))
                winner = racer;
        if (winner == count)
            break;
        const BoxCandidate contact = candidates[winner];
        PortableMeshItemStep step = {0};
        const uint16_t seed = (uint16_t)portable_source_random(&staged.seed);
        if (portable_mesh_item_inventory_collect(&staged, rules, winner, 1, seed, &step) !=
                PORTABLE_MESH_ITEM_OK ||
            portable_mesh_item_inventory_commit(&staged, &step, 0) != PORTABLE_MESH_ITEM_OK)
            return invalid(error, capacity, course->id, "pickup inventory", winner);
        PortableCourse3DItem item = {0};
        (void)portable_course_3d_item(course, contact.placement, &item);
        next.cooldowns[contact.placement] = (uint16_t)item.respawn_ticks;
        picked.pickups[picked.count++] =
            (PortableMeshItemBoxPickup){.placement = contact.placement,
                                        .fraction_q32 = contact.fraction,
                                        .racer = (uint8_t)winner};
        candidates[winner].available = 0;
        /* Only competitors whose best box was reserved need another search.
         * Winners stay disabled, enforcing one pickup per racer per tick. */
        for (unsigned racer = 0; racer < count; ++racer)
            if (candidates[racer].available && candidates[racer].placement == contact.placement)
                candidates[racer] = earliest(&next, course, &samples[racer]);
    }
    *boxes = next;
    *inventory = staged;
    *events = picked;
    if (error && capacity)
        error[0] = 0;
    return PORTABLE_MESH_ITEM_OK;
}
