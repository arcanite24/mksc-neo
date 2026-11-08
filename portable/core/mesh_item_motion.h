#ifndef PORTABLE_MESH_ITEM_MOTION_H
#define PORTABLE_MESH_ITEM_MOTION_H
#include "mesh_item_rules.h"
#include "mesh_kart.h"

typedef enum
{
    PORTABLE_MESH_ITEM_SHELL = 0,
    PORTABLE_MESH_ITEM_BANANA = 1
} PortableMeshItemMotionMode;
typedef enum
{
    PORTABLE_MESH_ITEM_MOTION_OK = 0,
    PORTABLE_MESH_ITEM_MOTION_INVALID,
    PORTABLE_MESH_ITEM_MOTION_OUTSIDE,
    PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET,
    PORTABLE_MESH_ITEM_MOTION_BOUNCE_LIMIT
} PortableMeshItemMotionStatus;
typedef struct
{
    int32_t position_q16[3], velocity_q16[3], support_normal_q30[3];
    uint32_t contact_triangle, contact_surface, respawn_index, bounce_count;
    uint8_t mode, grounded;
} PortableMeshItemMotionState;
typedef struct
{
    uint32_t wall_impacts;
    uint8_t landed, resting;
} PortableMeshItemMotionEvents;
typedef enum
{
    PORTABLE_MESH_ITEM_FAULT_NONE = 0,
    PORTABLE_MESH_ITEM_FAULT_OVERLAP,
    PORTABLE_MESH_ITEM_FAULT_SWEEP,
    PORTABLE_MESH_ITEM_FAULT_SUBSTEPS
} PortableMeshItemMotionFaultPhase;
typedef struct
{
    PortableMeshItemMotionFaultPhase phase;
    uint32_t iteration, triangle;
    int32_t position_q16[3], velocity_q16[3], radius_q16;
    int32_t normal_q30[3], remaining_q16[3];
    uint64_t fraction_q32;
} PortableMeshItemMotionFault;
/* Optional deterministic fault is published ONLY on COLLISION_BUDGET; other
 * results preserve it. Position/velocity are the failing phase's entry state.
 * iteration is the exhausted pass/impact/substep count. triangle UINT32_MAX
 * means no contact; overlap has no sweep fraction or remaining displacement. */
PortableMeshItemMotionStatus portable_mesh_item_motion_tick_diagnostic(
    PortableMeshItemMotionState *, const PortableMeshItemRules *, const PortableMeshCourse *,
    int32_t floor_cutoff_q30, PortableMeshItemMotionEvents *, PortableMeshItemMotionFault *);
const char *portable_mesh_item_motion_fault_phase_name(PortableMeshItemMotionFaultPhase);
/* Pointer-free sphere center/velocity in SI Q16. Launch/targeting, lifetime,
 * racer hits and inventory are outside this owner. Shell acceleration/speed
 * are applied by its caller, not repeated here. Banana gravity is MIR SI;
 * unsupported surfaces 6..8 return OUTSIDE without publishing anything.
 * Z-up only, +/-4096m and +/-256m/s per axis. Each tick has at most 256
 * radius/2 substeps, 8 swept impacts per substep and 8 stable overlap passes.
 * Face sweeps plus endpoint overlap are not complete edge/vertex CCD. */
PortableMeshItemMotionStatus portable_mesh_item_motion_init(PortableMeshItemMotionState *,
                                                            PortableMeshItemMotionMode,
                                                            const int32_t position_q16[3],
                                                            const int32_t velocity_q16[3]);
/* Fixed dt 4389/262144. Rules/course are borrowed for this call only. Positive
 * Q30 floor cutoff distinguishes support from walls/ceilings. Shells reflect
 * inward wall velocity using MIR restitution and bounce limit; bananas cancel
 * inward wall/ceiling velocity and stop on usable floors. Failures preserve
 * both state and events, including exhausted collision/bounce budgets. */
PortableMeshItemMotionStatus portable_mesh_item_motion_tick(PortableMeshItemMotionState *,
                                                            const PortableMeshItemRules *,
                                                            const PortableMeshCourse *,
                                                            int32_t floor_cutoff_q30,
                                                            PortableMeshItemMotionEvents *);
const char *portable_mesh_item_motion_status_name(PortableMeshItemMotionStatus);
#endif
