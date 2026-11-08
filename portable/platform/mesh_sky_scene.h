#ifndef PORTABLE_MESH_SKY_SCENE_H
#define PORTABLE_MESH_SKY_SCENE_H

#include "mesh_sky.h"
#include "render_scene.h"

/* A source split is two immutable instances with disjoint viewport intervals.
 * Part0 uses the cached ordinary scroll; rows at/after split_y use the source
 * split coordinate. Failure preserves both outputs and count. */
int portable_mesh_sky_scene_instances_racer(const PortableMeshSkyConfig *, const PortableMeshSky *,
                                            unsigned racer, unsigned layer,
                                            const PortableRenderInstance *,
                                            const PortableRenderView *, unsigned pane,
                                            PortableRenderInstance out[2], unsigned *count);

/* GPU/GLES presentation only. A source panorama faces local -Y at U origin.
 * Resolve one immutable accepted clock for one camera, without changing either
 * owner or shared mesh/material bytes. Failure preserves output; aliasing works. */
int portable_mesh_sky_scene_instance(const PortableMeshSkyConfig *, const PortableMeshSky *,
                                     unsigned layer, const PortableRenderInstance *,
                                     const PortableRenderView *, unsigned pane,
                                     PortableRenderInstance *out);
int portable_mesh_sky_scene_instance_racer(const PortableMeshSkyConfig *, const PortableMeshSky *,
                                           unsigned racer, unsigned layer,
                                           const PortableRenderInstance *,
                                           const PortableRenderView *, unsigned pane,
                                           PortableRenderInstance *out);

#endif
