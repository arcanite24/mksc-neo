#ifndef PORTABLE_MESH_ITEM_TARGET_H
#define PORTABLE_MESH_ITEM_TARGET_H

#include "course_3d.h"
#include "mesh_item_motion.h"

enum { PORTABLE_MESH_ITEM_TARGET_TRANSITIONS = 64 };
typedef struct
{
    uint32_t course_id, route, from_node, to_node, selected_edge;
    uint32_t progress_q16, transition_count;
    int32_t previous_position_q16[3];
    uint8_t initialized, direct;
} PortableMeshItemTargetCursor;
typedef enum
{
    PORTABLE_MESH_ITEM_TARGET_OK = 0,
    PORTABLE_MESH_ITEM_TARGET_NO_ROUTE,
    PORTABLE_MESH_ITEM_TARGET_END_ROUTE,
    PORTABLE_MESH_ITEM_TARGET_NO_DIRECTION,
    PORTABLE_MESH_ITEM_TARGET_INVALID,
    PORTABLE_MESH_ITEM_TARGET_BUDGET
} PortableMeshItemTargetStatus;

/* ABSENT chooses the nearest ITEM(kind1) segment in full XYZ, with authored
 * route/edge order resolving ties. Zero-initialized cursors are also accepted
 * by steer, including direct pursuit when no ITEM route is authored. */
PortableMeshItemTargetStatus portable_mesh_item_target_init(PortableMeshItemTargetCursor *out,
    const PortableCourse3DView *course, uint32_t route, const PortableMeshItemMotionState *motion);
/* Target selection/retention belongs to the world. Direct pursuit requires
 * same_stage, MIR range, and a clear two-sided sphere/mesh FACE sweep. This
 * does not provide edge/vertex occlusion or mutate/advance motion. Otherwise
 * follow connected authored route edges; branches choose the successor whose
 * full XYZ endpoint is nearest target, keeping first edge on ties.
 * Desired/current directions are projected onto support when grounded. Turn
 * uses MIR turns/sec*fixed dt; speed uses MIR acceleration and speed cap.
 * Every non-OK status preserves next and velocity, including valid missing,
 * terminal or degenerate-direction cases for the caller's ballistic policy.
 * Outputs may alias their corresponding inputs. All content stays borrowed. */
PortableMeshItemTargetStatus portable_mesh_item_target_steer(PortableMeshItemTargetCursor *next,
    const PortableMeshItemTargetCursor *previous, const PortableCourse3DView *course,
    const PortableMeshItemRules *rules, const PortableMeshItemMotionState *motion,
    const int32_t target_q16[3], int same_stage, int32_t velocity_q16[3]);
/* Identical steering with a borrowed immutable collision snapshot. NULL is the
 * legacy linear path; route/target/cursor ownership and rollback are unchanged. */
PortableMeshItemTargetStatus portable_mesh_item_target_steer_geometry(PortableMeshItemTargetCursor *next,
    const PortableMeshItemTargetCursor *previous, const PortableCourse3DView *course,
    const PortableMeshItemRules *rules, const PortableMeshItemMotionState *motion,
    const int32_t target_q16[3], int same_stage, int32_t velocity_q16[3],
    const PortableMeshCourse *geometry);
const char *portable_mesh_item_target_status_name(PortableMeshItemTargetStatus status);

#endif
