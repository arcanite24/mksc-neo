#include "render_pose.h"
#include "mesh_query.h"

#include <math.h>
#include <string.h>

static double dot(const double a[3], const double b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static int normalize(double vector[3])
{
    const double squared = dot(vector, vector);
    if (!isfinite(squared) || squared < 1e-12 || squared > 16.0)
        return 0;
    const double length = sqrt(squared);
    for (unsigned axis = 0; axis < 3u; ++axis)
        vector[axis] /= length;
    return 1;
}

static int orthonormalize(PortableRenderPose *pose)
{
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (!isfinite(pose->position[axis]) || fabs(pose->position[axis]) > 4096.0 ||
            !isfinite(pose->forward[axis]) || !isfinite(pose->up[axis]))
            return 0;
    if (!normalize(pose->up))
        return 0;
    const double vertical = dot(pose->forward, pose->up);
    for (unsigned axis = 0; axis < 3u; ++axis)
        pose->forward[axis] -= vertical * pose->up[axis];
    return normalize(pose->forward);
}

int portable_render_pose_from_kart(PortableRenderPose *out, const PortableMeshKartState *kart)
{
    if (!out || !kart)
        return 0;
    PortableRenderPose pose;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        pose.position[axis] = (double)kart->position_q16[axis] / 65536.0;
        pose.forward[axis] = (double)kart->forward_q30[axis] / 1073741824.0;
        pose.up[axis] = (double)kart->up_q30[axis] / 1073741824.0;
    }
    if (!orthonormalize(&pose))
        return 0;
    *out = pose;
    return 1;
}

int portable_render_pose_interpolate(PortableRenderPose *out,
                                      const PortableRenderPose *previous,
                                      const PortableRenderPose *current, uint32_t alpha_q16)
{
    if (!out || !previous || !current)
        return 0;
    PortableRenderPose a = *previous, b = *current;
    if (!orthonormalize(&a) || !orthonormalize(&b))
        return 0;
    if (alpha_q16 >= 65536u)
    {
        *out = b;
        return 1;
    }
    if (!alpha_q16)
    {
        *out = a;
        return 1;
    }
    /* Nlerp becomes ill-conditioned near 180 degrees. A discontinuity is
     * preferable to a camera frame passing through a zero-length axis. */
    if (dot(a.up, b.up) < -0.95 || dot(a.forward, b.forward) < -0.95)
    {
        *out = b;
        return 1;
    }
    const double alpha = (double)alpha_q16 / 65536.0;
    PortableRenderPose pose;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        pose.position[axis] = a.position[axis] + (b.position[axis] - a.position[axis]) * alpha;
        pose.up[axis] = a.up[axis] + (b.up[axis] - a.up[axis]) * alpha;
        pose.forward[axis] = a.forward[axis] + (b.forward[axis] - a.forward[axis]) * alpha;
    }
    if (!orthonormalize(&pose))
        pose = b;
    *out = pose;
    return 1;
}

int portable_render_pose_chase(const PortableRenderPose *pose, double distance,
                                double elevation, double pitch, PortableRenderView *view)
{
    if (!pose || !view || !isfinite(distance) || distance < 0.0 || distance > 4096.0 ||
        !isfinite(elevation) || fabs(elevation) > 4096.0 || !isfinite(pitch))
        return 0;
    PortableRenderPose basis = *pose;
    if (!orthonormalize(&basis))
        return 0;
    PortableRenderView result = *view;
    cross(basis.up, basis.forward, result.right);
    const double cosine = cos(pitch), sine = sin(pitch);
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        result.position[axis] = basis.position[axis] - distance * basis.forward[axis] +
                                elevation * basis.up[axis];
        if (fabs(result.position[axis]) > 4096.0)
            return 0;
        result.up[axis] = basis.up[axis] * cosine - basis.forward[axis] * sine;
        result.forward[axis] = basis.forward[axis] * cosine + basis.up[axis] * sine;
    }
    *view = result;
    return 1;
}

int portable_render_pose_actor(const PortableRenderPose *pose, double clearance,
                                double transform[12])
{
    if (!pose || !transform || !isfinite(clearance) || clearance < 0.0 || clearance > 4096.0)
        return 0;
    PortableRenderPose basis = *pose;
    if (!orthonormalize(&basis))
        return 0;
    double side[3], result[12];
    cross(basis.forward, basis.up, side);
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        result[axis * 4u] = side[axis];
        result[axis * 4u + 1u] = basis.forward[axis];
        result[axis * 4u + 2u] = basis.up[axis];
        result[axis * 4u + 3u] = basis.position[axis] - clearance * basis.up[axis];
        if (fabs(result[axis * 4u + 3u]) > 4096.0)
            return 0;
    }
    memcpy(transform, result, sizeof result);
    return 1;
}

int portable_render_support_interpolate(PortableRenderSupport *out,
    const PortableRenderSupport *previous, const PortableRenderSupport *current, uint32_t alpha_q16)
{
    if (!out || !previous || !current) return 0;
    PortableRenderSupport next = *current;
    if (!portable_render_pose_interpolate(&next.body, &previous->body, &current->body, alpha_q16)) return 0;
    if (previous->floor_valid && current->floor_valid)
    {
        if (!portable_render_pose_interpolate(&next.floor, &previous->floor, &current->floor, alpha_q16)) return 0;
        const double alpha = (alpha_q16 > 65536u ? 65536u : alpha_q16) / 65536.0;
        next.gap = previous->gap + alpha * (current->gap - previous->gap);
    }
    *out = next;
    return 1;
}

int portable_render_pose_support(PortableRenderSupport *out, const PortableRenderPose *pose,
                                  const PortableMeshKartState *kart,
                                  const PortableMeshCourse *course, double clearance)
{
    if (!out || !pose || !isfinite(clearance) || clearance <= 0 || clearance > 4096)
        return 0;
    PortableRenderSupport next = {.body = *pose};
    if (!orthonormalize(&next.body)) return 0;
    for (unsigned a = 0; a < 3; ++a)
        next.body.position[a] -= clearance * next.body.up[a];
    int seated = 0;
    if (kart && course && kart->grounded)
    {
        PortableMeshTriangle triangle;
        int32_t normal[3];
        if (!course->triangle || kart->contact_triangle >= course->triangle_count ||
            !course->triangle(course->context, kart->contact_triangle, &triangle) ||
            !portable_mesh_query_normal(&triangle, normal)) /* loop floors may face down */
            return 0;
        /* The floor faces the kart's side: imported collision may wind a
         * two-sided floor either way. A kart perched on a wall's top edge
         * (MK8 edge support) has a wall for contact: probe the floor below
         * instead, as in the air. */
        double distance = 0, facing = 0;
        PortableRenderPose floor = next.body;
        for (unsigned a = 0; a < 3; ++a)
            facing += (double)normal[a] / 1073741824.0 * pose->up[a];
        for (unsigned a = 0; a < 3; ++a)
        {
            floor.up[a] = (facing < 0 ? -(double)normal[a] : (double)normal[a]) / 1073741824.0;
            distance += (pose->position[a] - triangle.vertices_q16[0][a] / 65536.0) * floor.up[a];
        }
        if (!isfinite(distance))
            return 0;
        if (fabs(facing) >= .2 && distance >= -.25 && distance <= clearance + .5)
        {
            if (!orthonormalize(&floor)) return 0;
            for (unsigned a = 0; a < 3; ++a)
                floor.position[a] = pose->position[a] - distance * floor.up[a];
            next.body = next.floor = floor;
            next.floor_valid = 1;
            seated = 1;
        }
    }
    if (kart && course && !seated)
    {
        int32_t center[3];
        for (unsigned a = 0; a < 3; ++a)
        {
            const double value = next.body.position[a] + (a == 2 ? .004 : 0);
            if (!isfinite(value) || fabs(value) > 4096) return 0;
            center[a] = (int32_t)llround(value * 65536.0);
        }
        PortableMeshSupportHit hit;
        const PortableMeshQueryStatus status = portable_mesh_query_support(
            course, center, 1, 3 * 65536, 1, &hit);
        if (status == PORTABLE_MESH_QUERY_INVALID) return 0;
        if (status == PORTABLE_MESH_QUERY_HIT)
        {
            next.floor = next.body;
            for (unsigned a = 0; a < 3; ++a)
            {
                next.floor.up[a] = hit.normal_q30[a] / 1073741824.0;
                next.floor.position[a] = hit.center_q16[a] / 65536.0 -
                                        next.floor.up[a] / 65536.0;
            }
            if (!orthonormalize(&next.floor)) return 0;
            double delta[3];
            for (unsigned a = 0; a < 3; ++a)
                delta[a] = next.body.position[a] - next.floor.position[a];
            next.gap = fmax(0, dot(delta, next.floor.up));
            next.floor_valid = 1;
        }
    }
    for (unsigned a = 0; a < 3; ++a)
        if (!isfinite(next.body.position[a]) || fabs(next.body.position[a]) > 4096) return 0;
    *out = next;
    return 1;
}
