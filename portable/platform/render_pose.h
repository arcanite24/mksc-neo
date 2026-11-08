#ifndef PORTABLE_RENDER_POSE_H
#define PORTABLE_RENDER_POSE_H

#include "mesh_kart.h"
#include "render_scene.h"

/* Presentation-only SI coordinates and an orthonormal terrain frame. No
 * pointers or simulation clocks are retained by these value operations. */
typedef struct
{
    double position[3], forward[3], up[3];
} PortableRenderPose;

/* All operations preserve their output on failure and permit output/input
 * aliasing. Forward is projected onto the supplied up plane when extracting
 * a kart whose suspension normal is still settling. */
int portable_render_pose_from_kart(PortableRenderPose *out, const PortableMeshKartState *kart);
/* Alpha clamps to [0,65536]. Near-opposite terrain frames snap to current;
 * lifecycle/teleport discontinuities must also be snapped by the caller. */
int portable_render_pose_interpolate(PortableRenderPose *out,
                                      const PortableRenderPose *previous,
                                      const PortableRenderPose *current, uint32_t alpha_q16);
/* Only position/right/up/forward are written; viewport/projection stay owned
 * by the caller. Pitch is radians, distance/elevation are metres. */
int portable_render_pose_chase(const PortableRenderPose *pose, double distance,
                                double elevation, double pitch, PortableRenderView *view);
/* Row-major affine transform; local forward is +Y, up is +Z. The existing
 * box's local +X is forward cross up (opposite camera right). */
int portable_render_pose_actor(const PortableRenderPose *pose, double clearance,
                                double transform[12]);

typedef struct
{
    PortableRenderPose body, floor; /* Origins at the visible foot and its ground projection. */
    double gap;
    uint8_t floor_valid;
} PortableRenderSupport;
/* Interpolate accepted seating rather than reprojecting onto the current
 * moving floor at draw time. A missing floor keeps the current shadow state. */
int portable_render_support_interpolate(PortableRenderSupport *out,
    const PortableRenderSupport *previous, const PortableRenderSupport *current, uint32_t alpha_q16);
/* Grounded seating uses the accepted contact triangle, never a roof above the
 * kart. Airborne bodies retain their displacement; a bounded downward query
 * supplies only the optional shadow. Reads immutable geometry/state. */
int portable_render_pose_support(PortableRenderSupport *out, const PortableRenderPose *pose,
                                  const PortableMeshKartState *kart,
                                  const PortableMeshCourse *course, double clearance);

#endif
