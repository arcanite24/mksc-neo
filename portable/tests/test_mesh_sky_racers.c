#include "mesh_sky.h"
#include "mesh_sky_progress.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

#define CHECK(value)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(value))                                                                              \
        {                                                                                          \
            fprintf(stderr, "sky racers:%d: %s\n", __LINE__, #value);                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

/* Generated mathematical fixture, independent of owner-ROM tables. */
static int32_t sine[1024];

static PortableMeshSkyConfig fixture(unsigned controller)
{
    PortableMeshSkyConfig c = {.enabled = 1, .profile_id = 41, .revision = 7, .render_mask = 7};
    c.rules = (PortableSkyMotionRules){.controller = (uint8_t)controller,
                                       .yaw_shift = {7, 7, 8},
                                       .layer_mask = 7,
                                       .x_bias = {3, -96, 11},
                                       .y_bias = {38, 32, 38},
                                       .split_y = -1,
                                       .layer_source = {0, 1, 2}};
    c.progress = (PortableMeshSkyProgressConfig){
        .enabled = 1, .stage_count = 4, .source_raw = {0, 7, 7, 92}};
    for (unsigned i = 0; i < 3; ++i)
    {
        c.logical_width[i] = 256;
        c.radius_q16[i] = 1024 * 65536;
    }
    memcpy(c.sine, sine, sizeof sine);
    return c;
}
static const PortableSkyMotion *motion(const PortableMeshSky *w, unsigned racer)
{
    return racer ? &w->other_motion[racer - 1] : &w->motion;
}
/* Compare with the unchanged source controller, rather than duplicating its
 * transition or pan formulas in the expected implementation. */
static int step(const PortableMeshSkyConfig *c, PortableMeshSky *w,
                const PortableMeshSkyProgressInput *poses, unsigned count, unsigned leader)
{
    const PortableMeshSky before = *w;
    CHECK(portable_mesh_sky_advance_racers(c, w, &before, poses, count, leader));
    CHECK(w->clock == before.clock + 1 && w->racer_count == count);
    for (unsigned i = 0; i < count; ++i)
    {
        PortableSkyMotion expected = *motion(&before, i);
        const PortableSkyMotionInputs in = {.clock = w->clock,
                                            .selected_progress = w->progress[i].high_water,
                                            .leader_progress = w->progress[leader].high_water,
                                            .leader_route_index = w->progress[leader].route_index,
                                            .leader_route_wrap = w->progress[leader].route_wrap,
                                            .scene_flags_7fe = 256,
                                            .precise_vertical_offset = 1};
        CHECK(portable_sky_motion_tick(&expected, &c->rules, &in, c->sine, 1024) == 1);
        CHECK(!memcmp(motion(w, i), &expected, sizeof expected));
    }
    if (c->palette.enabled)
    {
        PortableAmbientState expected = before.palette.ambient;
        const PortableMeshPaletteConfig *p = &c->palette;
        const PortableAmbientRules rules = {.lanes = p->lanes,
                                            .frames = p->frames,
                                            .patches = p->patches,
                                            .init_patches = p->initial,
                                            .data = p->data,
                                            .lane_count = p->lane_count,
                                            .frame_count = p->frame_count,
                                            .patch_count = p->patch_count,
                                            .init_patch_count = p->init_patch_count,
                                            .data_size = p->data_size,
                                            .object_seed_offset = p->object_seed_offset};
        const PortableAmbientInputs in = {.clock = w->clock,
                                          .selected_progress = w->progress[0].high_water,
                                          .leader_progress = w->progress[leader].high_water,
                                          .leader_route_wrap = w->progress[leader].route_wrap,
                                          .sky_phase_before = before.motion.phase,
                                          .sky_phase_after = w->motion.phase,
                                          .sky_elapsed_before = before.motion.elapsed,
                                          .sky_counter_before = before.motion.transition_counter};
        CHECK(portable_ambient_tick(&expected, &rules, PORTABLE_AMBIENT_PRE_PROFILE, &in, c->sine,
                                    1024) == 1);
        CHECK(portable_ambient_tick(&expected, &rules, PORTABLE_AMBIENT_POST_DRIVER, &in, c->sine,
                                    1024) == 1);
        CHECK(!memcmp(&w->palette.ambient, &expected, sizeof expected));
    }
    for (unsigned i = count; i < 8; ++i)
    {
        CHECK(!memcmp(motion(w, i), motion(&before, i), sizeof(PortableSkyMotion)));
        CHECK(!memcmp(&w->progress[i], &before.progress[i], sizeof w->progress[i]));
    }
    return 0;
}
static int snapshot_oracle(const PortableMeshSkyConfig *c, const PortableMeshSky *w, unsigned racer)
{
    const PortableMeshSky saved = *w;
    const uint16_t headings[] = {0, 1, 16384, 32767, 32768, 49152, 65535};
    for (unsigned i = 0; i < sizeof headings / sizeof headings[0]; ++i)
    {
        PortableSkyMotion expected = *motion(w, racer);
        const PortableSkyMotionInputs in = {.clock = w->clock,
                                            .yaw = headings[i],
                                            .selected_progress = w->progress[racer].high_water,
                                            .scene_flags_7fe = 256,
                                            .precise_vertical_offset = 1};
        CHECK(portable_sky_motion_resolve(&expected, &c->rules, &in, c->sine, 1024, 0) == 1);
        PortableSkyMotionSnapshot actual;
        CHECK(portable_mesh_sky_snapshot_racer(c, w, racer, headings[i], &actual));
        CHECK(!memcmp(&actual, &expected.snapshot, sizeof actual));
        CHECK(!memcmp(w, &saved, sizeof saved));
    }
    return 0;
}
static int speed_pan(void)
{
    PortableMeshSkyConfig c = fixture(PORTABLE_SKY_MOTION_SPEED_PAN);
    PortableMeshSky w, reset;
    CHECK(portable_mesh_sky_config_valid(&c));
    CHECK(portable_mesh_sky_init(&c, &w));
    CHECK(w.progress_enabled && !w.racer_count && !w.clock);
    for (unsigned i = 0; i < 8; ++i)
    {
        CHECK(w.progress[i].high_water == 0x7f00);
        CHECK(!memcmp(motion(&w, i), &w.motion, sizeof w.motion));
    }
    const PortableMeshSky initial = w;
    PortableMeshSkyProgressInput poses[3] = {{3, 0, 1}, {3, 1, 1}, {0, 2, 0}};
    CHECK(!step(&c, &w, poses, 3, 1));
    CHECK(w.progress[0].route_wrap == 128 && w.progress[1].route_wrap == 129 &&
          w.progress[2].route_wrap == 130);
    CHECK(w.motion.phase == 0 && w.other_motion[0].phase == 0 && w.other_motion[1].phase == 1);
    for (unsigned i = 0; i < 5; ++i)
        CHECK(!step(&c, &w, poses, 3, 1));
    poses[0] = (PortableMeshSkyProgressInput){0, 2, 0};
    CHECK(!step(&c, &w, poses, 3, 1));
    CHECK(w.motion.phase == 1 && w.motion.elapsed == 0 && w.other_motion[1].elapsed == 6);
    PortableSkyMotionSnapshot accepted_view;
    CHECK(portable_mesh_sky_snapshot_racer(&c, &w, 2, 0, &accepted_view));
    CHECK(!memcmp(&accepted_view, &w.other_motion[1].snapshot, sizeof accepted_view));

    while (w.other_motion[1].elapsed < 1024)
        CHECK(!step(&c, &w, poses, 3, 1));
    CHECK(w.other_motion[1].phase == 1 && w.motion.phase == 1);
    CHECK(!step(&c, &w, poses, 3, 1));
    CHECK(w.other_motion[1].phase == 2 && w.other_motion[1].elapsed == 1025 && w.motion.phase == 1);
    const uint32_t transition = w.other_motion[1].transition_clock;
    CHECK(transition == w.clock);
    for (unsigned i = 0; i < 129; ++i)
    {
        CHECK(!step(&c, &w, poses, 3, 1));
        CHECK(!snapshot_oracle(&c, &w, 2));
    }
    CHECK(w.other_motion[1].transition_clock == transition);
    PortableSkyMotionSnapshot front, quarter, negative;
    CHECK(portable_mesh_sky_snapshot_racer(&c, &w, 2, 0, &front));
    CHECK(portable_mesh_sky_snapshot_racer(&c, &w, 2, 16384, &quarter));
    CHECK(portable_mesh_sky_snapshot_racer(&c, &w, 2, 49152, &negative));
    CHECK(quarter.scroll_x_q16[0] == front.scroll_x_q16[0]);
    CHECK(quarter.scroll_x_q16[1] - front.scroll_x_q16[1] == 256 * 65536);
    CHECK(quarter.scroll_x_q16[2] - front.scroll_x_q16[2] == 64 * 65536);
    CHECK(negative.scroll_x_q16[0] == front.scroll_x_q16[0]);
    CHECK(negative.scroll_x_q16[1] - front.scroll_x_q16[1] == -256 * 65536);
    CHECK(negative.scroll_x_q16[2] - front.scroll_x_q16[2] == -64 * 65536);
    CHECK(portable_mesh_sky_init(&c, &reset) && !memcmp(&initial, &reset, sizeof reset));
    return 0;
}
static int leader_shake(void)
{
    PortableMeshSkyConfig c = fixture(PORTABLE_SKY_MOTION_LEADER_SHAKE);
    PortableMeshSky w;
    CHECK(portable_mesh_sky_init(&c, &w));
    PortableMeshSkyProgressInput poses[3] = {{3, 0, 1}, {3, 1, 1}, {1, 0, 1}};
    CHECK(!step(&c, &w, poses, 3, 0));
    for (unsigned i = 0; i < 3; ++i)
        CHECK(!motion(&w, i)->phase);
    CHECK(!step(&c, &w, poses, 3, 1));
    for (unsigned i = 0; i < 3; ++i)
        CHECK(motion(&w, i)->phase == 1);
    for (unsigned tick = 0; tick < 8; ++tick)
    {
        CHECK(!step(&c, &w, poses, 3, 0));
        for (unsigned i = 0; i < 3; ++i)
        {
            CHECK(motion(&w, i)->snapshot.scroll_x_q16[1] ==
                  (-96 + (w.clock & 1u ? 1 : -1)) * 65536);
            CHECK(!snapshot_oracle(&c, &w, i));
        }
    }
    return 0;
}
static int rollback(void)
{
    PortableMeshSkyConfig c = fixture(PORTABLE_SKY_MOTION_SPEED_PAN);
    PortableMeshSky w;
    CHECK(portable_mesh_sky_init(&c, &w));
    PortableMeshSkyProgressInput poses[8] = {0};
    for (unsigned i = 0; i < 8; ++i)
        poses[i] = (PortableMeshSkyProgressInput){3, 0, 1};
    CHECK(!step(&c, &w, poses, 8, 7));
    const PortableMeshSky saved = w;
    PortableMeshSky out;
    memset(&out, 0xa5, sizeof out);
    const PortableMeshSky sentinel = out;
    const PortableMeshSkyProgressInput bad[] = {{4, 0, 1}, {3, 128, 1}, {3, 0, 2}};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i)
    {
        poses[7] = bad[i];
        CHECK(!portable_mesh_sky_advance_racers(&c, &out, &w, poses, 8, 0));
        CHECK(!memcmp(&out, &sentinel, sizeof out) && !memcmp(&w, &saved, sizeof w));
        poses[7] = (PortableMeshSkyProgressInput){3, 0, 1};
    }
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &w, poses, 8, 8));
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &w, poses, 7, 0));
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &w, poses, 0, 0));
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &w, poses, 9, 0));
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &w, NULL, 8, 0));
    CHECK(!portable_mesh_sky_advance(&c, &out, &w));
    CHECK(!memcmp(&out, &sentinel, sizeof out) && !memcmp(&w, &saved, sizeof w));
    PortableSkyMotionSnapshot snapshot;
    memset(&snapshot, 0xa5, sizeof snapshot);
    const PortableSkyMotionSnapshot before_snapshot = snapshot;
    CHECK(!portable_mesh_sky_snapshot_racer(&c, &w, 8, 0, &snapshot));
    CHECK(!memcmp(&snapshot, &before_snapshot, sizeof snapshot));
    return 0;
}
static void put(uint8_t *p, uint32_t value)
{
    for (unsigned b = 0; b < 4; ++b)
        p[b] = (uint8_t)(value >> (8 * b));
}
static int sunset_palette(PortableMeshPaletteConfig *, uint8_t bytes[20000]);
static int unbound_profiles(void)
{
    const unsigned controllers[] = {2, 4, 5};
    for (unsigned choice = 0; choice < 3; ++choice)
    {
        const unsigned controller = controllers[choice];
        PortableMeshSkyConfig c = fixture(controller), decoded;
        uint8_t bytes[PORTABLE_MESH_SKY_PROFILE_SIZE] = {0};
        const uint32_t header[] = {
            UINT32_C(0x314b534d), 1, PORTABLE_MESH_SKY_PROFILE_SIZE, 41, 7, 7, 0, 1024};
        const int32_t rules[] = {(int32_t)controller,
                                 7,
                                 7,
                                 8,
                                 7,
                                 0,
                                 0,
                                 0,
                                 3,
                                 -96,
                                 11,
                                 38,
                                 32,
                                 38,
                                 -1,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 1,
                                 2};
        for (unsigned i = 0; i < 8; ++i)
            put(bytes + 4 * i, header[i]);
        for (unsigned i = 0; i < 23; ++i)
            put(bytes + 32 + 4 * i, (uint32_t)rules[i]);
        for (unsigned i = 0; i < 3; ++i)
        {
            put(bytes + 124 + 8 * i, c.logical_width[i]);
            put(bytes + 128 + 8 * i, (uint32_t)c.radius_q16[i]);
        }
        for (unsigned i = 0; i < 1024; ++i)
            put(bytes + 148 + 4 * i, (uint32_t)c.sine[i]);
        char error[192];
        CHECK(portable_mesh_sky_decode(bytes, sizeof bytes, 41, &decoded, error, sizeof error));
        CHECK(!decoded.progress.enabled && !portable_mesh_sky_config_valid(&decoded));
        PortableMeshSky out;
        memset(&out, 0xa5, sizeof out);
        const PortableMeshSky sentinel = out;
        CHECK(!portable_mesh_sky_init(&decoded, &out) && !memcmp(&out, &sentinel, sizeof out));
        decoded.progress = c.progress;
        uint8_t palette_bytes[20000];
        if (controller == 4)
        {
            CHECK(!portable_mesh_sky_config_valid(&decoded));
            CHECK(!sunset_palette(&decoded.palette, palette_bytes));
        }
        CHECK(portable_mesh_sky_config_valid(&decoded) && portable_mesh_sky_init(&decoded, &out));
    }
    return 0;
}

static void palette_half(uint8_t *p, unsigned value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void palette_patch(uint8_t *p, unsigned target, unsigned offset, unsigned count,
                          unsigned source)
{
    p[0] = (uint8_t)target;
    palette_half(p + 2, offset);
    palette_half(p + 4, count);
    put(p + 8, source);
}
/* Source-style64 sunset frames patch sky0..47, road0..31 and road48..63.
 * The independent3-frame wave overwrites road60..61 after the PRE lane. */
static int sunset_palette(PortableMeshPaletteConfig *out, uint8_t bytes[20000])
{
    memset(bytes, 0, 20000);
    enum
    {
        LANES = 2,
        FRAMES = 67,
        PATCHES = 195,
        DATA = 256 + 64 * 192 + 3 * 4,
        AMBIENT = 32 + LANES * 32 + FRAMES * 8 + PATCHES * 12 + DATA,
        SIZE = 336 + AMBIENT + 4
    };
    const uint32_t header[] = {UINT32_C(0x3141504d), 1, SIZE, 55, 1, 1, AMBIENT, 4, 0, 0, 0, 0};
    for (unsigned i = 0; i < 12; ++i)
        put(bytes + i * 4, header[i]);
    for (unsigned i = 0; i < 64; ++i)
    {
        palette_half(bytes + 48 + i * 2, 100 + i);
        palette_half(bytes + 176 + i * 2, 200 + i);
    }
    const uint32_t binding[] = {97, 100, 0, 2, 2, 0, 4, 0};
    for (unsigned i = 0; i < 8; ++i)
        put(bytes + 304 + i * 4, binding[i]);
    uint8_t *a = bytes + 336;
    const uint32_t ambient[] = {UINT32_C(0x31424d41), 1, LANES, FRAMES, PATCHES, 0, DATA, 0};
    for (unsigned i = 0; i < 8; ++i)
        put(a + i * 4, ambient[i]);
    a[32] = PORTABLE_AMBIENT_PRE_PROFILE;
    a[34] = PORTABLE_AMBIENT_SUNSET;
    palette_half(a + 38, 64);
    a[64] = PORTABLE_AMBIENT_POST_DRIVER;
    a[66] = PORTABLE_AMBIENT_TIMER;
    palette_half(a + 68, 64);
    palette_half(a + 70, 3);
    uint8_t *frames = a + 32 + LANES * 32;
    uint8_t *patches = frames + FRAMES * 8;
    uint8_t *data = patches + PATCHES * 12;
    for (unsigned i = 0; i < 128; ++i)
        palette_half(data + i * 2, 500 + i);
    for (unsigned f = 0; f < 64; ++f)
    {
        uint8_t *frame = frames + f * 8;
        palette_half(frame, 4);
        palette_half(frame + 2, f * 3);
        frame[4] = 3;
        const unsigned offset = 256 + f * 192;
        palette_patch(patches + f * 36, PORTABLE_AMBIENT_SKY, 0, 48, offset);
        palette_patch(patches + f * 36 + 12, PORTABLE_AMBIENT_ROAD, 0, 32, offset + 96);
        palette_patch(patches + f * 36 + 24, PORTABLE_AMBIENT_ROAD, 48, 16, offset + 160);
        for (unsigned i = 0; i < 48; ++i)
            palette_half(data + offset + i * 2, 5000 + f * 64 + i);
        for (unsigned i = 0; i < 32; ++i)
            palette_half(data + offset + 96 + i * 2, 1000 + f * 32 + i);
        for (unsigned i = 0; i < 16; ++i)
            palette_half(data + offset + 160 + i * 2, 3000 + f * 16 + i);
    }
    for (unsigned f = 0; f < 3; ++f)
    {
        uint8_t *frame = frames + (64 + f) * 8;
        palette_half(frame, 2 + f);
        palette_half(frame + 2, 192 + f);
        frame[4] = 1;
        const unsigned offset = 256 + 64 * 192 + f * 4;
        palette_patch(patches + (192 + f) * 12, PORTABLE_AMBIENT_ROAD, 60, 2, offset);
        palette_half(data + offset, 16000 + f * 2);
        palette_half(data + offset + 2, 16001 + f * 2);
    }
    const uint8_t indices[] = {0, 48, 60, 255};
    memcpy(bytes + SIZE - 4, indices, 4);
    char error[192];
    CHECK(portable_mesh_palette_decode(bytes, SIZE, 55, out, error, sizeof error));
    CHECK(out->variant_count == 260);
    return 0;
}
static int sunset_step(const PortableMeshSkyConfig *c, PortableMeshSky *w,
                       const PortableMeshSkyProgressInput *poses, uint64_t *emitted)
{
    const PortableSkyMotion before = w->motion;
    CHECK(!step(c, w, poses, 3, 1));
    const int emits = before.transition_counter == 3 &&
                      ((before.phase == 1 && before.elapsed <= 31) ||
                       (before.phase == 4 && before.elapsed >= 32 && before.elapsed <= 63));
    if (emits)
    {
        CHECK(before.elapsed >= 0 && before.elapsed < 64);
        const uint64_t bit = UINT64_C(1) << (unsigned)before.elapsed;
        CHECK(!(*emitted & bit));
        *emitted |= bit;
        CHECK(w->palette.ambient.snapshot.sky[0] == 5000 + before.elapsed * 64);
    }
    CHECK(w->palette.ambient.snapshot.road[32] == 132 &&
          w->palette.ambient.snapshot.sky[55] == 255);
    const PortableMeshSky saved = *w;
    const uint64_t checksum = portable_mesh_sky_checksum(c, w);
    for (unsigned racer = 0; racer < 3; ++racer)
    {
        CHECK(motion(w, racer)->phase == w->motion.phase);
        PortableSkyMotionSnapshot view;
        CHECK(portable_mesh_sky_snapshot_racer(c, w, racer, 0, &view));
        CHECK(!memcmp(&view, &motion(w, racer)->snapshot, sizeof view));
        CHECK(portable_mesh_sky_snapshot_racer(c, w, racer, 49152, &view));
        CHECK(view.scroll_y_q16[2] == motion(w, racer)->snapshot.scroll_y_q16[2]);
        CHECK(view.split_vertical == (w->motion.phase > 4));
        if (w->motion.phase > 4)
            CHECK(view.split_y == 24 && view.split_layer == 2 && view.split_scroll_y == 46 &&
                  view.split_scroll_y_q16 == 46 * 65536);
    }
    CHECK(!memcmp(w, &saved, sizeof saved) && portable_mesh_sky_checksum(c, w) == checksum);
    return 0;
}
static int sunset(void)
{
    PortableMeshSkyConfig c = fixture(PORTABLE_SKY_MOTION_SUNSET);
    uint8_t bytes[20000];
    CHECK(!sunset_palette(&c.palette, bytes));
    CHECK(portable_mesh_sky_config_valid(&c));
    PortableMeshSky w, reset;
    CHECK(portable_mesh_sky_init(&c, &w));
    const PortableMeshSky initial = w;
    PortableMeshSkyProgressInput poses[3] = {{1, 0, 1}, {3, 0, 1}, {2, 0, 1}};
    uint64_t emitted = 0;
    CHECK(!sunset_step(&c, &w, poses, &emitted));
    CHECK(w.motion.phase == 1 && w.palette.ambient.snapshot.sky[0] == 200 && !emitted);
    unsigned budget = 0;
    while (w.motion.phase != 2 && budget++ < 200)
        CHECK(!sunset_step(&c, &w, poses, &emitted));
    CHECK(w.motion.phase == 2 && emitted == UINT64_C(0xffffffff));
    for (unsigned i = 0; i < 8; ++i)
        CHECK(!sunset_step(&c, &w, poses, &emitted));
    CHECK(w.motion.phase == 2 && w.motion.horizon_y == 144);
    poses[1] = (PortableMeshSkyProgressInput){3, 1, 1};
    CHECK(!sunset_step(&c, &w, poses, &emitted));
    CHECK(w.motion.phase == 3 && !w.motion.elapsed);
    budget = 0;
    while (w.motion.phase != 6 && budget++ < 400)
        CHECK(!sunset_step(&c, &w, poses, &emitted));
    CHECK(w.motion.phase == 6 && w.motion.elapsed == 65 && w.motion.horizon_y == 32 &&
          emitted == UINT64_MAX);
    CHECK(portable_mesh_sky_init(&c, &reset) && !memcmp(&reset, &initial, sizeof reset));
    const PortableMeshSky saved = w;
    PortableMeshSky out;
    memset(&out, 0xa5, sizeof out);
    const PortableMeshSky sentinel = out;
    PortableMeshSky bad = w;
    bad.motion.transition_counter = 4;
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &bad, poses, 3, 1));
    bad = w;
    bad.palette.variant = UINT32_MAX;
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &bad, poses, 3, 1));
    bad = w;
    bad.motion.snapshot.split_vertical = 0;
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &bad, poses, 3, 1));
    poses[2].stage = 4;
    CHECK(!portable_mesh_sky_advance_racers(&c, &out, &w, poses, 3, 1));
    CHECK(!memcmp(&out, &sentinel, sizeof out) && !memcmp(&w, &saved, sizeof w));
    c.rules.controller = PORTABLE_SKY_MOTION_CLOCK_MID;
    c.progress = (PortableMeshSkyProgressConfig){0};
    CHECK(!portable_mesh_sky_config_valid(&c)); /* Orphan palette. */
    c.rules.controller = PORTABLE_SKY_MOTION_SUNSET;
    c.progress = fixture(PORTABLE_SKY_MOTION_SUNSET).progress;
    c.palette = (PortableMeshPaletteConfig){0};
    CHECK(!portable_mesh_sky_config_valid(&c));
    CHECK(!portable_mesh_sky_init(&c, &out) && !memcmp(&out, &sentinel, sizeof out));
    return 0;
}
int main(void)
{
    for (unsigned i = 0; i < 1024; ++i)
        sine[i] = (int32_t)lround(sin(i * 6.28318530717958647693 / 1024) * 32768);
    CHECK(!unbound_profiles() && !speed_pan() && !leader_shake() && !rollback() && !sunset());
    puts(
        "mesh sky racers: PASS selected progress, shared leader, source timing, pane yaw, immutable rollback");
    return 0;
}
