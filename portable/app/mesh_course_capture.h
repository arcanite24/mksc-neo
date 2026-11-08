#ifndef PORTABLE_MESH_COURSE_CAPTURE_H
#define PORTABLE_MESH_COURSE_CAPTURE_H
#include "mesh_racer.h"
#include "mesh_item_status.h"
/* Modern SI adaptation of source material-2A capture. Zero is inactive;
 * all fields participate in the conditional plant-world checksum. */
typedef struct
{
    uint32_t remaining, actor_index_plus1, started_tick, spin;
    int32_t pull_q16[3], forward_q30[3], up_q30[3];
} PortableMeshCaptureState;
struct PortableMeshCourseActorsConfig;
struct PortableMeshCourseActors;
struct PortableMeshCourseActorEvents;
/* Source owners have no SDL or host APIs. Admission and motion are staged by
 * the existing GP/TT turn, after accepted item activation and before ordinary kart movement. */
int portable_mesh_course_capture_admit(const struct PortableMeshCourseActorsConfig *,
    struct PortableMeshCourseActors *, const PortableCourse3DView *,
    const PortableMeshKartRules *, const PortableMeshItemRules *, const PortableMeshItemStatusOwner *,
    const PortableMeshRacerState *, size_t, uint8_t recovering_mask,
    struct PortableMeshCourseActorEvents *, char *, size_t);
/* handled=1 owns this turn's hold/pull movement and clock. A launch publishes
 * velocity then handled=0 lets the ordinary mesh integrator sweep it. */
int portable_mesh_course_capture_move(const struct PortableMeshCourseActorsConfig *,
    struct PortableMeshCourseActors *, unsigned, PortableMeshRacer *, PortableMeshKartInput,
    PortableMeshRacerTurn *, int *handled, struct PortableMeshCourseActorEvents *, char *, size_t);
#endif
