#include "collision.h"

#include <stddef.h>

static int absolute_difference_exceeds(int32_t a, int32_t b, uint16_t size)
{
    const int64_t difference = (int64_t)a - b;
    const int64_t magnitude = difference < 0 ? -difference : difference;
    const int64_t limit = (int64_t)size << 14;

    return magnitude > limit;
}

static int collision_filter_allows(const PortableCollisionActor *actor,
                                   const PortableCollisionActor *other)
{
    return (actor->filter_flags | other->filter_flags) <= 1u;
}

static void collide_pair(PortableCollisionActor *actor,
                         PortableCollisionActor *other)
{
    if (actor == other)
        return;
    if (absolute_difference_exceeds(actor->pos_x, other->pos_x,
                                    (uint16_t)(actor->size_x + other->size_x)) ||
        absolute_difference_exceeds(actor->pos_y, other->pos_y,
                                    (uint16_t)(actor->size_y + other->size_y)) ||
        absolute_difference_exceeds(actor->pos_z, other->pos_z,
                                    (uint16_t)(actor->size_z + other->size_z)) ||
        !collision_filter_allows(actor, other))
        return;

    if (actor->collide != NULL)
        actor->collide(actor, other, actor->user);
    if (other->collide != NULL)
        other->collide(other, actor, other->user);
}

static void sort_by_checkpoint(PortableCollisionActor **actors,
                               uint8_t count,
                               uint8_t *starts,
                               uint16_t nr_checkpoints)
{
    uint16_t i;
    uint16_t j;
    uint16_t index;

    for (i = 0; i + 1u < count; ++i)
    {
        PortableCollisionActor *selected = actors[i];
        index = i;
        for (j = (uint16_t)(i + 1u); j < count; ++j)
        {
            if (actors[j]->checkpoint_id < selected->checkpoint_id)
            {
                selected = actors[j];
                index = j;
            }
        }
        actors[index] = actors[i];
        actors[i] = selected;
    }

    i = 0;
    index = 0;
    for (j = 0; j < count; ++j)
    {
        while (i <= actors[j]->checkpoint_id &&
               i < PORTABLE_COLLISION_CHECKPOINT_BUCKETS)
            starts[i++] = (uint8_t)index;
        ++index;
    }
    while (i < nr_checkpoints)
        starts[i++] = (uint8_t)index;
}

static void collide_active_active_fast(const PortableCollisionList *list,
                                       const uint8_t *starts,
                                       uint16_t nr_checkpoints)
{
    uint16_t i;

    for (i = 0; i < list->active_count; ++i)
    {
        PortableCollisionActor *actor = list->active[i];
        const int checkpoint = actor->checkpoint_id;
        int start_checkpoint;
        int end_checkpoint;
        uint16_t start_index;
        uint16_t end_index;
        uint16_t j;

        if (checkpoint == 127)
            continue;
        start_checkpoint = checkpoint - 2;
        end_checkpoint = checkpoint + 3;
        if (start_checkpoint < 0)
            start_checkpoint += nr_checkpoints;
        if (end_checkpoint >= nr_checkpoints)
            end_checkpoint -= nr_checkpoints;
        start_index = starts[start_checkpoint];
        end_index = starts[end_checkpoint];
        if (start_index <= i)
            start_index = (uint16_t)(i + 1u);
        if (start_index < end_index)
        {
            for (j = start_index; j < end_index; ++j)
                collide_pair(actor, list->active[j]);
        }
        else
        {
            for (j = (uint16_t)(i + 1u); j < end_index; ++j)
                collide_pair(actor, list->active[j]);
            for (j = start_index; j < list->active_count; ++j)
                collide_pair(actor, list->active[j]);
        }
    }
}

static void collide_active_active_full(const PortableCollisionList *list)
{
    uint16_t i;

    for (i = 0; i < list->active_count; ++i)
    {
        uint16_t j;

        for (j = (uint16_t)(i + 1u); j < list->active_count; ++j)
            collide_pair(list->active[i], list->active[j]);
    }
}

static void collide_active_passive_fast(const PortableCollisionList *list,
                                        const uint8_t *starts,
                                        uint16_t nr_checkpoints)
{
    uint16_t i;

    for (i = 0; i < list->active_count; ++i)
    {
        PortableCollisionActor *actor = list->active[i];
        const int checkpoint = actor->checkpoint_id;
        int start_checkpoint;
        int end_checkpoint;
        uint16_t start_index;
        uint16_t end_index;
        uint16_t j;

        if (checkpoint == 127)
            continue;
        start_checkpoint = checkpoint - 2;
        end_checkpoint = checkpoint + 3;
        if (start_checkpoint < 0)
            start_checkpoint += nr_checkpoints;
        if (end_checkpoint >= nr_checkpoints)
            end_checkpoint -= nr_checkpoints;
        start_index = starts[start_checkpoint];
        end_index = starts[end_checkpoint];
        if (start_index < end_index)
        {
            for (j = start_index; j < end_index; ++j)
                collide_pair(actor, list->passive[j]);
        }
        else
        {
            for (j = 0; j < end_index; ++j)
                collide_pair(actor, list->passive[j]);
            for (j = start_index; j < list->passive_count; ++j)
                collide_pair(actor, list->passive[j]);
        }
    }
}

static void collide_active_passive_full(const PortableCollisionList *list)
{
    uint16_t i;

    for (i = 0; i < list->active_count; ++i)
    {
        uint16_t j;

        for (j = 0; j < list->passive_count; ++j)
            collide_pair(list->active[i], list->passive[j]);
    }
}

void portable_collision_clear(PortableCollisionList *list)
{
    if (list == NULL)
        return;
    list->active_count = 0;
    list->passive_count = 0;
}

PortableCollisionStatus portable_collision_add_active(
    PortableCollisionList *list,
    PortableCollisionActor *actor)
{
    if (list == NULL || actor == NULL)
        return PORTABLE_COLLISION_INVALID_ARGUMENT;
    if (list->active_count == PORTABLE_COLLISION_MAX_ACTIVE)
        return PORTABLE_COLLISION_CAPACITY_EXCEEDED;
    list->active[list->active_count++] = actor;
    return PORTABLE_COLLISION_OK;
}

PortableCollisionStatus portable_collision_add_passive(
    PortableCollisionList *list,
    PortableCollisionActor *actor)
{
    if (list == NULL || actor == NULL)
        return PORTABLE_COLLISION_INVALID_ARGUMENT;
    if (list->passive_count == PORTABLE_COLLISION_MAX_PASSIVE)
        return PORTABLE_COLLISION_CAPACITY_EXCEEDED;
    list->passive[list->passive_count++] = actor;
    return PORTABLE_COLLISION_OK;
}

PortableCollisionStatus portable_collision_collide(
    PortableCollisionList *list,
    uint16_t nr_checkpoints,
    PortableCollisionMode mode)
{
    uint8_t active_starts[PORTABLE_COLLISION_CHECKPOINT_BUCKETS];
    uint8_t passive_starts[PORTABLE_COLLISION_CHECKPOINT_BUCKETS];
    uint16_t i;

    if (list == NULL || nr_checkpoints == 0 ||
        nr_checkpoints > PORTABLE_COLLISION_CHECKPOINT_BUCKETS)
        return PORTABLE_COLLISION_INVALID_ARGUMENT;
    if (mode != PORTABLE_COLLISION_FAST && mode != PORTABLE_COLLISION_FULL)
        return PORTABLE_COLLISION_INVALID_ARGUMENT;
    if (mode == PORTABLE_COLLISION_FULL)
    {
        collide_active_active_full(list);
        collide_active_passive_full(list);
        return PORTABLE_COLLISION_OK;
    }

    for (i = 0; i < list->active_count; ++i)
    {
        if (list->active[i]->checkpoint_id != 127 &&
            list->active[i]->checkpoint_id >= nr_checkpoints)
            return PORTABLE_COLLISION_INVALID_CHECKPOINT;
    }
    for (i = 0; i < list->passive_count; ++i)
    {
        if (list->passive[i]->checkpoint_id != 127 &&
            list->passive[i]->checkpoint_id >= nr_checkpoints)
            return PORTABLE_COLLISION_INVALID_CHECKPOINT;
    }
    sort_by_checkpoint(list->active, list->active_count, active_starts,
                       nr_checkpoints);
    sort_by_checkpoint(list->passive, list->passive_count, passive_starts,
                       nr_checkpoints);
    collide_active_active_fast(list, active_starts, nr_checkpoints);
    collide_active_passive_fast(list, passive_starts, nr_checkpoints);
    return PORTABLE_COLLISION_OK;
}
