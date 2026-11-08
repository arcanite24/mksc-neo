#ifndef PORTABLE_MESH_SKY_H
#define PORTABLE_MESH_SKY_H
#include "sky_motion.h"
#include "course_3d.h"
#include "mesh_sky_progress.h"
#include "mesh_palette.h"

enum
{
    PORTABLE_MESH_SKY_PROFILE_SIZE = 4244,
    PORTABLE_MESH_SKY_RULES_KIND = 106
};
typedef struct
{
    uint32_t enabled, profile_id, revision, render_mask, entry_delay;
    uint32_t logical_width[3];
    int32_t radius_q16[3], sine[1024];
    PortableSkyMotionRules rules;
    PortableMeshSkyProgressConfig progress;
    PortableMeshPaletteConfig palette;
} PortableMeshSkyConfig;
typedef struct
{
    uint32_t enabled, profile_id, revision, clock;
    PortableSkyMotion motion;
    uint32_t progress_enabled, racer_count;
    PortableMeshSkyProgress progress[8];
    PortableSkyMotion other_motion[7];
    PortableMeshPalette palette;
} PortableMeshSky;

typedef struct
{
    uint32_t object_indices[3]; /* UINT32_MAX for unbound logical layers. */
} PortableMeshSkyBindings;
/* Resolve META and ordinary model placements transactionally. Each rendered
 * logical layer must have exactly one object, independent of its art bank. */
int portable_mesh_sky_config_open(const PortableCourse3DView *, PortableMeshSkyConfig *,
                                  PortableMeshSkyBindings *, char *, size_t);
/* Decode profile fields transactionally. Progress profiles need course-bound
 * provenance before config_valid/init succeeds; clock-only profiles are complete. */
int portable_mesh_sky_decode(const uint8_t *, size_t, uint32_t content_id, PortableMeshSkyConfig *,
                             char *, size_t);
int portable_mesh_sky_config_valid(const PortableMeshSkyConfig *);
int portable_mesh_sky_valid(const PortableMeshSkyConfig *, const PortableMeshSky *);
/* Internal composition only: the exact immutable config must already have
 * passed config_valid in this call; never persist that assumption across calls.
 * All mutable world checks are retained. */
int portable_mesh_sky_state_valid(const PortableMeshSkyConfig *, const PortableMeshSky *);
int portable_mesh_sky_init(const PortableMeshSkyConfig *, PortableMeshSky *);
/* One call per accepted countdown/racing turn, never from presentation. */
int portable_mesh_sky_advance(const PortableMeshSkyConfig *, PortableMeshSky *,
                              const PortableMeshSky *);
/* Progress controllers consume prior accepted racer poses/standing leader.
 * First turn establishes racer count; all later turns preserve it. Clock-only
 * profiles retain their legacy state/hash and ignore these progress inputs. */
int portable_mesh_sky_advance_racers(const PortableMeshSkyConfig *, PortableMeshSky *,
                                     const PortableMeshSky *, const PortableMeshSkyProgressInput *,
                                     unsigned count, unsigned leader);
/* Camera resolution copies cached accepted coordinates; RISE never gains an
 * extra elapsed step when another view is drawn. No camera-height coupling. */
int portable_mesh_sky_snapshot(const PortableMeshSkyConfig *, const PortableMeshSky *, uint16_t yaw,
                               PortableSkyMotionSnapshot *);
int portable_mesh_sky_snapshot_racer(const PortableMeshSkyConfig *, const PortableMeshSky *,
                                     unsigned racer, uint16_t yaw, PortableSkyMotionSnapshot *);
/* Append named state only when enabled; caller validates config/world pairing. */
uint64_t portable_mesh_sky_hash(const PortableMeshSky *, uint64_t);
uint64_t portable_mesh_sky_checksum(const PortableMeshSkyConfig *, const PortableMeshSky *);
#endif
