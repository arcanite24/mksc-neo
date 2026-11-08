#include "driver_collision.h"

static int32_t source_sine_times_48(int32_t sine)
{
    const uint32_t bits = (uint32_t)((int64_t)sine * 48);

    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

static void record_probe_callback(PortableCollisionActor *actor,
                                  PortableCollisionActor *other,
                                  void *user)
{
    PortableDriverCollisionState *state = user;

    if (state == NULL || actor == NULL || other == NULL)
        return;
    if (state->callback_count == 0)
        state->first_other_tag = other->tag;
    ++state->callback_count;
}

static void record_imported_object_callback(PortableCollisionActor *actor,
                                             PortableCollisionActor *other,
                                             void *user)
{
    PortableDriverCollisionState *state = user;

    if (actor == NULL || other == NULL || state == NULL)
        return;
    /* sub_8059E44: the other actor must be a live 0x2000-family driver. */
    if ((other->source.type_flags & UINT16_C(0xf000)) !=
            UINT16_C(0x2000) ||
        other->source.field_6a == 0 || actor->source.active != 0)
        return;
    actor->source.active = 1;
    actor->source.timer = 0x20;
    actor->source.draw_scale = UINT16_C(0xff00);
    ++state->imported_trigger_count;
}

static void initialize_kart_actor(const PortableDriverPoseState *pose,
                                  PortableDriverCollisionState *state)
{
    state->kart.pos_x = pose->position_x;
    state->kart.pos_y = pose->position_y;
    state->kart.pos_z = 0;
    state->kart.size_x = 2;
    state->kart.size_y = 2;
    state->kart.size_z = 2;
    state->kart.filter_flags = 0;
    state->kart.checkpoint_id = 0;
    state->kart.tag = 1;
    state->kart.collide = record_probe_callback;
    state->kart.user = state;
}

int portable_driver_collision_tick(
    const PortableDriverPoseState *pose,
    PortableDriverCollisionState *state,
    int32_t passive_x,
    int32_t passive_y,
    uint8_t passive_tag,
    uint16_t nr_checkpoints)
{
    PortableCollisionStatus status;

    if (pose == NULL || state == NULL || nr_checkpoints == 0)
        return PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT;

    portable_collision_clear(&state->list);
    state->callback_count = 0;
    state->first_other_tag = 0;
    initialize_kart_actor(pose, state);

    state->passive_probe.pos_x = passive_x;
    state->passive_probe.pos_y = passive_y;
    state->passive_probe.pos_z = 0;
    state->passive_probe.size_x = 2;
    state->passive_probe.size_y = 2;
    state->passive_probe.size_z = 2;
    state->passive_probe.filter_flags = 0;
    state->passive_probe.checkpoint_id = 0;
    state->passive_probe.tag = passive_tag;
    state->passive_probe.collide = record_probe_callback;
    state->passive_probe.user = state;

    if (portable_collision_add_active(&state->list, &state->kart) !=
            PORTABLE_COLLISION_OK ||
        portable_collision_add_passive(&state->list, &state->passive_probe) !=
            PORTABLE_COLLISION_OK)
        return PORTABLE_DRIVER_COLLISION_DISPATCH_ERROR;
    status = portable_collision_collide(&state->list, nr_checkpoints,
                                        PORTABLE_COLLISION_FAST);
    return status == PORTABLE_COLLISION_OK
               ? PORTABLE_DRIVER_COLLISION_OK
               : PORTABLE_DRIVER_COLLISION_DISPATCH_ERROR;
}

int portable_driver_collision_tick_imported(
    const PortableDriverPoseState *pose,
    PortableDriverCollisionState *state,
    const PortableTrackObjectActorInit *object_init,
    int32_t object_x,
    int32_t object_y,
    uint8_t object_tag,
    uint16_t nr_checkpoints)
{
    PortableCollisionStatus status;

    if (pose == NULL || state == NULL || object_init == NULL ||
        nr_checkpoints == 0 ||
        object_init->kind != PORTABLE_TRACK_OBJECT_ACTOR_INIT_GENERIC_EXTENDED ||
        object_init->collision_kind != PORTABLE_TRACK_OBJECT_COLLISION_8059E44)
        return PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT;

    portable_collision_clear(&state->list);
    state->callback_count = 0;
    state->first_other_tag = 0;
    state->imported_trigger_count = 0;
    initialize_kart_actor(pose, state);
    state->kart.source.type_flags = UINT16_C(0x2001);
    state->kart.source.field_66 = 0;
    state->kart.source.field_6a = 1;

    state->imported_object.pos_x = object_x;
    state->imported_object.pos_y = object_y;
    state->imported_object.pos_z = 0;
    state->imported_object.size_x = object_init->size_x;
    state->imported_object.size_y = object_init->size_y;
    state->imported_object.size_z = object_init->size_z;
    state->imported_object.filter_flags = 0;
    state->imported_object.checkpoint_id = 0;
    state->imported_object.tag = object_tag;
    state->imported_object.collide = record_imported_object_callback;
    state->imported_object.user = state;
    state->imported_object.source.type_flags = object_init->source_actor_type;
    state->imported_object.source.field_66 = 0;
    state->imported_object.source.field_6a = 0;
    state->imported_object.source.flags = object_init->source_flags;
    state->imported_object.source.draw_scale = object_init->draw_scale;
    state->imported_object.source.active = object_init->active;
    state->imported_object.source.timer = object_init->timer;

    if (portable_collision_add_active(&state->list, &state->kart) !=
            PORTABLE_COLLISION_OK ||
        portable_collision_add_passive(&state->list, &state->imported_object) !=
            PORTABLE_COLLISION_OK)
        return PORTABLE_DRIVER_COLLISION_DISPATCH_ERROR;
    status = portable_collision_collide(&state->list, nr_checkpoints,
                                        PORTABLE_COLLISION_FAST);
    return status == PORTABLE_COLLISION_OK
               ? PORTABLE_DRIVER_COLLISION_OK
               : PORTABLE_DRIVER_COLLISION_DISPATCH_ERROR;
}

int portable_driver_collision_update_imported(
    PortableDriverCollisionState *state,
    const int32_t *sine_table,
    size_t sine_count)
{
    int32_t timer;
    uint32_t sine_index;

    if (state == NULL || sine_table == NULL || sine_count < 1024u)
        return PORTABLE_DRIVER_COLLISION_INVALID_ARGUMENT;
    if (state->imported_object.source.active == 0 ||
        state->imported_object.source.timer < 0)
        return PORTABLE_DRIVER_COLLISION_OK;

    timer = state->imported_object.source.timer;
    sine_index = (uint32_t)timer * 16u;
    if ((size_t)sine_index >= sine_count)
        return PORTABLE_DRIVER_COLLISION_DISPATCH_ERROR;

    /* sub_8059E00: flags &= 0xfffd; posZ = gSinTable[timer * 16] * 48. */
    state->imported_object.source.flags &= UINT16_C(0xfffd);
    state->imported_object.pos_z =
        source_sine_times_48(sine_table[sine_index]);
    --state->imported_object.source.timer;
    if (state->imported_object.source.timer < 0)
        state->imported_object.source.flags &= UINT16_C(0xfffe);
    return PORTABLE_DRIVER_COLLISION_OK;
}
