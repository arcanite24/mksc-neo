#include "mesh_sky_scene.h"
#include <math.h>

static int bounded(double value)
{
    return isfinite(value) && fabs(value) <= 32768.0;
}

static int sky_instance(const PortableMeshSkyConfig *config, const PortableMeshSky *world,
                        unsigned racer, unsigned layer, const PortableRenderInstance *source,
                        const PortableRenderView *view, unsigned pane, PortableRenderInstance *out,
                        PortableSkyMotionSnapshot *snapshot)
{
    if (!config || !world || !source || !view || !out || layer >= 3u || pane >= 4u ||
        !config->enabled || !(config->render_mask & (1u << layer)) || !source->mesh ||
        source->flags || source->hidden_views > 15u)
        return 0;
    for (unsigned i = 0; i < 12u; ++i)
        if (!bounded(source->transform[i]))
            return 0;
    for (unsigned i = 0; i < 3u; ++i)
        if (!bounded(view->position[i]) || !isfinite(view->forward[i]) || !isfinite(view->right[i]))
            return 0;
    if (!bounded(source->uv_offset[0]) || !bounded(source->uv_offset[1]))
        return 0;

    /* Rotate the cylinder with the camera, then supply source yaw in U exactly
     * once. Leaving a stationary cylinder would add its geometric yaw again.
     * A vertical camera uses its horizontal right vector to define yaw. */
    double x = view->forward[0], y = -view->forward[1];
    if (hypot(x, y) < 1e-12)
    {
        x = view->right[1];
        y = view->right[0];
    }
    if (hypot(x, y) < 1e-12)
        return 0;
    const double tau = 6.28318530717958647693;
    const double angle = atan2(x, y), sine = sin(angle), cosine = cos(angle);
    const int32_t heading = (int32_t)lround(angle * 65536.0 / tau);
    PortableSkyMotionSnapshot motion;
    if (!portable_mesh_sky_snapshot_racer(config, world, racer, (uint16_t)(uint32_t)heading,
                                          &motion))
        return 0;
    PortableRenderInstance staged = *source;
    for (unsigned column = 0; column < 3u; ++column)
    {
        staged.transform[column] =
            cosine * source->transform[column] - sine * source->transform[4u + column];
        staged.transform[4u + column] =
            sine * source->transform[column] + cosine * source->transform[4u + column];
    }
    /* Panoramas are infinitely distant: translation, including a kart hop,
     * cannot introduce parallax. Authored Z is an offset from the camera. */
    staged.transform[3] = view->position[0];
    staged.transform[7] = view->position[1];
    const double radius = config->radius_q16[layer] / 65536.0;
    const double vertical = motion.scroll_y_q16[layer] / 65536.0 - config->rules.y_bias[layer];
    staged.transform[11] = view->position[2] + source->transform[11] + vertical * radius / 136.0;
    const double horizontal = motion.scroll_x_q16[layer] / 65536.0 - config->rules.x_bias[layer];
    staged.uv_offset[0] =
        fmod(source->uv_offset[0] + horizontal / config->logical_width[layer], 1.0);
    staged.hidden_views |= 15u & ~(1u << pane);
    for (unsigned i = 0; i < 12u; ++i)
        if (!bounded(staged.transform[i]))
            return 0;
    if (!bounded(staged.uv_offset[0]))
        return 0;
    *out = staged;
    if (snapshot)
        *snapshot = motion;
    return 1;
}

int portable_mesh_sky_scene_instance_racer(const PortableMeshSkyConfig *config,
                                           const PortableMeshSky *world, unsigned racer,
                                           unsigned layer, const PortableRenderInstance *source,
                                           const PortableRenderView *view, unsigned pane,
                                           PortableRenderInstance *out)
{
    return sky_instance(config, world, racer, layer, source, view, pane, out, NULL);
}

int portable_mesh_sky_scene_instances_racer(const PortableMeshSkyConfig *config,
                                            const PortableMeshSky *world, unsigned racer,
                                            unsigned layer, const PortableRenderInstance *source,
                                            const PortableRenderView *view, unsigned pane,
                                            PortableRenderInstance out[2], unsigned *count)
{
    PortableRenderInstance staged[2];
    PortableSkyMotionSnapshot motion;
    if (!out || !count || !source || source->clip_y_extent || source->clip_y_begin ||
        source->clip_y_end ||
        !sky_instance(config, world, racer, layer, source, view, pane, &staged[0], &motion))
        return 0;
    unsigned parts = 1;
    if (motion.split_y >= 0 && layer == motion.split_layer)
    {
        staged[1] = staged[0];
        if (motion.split_vertical)
        {
            const double delta =
                ((int64_t)motion.split_scroll_y_q16 - motion.scroll_y_q16[layer]) / 65536.0;
            staged[1].transform[11] += delta * (config->radius_q16[layer] / 65536.0) / 136.0;
        }
        else
            staged[1].uv_offset[0] +=
                ((int64_t)motion.split_scroll_x_q16 - motion.scroll_x_q16[layer]) / 65536.0 /
                config->logical_width[layer];
        if (!bounded(staged[1].transform[11]) || !bounded(staged[1].uv_offset[0]))
            return 0;
        if (motion.split_y == 0)
            staged[0] = staged[1];
        else
        {
            staged[0].clip_y_end = (uint32_t)motion.split_y;
            staged[0].clip_y_extent = 160;
            staged[1].clip_y_begin = (uint32_t)motion.split_y;
            staged[1].clip_y_end = staged[1].clip_y_extent = 160;
            parts = 2;
        }
    }
    out[0] = staged[0];
    if (parts == 2)
        out[1] = staged[1];
    *count = parts;
    return 1;
}

int portable_mesh_sky_scene_instance(const PortableMeshSkyConfig *config,
                                     const PortableMeshSky *world, unsigned layer,
                                     const PortableRenderInstance *source,
                                     const PortableRenderView *view, unsigned pane,
                                     PortableRenderInstance *out)
{
    return portable_mesh_sky_scene_instance_racer(config, world, 0, layer, source, view, pane, out);
}
