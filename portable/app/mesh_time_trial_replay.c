#include "mesh_time_trial_replay.h"
#include "mesh_items_state.h"

#include <string.h>

#define STATE_BYTES (PORTABLE_MESH_TIME_TRIAL_STATE_WORDS * 4u)
#define INPUT_MASK                                                                                 \
    (PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_BRAKE | PORTABLE_MESH_HOP | PORTABLE_MESH_RESCUE)
static const uint8_t replay_magic[8] = {'M', 'K', 'S', 'C', '3', 'R', 'P', 'L'};
static const uint8_t ghost_magic[8] = {'M', 'K', 'S', 'C', '3', 'G', 'H', 'O'};

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
static uint64_t read_u64(const uint8_t *p)
{
    return (uint64_t)read_u32(p) | (uint64_t)read_u32(p + 4u) << 32u;
}
static void write_u32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (8u * i));
}
static void write_u64(uint8_t *p, uint64_t value)
{
    write_u32(p, (uint32_t)value);
    write_u32(p + 4u, (uint32_t)(value >> 32u));
}
static int compatibility_valid(const PortableMeshTimeTrialCompatibility *identity)
{
    return identity != NULL && identity->course_id != 0u && identity->driver_id != 0u &&
           identity->profile_id != 0u && identity->profile_revision != 0u &&
           identity->tick_numerator == 4389u && identity->tick_denominator == 262144u;
}
static int session_compatible(const PortableMeshTimeTrialSession *session,
                              const PortableMeshTimeTrialCompatibility *identity)
{
    return session != NULL && session->course != NULL && session->rules != NULL &&
           compatibility_valid(identity) && session->course->id == identity->course_id &&
           session->rules->profile_id == identity->profile_id &&
           session->rules->revision == identity->profile_revision;
}
int portable_mesh_compatibility_write(const PortableMeshTimeTrialCompatibility *identity,
                                      uint8_t bytes[PORTABLE_MESH_COMPATIBILITY_BYTES])
{
    if (bytes == NULL || !compatibility_valid(identity))
        return 0;
    write_u32(bytes, identity->course_id);
    write_u32(bytes + 4u, identity->driver_id);
    write_u32(bytes + 8u, identity->profile_id);
    write_u32(bytes + 12u, identity->profile_revision);
    write_u32(bytes + 16u, identity->tick_numerator);
    write_u32(bytes + 20u, identity->tick_denominator);
    memcpy(bytes + 24u, identity->digest, 32u);
    return 1;
}
int portable_mesh_compatibility_read(PortableMeshTimeTrialCompatibility *identity,
                                     const uint8_t bytes[PORTABLE_MESH_COMPATIBILITY_BYTES])
{
    if (identity == NULL || bytes == NULL)
        return 0;
    PortableMeshTimeTrialCompatibility next = {0};
    next.course_id = read_u32(bytes);
    next.driver_id = read_u32(bytes + 4u);
    next.profile_id = read_u32(bytes + 8u);
    next.profile_revision = read_u32(bytes + 12u);
    next.tick_numerator = read_u32(bytes + 16u);
    next.tick_denominator = read_u32(bytes + 20u);
    memcpy(next.digest, bytes + 24u, 32u);
    if (!compatibility_valid(&next))
        return 0;
    *identity = next;
    return 1;
}
int portable_mesh_compatibility_equal(const PortableMeshTimeTrialCompatibility *a,
                                      const PortableMeshTimeTrialCompatibility *b)
{
    uint8_t left[PORTABLE_MESH_COMPATIBILITY_BYTES], right[PORTABLE_MESH_COMPATIBILITY_BYTES];
    return portable_mesh_compatibility_write(a, left) &&
           portable_mesh_compatibility_write(b, right) && memcmp(left, right, sizeof left) == 0 &&
           a->kart_id == b->kart_id;
}
static PortableMeshReplayStatus read_exact(PortableReplayReader *reader, uint8_t *bytes,
                                           size_t size)
{
    if (reader == NULL || reader->read == NULL || reader->failed)
        return PORTABLE_MESH_REPLAY_IO_ERROR;
    for (size_t i = 0u; i < size; ++i)
    {
        const int result = reader->read(reader->context, &bytes[i]);
        if (result < 0)
        {
            reader->failed = 1;
            return PORTABLE_MESH_REPLAY_IO_ERROR;
        }
        if (result == 0)
            return PORTABLE_MESH_REPLAY_TRUNCATED;
    }
    return PORTABLE_MESH_REPLAY_OK;
}
static PortableMeshReplayStatus write_exact(PortableReplayWriter *writer, const uint8_t *bytes,
                                            size_t size)
{
    if (writer == NULL || writer->write == NULL || writer->failed)
        return PORTABLE_MESH_REPLAY_IO_ERROR;
    if (writer->write(writer->context, (const char *)bytes, size) != size)
    {
        writer->failed = 1;
        return PORTABLE_MESH_REPLAY_IO_ERROR;
    }
    return PORTABLE_MESH_REPLAY_OK;
}
PortableMeshReplayStatus portable_mesh_replay_inspect_header(PortableReplayReader *reader,
                                                             PortableMeshReplayHeader *out)
{
    if (out == NULL)
        return PORTABLE_MESH_REPLAY_INVALID;
    uint8_t bytes[PORTABLE_MESH_REPLAY_HEADER_BYTES];
    PortableMeshReplayStatus status = read_exact(reader, bytes, sizeof bytes);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    PortableMeshReplayHeader next = {0};
    if (memcmp(bytes, replay_magic, 8u) == 0)
        next.ghost = 0u;
    else if (memcmp(bytes, ghost_magic, 8u) == 0)
        next.ghost = 1u;
    else
        return PORTABLE_MESH_REPLAY_INVALID;
    next.version = read_u32(bytes + 8u);
    next.state_words = read_u32(bytes + 72u);
    next.initial_checksum = read_u64(bytes + 80u);
    const uint32_t header_size = read_u32(bytes + 12u);
    if ((next.version != PORTABLE_MESH_REPLAY_VERSION &&
         next.version != PORTABLE_MESH_REPLAY_ITEMS_VERSION &&
         next.version != PORTABLE_MESH_REPLAY_ACTORS_VERSION) ||
        header_size != (uint32_t)(next.version == PORTABLE_MESH_REPLAY_VERSION
                                      ? PORTABLE_MESH_REPLAY_HEADER_BYTES
                                  : next.version == PORTABLE_MESH_REPLAY_ITEMS_VERSION
                                      ? PORTABLE_MESH_REPLAY_ITEMS_HEADER_BYTES
                                      : PORTABLE_MESH_REPLAY_ACTORS_HEADER_BYTES) ||
        next.state_words != PORTABLE_MESH_TIME_TRIAL_STATE_WORDS ||
        !portable_mesh_compatibility_read(&next.compatibility, bytes + 16u))
        return PORTABLE_MESH_REPLAY_INVALID;
    next.compatibility.kart_id = read_u32(bytes + 76u);
    if (next.version >= PORTABLE_MESH_REPLAY_ITEMS_VERSION)
    {
        uint8_t extension[24];
        status = read_exact(reader, extension, sizeof extension);
        if (status != PORTABLE_MESH_REPLAY_OK)
            return status;
        next.item_profile_id = read_u32(extension);
        next.item_profile_revision = read_u32(extension + 4u);
        next.item_seed = read_u32(extension + 8u);
        next.initial_item_checksum = read_u64(extension + 16u);
        const int itemless =
            next.version == PORTABLE_MESH_REPLAY_ACTORS_VERSION && !next.item_profile_id;
        if (read_u32(extension + 12u) ||
            (itemless ? (next.item_profile_revision || next.item_seed || next.initial_item_checksum)
                      : (!next.item_profile_id || next.item_profile_id == UINT32_MAX ||
                         !next.item_profile_revision)))
            return PORTABLE_MESH_REPLAY_INVALID;
    }
    if (next.version == PORTABLE_MESH_REPLAY_ACTORS_VERSION)
    {
        uint8_t extension[16];
        status = read_exact(reader, extension, sizeof extension);
        if (status != PORTABLE_MESH_REPLAY_OK)
            return status;
        next.actor_profile_id = read_u32(extension);
        next.actor_profile_revision = read_u32(extension + 4u);
        next.initial_actor_checksum = read_u64(extension + 8u);
        if (!next.actor_profile_id || next.actor_profile_id == UINT32_MAX ||
            !next.actor_profile_revision)
            return PORTABLE_MESH_REPLAY_INVALID;
    }
    *out = next;
    return PORTABLE_MESH_REPLAY_OK;
}
static int item_binding(const PortableMeshTimeTrialSession *session, const PortableMeshItems *world,
                        const PortableMeshItemsConfig *config)
{
    uint16_t hud;
    return session && world && config && config->course == session->course &&
           config->kart_rules == session->rules && config->geometry == session->geometry &&
           session->mode == 2u && config->mode == 2u && config->count == 1u &&
           config->human_mask == 1u && world->inventory.count == 1u &&
           world->inventory.human_mask == 1u && world->status.count == 1u &&
           world->status.human_mask == 1u && !world->prepared &&
           portable_mesh_kart_rules_valid(session->rules) &&
           portable_mesh_item_rules_valid(config->rules) &&
           portable_mesh_item_rules_fit_profile(config->rules, session->rules) &&
           portable_mesh_items_hud(world, 0u, &hud) == PORTABLE_MESH_ITEMS_OK;
}
static int initial_items(const PortableMeshTimeTrialSession *session,
                         const PortableMeshItems *world, const PortableMeshItemsConfig *config,
                         uint32_t seed, uint64_t expected_checksum)
{
    if (!item_binding(session, world, config))
        return 0;
    PortableMeshItems initial;
    return portable_mesh_items_init(&initial, config, seed, NULL, 0) == PORTABLE_MESH_ITEMS_OK &&
           portable_mesh_items_checksum(&initial) == expected_checksum &&
           portable_mesh_items_checksum(world) == expected_checksum;
}
static int actor_binding(const PortableMeshTimeTrialSession *session,
                         const PortableMeshCourseActors *actors,
                         const PortableMeshCourseActorsConfig *config)
{
    return session && session->course && actors && config &&
           config->course_id == session->course->id &&
           portable_mesh_course_actors_required(config) &&
           portable_mesh_course_actors_count(actors) ==
               portable_mesh_course_actors_config_count(config) &&
           actors->count == config->count && actors->snowman_count == config->snowman_count &&
           actors->penguin_count == config->penguin_count &&
           actors->mouse_count == config->mouse_count &&
           actors->piranha_count == config->piranha_count &&
           actors->seagull_count == config->seagull_count &&
           actors->lava_count == config->lava_count &&
           actors->thwomp_count == config->thwomp_count &&
           actors->cannonball_count == config->cannonball_count &&
           actors->boo_count == config->boo_count && actors->rock_count == config->rock_count &&
           actors->teepee_count == config->teepee_count &&
           actors->falling_star_count == config->falling_star_count &&
           actors->thundercloud_count == config->thundercloud_count &&
           actors->common_prop_count == config->common_prop_count &&
           actors->mechakoopa_count == config->mechakoopa_count &&
           actors->kamek_count == config->kamek_count &&
           actors->pier_boo_count == config->pier_boo_count && config->profile_id &&
           config->profile_id != UINT32_MAX && config->revision &&
           portable_mesh_course_coins_valid(&config->coins, &actors->coins) &&
           portable_mesh_sky_valid(&config->sky, &actors->sky);
}
/* Itemless v3 is reserved for an actual sky-only owner. Interactive source
 * controllers and coins retain their existing MIR/status dependency. */
static int sky_only(const PortableMeshTimeTrialSession *session,
                    const PortableMeshCourseActorsConfig *config)
{
    PortableCourse3DMeta meta;
    if (!session || !session->course || !config || !config->sky.enabled || config->coins.enabled ||
        portable_mesh_course_actors_config_count(config) ||
        !portable_course_3d_meta(session->course, &meta) || meta.property_count > 16384u ||
        meta.property_first > UINT32_MAX - meta.property_count)
        return 0;
    for (uint32_t i = 0; i < meta.property_count; ++i)
    {
        PortableCourse3DProperty property;
        if (!portable_course_3d_property_at(session->course, meta.property_first + i, &property) ||
            !strcmp(property.key, "mksc:item-rules"))
            return 0;
    }
    return 1;
}
static int initial_actors(const PortableMeshTimeTrialSession *session,
                          const PortableMeshCourseActors *actors,
                          const PortableMeshCourseActorsConfig *config, uint64_t checksum)
{
    PortableMeshCourseActors initial;
    return actor_binding(session, actors, config) &&
           portable_mesh_course_actors_init_on_course(config, session->geometry, &initial, NULL, 0) &&
           portable_mesh_course_actors_checksum(&initial) == checksum &&
           portable_mesh_course_actors_checksum(actors) == checksum;
}
static PortableMeshReplayStatus write_header(PortableReplayWriter *writer,
                                             const PortableMeshTimeTrialCompatibility *identity,
                                             const PortableMeshTimeTrialSession *session,
                                             const PortableMeshItems *world,
                                             const PortableMeshItemsConfig *config, uint32_t seed,
                                             const PortableMeshCourseActors *actors,
                                             const PortableMeshCourseActorsConfig *actor_config)
{
    const int items = world != NULL;
    const size_t size = actors  ? PORTABLE_MESH_REPLAY_ACTORS_HEADER_BYTES
                        : items ? PORTABLE_MESH_REPLAY_ITEMS_HEADER_BYTES
                                : PORTABLE_MESH_REPLAY_HEADER_BYTES;
    uint8_t bytes[PORTABLE_MESH_REPLAY_ACTORS_HEADER_BYTES] = {0}, state[STATE_BYTES];
    if (!session_compatible(session, identity) || session->state.tick != 0u ||
        session->state.phase != PORTABLE_MESH_TT_COUNTDOWN ||
        !portable_mesh_compatibility_write(identity, bytes + 16u) ||
        !portable_mesh_time_trial_state_write(&session->state, state, sizeof state))
        return PORTABLE_MESH_REPLAY_INVALID;
    if (items)
    {
        const uint64_t checksum = portable_mesh_items_checksum(world);
        if (!initial_items(session, world, config, seed, checksum))
            return PORTABLE_MESH_REPLAY_INVALID;
        write_u32(bytes + 88u, config->rules->profile_id);
        write_u32(bytes + 92u, config->rules->revision);
        write_u32(bytes + 96u, seed);
        write_u64(bytes + 104u, checksum);
    }
    if (actors)
    {
        const uint64_t checksum = portable_mesh_course_actors_checksum(actors);
        if ((!items && (!sky_only(session, actor_config) || config || seed)) ||
            !initial_actors(session, actors, actor_config, checksum))
            return PORTABLE_MESH_REPLAY_INVALID;
        write_u32(bytes + 112u, actor_config->profile_id);
        write_u32(bytes + 116u, actor_config->revision);
        write_u64(bytes + 120u, checksum);
    }
    memcpy(bytes, replay_magic, sizeof replay_magic);
    write_u32(bytes + 8u, actors  ? PORTABLE_MESH_REPLAY_ACTORS_VERSION
                          : items ? PORTABLE_MESH_REPLAY_ITEMS_VERSION
                                  : PORTABLE_MESH_REPLAY_VERSION);
    write_u32(bytes + 12u, (uint32_t)size);
    write_u32(bytes + 72u, PORTABLE_MESH_TIME_TRIAL_STATE_WORDS);
    write_u32(bytes + 76u, identity->kart_id);
    write_u64(bytes + 80u, portable_mesh_time_trial_checksum(&session->state));
    PortableMeshReplayStatus status = write_exact(writer, bytes, size);
    return status == PORTABLE_MESH_REPLAY_OK ? write_exact(writer, state, sizeof state) : status;
}
PortableMeshReplayStatus
portable_mesh_replay_write_header(PortableReplayWriter *writer,
                                  const PortableMeshTimeTrialCompatibility *identity,
                                  const PortableMeshTimeTrialSession *session)
{
    return write_header(writer, identity, session, NULL, NULL, 0, NULL, NULL);
}
PortableMeshReplayStatus portable_mesh_replay_write_header_items(
    PortableReplayWriter *writer, const PortableMeshTimeTrialCompatibility *identity,
    const PortableMeshTimeTrialSession *session, const PortableMeshItems *world,
    const PortableMeshItemsConfig *config, uint32_t seed)
{
    if (!world || !config)
        return PORTABLE_MESH_REPLAY_INVALID;
    return write_header(writer, identity, session, world, config, seed, NULL, NULL);
}
PortableMeshReplayStatus portable_mesh_replay_write_header_actors(
    PortableReplayWriter *writer, const PortableMeshTimeTrialCompatibility *identity,
    const PortableMeshTimeTrialSession *session, const PortableMeshItems *items,
    const PortableMeshItemsConfig *item_config, uint32_t seed,
    const PortableMeshCourseActors *actors, const PortableMeshCourseActorsConfig *config)
{
    if (!actors || !config || ((items == NULL) != (item_config == NULL)))
        return PORTABLE_MESH_REPLAY_INVALID;
    return write_header(writer, identity, session, items, item_config, seed, actors, config);
}
static int item_controls_valid(PortableMeshItemControls controls)
{
    return controls.item_pressed <= 1u && controls.aim_backward <= 1u && controls.aim_forward <= 1u;
}
static PortableMeshReplayStatus
write_record(PortableReplayWriter *writer, PortableMeshKartInput input,
             PortableMeshItemControls controls, const PortableMeshTimeTrialSession *session,
             const PortableMeshItems *world, const PortableMeshCourseActors *actors, int end)
{
    uint8_t bytes[40] = {0}, state[STATE_BYTES];
    if (!session ||
        (actors && ((!portable_mesh_course_actors_count(actors) && !actors->coins.enabled &&
                     !actors->sky.enabled) ||
                    (!world && (!actors->sky.enabled || actors->coins.enabled ||
                                portable_mesh_course_actors_count(actors))) ||
                    !portable_mesh_course_actors_checksum(actors))) ||
        (actors && !world &&
         (controls.item_pressed || controls.aim_backward || controls.aim_forward)) ||
        (!end && (session->state.tick == 0u || (input.buttons & ~INPUT_MASK))) ||
        (world && (world->prepared || world->inventory.count != 1u ||
                   world->inventory.human_mask != 1u || world->status.count != 1u ||
                   world->status.human_mask != 1u || !item_controls_valid(controls))))
        return PORTABLE_MESH_REPLAY_INVALID;
    if (end)
    {
        if (session->state.phase != PORTABLE_MESH_TT_FINISHED || !session->state.finish_ticks ||
            !session->state.tick || session->state.tick > PORTABLE_MESH_REPLAY_MAX_TICKS ||
            !portable_mesh_time_trial_state_write(&session->state, state, sizeof state))
            return PORTABLE_MESH_REPLAY_INVALID;
        write_u32(bytes, UINT32_MAX);
    }
    else
    {
        if (session->state.tick > PORTABLE_MESH_REPLAY_MAX_TICKS)
            return PORTABLE_MESH_REPLAY_LIMIT;
        write_u32(bytes, session->state.tick);
        bytes[4] = (uint8_t)input.buttons;
        bytes[5] = (uint8_t)(input.buttons >> 8u);
        const uint16_t steering = (uint16_t)input.steering;
        bytes[6] = (uint8_t)steering;
        bytes[7] = (uint8_t)(steering >> 8u);
        if (world)
            write_u32(bytes + 16u, controls.item_pressed | (uint32_t)controls.aim_backward << 1u |
                                       (uint32_t)controls.aim_forward << 2u);
    }
    write_u64(bytes + 8u, portable_mesh_time_trial_checksum(&session->state));
    if (world)
        write_u64(bytes + 24u, portable_mesh_items_checksum(world));
    if (actors)
        write_u64(bytes + 32u, portable_mesh_course_actors_checksum(actors));
    const PortableMeshReplayStatus status = write_exact(writer, bytes,
                                                        actors  ? 40u
                                                        : world ? 32u
                                                                : 16u);
    return status == PORTABLE_MESH_REPLAY_OK && end ? write_exact(writer, state, sizeof state)
                                                    : status;
}
PortableMeshReplayStatus
portable_mesh_replay_write_tick(PortableReplayWriter *writer, PortableMeshKartInput input,
                                const PortableMeshTimeTrialSession *session)
{
    return write_record(writer, input, (PortableMeshItemControls){0}, session, NULL, NULL, 0);
}
PortableMeshReplayStatus portable_mesh_replay_write_tick_items(
    PortableReplayWriter *writer, PortableMeshKartInput input, PortableMeshItemControls controls,
    const PortableMeshTimeTrialSession *session, const PortableMeshItems *world)
{
    if (!world)
        return PORTABLE_MESH_REPLAY_INVALID;
    return write_record(writer, input, controls, session, world, NULL, 0);
}
PortableMeshReplayStatus portable_mesh_replay_write_end(PortableReplayWriter *writer,
                                                        const PortableMeshTimeTrialSession *session)
{
    return write_record(writer, (PortableMeshKartInput){0}, (PortableMeshItemControls){0}, session,
                        NULL, NULL, 1);
}
PortableMeshReplayStatus
portable_mesh_replay_write_end_items(PortableReplayWriter *writer,
                                     const PortableMeshTimeTrialSession *session,
                                     const PortableMeshItems *world)
{
    if (!world)
        return PORTABLE_MESH_REPLAY_INVALID;
    return write_record(writer, (PortableMeshKartInput){0}, (PortableMeshItemControls){0}, session,
                        world, NULL, 1);
}
PortableMeshReplayStatus portable_mesh_replay_write_tick_actors(
    PortableReplayWriter *writer, PortableMeshKartInput input, PortableMeshItemControls controls,
    const PortableMeshTimeTrialSession *session, const PortableMeshItems *items,
    const PortableMeshCourseActors *actors)
{
    if (!actors)
        return PORTABLE_MESH_REPLAY_INVALID;
    return write_record(writer, input, controls, session, items, actors, 0);
}
PortableMeshReplayStatus portable_mesh_replay_write_end_actors(
    PortableReplayWriter *writer, const PortableMeshTimeTrialSession *session,
    const PortableMeshItems *items, const PortableMeshCourseActors *actors)
{
    if (!actors)
        return PORTABLE_MESH_REPLAY_INVALID;
    return write_record(writer, (PortableMeshKartInput){0}, (PortableMeshItemControls){0}, session,
                        items, actors, 1);
}
static PortableMeshReplayStatus read_state(PortableReplayReader *reader,
                                           PortableMeshTimeTrialState *state, uint64_t checksum)
{
    uint8_t bytes[STATE_BYTES];
    const PortableMeshReplayStatus status = read_exact(reader, bytes, sizeof bytes);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    if (!portable_mesh_time_trial_state_read(state, bytes, sizeof bytes) ||
        portable_mesh_time_trial_checksum(state) != checksum)
        return PORTABLE_MESH_REPLAY_INVALID;
    return PORTABLE_MESH_REPLAY_OK;
}
static int same_state(const PortableMeshTimeTrialState *a, const PortableMeshTimeTrialState *b)
{
    uint8_t left[STATE_BYTES], right[STATE_BYTES];
    return portable_mesh_time_trial_state_write(a, left, sizeof left) &&
           portable_mesh_time_trial_state_write(b, right, sizeof right) &&
           memcmp(left, right, sizeof left) == 0;
}
static PortableMeshReplayStatus read_initial(PortableReplayReader *reader,
                                             const PortableMeshTimeTrialCompatibility *expected,
                                             PortableMeshTimeTrialState *state,
                                             PortableMeshReplayHeader *out_header)
{
    PortableMeshReplayHeader header;
    PortableMeshReplayStatus status = portable_mesh_replay_inspect_header(reader, &header);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    if (!portable_mesh_compatibility_equal(expected, &header.compatibility))
        return PORTABLE_MESH_REPLAY_INCOMPATIBLE;
    status = read_state(reader, state, header.initial_checksum);
    if (status == PORTABLE_MESH_REPLAY_OK &&
        (state->tick != 0u || state->phase != PORTABLE_MESH_TT_COUNTDOWN))
        status = PORTABLE_MESH_REPLAY_INVALID;
    if (status == PORTABLE_MESH_REPLAY_OK)
        *out_header = header;
    return status;
}
static PortableMeshReplayStatus read_end(PortableReplayReader *reader, uint64_t checksum,
                                         uint32_t ticks, PortableMeshTimeTrialState *out)
{
    PortableMeshTimeTrialState state;
    PortableMeshReplayStatus status = read_state(reader, &state, checksum);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    if (state.tick != ticks || state.phase != PORTABLE_MESH_TT_FINISHED ||
        state.finish_ticks == 0u || state.finish_ticks > ticks ||
        state.race_ticks != state.finish_ticks || state.countdown_ticks != 0u || state.lap == 0u)
        return PORTABLE_MESH_REPLAY_INVALID;
    uint8_t trailing;
    const int result = reader->read(reader->context, &trailing);
    if (result < 0)
    {
        reader->failed = 1;
        return PORTABLE_MESH_REPLAY_IO_ERROR;
    }
    if (result != 0)
        return PORTABLE_MESH_REPLAY_INVALID;
    *out = state;
    return PORTABLE_MESH_REPLAY_COMPLETE;
}
static PortableMeshReplayStatus
playback_init(PortableMeshReplayPlayback *playback, PortableReplayReader *reader,
              const PortableMeshTimeTrialCompatibility *expected,
              PortableMeshTimeTrialSession *session, PortableMeshItems *world,
              const PortableMeshItemsConfig *config, PortableMeshCourseActors *actors,
              const PortableMeshCourseActorsConfig *actor_config)
{
    if (!playback || !compatibility_valid(expected))
        return PORTABLE_MESH_REPLAY_INVALID;
    if (!session_compatible(session, expected))
        return PORTABLE_MESH_REPLAY_INCOMPATIBLE;
    PortableMeshTimeTrialState initial;
    PortableMeshReplayHeader header;
    const PortableMeshReplayStatus status = read_initial(reader, expected, &initial, &header);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    if (!same_state(&initial, &session->state) ||
        (header.item_profile_id != 0u) != (world != NULL) ||
        (header.version == PORTABLE_MESH_REPLAY_ACTORS_VERSION) != (actors != NULL))
        return PORTABLE_MESH_REPLAY_INCOMPATIBLE;
    if (world &&
        (!initial_items(session, world, config, header.item_seed, header.initial_item_checksum) ||
         config->rules->profile_id != header.item_profile_id ||
         config->rules->revision != header.item_profile_revision))
        return PORTABLE_MESH_REPLAY_INCOMPATIBLE;
    if (actors && ((!world && !sky_only(session, actor_config)) ||
                   !initial_actors(session, actors, actor_config, header.initial_actor_checksum) ||
                   actor_config->profile_id != header.actor_profile_id ||
                   actor_config->revision != header.actor_profile_revision))
        return PORTABLE_MESH_REPLAY_INCOMPATIBLE;
    const PortableMeshReplayPlayback next = {.reader = reader,
                                             .session = session,
                                             .status = PORTABLE_MESH_REPLAY_OK,
                                             .version = header.version,
                                             .world = world,
                                             .item_config = config,
                                             .actors = actors,
                                             .actor_config = actor_config};
    *playback = next;
    return PORTABLE_MESH_REPLAY_OK;
}
PortableMeshReplayStatus portable_mesh_replay_playback_init(
    PortableMeshReplayPlayback *playback, PortableReplayReader *reader,
    const PortableMeshTimeTrialCompatibility *expected, PortableMeshTimeTrialSession *session)
{
    return playback_init(playback, reader, expected, session, NULL, NULL, NULL, NULL);
}
PortableMeshReplayStatus portable_mesh_replay_playback_init_items(
    PortableMeshReplayPlayback *playback, PortableReplayReader *reader,
    const PortableMeshTimeTrialCompatibility *expected, PortableMeshTimeTrialSession *session,
    PortableMeshItems *world, const PortableMeshItemsConfig *config)
{
    if (!world || !config)
        return PORTABLE_MESH_REPLAY_INVALID;
    return playback_init(playback, reader, expected, session, world, config, NULL, NULL);
}
PortableMeshReplayStatus portable_mesh_replay_playback_init_actors(
    PortableMeshReplayPlayback *playback, PortableReplayReader *reader,
    const PortableMeshTimeTrialCompatibility *expected, PortableMeshTimeTrialSession *session,
    PortableMeshItems *items, const PortableMeshItemsConfig *item_config,
    PortableMeshCourseActors *actors, const PortableMeshCourseActorsConfig *config)
{
    if (!actors || !config || ((items == NULL) != (item_config == NULL)))
        return PORTABLE_MESH_REPLAY_INVALID;
    return playback_init(playback, reader, expected, session, items, item_config, actors, config);
}
static int record_items(const uint8_t record[32], int items, int extended, uint32_t *flags,
                        uint64_t *checksum)
{
    *flags = extended ? read_u32(record + 16u) : 0u;
    *checksum = extended ? read_u64(record + 24u) : 0u;
    if (!extended)
        return 1;
    return !read_u32(record + 20u) && (items ? !(*flags & ~7u) : (!*flags && !*checksum));
}
static void clear_playback_events(PortableMeshKartEvents *events, PortableMeshItemEvents *items,
                                  PortableMeshCourseActorEvents *actors)
{
    if (events)
        memset(events, 0, sizeof *events);
    if (items)
        memset(items, 0, sizeof *items);
    if (actors)
        memset(actors, 0, sizeof *actors);
}
static PortableMeshReplayStatus playback_step(PortableMeshReplayPlayback *playback,
                                              PortableReplayWriter *diagnostics,
                                              PortableMeshKartEvents *out_events,
                                              PortableMeshItemEvents *out_item_events,
                                              PortableMeshCourseActorEvents *out_actor_events)
{
    PortableReplayReader *reader = playback->reader;
    PortableMeshTimeTrialSession *session = playback->session;
    const int items = playback->world != NULL;
    const int actors = playback->version == PORTABLE_MESH_REPLAY_ACTORS_VERSION;
    uint8_t bytes[40];
    PortableMeshReplayStatus status = read_exact(reader, bytes, actors ? 40u : items ? 32u : 16u);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    const uint32_t ordinal = read_u32(bytes);
    const uint16_t buttons = (uint16_t)((uint16_t)bytes[4] | (uint16_t)bytes[5] << 8u);
    const uint16_t steering = (uint16_t)((uint16_t)bytes[6] | (uint16_t)bytes[7] << 8u);
    const uint64_t checksum = read_u64(bytes + 8u);
    const uint64_t actor_checksum = actors ? read_u64(bytes + 32u) : 0u;
    uint32_t item_flags;
    uint64_t item_checksum;
    if (!record_items(bytes, items, items || actors, &item_flags, &item_checksum))
        return PORTABLE_MESH_REPLAY_INVALID;
    if (ordinal == UINT32_MAX)
    {
        if (buttons || steering || item_flags || playback->ticks == 0u)
            return PORTABLE_MESH_REPLAY_INVALID;
        PortableMeshTimeTrialState final;
        status = read_end(reader, checksum, playback->ticks, &final);
        if (status == PORTABLE_MESH_REPLAY_COMPLETE &&
            (!same_state(&final, &session->state) ||
             (items && portable_mesh_items_checksum(playback->world) != item_checksum) ||
             (actors && portable_mesh_course_actors_checksum(playback->actors) != actor_checksum)))
            return PORTABLE_MESH_REPLAY_DIVERGED;
        if (status == PORTABLE_MESH_REPLAY_COMPLETE)
            clear_playback_events(out_events, out_item_events, out_actor_events);
        return status;
    }
    if (playback->ticks == PORTABLE_MESH_REPLAY_MAX_TICKS)
        return PORTABLE_MESH_REPLAY_LIMIT;
    if (ordinal != playback->ticks + 1u || (buttons & ~INPUT_MASK) ||
        session->state.phase == PORTABLE_MESH_TT_FINISHED)
        return PORTABLE_MESH_REPLAY_INVALID;
    const PortableMeshKartInput input = {
        (int16_t)(steering <= INT16_MAX ? (int32_t)steering : (int32_t)steering - 65536), buttons};
    PortableMeshTimeTrialSession next = *session;
    PortableMeshKartEvents events;
    char error[256] = {0};
    PortableMeshItems next_world;
    PortableMeshCourseActors next_actors;
    PortableMeshCourseActorEvents actor_events = {0};
    PortableMeshItemEvents item_events = {0};
    PortableMeshStatus result;
    if (items || actors)
    {
        if (items)
            next_world = *playback->world;
        const PortableMeshItemControls controls = {
            .item_pressed = (uint8_t)(item_flags & 1u),
            .aim_backward = (uint8_t)((item_flags >> 1u) & 1u),
            .aim_forward = (uint8_t)((item_flags >> 2u) & 1u)};
        if (actors)
        {
            next_actors = *playback->actors;
            result = portable_mesh_time_trial_tick_actors(
                &next, items ? &next_world : NULL, playback->item_config, &next_actors,
                playback->actor_config, input, controls, &events, &item_events, &actor_events,
                error, sizeof error);
        }
        else
            result = portable_mesh_time_trial_tick_items(&next, &next_world, playback->item_config,
                                                         input, controls, &events, &item_events,
                                                         error, sizeof error);
    }
    else
        result = portable_mesh_time_trial_tick(&next, input, &events, error, sizeof error);
    if (result != PORTABLE_MESH_OK)
    {
        if (diagnostics != NULL)
            (void)portable_replay_printf(diagnostics, "3D replay tick=%u: %s\n", ordinal, error);
        return PORTABLE_MESH_REPLAY_SIMULATION_ERROR;
    }
    if (next.state.tick != ordinal || portable_mesh_time_trial_checksum(&next.state) != checksum ||
        (items && portable_mesh_items_checksum(&next_world) != item_checksum) ||
        (actors && portable_mesh_course_actors_checksum(&next_actors) != actor_checksum))
    {
        if (diagnostics != NULL)
            (void)portable_replay_printf(diagnostics, "3D replay tick=%u: state checksum differs\n",
                                         ordinal);
        return PORTABLE_MESH_REPLAY_DIVERGED;
    }
    *session = next;
    if (items)
        *playback->world = next_world;
    if (actors)
        *playback->actors = next_actors;
    ++playback->ticks;
    if (out_events)
        *out_events = events;
    if (out_item_events)
        *out_item_events = item_events;
    if (out_actor_events)
        *out_actor_events = actor_events;
    return PORTABLE_MESH_REPLAY_OK;
}
PortableMeshReplayStatus portable_mesh_replay_playback_tick_actor_events(
    PortableMeshReplayPlayback *playback, PortableReplayWriter *diagnostics,
    PortableMeshKartEvents *events, PortableMeshItemEvents *items,
    PortableMeshCourseActorEvents *actors)
{
    if (playback == NULL || playback->reader == NULL || playback->session == NULL ||
        (playback->version != PORTABLE_MESH_REPLAY_VERSION &&
         playback->version != PORTABLE_MESH_REPLAY_ITEMS_VERSION &&
         playback->version != PORTABLE_MESH_REPLAY_ACTORS_VERSION) ||
        ((playback->world == NULL) != (playback->item_config == NULL)) ||
        (playback->version == PORTABLE_MESH_REPLAY_ITEMS_VERSION && !playback->world) ||
        (playback->version == PORTABLE_MESH_REPLAY_ACTORS_VERSION && !playback->world &&
         !sky_only(playback->session, playback->actor_config)) ||
        (playback->version == PORTABLE_MESH_REPLAY_ACTORS_VERSION &&
         (!playback->actors || !playback->actor_config)))
        return PORTABLE_MESH_REPLAY_INVALID;
    if (playback->status != PORTABLE_MESH_REPLAY_OK)
    {
        if (playback->status == PORTABLE_MESH_REPLAY_COMPLETE)
            clear_playback_events(events, items, actors);
        return playback->status;
    }
    playback->status = playback_step(playback, diagnostics, events, items, actors);
    return playback->status;
}
PortableMeshReplayStatus portable_mesh_replay_playback_tick_events(
    PortableMeshReplayPlayback *playback, PortableReplayWriter *diagnostics,
    PortableMeshKartEvents *events, PortableMeshItemEvents *items)
{
    return portable_mesh_replay_playback_tick_actor_events(playback, diagnostics, events, items,
                                                           NULL);
}
PortableMeshReplayStatus portable_mesh_replay_playback_tick(PortableMeshReplayPlayback *playback,
                                                            PortableReplayWriter *diagnostics)
{
    return portable_mesh_replay_playback_tick_events(playback, diagnostics, NULL, NULL);
}
PortableMeshReplayStatus portable_mesh_replay_run(
    PortableReplayReader *reader, const PortableMeshTimeTrialCompatibility *expected,
    PortableMeshTimeTrialSession *session, PortableReplayWriter *diagnostics, size_t *out_ticks)
{
    if (out_ticks != NULL)
        *out_ticks = 0u;
    PortableMeshReplayPlayback playback;
    PortableMeshReplayStatus status =
        portable_mesh_replay_playback_init(&playback, reader, expected, session);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    do
    {
        status = portable_mesh_replay_playback_tick(&playback, diagnostics);
        if (out_ticks != NULL)
            *out_ticks = playback.ticks;
    } while (status == PORTABLE_MESH_REPLAY_OK);
    return status;
}
PortableMeshReplayStatus
portable_mesh_replay_validate(const uint8_t *bytes, size_t size,
                              const PortableMeshTimeTrialCompatibility *expected,
                              uint32_t *out_ticks)
{
    if (bytes == NULL || !compatibility_valid(expected))
        return PORTABLE_MESH_REPLAY_INVALID;
    if (size > PORTABLE_MESH_REPLAY_ACTORS_MAX_BYTES ||
        (size > PORTABLE_MESH_REPLAY_ITEMS_MAX_BYTES &&
         read_u32(bytes + 8u) != PORTABLE_MESH_REPLAY_ACTORS_VERSION) ||
        (size > PORTABLE_MESH_REPLAY_MAX_BYTES &&
         read_u32(bytes + 8u) != PORTABLE_MESH_REPLAY_ITEMS_VERSION &&
         read_u32(bytes + 8u) != PORTABLE_MESH_REPLAY_ACTORS_VERSION))
        return PORTABLE_MESH_REPLAY_LIMIT;
    PortableReplayMemoryInput memory = {bytes, size, 0u};
    PortableReplayReader reader = portable_replay_memory_reader(&memory);
    PortableMeshTimeTrialState state;
    PortableMeshReplayHeader header;
    PortableMeshReplayStatus status = read_initial(&reader, expected, &state, &header);
    if (status != PORTABLE_MESH_REPLAY_OK)
        return status;
    const int items = header.item_profile_id != 0u;
    const int actors = header.version == PORTABLE_MESH_REPLAY_ACTORS_VERSION;
    uint32_t ticks = 0u;
    uint64_t checksum = portable_mesh_time_trial_checksum(&state);
    uint64_t item_checksum = header.initial_item_checksum;
    uint64_t actor_checksum = header.initial_actor_checksum;
    for (;;)
    {
        uint8_t record[40];
        status = read_exact(&reader, record, actors ? 40u : items ? 32u : 16u);
        if (status != PORTABLE_MESH_REPLAY_OK)
            return status;
        const uint32_t ordinal = read_u32(record);
        uint32_t flags;
        uint64_t record_item_checksum;
        if (!record_items(record, items, items || actors, &flags, &record_item_checksum))
            return PORTABLE_MESH_REPLAY_INVALID;
        if (ordinal == UINT32_MAX)
        {
            if (read_u32(record + 4u) != 0u || flags || ticks == 0u ||
                read_u64(record + 8u) != checksum ||
                (items && record_item_checksum != item_checksum) ||
                (actors && read_u64(record + 32u) != actor_checksum))
                return PORTABLE_MESH_REPLAY_INVALID;
            status = read_end(&reader, checksum, ticks, &state);
            if (status == PORTABLE_MESH_REPLAY_COMPLETE && out_ticks != NULL)
                *out_ticks = ticks;
            return status;
        }
        if (ticks == PORTABLE_MESH_REPLAY_MAX_TICKS)
            return PORTABLE_MESH_REPLAY_LIMIT;
        if (ordinal != ticks + 1u || (record[4] & ~INPUT_MASK) || record[5] != 0u)
            return PORTABLE_MESH_REPLAY_INVALID;
        checksum = read_u64(record + 8u);
        item_checksum = record_item_checksum;
        if (actors)
            actor_checksum = read_u64(record + 32u);
        ++ticks;
    }
}
const char *portable_mesh_replay_status_name(PortableMeshReplayStatus status)
{
    switch (status)
    {
    case PORTABLE_MESH_REPLAY_OK:
        return "ok";
    case PORTABLE_MESH_REPLAY_COMPLETE:
        return "complete";
    case PORTABLE_MESH_REPLAY_INVALID:
        return "invalid";
    case PORTABLE_MESH_REPLAY_TRUNCATED:
        return "truncated";
    case PORTABLE_MESH_REPLAY_INCOMPATIBLE:
        return "incompatible";
    case PORTABLE_MESH_REPLAY_IO_ERROR:
        return "io_error";
    case PORTABLE_MESH_REPLAY_LIMIT:
        return "limit";
    case PORTABLE_MESH_REPLAY_DIVERGED:
        return "diverged";
    case PORTABLE_MESH_REPLAY_SIMULATION_ERROR:
        return "simulation_error";
    }
    return "unknown";
}
