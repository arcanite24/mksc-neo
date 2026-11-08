#ifndef PORTABLE_MESH_CUP_H
#define PORTABLE_MESH_CUP_H
#include "mesh_race.h"
#include "gp_scoring.h"

enum
{
    PORTABLE_MESH_CUP_COURSES = 4
};
typedef enum
{
    PORTABLE_MESH_CUP_RACE,
    PORTABLE_MESH_CUP_STANDINGS,
    PORTABLE_MESH_CUP_RETRY,
    PORTABLE_MESH_CUP_COMPLETE,
    PORTABLE_MESH_CUP_GAME_OVER
} PortableMeshCupStage;
typedef struct
{
    uint32_t cup_id, course_ids[PORTABLE_MESH_CUP_COURSES];
} PortableMeshCupDefinition;
typedef struct
{
    PortableMeshCupDefinition definition;
    uint32_t driver_ids[8], engine_class, course_index;
    PortableGpScoringRules scoring;
    PortableMeshCupStage stage;
    uint8_t retry_credits, grid_slots[8], points[8], awards[8];
    uint8_t race_order[8], standing_order[8], standing_rank[8];
    uint8_t player_rank;   /* Current attempt, zero-based; UINT8_MAX before finish. */
    uint32_t finish_ticks; /* Current attempt's real human clock. */
    uint32_t race_ticks[PORTABLE_MESH_CUP_COURSES];
    uint8_t race_ranks[PORTABLE_MESH_CUP_COURSES]; /* Successful races only. */
} PortableMeshCupSession;
/* Every operation is transactional, pointer-free and OS-free. Imported scoring
 * is copied; mesh driving quality is deliberately not inferred from 2D rules. */
PortableMeshStatus portable_mesh_cup_init(PortableMeshCupSession *,
                                          const PortableMeshCupDefinition *,
                                          const uint32_t driver_ids[8], unsigned engine_class,
                                          const PortableGpScoringRules *, char *error,
                                          size_t capacity);
PortableMeshStatus portable_mesh_cup_finish(PortableMeshCupSession *,
                                            const PortableMeshRaceSession *, char *error,
                                            size_t capacity);
/* Advance after successful standings: next course or COMPLETE. Retry consumes
 * one credit from RACE or RETRY and preserves the current race's grid;
 * no points are awarded. */
PortableMeshStatus portable_mesh_cup_advance(PortableMeshCupSession *, char *, size_t);
PortableMeshStatus portable_mesh_cup_retry(PortableMeshCupSession *, char *, size_t);
#endif
