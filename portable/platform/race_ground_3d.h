#ifndef PORTABLE_RACE_GROUND_3D_H
#define PORTABLE_RACE_GROUND_3D_H

#include "race_scene_renderer.h"
#include "scene_3d.h"

typedef enum {
    PORTABLE_RACE_GROUND_3D_OK = 0,
    PORTABLE_RACE_GROUND_3D_INVALID,
    PORTABLE_RACE_GROUND_3D_CONTENT,
    PORTABLE_RACE_GROUND_3D_UNSUPPORTED
} PortableRaceGround3DStatus;

/* Flat worldZ0 plane, using the complete world map (not the rolling BG window).
 * The caller begins depth, supplies its ambient-resolved tileset palette and
 * owns sky/horizon clipping/blending. prior_physical_y is the shared actor
 * draw translation; near/far use source actor-distance units, not world units. */
PortableRaceGround3DStatus portable_race_ground_3d_render(PortableScene3D *target,
    const PortableRaceSceneView *scene, const PortableCameraState *camera,
    const PortableCameraProjection *projection, int prior_physical_y,
    double near_z, double far_z);

#endif
