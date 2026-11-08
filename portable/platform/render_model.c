#include "render_model.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failure(char *error, size_t capacity, const PortableModelBinding *binding, uint32_t row,
                   const char *reason)
{
    if (error && capacity)
        snprintf(error, capacity, "3D render model=%u target=%u row=%u: %s",
                 binding ? binding->mesh_id : 0u, binding ? binding->target_id : 0u, row, reason);
    return 0;
}

static int reserve(size_t *size, size_t count, size_t stride, size_t *offset)
{
    if (*size > SIZE_MAX - 7u)
        return 0;
    const size_t aligned = (*size + 7u) & ~(size_t)7u;
    if (count > (SIZE_MAX - aligned) / stride)
        return 0;
    *offset = aligned;
    *size = aligned + count * stride;
    return 1;
}

/* MSH1 carries no normals: each vertex takes the area-weighted normal of the
 * faces sharing its position (Gouraud shading across welded seams). */
typedef struct
{
    double position[3];
    uint32_t index;
} WeldKey;
static int position_order(const void *a, const void *b)
{
    const WeldKey *p = a, *q = b;
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (p->position[axis] != q->position[axis])
            return p->position[axis] < q->position[axis] ? -1 : 1;
    return p->index < q->index ? -1 : 1;
}

static void smooth_normals(PortableRenderVertex *vertices, uint32_t vertex_count, const uint32_t *indices,
                           uint32_t index_count)
{
    for (uint32_t i = 0; i + 2u < index_count; i += 3u)
    {
        const double *a = vertices[indices[i]].position, *b = vertices[indices[i + 1u]].position,
                     *c = vertices[indices[i + 2u]].position;
        const double ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ac[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const double n[3] = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                             ab[0] * ac[1] - ab[1] * ac[0]};
        for (unsigned k = 0; k < 3u; ++k)
            for (unsigned axis = 0; axis < 3u; ++axis)
                vertices[indices[i + k]].normal[axis] += n[axis];
    }
    WeldKey keys[PORTABLE_MODEL_MAX_VERTICES];
    uint32_t order[PORTABLE_MODEL_MAX_VERTICES];
    for (uint32_t i = 0; i < vertex_count; ++i)
    {
        memcpy(keys[i].position, vertices[i].position, sizeof keys[i].position);
        keys[i].index = i;
    }
    qsort(keys, vertex_count, sizeof *keys, position_order);
    for (uint32_t i = 0; i < vertex_count; ++i)
        order[i] = keys[i].index;
    for (uint32_t first = 0; first < vertex_count;)
    {
        uint32_t last = first + 1u;
        double sum[3] = {vertices[order[first]].normal[0], vertices[order[first]].normal[1],
                         vertices[order[first]].normal[2]};
        while (last < vertex_count &&
               !memcmp(keys[last].position, keys[first].position, sizeof sum))
        {
            for (unsigned axis = 0; axis < 3u; ++axis)
                sum[axis] += vertices[order[last]].normal[axis];
            ++last;
        }
        const double length = sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
        for (uint32_t i = first; i < last; ++i)
            for (unsigned axis = 0; axis < 3u; ++axis)
                vertices[order[i]].normal[axis] = length > 0.0 ? sum[axis] / length : 0.0;
        first = last;
    }
}

static int material_valid(const PortableModelMaterial *material)
{
    if (material->flags & ~(uint32_t)(PORTABLE_MODEL_WRAP_U | PORTABLE_MODEL_WRAP_V | PORTABLE_MODEL_LIT |
                                      PORTABLE_MODEL_FOG | PORTABLE_MODEL_BLEND))
        return 0;
    const PortableNativeImage *texture = &material->texture;
    if (!texture->pixels)
        return !texture->palette;
    return texture->width && texture->height &&
           texture->width <= PORTABLE_MODEL_MAX_TEXTURE_EXTENT &&
           texture->height <= PORTABLE_MODEL_MAX_TEXTURE_EXTENT &&
           (!texture->palette ||
            (texture->palette_count &&
             (unsigned)texture->palette_base + texture->palette_count <= 256u));
}

int portable_render_resources_open_model(PortableRenderResources *resources,
                                         const PortableModelBinding *binding,
                                         const PortableAssetPackView *pack, char *error,
                                         size_t capacity)
{
    if (!resources || !binding || !pack)
        return failure(error, capacity, binding, 0, "missing resources/binding/pack");
    const PortableModelView *source = &binding->mesh;
    if (!source->vertices || !source->triangles || !source->vertex_count ||
        source->vertex_count > PORTABLE_MODEL_MAX_VERTICES || !source->triangle_count ||
        source->triangle_count > PORTABLE_MODEL_MAX_TRIANGLES || !source->material_count ||
        source->material_count > PORTABLE_MODEL_MAX_MATERIALS)
        return failure(error, capacity, binding, 0, "invalid model counts/storage");
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (!binding->scale_q16[axis])
            return failure(error, capacity, binding, axis, "zero binding scale");
    for (uint32_t i = 0; i < source->material_count; ++i)
        if (!material_valid(&source->materials[i]))
            return failure(error, capacity, binding, i, "invalid material/texture");
    PortableModelRig rig;
    const uint32_t swaps =
        portable_model_rig_open(pack, binding->mesh_id, source, &rig) ? rig.swap_count : 0u;
    size_t size = 0, vertex_offset, index_offset, material_offset, run_offset, swap_offset,
           start_offset, key_offset;
    const uint32_t index_count = source->triangle_count * 3u;
    if (!reserve(&size, source->vertex_count, sizeof(PortableRenderVertex), &vertex_offset) ||
        !reserve(&size, index_count, sizeof(uint32_t), &index_offset) ||
        !reserve(&size, source->material_count, sizeof(PortableRenderMaterial), &material_offset) ||
        !reserve(&size, source->triangle_count, sizeof(PortableRenderRun), &run_offset) ||
        !reserve(&size, swaps, sizeof(PortableRenderTextureAnimation), &swap_offset) ||
        !reserve(&size, swaps * 2u, sizeof(uint32_t), &start_offset) ||
        !reserve(&size, swaps * 2u, sizeof(PortableNativeImage), &key_offset))
        return failure(error, capacity, binding, 0, "allocation size overflow");
    uint8_t *storage = calloc(1, size);
    if (!storage)
        return failure(error, capacity, binding, 0, "allocation failed");
    PortableRenderVertex *vertices = (PortableRenderVertex *)(void *)(storage + vertex_offset);
    uint32_t *indices = (uint32_t *)(void *)(storage + index_offset);
    PortableRenderMaterial *materials =
        (PortableRenderMaterial *)(void *)(storage + material_offset);
    PortableRenderRun *runs = (PortableRenderRun *)(void *)(storage + run_offset);
    const char *problem = NULL;
    uint32_t row = 0, run_count = 0;
    for (uint32_t i = 0; i < source->vertex_count; ++i)
    {
        int32_t xyz[3], uv[2];
        if (!portable_model_vertex(source, i, xyz, uv))
        {
            problem = "vertex decode failed";
            row = i;
            break;
        }
        for (unsigned axis = 0; axis < 3u; ++axis)
            vertices[i].position[axis] = (double)xyz[axis] / 65536.0;
        for (unsigned axis = 0; axis < 2u; ++axis)
            vertices[i].uv[axis] = (double)uv[axis] / 65536.0;
    }
    for (uint32_t i = 0; i < source->material_count; ++i)
    {
        const uint32_t flags = source->materials[i].flags;
        materials[i] =
            (PortableRenderMaterial){.argb = source->materials[i].argb,
                                     /* Blended materials draw straight alpha (32) instead of the cutoff (16). */
                                     .flags = (flags & (PORTABLE_MODEL_WRAP_U | PORTABLE_MODEL_WRAP_V)) | 4u |
                                              (flags & PORTABLE_MODEL_BLEND ? 32u : 16u) |
                                              (flags & PORTABLE_MODEL_LIT ? PORTABLE_RENDER_MATERIAL_LIT : 0u) |
                                              (flags & PORTABLE_MODEL_FOG ? PORTABLE_RENDER_MATERIAL_FOG : 0u),
                                     /* R2's sampler keeps byte alpha >=128 after material tint. */
                                     .alpha_cutoff_q16 = flags & PORTABLE_MODEL_BLEND ? 1 : 32768,
                                     .texture = source->materials[i].texture,
                                     /* Until the course supplies its model light (DS kartcrsmat). */
                                     .ambient_argb = PORTABLE_RENDER_MODEL_AMBIENT,
                                     .emission_argb = PORTABLE_RENDER_MODEL_EMISSION};
    }
    /* A rig's material swaps become texture patterns: the slot's own texture
     * from frame 0, then each of its swaps' alternates from their starts (a
     * blink, or a flipbook when one slot has several). */
    PortableRenderTextureAnimation *animations =
        (PortableRenderTextureAnimation *)(void *)(storage + swap_offset);
    uint32_t *starts = (uint32_t *)(void *)(storage + start_offset);
    PortableNativeImage *keys = (PortableNativeImage *)(void *)(storage + key_offset);
    uint32_t key_count = 0, animation_count = 0;
    for (uint32_t i = 0; i < swaps; ++i)
    {
        uint32_t seen = 0;
        while (seen < i && rig.swaps[seen].slot != rig.swaps[i].slot)
            ++seen;
        if (seen < i)
            continue;
        PortableRenderTextureAnimation *animation = &animations[animation_count++];
        *animation = (PortableRenderTextureAnimation){.key_frames = rig.swaps[i].period, .key_count = 1u,
                                                      .key_starts = starts + key_count,
                                                      .key_textures = keys + key_count};
        starts[key_count] = 0;
        keys[key_count++] = source->materials[rig.swaps[i].slot].texture;
        for (uint32_t j = i; j < swaps; ++j)
            if (rig.swaps[j].slot == rig.swaps[i].slot)
            {
                starts[key_count] = rig.swaps[j].start;
                keys[key_count++] = source->materials[rig.swaps[j].alternate].texture;
                ++animation->key_count;
            }
        materials[rig.swaps[i].slot].animation = animation;
    }
    for (uint32_t i = 0; !problem && i < source->triangle_count; ++i)
    {
        uint16_t corners[3], material;
        if (!portable_model_triangle(source, i, corners, &material) ||
            material >= source->material_count)
        {
            problem = "triangle/material decode failed";
            row = i;
            break;
        }
        for (unsigned corner = 0; corner < 3u; ++corner)
        {
            if (corners[corner] >= source->vertex_count)
            {
                problem = "triangle vertex outside model";
                row = i;
                break;
            }
            indices[i * 3u + corner] = corners[corner];
        }
        if (problem)
            break;
        if (run_count && runs[run_count - 1u].material == material)
            runs[run_count - 1u].index_count += 3u;
        else
            runs[run_count++] = (PortableRenderRun){i * 3u, 3u, material};
    }
    if (problem)
    {
        free(storage);
        return failure(error, capacity, binding, row, problem);
    }
    smooth_normals(vertices, source->vertex_count, indices, index_count);
    const PortableRenderResources candidate = {.mesh = {vertices, indices, materials, runs,
                                                        source->vertex_count, index_count,
                                                        source->material_count, run_count, NULL, 0u},
                                               .storage = storage,
                                               .pack = pack};
    portable_render_resources_close(resources);
    *resources = candidate;
    if (error && capacity)
        error[0] = 0;
    return 1;
}

int portable_render_model_local_transform(double out[12], const double original[12],
                                          const PortableModelBinding *binding)
{
    if (!out || !original)
        return 0;
    double result[12];
    for (unsigned i = 0; i < 12u; ++i)
        if (!isfinite(original[i]) || fabs(original[i]) > (i % 4u == 3u ? 4096.0 : 32768.0))
            return 0;
    memcpy(result, original, sizeof result);
    if (binding)
        for (unsigned row = 0; row < 3u; ++row)
        {
            for (unsigned axis = 0; axis < 3u; ++axis)
            {
                if (!binding->scale_q16[axis])
                    return 0;
                const double scaled =
                    original[row * 4u + axis] * ((double)binding->scale_q16[axis] / 65536.0);
                if (!isfinite(scaled) || fabs(scaled) > 32768.0)
                    return 0;
                result[row * 4u + axis] = scaled;
                result[row * 4u + 3u] -= scaled * ((double)binding->pivot_q16[axis] / 65536.0);
            }
            if (!isfinite(result[row * 4u + 3u]) || fabs(result[row * 4u + 3u]) > 4096.0)
                return 0;
        }
    memcpy(out, result, sizeof result);
    return 1;
}

int portable_render_model_transform(double out[12], const PortableModelBinding *binding,
                                    const PortableRenderPose *pose, double clearance,
                                    double metres_per_unit)
{
    const double identity[3] = {1, 1, 1};
    return portable_render_model_transform_scaled(out, binding, pose, clearance, metres_per_unit,
                                                  identity);
}

int portable_render_model_transform_scaled(double out[12], const PortableModelBinding *binding,
                                           const PortableRenderPose *pose, double clearance,
                                           double metres_per_unit, const double body_scale[3])
{
    if (!out || !binding || !pose || !body_scale || !isfinite(metres_per_unit) ||
        metres_per_unit <= 0.0)
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(body_scale[axis]) || body_scale[axis] <= 0 || body_scale[axis] > 32768.0)
            return 0;
    double basis[12], result[12];
    if (!portable_render_pose_actor(pose, clearance, basis))
        return 0;
    for (unsigned row = 0; row < 3u; ++row)
    {
        result[row * 4u + 3u] = basis[row * 4u + 3u];
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            if (!binding->scale_q16[axis])
                return 0;
            /* Model +X is right and -Y is forward; the box basis uses
             * +X left/+Y forward. Rotate both horizontal axes, not handedness. */
            const double direction = axis < 2u ? -1.0 : 1.0;
            const double scaled = direction * basis[row * 4u + axis] *
                                  ((double)binding->scale_q16[axis] / 65536.0) * metres_per_unit *
                                  body_scale[axis];
            if (!isfinite(scaled) || fabs(scaled) > 32768.0)
                return 0;
            result[row * 4u + axis] = scaled;
            result[row * 4u + 3u] -= scaled * ((double)binding->pivot_q16[axis] / 65536.0);
        }
        if (!isfinite(result[row * 4u + 3u]) || fabs(result[row * 4u + 3u]) > 4096.0)
            return 0;
    }
    memcpy(out, result, sizeof result);
    return 1;
}

int portable_render_model_skin(const PortableRenderMesh *rest, const PortableModelRig *rig, uint32_t clip,
                               uint32_t frame, PortableRenderVertex *out)
{
    if (!rest || !rig || !out || clip >= rig->clip_count || rest->vertex_count != rig->vertex_count)
        return 0;
    double matrices[PORTABLE_MODEL_RIG_MAX_NODES][12];
    for (uint32_t node = 0; node < rig->node_count; ++node)
    {
        int32_t pose[12];
        portable_model_rig_pose(rig, clip, frame, node, pose);
        for (unsigned i = 0; i < 12u; ++i)
            matrices[node][i] = pose[i] / 65536.0;
    }
    return portable_render_model_skin_nodes(rest, rig, (const double (*)[12])matrices, out);
}

int portable_render_model_skin_sample(const PortableRenderMesh *rest, const PortableModelRig *rig,
                                      uint32_t clip, double frame, PortableRenderVertex *out)
{
    if (!rest || !rig || !out || clip >= rig->clip_count || !isfinite(frame) || frame < 0 ||
        !rig->clips[clip].frames || rig->node_count > PORTABLE_MODEL_RIG_MAX_NODES ||
        rest->vertex_count != rig->vertex_count)
        return 0;
    const PortableModelClip *c = &rig->clips[clip];
    frame = c->flags & PORTABLE_MODEL_CLIP_LOOP ? fmod(frame, c->frames) : fmin(frame, c->frames - 1u);
    const uint32_t first = (uint32_t)frame;
    const uint32_t next = first + 1u < c->frames ? first + 1u
                         : c->flags & PORTABLE_MODEL_CLIP_LOOP ? 0u : first;
    const double alpha = frame - first;
    double matrices[PORTABLE_MODEL_RIG_MAX_NODES][12];
    for (uint32_t node = 0; node < rig->node_count; ++node)
    {
        int32_t a[12], b[12];
        portable_model_rig_pose(rig, clip, first, node, a);
        portable_model_rig_pose(rig, clip, next, node, b);
        for (unsigned i = 0; i < 12u; ++i)
            matrices[node][i] = (a[i] + alpha * ((double)b[i] - a[i])) / 65536.0;
    }
    return portable_render_model_skin_nodes(rest, rig, (const double (*)[12])matrices, out);
}

int portable_render_model_skin_nodes(const PortableRenderMesh *rest, const PortableModelRig *rig,
                                     const double (*matrices)[12], PortableRenderVertex *out)
{
    if (!rest || !rig || !matrices || !out || rest->vertex_count != rig->vertex_count)
        return 0;
    for (uint32_t i = 0; i < rest->vertex_count; ++i)
    {
        out[i] = rest->vertices[i];
        if (rig->nodes[i] == PORTABLE_MODEL_RIG_STATIC)
            continue;
        const double *m = matrices[rig->nodes[i]], *p = rest->vertices[i].position, *n = rest->vertices[i].normal;
        double normal[3], length = 0.0;
        for (unsigned row = 0; row < 3u; ++row)
        {
            out[i].position[row] = m[row * 4] * p[0] + m[row * 4 + 1] * p[1] + m[row * 4 + 2] * p[2] + m[row * 4 + 3];
            /* Rig poses rotate with uniform scale; keep the normal unit length. */
            normal[row] = m[row * 4] * n[0] + m[row * 4 + 1] * n[1] + m[row * 4 + 2] * n[2];
            length += normal[row] * normal[row];
        }
        length = sqrt(length);
        for (unsigned row = 0; row < 3u; ++row)
            out[i].normal[row] = length > 1e-12 ? normal[row] / length : 0.0;
    }
    return 1;
}

/* Poses a rest transform as the collision does: p' = pivot + offset + R (p - pivot). */
void portable_render_model_motion(const PortableMeshMotion *motion, uint32_t tick, double transform[12])
{
    PortableMeshPose pose;
    portable_mesh_motion_pose(motion, tick, &pose);
    double rest[12], r[9];
    memcpy(rest, transform, sizeof rest);
    for (unsigned i = 0; i < 9u; ++i)
        r[i] = pose.rotation_q30[i] / 1073741824.0;
    for (unsigned row = 0; row < 3u; ++row)
    {
        for (unsigned col = 0; col < 3u; ++col)
            transform[row * 4u + col] = r[row * 3u] * rest[col] + r[row * 3u + 1u] * rest[4u + col] +
                                        r[row * 3u + 2u] * rest[8u + col];
        double moved = (pose.pivot_q16[row] + pose.offset_q16[row]) / 65536.0;
        for (unsigned k = 0; k < 3u; ++k)
            moved += r[row * 3u + k] * (rest[k * 4u + 3u] - pose.pivot_q16[k] / 65536.0);
        transform[row * 4u + 3u] = moved;
    }
}
