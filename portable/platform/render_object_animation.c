#include "render_object_animation.h"
#include "render_model.h"
#include "render_pose.h"
#include "race_interpolation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t alpha(uint32_t a) { return a > 65536u ? 65536u : a; }
static int continuous(const int32_t a[3], const int32_t b[3])
{
    for (unsigned i = 0; i < 3; ++i)
        if (llabs((int64_t)b[i] - a[i]) > INT64_C(64) * 65536) return 0;
    return 1;
}
static int32_t blend(int32_t a, int32_t b, uint32_t t)
{
    return (int32_t)(a + ((int64_t)b - a) * t / 65536);
}
double portable_render_clip_loop_frame(uint32_t tick, uint32_t t, uint32_t frames)
{
    if (!frames)
        return 0;
    t = alpha(t);
    if (!tick || t >= 65536u)
        return tick % frames;
    return (tick - 1u) % frames + t / 65536.0;
}
void portable_render_course_actor_sample(const PortableMeshCourseActorView *a,
    PortableMeshCourseActorView *b, uint32_t t)
{
    t = alpha(t);
    if (!a || !b || !a->visible || !b->visible || a->object_index != b->object_index ||
        a->flip_y != b->flip_y || !continuous(a->position_q16, b->position_q16)) return;
    for (unsigned i = 0; i < 3; ++i)
        b->position_q16[i] = blend(a->position_q16[i], b->position_q16[i], t);
    if (a->affine_x_q8 && a->affine_y_q8 && b->affine_x_q8 && b->affine_y_q8)
    {
        b->affine_x_q8 = (uint16_t)blend(a->affine_x_q8, b->affine_x_q8, t);
        b->affine_y_q8 = (uint16_t)blend(a->affine_y_q8, b->affine_y_q8, t);
    }
    b->rotation_angle = portable_angle_interpolate(a->rotation_angle, b->rotation_angle, t);
    b->view_heading = portable_angle_interpolate(a->view_heading, b->view_heading, t);
    if (a->has_facing && b->has_facing && a->turn_flip == b->turn_flip &&
        (double)a->facing_x_q16 * b->facing_x_q16 + (double)a->facing_y_q16 * b->facing_y_q16 > 0)
    {
        b->facing_x_q16 = blend(a->facing_x_q16, b->facing_x_q16, t);
        b->facing_y_q16 = blend(a->facing_y_q16, b->facing_y_q16, t);
    }
}
void portable_render_item_sample(const PortableMeshItemPose *previous, size_t count,
    const PortableMeshItemPose *current, uint32_t t, double position[3])
{
    t = alpha(t);
    const PortableMeshItemPose *a = current;
    for (size_t i = 0; previous && i < count; ++i)
        if (previous[i].serial == current->serial && previous[i].item == current->item &&
            previous[i].owner == current->owner && previous[i].phase == current->phase &&
            continuous(previous[i].position_q16, current->position_q16))
        { a = &previous[i]; break; }
    for (unsigned i = 0; i < 3; ++i)
        position[i] = (a->position_q16[i] + ((double)current->position_q16[i] - a->position_q16[i]) * t / 65536.0) / 65536.0;
}
void portable_render_sky_sample(const PortableRenderInstance *a, PortableRenderInstance *b, uint32_t t)
{
    if (!a || !b || a->clip_y_extent != b->clip_y_extent || a->clip_y_begin != b->clip_y_begin ||
        a->clip_y_end != b->clip_y_end || fabs(a->transform[11] - b->transform[11]) > 64) return;
    const double rewind = 1 - alpha(t) / 65536.0;
    b->transform[11] += rewind * (a->transform[11] - b->transform[11]);
    for (unsigned i = 0; i < 2; ++i)
        b->uv_offset[i] += rewind * remainder(a->uv_offset[i] - b->uv_offset[i], 1.0);
}

void portable_render_motion_sample(const PortableMeshMotion *motion, uint32_t tick,
    uint32_t t, PortableRenderInstance *instance)
{
    /* Accepted ticks use the shared collision-matching pose exactly. */
    if (alpha(t) >= 65536u)
    {
        portable_render_model_motion(motion, tick, instance->transform);
        return;
    }
    PortableMeshPose a, b;
    portable_mesh_motion_pose(motion, tick ? tick - 1u : tick, &a);
    portable_mesh_motion_pose(motion, tick, &b);
    PortableRenderPose pa = {0}, pb = {0}, pose;
    for (unsigned i = 0; i < 3; ++i)
    {
        pa.forward[i] = a.rotation_q30[i * 3 + 1] / 1073741824.0;
        pb.forward[i] = b.rotation_q30[i * 3 + 1] / 1073741824.0;
        pa.up[i] = a.rotation_q30[i * 3 + 2] / 1073741824.0;
        pb.up[i] = b.rotation_q30[i * 3 + 2] / 1073741824.0;
    }
    if (!portable_render_pose_interpolate(&pose, &pa, &pb, t))
    {
        portable_render_model_motion(motion, tick, instance->transform);
        return;
    }
    const double right[3] = {pose.forward[1]*pose.up[2]-pose.forward[2]*pose.up[1],
        pose.forward[2]*pose.up[0]-pose.forward[0]*pose.up[2],
        pose.forward[0]*pose.up[1]-pose.forward[1]*pose.up[0]};
    double rest[12];
    memcpy(rest, instance->transform, sizeof rest);
    for (unsigned row = 0; row < 3; ++row)
    {
        const double r[3] = {right[row], pose.forward[row], pose.up[row]};
        for (unsigned col = 0; col < 3; ++col)
            instance->transform[row*4+col] = r[0]*rest[col]+r[1]*rest[4+col]+r[2]*rest[8+col];
        /* A route offset can span twice the world limit while its final
         * transformed point remains in bounds; it is not a world position. */
        const double offset = a.offset_q16[row] + t / 65536.0 * ((double)b.offset_q16[row] - a.offset_q16[row]);
        double moved = motion->pivot_q16[row]/65536.0 + offset/65536.0;
        for (unsigned axis = 0; axis < 3; ++axis)
            moved += r[axis]*(rest[axis*4+3]-motion->pivot_q16[axis]/65536.0);
        instance->transform[row*4+3] = moved;
    }
}
