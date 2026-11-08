#ifndef PORTABLE_RENDER_OBJECT_ANIMATION_H
#define PORTABLE_RENDER_OBJECT_ANIMATION_H
#include "mesh_course_actors.h"
#include "mesh_motion.h"
#include "render_scene.h"

/* Pure presentation sampling. Visibility/art remain current. Stable placement
 * or actor-generation identity and a bounded displacement are required. */
void portable_render_course_actor_sample(const PortableMeshCourseActorView *previous,
    PortableMeshCourseActorView *current, uint32_t alpha_q16);
void portable_render_item_sample(const PortableMeshItemPose *previous, size_t count,
    const PortableMeshItemPose *current, uint32_t alpha_q16, double position[3]);
/* Both sky instances must be resolved for the same interpolated camera. */
void portable_render_sky_sample(const PortableRenderInstance *previous,
    PortableRenderInstance *current, uint32_t alpha_q16);
/* The fractional frame of a looping clip of `frames` frames played at one
 * frame per tick: tick exactly when alpha is whole, else between tick-1 and
 * tick, like motion sampling. Always in [0, frames); 0 without frames. */
double portable_render_clip_loop_frame(uint32_t tick, uint32_t alpha_q16, uint32_t frames);
/* Pose authored moving geometry between two accepted collision ticks. */
void portable_render_motion_sample(const PortableMeshMotion *motion, uint32_t tick,
    uint32_t alpha_q16, PortableRenderInstance *instance);
#endif
