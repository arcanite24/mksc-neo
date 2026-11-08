#ifndef PORTABLE_SCENE_3D_WORKERS_H
#define PORTABLE_SCENE_3D_WORKERS_H

#include "scene_3d.h"

/* Internal synchronous dispatch. -1 means unavailable, with no pixels written. */
int portable_scene_3d_workers_quad(PortableScene3D *scene,
                                   const PortableScene3DProjection *projection,
                                   const PortableScene3DVertex vertices[4],
                                   PortableScene3DSampler sampler, const void *userdata);
void portable_scene_3d_workers_close(PortableScene3D *scene);

#endif
