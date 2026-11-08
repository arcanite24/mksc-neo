#ifndef MKSC_PORTABLE_DRIVER_COLLISION_H
#define MKSC_PORTABLE_DRIVER_COLLISION_H

#include <stddef.h>
#include <stdint.h>

#include "collision.h"
#include "driver_pose.h"
#include "track_object_decoder.h"

enum
{
    PORTABLE_DRIVER_COLLISION_OK = 0,
    PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_COLLISION_DISPATCH_ERROR = 2,
};

typedef struct
{
    PortableCollisionList list;
    PortableCollisionActor kart;
    PortableCollisionActor passive_probe;
    PortableCollisionActor imported_object;
    uint32_t callback_count;
    uint8_t first_other_tag;
    uint32_t imported_trigger_count;
} PortableDriverCollisionState;

/*
 * Register the pilot as one active actor and a caller-positioned passive probe,
 * then run the accepted fast collision dispatch. The probe is an explicit
 * integration seam; it is not imported track hazard geometry.
 */
int portable_driver_collision_tick(
    const PortableDriverPoseState *pose,
    PortableDriverCollisionState *state,
    int32_t passive_x,
    int32_t passive_y,
    uint8_t passive_tag,
    uint16_t nr_checkpoints);

/* Register one source-decoded passive object and reproduce its recovered
 * sub_8059E44 trigger state when the kart overlaps it. */
int portable_driver_collision_tick_imported(
    const PortableDriverPoseState *pose,
    PortableDriverCollisionState *state,
    const PortableTrackObjectActorInit *object_init,
    int32_t object_x,
    int32_t object_y,
    uint8_t object_tag,
    uint16_t nr_checkpoints);

/* Reproduce one bounded sub_8059E00 update for a triggered imported object.
 * The source owner retires the object after the negative-timer state; this
 * helper intentionally stops there rather than guessing that owner path. */
int portable_driver_collision_update_imported(
    PortableDriverCollisionState *state,
    const int32_t *sine_table,
    size_t sine_count);

#endif
