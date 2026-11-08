#include "render_model.h"
#include "render_object_animation.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "render model line %d: %s\n", __LINE__, #c);                           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define GUARD UINT32_C(0x76543210)
#define CLEAR UINT32_C(0xff102030)

typedef struct
{
    PortableModelBinding binding;
    PortableAssetPackView pack;
    uint8_t vertices[80], triangles[32], texture[16];
} Fixture;

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (i * 8u));
}
static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}
static void fixture(Fixture *f)
{
    memset(f, 0, sizeof *f);
    f->binding = (PortableModelBinding){.kind = PORTABLE_MODEL_DRIVER,
                                        .target_id = 1001,
                                        .mesh_id = 71,
                                        .scale_q16 = {65536, 65536, 65536}};
    f->binding.mesh = (PortableModelView){.vertices = f->vertices,
                                          .triangles = f->triangles,
                                          .vertex_count = 4,
                                          .triangle_count = 4,
                                          .material_count = 2};
    const int32_t positions[4][3] = {{-2 * 65536, -2 * 65536, 2 * 65536},
                                     {2 * 65536, -2 * 65536, 2 * 65536},
                                     {2 * 65536, 2 * 65536, 2 * 65536},
                                     {-2 * 65536, 2 * 65536, 2 * 65536}};
    for (unsigned i = 0; i < 4u; ++i)
    {
        for (unsigned axis = 0; axis < 3u; ++axis)
            put32(f->vertices + i * 20u + axis * 4u, (uint32_t)positions[i][axis]);
        put32(f->vertices + i * 20u + 12u, (i == 1u || i == 2u) ? 65536u : 0u);
        put32(f->vertices + i * 20u + 16u, 32768u);
        put32(f->texture + i * 4u, ((uint32_t)(i == 0u   ? 0u
                                               : i == 1u ? 127u
                                               : i == 2u ? 128u
                                                         : 255u)
                                    << 24u) |
                                       0x003366aau);
    }
    const uint16_t triangles[4][4] = {{0, 1, 2, 0}, {0, 2, 3, 0}, {0, 1, 2, 1}, {0, 2, 3, 0}};
    for (unsigned i = 0; i < 4u; ++i)
        for (unsigned field = 0; field < 4u; ++field)
            put16(f->triangles + i * 8u + field * 2u, triangles[i][field]);
    f->binding.mesh.materials[0] =
        (PortableModelMaterial){.argb = UINT32_MAX,
                                .flags = PORTABLE_MODEL_WRAP_U,
                                .texture = {.pixels = f->texture, .width = 4, .height = 1}};
    f->binding.mesh.materials[1] =
        (PortableModelMaterial){.argb = 0xff112233u, .flags = PORTABLE_MODEL_LIT | PORTABLE_MODEL_FOG};
}

static int decode_and_atomic_failure(void)
{
    Fixture f;
    fixture(&f);
    const Fixture before = f;
    PortableRenderResources resources = {0};
    char error[192];
    CHECK(
        portable_render_resources_open_model(&resources, &f.binding, &f.pack, error, sizeof error));
    CHECK(!error[0] && resources.pack == &f.pack);
    CHECK(resources.mesh.vertex_count == 4u && resources.mesh.index_count == 12u &&
          resources.mesh.material_count == 2u);
    CHECK(resources.mesh.run_count == 3u);
    CHECK(resources.mesh.runs[0].first_index == 0u && resources.mesh.runs[0].index_count == 6u);
    CHECK(resources.mesh.runs[1].first_index == 6u && resources.mesh.runs[1].material == 1u);
    CHECK(resources.mesh.runs[2].first_index == 9u && resources.mesh.runs[2].material == 0u);
    CHECK(resources.mesh.vertices[0].position[0] == -2.0 && resources.mesh.vertices[0].uv[1] == .5);
    /* Welded smooth normals: this flat quad faces +Z. */
    CHECK(resources.mesh.vertices[0].normal[2] == 1.0 && resources.mesh.vertices[3].normal[0] == 0.0 &&
          resources.mesh.indices[11] == 3u);
    CHECK(resources.mesh.materials[0].flags == (1u | 4u | 16u));
    CHECK(resources.mesh.materials[1].flags ==
          (4u | 16u | PORTABLE_RENDER_MATERIAL_LIT | PORTABLE_RENDER_MATERIAL_FOG));
    CHECK(resources.mesh.materials[1].ambient_argb == PORTABLE_RENDER_MODEL_AMBIENT);
    CHECK(resources.mesh.materials[0].alpha_cutoff_q16 == 32768);
    CHECK(resources.mesh.materials[0].texture.pixels == f.texture);
    CHECK(!memcmp(&f, &before, sizeof f));
    const PortableRenderResources saved = resources;
    PortableModelBinding invalid = f.binding;
    invalid.mesh.vertex_count = PORTABLE_MODEL_MAX_VERTICES + 1u;
    CHECK(
        !portable_render_resources_open_model(&resources, &invalid, &f.pack, error, sizeof error));
    CHECK(strstr(error, "model=71") && !memcmp(&resources, &saved, sizeof resources));
    invalid = f.binding;
    invalid.mesh.materials[0].flags = 16u;
    CHECK(
        !portable_render_resources_open_model(&resources, &invalid, &f.pack, error, sizeof error));
    invalid = f.binding;
    invalid.mesh.materials[0].texture.width = 1025;
    CHECK(
        !portable_render_resources_open_model(&resources, &invalid, &f.pack, error, sizeof error));
    invalid = f.binding;
    invalid.mesh.materials[0].texture.palette = f.texture;
    CHECK(
        !portable_render_resources_open_model(&resources, &invalid, &f.pack, error, sizeof error));
    put16(f.triangles + 6u, 2u);
    CHECK(!portable_render_resources_open_model(&resources, &f.binding, &f.pack, error,
                                                sizeof error));
    put16(f.triangles + 6u, 0u);
    put16(f.triangles, 4u);
    CHECK(!portable_render_resources_open_model(&resources, &f.binding, &f.pack, error,
                                                sizeof error));
    put16(f.triangles, 0u);
    CHECK(!memcmp(&resources, &saved, sizeof resources));
    CHECK(!portable_render_resources_open_model(&resources, NULL, &f.pack, error, sizeof error));
    CHECK(!portable_render_resources_open_model(&resources, &f.binding, NULL, error, sizeof error));
    portable_render_resources_close(&resources);
    CHECK(!resources.storage && !resources.mesh.vertices);
    portable_render_resources_close(&resources);
    return 0;
}

static int transforms_and_large_local_geometry(void)
{
    Fixture f;
    fixture(&f);
    f.binding.scale_q16[0] = 2 * 65536;
    f.binding.scale_q16[1] = 32768;
    f.binding.scale_q16[2] = -65536;
    f.binding.pivot_q16[0] = 65536;
    f.binding.pivot_q16[1] = -2 * 65536;
    f.binding.pivot_q16[2] = 3 * 65536;
    PortableRenderPose pose = {.position = {10, 20, 30}, .forward = {0, 1, 0}, .up = {0, 0, 1}};
    double transform[12];
    CHECK(portable_render_model_transform(transform, &f.binding, &pose, .75, .25));
    CHECK(transform[0] == -.5 && transform[5] == -.125 && transform[10] == -.25);
    const double point[3] = {3, 2, 11};
    const double expected[3] = {9, 19.5, 27.25};
    for (unsigned row = 0; row < 3u; ++row)
    {
        double value = transform[row * 4u + 3u];
        for (unsigned axis = 0; axis < 3u; ++axis)
            value += transform[row * 4u + axis] * point[axis];
        CHECK(fabs(value - expected[row]) < 1e-12);
    }
    const double saved[12] = {transform[0], transform[1], transform[2],  transform[3],
                              transform[4], transform[5], transform[6],  transform[7],
                              transform[8], transform[9], transform[10], transform[11]};
    const double invalid_units[4] = {0, -1, NAN, INFINITY};
    for (unsigned i = 0; i < 4u; ++i)
    {
        CHECK(
            !portable_render_model_transform(transform, &f.binding, &pose, .75, invalid_units[i]));
        CHECK(!memcmp(transform, saved, sizeof saved));
    }
    f.binding.scale_q16[0] = 0;
    CHECK(!portable_render_model_transform(transform, &f.binding, &pose, .75, 1));
    CHECK(!memcmp(transform, saved, sizeof saved));
    fixture(&f);
    pose = (PortableRenderPose){.forward = {0, -1, 0}, .up = {0, 0, 1}};
    CHECK(portable_render_model_transform(transform, &f.binding, &pose, 0, 1));
    for (unsigned row = 0; row < 3u; ++row)
        for (unsigned column = 0; column < 4u; ++column)
            CHECK(transform[row * 4u + column] == (row == column ? 1.0 : 0.0));
    put32(f.vertices, 6000u * 65536u);
    PortableRenderResources resources = {0};
    char error[192];
    CHECK(
        portable_render_resources_open_model(&resources, &f.binding, &f.pack, error, sizeof error));
    pose = (PortableRenderPose){.forward = {0, -1, 0}, .up = {0, 0, 1}};
    PortableRenderInstance instance = {.mesh = &resources.mesh, .tint_argb = UINT32_MAX};
    CHECK(portable_render_model_transform(instance.transform, &f.binding, &pose, 0, .001));
    CHECK(fabs(resources.mesh.vertices[0].position[0] * instance.transform[0] - 6.0) < 1e-12);
    PortableRenderFrame frame = {.instances = &instance, .instance_count = 1, .view_count = 1};
    frame.views[0] = (PortableRenderView){.width = 8,
                                          .height = 8,
                                          .right = {1, 0, 0},
                                          .up = {0, 1, 0},
                                          .forward = {0, 0, 1},
                                          .projection = {4, 4, 4, 4, .1, 100}};
    CHECK(portable_render_frame_valid(&frame, 8, 8, error, sizeof error));
    portable_render_resources_close(&resources);
    return 0;
}

static int scaled_pivot_seating(void)
{
    Fixture f;
    fixture(&f);
    f.binding.pivot_q16[0] = 2 * 65536;
    f.binding.pivot_q16[1] = -3 * 65536;
    f.binding.pivot_q16[2] = 4 * 65536;
    f.binding.scale_q16[0] = -2 * 65536;
    f.binding.scale_q16[1] = 32768;
    f.binding.scale_q16[2] = 3 * 65536;
    const PortableModelBinding binding_before = f.binding;
    const PortableRenderPose pose = {
        .position = {-4, 7, 12}, .forward = {0, .8, -.6}, .up = {0, .6, .8}};
    const PortableRenderPose pose_before = pose;
    const double ratios[][3] = {{1, 1, 1}, {.5, .5, .5}, {2, 2, .25}};
    for (unsigned i = 0; i < 3; ++i)
    {
        double transform[12], basis[12];
        CHECK(portable_render_model_transform_scaled(transform, &f.binding, &pose, .6, .05,
                                                     ratios[i]));
        CHECK(portable_render_pose_actor(&pose, .6, basis));
        for (unsigned row = 0; row < 3; ++row)
        {
            double pivot = transform[row * 4u + 3u];
            for (unsigned axis = 0; axis < 3; ++axis)
                pivot += transform[row * 4u + axis] * f.binding.pivot_q16[axis] / 65536.0;
            CHECK(fabs(pivot - basis[row * 4u + 3u]) < 1e-12);
        }
        if (!i)
        {
            double legacy[12];
            CHECK(portable_render_model_transform(legacy, &f.binding, &pose, .6, .05));
            CHECK(!memcmp(legacy, transform, sizeof legacy));
        }
        const double saved[12] = {transform[0], transform[1], transform[2],  transform[3],
                                  transform[4], transform[5], transform[6],  transform[7],
                                  transform[8], transform[9], transform[10], transform[11]};
        const double bad[] = {0, -1, NAN, INFINITY, 32769};
        for (unsigned j = 0; j < sizeof bad / sizeof bad[0]; ++j)
        {
            double invalid[3] = {1, 1, bad[j]};
            CHECK(!portable_render_model_transform_scaled(transform, &f.binding, &pose, .6, .05,
                                                          invalid));
            CHECK(!memcmp(saved, transform, sizeof saved));
        }
        CHECK(!portable_render_model_transform_scaled(transform, &f.binding, &pose, .6, .05, NULL));
        CHECK(!memcmp(saved, transform, sizeof saved));
    }
    CHECK(!memcmp(&pose, &pose_before, sizeof pose));
    CHECK(!memcmp(&f.binding, &binding_before, sizeof binding_before));
    return 0;
}

static int local_scenery_transform(void)
{
    /* A rotated, reflected, nonuniform OBJ still owns its SI conversion and
     * translation. Two placements can share the same immutable local mesh. */
    const double original[12] = {0, -3, 0, 10, 2, 0, 0, 20, 0, 0, -.5, 30};
    Fixture f;
    fixture(&f);
    f.binding.scale_q16[0] = 2 * 65536;
    f.binding.scale_q16[1] = 32768;
    f.binding.scale_q16[2] = -65536;
    f.binding.pivot_q16[0] = 65536;
    f.binding.pivot_q16[1] = -2 * 65536;
    f.binding.pivot_q16[2] = 3 * 65536;
    const Fixture saved_fixture = f;
    double out[12];
    CHECK(portable_render_model_local_transform(out, original, &f.binding));
    const double expected[12] = {0, -1.5, 0, 7, 4, 0, 0, 16, 0, 0, .5, 28.5};
    for (unsigned i = 0; i < 12u; ++i)
        CHECK(out[i] == expected[i]);
    /* The pivot maps to precisely the original OBJ origin. */
    for (unsigned row = 0; row < 3u; ++row)
    {
        double value = out[row * 4u + 3u];
        for (unsigned axis = 0; axis < 3u; ++axis)
            value += out[row * 4u + axis] * f.binding.pivot_q16[axis] / 65536.0;
        CHECK(value == original[row * 4u + 3u]);
    }
    memcpy(out, original, sizeof out);
    CHECK(portable_render_model_local_transform(out, out, &f.binding));
    for (unsigned i = 0; i < 12u; ++i)
        CHECK(out[i] == expected[i]);
    CHECK(!memcmp(&f, &saved_fixture, sizeof f));
    CHECK(portable_render_model_local_transform(out, original, NULL));
    CHECK(!memcmp(out, original, sizeof out));
    f.binding.scale_q16[1] = 0;
    CHECK(!portable_render_model_local_transform(out, original, &f.binding));
    CHECK(!memcmp(out, original, sizeof out));
    f = saved_fixture;
    f.binding.pivot_q16[0] = INT32_MAX;
    CHECK(!portable_render_model_local_transform(out, original, &f.binding));
    CHECK(!memcmp(out, original, sizeof out));
    /* Host selection tries the optional affine in-place before opening its
     * resource. An otherwise valid binding with an excessive pivot keeps the
     * original affine exactly, so the direct model/billboard can remain selected. */
    CHECK(!portable_render_model_local_transform(out, out, &f.binding));
    CHECK(!memcmp(out, original, sizeof out));
    f = saved_fixture;
    f.binding.scale_q16[1] = INT32_MAX;
    CHECK(!portable_render_model_local_transform(out, out, &f.binding));
    CHECK(!memcmp(out, original, sizeof out));
    double invalid[12];
    memcpy(invalid, original, sizeof invalid);
    invalid[0] = NAN;
    CHECK(!portable_render_model_local_transform(out, invalid, NULL));
    CHECK(!memcmp(out, original, sizeof out));
    return 0;
}

static int alpha_and_physical_stride(unsigned scale)
{
    Fixture f;
    fixture(&f);
    f.binding.mesh.triangle_count = 2u;
    f.binding.mesh.material_count = 1u;
    f.binding.mesh.materials[0].flags = 0;
    const Fixture before = f;
    const size_t extent = 8u * scale, stride = extent + 5u;
    uint32_t pixels[(32u + 5u) * 32u + 2u];
    for (size_t i = 0; i < sizeof pixels / sizeof pixels[0]; ++i)
        pixels[i] = GUARD;
    PortableSoftwareFrame target = {
        .pixels = pixels + 1, .width = 8, .height = 8, .scale = (uint8_t)scale, .stride = stride};
    PortableRenderResources resources = {0};
    PortableRenderCpu cpu = {0};
    char error[192];
    CHECK(
        portable_render_resources_open_model(&resources, &f.binding, &f.pack, error, sizeof error));
    PortableRenderInstance instance;
    portable_render_instance_identity(&instance, &resources.mesh);
    PortableRenderFrame frame = {.instances = &instance, .instance_count = 1, .view_count = 1};
    frame.views[0] = (PortableRenderView){
        .width = extent,
        .height = extent,
        .right = {1, 0, 0},
        .up = {0, 1, 0},
        .forward = {0, 0, 1},
        .projection = {4.0 * scale, 4.0 * scale, 4.0 * scale, 4.0 * scale, .1, 100},
        .clear_argb = CLEAR};
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof error));
    for (size_t x = 0; x < extent; ++x)
        CHECK(pixels[1u + (extent / 2u) * stride + x] ==
              (x < extent / 2u ? CLEAR : UINT32_C(0xff3366aa)));
    /* Material alpha128 keeps only texel alpha255; alpha128 becomes64. */
    f.binding.mesh.materials[0].argb = UINT32_C(0x80ffffff);
    CHECK(
        portable_render_resources_open_model(&resources, &f.binding, &f.pack, error, sizeof error));
    instance.mesh = &resources.mesh;
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof error));
    for (size_t x = 0; x < extent; ++x)
        CHECK(pixels[1u + (extent / 2u) * stride + x] ==
              (x < extent * 3u / 4u ? CLEAR : UINT32_C(0xff3366aa)));
    for (size_t y = 0; y < extent; ++y)
        for (size_t x = extent; x < stride; ++x)
            CHECK(pixels[1u + y * stride + x] == GUARD);
    CHECK(pixels[0] == GUARD && pixels[1u + extent * stride] == GUARD);
    f.binding.mesh.materials[0].argb = UINT32_MAX;
    CHECK(!memcmp(&f, &before, sizeof f));
    portable_render_cpu_close(&cpu);
    portable_render_resources_close(&resources);
    return 0;
}

static int motion_matches_collision(void)
{
    /* A rotated, nonuniformly scaled model with an off-origin contact point.
     * The pivot is deliberately distinct from its placement (orbit/hinge). */
    const double rest[12] = {0, -2, 0, 11, 3, 0, 0, -7, 0, 0, .5, 4};
    const int32_t local[3] = {2 * 65536, -65536, 6 * 65536};
    const int32_t contact[3] = {13 * 65536, -65536, 7 * 65536};
    PortableMeshMotion motion = {.kind = PORTABLE_MESH_MOTION_SPIN,
        .pivot_q16 = {8 * 65536, -3 * 65536, 2 * 65536},
        .axis_q30 = {0, 1073741824, 0}, .rate = -12000000};
    for (unsigned kind = 1; kind <= 3; ++kind)
    {
        motion.kind = kind;
        motion.amplitude = 12000;
        motion.period = 180;
        motion.point_count = 3;
        motion.face = 1;
        const int32_t points[3][3] = {{65536, 2*65536, 0}, {5*65536, 2*65536, 65536},
                                     {5*65536, -3*65536, 0}};
        memcpy(motion.points_q16, points, sizeof points);
        motion.travel[0] = 30; motion.travel[1] = 45; motion.travel[2] = 60;
        motion.wait = 7;
        CHECK(portable_mesh_motion_valid(&motion));
        for (uint32_t tick = 0; tick < 500; ++tick)
        {
            PortableMeshPose pose;
            portable_mesh_motion_pose(&motion, tick, &pose);
            int32_t collision[3];
            portable_mesh_pose_point(&pose, contact, collision);
            double rendered[12];
            memcpy(rendered, rest, sizeof rest);
            portable_render_model_motion(&motion, tick, rendered);
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                double value = rendered[axis*4+3];
                for (unsigned k = 0; k < 3; ++k)
                    value += rendered[axis*4+k] * local[k] / 65536.0;
                CHECK(fabs(value - collision[axis] / 65536.0) < 2.0 / 65536.0);
            }
            /* Repeated draws, including pause and split views, are read-only. */
            double repeated[12];
            memcpy(repeated, rest, sizeof rest);
            portable_render_model_motion(&motion, tick, repeated);
            CHECK(!memcmp(repeated, rendered, sizeof repeated));
            /* Interpolated presentation lands on the same pose at each accepted tick. */
            PortableRenderInstance sampled = {0};
            memcpy(sampled.transform, rest, sizeof rest);
            portable_render_motion_sample(&motion, tick, 65536u, &sampled);
            CHECK(!memcmp(sampled.transform, rendered, sizeof rendered));
        }
    }
    return 0;
}

static int fractional_material(void)
{
    const double matrices[12] = {1,0,0,1,.9,0, 1,0,0,1,.1,.5};
    const PortableRenderTextureAnimation animation = {.matrix_frames=2, .matrix_count=2, .matrices=matrices};
    const PortableRenderMaterial material = {.flags=1, .animation=&animation};
    const PortableNativeImage *texture;
    double uv[6];
    portable_render_material_sample(&material, 1, 32768, uv, &texture);
    CHECK(fabs(uv[4]) < 1e-10 && uv[5] == .25 && texture == &material.texture);
    portable_render_material_sample(&material, 1, 0, uv, &texture);
    CHECK(uv[4] == .1 && uv[5] == .5);
    portable_render_material_sample(&material, 0, 65536, uv, &texture);
    CHECK(uv[4] == .9 && uv[5] == 0);
    return 0;
}

static int fractional_skin(void)
{
    uint8_t poses[96] = {0}, nodes[2] = {0, PORTABLE_MODEL_RIG_STATIC};
    for (unsigned frame = 0; frame < 2; ++frame)
    {
        for (unsigned axis = 0; axis < 3; ++axis)
            put32(poses + frame*48 + (axis*4+axis)*4, 65536);
        put32(poses + frame*48 + 12, frame*4*65536);
    }
    PortableModelRig rig = {.poses=poses, .nodes=nodes, .vertex_count=2, .node_count=1,
        .clip_count=1, .pose_count=2, .clips={{.frames=2}}};
    const PortableRenderVertex vertices[2] = {{.position={1,2,3}}, {.position={4,5,6}}};
    const PortableRenderMesh rest = {.vertices=vertices, .vertex_count=2};
    PortableRenderVertex out[2];
    for (unsigned sub = 0; sub <= 4; ++sub)
    {
        CHECK(portable_render_model_skin_sample(&rest, &rig, 0, sub/4.0, out));
        CHECK(fabs(out[0].position[0] - (1 + sub)) < 1e-10);
        CHECK(!memcmp(&out[1], &vertices[1], sizeof out[1]));
    }
    CHECK(portable_render_model_skin_sample(&rest, &rig, 0, 50, out));
    CHECK(out[0].position[0] == 5);
    rig.clips[0].flags = PORTABLE_MODEL_CLIP_LOOP;
    CHECK(portable_render_model_skin_sample(&rest, &rig, 0, 1.5, out));
    CHECK(out[0].position[0] == 3);
    CHECK(portable_render_model_skin_sample(&rest, &rig, 0, 2, out));
    CHECK(out[0].position[0] == 1);
    const PortableRenderVertex saved = out[0];
    CHECK(!portable_render_model_skin_sample(&rest, &rig, 0, NAN, out));
    CHECK(!memcmp(&saved, out, sizeof saved));
    return 0;
}

/* Animated objects play one clip frame per accepted tick, between the
 * previous and current tick by the presentation alpha, and wrap. */
static int clip_loop_clock(void)
{
    CHECK(portable_render_clip_loop_frame(7, 65536u, 0) == 0);
    CHECK(portable_render_clip_loop_frame(0, 0, 5) == 0);
    CHECK(portable_render_clip_loop_frame(3, 65536u, 5) == 3);
    CHECK(portable_render_clip_loop_frame(3, 70000u, 5) == 3);
    CHECK(portable_render_clip_loop_frame(3, 32768u, 5) == 2.5);
    CHECK(portable_render_clip_loop_frame(5, 65536u, 5) == 0);
    CHECK(portable_render_clip_loop_frame(5, 16384u, 5) == 4.25);
    CHECK(portable_render_clip_loop_frame(12, 0, 5) == 1);
    return 0;
}

int main(void)
{
    if (motion_matches_collision() || fractional_material() || fractional_skin() || clip_loop_clock() ||
        decode_and_atomic_failure() || transforms_and_large_local_geometry() ||
        local_scenery_transform() || scaled_pivot_seating() || alpha_and_physical_stride(1))
        return 1;
    puts("Render model resources: PASS");
    return 0;
}
