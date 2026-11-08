#include "render_scene.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define GUARD UINT32_C(0x76543210)

typedef struct
{
    PortableCourse3DView course;
    uint8_t vertices[128], triangles[48], materials[96];
} Fixture;

static void put(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (i * 8u));
}

static void fixture_init(Fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->course.id = 90000u;
    const int32_t positions[4][3] = {{-65536, -65536, 4 * 65536},
                                     {65536, -65536, 4 * 65536},
                                     {65536, 65536, 4 * 65536},
                                     {-65536, 65536, 4 * 65536}};
    for (unsigned vertex = 0; vertex < 4u; ++vertex)
    {
        for (unsigned axis = 0; axis < 3u; ++axis)
            put(fixture->vertices + vertex * 32u + axis * 4u, (uint32_t)positions[vertex][axis]);
        put(fixture->vertices + vertex * 32u + 20u, 1073741824u);
        put(fixture->vertices + vertex * 32u + 24u, vertex == 1u || vertex == 2u ? 65536u : 0u);
    }
    const uint32_t triangles[3][4] = {{0, 1, 2, 0}, {0, 2, 3, 1}, {0, 1, 2, 0}};
    for (unsigned triangle = 0; triangle < 3u; ++triangle)
        for (unsigned field = 0; field < 4u; ++field)
            put(fixture->triangles + triangle * 16u + field * 4u, triangles[triangle][field]);
    for (unsigned material = 0; material < 2u; ++material)
    {
        uint8_t *row = fixture->materials + material * 48u;
        for (unsigned texture = 0; texture < 4u; ++texture)
            put(row + texture * 4u, UINT32_MAX);
        put(row + 16u, material ? UINT32_C(0x0033ccff) : UINT32_C(0xcc3300ff));
        put(row + 36u, 4u);
    }
    fixture->course.tables[0] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('V', 'E', 'R', 'T'),
                                                        4, 32, fixture->vertices};
    fixture->course.tables[1] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('T', 'R', 'I', 'S'),
                                                        3, 16, fixture->triangles};
    fixture->course.tables[2] = (PortableCourse3DTable){PORTABLE_COURSE_3D_TAG('M', 'A', 'T', 'S'),
                                                        2, 48, fixture->materials};
    fixture->course.table_count = 3;
}

static PortableRenderView view(size_t x, size_t y, size_t width, size_t height, uint32_t clear)
{
    PortableRenderView result = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .right = {1, 0, 0},
        .up = {0, 1, 0},
        .forward = {0, 0, 1},
        .projection = {8, 8, (double)width / 2.0, (double)height / 2.0, 0.1, 100},
        .clear_argb = clear};
    return result;
}

static uint32_t solid(const void *context, double u, double v)
{
    (void)u;
    (void)v;
    return *(const uint32_t *)context;
}

static int resources_and_prior_cpu(void)
{
    Fixture fixture;
    fixture_init(&fixture);
    PortableRenderResources resources = {0};
    char error[192];
    CHECK(portable_render_resources_open_course(&resources, &fixture.course, error, sizeof(error)));
    CHECK(resources.mesh.vertex_count == 4u && resources.mesh.index_count == 9u &&
          resources.mesh.run_count == 3u);
    CHECK(resources.mesh.runs[0].material == 0u && resources.mesh.runs[1].material == 1u &&
          resources.mesh.runs[2].material == 0u);
    CHECK(resources.mesh.runs[2].first_index == 6u && resources.mesh.indices[7] == 1u);
    CHECK(resources.mesh.vertices[0].position[0] == -1.0 &&
          resources.mesh.vertices[0].position[2] == 4.0);
    PortableRenderInstance instance;
    portable_render_instance_identity(&instance, &resources.mesh);
    PortableRenderFrame frame = {.instances = &instance, .instance_count = 1, .view_count = 1};
    frame.views[0] = view(0, 0, 16, 12, UINT32_C(0xff102030));
    uint32_t pixels[16 * 12], reference[16 * 12];
    PortableSoftwareFrame target = {.pixels = pixels, .width = 16, .height = 12};
    PortableRenderCpu cpu = {0};
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)));
    PortableScene3D original = {0};
    PortableSoftwareFrame old_target = {.pixels = reference, .width = 16, .height = 12};
    for (size_t i = 0; i < 16u * 12u; ++i)
        reference[i] = UINT32_C(0xff102030);
    CHECK(portable_scene_3d_begin(&original, &old_target));
    const PortableScene3DVertex vertices[4] = {
        {-1, -1, 4, 0, 0}, {1, -1, 4, 1, 0}, {1, 1, 4, 1, 0}, {-1, 1, 4, 0, 0}};
    const uint32_t order[3][3] = {{0, 1, 2}, {0, 2, 3}, {0, 1, 2}};
    const uint32_t colors[3] = {UINT32_C(0xffcc3300), UINT32_C(0xff0033cc), UINT32_C(0xffcc3300)};
    for (unsigned i = 0; i < 3u; ++i)
    {
        const PortableScene3DVertex triangle[3] = {vertices[order[i][0]], vertices[order[i][1]],
                                                   vertices[order[i][2]]};
        CHECK(portable_scene_3d_triangle(&original, &frame.views[0].projection, triangle, solid,
                                         &colors[i]));
    }
    CHECK(memcmp(pixels, reference, sizeof(pixels)) == 0);
    CHECK(memcmp(cpu.panes[0].depth, original.depth, sizeof(pixels)) == 0);
    CHECK(cpu.panes[0].frame == NULL);
    /* New GPU-only offsets cannot silently alter the frozen CPU adapter. */
    instance.uv_offset[0] = 0.5;
    CHECK(portable_render_frame_valid(&frame, 16, 12, error, sizeof error));
    CHECK(!portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof error));
    CHECK(strstr(error, "UV offsets require") && memcmp(pixels, reference, sizeof pixels) == 0);
    instance.uv_offset[1] = NAN;
    CHECK(!portable_render_frame_valid(&frame, 16, 12, error, sizeof error));
    instance.uv_offset[1] = 32769;
    CHECK(!portable_render_frame_valid(&frame, 16, 12, error, sizeof error));
    instance.uv_offset[0] = instance.uv_offset[1] = 0;
    portable_scene_3d_close(&original);
    void *storage = resources.storage;
    /* Runtime-only flags (multiply here) cannot come from course content. */
    put(fixture.materials + 48u + 36u, 64u);
    CHECK(
        !portable_render_resources_open_course(&resources, &fixture.course, error, sizeof(error)));
    CHECK(resources.storage == storage && strstr(error, "invalid material flags") &&
          resources.mesh.runs[2].material == 0);
    put(fixture.materials + 48u + 36u, 4u);
    put(fixture.triangles + 32u, 99u);
    CHECK(
        !portable_render_resources_open_course(&resources, &fixture.course, error, sizeof(error)) &&
        resources.storage == storage);
    portable_render_cpu_close(&cpu);
    portable_render_resources_close(&resources);
    CHECK(!resources.storage && !resources.mesh.vertices);
    return 0;
}

static int split_depth_cutout_and_ownership(void)
{
    Fixture fixture;
    fixture_init(&fixture);
    PortableRenderResources resources = {0};
    char error[192];
    CHECK(portable_render_resources_open_course(&resources, &fixture.course, error, sizeof(error)));
    uint8_t texture[8];
    put(texture, UINT32_C(0xff11ee22));
    put(texture + 4u, 0u);
    PortableRenderMaterial material = {.argb = UINT32_C(0xffffffff),
                                       .flags = 4u | 16u,
                                       .alpha_cutoff_q16 = 32768,
                                       .texture = {.pixels = texture, .width = 2, .height = 1}};
    const uint32_t near_indices[6] = {0, 1, 2, 0, 2, 3};
    const PortableRenderRun near_run = {0, 6, 0};
    PortableRenderMesh near_mesh = {
        resources.mesh.vertices, near_indices, &material, &near_run, 4, 6, 1, 1, NULL, 0u};
    PortableRenderInstance instances[2];
    portable_render_instance_identity(&instances[0], &resources.mesh);
    portable_render_instance_identity(&instances[1], &near_mesh);
    instances[1].transform[11] = -1.0;
    PortableRenderFrame frame = {.instances = instances, .instance_count = 2, .view_count = 4};
    frame.views[0] = view(1, 1, 16, 12, UINT32_C(0xff010101));
    frame.views[1] = view(18, 1, 16, 12, UINT32_C(0xff020202));
    frame.views[1].position[0] = 1.0;
    frame.views[2] = view(1, 14, 16, 12, UINT32_C(0xff030303));
    frame.views[3] = view(18, 14, 16, 12, UINT32_C(0xff040404));
    enum
    {
        WIDTH = 35,
        HEIGHT = 27,
        STRIDE = 43
    };
    uint32_t guarded[STRIDE * HEIGHT + 2], repeated[STRIDE * HEIGHT + 2];
    for (size_t i = 0; i < sizeof(guarded) / sizeof(guarded[0]); ++i)
        guarded[i] = GUARD;
    PortableSoftwareFrame target = {
        .pixels = guarded + 1, .width = WIDTH, .height = HEIGHT, .stride = STRIDE};
    uint8_t frame_before[sizeof(frame)], instances_before[sizeof(instances)],
        target_before[sizeof(target)], fixture_before[sizeof(fixture)];
    memcpy(frame_before, &frame, sizeof(frame));
    memcpy(instances_before, instances, sizeof(instances));
    memcpy(target_before, &target, sizeof(target));
    memcpy(fixture_before, &fixture, sizeof(fixture));
    PortableRenderCpu cpu = {0};
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)));
    CHECK(memcmp(frame_before, &frame, sizeof(frame)) == 0 &&
          memcmp(instances_before, instances, sizeof(instances)) == 0);
    CHECK(memcmp(target_before, &target, sizeof(target)) == 0 &&
          memcmp(fixture_before, &fixture, sizeof(fixture)) == 0);
    for (size_t pane = 0; pane < 4u; ++pane)
    {
        const PortableRenderView *v = &frame.views[pane];
        const size_t near_x = pane == 1u ? 3u : 6u;
        CHECK(guarded[1u + (v->y + 6u) * STRIDE + v->x + near_x] == UINT32_C(0xff11ee22));
        CHECK(guarded[1u + (v->y + 6u) * STRIDE + v->x + 8u] ==
              (pane == 1u ? UINT32_C(0xff020202) : UINT32_C(0xffcc3300)));
        CHECK(cpu.panes[pane].depth[6u * 16u + near_x] == 3u * 65536u);
        CHECK(cpu.panes[pane].depth[6u * 16u + 8u] == (pane == 1u ? UINT32_MAX : 4u * 65536u));
        CHECK(cpu.panes[pane].frame == NULL);
    }
    for (size_t y = 0; y < HEIGHT; ++y)
        for (size_t x = 0; x < STRIDE; ++x)
        {
            int inside = 0;
            for (size_t p = 0; p < 4u; ++p)
                inside |= x >= frame.views[p].x && x < frame.views[p].x + frame.views[p].width &&
                          y >= frame.views[p].y && y < frame.views[p].y + frame.views[p].height;
            if (!inside)
                CHECK(guarded[1u + y * STRIDE + x] == GUARD);
        }
    CHECK(guarded[0] == GUARD && guarded[STRIDE * HEIGHT + 1] == GUARD);
    memcpy(repeated, guarded, sizeof(guarded));
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)) &&
          memcmp(repeated, guarded, sizeof(guarded)) == 0);
    /* The invalid fourth viewport must fail before even the first viewport clears. */
    frame.views[3].x = WIDTH;
    CHECK(!portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)) &&
          memcmp(repeated, guarded, sizeof(guarded)) == 0);
    frame.views[3].x = 18;
    frame.views[1].x = 1;
    CHECK(!portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)) &&
          strstr(error, "overlap"));
    frame.views[1].x = 18;
    material.flags = 32;
    CHECK(!portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)) &&
          strstr(error, "alpha blending") && memcmp(repeated, guarded, sizeof(guarded)) == 0);
    material.flags = 4u | 16u;
    instances[1].transform[0] = NAN;
    CHECK(!portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)) &&
          memcmp(repeated, guarded, sizeof(guarded)) == 0);
    instances[1].transform[0] = -1;
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)));
    CHECK(guarded[1u + 7u * STRIDE + 7u] == UINT32_C(0xff0033cc));
    CHECK(guarded[1u + 7u * STRIDE + 9u] == UINT32_C(0xff11ee22));
    instances[1].transform[0] = 1;
    frame.view_count = 2;
    frame.views[0] = view(1, 1, 8, 9, UINT32_C(0xff010101));
    frame.views[1] = view(18, 1, 8, 9, UINT32_C(0xff020202));
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)));
    frame.view_count = 4;
    frame.views[0] = view(1, 1, 16, 12, UINT32_C(0xff010101));
    frame.views[1] = view(18, 1, 16, 12, UINT32_C(0xff020202));
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)));
    instances[0].hidden_views = instances[1].hidden_views = 1u;
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)));
    CHECK(guarded[1u + 7u * STRIDE + 9u] == frame.views[0].clear_argb);
    CHECK(guarded[1u + (frame.views[1].y + 6u) * STRIDE + frame.views[1].x + 6u] ==
          UINT32_C(0xff11ee22));
    instances[0].hidden_views = instances[1].hidden_views = 15u;
    CHECK(portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof(error)));
    for (size_t i = 0; i < 4; ++i)
    {
        const PortableRenderView *pane = &frame.views[i];
        CHECK(guarded[1u + (pane->y + pane->height / 2u) * STRIDE + pane->x + pane->width / 2u] ==
              pane->clear_argb);
    }
    memcpy(repeated, guarded, sizeof guarded);
    instances[1].hidden_views = 16u;
    CHECK(!portable_render_frame_valid(&frame, WIDTH, HEIGHT, error, sizeof error));
    CHECK(!portable_render_cpu_draw(&cpu, &frame, &target, error, sizeof error) &&
          !memcmp(repeated, guarded, sizeof guarded));
    instances[0].hidden_views = instances[1].hidden_views = 0;
    CHECK(portable_render_instance_visible(&instances[0], 0));
    CHECK(portable_render_instance_visible(&instances[0], 3));
    CHECK(!portable_render_instance_visible(&instances[0], 4));
    CHECK(!portable_render_instance_visible(NULL, 0));
    portable_render_cpu_close(&cpu);
    portable_render_resources_close(&resources);
    return 0;
}

static int physical_scale_and_stride(void)
{
    Fixture fixture;
    fixture_init(&fixture);
    PortableRenderResources resources = {0};
    char error[192];
    CHECK(portable_render_resources_open_course(&resources, &fixture.course, error, sizeof(error)));
    PortableRenderInstance instance;
    portable_render_instance_identity(&instance, &resources.mesh);
    PortableRenderFrame scene = {.instances = &instance, .instance_count = 1, .view_count = 1};
    enum
    {
        WIDTH = 140,
        HEIGHT = 108,
        STRIDE = 147
    };
    uint32_t pixels[STRIDE * HEIGHT + 2];
    for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); ++i)
        pixels[i] = GUARD;
    PortableSoftwareFrame target = {
        .pixels = pixels + 1, .width = 35, .height = 27, .scale = 4, .stride = STRIDE};
    scene.views[0] = view(0, 0, WIDTH, HEIGHT, UINT32_C(0xff102030));
    scene.views[0].projection.focal_x = scene.views[0].projection.focal_y = 32;
    PortableRenderCpu cpu = {0};
    CHECK(portable_render_cpu_draw(&cpu, &scene, &target, error, sizeof(error)));
    CHECK(pixels[1u + 54u * STRIDE + 70u] == UINT32_C(0xffcc3300));
    CHECK(cpu.panes[0].width == WIDTH && cpu.panes[0].height == HEIGHT &&
          cpu.panes[0].depth[54u * WIDTH + 70u] == 4u * 65536u);
    for (size_t y = 0; y < HEIGHT; ++y)
        for (size_t x = WIDTH; x < STRIDE; ++x)
            CHECK(pixels[1u + y * STRIDE + x] == GUARD);
    CHECK(pixels[0] == GUARD && pixels[STRIDE * HEIGHT + 1u] == GUARD);
    portable_render_cpu_close(&cpu);
    portable_render_resources_close(&resources);
    return 0;
}

static int world_resolution(void)
{
    size_t width = 7, height = 9;
    char error[128];
    CHECK(portable_render_world_extent(320, 180, 1, 1, &width, &height, error, sizeof error));
    CHECK(width == 320 && height == 180);
    CHECK(portable_render_world_extent(320, 180, 4, 1, &width, &height, error, sizeof error));
    CHECK(width == 1138 && height == 640);
    CHECK(portable_render_world_extent(320, 180, 4, 2, &width, &height, error, sizeof error));
    CHECK(width == 2276 && height == 1280);
    CHECK(portable_render_world_extent(3840, 2160, 4, 1, &width, &height, error, sizeof error));
    CHECK(width == 3840 && height == 2160);
    CHECK(portable_render_world_extent(7680, 4320, 4, 1, &width, &height, error, sizeof error));
    CHECK(width == 4320 && height == 2430 && width * height <= PORTABLE_RENDER_TARGET_PIXEL_LIMIT);
    const size_t old_width = width, old_height = height;
    CHECK(!portable_render_world_extent(0, 180, 1, 1, &width, &height, error, sizeof error));
    CHECK(!portable_render_world_extent(320, 180, 9, 1, &width, &height, error, sizeof error));
    CHECK(!portable_render_world_extent(320, 180, 1, 0, &width, &height, error, sizeof error));
    CHECK(!portable_render_world_extent(INT32_MAX, 1, 4, 1, &width, &height, error, sizeof error));
    CHECK(width == old_width && height == old_height);
    return 0;
}

static int supported_billboard_views(void)
{
    PortableRenderInstance source;
    portable_render_instance_identity(&source, NULL);
    source.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_UP;
    source.transform[3] = -2;
    source.transform[7] = 4;
    source.transform[11] = 3;
    /* The road is 3Y + 4Z = 24; a nonunit supplied normal is legal. */
    source.transform[2] = 0;
    source.transform[6] = 6;
    source.transform[10] = 8;
    source.billboard_scale[0] = 2;
    source.billboard_scale[1] = .5;
    source.billboard_scale[2] = 3;
    const PortableRenderInstance before = source;
    const double rights[4][3] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, .6, .8}};
    for (unsigned camera = 0; camera < 4; ++camera)
    {
        PortableRenderView view = {.position = {100 * camera, -30, 8}};
        memcpy(view.right, rights[camera], sizeof view.right);
        const PortableRenderView view_before = view;
        PortableRenderInstance resolved;
        CHECK(portable_render_instance_for_view(&source, &view, &resolved));
        CHECK(resolved.flags == 0);
        PortableRenderInstance repeated;
        CHECK(portable_render_instance_for_view(&resolved, &view, &repeated));
        CHECK(!memcmp(&resolved, &repeated, sizeof resolved));
        CHECK(!memcmp(&view, &view_before, sizeof view));
        for (unsigned row = 0; row < 3; ++row)
            CHECK(resolved.transform[row * 4u + 3u] == source.transform[row * 4u + 3u]);
        CHECK(fabs(resolved.transform[2]) < 1e-12);
        CHECK(fabs(resolved.transform[6] - 1.8) < 1e-12);
        CHECK(fabs(resolved.transform[10] - 2.4) < 1e-12);
        /* Both tire endpoints lie on the same road plane at every yaw. */
        for (int end = -1; end <= 1; end += 2)
        {
            double y = resolved.transform[7] + end * resolved.transform[4];
            double z = resolved.transform[11] + end * resolved.transform[8];
            CHECK(fabs(3 * y + 4 * z - 24) < 1e-12);
        }
        double right_length = 0, forward_length = 0, dot = 0;
        for (unsigned row = 0; row < 3; ++row)
        {
            right_length += resolved.transform[row * 4u] * resolved.transform[row * 4u];
            forward_length += resolved.transform[row * 4u + 1u] * resolved.transform[row * 4u + 1u];
            dot += resolved.transform[row * 4u] * resolved.transform[row * 4u + 1u];
        }
        CHECK(fabs(right_length - 4) < 1e-12 && fabs(forward_length - .25) < 1e-12);
        CHECK(fabs(dot) < 1e-12);
        view.position[0] += 1000; /* Distance never changes the support frame. */
        PortableRenderInstance distant;
        CHECK(portable_render_instance_for_view(&source, &view, &distant));
        CHECK(!memcmp(&resolved, &distant, sizeof resolved));
    }
    CHECK(!memcmp(&source, &before, sizeof source));
    PortableRenderView view = {.right = {1, 0, 0}};
    PortableRenderInstance out = before, saved = out;
    const unsigned invalid_flags[] = {3, 4};
    for (unsigned i = 0; i < 2; ++i)
    {
        source.flags = invalid_flags[i];
        CHECK(!portable_render_instance_for_view(&source, &view, &out));
        CHECK(!memcmp(&out, &saved, sizeof out));
    }
    source = before;
    source.transform[6] = source.transform[10] = 0;
    CHECK(!portable_render_instance_for_view(&source, &view, &out));
    source = before;
    source.transform[2] = NAN;
    CHECK(!portable_render_instance_for_view(&source, &view, &out));
    source = before;
    source.billboard_scale[2] = 0;
    CHECK(!portable_render_instance_for_view(&source, &view, &out));
    source = before;
    view.right[2] = INFINITY;
    CHECK(!portable_render_instance_for_view(&source, &view, &out));
    CHECK(!memcmp(&out, &saved, sizeof out));
    source = before;
    CHECK(portable_render_instance_flip_vertical(&source, 2));
    CHECK(fabs(source.transform[7] - 7.6) < 1e-12);
    CHECK(fabs(source.transform[11] - 7.8) < 1e-12);
    CHECK(source.billboard_scale[2] == -3);
    CHECK(source.transform[6] == 6 && source.transform[10] == 8);
    return 0;
}

static int billboard_views(void)
{
    PortableRenderInstance source;
    portable_render_instance_identity(&source, NULL);
    source.transform[3] = 3;
    source.transform[7] = 4;
    source.transform[11] = 5;
    source.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
    source.billboard_scale[0] = 2;
    source.billboard_scale[1] = -3;
    source.billboard_scale[2] = .5;
    const PortableRenderInstance before = source;
    PortableRenderView view = {.right = {1, 0, 0}};
    const PortableRenderView view_before = view;
    PortableRenderInstance right, left;
    CHECK(portable_render_instance_for_view(&source, &view, &right));
    CHECK(right.transform[0] == 2 && right.transform[5] == -3 && right.transform[10] == .5);
    CHECK(right.transform[3] == 3 && right.transform[7] == 4 && right.transform[11] == 5);
    CHECK(!memcmp(&view, &view_before, sizeof view));
    view.right[0] = -1;
    CHECK(portable_render_instance_for_view(&source, &view, &left));
    CHECK(left.transform[0] == -2 && left.transform[5] == 3 && left.transform[10] == .5);
    CHECK(left.transform[3] == 3 && left.transform[7] == 4 && left.transform[11] == 5);
    CHECK(!memcmp(&source, &before, sizeof source));
    CHECK(portable_render_instance_for_view(&left, &view, &left));
    CHECK(left.transform[0] == -2 && left.transform[5] == 3);
    source.flags = 0;
    source.billboard_scale[0] = NAN; /* Inactive metadata does not affect ordinary instances. */
    CHECK(portable_render_instance_for_view(&source, &view, &right));
    CHECK(!memcmp(&source, &right, sizeof source));
    const PortableRenderInstance saved = right;
    source.flags = 4;
    CHECK(!portable_render_instance_for_view(&source, &view, &right));
    CHECK(!memcmp(&saved, &right, sizeof saved));
    source = before;
    source.billboard_scale[0] = 0;
    CHECK(!portable_render_instance_for_view(&source, &view, &right));
    source = before;
    view.right[0] = NAN;
    CHECK(!portable_render_instance_for_view(&source, &view, &right));
    CHECK(!memcmp(&saved, &right, sizeof saved));
    view.right[0] = 0;
    view.right[2] = 1;
    CHECK(portable_render_instance_for_view(&before, &view, &right));
    CHECK(right.transform[0] == 2 &&
          right.transform[5] == -3); /* Vertical view deterministic fallback. */
    Fixture fixture;
    fixture_init(&fixture);
    PortableRenderResources resources = {0};
    char error[128];
    CHECK(portable_render_resources_open_course(&resources, &fixture.course, error, sizeof error));
    source = before;
    source.mesh = &resources.mesh;
    PortableRenderFrame frame = {.instances = &source, .instance_count = 1, .view_count = 1};
    frame.views[0] = (PortableRenderView){.width = 16,
                                          .height = 16,
                                          .right = {1, 0, 0},
                                          .up = {0, 1, 0},
                                          .forward = {0, 0, 1},
                                          .projection = {8, 8, 8, 8, .1, 100}};
    CHECK(portable_render_frame_valid(&frame, 16, 16, error, sizeof error));
    source.flags = 4;
    CHECK(!portable_render_frame_valid(&frame, 16, 16, error, sizeof error));
    source.flags = 1;
    source.billboard_scale[1] = INFINITY;
    CHECK(!portable_render_frame_valid(&frame, 16, 16, error, sizeof error));
    source.billboard_scale[1] = -32769;
    CHECK(!portable_render_frame_valid(&frame, 16, 16, error, sizeof error));
    portable_render_resources_close(&resources);
    return 0;
}

static int anchored_vertical_flip(void)
{
    PortableRenderMesh mesh = {0};
    PortableRenderInstance source, flipped, oriented;
    portable_render_instance_identity(&source, &mesh);
    source.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
    source.billboard_scale[0] = source.billboard_scale[1] = source.billboard_scale[2] = .1;
    source.transform[11] = 2.4;
    source.hidden_views = 2;
    const PortableRenderView camera = {.right = {1, 0, 0}};
    flipped = source;
    CHECK(portable_render_instance_flip_vertical(&flipped, 28));
    /* x87 can retain extra precision for decimal constants; use the same
     * presentation tolerance as the transformed anchor bounds below. */
    CHECK(fabs(flipped.billboard_scale[2] + .1) < 1e-12 && flipped.hidden_views == 2);
    CHECK(flipped.mesh == source.mesh && flipped.tint_argb == source.tint_argb);
    CHECK(portable_render_instance_for_view(&flipped, &camera, &oriented));
    /* Stock anchored art spans local Z[-2,30]. Flip swaps endpoints while
     * retaining its world [2.2,5.4] bounds, rather than moving under the floor. */
    CHECK(fabs(oriented.transform[11] - 2 * oriented.transform[10] - 5.4) < 1e-12);
    CHECK(fabs(oriented.transform[11] + 30 * oriented.transform[10] - 2.2) < 1e-12);
    CHECK(portable_render_instance_flip_vertical(&flipped, 28));
    CHECK(fabs(flipped.transform[11] - source.transform[11]) < 1e-12);
    CHECK(flipped.billboard_scale[2] == source.billboard_scale[2]);
    /* Two camera-selected source cells can have different anchors. The host
     * copies an already-flipped front cell, undoes those bounds, then flips
     * the alternate cell about its own bounds without changing either owner. */
    const PortableRenderInstance original = source;
    PortableRenderMesh alternate_mesh = {0};
    PortableRenderInstance front = source;
    CHECK(portable_render_instance_flip_vertical(&front, 28));
    const PortableRenderInstance prepared_front = front;
    PortableRenderInstance alternate = front;
    alternate.mesh = &alternate_mesh;
    CHECK(portable_render_instance_flip_vertical(&alternate, 28));
    CHECK(portable_render_instance_flip_vertical(&alternate, 26));
    alternate.hidden_views = 1;
    CHECK(portable_render_instance_for_view(&alternate, &camera, &oriented));
    CHECK(fabs(oriented.transform[11] - 3 * oriented.transform[10] - 5.3) < 1e-12);
    CHECK(fabs(oriented.transform[11] + 29 * oriented.transform[10] - 2.1) < 1e-12);
    CHECK(oriented.mesh == &alternate_mesh && oriented.hidden_views == 1);
    CHECK(!memcmp(&source, &original, sizeof source) &&
          !memcmp(&front, &prepared_front, sizeof front));
    CHECK(portable_render_instance_for_view(&front, &camera, &oriented));
    CHECK(fabs(oriented.transform[11] - 2 * oriented.transform[10] - 5.4) < 1e-12);
    CHECK(fabs(oriented.transform[11] + 30 * oriented.transform[10] - 2.2) < 1e-12);
    flipped = source;
    CHECK(portable_render_instance_flip_vertical(&flipped, 16));
    CHECK(portable_render_instance_for_view(&flipped, &camera, &oriented));
    /* Original fixture feet Z0 and head Z16 retain world [2.4,4.0]. */
    CHECK(fabs(oriented.transform[11] - 4.0) < 1e-12);
    CHECK(fabs(oriented.transform[11] + 16 * oriented.transform[10] - 2.4) < 1e-12);
    flipped = source;
    flipped.billboard_scale[2] = -.1;
    CHECK(portable_render_instance_flip_vertical(&flipped, 28));
    CHECK(portable_render_instance_for_view(&flipped, &camera, &oriented));
    CHECK(fabs(oriented.transform[11] - 2 * oriented.transform[10] + .6) < 1e-12);
    CHECK(fabs(oriented.transform[11] + 30 * oriented.transform[10] - 2.6) < 1e-12);

    portable_render_instance_identity(&source, &mesh);
    source.transform[2] = .25;
    source.transform[6] = -.5;
    source.transform[10] = 0;
    source.transform[3] = 1;
    source.transform[7] = 2;
    source.transform[11] = 3;
    flipped = source;
    CHECK(portable_render_instance_flip_vertical(&flipped, 28));
    const double low[3] = {.5, 3, 3}, high[3] = {8.5, -13, 3};
    for (unsigned row = 0; row < 3; ++row)
    {
        CHECK(fabs(flipped.transform[row * 4 + 3] - 2 * flipped.transform[row * 4 + 2] -
                   high[row]) < 1e-12);
        CHECK(fabs(flipped.transform[row * 4 + 3] + 30 * flipped.transform[row * 4 + 2] -
                   low[row]) < 1e-12);
    }
    CHECK(portable_render_instance_flip_vertical(&flipped, 28));
    for (unsigned i = 0; i < 12; ++i)
        CHECK(flipped.transform[i] == source.transform[i]);
    CHECK(!portable_render_instance_flip_vertical(NULL, 28));
    for (unsigned i = 0; i < 8; ++i)
    {
        flipped = source;
        double sum = 28;
        switch (i)
        {
        case 0:
            sum = NAN;
            break;
        case 1:
            sum = INFINITY;
            break;
        case 2:
            sum = DBL_MAX;
            break;
        case 3:
            flipped.flags = 4;
            break;
        case 4:
            flipped.hidden_views = 16;
            break;
        case 5:
            flipped.transform[2] = NAN;
            break;
        case 6:
            flipped.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
            flipped.billboard_scale[0] = flipped.billboard_scale[1] = 1;
            flipped.billboard_scale[2] = 0;
            break;
        case 7:
            flipped.transform[3] = 4096;
            break;
        }
        const PortableRenderInstance before = flipped;
        CHECK(!portable_render_instance_flip_vertical(&flipped, sum));
        CHECK(!memcmp(&before, &flipped, sizeof before));
    }
    return 0;
}

static int directional_views(void)
{
    PortableRenderMesh mesh = {0};
    PortableRenderInstance source, left, right, turned;
    portable_render_instance_identity(&source, &mesh);
    source.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
    source.billboard_scale[0] = source.billboard_scale[1] = source.billboard_scale[2] = .1;
    source.transform[3] = 10;
    source.transform[7] = 20;
    source.hidden_views = 4;
    const PortableRenderInstance saved = source;
    const PortableRenderView a = {.right = {1, 0, 0}}, b = {.right = {-1, 0, 0}};
    CHECK(portable_render_instance_face_for_view(&source, &a, 32768, 0, 0, 0, 8, &right));
    CHECK(portable_render_instance_face_for_view(&source, &b, 32768, 0, 0, 0, 8, &left));
    CHECK(right.flags == 0 && left.flags == 0 && right.hidden_views == 4);
    CHECK(fabs(right.transform[0] + .1) < 1e-12 && fabs(left.transform[0] + .1) < 1e-12);
    /* Asymmetric anchored local X[-4,12]: reflection retains world bounds;
     * the opposite camera sees unreflected art with its own reversed basis. */
    CHECK(fabs(right.transform[3] - 4 * right.transform[0] - 11.2) < 1e-12);
    CHECK(fabs(right.transform[3] + 12 * right.transform[0] - 9.6) < 1e-12);
    CHECK(fabs(left.transform[3] - 4 * left.transform[0] - 10.4) < 1e-12);
    CHECK(fabs(left.transform[3] + 12 * left.transform[0] - 8.8) < 1e-12);
    CHECK(portable_render_instance_face_for_view(&source, &a, 32768, 0, 1, 0, 8, &turned));
    CHECK(fabs(turned.transform[0] - .1) < 1e-12 && turned.transform[3] == 10);
    /* Seagulls keep source heading0x4000 and reflect only for cameras on
     * its negative-X side (the source unsigned half-turn predicate).
     * Each camera gets its own copy; reversing one must not alter the other. */
    CHECK(portable_render_instance_face_for_view(&source, &a, 65536, 0, 1, 1, 8, &right));
    CHECK(portable_render_instance_face_for_view(&source, &b, 65536, 0, 1, 1, 8, &left));
    CHECK(fabs(right.transform[0] - .1) < 1e-12 && right.transform[3] == 10);
    CHECK(fabs(left.transform[0] - .1) < 1e-12 && fabs(left.transform[3] - 9.2) < 1e-12);
    CHECK(right.hidden_views == 4 && left.hidden_views == 4);
    const PortableRenderView perpendicular_a = {.right = {0, 1, 0}},
                             perpendicular_b = {.right = {0, -1, 0}};
    CHECK(portable_render_instance_face_for_view(&source, &perpendicular_a, 65536, 0, 1, 1, 8,
                                                 &right));
    CHECK(portable_render_instance_face_for_view(&source, &perpendicular_b, 65536, 0, 1, 1, 8,
                                                 &left));
    CHECK(fabs(right.transform[4] - .1) < 1e-12 && right.transform[7] == 20);
    CHECK(fabs(left.transform[4] - .1) < 1e-12 && fabs(left.transform[7] - 19.2) < 1e-12);
    CHECK(!portable_render_instance_face_for_view(&source, &a, 1, 0, 0, 2, 8, &turned));
    CHECK(!memcmp(&saved, &source, sizeof source));
    for (unsigned camera = 0; camera < 65536; ++camera)
    {
        const double yaw = camera * 6.28318530717958647692 / 65536.0;
        PortableRenderView pane = {.right = {cos(yaw), sin(yaw), 0}};
        if (!(camera & 0x3fff))
        {
            static const double xs[4] = {1, 0, -1, 0}, ys[4] = {0, 1, 0, -1};
            pane.right[0] = xs[camera >> 14];
            pane.right[1] = ys[camera >> 14];
        }
        unsigned side = 7;
        CHECK(portable_render_view_art_side(&pane, 0x4000, &side));
        CHECK(side == ((uint16_t)(0x4000 - camera) > 32767u));
    }
    unsigned side = 7;
    PortableRenderView invalid = {.right = {NAN, 1, 0}};
    CHECK(!portable_render_view_art_side(&invalid, 0x4000, &side) && side == 7);
    invalid.right[0] = 0;
    invalid.right[1] = 0;
    CHECK(portable_render_view_art_side(&invalid, 0x4000, &side) && side == 0);
    const PortableRenderInstance before = turned;
    CHECK(!portable_render_instance_face_for_view(&source, &a, 0, 0, 0, 0, 8, &turned));
    CHECK(!portable_render_instance_face_for_view(&source, &a, 1, 0, 2, 0, 8, &turned));
    CHECK(!portable_render_instance_face_for_view(&source, &a, 1, 0, 0, 0, NAN, &turned));
    CHECK(!memcmp(&before, &turned, sizeof turned));
    return 0;
}

static int clip_intervals(void)
{
    PortableRenderMesh mesh = {0};
    PortableRenderInstance upper, lower;
    portable_render_instance_identity(&upper, &mesh);
    lower = upper;
    PortableRenderView camera = {.height = 1};
    size_t first = 7, count = 9;
    CHECK(portable_render_instance_clip_rows(&upper, &camera, &first, &count));
    CHECK(first == 0 && count == 1);
    upper.clip_y_end = lower.clip_y_begin = 24;
    upper.clip_y_extent = lower.clip_y_end = lower.clip_y_extent = 160;
    const size_t heights[] = {1, 2, 7, 159, 160, 161, 1080, 16384};
    for (unsigned i = 0; i < sizeof heights / sizeof heights[0]; ++i)
    {
        camera.height = heights[i];
        size_t second_first = 0, second_count = 0;
        CHECK(portable_render_instance_clip_rows(&upper, &camera, &first, &count));
        CHECK(portable_render_instance_clip_rows(&lower, &camera, &second_first, &second_count));
        CHECK(first == 0 && count == (24u * camera.height + 159u) / 160u && second_first == count &&
              second_count + count == camera.height);
    }
    for (unsigned bad = 0; bad < 6; ++bad)
    {
        PortableRenderInstance invalid = upper;
        PortableRenderView view_copy = camera;
        switch (bad)
        {
        case 0:
            invalid.clip_y_begin = 24;
            break;
        case 1:
            invalid.clip_y_end = 161;
            break;
        case 2:
            invalid.clip_y_extent = 65537;
            break;
        case 3:
            invalid.clip_y_extent = 0;
            break;
        case 4:
            view_copy.height = 0;
            break;
        default:
            view_copy.height = (size_t)INT32_MAX + 1u;
            break;
        }
        first = 7;
        count = 9;
        CHECK(!portable_render_instance_clip_rows(&invalid, &view_copy, &first, &count));
        CHECK(first == 7 && count == 9);
    }
    return 0;
}

typedef struct
{
    const PortableRenderMesh *uploaded;
    unsigned calls;
} GeometryProof;
static int uploaded_geometry(const void *context, const PortableRenderMesh *mesh)
{
    GeometryProof *proof = (GeometryProof *)context;
    ++proof->calls;
    const PortableRenderMesh *old = proof->uploaded;
    return mesh->vertices == old->vertices && mesh->indices == old->indices &&
           mesh->vertex_count == old->vertex_count && mesh->index_count == old->index_count;
}
static int immutable_geometry_validation(void)
{
    PortableRenderVertex vertices[3] = {0};
    uint32_t indices[3] = {0, 1, 2};
    PortableRenderMaterial material = {.argb = UINT32_MAX};
    PortableRenderRun run = {0, 3, 0};
    PortableRenderMesh mesh = {.vertices = vertices,
                               .indices = indices,
                               .materials = &material,
                               .runs = &run,
                               .vertex_count = 3,
                               .index_count = 3,
                               .material_count = 1,
                               .run_count = 1};
    PortableRenderInstance instance;
    portable_render_instance_identity(&instance, &mesh);
    PortableRenderFrame frame = {.instances = &instance, .instance_count = 1, .view_count = 1};
    frame.views[0] = view(0, 0, 16, 16, 0);
    char error[160];
    CHECK(portable_render_frame_valid(&frame, 16, 16, error, sizeof error));
    GeometryProof proof = {&mesh, 0};
    CHECK(portable_render_frame_valid_geometry(&frame, 16, 16, uploaded_geometry, &proof, error,
                                               sizeof error));
    CHECK(proof.calls == 1);
    material.flags = 48; /* mask and blend are exclusive */
    CHECK(!portable_render_frame_valid_geometry(&frame, 16, 16, uploaded_geometry, &proof, error,
                                                sizeof error));
    material.flags = 0;
    run.material = 1;
    CHECK(!portable_render_frame_valid_geometry(&frame, 16, 16, uploaded_geometry, &proof, error,
                                                sizeof error));
    run.material = 0;
    instance.transform[0] = NAN;
    CHECK(!portable_render_frame_valid_geometry(&frame, 16, 16, uploaded_geometry, &proof, error,
                                                sizeof error));
    instance.transform[0] = 1;
    /* The proof represents an immutable uploaded copy. The generic validator
     * deliberately continues to inspect original arrays on every invocation. */
    vertices[0].position[0] = NAN;
    CHECK(!portable_render_frame_valid(&frame, 16, 16, error, sizeof error));
    CHECK(portable_render_frame_valid_geometry(&frame, 16, 16, uploaded_geometry, &proof, error,
                                               sizeof error));
    CHECK(!portable_render_frame_valid_geometry(&frame, 16, 16, NULL, NULL, error, sizeof error));
    PortableRenderVertex other[3] = {0};
    other[1].uv[0] = NAN;
    PortableRenderMesh unknown = mesh;
    unknown.vertices = other;
    instance.mesh = &unknown;
    CHECK(!portable_render_frame_valid_geometry(&frame, 16, 16, uploaded_geometry, &proof, error,
                                                sizeof error));
    return 0;
}

/* Lit materials get a mesh-local light and premultiplied terms; fog needs the
 * material flag and a coloured, ordered range; frames without light stay unlit. */
static int environment_paint(void)
{
    PortableRenderFrame frame;
    PortableRenderView view;
    PortableRenderInstance instance;
    PortableRenderMaterial material;
    memset(&frame, 0, sizeof frame);
    memset(&view, 0, sizeof view);
    memset(&material, 0, sizeof material);
    portable_render_instance_identity(&instance, NULL);
    /* Scale 2 and a quarter turn about Z: world +X is local -Y. */
    memset(instance.transform, 0, sizeof instance.transform);
    instance.transform[1] = -2.0;
    instance.transform[4] = 2.0;
    instance.transform[10] = 2.0;
    material.argb = 0xff808080u;
    material.ambient_argb = 0xff404040u;
    material.emission_argb = 0xff202020u;
    material.flags = PORTABLE_RENDER_MATERIAL_LIT | PORTABLE_RENDER_MATERIAL_FOG;
    material.filter = PORTABLE_RENDER_FILTER_THREE_POINT;
    float out[20];
    portable_render_paint_environment(&frame, &view, &instance, &material, 0, out);
    CHECK(out[3] == 0.0f && out[15] == 0.0f && out[19] == 2.0f);
    frame.environment.light_direction[0] = 1.0;
    frame.environment.light_argb = 0xffffffffu;
    instance.light_argb = 0xff0000ffu; /* the surface light wins */
    frame.environment.fog_argb = 0x80102030u;
    frame.environment.fog_near = 10.0;
    frame.environment.fog_far = 100.0;
    frame.environment.fog_curve = PORTABLE_RENDER_FOG_EASE_IN;
    portable_render_paint_environment(&frame, &view, &instance, &material, 0, out);
    CHECK(out[3] == 1.0f && fabsf(out[0]) < 1e-6f && fabsf(out[1] + 1.0f) < 1e-6f && fabsf(out[2]) < 1e-6f);
    CHECK(fabsf(out[4] - 32.0f / 255.0f) < 1e-6f && fabsf(out[6] - (32.0f + 64.0f) / 255.0f) < 1e-6f);
    CHECK(out[8] == 0.0f && fabsf(out[10] - 128.0f / 255.0f) < 1e-6f);
    CHECK(fabsf(out[12] - 16.0f / 255.0f) < 1e-6f && fabsf(out[15] - 128.0f / 255.0f) < 1e-6f &&
          out[16] == 10.0f && out[17] == 100.0f && out[18] == 1.0f);
    frame.environment.fog_far = 5.0;
    portable_render_paint_environment(&frame, &view, &instance, &material, 1, out);
    CHECK(out[3] == 0.0f && out[15] == 0.0f);
    portable_render_paint_environment(&frame, &view, &instance, &material, 0, out);
    CHECK(out[3] == 1.0f && out[15] == 0.0f);
    return 0;
}

int main(void)
{
    CHECK(immutable_geometry_validation() == 0);
    CHECK(clip_intervals() == 0);
    CHECK(anchored_vertical_flip() == 0);
    CHECK(directional_views() == 0);
    CHECK(billboard_views() == 0);
    CHECK(supported_billboard_views() == 0);
    CHECK(world_resolution() == 0);
    CHECK(resources_and_prior_cpu() == 0);
    CHECK(split_depth_cutout_and_ownership() == 0);
    CHECK(physical_scale_and_stride() == 0);
    CHECK(environment_paint() == 0);
    return 0;
}
