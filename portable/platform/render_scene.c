#include "render_scene.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RENDER_VERTEX_LIMIT 131072u
#define RENDER_INDEX_LIMIT (262144u * 3u)
#define RENDER_MATERIAL_LIMIT 256u
#define RENDER_RUN_LIMIT 262144u

static int failure(char *error, size_t capacity, const char *reason)
{
    if (error && capacity)
        snprintf(error, capacity, "3D render scene: %s", reason);
    return 0;
}

int portable_render_world_extent(size_t output_width, size_t output_height, unsigned internal_scale,
                                 unsigned viewport_rows, size_t *width, size_t *height, char *error,
                                 size_t capacity)
{
    if (!output_width || !output_height || output_width > INT32_MAX || output_height > INT32_MAX ||
        !internal_scale || internal_scale > 8u || !viewport_rows || viewport_rows > 4u || !width ||
        !height || width == height)
        return failure(error, capacity, "invalid world resolution request");
    uint64_t h = (uint64_t)160u * internal_scale * viewport_rows;
    if (h < output_height)
        h = output_height;
    for (;;)
    {
        uint64_t w = ((uint64_t)output_width * h + output_height / 2u) / output_height;
        if (!w)
            w = 1u;
        if (w <= PORTABLE_RENDER_TARGET_EXTENT_LIMIT && h <= PORTABLE_RENDER_TARGET_EXTENT_LIMIT &&
            w * h <= PORTABLE_RENDER_TARGET_PIXEL_LIMIT)
        {
            *width = (size_t)w;
            *height = (size_t)h;
            if (error && capacity)
                error[0] = 0;
            return 1;
        }
        if (h == 1u)
            return failure(error, capacity, "output aspect exceeds GPU target budget");
        h = h * 3u / 4u;
        if (!h)
            h = 1u;
    }
}

static int resource_failure(char *error, size_t capacity, uint32_t course, uint32_t row,
                            const char *reason)
{
    if (error && capacity)
        snprintf(error, capacity, "3D render course=%u row=%u: %s", (unsigned)course, (unsigned)row,
                 reason);
    return 0;
}

static uint32_t argb(uint32_t rgba)
{
    return (rgba >> 8u) | (rgba << 24u);
}

void portable_render_resources_close(PortableRenderResources *resources)
{
    if (!resources)
        return;
    free(resources->storage);
    memset(resources, 0, sizeof(*resources));
}

static int append_storage(size_t *total, size_t count, size_t stride, size_t *offset)
{
    if (*total > SIZE_MAX - 7u)
        return 0;
    const size_t aligned = (*total + 7u) & ~(size_t)7u;
    if (count > (SIZE_MAX - aligned) / stride)
        return 0;
    *offset = aligned;
    *total = aligned + count * stride;
    return 1;
}

static int instance_flags_valid(const PortableRenderInstance *instance)
{
    if (instance->clip_y_extent
            ? (instance->clip_y_extent > 65536u || instance->clip_y_begin >= instance->clip_y_end ||
               instance->clip_y_end > instance->clip_y_extent)
            : (instance->clip_y_begin || instance->clip_y_end))
        return 0;
    for (unsigned axis = 0; axis < 2u; ++axis)
        if (!isfinite(instance->uv_offset[axis]) || fabs(instance->uv_offset[axis]) > 32768.0)
            return 0;
    const uint32_t billboard =
        PORTABLE_RENDER_INSTANCE_BILLBOARD_Z | PORTABLE_RENDER_INSTANCE_BILLBOARD_UP;
    if ((instance->flags & ~billboard) || (instance->flags & billboard) == billboard ||
        (instance->hidden_views & ~UINT32_C(15)))
        return 0;
    if (instance->flags & billboard)
        for (unsigned axis = 0; axis < 3u; ++axis)
            if (!isfinite(instance->billboard_scale[axis]) ||
                instance->billboard_scale[axis] == 0.0 ||
                fabs(instance->billboard_scale[axis]) > 32768.0)
                return 0;
    if (instance->flags & PORTABLE_RENDER_INSTANCE_BILLBOARD_UP)
    {
        double square = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const double value = instance->transform[axis * 4u + 2u];
            if (!isfinite(value) || fabs(value) > 32768.0)
                return 0;
            square += value * value;
        }
        if (square < 1e-24)
            return 0;
    }
    return 1;
}

int portable_render_instance_clip_rows(const PortableRenderInstance *instance,
                                       const PortableRenderView *view, size_t *first, size_t *count)
{
    if (!instance || !view || !first || !count || !view->height || view->height > INT32_MAX ||
        !instance_flags_valid(instance))
        return 0;
    size_t begin = 0, end = view->height;
    if (instance->clip_y_extent)
    {
        const uint32_t denominator = instance->clip_y_extent;
        begin = (size_t)(((uint64_t)instance->clip_y_begin * view->height + denominator - 1u) /
                         denominator);
        end = (size_t)(((uint64_t)instance->clip_y_end * view->height + denominator - 1u) /
                       denominator);
    }
    *first = begin;
    *count = end - begin;
    return 1;
}

int portable_render_instance_visible(const PortableRenderInstance *source, size_t view_index)
{
    return source && view_index < PORTABLE_RENDER_VIEW_LIMIT &&
           !(source->hidden_views & ~UINT32_C(15)) &&
           !(source->hidden_views & (UINT32_C(1) << view_index));
}

int portable_render_instance_for_view(const PortableRenderInstance *source,
                                      const PortableRenderView *view, PortableRenderInstance *out)
{
    if (!source || !view || !out || !instance_flags_valid(source))
        return 0;
    PortableRenderInstance candidate;
    memcpy(&candidate, source, sizeof candidate);
    if (source->flags & PORTABLE_RENDER_INSTANCE_BILLBOARD_Z)
    {
        double x = view->right[0], y = view->right[1];
        if (!isfinite(x) || !isfinite(y))
            return 0;
        const double length = hypot(x, y);
        if (!isfinite(length))
            return 0;
        if (length > 1e-12)
        {
            x /= length;
            y /= length;
        }
        else
        {
            x = 1.0;
            y = 0.0;
        }
        const double basis[9] = {x, -y, 0, y, x, 0, 0, 0, 1};
        for (unsigned row = 0; row < 3u; ++row)
        {
            if (!isfinite(source->transform[row * 4u + 3u]) ||
                fabs(source->transform[row * 4u + 3u]) > 4096.0)
                return 0;
            for (unsigned column = 0; column < 3u; ++column)
                candidate.transform[row * 4u + column] =
                    basis[row * 3u + column] * source->billboard_scale[column];
        }
    }
    if (source->flags & PORTABLE_RENDER_INSTANCE_BILLBOARD_UP)
    {
        double up[3], right[3], forward[3];
        double length = 0, dot = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            up[axis] = source->transform[axis * 4u + 2u];
            length += up[axis] * up[axis];
            if (!isfinite(view->right[axis]) || fabs(view->right[axis]) > 32768.0 ||
                !isfinite(source->transform[axis * 4u + 3u]) ||
                fabs(source->transform[axis * 4u + 3u]) > 4096.0)
                return 0;
        }
        length = sqrt(length);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            up[axis] /= length;
            dot += up[axis] * view->right[axis];
        }
        length = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            right[axis] = view->right[axis] - dot * up[axis];
            length += right[axis] * right[axis];
        }
        if (length < 1e-24)
        {
            unsigned least = 0;
            for (unsigned axis = 1; axis < 3; ++axis)
                if (fabs(up[axis]) < fabs(up[least]))
                    least = axis;
            length = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                right[axis] = (axis == least ? 1.0 : 0.0) - up[least] * up[axis];
                length += right[axis] * right[axis];
            }
        }
        length = sqrt(length);
        for (unsigned axis = 0; axis < 3; ++axis)
            right[axis] /= length;
        for (unsigned axis = 0; axis < 3; ++axis)
            forward[axis] = up[(axis + 1) % 3] * right[(axis + 2) % 3] -
                            up[(axis + 2) % 3] * right[(axis + 1) % 3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            candidate.transform[axis * 4u] = right[axis] * source->billboard_scale[0];
            candidate.transform[axis * 4u + 1u] = forward[axis] * source->billboard_scale[1];
            candidate.transform[axis * 4u + 2u] = up[axis] * source->billboard_scale[2];
        }
        /* A signed scale must not become the support normal on a second resolve.
         * This copy is now ordinary affine geometry for this camera only. */
        candidate.flags = 0;
    }
    memcpy(out, &candidate, sizeof candidate);
    return 1;
}

static int instance_transform_valid(const PortableRenderInstance *instance)
{
    for (unsigned component = 0; component < 12u; ++component)
        if (!isfinite(instance->transform[component]) ||
            fabs(instance->transform[component]) > (component % 4u == 3u ? 4096.0 : 32768.0))
            return 0;
    return 1;
}

int portable_render_view_art_side(const PortableRenderView *view, uint16_t heading, unsigned *out)
{
    if (!view || !out || !isfinite(view->right[0]) || !isfinite(view->right[1]))
        return 0;
    double x, y;
    if (!(heading & 0x3fff))
    {
        static const double xs[4] = {0, 1, 0, -1}, ys[4] = {-1, 0, 1, 0};
        x = xs[heading >> 14];
        y = ys[heading >> 14];
    }
    else
    {
        const double angle = heading * 6.28318530717958647692 / 65536.0;
        x = sin(angle);
        y = -cos(angle);
    }
    const double side = view->right[0] * x + view->right[1] * y;
    const double edge = view->right[1] * x - view->right[0] * y;
    if (!isfinite(side) || !isfinite(edge))
        return 0;
    *out = side < 0 || (side == 0 && edge < 0);
    return 1;
}

int portable_render_instance_face_for_view(const PortableRenderInstance *source,
                                           const PortableRenderView *view, int32_t facing_x,
                                           int32_t facing_y, uint8_t turn, uint8_t half_turn,
                                           double local_min_plus_max_x, PortableRenderInstance *out)
{
    if (!out || !view || turn > 1u || half_turn > 1u || (!facing_x && !facing_y) ||
        !isfinite(local_min_plus_max_x) || !isfinite(view->right[0]) || !isfinite(view->right[1]))
        return 0;
    PortableRenderInstance candidate;
    if (!portable_render_instance_for_view(source, view, &candidate) ||
        !instance_transform_valid(&candidate))
        return 0;
    /* The billboard is now a camera-specific ordinary affine transform. */
    candidate.flags = 0;
    double side = view->right[0] * facing_x + view->right[1] * facing_y;
    /* A source unsigned half-turn includes exactly one perpendicular edge. */
    if (half_turn && side == 0)
        side = view->right[1] * facing_x - view->right[0] * facing_y;
    if (!isfinite(side))
        return 0;
    if ((side >= 0) != (turn != 0))
        for (unsigned row = 0; row < 3u; ++row)
        {
            const unsigned column = row * 4u;
            candidate.transform[column + 3u] += candidate.transform[column] * local_min_plus_max_x;
            candidate.transform[column] = -candidate.transform[column];
        }
    if (!instance_transform_valid(&candidate))
        return 0;
    memcpy(out, &candidate, sizeof candidate);
    return 1;
}

int portable_render_instance_flip_vertical(PortableRenderInstance *instance,
                                           double local_min_plus_max_z)
{
    if (!instance || !isfinite(local_min_plus_max_z) || !instance_flags_valid(instance) ||
        !instance_transform_valid(instance))
        return 0;
    PortableRenderInstance candidate;
    memcpy(&candidate, instance, sizeof candidate);
    if (instance->flags & PORTABLE_RENDER_INSTANCE_BILLBOARD_Z)
    {
        candidate.transform[11] += instance->billboard_scale[2] * local_min_plus_max_z;
        candidate.billboard_scale[2] = -instance->billboard_scale[2];
    }
    else if (instance->flags & PORTABLE_RENDER_INSTANCE_BILLBOARD_UP)
    {
        double length = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            length += instance->transform[axis * 4u + 2u] * instance->transform[axis * 4u + 2u];
        length = sqrt(length);
        for (unsigned axis = 0; axis < 3; ++axis)
            candidate.transform[axis * 4u + 3u] += instance->transform[axis * 4u + 2u] / length *
                                                   instance->billboard_scale[2] *
                                                   local_min_plus_max_z;
        candidate.billboard_scale[2] = -instance->billboard_scale[2];
    }
    else
        for (unsigned row = 0; row < 3u; ++row)
        {
            const unsigned column = row * 4u + 2u;
            candidate.transform[row * 4u + 3u] +=
                instance->transform[column] * local_min_plus_max_z;
            candidate.transform[column] = -instance->transform[column];
        }
    if (!instance_transform_valid(&candidate))
        return 0;
    memcpy(instance, &candidate, sizeof candidate);
    return 1;
}

static int mesh_valid(const PortableRenderMesh *mesh, char *error, size_t capacity);

int portable_render_resources_open_course(PortableRenderResources *resources,
                                          const PortableCourse3DView *course, char *error,
                                          size_t capacity)
{
    return portable_render_resources_open_course_present(resources, course,
                                                         PORTABLE_COURSE_3D_PRESENT_ALL, error, capacity);
}

int portable_render_resources_open_course_present(PortableRenderResources *resources,
                                                  const PortableCourse3DView *course, uint32_t presence,
                                                  char *error, size_t capacity)
{
    if (!resources || !course)
        return failure(error, capacity, "missing course/resources");
    const PortableCourse3DTable *vertices =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('V', 'E', 'R', 'T'));
    const PortableCourse3DTable *triangles =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('T', 'R', 'I', 'S'));
    const PortableCourse3DTable *materials =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('M', 'A', 'T', 'S'));
    if (!vertices || !triangles || !materials || !vertices->bytes || !triangles->bytes ||
        !materials->bytes || !vertices->count || vertices->count > RENDER_VERTEX_LIMIT ||
        !triangles->count || triangles->count > RENDER_RUN_LIMIT || !materials->count ||
        materials->count > RENDER_MATERIAL_LIMIT || vertices->stride != 32u ||
        triangles->stride != 16u || materials->stride != 48u)
        return resource_failure(error, capacity, course->id, 0, "invalid mesh table/count/stride");
    const PortableCourse3DTable *colors =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('V', 'C', 'O', 'L'));
    const PortableCourse3DTable *animations =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('T', 'X', 'A', 'N'));
    const PortableCourse3DTable *matrices =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('T', 'X', 'S', 'R'));
    const PortableCourse3DTable *keys =
        portable_course_3d_table(course, PORTABLE_COURSE_3D_TAG('T', 'X', 'P', 'T'));
    const uint32_t color_count = colors ? colors->count : 0u;
    const uint32_t animation_count = animations && animations->count ? materials->count : 0u;
    const uint32_t matrix_count = matrices ? matrices->count : 0u, key_count = keys ? keys->count : 0u;
    size_t total = 0, vertex_offset, material_offset, run_offset, index_offset, color_offset,
           animation_offset, matrix_offset, start_offset, texture_offset;
    const uint32_t index_count = triangles->count * 3u;
    if (!append_storage(&total, vertices->count, sizeof(PortableRenderVertex), &vertex_offset) ||
        !append_storage(&total, materials->count, sizeof(PortableRenderMaterial),
                        &material_offset) ||
        !append_storage(&total, triangles->count, sizeof(PortableRenderRun), &run_offset) ||
        !append_storage(&total, index_count, sizeof(uint32_t), &index_offset) ||
        !append_storage(&total, color_count, sizeof(uint32_t), &color_offset) ||
        !append_storage(&total, animation_count, sizeof(PortableRenderTextureAnimation),
                        &animation_offset) ||
        !append_storage(&total, (size_t)matrix_count * 6u, sizeof(double), &matrix_offset) ||
        !append_storage(&total, key_count, sizeof(uint32_t), &start_offset) ||
        !append_storage(&total, key_count, sizeof(PortableNativeImage), &texture_offset))
        return resource_failure(error, capacity, course->id, 0, "mesh allocation overflow");
    uint8_t *storage = calloc(1u, total);
    if (!storage)
        return resource_failure(error, capacity, course->id, 0, "mesh allocation failed");
    uint32_t *vertex_argb = color_count ? (uint32_t *)(void *)(storage + color_offset) : NULL;
    PortableRenderTextureAnimation *decoded_animations =
        (PortableRenderTextureAnimation *)(void *)(storage + animation_offset);
    double *decoded_matrices = (double *)(void *)(storage + matrix_offset);
    uint32_t *key_starts = (uint32_t *)(void *)(storage + start_offset);
    PortableNativeImage *key_textures = (PortableNativeImage *)(void *)(storage + texture_offset);
    PortableRenderVertex *decoded_vertices =
        (PortableRenderVertex *)(void *)(storage + vertex_offset);
    PortableRenderMaterial *decoded_materials =
        (PortableRenderMaterial *)(void *)(storage + material_offset);
    PortableRenderRun *runs = (PortableRenderRun *)(void *)(storage + run_offset);
    uint32_t *indices = (uint32_t *)(void *)(storage + index_offset);
    uint32_t run_count = 0;
    const char *problem = NULL;
    uint32_t problem_row = 0;
    PortableCourse3DMeta meta;
    const uint32_t meta_ambient = portable_course_3d_meta(course, &meta) ? meta.ambient_rgba : 0x404040FFu;
    for (uint32_t i = 0; i < materials->count; ++i)
    {
        PortableCourse3DMaterial source;
        if (!portable_course_3d_material(course, i, &source))
        {
            problem = "material decode failed";
            problem_row = i;
            break;
        }
        if ((source.flags & ~63u) || source.alpha_cutoff_q16 < 0 || source.alpha_cutoff_q16 > 65536)
        {
            problem = "invalid material flags/cutoff";
            problem_row = i;
            break;
        }
        PortableCourse3DProperty sky;
        const uint32_t background =
            portable_course_3d_property_find(course, source.property_first, source.property_count,
                                             "mksc:sky", &sky) &&
                    sky.type == PORTABLE_COURSE_3D_PROPERTY_BOOLEAN && sky.value.boolean
                ? PORTABLE_RENDER_MATERIAL_BACKGROUND
                : 0u;
        /* Opt-in environment properties: mksc:lit and mksc:fog booleans,
         * mksc:ambient RGBA integer (lit; default META ambient) and integer
         * mksc:texture-filter (0 nearest, 1 bilinear, 2 three-point). */
        PortableCourse3DProperty lit, fog, ambient, filter, world;
        const uint32_t world_sky =
            background && portable_course_3d_property_find(course, source.property_first, source.property_count,
                                                           "mksc:sky-world", &world) &&
                    world.type == PORTABLE_COURSE_3D_PROPERTY_BOOLEAN && world.value.boolean
                ? PORTABLE_RENDER_MATERIAL_WORLD_SKY
                : 0u;
        const int is_lit = portable_course_3d_property_find(course, source.property_first, source.property_count,
                                                            "mksc:lit", &lit) &&
                           lit.type == PORTABLE_COURSE_3D_PROPERTY_BOOLEAN && lit.value.boolean;
        const int is_fog = portable_course_3d_property_find(course, source.property_first, source.property_count,
                                                            "mksc:fog", &fog) &&
                           fog.type == PORTABLE_COURSE_3D_PROPERTY_BOOLEAN && fog.value.boolean;
        uint32_t ambient_rgba = meta_ambient;
        if (portable_course_3d_property_find(course, source.property_first, source.property_count, "mksc:ambient",
                                             &ambient) &&
            ambient.type == PORTABLE_COURSE_3D_PROPERTY_INTEGER)
            ambient_rgba = (uint32_t)ambient.value.integer;
        uint32_t texture_filter = PORTABLE_RENDER_FILTER_NEAREST;
        if (portable_course_3d_property_find(course, source.property_first, source.property_count,
                                             "mksc:texture-filter", &filter))
        {
            if (filter.type != PORTABLE_COURSE_3D_PROPERTY_INTEGER || filter.value.integer < 0 ||
                filter.value.integer > (int32_t)PORTABLE_RENDER_FILTER_THREE_POINT)
            {
                problem = "invalid mksc:texture-filter";
                problem_row = i;
                break;
            }
            texture_filter = (uint32_t)filter.value.integer;
        }
        decoded_materials[i] =
            (PortableRenderMaterial){.argb = argb(source.base_rgba),
                                     .flags = source.flags | background | world_sky |
                                              (is_lit ? PORTABLE_RENDER_MATERIAL_LIT : 0u) |
                                              (is_fog && !background ? PORTABLE_RENDER_MATERIAL_FOG : 0u),
                                     .alpha_cutoff_q16 = source.alpha_cutoff_q16,
                                     .ambient_argb = argb(ambient_rgba),
                                     .emission_argb = argb(source.emissive_rgba),
                                     .filter = texture_filter};
        if (source.textures[0] != PORTABLE_COURSE_3D_ABSENT &&
            !portable_course_3d_texture(course, source.textures[0], &decoded_materials[i].texture))
        {
            problem = "base texture unavailable";
            problem_row = i;
            break;
        }
    }
    for (uint32_t i = 0; !problem && i < matrix_count; ++i)
    {
        int32_t matrix[6];
        if (!portable_course_3d_texture_srt(course, i, matrix))
        {
            problem = "texture matrix decode failed";
            problem_row = i;
        }
        for (unsigned j = 0; !problem && j < 6u; ++j)
            decoded_matrices[i * 6u + j] = (double)matrix[j] / 65536.0;
    }
    for (uint32_t i = 0; !problem && i < key_count; ++i)
    {
        uint32_t texture;
        if (!portable_course_3d_texture_key(course, i, &key_starts[i], &texture) ||
            !portable_course_3d_texture(course, texture, &key_textures[i]))
        {
            problem = "texture pattern key unavailable";
            problem_row = i;
        }
    }
    for (uint32_t i = 0; !problem && animations && i < animations->count; ++i)
    {
        PortableCourse3DTextureAnimation source;
        if (!portable_course_3d_texture_animation(course, i, &source) ||
            source.material >= materials->count)
        {
            problem = "texture animation decode failed";
            problem_row = i;
            break;
        }
        PortableRenderTextureAnimation *animation = &decoded_animations[source.material];
        if (source.kind == 0u)
        {
            animation->matrix_frames = source.frame_count;
            animation->matrix_count = source.count;
            animation->matrices = decoded_matrices + (size_t)source.first * 6u;
        }
        else
        {
            animation->key_frames = source.frame_count;
            animation->key_count = source.count;
            animation->key_starts = key_starts + source.first;
            animation->key_textures = key_textures + source.first;
        }
        decoded_materials[source.material].animation = animation;
    }
    for (uint32_t i = 0; !problem && i < color_count; ++i)
        vertex_argb[i] = argb(portable_course_3d_vertex_rgba(course, i));
    for (uint32_t i = 0; !problem && i < vertices->count; ++i)
    {
        PortableCourse3DVertex source;
        if (!portable_course_3d_vertex(course, i, &source))
        {
            problem = "vertex decode failed";
            problem_row = i;
            break;
        }
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            decoded_vertices[i].position[axis] = (double)source.position_q16[axis] / 65536.0;
            decoded_vertices[i].normal[axis] = (double)source.normal_q30[axis] / 1073741824.0;
        }
        for (unsigned axis = 0; axis < 2u; ++axis)
            decoded_vertices[i].uv[axis] = (double)source.uv_q16[axis] / 65536.0;
    }
    /* Faces of materials absent from this race (mksc:present-in) are left out. */
    uint8_t present[RENDER_MATERIAL_LIMIT];
    for (uint32_t i = 0; i < materials->count; ++i)
    {
        PortableCourse3DMaterial source;
        present[i] = (uint8_t)(!portable_course_3d_material(course, i, &source) ||
                               portable_course_3d_present(course, source.property_first, source.property_count,
                                                          presence));
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; !problem && i < triangles->count; ++i)
    {
        PortableCourse3DTriangle source;
        if (!portable_course_3d_triangle(course, i, &source) || source.material >= materials->count)
        {
            problem = "triangle/material decode failed";
            problem_row = i;
            break;
        }
        for (unsigned corner = 0; corner < 3u; ++corner)
            if (source.vertices[corner] >= vertices->count)
            {
                problem = "triangle vertex outside mesh";
                problem_row = i;
                break;
            }
        if (problem)
            break;
        if (!present[source.material])
            continue;
        for (unsigned corner = 0; corner < 3u; ++corner)
            indices[kept * 3u + corner] = source.vertices[corner];
        if (run_count && runs[run_count - 1u].material == source.material)
            runs[run_count - 1u].index_count += 3u;
        else
            runs[run_count++] = (PortableRenderRun){kept * 3u, 3u, source.material};
        ++kept;
    }
    if (problem)
    {
        free(storage);
        return resource_failure(error, capacity, course->id, problem_row, problem);
    }
    PortableRenderResources candidate = {.mesh = {decoded_vertices, indices, decoded_materials,
                                                  runs, vertices->count, kept * 3u,
                                                  materials->count, run_count, vertex_argb, 0u},
                                         .storage = storage,
                                         .pack = course->pack,
                                         .course_id = course->id};
    char validation[160];
    if (!mesh_valid(&candidate.mesh, validation, sizeof(validation)))
    {
        free(storage);
        return resource_failure(error, capacity, course->id, 0, validation);
    }
    portable_render_resources_close(resources);
    *resources = candidate;
    if (error && capacity)
        error[0] = 0;
    return 1;
}

void portable_render_instance_identity(PortableRenderInstance *instance,
                                       const PortableRenderMesh *mesh)
{
    if (!instance)
        return;
    memset(instance, 0, sizeof(*instance));
    instance->mesh = mesh;
    instance->transform[0] = instance->transform[5] = instance->transform[10] = 1.0;
    instance->tint_argb = UINT32_C(0xffffffff);
}

static int image_valid(const PortableNativeImage *texture)
{
    return !texture->pixels || (texture->width && texture->height && texture->width <= 4096u &&
                                texture->height <= 4096u &&
                                (size_t)texture->width * texture->height <= 4000000u);
}

static int animation_valid(const PortableRenderTextureAnimation *animation)
{
    if (!animation)
        return 1;
    if ((animation->matrix_count &&
         (!animation->matrices || !animation->matrix_frames ||
          (animation->matrix_count != 1u && animation->matrix_count != animation->matrix_frames))) ||
        (animation->key_count && (!animation->key_starts || !animation->key_textures ||
                                  !animation->key_frames || animation->key_starts[0])))
        return 0;
    for (uint32_t i = 0; i < animation->matrix_count * 6u; ++i)
        if (!isfinite(animation->matrices[i]) || fabs(animation->matrices[i]) > 256.0)
            return 0;
    for (uint32_t i = 0; i < animation->key_count; ++i)
        if ((i && animation->key_starts[i] <= animation->key_starts[i - 1u]) ||
            animation->key_starts[i] >= animation->key_frames || !animation->key_textures[i].pixels ||
            !image_valid(&animation->key_textures[i]))
            return 0;
    return 1;
}

void portable_render_material_at(const PortableRenderMaterial *material, uint32_t tick, double uv[6],
                                 const PortableNativeImage **texture)
{
    static const double identity[6] = {1, 0, 0, 1, 0, 0};
    const PortableRenderTextureAnimation *animation = material->animation;
    const double *matrix = identity;
    *texture = &material->texture;
    if (animation && animation->matrix_count)
        matrix = animation->matrices +
                 (size_t)(animation->matrix_count == 1u ? 0u : tick % animation->matrix_frames) * 6u;
    if (animation && animation->key_count)
    {
        const uint32_t frame = tick % animation->key_frames;
        uint32_t key = 0;
        while (key + 1u < animation->key_count && animation->key_starts[key + 1u] <= frame)
            ++key;
        *texture = &animation->key_textures[key];
    }
    memcpy(uv, matrix, sizeof identity);
}

void portable_render_material_sample(const PortableRenderMaterial *material, uint32_t tick,
    uint32_t rewind_q16, double uv[6], const PortableNativeImage **texture)
{
    portable_render_material_at(material, tick, uv, texture);
    if (!tick || !rewind_q16 || !material->animation || material->animation->matrix_count <= 1u) return;
    double previous[6];
    const PortableNativeImage *ignored;
    portable_render_material_at(material, tick - 1u, previous, &ignored);
    const double rewind = (rewind_q16 > 65536u ? 65536u : rewind_q16) / 65536.0;
    for (unsigned i = 0; i < 6; ++i)
    {
        double delta = previous[i] - uv[i];
        /* Wrapped scrolling may cross the UV seam in either direction. */
        if ((i == 4 && (material->flags & 1u)) || (i == 5 && (material->flags & 2u)))
            delta = remainder(delta, 1.0);
        uv[i] += rewind * delta;
    }
}

static int mesh_valid_geometry(const PortableRenderMesh *mesh, int geometry_known, char *error,
                               size_t capacity)
{
    if (!mesh || !mesh->vertices || !mesh->indices || !mesh->materials || !mesh->runs ||
        !mesh->vertex_count || mesh->vertex_count > RENDER_VERTEX_LIMIT || !mesh->index_count ||
        mesh->index_count > RENDER_INDEX_LIMIT || mesh->index_count % 3u || !mesh->material_count ||
        mesh->material_count > RENDER_MATERIAL_LIMIT || !mesh->run_count ||
        mesh->run_count > RENDER_RUN_LIMIT)
        return failure(error, capacity, "invalid mesh arrays/counts");
    if (!geometry_known)
    {
        for (uint32_t i = 0; i < mesh->vertex_count; ++i)
        {
            const PortableRenderVertex *vertex = &mesh->vertices[i];
            for (unsigned axis = 0; axis < 3u; ++axis)
                if (!isfinite(vertex->position[axis]) || fabs(vertex->position[axis]) > 32768.0 ||
                    !isfinite(vertex->normal[axis]) || fabs(vertex->normal[axis]) > 1.001)
                    return failure(error, capacity, "invalid vertex position/normal");
            for (unsigned axis = 0; axis < 2u; ++axis)
                if (!isfinite(vertex->uv[axis]) || fabs(vertex->uv[axis]) > 32768.0)
                    return failure(error, capacity, "invalid vertex UV");
        }
        for (uint32_t i = 0; i < mesh->index_count; ++i)
            if (mesh->indices[i] >= mesh->vertex_count)
                return failure(error, capacity, "index outside mesh");
    }
    for (uint32_t i = 0; i < mesh->material_count; ++i)
    {
        const PortableRenderMaterial *material = &mesh->materials[i];
        const uint32_t special = material->flags & (PORTABLE_RENDER_MATERIAL_BLENDED |
                                                    PORTABLE_RENDER_MATERIAL_ALPHA_BLEND);
        if ((material->flags & ~(63u | PORTABLE_RENDER_MATERIAL_BLENDED |
                                 PORTABLE_RENDER_MATERIAL_BACKGROUND | PORTABLE_RENDER_MATERIAL_LIT |
                                 PORTABLE_RENDER_MATERIAL_FOG | PORTABLE_RENDER_MATERIAL_WORLD_SKY)) ||
            ((material->flags & PORTABLE_RENDER_MATERIAL_WORLD_SKY) &&
             !(material->flags & PORTABLE_RENDER_MATERIAL_BACKGROUND)) ||
            material->filter > PORTABLE_RENDER_FILTER_THREE_POINT ||
            (special & (special - 1u)) || (material->flags & 48u) == 48u ||
            ((material->flags & PORTABLE_RENDER_MATERIAL_BACKGROUND) &&
             (material->flags & PORTABLE_RENDER_MATERIAL_BLENDED)) ||
            material->alpha_cutoff_q16 < 0 || material->alpha_cutoff_q16 > 65536)
            return failure(error, capacity, "invalid material flags/cutoff");
        if (!animation_valid(material->animation))
            return failure(error, capacity, "invalid material texture animation");
        if (!image_valid(&material->texture))
            return failure(error, capacity, "invalid material texture dimensions");
    }
    uint32_t first = 0;
    for (uint32_t i = 0; i < mesh->run_count; ++i)
    {
        const PortableRenderRun *run = &mesh->runs[i];
        if (run->first_index != first || !run->index_count || run->index_count % 3u ||
            run->index_count > mesh->index_count - first || run->material >= mesh->material_count)
            return failure(error, capacity, "invalid ordered material run");
        first += run->index_count;
    }
    return first == mesh->index_count
               ? 1
               : failure(error, capacity, "material runs do not cover indices");
}

static int mesh_valid(const PortableRenderMesh *mesh, char *error, size_t capacity)
{
    return mesh_valid_geometry(mesh, 0, error, capacity);
}

static int basis_valid(const double vector[3])
{
    double length = 0.0;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        if (!isfinite(vector[axis]) || fabs(vector[axis]) > 1.001)
            return 0;
        length += vector[axis] * vector[axis];
    }
    return fabs(length - 1.0) <= 1.0 / 1024.0;
}

int portable_render_frame_valid_geometry(const PortableRenderFrame *frame, size_t width,
                                         size_t height, PortableRenderGeometryKnown known,
                                         const void *context, char *error, size_t capacity)
{
    if (!frame || !width || !height || width > INT32_MAX || height > INT32_MAX ||
        !frame->view_count || frame->view_count > PORTABLE_RENDER_VIEW_LIMIT ||
        frame->instance_count > PORTABLE_RENDER_INSTANCE_LIMIT ||
        (frame->instance_count && !frame->instances))
        return failure(error, capacity, "invalid frame/target/counts");
    for (size_t i = 0; i < frame->instance_count; ++i)
    {
        const PortableRenderInstance *instance = &frame->instances[i];
        if (!instance_flags_valid(instance))
            return failure(error, capacity, "invalid instance flags/hidden views/billboard scales");
        const int geometry = known && instance->mesh ? known(context, instance->mesh) : 0;
        if (geometry < 0)
            return failure(error, capacity, "cached mesh geometry changed; forget meshes first");
        if (!mesh_valid_geometry(instance->mesh, geometry > 0, error, capacity))
            return 0;
        if (!instance_transform_valid(instance))
            return failure(error, capacity, "invalid instance transform");
    }
    for (size_t i = 0; i < frame->view_count; ++i)
    {
        const PortableRenderView *view = &frame->views[i];
        const PortableScene3DProjection *p = &view->projection;
        if (!view->width || !view->height || view->x > width || view->width > width - view->x ||
            view->y > height || view->height > height - view->y || !basis_valid(view->right) ||
            !basis_valid(view->up) || !basis_valid(view->forward) || !isfinite(p->focal_x) ||
            !isfinite(p->focal_y) || p->focal_x <= 0.0 || p->focal_y <= 0.0 ||
            p->focal_x > INT32_MAX || p->focal_y > INT32_MAX || !isfinite(p->center_x) ||
            !isfinite(p->center_y) || fabs(p->center_x) > INT32_MAX ||
            fabs(p->center_y) > INT32_MAX || !isfinite(p->near_z) || !isfinite(p->far_z) ||
            p->near_z < 1.0 / 65536.0 || p->far_z <= p->near_z || p->far_z > 65535.0)
            return failure(error, capacity, "invalid viewport/camera/projection");
        double ru = 0.0, rf = 0.0, uf = 0.0;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            if (!isfinite(view->position[axis]) || fabs(view->position[axis]) > 4096.0)
                return failure(error, capacity, "invalid camera position");
            ru += view->right[axis] * view->up[axis];
            rf += view->right[axis] * view->forward[axis];
            uf += view->up[axis] * view->forward[axis];
        }
        if (fabs(ru) > 1.0 / 1024.0 || fabs(rf) > 1.0 / 1024.0 || fabs(uf) > 1.0 / 1024.0)
            return failure(error, capacity, "camera basis is not orthogonal");
        for (size_t previous = 0; previous < i; ++previous)
        {
            const PortableRenderView *other = &frame->views[previous];
            if (view->x < other->x + other->width && other->x < view->x + view->width &&
                view->y < other->y + other->height && other->y < view->y + view->height)
                return failure(error, capacity, "viewports overlap");
        }
    }
    if (error && capacity)
        error[0] = 0;
    return 1;
}

int portable_render_frame_valid(const PortableRenderFrame *frame, size_t width, size_t height,
                                char *error, size_t capacity)
{
    return portable_render_frame_valid_geometry(frame, width, height, NULL, NULL, error, capacity);
}

static PortableScene3DVertex transform_vertex(const PortableRenderVertex *vertex,
                                              const PortableRenderInstance *instance,
                                              const PortableRenderView *view)
{
    double delta[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        const double *row = instance->transform + axis * 4u;
        delta[axis] = row[0] * vertex->position[0] + row[1] * vertex->position[1] +
                      row[2] * vertex->position[2] + row[3] - view->position[axis];
    }
    double x, y, z;
    if (view->right[2] == 0.0 && view->up[0] == -view->right[1] * view->forward[2] &&
        view->up[1] == view->right[0] * view->forward[2] &&
        view->forward[0] == view->right[1] * view->up[2] &&
        view->forward[1] == -view->right[0] * view->up[2])
    {
        /* Preserve the prior host's factored yaw/pitch evaluation order. */
        const double forward = delta[0] * view->right[1] - delta[1] * view->right[0];
        x = delta[0] * view->right[0] + delta[1] * view->right[1];
        y = delta[2] * view->up[2] - forward * view->forward[2];
        z = forward * view->up[2] + delta[2] * view->forward[2];
    }
    else
    {
        x = delta[0] * view->right[0] + delta[1] * view->right[1] + delta[2] * view->right[2];
        y = delta[0] * view->up[0] + delta[1] * view->up[1] + delta[2] * view->up[2];
        z = delta[0] * view->forward[0] + delta[1] * view->forward[1] + delta[2] * view->forward[2];
    }
    return (PortableScene3DVertex){x, y, z, vertex->uv[0], vertex->uv[1]};
}

static uint32_t multiply_argb(uint32_t a, uint32_t b)
{
    uint32_t color = 0;
    for (unsigned shift = 0; shift < 32u; shift += 8u)
        color |= (((a >> shift) & 255u) * ((b >> shift) & 255u) / 255u) << shift;
    return color;
}

typedef struct
{
    const PortableRenderMaterial *material;
    uint32_t tint, add;
} DrawMaterial;
static uint32_t sample_material(const void *owner, double u, double v)
{
    const DrawMaterial *draw = owner;
    const PortableRenderMaterial *material = draw->material;
    uint32_t color = material->argb;
    if (material->texture.pixels)
    {
        if (material->flags & 1u)
            u -= floor(u);
        if (material->flags & 2u)
            v -= floor(v);
        if (u < 0.0)
            u = 0.0;
        if (v < 0.0)
            v = 0.0;
        if (u > 1.0)
            u = 1.0;
        if (v > 1.0)
            v = 1.0;
        size_t x = (size_t)(u * material->texture.width),
               y = (size_t)(v * material->texture.height);
        if (x >= material->texture.width)
            x = material->texture.width - 1u;
        if (y >= material->texture.height)
            y = material->texture.height - 1u;
        color = multiply_argb(portable_native_image_pixel(&material->texture, x, y), color);
    }
    color = multiply_argb(color, draw->tint);
    if ((material->flags & 16u) &&
        (color >> 24u) * 65536u < (uint32_t)material->alpha_cutoff_q16 * 255u)
        return 0;
    for (unsigned shift = 0; shift < 24u; shift += 8u)
    {
        const uint32_t sum = ((color >> shift) & 255u) + ((draw->add >> shift) & 255u);
        color = (color & ~(UINT32_C(255) << shift)) | (sum > 255u ? 255u : sum) << shift;
    }
    return color | UINT32_C(0xff000000);
}

static void triangle_vertices(const PortableRenderMesh *mesh, uint32_t index,
                              const PortableRenderInstance *instance,
                              const PortableRenderView *view, PortableScene3DVertex vertices[3])
{
    for (unsigned corner = 0; corner < 3u; ++corner)
        vertices[corner] =
            transform_vertex(&mesh->vertices[mesh->indices[index + corner]], instance, view);
}

static int culled(const PortableScene3DVertex vertices[3])
{
    const double ax = vertices[1].x - vertices[0].x, ay = vertices[1].y - vertices[0].y,
                 az = vertices[1].z - vertices[0].z;
    const double bx = vertices[2].x - vertices[0].x, by = vertices[2].y - vertices[0].y,
                 bz = vertices[2].z - vertices[0].z;
    const double facing = (ay * bz - az * by) * vertices[0].x +
                          (az * bx - ax * bz) * vertices[0].y + (ax * by - ay * bx) * vertices[0].z;
    return facing >= 0.0;
}

int portable_render_cpu_draw(PortableRenderCpu *cpu, const PortableRenderFrame *frame,
                             const PortableSoftwareFrame *target, char *error, size_t capacity)
{
    if (!cpu || !target || !target->pixels || !target->width || !target->height)
        return failure(error, capacity, "missing CPU target");
    const size_t scale = portable_software_frame_scale(target);
    if (target->width > SIZE_MAX / scale || target->height > SIZE_MAX / scale)
        return failure(error, capacity, "CPU target dimensions overflow");
    const size_t width = target->width * scale, height = target->height * scale;
    const size_t stride = portable_software_frame_stride(target);
    if (stride < width || height > SIZE_MAX / stride ||
        height * stride > SIZE_MAX / sizeof(uint32_t))
        return failure(error, capacity, "CPU target stride overflow");
    if (!portable_render_frame_valid(frame, width, height, error, capacity))
        return 0;
    for (size_t i = 0; i < frame->instance_count; ++i)
    {
        if (frame->instances[i].uv_offset[0] != 0.0 || frame->instances[i].uv_offset[1] != 0.0)
            return failure(error, capacity, "UV offsets require the GPU/GLES backend");
        if (frame->instances[i].clip_y_extent)
            return failure(error, capacity, "view intervals require the GPU/GLES backend");
        const PortableRenderMesh *mesh = frame->instances[i].mesh;
        if (mesh->vertex_argb)
            return failure(error, capacity, "vertex colours require the GPU/GLES backend");
        for (uint32_t m = 0; m < mesh->material_count; ++m)
            if (mesh->materials[m].animation ||
                (mesh->materials[m].flags & (PORTABLE_RENDER_MATERIAL_ALPHA_BLEND |
                                             PORTABLE_RENDER_MATERIAL_BACKGROUND)))
                return failure(error, capacity,
                               "alpha blending, skies and texture animation require the GPU/GLES backend");
    }
    PortableSoftwareFrame panes[PORTABLE_RENDER_VIEW_LIMIT] = {{0}};
    int success = 0;
    /* Prepare every depth owner before touching color, including allocation failures. */
    for (size_t i = 0; i < frame->view_count; ++i)
    {
        const PortableRenderView *view = &frame->views[i];
        panes[i].pixels = target->pixels + view->y * stride + view->x;
        panes[i].width = view->width;
        panes[i].height = view->height;
        panes[i].stride = stride;
        if (!portable_scene_3d_begin(&cpu->panes[i], &panes[i]))
        {
            failure(error, capacity, "CPU depth allocation failed");
            goto finish;
        }
        for (size_t j = 0; j < frame->instance_count; ++j)
        {
            if (!portable_render_instance_visible(&frame->instances[j], i))
                continue;
            PortableRenderInstance oriented = frame->instances[j];
            /* Frame validation guarantees this view operation succeeds. */
            portable_render_instance_for_view(&frame->instances[j], view, &oriented);
            const PortableRenderInstance *instance = &oriented;
            const PortableRenderMesh *mesh = instance->mesh;
            for (uint32_t run_index = 0; run_index < mesh->run_count; ++run_index)
            {
                const PortableRenderRun *run = &mesh->runs[run_index];
                for (uint32_t index = run->first_index; index < run->first_index + run->index_count;
                     index += 3u)
                {
                    PortableScene3DVertex vertices[3];
                    triangle_vertices(mesh, index, instance, view, vertices);
                    if (!portable_scene_3d_triangle_validate(&cpu->panes[i], &view->projection,
                                                             vertices))
                    {
                        failure(error, capacity, "invalid transformed CPU geometry");
                        goto finish;
                    }
                }
            }
        }
    }
    for (size_t i = 0; i < frame->view_count; ++i)
    {
        const PortableRenderView *view = &frame->views[i];
        for (size_t y = 0; y < view->height; ++y)
            for (size_t x = 0; x < view->width; ++x)
                panes[i].pixels[y * stride + x] = view->clear_argb | UINT32_C(0xff000000);
        for (size_t j = 0; j < frame->instance_count; ++j)
        {
            if (!portable_render_instance_visible(&frame->instances[j], i))
                continue;
            PortableRenderInstance oriented = frame->instances[j];
            /* Frame validation guarantees this view operation succeeds. */
            portable_render_instance_for_view(&frame->instances[j], view, &oriented);
            const PortableRenderInstance *instance = &oriented;
            const PortableRenderMesh *mesh = instance->mesh;
            for (uint32_t run_index = 0; run_index < mesh->run_count; ++run_index)
            {
                const PortableRenderRun *run = &mesh->runs[run_index];
                const PortableRenderMaterial *material = &mesh->materials[run->material];
                if (material->flags & PORTABLE_RENDER_MATERIAL_BLENDED)
                    continue;
                const DrawMaterial draw = {material, instance->tint_argb, instance->add_argb};
                for (uint32_t index = run->first_index; index < run->first_index + run->index_count;
                     index += 3u)
                {
                    PortableScene3DVertex vertices[3];
                    triangle_vertices(mesh, index, instance, view, vertices);
                    if (!(material->flags & 4u) && culled(vertices))
                        continue;
                    if (!portable_scene_3d_triangle(&cpu->panes[i], &view->projection, vertices,
                                                    sample_material, &draw))
                    {
                        failure(error, capacity, "CPU triangle submission failed");
                        goto finish;
                    }
                }
            }
        }
    }
    success = 1;
    if (error && capacity)
        error[0] = 0;
finish:
    for (size_t i = 0; i < PORTABLE_RENDER_VIEW_LIMIT; ++i)
        cpu->panes[i].frame = NULL;
    return success;
}

void portable_render_cpu_close(PortableRenderCpu *cpu)
{
    if (!cpu)
        return;
    for (size_t i = 0; i < PORTABLE_RENDER_VIEW_LIMIT; ++i)
        portable_scene_3d_close(&cpu->panes[i]);
    memset(cpu, 0, sizeof(*cpu));
}

static double channel(uint32_t argb_value, unsigned shift)
{
    return (double)((argb_value >> shift) & 255u) / 255.0;
}

void portable_render_paint_environment(const PortableRenderFrame *frame, const PortableRenderView *view,
                                       const PortableRenderInstance *instance,
                                       const PortableRenderMaterial *material, int backdrop,
                                       float out[20])
{
    memset(out, 0, 20u * sizeof *out);
    out[19] = (float)material->filter;
    if (backdrop || !frame || !view || !instance)
        return;
    const PortableRenderEnvironment *environment = &frame->environment;
    const uint32_t light = instance->light_argb ? instance->light_argb : environment->light_argb;
    /* Frames without a light (menus, previews) draw lit materials unlit. */
    if ((material->flags & PORTABLE_RENDER_MATERIAL_LIT) && light)
    {
        double toward[3] = {environment->light_direction[0], environment->light_direction[1],
                            environment->light_direction[2]};
        if (environment->light_camera)
        {
            /* The light travels along the turned view axis; N.L uses its reverse. */
            const double turn = environment->light_camera_turns * 6.283185307179586;
            const double c = cos(turn), s = sin(turn);
            toward[0] = -(c * view->forward[0] - s * view->forward[1]);
            toward[1] = -(s * view->forward[0] + c * view->forward[1]);
            toward[2] = -view->forward[2];
        }
        /* Mesh-local direction: for M = scale * rotation, normalize(M^-1 L)
         * is exact; shearing transforms are approximated. */
        const double *m = instance->transform;
        const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9],
                     k = m[10];
        const double det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
        double local[3] = {0, 0, 0}, length = 0.0;
        if (fabs(det) > 1e-12)
        {
            local[0] = ((e * k - f * h) * toward[0] + (c * h - b * k) * toward[1] + (b * f - c * e) * toward[2]) / det;
            local[1] = ((f * g - d * k) * toward[0] + (a * k - c * g) * toward[1] + (c * d - a * f) * toward[2]) / det;
            local[2] = ((d * h - e * g) * toward[0] + (b * g - a * h) * toward[1] + (a * e - b * d) * toward[2]) / det;
            length = sqrt(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
        }
        for (unsigned axis = 0; axis < 3u && length > 1e-12; ++axis)
            out[axis] = (float)(local[axis] / length);
        out[3] = 1.0f;
        for (unsigned i = 0; i < 3u; ++i)
        {
            const unsigned shift = 16u - 8u * i;
            out[4u + i] = (float)(channel(material->emission_argb, shift) +
                                  channel(light, shift) * channel(material->ambient_argb, shift));
            out[8u + i] = (float)(channel(light, shift) * channel(material->argb, shift));
        }
    }
    if ((material->flags & PORTABLE_RENDER_MATERIAL_FOG) && (environment->fog_argb >> 24) &&
        environment->fog_far > environment->fog_near &&
        (environment->fog_near > 0.0 || environment->fog_curve != PORTABLE_RENDER_FOG_RECIPROCAL))
    {
        out[12] = (float)channel(environment->fog_argb, 16);
        out[13] = (float)channel(environment->fog_argb, 8);
        out[14] = (float)channel(environment->fog_argb, 0);
        out[15] = (float)channel(environment->fog_argb, 24);
        out[16] = (float)environment->fog_near;
        out[17] = (float)environment->fog_far;
        out[18] = (float)environment->fog_curve;
    }
}
