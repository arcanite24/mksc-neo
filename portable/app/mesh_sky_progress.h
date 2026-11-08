#ifndef PORTABLE_MESH_SKY_PROGRESS_H
#define PORTABLE_MESH_SKY_PROGRESS_H
#include "course_3d.h"
enum
{
    PORTABLE_MESH_SKY_PROGRESS_STAGES = 1024
};
typedef struct
{
    uint32_t enabled, stage_count;
    uint8_t source_raw[PORTABLE_MESH_SKY_PROGRESS_STAGES];
} PortableMeshSkyProgressConfig;
typedef struct
{
    uint16_t high_water;
    uint8_t route_index, route_wrap;
} PortableMeshSkyProgress;
typedef struct
{
    uint32_t stage, completed_laps, crossing_armed;
} PortableMeshSkyProgressInput;
/* Resolve optional typed META provenance against actual CPNT stages.
 * Failure preserves every output; keys and counts are bounded. */
int portable_mesh_sky_progress_open(const PortableCourse3DView *, PortableMeshSkyProgressConfig *,
                                    char *, size_t);
int portable_mesh_sky_progress_config_valid(const PortableMeshSkyProgressConfig *);
int portable_mesh_sky_progress_valid(const PortableMeshSkyProgressConfig *,
                                     const PortableMeshSkyProgress *);
int portable_mesh_sky_progress_init(const PortableMeshSkyProgressConfig *,
                                    PortableMeshSkyProgress *);
/* Adapt prior accepted racer state, never render state or generated AI guides.
 * Does not accept a gate or increment laps. Aliasing works; failure preserves out. */
int portable_mesh_sky_progress_update(const PortableMeshSkyProgressConfig *,
                                      PortableMeshSkyProgress *, const PortableMeshSkyProgress *,
                                      const PortableMeshSkyProgressInput *);
#endif
