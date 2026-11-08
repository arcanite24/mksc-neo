#ifndef PORTABLE_MESH_CPU_H
#define PORTABLE_MESH_CPU_H
#include "mesh_racer.h"

typedef struct
{
    uint32_t course_id, profile_id, profile_revision;
    uint32_t route, from_node, to_node, selected_edge, visit_serial;
    uint32_t projected_fraction_q16, lost_ticks, recovery_ticks;
    uint32_t observed_respawn_ticks, observed_last_respawn;
    int32_t previous_foot_q16[3], lookahead_q16, corner_speed_q16;
} PortableMeshCpuState;

/* ABSENT chooses the first CPU route. Production racing requires a closed,
 * connected route with nonzero-length edges. Failure preserves output. */
PortableMeshStatus portable_mesh_cpu_init(PortableMeshCpuState *state,
                                          const PortableMeshRacer *racer, uint32_t route,
                                          char *error, size_t capacity);
/* Reads one immutable preturn racer. Caller stages every controller/input,
 * advances the shared world, then commits controllers only on world success.
 * next may alias previous; failure preserves next and input. */
PortableMeshStatus portable_mesh_cpu_input(PortableMeshCpuState *next,
                                           const PortableMeshCpuState *previous,
                                           const PortableMeshRacer *racer,
                                           PortableMeshKartInput *input, char *error,
                                           size_t capacity);
#endif
