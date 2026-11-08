#ifndef PORTABLE_MESH_TIME_TRIAL_REPLAY_H
#define PORTABLE_MESH_TIME_TRIAL_REPLAY_H

#include "mesh_time_trial.h"
#include "mesh_course_actors.h"
#include "replay_stream.h"

enum
{
    PORTABLE_MESH_REPLAY_VERSION = 1u,
    PORTABLE_MESH_REPLAY_MAX_TICKS = 216000u,
    PORTABLE_MESH_REPLAY_MAX_BYTES = 4u * 1024u * 1024u,
    PORTABLE_MESH_COMPATIBILITY_BYTES = 56u,
    PORTABLE_MESH_REPLAY_HEADER_BYTES = 88u,
    PORTABLE_MESH_REPLAY_ITEMS_VERSION = 2u,
    PORTABLE_MESH_REPLAY_ITEMS_HEADER_BYTES = 112u,
    PORTABLE_MESH_REPLAY_ITEMS_MAX_BYTES = 8u * 1024u * 1024u,
    PORTABLE_MESH_REPLAY_ACTORS_VERSION = 3u,
    PORTABLE_MESH_REPLAY_ACTORS_HEADER_BYTES = 128u,
    PORTABLE_MESH_REPLAY_ACTORS_MAX_BYTES = 12u * 1024u * 1024u
};
typedef struct
{
    uint32_t course_id, driver_id, profile_id, profile_revision;
    uint32_t tick_numerator, tick_denominator;
    uint8_t digest[32];
    uint32_t kart_id; /* 3D kart (kind 111) or 0; header byte 76, outside the 56-byte block. */
} PortableMeshTimeTrialCompatibility;
typedef enum
{
    PORTABLE_MESH_REPLAY_OK = 0,
    PORTABLE_MESH_REPLAY_COMPLETE,
    PORTABLE_MESH_REPLAY_INVALID,
    PORTABLE_MESH_REPLAY_TRUNCATED,
    PORTABLE_MESH_REPLAY_INCOMPATIBLE,
    PORTABLE_MESH_REPLAY_IO_ERROR,
    PORTABLE_MESH_REPLAY_LIMIT,
    PORTABLE_MESH_REPLAY_DIVERGED,
    PORTABLE_MESH_REPLAY_SIMULATION_ERROR
} PortableMeshReplayStatus;
typedef struct
{
    PortableMeshTimeTrialCompatibility compatibility;
    uint64_t initial_checksum;
    uint32_t version, state_words;
    uint8_t ghost;
    uint32_t item_profile_id, item_profile_revision, item_seed;
    uint64_t initial_item_checksum; /* Zero for version1 and itemless version3. */
    uint32_t actor_profile_id, actor_profile_revision;
    uint64_t initial_actor_checksum; /* Zero before version3. */
} PortableMeshReplayHeader;

int portable_mesh_compatibility_equal(const PortableMeshTimeTrialCompatibility *a,
                                      const PortableMeshTimeTrialCompatibility *b);
int portable_mesh_compatibility_write(const PortableMeshTimeTrialCompatibility *identity,
                                      uint8_t bytes[PORTABLE_MESH_COMPATIBILITY_BYTES]);
int portable_mesh_compatibility_read(PortableMeshTimeTrialCompatibility *identity,
                                     const uint8_t bytes[PORTABLE_MESH_COMPATIBILITY_BYTES]);
/* Inspect consumes the declared fixed 88/112/128-byte header; reset before run. */
PortableMeshReplayStatus portable_mesh_replay_inspect_header(PortableReplayReader *reader,
                                                             PortableMeshReplayHeader *out);
PortableMeshReplayStatus
portable_mesh_replay_write_header(PortableReplayWriter *writer,
                                  const PortableMeshTimeTrialCompatibility *identity,
                                  const PortableMeshTimeTrialSession *session);
PortableMeshReplayStatus
portable_mesh_replay_write_tick(PortableReplayWriter *writer, PortableMeshKartInput input,
                                const PortableMeshTimeTrialSession *session);
PortableMeshReplayStatus
portable_mesh_replay_write_end(PortableReplayWriter *writer,
                               const PortableMeshTimeTrialSession *session);
/* Version2 adds a fixed 24-byte item header extension and fixed 32-byte
 * records. Item flags encode held/back/forward only; recovering derives from
 * kart RESCUE edges. The initial world must equal a fresh init from seed and
 * config. No native world struct is serialized; MIS1 verifies each turn. */
PortableMeshReplayStatus portable_mesh_replay_write_header_items(
    PortableReplayWriter *, const PortableMeshTimeTrialCompatibility *,
    const PortableMeshTimeTrialSession *, const PortableMeshItems *,
    const PortableMeshItemsConfig *, uint32_t seed);
PortableMeshReplayStatus portable_mesh_replay_write_tick_items(PortableReplayWriter *,
                                                               PortableMeshKartInput,
                                                               PortableMeshItemControls,
                                                               const PortableMeshTimeTrialSession *,
                                                               const PortableMeshItems *);
PortableMeshReplayStatus portable_mesh_replay_write_end_items(PortableReplayWriter *,
                                                              const PortableMeshTimeTrialSession *,
                                                              const PortableMeshItems *);
/* Version3 retains all v2 fields, adds16 header bytes and8 per record.
 * Requires a declared actor/coin/sky config and a fresh independent owner.
 * Sky-only courses may pass NULL for both item owner/config and seed0. Their
 * course must not declare META mksc:item-rules (even a wrong-type marker).
 * Their entire24-byte item header extension and16-byte item record extension are
 * canonical zero; nonzero item controls are rejected. Existing item-enabled
 * version3 encoding is unchanged. */
PortableMeshReplayStatus portable_mesh_replay_write_header_actors(
    PortableReplayWriter *, const PortableMeshTimeTrialCompatibility *,
    const PortableMeshTimeTrialSession *, const PortableMeshItems *,
    const PortableMeshItemsConfig *, uint32_t seed, const PortableMeshCourseActors *,
    const PortableMeshCourseActorsConfig *);
PortableMeshReplayStatus
portable_mesh_replay_write_tick_actors(PortableReplayWriter *, PortableMeshKartInput,
                                       PortableMeshItemControls,
                                       const PortableMeshTimeTrialSession *,
                                       const PortableMeshItems *, const PortableMeshCourseActors *);
PortableMeshReplayStatus portable_mesh_replay_write_end_actors(PortableReplayWriter *,
                                                               const PortableMeshTimeTrialSession *,
                                                               const PortableMeshItems *,
                                                               const PortableMeshCourseActors *);
typedef struct
{
    PortableReplayReader *reader;
    PortableMeshTimeTrialSession *session;
    uint32_t ticks;
    PortableMeshReplayStatus status;
    uint32_t version;
    PortableMeshItems *world;
    const PortableMeshItemsConfig *item_config;
    PortableMeshCourseActors *actors;
    const PortableMeshCourseActorsConfig *actor_config;
} PortableMeshReplayPlayback;
/* Reader/session stay caller-owned and stationary until playback ends. */
PortableMeshReplayStatus portable_mesh_replay_playback_init(
    PortableMeshReplayPlayback *playback, PortableReplayReader *reader,
    const PortableMeshTimeTrialCompatibility *expected, PortableMeshTimeTrialSession *session);
/* Version2 only. Reader/session/world/config remain caller-owned and
 * stationary. Initialization and each verified tick publish transactionally. */
PortableMeshReplayStatus
portable_mesh_replay_playback_init_items(PortableMeshReplayPlayback *, PortableReplayReader *,
                                         const PortableMeshTimeTrialCompatibility *,
                                         PortableMeshTimeTrialSession *, PortableMeshItems *,
                                         const PortableMeshItemsConfig *);
/* Item owner/config may both be NULL only for a sky-only version3 stream.
 * Header item_profile_id determines presence; a zero ID requires all other
 * item-extension fields to be zero. Each owner remains caller-owned. */
PortableMeshReplayStatus portable_mesh_replay_playback_init_actors(
    PortableMeshReplayPlayback *, PortableReplayReader *,
    const PortableMeshTimeTrialCompatibility *, PortableMeshTimeTrialSession *, PortableMeshItems *,
    const PortableMeshItemsConfig *, PortableMeshCourseActors *,
    const PortableMeshCourseActorsConfig *);
PortableMeshReplayStatus portable_mesh_replay_playback_tick_actor_events(
    PortableMeshReplayPlayback *, PortableReplayWriter *, PortableMeshKartEvents *,
    PortableMeshItemEvents *, PortableMeshCourseActorEvents *);
PortableMeshReplayStatus portable_mesh_replay_playback_tick(PortableMeshReplayPlayback *playback,
                                                            PortableReplayWriter *diagnostics);
/* Optional event outputs are committed only with a verified turn; every
 * failure leaves them unchanged. COMPLETE (including repeated terminal calls)
 * zeros both outputs without committing either owner. Version1 emits actual
 * kart events and empty item events. Outputs must not overlap owners. */
PortableMeshReplayStatus
portable_mesh_replay_playback_tick_events(PortableMeshReplayPlayback *,
                                          PortableReplayWriter *diagnostics,
                                          PortableMeshKartEvents *, PortableMeshItemEvents *);
/* Header/initial-state mismatch never mutates session. Each verified tick is
 * published only after its checksum agrees; diagnostics are optional. */
PortableMeshReplayStatus portable_mesh_replay_run(
    PortableReplayReader *reader, const PortableMeshTimeTrialCompatibility *expected,
    PortableMeshTimeTrialSession *session, PortableReplayWriter *diagnostics, size_t *out_ticks);
/* Bounded structural validation for persistence, without running simulation.
 * Full determinism verification uses run through the actual tick owner. */
PortableMeshReplayStatus
portable_mesh_replay_validate(const uint8_t *bytes, size_t size,
                              const PortableMeshTimeTrialCompatibility *expected,
                              uint32_t *out_ticks);
const char *portable_mesh_replay_status_name(PortableMeshReplayStatus status);

#endif
