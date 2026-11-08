#ifndef PORTABLE_RENDER_CHASE_H
#define PORTABLE_RENDER_CHASE_H

#include "render_pose.h"
#include <stddef.h>

/* Presentation only: accepted support-foot pose and clocks, never simulation
 * ownership or replay bytes. cut resets the camera after Retry, rescue or teleport.
 * dynamic adds speed field of view, boost pull-back and drift yaw; shake adds
 * landing, boost, wall and spin-out shake. immersive selects lower/closer
 * framing, with bounded pitch/lean when dynamic and softer jolts when shake. */
/* The resting rig in metres from the support foot: the eye trail behind and
 * height above it, the look point's height over the foot and how far ahead
 * along the view the aim lies. Speed and boost pull-back scale with trail.
 * All zero selects the stock modern rig. */
typedef struct
{
    double trail, height, look_height, look_ahead;
} PortableRenderChaseRig;
/* Stock rig times scale, or the rig whose resting eye and aim (x right, y
 * forward, z up from the foot, metres) are eye and target. 0 on a rig the
 * camera cannot hold (eye under 1 m behind or below the aim). */
int portable_render_chase_rig(PortableRenderChaseRig *out, const double eye[3], const double target[3],
                              double scale);
typedef struct
{
    PortableRenderPose foot;
    PortableRenderChaseRig rig;
    double gravity_up[3]; /* The view's up (loops, anti-gravity); zero is world up. */
    uint32_t tick;
    int32_t outside_drift_q16;
    int32_t speed_ratio_q16; /* Forward speed over authored top speed, 0..2. */
    uint8_t grounded, boosting, spinning, cut, dynamic, shake, immersive;
} PortableRenderChaseInput;
typedef struct
{
    /* MK8 Deluxe's chase camera (0x10f160): translation rides the kart
     * rigidly (anchor = foot); only rotation lags. dir eases toward the kart's
     * forward, pitch tilts the view below it, up turns toward gravity's;
     * heading is dir flattened against up. offset is eye minus look point,
     * pushed in by scenery and eased back as recover returns to 1; boom is
     * its share of the free boom. */
    double eye[3], target[3], heading[3], up[3], anchor[3], dir[3], offset[3];
    double boom, swing, focal_scale, recover;
    double distance, pitch, shake, speed;
    double motion_pitch, lean, jolt[3]; /* Immersive-only, accepted-tick filters. */
    uint32_t tick;
    uint16_t air_ticks;
    uint8_t valid, discontinuous, boosting, spinning, immersive;
} PortableRenderChase;
typedef struct
{
    double position[3], target[3], right[3], up[3], forward[3];
    double swing;       /* Drift yaw toward the travel direction, radians. */
    double focal_scale; /* Multiply fresh, unscaled viewport focal lengths. */
} PortableRenderChaseSnapshot;
typedef enum
{
    PORTABLE_RENDER_CHASE_OK = 0,
    PORTABLE_RENDER_CHASE_INVALID,
    PORTABLE_RENDER_CHASE_BUDGET,
    PORTABLE_RENDER_CHASE_OBSTRUCTED
} PortableRenderChaseStatus;

/* Zero initialize before first step. One accepted tick per call, same-tick
 * calls are immutable. Cuts may reset a lower clock. Geometry remains borrowed;
 * indexed triangle queries are used where supplied. Bounded conservative sphere
 * samples include edge/vertex clearance; invalid/budget failures preserve out.
 * The kart's position is followed rigidly; only direction, pitch and up lag. */
PortableRenderChaseStatus portable_render_chase_step(PortableRenderChase *out,
                                                     const PortableRenderChase *before,
                                                     const PortableRenderChaseInput *input,
                                                     const PortableMeshCourse *course, char *error,
                                                     size_t capacity);
/* Rendering only interpolates immutable accepted owners. On a cut, the caller
 * publishes previous=current. Obstruction cuts snap automatically; otherwise
 * interpolated eye segments were checked at step.
 * Failure preserves out; viewport, HUD and simulation state remain untouched. */
int portable_render_chase_snapshot(PortableRenderChaseSnapshot *out,
                                   const PortableRenderChase *previous,
                                   const PortableRenderChase *current, uint32_t alpha_q16);

#endif
