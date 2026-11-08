#ifndef MKSC_PORTABLE_COLLISION_H
#define MKSC_PORTABLE_COLLISION_H

#include <stdint.h>

enum
{
    PORTABLE_COLLISION_MAX_ACTIVE = 24u,
    PORTABLE_COLLISION_MAX_PASSIVE = 64u,
    PORTABLE_COLLISION_CHECKPOINT_BUCKETS = 128u,
};

typedef struct PortableCollisionActor PortableCollisionActor;
typedef void (*PortableCollisionCallback)(PortableCollisionActor *actor,
                                           PortableCollisionActor *other,
                                           void *user);

/* The fields below mirror only the source actor state consumed by recovered
 * object callbacks. They are inert for actors that do not opt into it. */
typedef struct
{
    uint16_t type_flags;
    int16_t field_66;
    int16_t field_6a;
    uint16_t flags;
    uint16_t draw_scale;
    uint32_t active;
    int32_t timer;
} PortableCollisionSourceState;

struct PortableCollisionActor
{
    int32_t pos_x;
    int32_t pos_y;
    int32_t pos_z;
    uint8_t size_x;
    uint8_t size_y;
    uint8_t size_z;
    uint16_t filter_flags;
    uint8_t checkpoint_id;
    uint8_t tag;
    PortableCollisionCallback collide;
    void *user;
    PortableCollisionSourceState source;
};

typedef struct
{
    uint8_t active_count;
    uint8_t passive_count;
    PortableCollisionActor *active[PORTABLE_COLLISION_MAX_ACTIVE];
    PortableCollisionActor *passive[PORTABLE_COLLISION_MAX_PASSIVE];
} PortableCollisionList;

typedef enum
{
    PORTABLE_COLLISION_FAST = 0,
    PORTABLE_COLLISION_FULL,
} PortableCollisionMode;

typedef enum
{
    PORTABLE_COLLISION_OK = 0,
    PORTABLE_COLLISION_INVALID_ARGUMENT,
    PORTABLE_COLLISION_CAPACITY_EXCEEDED,
    PORTABLE_COLLISION_INVALID_CHECKPOINT,
} PortableCollisionStatus;

void portable_collision_clear(PortableCollisionList *list);
PortableCollisionStatus portable_collision_add_active(
    PortableCollisionList *list,
    PortableCollisionActor *actor);
PortableCollisionStatus portable_collision_add_passive(
    PortableCollisionList *list,
    PortableCollisionActor *actor);
PortableCollisionStatus portable_collision_collide(
    PortableCollisionList *list,
    uint16_t nr_checkpoints,
    PortableCollisionMode mode);

#endif
