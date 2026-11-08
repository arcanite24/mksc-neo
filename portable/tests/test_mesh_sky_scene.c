#include "mesh_sky_scene.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(v)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(v))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "sky scene:%d: %s\n", __LINE__, #v);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static int near(double a, double b)
{
    return fabs(a - b) < 1e-8;
}

static int racer_owned_scene(PortableMeshSkyConfig config)
{
    config.rules.controller = PORTABLE_SKY_MOTION_SPEED_PAN;
    config.progress.enabled = 1;
    config.progress.stage_count = 3;
    config.progress.source_raw[1] = 40;
    config.progress.source_raw[2] = 90;
    config.rules.split_y = 24;
    config.rules.split_layer = 1;
    config.rules.split_x_bias = 19;
    config.rules.split_yaw_shift = 7;
    PortableMeshSky world;
    CHECK(portable_mesh_sky_init(&config, &world));
    const PortableMeshSkyProgressInput poses[2] = {{0, 0, 1}, {0, 2, 1}};
    for (unsigned tick = 0; tick < 34; ++tick)
        CHECK(portable_mesh_sky_advance_racers(&config, &world, &world, poses, 2, 1));
    CHECK(world.racer_count == 2 && world.motion.phase == 0 && world.other_motion[0].phase == 1 &&
          world.other_motion[0].elapsed == 33);
    const PortableMeshSky accepted = world;
    PortableRenderMesh mesh = {0};
    PortableRenderInstance base, first, second;
    portable_render_instance_identity(&base, &mesh);
    base.transform[11] = 4;
    base.uv_offset[0] = .125;
    base.uv_offset[1] = .375;
    const PortableRenderInstance original = base;
    const PortableRenderView view = {
        .position = {17, -29, 5}, .right = {1, 0, 0}, .forward = {0, -1, 0}};
    /* Identical cameras exclude yaw as the reason these panes differ. The
     * source phase1 publishes elapsed32 before incrementing its clock to33:
     * the middle layer pans four texels and rises one source pixel. */
    CHECK(portable_mesh_sky_scene_instance_racer(&config, &world, 0, 1, &base, &view, 0, &first));
    CHECK(portable_mesh_sky_scene_instance_racer(&config, &world, 1, 1, &base, &view, 0, &second));
    CHECK(first.mesh == &mesh && second.mesh == &mesh && first.hidden_views == 14 &&
          second.hidden_views == 14);
    CHECK(near(first.uv_offset[0], .125) && near(first.transform[11], 9));
    CHECK(near(second.uv_offset[0], .125 + 4.0 / config.logical_width[1]) &&
          near(second.transform[11], 9 + config.radius_q16[1] / 65536.0 / 136));
    CHECK(near(first.uv_offset[1], .375) && near(second.uv_offset[1], .375));
    for (unsigned i = 0; i < 11; ++i)
        CHECK(near(first.transform[i], second.transform[i]));
    CHECK(!memcmp(&world, &accepted, sizeof world) && !memcmp(&base, &original, sizeof base));
    PortableRenderInstance first_parts[2], second_parts[2];
    unsigned first_count = 0, second_count = 0;
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 0, 1, &base, &view, 0,
                                                  first_parts, &first_count));
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 1, 1, &base, &view, 1,
                                                  second_parts, &second_count));
    CHECK(first_count == 2 && second_count == 2);
    CHECK(near(first_parts[0].uv_offset[0], first.uv_offset[0]) &&
          near(second_parts[0].uv_offset[0], second.uv_offset[0]));
    CHECK(near(first_parts[1].uv_offset[0], .125 + 19.0 / config.logical_width[1]) &&
          near(second_parts[1].uv_offset[0], first_parts[1].uv_offset[0]));
    CHECK(first_parts[0].hidden_views == 14 && first_parts[1].hidden_views == 14 &&
          second_parts[0].hidden_views == 13 && second_parts[1].hidden_views == 13);
    CHECK(near(first_parts[1].transform[11], first.transform[11]) &&
          near(second_parts[1].transform[11], second.transform[11]));
    CHECK(first_parts[0].clip_y_begin == 0 && first_parts[0].clip_y_end == 24 &&
          first_parts[0].clip_y_extent == 160 && first_parts[1].clip_y_begin == 24 &&
          first_parts[1].clip_y_end == 160 && first_parts[1].clip_y_extent == 160);
    CHECK(!memcmp(&world, &accepted, sizeof world) && !memcmp(&base, &original, sizeof base));
    const unsigned invalid[] = {2, 7, 8, UINT32_MAX};
    const PortableRenderInstance saved = second;
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i)
    {
        CHECK(!portable_mesh_sky_scene_instance_racer(&config, &world, invalid[i], 1, &base, &view,
                                                      0, &second));
        CHECK(!memcmp(&second, &saved, sizeof second) && !memcmp(&world, &accepted, sizeof world));
    }
    return 0;
}

static int horizontal_parts(PortableMeshSkyConfig config)
{
    config.rules.controller = PORTABLE_SKY_MOTION_STATIC;
    config.rules.split_y = 24;
    config.rules.split_layer = 1;
    config.rules.split_yaw_shift = 7;
    config.rules.split_x_bias = 19;
    config.rules.x_bias[1] = 5;
    PortableMeshSky world;
    CHECK(portable_mesh_sky_init(&config, &world));
    const PortableMeshSky accepted = world;
    PortableRenderMesh mesh = {0};
    PortableRenderInstance base, parts[2];
    portable_render_instance_identity(&base, &mesh);
    base.transform[11] = 4;
    base.uv_offset[0] = .125;
    base.uv_offset[1] = .375;
    const PortableRenderInstance original = base;
    const PortableRenderView view = {.x = 803,
                                     .y = 101,
                                     .width = 321,
                                     .height = 239,
                                     .position = {17, -29, 5},
                                     .right = {0, 1, 0},
                                     .forward = {1, 0, 0}};
    const PortableRenderView original_view = view;
    unsigned count = 91;
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 0, 1, &base, &view, 2, parts,
                                                  &count));
    CHECK(count == 2 && near(parts[0].uv_offset[0], .625) &&
          near(parts[1].uv_offset[0], .125 + (128.0 + 19 - 5) / 256));
    CHECK(near(parts[0].transform[11], 9) && near(parts[1].transform[11], 9) &&
          near(parts[0].uv_offset[1], .375) && near(parts[1].uv_offset[1], .375));
    CHECK(parts[0].hidden_views == 11 && parts[1].hidden_views == 11 &&
          parts[0].clip_y_begin == 0 && parts[0].clip_y_end == 24 &&
          parts[0].clip_y_extent == 160 && parts[1].clip_y_begin == 24 &&
          parts[1].clip_y_end == 160 && parts[1].clip_y_extent == 160);
    CHECK(!memcmp(&base, &original, sizeof base) && !memcmp(&world, &accepted, sizeof world) &&
          !memcmp(&view, &original_view, sizeof view));
    /* The exact source boundary belongs to the lower interval, independent
     * of this pane's physical origin or odd pixel dimensions. */
    CHECK(parts[0].clip_y_end == parts[1].clip_y_begin);
    PortableRenderInstance ordinary;
    CHECK(
        portable_mesh_sky_scene_instance_racer(&config, &world, 0, 0, &base, &view, 2, &ordinary));
    memset(parts, 0xa5, sizeof parts);
    const PortableRenderInstance untouched = parts[1];
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 0, 0, &base, &view, 2, parts,
                                                  &count));
    CHECK(count == 1 && !memcmp(&parts[0], &ordinary, sizeof ordinary) &&
          !memcmp(&parts[1], &untouched, sizeof untouched));
    config.rules.split_y = -1;
    CHECK(portable_mesh_sky_init(&config, &world));
    CHECK(
        portable_mesh_sky_scene_instance_racer(&config, &world, 0, 1, &base, &view, 2, &ordinary));
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 0, 1, &base, &view, 2, parts,
                                                  &count));
    CHECK(count == 1 && !memcmp(&parts[0], &ordinary, sizeof ordinary));
    config.rules.split_y = 0;
    CHECK(portable_mesh_sky_init(&config, &world));
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 0, 1, &base, &view, 2, parts,
                                                  &count));
    CHECK(count == 1 && near(parts[0].uv_offset[0], .125 + (128.0 + 19 - 5) / 256) &&
          !parts[0].clip_y_begin && !parts[0].clip_y_end && !parts[0].clip_y_extent);
    config.rules.split_y = 159;
    CHECK(portable_mesh_sky_init(&config, &world));
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 0, 1, &base, &view, 2, parts,
                                                  &count));
    CHECK(count == 2 && parts[0].clip_y_end == 159 && parts[1].clip_y_begin == 159 &&
          parts[1].clip_y_end == 160 && parts[1].clip_y_extent == 160);
    /* Aliasing source with the first output must retain the same result. */
    const PortableRenderInstance expected[2] = {parts[0], parts[1]};
    parts[0] = base;
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 0, 1, &parts[0], &view, 2, parts,
                                                  &count));
    CHECK(count == 2 && !memcmp(parts, expected, sizeof parts));
    return 0;
}
static int vertical_parts(PortableMeshSkyConfig config)
{
    config.rules.controller = PORTABLE_SKY_MOTION_SUNSET;
    config.progress =
        (PortableMeshSkyProgressConfig){.enabled = 1, .stage_count = 2, .source_raw = {0, 90}};
    static const uint8_t object_seed[256] = {0}, indices[1] = {255};
    config.palette = (PortableMeshPaletteConfig){.enabled = 1,
                                                 .profile_id = 55,
                                                 .revision = 1,
                                                 .binding_count = 1,
                                                 .variant_count = 1,
                                                 .index_bytes = 1,
                                                 .data_size = 256,
                                                 .lane_count = 1,
                                                 .frame_count = 64,
                                                 .sunset_lane = 0,
                                                 .timer_lane = UINT16_MAX,
                                                 .data = object_seed,
                                                 .indices = indices,
                                                 .initial_snapshot = {.initialized = 1}};
    config.palette.bindings[0] = (PortableMeshPaletteBinding){97, 100, 0, 1, 1, 0, 1};
    config.palette.lanes[0] = (PortableAmbientLane){.stage = PORTABLE_AMBIENT_PRE_PROFILE,
                                                    .controller = PORTABLE_AMBIENT_SUNSET,
                                                    .frame_count = 64};
    for (unsigned i = 0; i < 64; ++i)
        config.palette.frames[i].duration = 1;
    PortableMeshSky world;
    CHECK(portable_mesh_sky_init(&config, &world));
    PortableMeshSkyProgressInput poses[2] = {{0, 0, 1}, {1, 0, 1}};
    unsigned budget = 0;
    while (world.motion.phase != 2 && budget++ < 150)
        CHECK(portable_mesh_sky_advance_racers(&config, &world, &world, poses, 2, 1));
    CHECK(world.motion.phase == 2);
    poses[1].completed_laps = 1;
    budget = 0;
    while (world.motion.phase != 5 && budget++ < 300)
        CHECK(portable_mesh_sky_advance_racers(&config, &world, &world, poses, 2, 1));
    CHECK(world.motion.phase == 5);
    for (unsigned i = 0; i < 16; ++i)
        CHECK(portable_mesh_sky_advance_racers(&config, &world, &world, poses, 2, 1));
    CHECK(world.motion.elapsed == 16 && world.motion.snapshot.split_vertical &&
          world.motion.snapshot.scroll_y_q16[2] != 46 * 65536);
    const PortableMeshSky accepted = world;
    const uint64_t hash = portable_mesh_sky_checksum(&config, &world);
    PortableRenderMesh mesh = {0};
    PortableRenderInstance base, parts[2];
    portable_render_instance_identity(&base, &mesh);
    base.transform[11] = 4;
    base.uv_offset[0] = .125;
    base.uv_offset[1] = .375;
    const PortableRenderInstance original = base;
    const PortableRenderView view = {.x = 641,
                                     .y = 17,
                                     .width = 319,
                                     .height = 181,
                                     .position = {17, -29, 5},
                                     .right = {1, 0, 0},
                                     .forward = {0, -1, 0}};
    unsigned count = 97;
    CHECK(portable_mesh_sky_scene_instances_racer(&config, &world, 1, 2, &base, &view, 3, parts,
                                                  &count));
    PortableRenderInstance ordinary;
    CHECK(
        portable_mesh_sky_scene_instance_racer(&config, &world, 1, 2, &base, &view, 3, &ordinary));
    CHECK(count == 2 && near(parts[0].transform[11], ordinary.transform[11]) &&
          near(parts[1].transform[11], 9 + 8 * (config.radius_q16[2] / 65536.0) / 136));
    CHECK(!near(parts[0].transform[11], parts[1].transform[11]) &&
          near(parts[0].uv_offset[0], parts[1].uv_offset[0]) &&
          near(parts[0].uv_offset[1], parts[1].uv_offset[1]));
    CHECK(parts[0].clip_y_begin == 0 && parts[0].clip_y_end == 24 &&
          parts[0].clip_y_extent == 160 && parts[1].clip_y_begin == 24 &&
          parts[1].clip_y_end == 160 && parts[1].clip_y_extent == 160 &&
          parts[0].hidden_views == 7 && parts[1].hidden_views == 7);
    CHECK(hash == portable_mesh_sky_checksum(&config, &world) &&
          !memcmp(&world, &accepted, sizeof world) && !memcmp(&base, &original, sizeof base));
    /* Include failures after the first part is valid, such as the lower
     * vertical shift exceeding the finite world coordinate bound. */
    for (unsigned bad = 0; bad < 15; ++bad)
    {
        PortableRenderInstance source = base;
        PortableRenderView camera = view;
        PortableMeshSky snapshot = world;
        unsigned racer = 1, layer = 2, pane = 3;
        switch (bad)
        {
        case 0:
            source.clip_y_extent = 160;
            break;
        case 1:
            source.clip_y_begin = 1;
            break;
        case 2:
            source.clip_y_end = 24;
            break;
        case 3:
            source.mesh = NULL;
            break;
        case 4:
            source.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
            break;
        case 5:
            source.transform[4] = NAN;
            break;
        case 6:
            source.uv_offset[0] = NAN;
            break;
        case 7:
            camera.forward[0] = INFINITY;
            break;
        case 8:
            camera.forward[0] = camera.forward[1] = camera.right[0] = camera.right[1] = 0;
            break;
        case 9:
            racer = 2;
            break;
        case 10:
            layer = 3;
            break;
        case 11:
            pane = 4;
            break;
        case 12:
            snapshot.revision++;
            break;
        case 13:
            source.hidden_views = 16;
            break;
        default:
            source.transform[11] = 32720;
            break;
        }
        memset(parts, 0xa5, sizeof parts);
        const PortableRenderInstance saved_parts[2] = {parts[0], parts[1]};
        count = 97;
        CHECK(!portable_mesh_sky_scene_instances_racer(&config, &snapshot, racer, layer, &source,
                                                       &camera, pane, parts, &count));
        CHECK(count == 97 && !memcmp(parts, saved_parts, sizeof parts));
    }
    memset(parts, 0xa5, sizeof parts);
    const PortableRenderInstance saved[2] = {parts[0], parts[1]};
    CHECK(!portable_mesh_sky_scene_instances_racer(&config, &world, 1, 2, &base, &view, 3, parts,
                                                   NULL));
    CHECK(!memcmp(parts, saved, sizeof parts));
    for (unsigned absent = 0; absent < 4; ++absent)
    {
        count = 97;
        CHECK(!portable_mesh_sky_scene_instances_racer(
            absent == 0 ? NULL : &config, absent == 1 ? NULL : &world, 1, 2,
            absent == 2 ? NULL : &base, absent == 3 ? NULL : &view, 3, parts, &count));
        CHECK(count == 97 && !memcmp(parts, saved, sizeof parts));
    }
    count = 97;
    CHECK(!portable_mesh_sky_scene_instances_racer(&config, &world, 1, 2, &base, &view, 3, NULL,
                                                   &count));
    CHECK(count == 97 && !memcmp(&world, &accepted, sizeof world) &&
          !memcmp(&base, &original, sizeof base));
    return 0;
}

int main(void)
{
    PortableMeshSkyConfig config = {.enabled = 1,
                                    .profile_id = 9,
                                    .revision = 1,
                                    .render_mask = 7,
                                    .logical_width = {256, 256, 512},
                                    .radius_q16 = {67108864, 67633152, 68157440},
                                    .rules = {.controller = 1,
                                              .yaw_shift = {7, 7, 8},
                                              .layer_mask = 7,
                                              .split_y = -1,
                                              .x_bias = {-64, 0, 31},
                                              .y_bias = {38, 38, 38},
                                              .layer_source = {0, 1, 2}}};
    for (unsigned i = 0; i < 1024; ++i)
        config.sine[i] = (int32_t)lround(sin(i * 6.28318530717958647693 / 1024) * 32768);
    CHECK(!racer_owned_scene(config));
    CHECK(!horizontal_parts(config));
    CHECK(!vertical_parts(config));
    PortableMeshSky world;
    CHECK(portable_mesh_sky_init(&config, &world));
    PortableRenderMesh mesh = {0};
    PortableRenderInstance base, out;
    portable_render_instance_identity(&base, &mesh);
    base.transform[3] = 200;
    base.transform[7] = -80;
    base.transform[11] = 4;
    const PortableRenderInstance original = base;
    const uint64_t initial = portable_mesh_sky_checksum(&config, &world);
    PortableRenderView view = {.position = {17, -29, 5}, .right = {1, 0, 0}, .forward = {0, -1, 0}};
    const PortableRenderView original_view = view;
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &view, 0, &out));
    CHECK(out.mesh == &mesh && out.hidden_views == 14 && near(out.uv_offset[0], .0625));
    CHECK(near(out.transform[3], 17) && near(out.transform[7], -29) && near(out.transform[11], 9));
    const PortableRenderInstance front = out;
    /* Quarter-turn yaw: exact source128 texels, with a counterposed cylinder
     * so sampling does not acquire a second geometric quarter turn. */
    view.forward[0] = 1;
    view.forward[1] = 0;
    view.right[0] = 0;
    view.right[1] = 1;
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &view, 1, &out));
    CHECK(out.hidden_views == 13 && near(out.uv_offset[0], front.uv_offset[0] + .5));
    CHECK(near(out.transform[0], 0) && near(out.transform[1], -1) && near(out.transform[4], 1));
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 2, &base, &view, 3, &out));
    CHECK(out.hidden_views == 7 && near(out.uv_offset[0], .125));
    /* Pure camera translation/hop changes no relative panorama geometry. */
    view = original_view;
    view.position[0] += 11;
    view.position[1] -= 23;
    view.position[2] += 2.5;
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &view, 0, &out));
    CHECK(near(out.transform[3] - front.transform[3], 11) &&
          near(out.transform[7] - front.transform[7], -23) &&
          near(out.transform[11] - front.transform[11], 2.5) &&
          near(out.uv_offset[0], front.uv_offset[0]));
    for (unsigned i = 0; i < 64; ++i)
        CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &original_view, 0, &out));
    CHECK(!near(out.uv_offset[0], front.uv_offset[0]));
    const uint64_t advanced = portable_mesh_sky_checksum(&config, &world);
    for (unsigned i = 0; i < 20; ++i)
        CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &original_view, i % 4,
                                               &out));
    CHECK(advanced == portable_mesh_sky_checksum(&config, &world) && advanced != initial);
    CHECK(!memcmp(&base, &original, sizeof base));
    CHECK(portable_mesh_sky_init(&config, &world));
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &original_view, 0, &out));
    CHECK(!memcmp(&out, &front, sizeof out));
    /* Rising layers move the cropped mesh, never V-clamp the texture. */
    config.rules.controller = 3;
    config.rules.y_bias[0] = 352;
    config.entry_delay = 1;
    CHECK(portable_mesh_sky_init(&config, &world));
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &original_view, 0, &out));
    const double start_z = out.transform[11];
    for (unsigned i = 0; i < 400; ++i)
        CHECK(portable_mesh_sky_advance(&config, &world, &world));
    CHECK(portable_mesh_sky_scene_instance(&config, &world, 0, &base, &original_view, 0, &out));
    CHECK(!near(out.transform[11], start_z) && out.uv_offset[1] == 0);
    /* Every rejected descriptor preserves the preceding valid presentation. */
    const PortableRenderInstance saved = out;
    for (unsigned bad = 0; bad < 9; ++bad)
    {
        PortableRenderInstance candidate = base;
        PortableRenderView camera = original_view;
        PortableMeshSkyConfig rules = config;
        unsigned layer = 0, pane = 0;
        switch (bad)
        {
        case 0:
            candidate.transform[4] = NAN;
            break;
        case 1:
            camera.forward[0] = INFINITY;
            break;
        case 2:
            candidate.flags = PORTABLE_RENDER_INSTANCE_BILLBOARD_Z;
            break;
        case 3:
            candidate.mesh = NULL;
            break;
        case 4:
            pane = 4;
            break;
        case 5:
            layer = 3;
            break;
        case 6:
            rules.logical_width[0] = 0;
            break;
        case 7:
            candidate.uv_offset[0] = NAN;
            break;
        default:
            camera.position[2] = 32768;
            candidate.transform[11] = 32768;
            break;
        }
        CHECK(!portable_mesh_sky_scene_instance(&rules, &world, layer, &candidate, &camera, pane,
                                                &out));
        CHECK(!memcmp(&out, &saved, sizeof out));
    }
    puts("mesh sky scene: PASS independent yaw immutable clock hop rise rollback");
    return 0;
}
