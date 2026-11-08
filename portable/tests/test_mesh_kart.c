#include "mesh_course_fixture.h"
#include "mesh_kart_mk8.h"
#include "fixed_3d.h"
#include "mesh_kart_rules_asset.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI /* Strict C11 headers (glibc, MSVC) omit it. */
#define M_PI 3.14159265358979323846
#endif
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
typedef struct
{
    PortableMeshTriangle triangles[48];
    PortableMeshSurface surfaces[2];
    uint32_t count;
    int invalid_index;
} World;
static int triangle(const void *ctx, uint32_t i, PortableMeshTriangle *out)
{
    const World *w = ctx;
    if (i >= w->count)
        return 0;
    *out = w->triangles[i];
    return 1;
}
static int surface(const void *ctx, uint32_t i, PortableMeshSurface *out)
{
    const World *w = ctx;
    if (i >= 2u)
        return 0;
    *out = w->surfaces[i];
    return 1;
}
static void floor_world(World *w)
{
    memset(w, 0, sizeof(*w));
    w->count = 2;
    const int32_t points[4][3] = {{-20 * 65536, -20 * 65536, 0},
                                  {20 * 65536, -20 * 65536, 0},
                                  {20 * 65536, 20 * 65536, 0},
                                  {-20 * 65536, 20 * 65536, 0}};
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2u; ++t)
    {
        for (unsigned i = 0; i < 3u; ++i)
            memcpy(w->triangles[t].vertices_q16[i], points[indices[t][i]], 12);
        w->triangles[t].respawn = UINT32_MAX;
    }
    for (unsigned i = 0; i < 2u; ++i)
    {
        w->surfaces[i].friction_q16 = 65536;
        w->surfaces[i].speed_ratio_q16 = 65536;
        w->surfaces[i].gravity_ratio_q16 = 65536;
    }
}
static int world_next(const void *context, const int32_t low[3], const int32_t high[3],
                      uint32_t first, uint32_t *out)
{
    const World *world = context;
    if (world->invalid_index)
    {
        *out = world->count;
        return 1;
    }
    for (uint32_t i = first; i < world->count; ++i)
    {
        int overlaps = 1;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            int32_t minimum = INT32_MAX, maximum = INT32_MIN;
            for (unsigned vertex = 0; vertex < 3u; ++vertex)
            {
                const int32_t value = world->triangles[i].vertices_q16[vertex][axis];
                if (value < minimum)
                    minimum = value;
                if (value > maximum)
                    maximum = value;
            }
            overlaps &= maximum >= low[axis] && minimum <= high[axis];
        }
        if (overlaps)
        {
            *out = i;
            return 1;
        }
    }
    return 0;
}

static int indexed_corrections(void)
{
    World world;
    floor_world(&world);
    world.count = 48;
    for (unsigned i = 0; i < world.count; ++i)
    {
        const int32_t x = (int32_t)i * 65536 * 2 / 5;
        world.triangles[i] = (PortableMeshTriangle){.vertices_q16 = {{x, -10 * 65536, -10 * 65536},
                                                                     {x, 10 * 65536, -10 * 65536},
                                                                     {x, 0, 10 * 65536}},
                                                    .respawn = UINT32_MAX};
    }
    PortableMeshKartRules rules = test_mesh_rules();
    PortableMeshCourse linear = {&world, world.count, 0, triangle, surface, NULL, NULL};
    PortableMeshCourse indexed = linear;
    indexed.next_triangle = world_next;
    const int32_t position[3] = {0, 0, 0}, forward[3] = {0, -1073741824, 0};
    PortableMeshKartState a, b;
    PortableMeshKartEvents ea, eb;
    CHECK(portable_mesh_kart_init(&a, &rules, position, forward) == PORTABLE_MESH_OK);
    b = a;
    /* Each canonical face pushes into the next. A static candidate list at
     * the original endpoint cannot reach the end within eight contact passes. */
    CHECK(portable_mesh_kart_tick(&a, &rules, &linear, (PortableMeshKartInput){0}, &ea) ==
          PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&b, &rules, &indexed, (PortableMeshKartInput){0}, &eb) ==
          PORTABLE_MESH_OK);
    CHECK(a.position_q16[0] > 19 * 65536 && ea.wall);
    CHECK(!memcmp(&a, &b, sizeof a) && !memcmp(&ea, &eb, sizeof ea));
    const PortableMeshKartState preserved = b;
    const PortableMeshKartEvents previous = eb;
    world.invalid_index = 1;
    CHECK(portable_mesh_kart_tick(&b, &rules, &indexed, (PortableMeshKartInput){0}, &eb) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&b, &preserved, sizeof b) && !memcmp(&eb, &previous, sizeof eb));
    return 0;
}

static int flat_and_drift(void)
{
    World w;
    floor_world(&w);
    PortableMeshKartRules r = test_mesh_rules();
    PortableMeshCourse course = {&w, w.count, 10 * 65536, triangle, surface, NULL, NULL};
    PortableMeshKartState s;
    const int32_t p[3] = {0, 0, 49152}, f[3] = {0, -1073741824, 0};
    PortableMeshKartEvents e;
    CHECK(portable_mesh_kart_init(&s, &r, p, f) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){0, 0}, &e) ==
              PORTABLE_MESH_OK &&
          s.grounded);
    const int32_t acceleration_tick = portable_fixed_3d_dt(10 * 65536);
    CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){0, 1}, &e) ==
          PORTABLE_MESH_OK);
    CHECK(s.speed_q16 == acceleration_tick && s.velocity_q16[1] == -acceleration_tick &&
          s.position_q16[2] == 49152);
    s.speed_q16 = 5 * 65536;
    s.velocity_q16[1] = -s.speed_q16;
    r.hop_velocity_q16 = 0;
    CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){20000, 5}, &e) ==
              PORTABLE_MESH_OK &&
          e.hopped);
    for (unsigned i = 0; i < 5u; ++i)
        CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){20000, 5}, &e) ==
              PORTABLE_MESH_OK);
    CHECK(s.drift_tier == 3 && s.drift_charge == r.charge_thresholds[2]);
    CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){0, 1}, &e) ==
              PORTABLE_MESH_OK &&
          e.released && s.last_boost_tier == 3);
    CHECK(s.boost_ticks[2] == r.boost_ticks[2] - 1u && s.drift_charge == 0);
    for (uint32_t i = 1; i < r.boost_ticks[2]; ++i)
        CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){0, 1}, &e) ==
                  PORTABLE_MESH_OK &&
              s.last_boost_tier == 3);
    CHECK(!s.boost_ticks[2]);
    w.surfaces[0].type = 4;
    CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){0, 1}, &e) ==
          PORTABLE_MESH_OK);
    CHECK(e.panel && s.panel_ticks == r.panel_boost_ticks - 1u);
    PortableMeshKartState copy = s;
    CHECK(portable_mesh_kart_tick(&s, &r, &course, (PortableMeshKartInput){-12000, 1}, &e) ==
          PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&copy, &r, &course, (PortableMeshKartInput){-12000, 1}, &e) ==
          PORTABLE_MESH_OK);
    CHECK(memcmp(s.position_q16, copy.position_q16, sizeof(s.position_q16)) == 0 &&
          memcmp(s.velocity_q16, copy.velocity_q16, sizeof(s.velocity_q16)) == 0 &&
          s.panel_ticks == copy.panel_ticks && s.drift_charge == copy.drift_charge);
    return 0;
}
static int slope_air_and_bump(void)
{
    World w;
    floor_world(&w);
    PortableMeshKartRules r = test_mesh_rules();
    /* z=x/2: support normal is analytically (-1,0,2)/sqrt(5). */
    for (unsigned t = 0; t < 2u; ++t)
        for (unsigned i = 0; i < 3u; ++i)
            w.triangles[t].vertices_q16[i][2] = w.triangles[t].vertices_q16[i][0] / 2;
    PortableMeshCourse c = {&w, 2, 10 * 65536, triangle, surface, NULL, NULL};
    PortableMeshKartState s;
    const int32_t p[3] = {0, 0, 49152}, f[3] = {0, -1073741824, 0};
    PortableMeshKartEvents e;
    CHECK(portable_mesh_kart_init(&s, &r, p, f) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 0}, &e) ==
              PORTABLE_MESH_OK &&
          s.grounded);
    CHECK(s.up_q30[0] < -479000000 && s.up_q30[0] > -482000000 && s.up_q30[2] > 958000000 &&
          s.up_q30[2] < 963000000);
    CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 4}, &e) ==
              PORTABLE_MESH_OK &&
          e.hopped && !s.grounded);
    CHECK(s.velocity_q16[2] > 0 && s.air_ticks == 1);
    PortableMeshKartState a, b;
    const int32_t ap[3] = {0, 0, 65536}, bp[3] = {32768, 0, 65536};
    CHECK(portable_mesh_kart_init(&a, &r, ap, f) == PORTABLE_MESH_OK &&
          portable_mesh_kart_init(&b, &r, bp, f) == PORTABLE_MESH_OK);
    a.velocity_q16[0] = 2 * 65536;
    CHECK(portable_mesh_karts_contact(&a, &r, &b, &r) == PORTABLE_MESH_OK);
    /* MK8 bumps split the overlap and exchange no speed; equal karts side by
     * side queue mirrored pushes away from each other for three ticks later. */
    CHECK(a.velocity_q16[0] == 2 * 65536 && b.velocity_q16[0] == 0 &&
          b.position_q16[0] - a.position_q16[0] == 65536);
    CHECK(a.bump_delay == MESH_KART_MK8_BUMP_DELAY && b.bump_delay == MESH_KART_MK8_BUMP_DELAY &&
          a.bump_push_q16[0] < 0 && a.bump_push_q16[0] == -b.bump_push_q16[0]);
    return 0;
}
static int contact_speed_survives_coast(void)
{
    World world;
    floor_world(&world);
    const PortableMeshKartRules rules = test_mesh_rules();
    const PortableMeshCourse course = {&world, world.count, 10 * 65536, triangle, surface, NULL, NULL};
    PortableMeshKartState a, b;
    PortableMeshKartEvents events;
    const int32_t ap[3] = {-16384, 0, 49152}, bp[3] = {16384, 0, 49152};
    const int32_t af[3] = {1073741824, 0, 0}, bf[3] = {-1073741824, 0, 0};
    CHECK(portable_mesh_kart_init(&a, &rules, ap, af) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_init(&b, &rules, bp, bf) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&a, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
              PORTABLE_MESH_OK &&
          a.grounded);
    CHECK(portable_mesh_kart_tick(&b, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
              PORTABLE_MESH_OK &&
          b.grounded);
    a.velocity_q16[0] = 65536;
    b.velocity_q16[0] = -65536;
    a.speed_q16 = b.speed_q16 = 65536;
    CHECK(portable_mesh_karts_contact(&a, &rules, &b, &rules) == PORTABLE_MESH_OK);
    /* Nose to nose, MK8 keeps both engines and queues a knock: 0.1 units/frame
     * back along the contact plus a sideways shove. */
    CHECK(a.velocity_q16[0] == 65536 && b.velocity_q16[0] == -65536 && a.speed_q16 == 65536 &&
          b.speed_q16 == 65536 && a.bump_push_q16[0] < 0 && b.bump_push_q16[0] > 0 &&
          a.bump_push_q16[1] && b.bump_push_q16[1]);
    const int32_t a_push = a.bump_push_q16[0], b_push = b.bump_push_q16[0];
    for (unsigned tick = 0; tick < MESH_KART_MK8_BUMP_DELAY; ++tick)
    {
        CHECK(!a.knock_q16[0] && !b.knock_q16[0]);
        CHECK(portable_mesh_kart_tick(&a, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
              PORTABLE_MESH_OK);
        CHECK(portable_mesh_kart_tick(&b, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
              PORTABLE_MESH_OK);
    }
    /* The push lands whole and holds traction raised for 12 ticks. */
    CHECK(a.knock_q16[0] == a_push && b.knock_q16[0] == b_push && !a.bump_delay &&
          a.traction_hold_ticks == MESH_KART_MK8_BUMP_TICKS && a.grounded && b.grounded);
    CHECK(portable_mesh_kart_tick(&a, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
          PORTABLE_MESH_OK);
    /* While held the knock keeps 0.99 a tick. */
    CHECK(a.knock_q16[0] == portable_fixed_3d_mul(a_push, MESH_KART_MK8_TRACTION_CAP_Q16));
    return 0;
}
/* MK8 weight decides who moves: the light kart takes about g=0.95 of the
 * push, the heavy one 0.05; a Star kart holds still and is not bumped. */
static int weighted_bumps(void)
{
    PortableMeshKartRules light = test_mesh_rules(), heavy = light;
    light.weight_q16[0] = light.weight_q16[1] = 5 * 65536;
    light.weight_q16[2] = 20 * 65536;
    heavy.weight_q16[0] = 50 * 65536;
    heavy.weight_q16[1] = heavy.weight_q16[2] = 100 * 65536;
    PortableMeshKartState a, b;
    const int32_t ap[3] = {0, 0, 65536}, bp[3] = {49152, 0, 65536}, f[3] = {0, 1073741824, 0};
    CHECK(portable_mesh_kart_init(&a, &light, ap, f) == PORTABLE_MESH_OK &&
          portable_mesh_kart_init(&b, &heavy, bp, f) == PORTABLE_MESH_OK);
    a.speed_q16 = b.speed_q16 = a.velocity_q16[1] = b.velocity_q16[1] = 10 * 65536;
    CHECK(portable_mesh_karts_contact(&a, &light, &b, &heavy) == PORTABLE_MESH_OK);
    CHECK(a.position_q16[0] == -8192 && b.position_q16[0] == 57344);
    CHECK(a.bump_push_q16[0] < 0 && b.bump_push_q16[0] > 0 && -a.bump_push_q16[0] >= 2 * b.bump_push_q16[0]);
    CHECK(a.speed_q16 == 10 * 65536 && b.speed_q16 == 10 * 65536);
    PortableMeshKartState c, d;
    CHECK(portable_mesh_kart_init(&c, &light, ap, f) == PORTABLE_MESH_OK &&
          portable_mesh_kart_init(&d, &light, bp, f) == PORTABLE_MESH_OK);
    const PortableMeshKartEffects star = {.speed_ratio_q16 = 65536, .body_scale_q16 = 65536,
        .boost_cap_ratio_q16 = 65536, .star_cap_ratio_q16 = 65536, .star = 1};
    CHECK(portable_mesh_karts_contact_effects(&c, &light, &star, &d, &light, NULL) == PORTABLE_MESH_OK);
    CHECK(c.position_q16[0] == 0 && d.position_q16[0] == 65536 && !c.bump_delay && !d.bump_delay);
    return 0;
}
/* MK8 slipstream: the strip's boxes catch a follower behind but not beside
 * the leader; a box every fifth tick keeps the grace alive, the 91st
 * charging tick fires the 135-tick boost toward 1.2x top speed, and 12
 * off-road ticks cancel it. */
static int slipstream(void)
{
    World w;
    floor_world(&w);
    const PortableMeshKartRules r = test_mesh_rules();
    PortableMeshCourse c = {&w, 2, 10 * 65536, triangle, surface, NULL, NULL};
    PortableMeshKartState leader, follower;
    const int32_t lp[3] = {0, 0, 49152}, fp[3] = {-98304, 0, 49152}, f[3] = {1073741824, 0, 0};
    CHECK(portable_mesh_kart_init(&leader, &r, lp, f) == PORTABLE_MESH_OK &&
          portable_mesh_kart_init(&follower, &r, fp, f) == PORTABLE_MESH_OK);
    leader.speed_q16 = follower.speed_q16 = r.forward_speed_q16;
    unsigned boxes = 0;
    for (unsigned box = 0; box < 5u; ++box)
        boxes += (unsigned)portable_mesh_kart_drafting(&follower, &r, &leader, &r, box);
    CHECK(boxes >= 2u && !portable_mesh_kart_drafting(&leader, &r, &follower, &r, 0));
    PortableMeshKartState beside = follower;
    beside.position_q16[1] = 3 * 65536;
    PortableMeshKartState slow = follower;
    slow.speed_q16 = r.forward_speed_q16 / 2;
    for (unsigned box = 0; box < 5u; ++box)
        CHECK(!portable_mesh_kart_drafting(&beside, &r, &leader, &r, box) &&
              !portable_mesh_kart_drafting(&slow, &r, &leader, &r, box));
    PortableMeshKartEvents e;
    CHECK(portable_mesh_kart_tick(&follower, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK &&
          follower.grounded);
    follower.position_q16[0] = -15 * 65536;
    for (unsigned tick = 1; tick <= 91u; ++tick)
    {
        follower.slip_in = tick % 5u == 1u;
        CHECK(portable_mesh_kart_tick(&follower, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &e) ==
              PORTABLE_MESH_OK);
        CHECK(tick < 91u ? follower.slip_charge == tick && !follower.slip_ticks
                         : !follower.slip_charge && follower.slip_ticks == MESH_KART_MK8_SLIP_TICKS - 1u);
    }
    CHECK(portable_mesh_kart_boosting(&follower));
    for (unsigned tick = 0; tick < 100u; ++tick)
        CHECK(portable_mesh_kart_tick(&follower, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &e) ==
              PORTABLE_MESH_OK);
    CHECK(follower.speed_q16 > r.forward_speed_q16 && !follower.slip_charge);
    w.surfaces[0].surface_class = w.surfaces[1].surface_class = MESH_KART_MK8_CLASS_GRASS;
    for (unsigned tick = 1; tick <= MESH_KART_MK8_OFFROAD_TICKS; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&follower, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &e) ==
              PORTABLE_MESH_OK);
        CHECK(!follower.slip_ticks == (tick == MESH_KART_MK8_OFFROAD_TICKS));
    }
    return 0;
}
/* MK8 tricks: a hop off a jump surface at speed tricks without adding
 * lift and boosts on a landing 20+ ticks later; flat ground, slow karts and
 * surfaces marked non-trick never trick. */
static int tricks(void)
{
    World w;
    floor_world(&w);
    const PortableMeshKartRules r = test_mesh_rules();
    PortableMeshCourse c = {&w, 2, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t p[3] = {-18 * 65536, 0, 49152}, f[3] = {1073741824, 0, 0};
    PortableMeshKartEvents e;
    for (unsigned variant = 0; variant < 4u; ++variant)
    {
        /* 0 jump, 1 flat road, 2 jump too slow, 3 jump marked non-trick */
        for (unsigned i = 0; i < 2u; ++i)
        {
            w.surfaces[i].type = variant == 1u ? 0u : 5u;
            w.surfaces[i].launch_q16 = variant == 1u ? 0 : 6 * 65536;
            w.surfaces[i].trick = variant == 3u ? 2u : 0u;
        }
        PortableMeshKartState s;
        CHECK(portable_mesh_kart_init(&s, &r, p, f) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK &&
              s.grounded && s.trick_surface == (variant != 1u && variant != 3u));
        s.speed_q16 = s.velocity_q16[0] = variant == 2u ? r.forward_speed_q16 / 2 : r.forward_speed_q16;
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                                      &e) == PORTABLE_MESH_OK);
        CHECK(e.tricked == !variant && !s.grounded);
        if (variant)
        {
            CHECK(!s.trick_kind);
            continue;
        }
        /* The launch alone lifts the kart: no hop on top of it. */
        CHECK(s.trick_kind == 1 && s.velocity_q16[2] > 5 * 65536 && s.velocity_q16[2] < 6 * 65536);
        unsigned air = 1, boosted = 0;
        while (!s.grounded && air < 400u)
        {
            CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &e) ==
                  PORTABLE_MESH_OK);
            ++air;
            boosted = e.trick_boost;
        }
        CHECK(air > MESH_KART_MK8_TRICK_LAND_TICKS && boosted && !s.trick_kind &&
              s.trick_boost_ticks == r.trick_ticks - 1u && portable_mesh_kart_boosting(&s));
    }
    return 0;
}
/* MK8 anti-gravity: a gravity-path segment lerps its ups within its radius;
 * a kart on a wall with the path's up there drives on it at the
 * anti-gravity row's speed; airborne karts right toward gravity after 11
 * ticks; anti-gravity bumps push at 2.5 and spin both karts the same way
 * into the 20-tick slot-2 boost, and spin bumpers knock and spin. */
static int anti_gravity(void)
{
    const PortableMeshGravityPoint from = {{0, 0, 0}, {0, 0, 1073741824}, 2 * 65536},
                                   to = {{10 * 65536, 0, 0}, {0, 1073741824, 0}, 2 * 65536};
    uint64_t distance;
    int32_t up[3];
    const int32_t middle[3] = {5 * 65536, 32768, 0}, wide[3] = {5 * 65536, 3 * 65536, 0},
                  before[3] = {-65536, 0, 0};
    CHECK(portable_mesh_gravity_segment(&from, &to, middle, &distance, up) && distance == (uint64_t)32768 * 32768 &&
          up[1] > 759000000 && up[1] < 760000000 && up[1] == up[2] && !up[0]);
    CHECK(!portable_mesh_gravity_segment(&from, &to, wide, &distance, up) &&
          !portable_mesh_gravity_segment(&from, &to, before, &distance, up));

    World w;
    floor_world(&w);
    const int32_t wall[4][3] = {{0, -20 * 65536, -20 * 65536}, {0, 20 * 65536, -20 * 65536},
                                {0, 20 * 65536, 20 * 65536}, {0, -20 * 65536, 20 * 65536}};
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2u; ++t)
        for (unsigned i = 0; i < 3u; ++i)
            memcpy(w.triangles[t].vertices_q16[i], wall[indices[t][i]], 12);
    PortableMeshKartRules r = test_mesh_rules();
    r.antigravity_speed_q16 = 6 * 65536;
    PortableMeshCourse c = {&w, 2, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t p[3] = {49152, -10 * 65536, 0}, f[3] = {0, 1073741824, 0};
    PortableMeshKartState s;
    PortableMeshKartEvents e;
    CHECK(portable_mesh_kart_init(&s, &r, p, f) == PORTABLE_MESH_OK);
    s.up_q30[0] = 1073741824;
    s.up_q30[2] = 0;
    for (unsigned tick = 0; tick < 150u; ++tick)
    {
        s.path_up_q30[0] = 1073741824;
        s.path_up_q30[1] = s.path_up_q30[2] = 0;
        s.anti_gravity = 1;
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &e) ==
              PORTABLE_MESH_OK);
    }
    CHECK(s.grounded && s.position_q16[2] > -6554 && s.position_q16[2] < 6554 && s.position_q16[0] < 65536 &&
          s.speed_q16 > 6 * 65536 - 6554 && s.speed_q16 <= 6 * 65536 &&
          s.anti_gravity_rate == MESH_KART_MK8_ANTIGRAVITY_RAMP);

    floor_world(&w);
    const int32_t high[3] = {0, 0, 20 * 65536}, along[3] = {1073741824, 0, 0};
    CHECK(portable_mesh_kart_init(&s, &r, high, along) == PORTABLE_MESH_OK);
    s.up_q30[1] = 536870912;
    s.up_q30[2] = 929887697;
    int32_t tilted = s.up_q30[2];
    for (unsigned tick = 1; tick <= 12u; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK &&
              !s.grounded);
        CHECK(tick < MESH_KART_MK8_AIR_UP_TICKS ? s.up_q30[2] == tilted : s.up_q30[2] > tilted);
        tilted = s.up_q30[2];
    }

    PortableMeshKartState a, b, plain_a, plain_b;
    const int32_t ap[3] = {0, 0, 49152}, bp[3] = {49152, 0, 49152}, ahead[3] = {0, 1073741824, 0};
    CHECK(portable_mesh_kart_init(&a, &r, ap, ahead) == PORTABLE_MESH_OK &&
          portable_mesh_kart_init(&b, &r, bp, ahead) == PORTABLE_MESH_OK);
    a.speed_q16 = b.speed_q16 = a.velocity_q16[1] = b.velocity_q16[1] = r.forward_speed_q16;
    plain_a = a;
    plain_b = b;
    a.anti_gravity = b.anti_gravity = 1;
    CHECK(portable_mesh_karts_contact(&a, &r, &b, &r) == PORTABLE_MESH_OK &&
          portable_mesh_karts_contact(&plain_a, &r, &plain_b, &r) == PORTABLE_MESH_OK);
    CHECK(a.spin_armed && b.spin_armed && a.spin_sign == b.spin_sign && a.spin_sign &&
          a.spin_ticks == MESH_KART_MK8_SPIN_TICKS_FAST && !plain_a.spin_armed &&
          plain_a.spin_ticks == MESH_KART_MK8_SPIN_TICKS_FAST && a.bump_push_q16[0] < 0 &&
          a.bump_push_q16[0] > plain_a.bump_push_q16[0]);
    unsigned fired = 0;
    for (unsigned tick = 1; tick <= MESH_KART_MK8_SPIN_TICKS_FAST; ++tick)
    {
        a.anti_gravity = 1;
        CHECK(portable_mesh_kart_tick(&a, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &e) ==
              PORTABLE_MESH_OK);
        if (tick == MESH_KART_MK8_SPIN_TICKS_FAST / 2u)
            CHECK(portable_mesh_kart_spin_turn(&a) * a.spin_sign > 16384);
        fired += e.spin_boost;
    }
    CHECK(fired == 1u && e.spin_boost && a.spin_boost_ticks == MESH_KART_MK8_SPIN_BOOST_TICKS && !a.spin_armed &&
          !portable_mesh_kart_spin_turn(&a) && portable_mesh_kart_boosting(&a));

    const int32_t bumper[3] = {2 * 65536, 0, 49152}, close[3] = {52429, 0, 49152};
    for (unsigned anti = 0; anti < 2u; ++anti)
    {
        CHECK(portable_mesh_kart_init(&s, &r, close, along) == PORTABLE_MESH_OK);
        s.speed_q16 = s.velocity_q16[0] = 10 * 65536;
        s.anti_gravity = (uint8_t)anti;
        CHECK(portable_mesh_kart_spin_bumper(&s, &r, bumper, 65536) == 1);
        CHECK(s.velocity_q16[0] == 0 && s.knock_q16[0] < 0 && s.position_q16[0] <= 32768 &&
              s.spin_ticks == MESH_KART_MK8_SPIN_TICKS_FAST && s.spin_armed == anti);
        const int32_t knocked = s.knock_q16[0];
        s.velocity_q16[0] = 10 * 65536;
        s.position_q16[0] = 52429;
        CHECK(portable_mesh_kart_spin_bumper(&s, &r, bumper, 65536) == 1 && s.knock_q16[0] == knocked &&
              s.velocity_q16[0] == 10 * 65536);
    }
    return 0;
}
/* Kart at 40 m flying +y at `units` units/frame over a glide path along +y
 * at 35 m (radius 10 m), the path's point kept beside the kart. */
static void glide_aloft(PortableMeshKartState *s, int32_t units)
{
    s->position_q16[2] = 40 * 65536;
    s->grounded = 0;
    s->contact_triangle = s->contact_surface = PORTABLE_MESH_ABSENT;
    memset(s->velocity_q16, 0, sizeof s->velocity_q16);
    s->velocity_q16[1] = (int32_t)((int64_t)units * MESH_KART_MK8_UNIT_SPEED_Q16 / 65536);
    s->speed_q16 = s->velocity_q16[1];
}
static void glide_beside(PortableMeshKartState *s, int32_t height_q16, uint8_t cannon)
{
    memset(&s->glide_path, 0, sizeof s->glide_path);
    s->glide_path.closest_q16[0] = s->position_q16[0];
    s->glide_path.closest_q16[1] = s->position_q16[1];
    s->glide_path.closest_q16[2] = height_q16;
    s->glide_path.tangent_q30[1] = s->glide_path.up_q30[2] = 1073741824;
    s->glide_path.radius_q16 = 10 * 65536;
    s->glide_path.found = 1;
    s->glide_path.cannon = cannon;
}
static int gliding(void)
{
    const PortableMeshGravityPoint a = {{0, 0, 0}, {0, 0, 1073741824}, 4 * 65536},
                                   b = {{10 * 65536, 0, 0}, {0, 0, 1073741824}, 8 * 65536};
    const int32_t past[3] = {15 * 65536, 3 * 65536, 0}, middle[3] = {5 * 65536, -65536, 0};
    uint64_t distance;
    int32_t closest[3], tangent[3], up[3], radius;
    CHECK(portable_mesh_glide_segment(&a, &b, past, &distance, closest, tangent, up, &radius) &&
          closest[0] == 10 * 65536 && distance == UINT64_C(34) << 32 && tangent[0] == 1073741824 &&
          up[2] == 1073741824 && radius == 8 * 65536);
    CHECK(portable_mesh_glide_segment(&a, &b, middle, &distance, closest, tangent, up, &radius) &&
          closest[0] == 5 * 65536 && distance == UINT64_C(1) << 32 && radius == 6 * 65536);

    World w;
    floor_world(&w);
    w.surfaces[0].glide = 1;
    PortableMeshKartRules r = test_mesh_rules();
    r.glide_speed_q16 = 25 * 65536; /* stock profiles: 17..24 m/s */
    PortableMeshCourse c = {&w, 2, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t p[3] = {0, 0, 49152}, ahead[3] = {0, 1073741824, 0}, behind[3] = {0, -1073741824, 0};
    PortableMeshKartState armed, s;
    PortableMeshKartEvents e;
    CHECK(portable_mesh_kart_init(&armed, &r, p, ahead) == PORTABLE_MESH_OK);
    for (unsigned tick = 0; tick < 3u; ++tick)
        CHECK(portable_mesh_kart_tick(&armed, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK);
    CHECK(armed.grounded && armed.glide == 1u);

    /* 15 airborne ticks facing the path open the glider with a lift. */
    s = armed;
    glide_aloft(&s, 8 * 65536);
    unsigned opened = 0;
    for (unsigned tick = 1; tick <= 20u && !opened; ++tick)
    {
        glide_beside(&s, 35 * 65536, 0);
        const int32_t falling = s.velocity_q16[2];
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK);
        if (e.glided)
        {
            opened = tick;
            CHECK(s.glide == 2u && s.velocity_q16[2] > falling);
        }
    }
    CHECK(opened == MESH_KART_MK8_GLIDE_AIR_TICKS + 2u);
    /* It holds altitude near the path; diving descends. */
    PortableMeshKartState dive = s;
    for (unsigned tick = 0; tick < 120u; ++tick)
    {
        glide_beside(&s, 35 * 65536, 0);
        glide_beside(&dive, 35 * 65536, 0);
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK &&
              s.glide == 2u && !e.glide_end);
        CHECK(portable_mesh_kart_tick(&dive, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_HOP}, &e) ==
                  PORTABLE_MESH_OK && dive.glide == 2u);
    }
    CHECK(s.position_q16[2] > 38 * 65536 && s.velocity_q16[1] > 0 && s.speed_q16 > 6 * MESH_KART_MK8_UNIT_SPEED_Q16 &&
          dive.position_q16[2] < s.position_q16[2] - 4 * 65536 && dive.glide_pitch_q16 > 32768);

    /* Far outside the path's tube the glide ends. */
    PortableMeshKartState lost = s;
    unsigned ended = 0;
    for (unsigned tick = 1; tick <= 20u && !ended; ++tick)
    {
        glide_beside(&lost, -200 * 65536, 0);
        CHECK(portable_mesh_kart_tick(&lost, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK);
        ended = e.glide_end ? tick : 0;
    }
    CHECK(ended >= 3u && !lost.glide);

    /* Settling onto ground past the grace lands it after a few ticks. */
    PortableMeshKartState landing = s;
    landing.position_q16[0] = landing.position_q16[1] = 0;
    landing.position_q16[2] = 65536;
    ended = 0;
    for (unsigned tick = 1; tick <= 60u && !ended; ++tick)
    {
        glide_beside(&landing, 0, 0);
        CHECK(portable_mesh_kart_tick(&landing, &r, &c, (PortableMeshKartInput){0, PORTABLE_MESH_HOP}, &e) ==
              PORTABLE_MESH_OK);
        ended = e.glide_end ? tick : 0;
    }
    CHECK(ended && !landing.glide && landing.grounded && landing.air_ticks == 0);

    /* A cannon path opens after 5 ticks and accelerates to 25 units/frame. */
    s = armed;
    glide_aloft(&s, 8 * 65536);
    opened = 0;
    for (unsigned tick = 1; tick <= 40u; ++tick)
    {
        glide_beside(&s, 40 * 65536, 1);
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK);
        opened = e.glided ? tick : opened;
    }
    CHECK(opened == MESH_KART_MK8_GLIDE_CANNON_AIR_TICKS + 2u && s.glide == 2u &&
          s.speed_q16 == (int32_t)((int64_t)MESH_KART_MK8_GLIDE_CANNON_TOP_Q16 * MESH_KART_MK8_UNIT_SPEED_Q16 / 65536));

    /* Facing away from the path disarms it instead. */
    s = armed;
    CHECK(portable_mesh_kart_init(&s, &r, p, behind) == PORTABLE_MESH_OK);
    s.glide = 1;
    glide_aloft(&s, -8 * 65536);
    for (unsigned tick = 1; tick <= 20u; ++tick)
    {
        glide_beside(&s, 35 * 65536, 0);
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0}, &e) == PORTABLE_MESH_OK && !e.glided);
    }
    CHECK(!s.glide);
    return 0;
}
static int hazard_knock(void)
{
    const PortableMeshKartRules rules = test_mesh_rules();
    PortableMeshKartState s;
    const int32_t p[3] = {0, 0, 65536}, f[3] = {1073741824, 0, 0};
    CHECK(portable_mesh_kart_init(&s, &rules, p, f) == PORTABLE_MESH_OK);
    s.velocity_q16[0] = 8 * 65536;
    s.drift_charge = 40;
    const int32_t far[3] = {10 * 65536, 0, 65536}, near[3] = {65536, 0, 65536};
    CHECK(portable_mesh_kart_hazard(&s, &rules, far, 65536) == 0 && s.velocity_q16[0] == 8 * 65536);
    CHECK(portable_mesh_kart_hazard(&s, &rules, near, 0) == -1);
    CHECK(portable_mesh_kart_hazard(&s, &rules, near, 65536) == 1);
    /* Out of the sphere; 4 m/s of closing (half of 8) cancelled and a 6 m/s knock out. */
    CHECK(near[0] - s.position_q16[0] == 65536 + rules.kart_radius_q16);
    CHECK(s.velocity_q16[0] == 0 && s.knock_q16[0] == -6 * 65536 &&
          s.traction_hold_ticks == MESH_KART_MK8_HAZARD_TICKS);
    CHECK(!s.drift_charge);
    /* A grounded kart under a tall hazard (a truck's box point above it) is
     * pushed out along the floor, never down through it. */
    PortableMeshKartState under;
    CHECK(portable_mesh_kart_init(&under, &rules, p, f) == PORTABLE_MESH_OK);
    under.grounded = 1;
    const int32_t above[3] = {16384, 0, 65536 + 49152};
    CHECK(portable_mesh_kart_hazard(&under, &rules, above, 32768) == 1);
    CHECK(under.position_q16[2] == p[2] && under.position_q16[0] < 0);
    s.cannon_ticks = 10;
    s.cannon_tick = 1;
    s.position_q16[0] = 65536;
    CHECK(portable_mesh_kart_hazard(&s, &rules, near, 65536) == 0);
    /* Reactions after the knock: a tumble throws the kart up, a squash stops it. */
    s.cannon_ticks = 0;
    s.cannon_tick = 0;
    PortableMeshKartState thrown = s;
    const int32_t rise = thrown.velocity_q16[2];
    CHECK(portable_mesh_kart_hazard_reaction(&thrown, PORTABLE_MESH_HAZARD_TUMBLE) == 0 &&
          thrown.velocity_q16[2] - rise == 4 * 65536 && !thrown.grounded);
    PortableMeshKartState flat = s;
    flat.velocity_q16[0] = 5 * 65536;
    CHECK(portable_mesh_kart_hazard_reaction(&flat, PORTABLE_MESH_HAZARD_SQUASH) == 0 &&
          !flat.velocity_q16[0] && !flat.speed_q16 && !flat.knock_q16[0]);
    PortableMeshKartState same = s;
    CHECK(portable_mesh_kart_hazard_reaction(&same, PORTABLE_MESH_HAZARD_SPIN) == 0 && !memcmp(&same, &s, sizeof s));
    CHECK(portable_mesh_kart_hazard_reaction(&same, PORTABLE_MESH_HAZARD_REACTIONS) == -1);
    return 0;
}
static int walls_tunnels_and_budget(void)
{
    World w;
    floor_world(&w);
    PortableMeshKartRules r = test_mesh_rules();
    PortableMeshKartState s;
    PortableMeshKartEvents e;
    const int32_t forward[3] = {0, -1073741824, 0};
    /* A vertical wall at x=1, an incoming 10m/s sphere: MK8 stops the
     * approach and knocks it out at 0.5x its 3.18 units/frame (5 m/s). */
    for (unsigned t = 0; t < 2u; ++t)
        for (unsigned i = 0; i < 3u; ++i)
        {
            int32_t *v = w.triangles[t].vertices_q16[i];
            const int32_t oldx = v[0];
            v[0] = 65536;
            v[2] = oldx;
        }
    PortableMeshCourse c = {&w, 2, 0, triangle, surface, NULL, NULL};
    const int32_t p[3] = {26214, 0, 65536};
    CHECK(portable_mesh_kart_init(&s, &r, p, forward) == PORTABLE_MESH_OK);
    s.velocity_q16[0] = 10 * 65536;
    CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 0}, &e) ==
          PORTABLE_MESH_OK);
    CHECK(e.wall && s.position_q16[0] <= 32769 && s.velocity_q16[0] == 0);
    CHECK(s.knock_q16[0] < -4 * 65536 - 62259 && s.knock_q16[0] > -5 * 65536 - 3277);
    CHECK(s.traction_hold_ticks == MESH_KART_MK8_WALL_KNOCK_TICKS &&
          s.wall_cooldown == MESH_KART_MK8_WALL_COOLDOWN - 1u && !s.wall_yaw_q24);
    floor_world(&w);
    w.count = 4;
    c.triangle_count = 4;
    c.gravity_q16 = 10 * 65536;
    for (unsigned t = 0; t < 2u; ++t)
    {
        w.triangles[t + 2u] = w.triangles[t];
        for (unsigned i = 0; i < 3u; ++i)
            w.triangles[t + 2u].vertices_q16[i][2] = 91750;
    }
    const int32_t floor_position[3] = {0, 0, 49152};
    CHECK(portable_mesh_kart_init(&s, &r, floor_position, forward) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 0}, &e) ==
              PORTABLE_MESH_OK &&
          s.grounded);
    CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 4}, &e) ==
          PORTABLE_MESH_OK);
    int hit_ceiling = 0, landed = 0;
    for (unsigned tick = 0; tick < 50u; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 0}, &e) ==
              PORTABLE_MESH_OK);
        CHECK(s.position_q16[2] <= 58984);
        hit_ceiling |= e.wall;
        landed |= e.landed;
    }
    CHECK(hit_ceiling && landed && s.grounded && s.position_q16[2] == 49152);
    /* Inconsistent clearance cannot be resolved by silently moving through either face. */
    for (unsigned t = 2; t < 4u; ++t)
        for (unsigned i = 0; i < 3u; ++i)
            w.triangles[t].vertices_q16[i][2] = 65536;
    CHECK(portable_mesh_kart_init(&s, &r, floor_position, forward) == PORTABLE_MESH_OK);
    uint8_t before[sizeof(s)];
    memcpy(before, &s, sizeof(s));
    CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 0}, &e) ==
          PORTABLE_MESH_COLLISION_BUDGET);
    CHECK(memcmp(before, &s, sizeof(s)) == 0);
    CHECK(portable_mesh_kart_init(&s, &r, floor_position, forward) == PORTABLE_MESH_OK);
    s.forward_q30[0] = 1073741824;
    memcpy(before, &s, sizeof(s));
    CHECK(portable_mesh_kart_tick(&s, &r, &c, (PortableMeshKartInput){0, 0}, &e) ==
              PORTABLE_MESH_INVALID &&
          memcmp(before, &s, sizeof(s)) == 0);
    return 0;
}
static int filtered_slope_transition(void)
{
    World w;
    floor_world(&w);
    w.count = 4;
    for (unsigned t = 0; t < 2u; ++t)
    {
        w.triangles[t + 2u] = w.triangles[t];
        for (unsigned i = 0; i < 3u; ++i)
        {
            int32_t *flat = w.triangles[t].vertices_q16[i],
                    *slope = w.triangles[t + 2u].vertices_q16[i];
            if (flat[0] > 0)
                flat[0] = 0;
            if (slope[0] < 0)
                slope[0] = 0;
            slope[2] = slope[0] / 2;
        }
    }
    PortableMeshKartRules rules = test_mesh_rules();
    rules.normal_alignment_response_q16 = 12 * 65536;
    const PortableMeshCourse course = {&w, 4, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t position[3] = {-65536, 0, 49152}, forward[3] = {1073741824, 0, 0};
    PortableMeshKartState state;
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_init(&state, &rules, position, forward) == PORTABLE_MESH_OK);
    int filtered_nonorthogonal = 0;
    for (unsigned tick = 0; tick < 80u; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 1},
                                      &events) == PORTABLE_MESH_OK);
        CHECK(portable_mesh_kart_state_valid(&state));
        const int64_t dot = portable_fixed_3d_dot(state.forward_q30, state.up_q30);
        filtered_nonorthogonal |= dot > (INT64_C(1) << 50u) || dot < -(INT64_C(1) << 50u);
    }
    CHECK(filtered_nonorthogonal && state.position_q16[0] > 65536 && state.position_q16[2] > 49152);
    return 0;
}
/* A climbing road made of faces that bend over a small convex crease, with
 * a hairline seam between them (imported DS prisms share no exact edges),
 * is ridden: the kart never leaves the floor for a tick between faces. */
static int faceted_crease_stays_grounded(void)
{
    World w;
    floor_world(&w);
    w.count = 4;
    for (unsigned t = 0; t < 2u; ++t)
    {
        w.triangles[t + 2u] = w.triangles[t];
        for (unsigned i = 0; i < 3u; ++i)
        {
            int32_t *steep = w.triangles[t].vertices_q16[i], *gentle = w.triangles[t + 2u].vertices_q16[i];
            if (steep[0] > 0)
                steep[0] = 0;
            if (gentle[0] < 1310)
                gentle[0] = 1310;
            steep[2] = (int32_t)((int64_t)steep[0] * 3 / 10);
            gentle[2] = gentle[0] / 4;
        }
    }
    const PortableMeshKartRules rules = test_mesh_rules();
    const PortableMeshCourse course = {&w, 4, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t position[3] = {-8 * 65536, 0, -108134}, forward[3] = {1073741824, 0, 0};
    PortableMeshKartState state;
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_init(&state, &rules, position, forward) == PORTABLE_MESH_OK);
    unsigned airborne = 0, crossed = 0;
    for (unsigned tick = 0; tick < 600u && state.position_q16[0] < 6 * 65536; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 1},
                                      &events) == PORTABLE_MESH_OK);
        if (tick >= 30u)
            airborne += !state.grounded;
        crossed |= state.position_q16[0] > 65536 && state.contact_triangle >= 2u;
    }
    CHECK(crossed && !airborne && state.speed_q16 > 4 * 65536);
    return 0;
}
static int concave_floor_joint(void)
{
    World world;
    floor_world(&world);
    world.count = 4;
    for (unsigned t = 0; t < 2u; ++t)
    {
        world.triangles[t + 2u] = world.triangles[t];
        for (unsigned i = 0; i < 3u; ++i)
        {
            int32_t *flat = world.triangles[t].vertices_q16[i];
            int32_t *ramp = world.triangles[t + 2u].vertices_q16[i];
            if (flat[0] > 0)
                flat[0] = 0;
            if (ramp[0] < 0)
                ramp[0] = 0;
            ramp[2] = ramp[0];
        }
    }
    PortableMeshKartRules rules = test_mesh_rules();
    rules.ground_snap_q16 = 16384;
    const PortableMeshCourse course = {&world, 4, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t position[3] = {-6554, 0, 49152}, forward[3] = {0, -1073741824, 0};
    PortableMeshKartState state;
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_init(&state, &rules, position, forward) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 0},
                                  &events) == PORTABLE_MESH_OK);
    /* Both planes are solid, but only the ramp touches the sphere after its outward push.
     * The lower flat face must not pull it back into the ramp. */
    CHECK(state.grounded && state.position_q16[2] > 52428 && state.position_q16[0] < -6554);
    CHECK(state.up_q30[0] < -758000000 && state.up_q30[2] > 758000000);
    const int64_t normal_distance =
        ((int64_t)state.position_q16[2] - state.position_q16[0]) * 759250125 / 1073741824;
    CHECK(normal_distance >= 49150 && normal_distance <= 49155);
    return 0;
}
static int longitudinal_acceleration_independent_of_grip(void)
{
    World world;
    floor_world(&world);
    PortableMeshKartRules rules = test_mesh_rules();
    rules.traction_on_q16 = 52429; /* 0.8 of slip kept a tick */
    PortableMeshCourse course = {&world, 2, 10 * 65536, triangle, surface, NULL, NULL};
    PortableMeshKartState state;
    PortableMeshKartEvents events;
    const int32_t position[3] = {0, 0, 49152}, forward[3] = {1073741824, 0, 0};
    CHECK(portable_mesh_kart_init(&state, &rules, position, forward) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 0},
                                  &events) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 1},
                                  &events) == PORTABLE_MESH_OK);
    CHECK(state.speed_q16 == portable_fixed_3d_dt(10 * 65536));
    /* On z=x/2 the kart climbs at its flat 10m/s^2 regardless of 12/s
     * lateral grip: MK8 grounded gravity pushes only into the floor. */
    for (unsigned t = 0; t < 2u; ++t)
        for (unsigned i = 0; i < 3u; ++i)
            world.triangles[t].vertices_q16[i][2] = world.triangles[t].vertices_q16[i][0] / 2;
    CHECK(portable_mesh_kart_init(&state, &rules, position, forward) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 0},
                                  &events) == PORTABLE_MESH_OK);
    const int32_t expected = portable_fixed_3d_dt(10 * 65536);
    for (unsigned tick = 0; tick < 30u; ++tick)
    {
        const int32_t before = state.speed_q16;
        CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 1},
                                      &events) == PORTABLE_MESH_OK);
        const int32_t increase = state.speed_q16 - before;
        /* Bound covers the Q30 normal and fixed-point projection roundings. */
        CHECK(increase >= expected - 16 && increase <= expected + 16 && state.grounded);
    }
    CHECK(state.speed_q16 > 2 * 65536 && state.position_q16[0] > 32768);
    return 0;
}
static int same_state(const PortableMeshKartState *a, const PortableMeshKartState *b)
{
    if (memcmp(a->position_q16, b->position_q16, sizeof a->position_q16) ||
        memcmp(a->velocity_q16, b->velocity_q16, sizeof a->velocity_q16) ||
        memcmp(a->forward_q30, b->forward_q30, sizeof a->forward_q30) ||
        memcmp(a->up_q30, b->up_q30, sizeof a->up_q30) ||
        memcmp(a->boost_ticks, b->boost_ticks, sizeof a->boost_ticks))
        return 0;
#define SAME(field) if (a->field != b->field) return 0
    SAME(steer_filtered_q16); SAME(speed_q16); SAME(outside_drift_q16);
    SAME(drift_charge); SAME(panel_ticks); SAME(air_ticks); SAME(respawn_ticks);
    SAME(contact_triangle); SAME(contact_surface); SAME(respawn_index);
    SAME(previous_buttons); SAME(drift_direction); SAME(drift_tier);
    SAME(grounded); SAME(last_boost_tier);
#undef SAME
    return 1;
}

static int effects_identity(void)
{
    const PortableMeshKartEffects identity = {.speed_ratio_q16 = 65536,
        .body_scale_q16 = 65536, .boost_cap_ratio_q16 = 65536};
    for (unsigned fixture = 0; fixture < 4u; ++fixture)
    {
        World world;
        floor_world(&world);
        const PortableMeshKartRules rules = test_mesh_rules();
        const PortableMeshCourse course = {&world,   world.count, 10 * 65536,
                                           triangle, surface,     NULL, NULL};
        if (fixture == 1u)
            for (unsigned t = 0; t < 2u; ++t)
                for (unsigned i = 0; i < 3u; ++i)
                    world.triangles[t].vertices_q16[i][2] = world.triangles[t].vertices_q16[i][0] / 2;
        if (fixture == 2u)
            world.surfaces[0].type = 4;
        const int32_t position[3] = {0, 0, fixture == 3u ? 10 * 65536 : 49152};
        const int32_t forward[3] = {0, -1073741824, 0};
        PortableMeshKartState original, enhanced;
        CHECK(portable_mesh_kart_init(&original, &rules, position, forward) == PORTABLE_MESH_OK);
        memcpy(&enhanced, &original, sizeof enhanced);
        unsigned char rules_before[sizeof rules];
        memcpy(rules_before, &rules, sizeof rules);
        for (unsigned tick = 0; tick < 24u; ++tick)
        {
            const PortableMeshKartInput input = {(int16_t)(tick % 3u ? 16000 : -8000),
                (uint16_t)(tick >= 10u && tick < 18u ? 5u : 1u)};
            PortableMeshKartEvents a, b;
            CHECK(portable_mesh_kart_tick(&original, &rules, &course, input, &a) == PORTABLE_MESH_OK);
            CHECK(portable_mesh_kart_tick_effects(&enhanced, &rules, &course, input, &identity, &b) ==
                  PORTABLE_MESH_OK);
            CHECK(same_state(&original, &enhanced));
            CHECK(a.hopped == b.hopped && a.landed == b.landed && a.tier_changed == b.tier_changed &&
                  a.released == b.released && a.panel == b.panel && a.wall == b.wall);
        }
        CHECK(memcmp(rules_before, &rules, sizeof rules) == 0);
    }
    return 0;
}

static int effects_dynamics(void)
{
    World world;
    floor_world(&world);
    const PortableMeshKartRules rules = test_mesh_rules();
    const PortableMeshCourse course = {&world, world.count, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t position[3] = {0, 0, 49152}, forward[3] = {0, -1073741824, 0};
    PortableMeshKartState state;
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_init(&state, &rules, position, forward) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
          PORTABLE_MESH_OK && state.grounded);
    const PortableMeshKartState initial = state;
    PortableMeshKartEffects effects = {.speed_ratio_q16 = 65536, .body_scale_q16 = 65536,
        .boost_cap_ratio_q16 = 3 * 65536, .boost_acceleration_q16 = 40 * 65536, .boost_active = 1};
    state.boost_ticks[1] = state.panel_ticks = 3;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 0},
        &effects, &events) == PORTABLE_MESH_OK);
    CHECK(state.speed_q16 == portable_fixed_3d_dt(40 * 65536) && state.boost_ticks[1] == 2 &&
          state.panel_ticks == 2);
    /* A live MK8 boost pulls toward its cap from either side; the mushroom's
     * scales the larger of top speed and 7.6 units/frame. */
    state.speed_q16 = 80 * 65536;
    state.velocity_q16[1] = -state.speed_q16;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 1},
        &effects, &events) == PORTABLE_MESH_OK);
    CHECK(state.speed_q16 == portable_fixed_3d_mul(MESH_KART_MK8_FLOOR_CAP_Q16, 3 * 65536));
    state.speed_q16 = 40 * 65536;
    state.velocity_q16[1] = -state.speed_q16;
    state.boost_ticks[1] = state.panel_ticks = 0;
    effects.boost_active = 0;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 1},
        &effects, &events) == PORTABLE_MESH_OK);
    /* MK8 bleeds 2% a tick above every cap instead of clamping. */
    const int32_t bled = portable_fixed_3d_mul(40 * 65536, MESH_KART_MK8_OVER_CAP_Q16);
    CHECK(state.speed_q16 == bled);
    effects.speed_ratio_q16 = 32768;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 1},
        &effects, &events) == PORTABLE_MESH_OK);
    CHECK(state.speed_q16 == portable_fixed_3d_mul(bled, MESH_KART_MK8_OVER_CAP_Q16));
    state = initial;
    state.speed_q16 = 8 * 65536;
    state.velocity_q16[1] = -state.speed_q16;
    effects.boost_active = 0;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 1},
        &effects, &events) == PORTABLE_MESH_OK &&
          state.speed_q16 == portable_fixed_3d_mul(8 * 65536, MESH_KART_MK8_OVER_CAP_Q16));
    state.speed_q16 = -3 * 65536;
    state.velocity_q16[1] = -state.speed_q16;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 2},
        &effects, &events) == PORTABLE_MESH_OK &&
          state.speed_q16 == portable_mesh_kart_mk8_drive(-3 * 65536,
              portable_fixed_3d_mul(rules.reverse_speed_q16, 32768), MESH_KART_MK8_BRAKE_RATE_Q16,
              MESH_KART_MK8_BRAKE_LIMIT_Q16, 65536, 65536, -1));
    state = initial;
    effects.speed_ratio_q16 = 65536;
    effects.body_scale_q16 = 32768;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 0},
        &effects, &events) == PORTABLE_MESH_OK);
    CHECK(state.grounded && state.position_q16[2] == 24576);
    /* An airborne boost does not invent an air-thrust path. */
    state = initial;
    state.grounded = 0;
    state.position_q16[2] = 10 * 65536;
    state.speed_q16 = 2 * 65536;
    state.velocity_q16[1] = -state.speed_q16;
    effects.boost_active = 1;
    effects.body_scale_q16 = 65536;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){0, 1},
        &effects, &events) == PORTABLE_MESH_OK);
    CHECK(!state.grounded && state.velocity_q16[1] == -2 * 65536 &&
          state.velocity_q16[2] == -portable_fixed_3d_dt(10 * 65536));
    state = initial;
    state.speed_q16 = 5 * 65536;
    state.velocity_q16[1] = -state.speed_q16;
    state.drift_direction = 1;
    state.drift_charge = rules.charge_thresholds[2];
    state.drift_tier = 3;
    state.boost_ticks[0] = state.boost_ticks[1] = state.boost_ticks[2] = state.panel_ticks = 3;
    effects.cancel_drift = effects.inhibit_controls = 1;
    world.surfaces[0].type = 4;
    CHECK(portable_mesh_kart_tick_effects(&state, &rules, &course, (PortableMeshKartInput){20000, 7},
        &effects, &events) == PORTABLE_MESH_OK);
    CHECK(!state.drift_charge && !state.drift_direction && !state.panel_ticks &&
          !state.boost_ticks[0] && !state.boost_ticks[1] && !state.boost_ticks[2] &&
          state.speed_q16 ==
              portable_fixed_3d_mul(5 * 65536, MESH_KART_MK8_COAST_Q16));
    CHECK(!events.hopped && !events.panel && !events.released && state.previous_buttons == 7 &&
          state.steer_filtered_q16 == 0 && state.forward_q30[0] == 0);
    CHECK(portable_mesh_kart_tick(&state, &rules, &course, (PortableMeshKartInput){0, 5}, &events) ==
          PORTABLE_MESH_OK && !events.hopped);
    return 0;
}

static int effects_pairs_and_rollback(void)
{
    World world;
    floor_world(&world);
    const PortableMeshKartRules rules = test_mesh_rules();
    const PortableMeshCourse course = {&world, world.count, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t ap[3] = {0, 0, 49152}, bp[3] = {32768, 0, 49152};
    const int32_t forward[3] = {0, -1073741824, 0};
    PortableMeshKartState a, b;
    CHECK(portable_mesh_kart_init(&a, &rules, ap, forward) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_init(&b, &rules, bp, forward) == PORTABLE_MESH_OK);
    const PortableMeshKartEffects small = {.speed_ratio_q16 = 65536,
        .body_scale_q16 = 32768, .boost_cap_ratio_q16 = 65536};
    CHECK(portable_mesh_karts_contact_effects(&a, &rules, &small, &b, &rules, NULL) == PORTABLE_MESH_OK);
    CHECK(b.position_q16[0] - a.position_q16[0] == 49152);
    unsigned char a_before[sizeof a], b_before[sizeof b];
    memcpy(a_before, &a, sizeof a);
    memcpy(b_before, &b, sizeof b);
    PortableMeshKartEffects bad = small;
    bad.speed_ratio_q16 = 0;
    PortableMeshKartEvents events;
    memset(&events, 0xa5, sizeof events);
    unsigned char events_before[sizeof events];
    memcpy(events_before, &events, sizeof events);
    CHECK(portable_mesh_kart_tick_effects(&a, &rules, &course, (PortableMeshKartInput){0, 1},
        &bad, &events) == PORTABLE_MESH_INVALID);
    CHECK(memcmp(a_before, &a, sizeof a) == 0 && memcmp(events_before, &events, sizeof events) == 0);
    CHECK(portable_mesh_karts_contact_effects(&a, &rules, &small, &b, &rules, &bad) == PORTABLE_MESH_INVALID);
    CHECK(memcmp(a_before, &a, sizeof a) == 0 && memcmp(b_before, &b, sizeof b) == 0);
    PortableMeshKartRules fast = rules;
    fast.forward_speed_q16 = 100 * 65536;
    bad = small;
    bad.boost_active = 1;
    bad.boost_cap_ratio_q16 = 3 * 65536;
    CHECK(!portable_mesh_kart_effects_valid(&fast, &bad));
    bad = small;
    bad.body_scale_q16 = 3276;
    CHECK(!portable_mesh_kart_effects_valid(&rules, &bad));
    bad = small;
    bad.boost_acceleration_q16 = 1024 * 65536 + 1;
    CHECK(!portable_mesh_kart_effects_valid(&rules, &bad));
    return 0;
}

static int automatic_launch_traction(void)
{
    World world;
    floor_world(&world);
    PortableMeshKartRules rules = test_mesh_rules();
    PortableMeshCourse course = {&world, world.count, 10 * 65536, triangle, surface, NULL, NULL};
    world.surfaces[0].type = 5;
    world.surfaces[0].launch_q16 = 3 * 65536;
    const int32_t position[3] = {0, 0, rules.kart_radius_q16 + rules.ride_height_q16};
    const int32_t forward[3] = {0, -1073741824, 0};
    PortableMeshKartState initial;
    CHECK(portable_mesh_kart_init(&initial, &rules, position, forward) == PORTABLE_MESH_OK);
    initial.grounded = 1;
    initial.contact_surface = 0;
    PortableMeshKartState driven;
    memcpy(&driven, &initial, sizeof driven);
    PortableMeshKartEvents events;
    const int32_t acceleration_tick = portable_fixed_3d_dt(10 * 65536);
    CHECK(portable_mesh_kart_tick(&driven, &rules, &course,
        (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events) == PORTABLE_MESH_OK);
    CHECK(events.hopped && !driven.grounded && driven.air_ticks == 1);
    CHECK(driven.speed_q16 == acceleration_tick && driven.velocity_q16[0] == 0 &&
        driven.velocity_q16[1] == -acceleration_tick);
    CHECK(driven.velocity_q16[2] == 3 * 65536 - portable_fixed_3d_dt(course.gravity_q16));
    CHECK(driven.position_q16[1] == -portable_fixed_3d_dt(acceleration_tick));

    /* Subsequent air turns cannot borrow the launch surface's traction. */
    for (unsigned i = 0; i < 3; ++i)
    {
        CHECK(portable_mesh_kart_tick(&driven, &rules, &course,
            (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events) == PORTABLE_MESH_OK);
        CHECK(!events.hopped && !driven.grounded && driven.speed_q16 == acceleration_tick &&
            driven.velocity_q16[1] == -acceleration_tick);
    }
    PortableMeshKartState neutral;
    memcpy(&neutral, &initial, sizeof neutral);
    CHECK(portable_mesh_kart_tick(&neutral, &rules, &course,
        (PortableMeshKartInput){0}, &events) == PORTABLE_MESH_OK);
    CHECK(events.hopped && !neutral.grounded && !neutral.speed_q16 &&
        !neutral.velocity_q16[0] && !neutral.velocity_q16[1]);

    /* Supported friction and an existing boost still apply before takeoff. */
    memcpy(&driven, &initial, sizeof driven);
    driven.velocity_q16[0] = 65536;
    driven.boost_ticks[0] = 2;
    world.surfaces[0].rolling_resistance_q16 = 65536;
    CHECK(portable_mesh_kart_tick(&driven, &rules, &course,
        (PortableMeshKartInput){0}, &events) == PORTABLE_MESH_OK);
    const int32_t boost_step = portable_fixed_3d_dt(rules.boost_acceleration_q16[0]) -
        portable_fixed_3d_dt(world.surfaces[0].rolling_resistance_q16);
    CHECK(events.hopped && driven.boost_ticks[0] == 1 && driven.last_boost_tier == 1 &&
        driven.speed_q16 == boost_step && /* MK8 slip keeps traction T of the sideways speed */
        driven.velocity_q16[0] == portable_fixed_3d_mul(65536, rules.traction_on_q16) &&
        driven.velocity_q16[1] == -boost_step &&
        driven.velocity_q16[2] == 3 * 65536 - portable_fixed_3d_dt(course.gravity_q16));

    /* A fresh manual hop bypasses ground traction exactly as before, even
     * when the underlying floor also has an automatic launch. */
    PortableMeshKartState manual_jump, manual_road;
    memcpy(&manual_jump, &initial, sizeof manual_jump);
    memcpy(&manual_road, &initial, sizeof manual_road);
    PortableMeshKartEvents jump_events, road_events;
    const PortableMeshKartInput hop = {0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP};
    CHECK(portable_mesh_kart_tick(&manual_jump, &rules, &course, hop, &jump_events) == PORTABLE_MESH_OK);
    world.surfaces[0].type = 0;
    world.surfaces[0].launch_q16 = 0;
    CHECK(portable_mesh_kart_tick(&manual_road, &rules, &course, hop, &road_events) == PORTABLE_MESH_OK);
    CHECK(!memcmp(&manual_jump, &manual_road, sizeof manual_jump) &&
        !memcmp(&jump_events, &road_events, sizeof jump_events));
    CHECK(jump_events.hopped && !manual_jump.velocity_q16[1] && !manual_jump.speed_q16);
    return 0;
}

static int jump_distance_ceiling(void)
{
    World world;
    floor_world(&world);
    PortableMeshKartRules rules=test_mesh_rules();
    rules.coast_deceleration_q16=0;
    rules.traction_on_q16=65536; /* slip kept (capped at 0.99) */
    PortableMeshCourse course={&world,world.count,10*65536,triangle,surface,NULL, NULL};
    world.surfaces[0].type=5;
    world.surfaces[0].launch_q16=9*65536;
    const int32_t position[3]={0,0,rules.kart_radius_q16+rules.ride_height_q16};
    const int32_t forward[3]={0,-1073741824,0};
    /* Actual horizontal velocity is about a3/4/5 vector, distinct from
     * longitudinal scalar speed4; the ceiling g*4/(2*|v|) is about4m/s. */
    const int32_t velocities[4][2]={{3*65536,-4*65536},{6*65536,-8*65536},
                                    {0,-65536},{0,0}};
    for (unsigned i=0;i<4;++i)
    {
        PortableMeshKartState kart;
        CHECK(portable_mesh_kart_init(&kart,&rules,position,forward)==PORTABLE_MESH_OK);
        kart.grounded=1;
        kart.contact_surface=0;
        kart.speed_q16=-velocities[i][1];
        kart.velocity_q16[0]=velocities[i][0];
        kart.velocity_q16[1]=velocities[i][1];
        world.surfaces[0].jump_max_distance_q16=4*65536;
        /* Holding A exactly at the cap keeps MK8's speed unchanged. */
        if (kart.speed_q16)
            rules.forward_speed_q16=kart.speed_q16;
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(&kart,&rules,&course,(PortableMeshKartInput){0,kart.speed_q16?PORTABLE_MESH_ACCELERATE:0},&events)==PORTABLE_MESH_OK);
        CHECK(events.hopped && !kart.grounded);
        /* MK8 slip keeps at most 0.99 of the sideways speed before the launch. */
        const int32_t kept=portable_fixed_3d_mul(velocities[i][0],MESH_KART_MK8_TRACTION_CAP_Q16);
        const uint64_t horizontal=portable_fixed_3d_sqrt((uint64_t)((int64_t)kept*kept+
            (int64_t)velocities[i][1]*velocities[i][1]));
        int64_t impulse=world.surfaces[0].launch_q16;
        if (horizontal && (int64_t)course.gravity_q16*4*65536/(2*(int64_t)horizontal)<impulse)
            impulse=(int64_t)course.gravity_q16*4*65536/(2*(int64_t)horizontal);
        CHECK(kart.velocity_q16[0]==kept && kart.velocity_q16[1]==velocities[i][1]);
        CHECK(kart.velocity_q16[2]==impulse-portable_fixed_3d_dt(course.gravity_q16));
    }
    PortableMeshKartState initial,legacy,bounded;
    CHECK(portable_mesh_kart_init(&initial,&rules,position,forward)==PORTABLE_MESH_OK);
    initial.grounded=1;
    initial.contact_surface=0;
    initial.velocity_q16[1]=-65536;
    initial.speed_q16=65536;
    /* Nonbinding ceiling is byte-identical; it never supplies a minimum boost. */
    memcpy(&legacy,&initial,sizeof legacy);
    world.surfaces[0].jump_max_distance_q16=0;
    PortableMeshKartEvents old_events,new_events;
    CHECK(portable_mesh_kart_tick(&legacy,&rules,&course,(PortableMeshKartInput){0},&old_events)==PORTABLE_MESH_OK);
    memcpy(&bounded,&initial,sizeof bounded);
    world.surfaces[0].jump_max_distance_q16=1024*65536;
    CHECK(portable_mesh_kart_tick(&bounded,&rules,&course,(PortableMeshKartInput){0},&new_events)==PORTABLE_MESH_OK);
    CHECK(!memcmp(&legacy,&bounded,sizeof legacy) && !memcmp(&old_events,&new_events,sizeof old_events));
    course.gravity_q16=0;
    memcpy(&bounded,&initial,sizeof bounded);
    world.surfaces[0].jump_max_distance_q16=65536;
    CHECK(portable_mesh_kart_tick(&bounded,&rules,&course,(PortableMeshKartInput){0},&new_events)==PORTABLE_MESH_OK);
    CHECK(bounded.velocity_q16[2]==9*65536);
    course.gravity_q16=10*65536;
    for (unsigned bad=0;bad<5;++bad)
    {
        world.surfaces[0].jump_max_distance_q16=bad==0?-1:bad==1?1024*65536+1:4*65536;
        world.surfaces[0].type=bad==2?0:5;
        world.surfaces[0].gravity_ratio_q16=bad==3?2*65536:65536;
        memcpy(&bounded,&initial,sizeof bounded);
        if (bad==4)
        {
            bounded.grounded=0;
            bounded.contact_surface=PORTABLE_MESH_ABSENT;
            world.surfaces[0].jump_max_distance_q16=-1; /* Late support query. */
        }
        unsigned char before[sizeof bounded],events_before[sizeof new_events];
        memcpy(before,&bounded,sizeof before);
        memset(&new_events,0xa5,sizeof new_events);
        memcpy(events_before,&new_events,sizeof events_before);
        CHECK(portable_mesh_kart_tick(&bounded,&rules,&course,(PortableMeshKartInput){0},&new_events)==PORTABLE_MESH_INVALID);
        CHECK(!memcmp(before,&bounded,sizeof before) && !memcmp(events_before,&new_events,sizeof events_before));
    }
    return 0;
}

/* These outcomes deliberately use the compiled shipping profile, rather than
 * the small 10m/s synthetic rules used by the older contact unit tests. */
static int production_settle(const PortableMeshKartRules *rules, PortableMeshCourse *course,
                             PortableMeshKartState *kart)
{
    const int32_t position[3] = {0, 0, rules->kart_radius_q16 + rules->ride_height_q16};
    const int32_t forward[3] = {1073741824, 0, 0};
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_init(kart, rules, position, forward) == PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(kart, rules, course, (PortableMeshKartInput){0}, &events) ==
          PORTABLE_MESH_OK);
    CHECK(kart->grounded && kart->contact_triangle < course->triangle_count);
    return 0;
}

static int coast_and_travel_regressions(void)
{
    World world;
    floor_world(&world);
    PortableMeshCourse course = {&world, world.count, 0, triangle, surface, NULL, NULL};
    PortableMeshKartRules rules = test_mesh_rules();
    /* MK8 coasting keeps 98% (forward) or 95% (reverse) of the speed a tick
     * and truncates toward zero, so a released kart always comes to rest. */
    const int32_t start = portable_fixed_3d_dt(21 * 65536);
    for (int sign = -1; sign <= 1; sign += 2)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(&rules, &course, &kart) == 0);
        kart.speed_q16 = sign * start;
        kart.velocity_q16[0] = kart.speed_q16;
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0},
                                      &events) == PORTABLE_MESH_OK);
        CHECK(kart.speed_q16 == portable_fixed_3d_mul(sign * start, sign > 0 ? 64225 : 62259));
        for (unsigned tick = 0; tick < 900; ++tick)
            CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0},
                                          &events) == PORTABLE_MESH_OK);
        CHECK(!kart.speed_q16 && !kart.velocity_q16[0]);
        const int32_t stopped = kart.position_q16[0];
        CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0},
                                      &events) == PORTABLE_MESH_OK);
        CHECK(kart.position_q16[0] == stopped);
    }
    for (unsigned boost = 0; boost < 3; ++boost)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(&rules, &course, &kart) == 0);
        kart.speed_q16 = kart.velocity_q16[0] = -1;
        PortableMeshKartEffects effects = {.speed_ratio_q16 = 65536,
                                           .body_scale_q16 = 65536,
                                           .boost_cap_ratio_q16 = 65536,
                                           .boost_acceleration_q16 = 2 * 65536,
                                           .boost_active = 1};
        if (boost == 0)
            kart.boost_ticks[0] = 3;
        if (boost == 1)
            kart.panel_ticks = 3;
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick_effects(&kart, &rules, &course, (PortableMeshKartInput){0},
                                              boost == 2 ? &effects : NULL,
                                              &events) == PORTABLE_MESH_OK);
        CHECK(kart.speed_q16 > 0); /* Neutral coast cannot swallow forced acceleration. */
    }
    PortableMeshKartState kart;
    CHECK(production_settle(&rules, &course, &kart) == 0);
    kart.speed_q16 = kart.velocity_q16[0] = 1;
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_tick(&kart, &rules, &course,
                                  (PortableMeshKartInput){0, PORTABLE_MESH_BRAKE},
                                  &events) == PORTABLE_MESH_OK);
    CHECK(kart.speed_q16 < 0);
    kart.speed_q16 = kart.velocity_q16[0] = -1;
    CHECK(portable_mesh_kart_tick(
              &kart, &rules, &course,
              (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_BRAKE},
              &events) == PORTABLE_MESH_OK);
    /* Both pedals act: throttle first, then the independent reverse target. */
    const int32_t both = portable_mesh_kart_mk8_drive(
        -1 + portable_fixed_3d_dt(rules.acceleration_limit_q16), rules.reverse_speed_q16,
        MESH_KART_MK8_BRAKE_RATE_Q16, MESH_KART_MK8_BRAKE_LIMIT_Q16, 65536, 65536, -1);
    CHECK(kart.speed_q16 == both);
    kart.speed_q16 = kart.velocity_q16[0] = -65536;
    CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0}, &events) ==
          PORTABLE_MESH_OK);
    CHECK(kart.speed_q16 == -62259); /* Reverse coasting keeps 95%. */

    CHECK(production_settle(&rules, &course, &kart) == 0);
    rules.outside_drift_angle_q16 = 5461;
    rules.outside_drift_recovery_q16 = 0;
    kart.drift_direction = 1;
    kart.outside_drift_q16 = 5461;
    kart.previous_buttons = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP;
    kart.speed_q16 = 10 * 65536;
    int32_t travel[3];
    CHECK(portable_mesh_kart_travel_direction(&kart, travel) == PORTABLE_MESH_OK);
    for (unsigned axis = 0; axis < 3; ++axis)
        kart.velocity_q16[axis] = (int32_t)((int64_t)travel[axis] * kart.speed_q16 / 1073741824);
    for (unsigned tick = 0; tick < 60; ++tick)
    {
        CHECK(portable_mesh_kart_tick(
                  &kart, &rules, &course,
                  (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                  &events) == PORTABLE_MESH_OK);
        /* MK8 slip dips speed once as the drift settles; it recovers. */
        CHECK(kart.grounded && kart.speed_q16 >= 10 * 65536 * 97 / 100);
    }
    /* A neutral held drift still arcs toward its side. */
    CHECK(kart.forward_q30[1] > 268435456);
    CHECK(portable_mesh_kart_travel_direction(&kart, travel) == PORTABLE_MESH_OK);
    PortableMeshKartState a = kart, b = kart;
    /* Side by side across the drift's travel, as the pair passes. */
    const int32_t across[2] = {-travel[1], travel[0]};
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        a.position_q16[axis] -= (int32_t)((int64_t)across[axis] * 16384 / 1073741824);
        b.position_q16[axis] += (int32_t)((int64_t)across[axis] * 16384 / 1073741824);
    }
    a.speed_q16 = 8 * 65536;
    b.speed_q16 = -8 * 65536;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        a.velocity_q16[axis] = (int32_t)((int64_t)travel[axis] * a.speed_q16 / 1073741824);
        b.velocity_q16[axis] = (int32_t)((int64_t)travel[axis] * b.speed_q16 / 1073741824);
    }
    CHECK(portable_mesh_karts_contact(&a, &rules, &b, &rules) == PORTABLE_MESH_OK);
    CHECK(a.speed_q16 > 65536 && b.speed_q16 < -65536);
    kart.outside_drift_q16 = 0;
    CHECK(portable_mesh_kart_travel_direction(&kart, travel) == PORTABLE_MESH_OK);
    CHECK(!memcmp(travel, kart.forward_q30, sizeof travel));
    kart.forward_q30[0] = kart.forward_q30[1] = 759250125;
    kart.forward_q30[2] = 0;
    kart.velocity_q16[0] = kart.velocity_q16[1] = 200 * 65536;
    int32_t projection = 12345;
    CHECK(portable_mesh_kart_project_travel_speed(&kart, &projection) == PORTABLE_MESH_INVALID &&
          projection == 12345);
    return 0;
}

static int production_hop(const PortableMeshKartRules *loaded, int32_t gravity)
{
    for (unsigned radius = 0; radius < 2; ++radius)
    {
        unsigned landings[2] = {0, 0};
        for (unsigned snap = 0; snap < 2; ++snap)
        {
            World world;
            floor_world(&world);
            PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
            PortableMeshKartRules rules = *loaded;
            if (radius)
                rules.kart_radius_q16 = 16384;
            if (snap)
                rules.ground_snap_q16 = 0;
            PortableMeshKartState kart;
            CHECK(production_settle(&rules, &course, &kart) == 0);
            const int32_t rest = kart.position_q16[2];
            int32_t peak = 0;
            unsigned hopped = 0, landed = 0, airborne = 0;
            for (unsigned tick = 1; tick <= 45; ++tick)
            {
                PortableMeshKartEvents events;
                CHECK(portable_mesh_kart_tick(&kart, &rules, &course,
                                              (PortableMeshKartInput){0, PORTABLE_MESH_HOP},
                                              &events) == PORTABLE_MESH_OK);
                hopped += events.hopped != 0;
                if (!kart.grounded)
                    ++airborne;
                if (kart.position_q16[2] - rest > peak)
                    peak = kart.position_q16[2] - rest;
                if (events.landed)
                {
                    CHECK(!landed);
                    landed = tick;
                    CHECK(kart.contact_triangle < course.triangle_count);
                }
                if (landed)
                    CHECK(kart.grounded && !events.hopped);
            }
            CHECK(hopped == 1 && landed >= 13 && landed <= 16); /* MK8: 1.1 u/f at 0.15 u/f^2. */
            CHECK(airborne + 1 == landed);
            CHECK(peak >= 11141 && peak <= 14418); /* .17.. .22 metres. */
            CHECK(abs(kart.position_q16[2] - rest) <= 2);
            landings[snap] = landed;
            printf("Production hop: radius=%d snap=%d landing=%u air=%u apex_q16=%d\n",
                   rules.kart_radius_q16, rules.ground_snap_q16, landed, airborne, peak);
        }
        CHECK(landings[1] >= landings[0] && landings[1] <= landings[0] + 2);
    }
    return 0;
}

static int production_fall(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    floor_world(&world);
    for (unsigned t = 0; t < world.count; ++t)
        for (unsigned v = 0; v < 3; ++v)
            world.triangles[t].vertices_q16[v][2] = -1000 * 65536;
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    const int32_t position[3] = {0, 0, 100 * 65536}, forward[3] = {1073741824, 0, 0};
    PortableMeshKartState kart;
    CHECK(portable_mesh_kart_init(&kart, rules, position, forward) == PORTABLE_MESH_OK);
    int32_t expected_velocity = 0, expected_z = position[2];
    unsigned capped = 0;
    for (unsigned tick = 0; tick < 120; ++tick)
    {
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){0}, &events) ==
              PORTABLE_MESH_OK);
        expected_velocity -= portable_fixed_3d_dt(gravity);
        if (expected_velocity < -rules->terminal_fall_speed_q16)
        {
            expected_velocity = -rules->terminal_fall_speed_q16;
            ++capped;
        }
        expected_z += portable_fixed_3d_dt(expected_velocity);
        CHECK(kart.velocity_q16[2] == expected_velocity && kart.position_q16[2] == expected_z);
        CHECK(!kart.grounded && !events.hopped && !events.landed);
    }
    CHECK(capped > 10 && kart.velocity_q16[2] == -36 * 65536);
    floor_world(&world);
    int32_t landing_position[3] = {0, 0,
                                   rules->kart_radius_q16 + rules->ride_height_q16 + 2 * 65536};
    CHECK(portable_mesh_kart_init(&kart, rules, landing_position, forward) == PORTABLE_MESH_OK);
    unsigned landed = 0;
    for (unsigned tick = 1; tick < 60; ++tick)
    {
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){0}, &events) ==
              PORTABLE_MESH_OK);
        CHECK(!events.hopped);
        if (events.landed)
        {
            landed = tick;
            break;
        }
    }
    CHECK(landed >= 20 && landed <= 30 && kart.grounded && kart.contact_triangle < world.count);
    CHECK(abs(kart.position_q16[2] - rules->kart_radius_q16 - rules->ride_height_q16) <= 2);
    printf("Production fall: two_metres_landing=%u terminal_q16=%d\n", landed, -36 * 65536);
    return 0;
}

static int production_departure(const PortableMeshKartRules *loaded, int32_t gravity)
{
    for (unsigned ramp = 0; ramp < 2; ++ramp)
    {
        World world;
        floor_world(&world);
        world.count = 4;
        for (unsigned t = 0; t < 2; ++t)
        {
            world.triangles[t + 2] = world.triangles[t];
            for (unsigned v = 0; v < 3; ++v)
            {
                int32_t *left = world.triangles[t].vertices_q16[v];
                int32_t *right = world.triangles[t + 2].vertices_q16[v];
                if (left[0] > 0)
                    left[0] = 0;
                left[2] = ramp ? left[0] / 2 : 0;
                if (right[0] < 0)
                    right[0] = 0;
                right[2] = -65536;
            }
        }
        PortableMeshKartRules rules = *loaded;
        rules.coast_deceleration_q16 = 0; /* Preserve the authored approach speed. */
        PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
        PortableMeshKartState kart;
        const int32_t position[3] = {-65536, 0, rules.kart_radius_q16 + rules.ride_height_q16};
        const int32_t forward[3] = {1073741824, 0, 0};
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_init(&kart, &rules, position, forward) == PORTABLE_MESH_OK);
        for (unsigned tick = 0; tick < 20; ++tick)
            CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0},
                                          &events) == PORTABLE_MESH_OK);
        CHECK(kart.grounded && kart.contact_triangle < 2);
        kart.speed_q16 = (ramp ? 8 : 4) * 65536;
        for (unsigned a = 0; a < 3; ++a)
            kart.velocity_q16[a] =
                (int32_t)((int64_t)kart.forward_q30[a] * kart.speed_q16 / 1073741824);
        unsigned unsupported = 0, landed = 0;
        int32_t first_air_vz = 0;
        for (unsigned tick = 1; tick <= 90; ++tick)
        {
            CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0},
                                          &events) == PORTABLE_MESH_OK);
            CHECK(!events.hopped); /* An ordinary road never adds a panel impulse. */
            if (!kart.grounded)
            {
                if (!unsupported)
                    first_air_vz = kart.velocity_q16[2];
                ++unsupported;
            }
            if (unsupported && events.landed)
            {
                landed = tick;
                CHECK(kart.contact_triangle >= 2 && kart.contact_triangle < 4);
                break;
            }
        }
        CHECK(unsupported >= 3 && landed && landed <= 90);
        if (ramp)
            CHECK(first_air_vz > 0);
        else
            CHECK(first_air_vz < 0);
        printf("Production departure: ramp=%u air=%u landing=%u first_vz=%d\n", ramp, unsupported,
               landed, first_air_vz);
    }
    return 0;
}

static int production_panels(const PortableMeshKartRules *loaded, int32_t gravity)
{
    /* Independent Q16 pins for the converter's flight-preserving sqrt(23.3766/9.8)*g/23.3766
     * adaptation. Keep explicit authored 3/6/9m/s behavior covered separately. */
    static const int32_t impulses[6] = {3 * 65536, 6 * 65536, 9 * 65536,
                                       365834, 731668, 1097502};
    static const unsigned earliest[6] = {11, 23, 36, 22, 45, 69}; /* 2v/g at MK8 gravity */
    static const unsigned latest[6] = {15, 28, 41, 26, 49, 73};
    unsigned uncapped_landings[6] = {0};
    int32_t uncapped_ranges[6] = {0};
    for (unsigned launch = 0; launch < 6; ++launch)
        for (unsigned capped = 0; capped < 2; ++capped)
        {
            World world;
            floor_world(&world);
            world.surfaces[0].type = 5;
            world.surfaces[0].launch_q16 = impulses[launch];
            world.surfaces[0].jump_max_distance_q16 = capped ? 65536 : 0;
            PortableMeshKartRules rules = *loaded;
            rules.forward_speed_q16 = 4 * 65536; /* A at the cap holds the speed. */
            PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
            PortableMeshKartState kart;
            CHECK(production_settle(&rules, &course, &kart) == 0);
            kart.speed_q16 = 4 * 65536;
            kart.velocity_q16[0] = 4 * 65536;
            const int32_t rest = kart.position_q16[2];
            unsigned hops = 0, landing = 0;
            int32_t peak = 0;
            for (unsigned tick = 1; tick < 100; ++tick)
            {
                PortableMeshKartEvents events;
                CHECK(portable_mesh_kart_tick(&kart, &rules, &course,
                                              (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                              &events) == PORTABLE_MESH_OK);
                hops += events.hopped != 0;
                if (kart.position_q16[2] - rest > peak)
                    peak = kart.position_q16[2] - rest;
                /* MK8 drags only karts that have flown 20 ticks. */
                CHECK(tick <= MESH_KART_MK8_AIR_TICKS ? kart.velocity_q16[0] == 4 * 65536
                                                      : kart.velocity_q16[0] < 4 * 65536);
                if (events.landed)
                {
                    landing = tick;
                    break;
                }
            }
            CHECK(hops == 1 && landing && kart.grounded && peak > 0);
            const unsigned pair = launch;
            if (!capped)
            {
                uncapped_landings[pair] = landing;
                uncapped_ranges[pair] = kart.position_q16[0];
            }
            else
            {
                /* A 1 m ceiling no longer binds a 3 m/s launch at MK8 gravity. */
                CHECK(launch ? landing < uncapped_landings[pair] : landing == uncapped_landings[pair]);
                CHECK(launch ? kart.position_q16[0] < uncapped_ranges[pair]
                             : kart.position_q16[0] == uncapped_ranges[pair]);
            }
            if (capped)
                CHECK(kart.position_q16[0] <= 65536 + 6554);
            if (!capped)
                CHECK(landing >= earliest[launch] && landing <= latest[launch]);
            if (capped)
                CHECK(landing <= 16);
            printf("Production panel: launch_q16=%d cap=%u landing=%u apex_q16=%d range_q16=%d\n",
                   impulses[launch], capped, landing, peak, kart.position_q16[0]);
        }
    return 0;
}

static void production_wide_floor(World *world)
{
    floor_world(world);
    for (unsigned t = 0; t < 2; ++t)
        for (unsigned v = 0; v < 3; ++v)
            for (unsigned axis = 0; axis < 2; ++axis)
                world->triangles[t].vertices_q16[v][axis] *= 40;
}
static int production_speed_metrics(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    PortableMeshKartState kart;
    CHECK(production_settle(rules, &course, &kart) == 0);
    /* MK8 closes rate*(cap-v) of the gap a tick, limited to the
     * acceleration stat: a linear launch, then an asymptotic approach. */
    const int32_t cap = rules->forward_speed_q16;
    unsigned milestones[3] = {0}, top = 0;
    const int32_t thresholds[3] = {6 * 65536, 12 * 65536, 18 * 65536};
    for (unsigned tick = 1; tick <= 600 && !top; ++tick)
    {
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                      (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                      &events) == PORTABLE_MESH_OK);
        CHECK(kart.grounded && kart.speed_q16 <= cap);
        for (unsigned i = 0; i < 3; ++i)
            if (!milestones[i] && kart.speed_q16 >= thresholds[i])
                milestones[i] = tick;
        if (kart.speed_q16 >= cap - cap / 100)
            top = tick;
    }
    CHECK(milestones[0] >= 25u && milestones[0] <= 30u &&
          top >= 410u && top <= 445u && milestones[1] < milestones[2]);
    PortableMeshKartState coast = kart, brake = kart;
    unsigned half = 0, stop = 0, braked = 0;
    for (unsigned tick = 1; tick <= 900; ++tick)
    {
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(&coast, rules, &course, (PortableMeshKartInput){0},
                                      &events) == PORTABLE_MESH_OK);
        CHECK(coast.speed_q16 >= 0);
        if (!half && coast.speed_q16 <= cap / 2)
            half = tick;
        if (!stop && !coast.speed_q16)
            stop = tick;
        if (!braked)
        {
            CHECK(portable_mesh_kart_tick(&brake, rules, &course,
                                          (PortableMeshKartInput){0, PORTABLE_MESH_BRAKE},
                                          &events) == PORTABLE_MESH_OK);
            if (brake.speed_q16 <= 0)
                braked = tick;
        }
    }
    printf("Production handling: accel6/12/18/99%%=%u/%u/%u/%u brake=%u coast_half/stop=%u/%u\n",
           milestones[0], milestones[1], milestones[2], top, braked, half, stop);
    CHECK(half >= 33u && half <= 35u && stop && braked >= 72u &&
          braked <= 80u);
    CHECK(!coast.speed_q16 && !coast.velocity_q16[0]);
    return 0;
}
static int production_circles_and_release(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    for (unsigned drift = 0; drift < 2; ++drift)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(rules, &course, &kart) == 0);
        kart.speed_q16 = rules->forward_speed_q16;
        kart.steer_filtered_q16 = 65534;
        kart.previous_buttons = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP;
        if (drift)
        {
            kart.drift_direction = 1;
            kart.outside_drift_q16 = rules->outside_drift_angle_q16;
        }
        int32_t travel[3];
        CHECK(portable_mesh_kart_travel_direction(&kart, travel) == PORTABLE_MESH_OK);
        for (unsigned axis = 0; axis < 3; ++axis)
            kart.velocity_q16[axis] =
                (int32_t)((int64_t)travel[axis] * kart.speed_q16 / 1073741824);
        const unsigned turns = drift ? 160 : 120;
        for (unsigned tick = 0; tick < turns; ++tick)
        {
            PortableMeshKartEvents events;
            CHECK(portable_mesh_kart_tick(
                      &kart, rules, &course,
                      (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE |
                                                         (drift ? PORTABLE_MESH_HOP : 0)},
                      &events) == PORTABLE_MESH_OK);
            /* MK8 full lock keeps 99.6% a tick (drifting 99.8%) against the throttle. */
            CHECK(kart.grounded && kart.speed_q16 >= rules->forward_speed_q16 * 7 / 10 && !events.hopped);
        }
        /* At full lock the yaw a tick follows MK8's speed curve; a full-inward
         * drift turns at half the drift row (0.02 at 0.5 units/frame to the
         * handling row at 7) plus the settled 0.015 bias. */
        const int32_t before[3] = {kart.forward_q30[0], kart.forward_q30[1], kart.forward_q30[2]};
        const int32_t speed = kart.speed_q16;
        PortableMeshKartEvents step;
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                      (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE |
                                                                         (drift ? PORTABLE_MESH_HOP : 0)},
                                      &step) == PORTABLE_MESH_OK);
        const int64_t sine = ((int64_t)before[0] * kart.forward_q30[1] -
                              (int64_t)before[1] * kart.forward_q30[0]) >> 44;
        int64_t row = (int64_t)(drift ? rules->drift_turn_rate_q16
                                      : portable_mesh_kart_turn_rate(rules, speed)) * 65536 / 622980;
        const int64_t units = (int64_t)speed * 65536 / 205892;
        if (drift)
            row = (1311 + (row - 1311) * (units - 32768) / (13 * 32768)) / 2 + 983;
        const int64_t yaw = sine < 0 ? -sine : sine;
        printf("Production turn: drift=%u yaw_q16=%lld row_q16=%lld speed_q16=%d\n", drift,
               (long long)yaw, (long long)row, kart.speed_q16);
        CHECK(yaw * 100 >= row * 95 && yaw * 100 <= row * 105);
        if (drift)
        {
            /* Counter-steer acts at once: full outward turns at the 0.015 bias
             * less half the row, capped at 0.018 outward (0.006), never straight;
             * 20 ticks bring the filtered stick most of the way. */
            for (unsigned tick = 0; tick < 20; ++tick)
            {
                PortableMeshKartEvents events;
                CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                              (PortableMeshKartInput){-32767, PORTABLE_MESH_ACCELERATE |
                                                                                  PORTABLE_MESH_HOP},
                                              &events) == PORTABLE_MESH_OK && kart.drift_direction == 1);
            }
            const int32_t wide[3] = {kart.forward_q30[0], kart.forward_q30[1], kart.forward_q30[2]};
            CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                          (PortableMeshKartInput){-32767, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                                          &step) == PORTABLE_MESH_OK);
            const int64_t outward = (((int64_t)wide[0] * kart.forward_q30[1] -
                                      (int64_t)wide[1] * kart.forward_q30[0]) >> 44);
            printf("Production counter-steer: yaw_q16=%lld\n", (long long)outward);
            CHECK((outward < 0 ? -outward : outward) * 100 >= 393 * 90 &&
                  (outward < 0 ? -outward : outward) * 100 <= 393 * 120 && outward * sine > 0);
            const int32_t initial = kart.outside_drift_q16;
            for (unsigned tick = 0; tick < 17; ++tick)
            {
                PortableMeshKartEvents events;
                CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                              (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                              &events) == PORTABLE_MESH_OK);
            }
            CHECK(!kart.drift_direction && abs(kart.outside_drift_q16) <= abs(initial) / 10 &&
                  kart.speed_q16 >= rules->forward_speed_q16 * 7 / 10);
        }
    }
    /* Isolate actual sideways velocity; engine speed starts at zero. */
    for (unsigned drift = 0; drift < 2; ++drift)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(rules, &course, &kart) == 0);
        PortableMeshKartRules isolated = *rules;
        isolated.outside_drift_angle_q16 = 0;
        if (drift)
        {
            kart.drift_direction = 1;
            kart.previous_buttons = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP;
        }
        kart.velocity_q16[1] = 65536;
        for (unsigned tick = 0; tick < (drift ? 23u : 12u); ++tick)
        {
            PortableMeshKartEvents events;
            CHECK(portable_mesh_kart_tick(
                      &kart, &isolated, &course,
                      (PortableMeshKartInput){
                          0, drift ? (PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP) : 0},
                      &events) == PORTABLE_MESH_OK);
        }
        /* Measure slip across travel; a held drift keeps MK8's 0.02 rad a tick
         * floor, so its arc leaves some centripetal slip. */
        int32_t travel[3];
        CHECK(portable_mesh_kart_travel_direction(&kart, travel) == PORTABLE_MESH_OK);
        const int64_t across = ((int64_t)kart.velocity_q16[1] * travel[0] -
                                (int64_t)kart.velocity_q16[0] * travel[1]) / 1073741824;
        const int64_t slip_limit = drift ? 39322 : 6554;
        CHECK(across >= -slip_limit && across <= slip_limit);
    }
    return 0;
}
/* MK8 handling shape: no idle spin, mirrored reverse, drifts that always
 * arc (stick tightens/widens them) and a drift side chosen while airborne. */
static double production_heading(const PortableMeshKartState *kart)
{
    return atan2((double)kart->forward_q30[1], (double)kart->forward_q30[0]);
}
static int production_modern_steering(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    PortableMeshKartEvents events;
    PortableMeshKartState kart;
    CHECK(production_settle(rules, &course, &kart) == 0);
    for (unsigned tick = 0; tick < 60; ++tick)
        CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){32767, 0},
                                      &events) == PORTABLE_MESH_OK);
    CHECK(kart.forward_q30[1] == 0 && kart.speed_q16 == 0);
    double turned[2];
    for (unsigned reverse = 0; reverse < 2; ++reverse)
    {
        CHECK(production_settle(rules, &course, &kart) == 0);
        kart.speed_q16 = kart.velocity_q16[0] = reverse ? -4 * 65536 : 4 * 65536;
        for (unsigned tick = 0; tick < 20; ++tick)
            CHECK(portable_mesh_kart_tick(
                      &kart, rules, &course,
                      (PortableMeshKartInput){32767, reverse ? 0 : PORTABLE_MESH_ACCELERATE},
                      &events) == PORTABLE_MESH_OK);
        turned[reverse] = production_heading(&kart);
    }
    CHECK(turned[0] > .1 && turned[1] < -.05);
    double arcs[3];
    for (unsigned stick = 0; stick < 3; ++stick)
    {
        CHECK(production_settle(rules, &course, &kart) == 0);
        kart.speed_q16 = 24 * 65536;
        kart.drift_direction = 1;
        kart.previous_buttons = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP;
        const int16_t steering = stick == 0 ? 32767 : stick == 1 ? 0 : -32767;
        kart.steer_filtered_q16 = steering * 2;
        int32_t travel[3];
        CHECK(portable_mesh_kart_travel_direction(&kart, travel) == PORTABLE_MESH_OK);
        for (unsigned axis = 0; axis < 3; ++axis)
            kart.velocity_q16[axis] = (int32_t)((int64_t)travel[axis] * kart.speed_q16 / 1073741824);
        const double start = production_heading(&kart);
        for (unsigned tick = 0; tick < 30; ++tick)
            CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                      (PortableMeshKartInput){steering, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                      &events) == PORTABLE_MESH_OK && kart.drift_direction == 1);
        arcs[stick] = production_heading(&kart) - start;
    }
    printf("Production drift arcs: inward=%.4f neutral=%.4f outward=%.4f rad/0.5s\n",
           arcs[0], arcs[1], arcs[2]);
    /* The stick swings the drift by half its row around a bias that ramps in
     * over the first 22 ticks, so a fresh drift held fully outward barely
     * turns while inward winds it tight. */
    CHECK(arcs[0] > arcs[1] * 2 && arcs[1] > arcs[2] + .2 && fabs(arcs[2]) < .06);
    CHECK(production_settle(rules, &course, &kart) == 0);
    kart.speed_q16 = kart.velocity_q16[0] = 24 * 65536;
    CHECK(portable_mesh_kart_tick(&kart, rules, &course,
              (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
              &events) == PORTABLE_MESH_OK && events.hopped && !kart.drift_direction);
    CHECK(portable_mesh_kart_tick(&kart, rules, &course,
              (PortableMeshKartInput){-32767, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
              &events) == PORTABLE_MESH_OK && !kart.grounded && kart.drift_direction == -1);
    unsigned landing = 0;
    while (!kart.grounded && ++landing < 60)
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                  (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                  &events) == PORTABLE_MESH_OK);
    CHECK(kart.grounded && kart.drift_direction == -1);
    CHECK(portable_mesh_kart_tick(&kart, rules, &course,
              (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
              &events) == PORTABLE_MESH_OK && kart.drift_charge > 0);
    return 0;
}
/* Full-lock yaw at 8/16/24 m/s against MK8's curve evaluated in doubles, and
 * mini-turbo tiers at the row thresholds for inward (+5) and neutral (+2) holds. */
static int production_mk8_radii_and_charge(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    PortableMeshKartEvents events;
    const double row = rules->handling_turn_rate_q16 / 622980.0;
    for (unsigned metres = 8; metres <= 24; metres += 8)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(rules, &course, &kart) == 0);
        kart.speed_q16 = kart.velocity_q16[0] = (int32_t)metres * 65536;
        kart.steer_filtered_q16 = 65534;
        const double before = production_heading(&kart);
        CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){32767, 0}, &events) ==
              PORTABLE_MESH_OK);
        const double units = metres / 3.1417, t = (units - .5) / 6.5;
        const double expected = units >= 7 ? row : .018 + (row - .018) * t * t;
        const double radius = metres / 59.7275 / fabs(production_heading(&kart) - before),
                     want = metres / 59.7275 / expected;
        printf("Production radius: %u m/s %.2f m (MK8 %.2f m)\n", metres, radius, want);
        CHECK(fabs(radius - want) <= want * .05);
    }
    for (unsigned inward = 0; inward < 2; ++inward)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(rules, &course, &kart) == 0);
        kart.speed_q16 = kart.velocity_q16[0] = rules->forward_speed_q16;
        kart.drift_direction = 1;
        kart.previous_buttons = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP;
        const uint32_t rate = inward ? 5u : 2u;
        unsigned tick = 0;
        for (unsigned tier = 1; tier <= 3; ++tier)
        {
            while (kart.drift_tier < tier && ++tick < 2000)
                CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                          (PortableMeshKartInput){inward ? 32767 : 0, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                          &events) == PORTABLE_MESH_OK && kart.drift_direction == 1);
            CHECK(tick == (rules->charge_thresholds[tier - 1] + rate - 1u) / rate);
        }
    }
    CHECK(rules->boost_ticks[2] == 2u * rules->boost_ticks[1] - rules->boost_ticks[0]);
    return 0;
}
/* MK8 boosts: rocket tiers and burnout, a mushroom over an MT counting the MT
 * down underneath, the dash panel's floor cap, and Star's forced speed. */
static int production_mk8_boosts(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    PortableMeshKartEvents events;
    static const uint16_t rocket[5] = {10, 20, 30, 45, 70};
    for (unsigned tier = 1; tier <= 6; ++tier)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(rules, &course, &kart) == 0);
        const unsigned hold = tier < 6 ? portable_mesh_kart_rocket_hold_ticks(tier) : 180u;
        CHECK(hold >= 80u && hold <= 180u);
        for (uint32_t left = 180; left; --left)
            portable_mesh_kart_countdown(&kart, tier < 6 ? portable_mesh_kart_rocket_buttons(left, tier)
                                                         : PORTABLE_MESH_ACCELERATE);
        CHECK(portable_mesh_kart_rocket_tier(kart.rocket_charge_q16) == tier);
        portable_mesh_kart_go(&kart);
        CHECK(tier < 6 ? kart.rocket_ticks == rocket[tier - 1] && !kart.burnout_ticks
                       : !kart.rocket_ticks && kart.burnout_ticks == MESH_KART_MK8_BURNOUT_TICKS);
        for (unsigned tick = 0; tick < 5; ++tick)
            CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                          &events) == PORTABLE_MESH_OK);
        /* Rocket: 0.1 u/f a tick toward 1.1x top; burnout: no throttle. */
        CHECK(tier < 6 ? kart.speed_q16 >= 5 * portable_fixed_3d_dt(MESH_KART_MK8_ROCKET_ACCEL_Q16)
                       : kart.speed_q16 == 0);
    }
    PortableMeshKartState kart;
    CHECK(production_settle(rules, &course, &kart) == 0);
    kart.speed_q16 = kart.velocity_q16[0] = rules->forward_speed_q16;
    kart.boost_ticks[1] = (uint32_t)rules->boost_ticks[1];
    PortableMeshKartEffects mushroom = {.speed_ratio_q16 = 65536, .body_scale_q16 = 65536,
        .boost_cap_ratio_q16 = 78643, .boost_acceleration_q16 = 4305715, .boost_active = 1};
    for (unsigned tick = 0; tick < 30; ++tick)
        CHECK(portable_mesh_kart_tick_effects(&kart, rules, &course, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                              &mushroom, &events) == PORTABLE_MESH_OK);
    const int32_t floor = rules->forward_speed_q16 > MESH_KART_MK8_FLOOR_CAP_Q16 ? rules->forward_speed_q16
                                                                                : MESH_KART_MK8_FLOOR_CAP_Q16;
    CHECK(kart.speed_q16 == portable_fixed_3d_mul(floor, 78643));
    CHECK(kart.boost_ticks[1] == rules->boost_ticks[1] - 30u && kart.last_boost_tier == 2);
    PortableMeshKartEffects star = {.speed_ratio_q16 = 65536, .body_scale_q16 = 65536, .boost_cap_ratio_q16 = 65536,
        .star_cap_ratio_q16 = 68813, .star_acceleration_q16 = 4305715, .star = 1};
    CHECK(production_settle(rules, &course, &kart) == 0);
    for (unsigned tick = 0; tick < 40; ++tick)
        CHECK(portable_mesh_kart_tick_effects(&kart, rules, &course, (PortableMeshKartInput){0, PORTABLE_MESH_BRAKE},
                                              &star, &events) == PORTABLE_MESH_OK);
    /* Star forces 0.35 u/f a tick to 1.05x top whatever the pedals say. */
    CHECK(kart.speed_q16 == portable_fixed_3d_mul(rules->forward_speed_q16, 68813));
    return 0;
}
/* MK8's hop-less drift (spin turn) and brake slide. */
static int production_spin_and_slide(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    PortableMeshKartEvents events;
    PortableMeshKartState kart;
    CHECK(production_settle(rules, &course, &kart) == 0);
    kart.speed_q16 = kart.velocity_q16[0] = rules->forward_speed_q16;
    kart.previous_buttons = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP;
    unsigned spun = 0;
    while (!kart.drift_direction && ++spun < 120)
    {
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                  (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                  &events) == PORTABLE_MESH_OK && kart.grounded && !events.hopped);
    }
    /* Gain 0.6..1.5 a tick with 0.99 decay crosses 30 within a second. */
    printf("Production spin turn: ticks=%u gain_q16=%d\n", spun, rules->spin_gain_q16);
    CHECK(kart.drift_direction == 1 && spun >= 20 && spun <= 60 && !kart.spin_q16);
    CHECK(production_settle(rules, &course, &kart) == 0);
    kart.speed_q16 = kart.velocity_q16[0] = rules->forward_speed_q16;
    kart.steer_filtered_q16 = 65534;
    CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){32767, PORTABLE_MESH_BRAKE},
                                  &events) == PORTABLE_MESH_OK);
    const unsigned length = kart.slide_ticks;
    /* D+0xac starts at 40..80 and the slide ends when it drops below zero: one extra tick. */
    CHECK(length >= 41u && length <= 81u && !kart.drift_direction);
    unsigned held = 0;
    /* Keep the kart at speed so only the countdown ends the slide. */
    while (kart.slide_ticks && ++held < 100)
    {
        kart.speed_q16 = kart.velocity_q16[0] = rules->forward_speed_q16;
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                  (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE}, &events) == PORTABLE_MESH_OK);
    }
    CHECK(held == length);
    /* Slide exit is SetSlide(0.245, 5): the traction hold is armed for 5 ticks. */
    CHECK(kart.traction_hold_ticks >= 1u && kart.traction_hold_ticks <= 5u);
    /* The slide's entry gate is 0.4 of top speed and its length 15 ticks per unit/frame, 40..80. */
    CHECK(MESH_KART_MK8_SLIDE_SPEED_Q16 == 26214 && MESH_KART_MK8_SLIDE_MIN_TICKS == 40u &&
          MESH_KART_MK8_SLIDE_MAX_TICKS == 80u && MESH_KART_MK8_SLIDE_EXIT_TICKS == 5u);
    return 0;
}
static int production_entry_turbos(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    /* MK8's drift gate (D+0x5c) is half of top speed. */
    CHECK(rules->minimum_drift_speed_ratio_q16 == 32768);
    const int32_t entry =
        portable_fixed_3d_mul(rules->forward_speed_q16, rules->minimum_drift_speed_ratio_q16);
    CHECK(entry == rules->forward_speed_q16 / 2);
    for (unsigned eligible = 0; eligible < 2; ++eligible)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(rules, &course, &kart) == 0);
        kart.speed_q16 = kart.velocity_q16[0] = entry + (eligible ? 1 : 0);
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(
                  &kart, rules, &course,
                  (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                  &events) == PORTABLE_MESH_OK);
        CHECK(events.hopped && !kart.grounded && (kart.drift_direction != 0) == (eligible != 0));
        CHECK(!kart.drift_charge);
    }
    const int32_t top = rules->forward_speed_q16;
    for (unsigned tier = 1; tier <= 3; ++tier)
    {
        PortableMeshKartState kart;
        CHECK(production_settle(rules, &course, &kart) == 0);
        kart.speed_q16 = kart.velocity_q16[0] = top;
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_tick(
                  &kart, rules, &course,
                  (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                  &events) == PORTABLE_MESH_OK);
        while (!kart.grounded)
        {
            CHECK(!kart.drift_charge);
            CHECK(portable_mesh_kart_tick(
                      &kart, rules, &course,
                      (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                      &events) == PORTABLE_MESH_OK);
        }
        unsigned charged = 0;
        while (kart.drift_tier < tier && charged < 181)
        {
            CHECK(portable_mesh_kart_tick(
                      &kart, rules, &course,
                      (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP},
                      &events) == PORTABLE_MESH_OK);
            ++charged;
        }
        /* Full lock charges at the inward rate from the first grounded tick. */
        const uint32_t threshold = rules->charge_thresholds[tier - 1];
        CHECK(charged == (threshold + rules->inward_charge_rate - 1u) / rules->inward_charge_rate);
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                      (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                      &events) == PORTABLE_MESH_OK);
        CHECK(events.released && kart.boost_ticks[tier - 1] == rules->boost_ticks[tier - 1] - 1);
        CHECK(kart.last_boost_tier == tier);
        unsigned active = 1;
        while (kart.boost_ticks[tier - 1])
        {
            CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                          (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                          &events) == PORTABLE_MESH_OK);
            CHECK(kart.speed_q16 > top * 7 / 10 &&
                  kart.speed_q16 <= portable_fixed_3d_mul(top, rules->boost_cap_ratio_q16[tier - 1]) + 2);
            ++active;
        }
        CHECK(active == rules->boost_ticks[tier - 1]);
        printf("Production turbo: tier=%u charge=%u active=%u speed=%d\n", tier, charged, active,
               kart.speed_q16);
    }
    return 0;
}
static int production_offroad(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    world.count = 4;
    world.surfaces[1] = world.surfaces[0];
    world.surfaces[1].type = 1;
    world.surfaces[1].speed_ratio_q16 = 39322;
    world.surfaces[1].friction_q16 = 52429;
    world.surfaces[1].surface_class = MESH_KART_MK8_CLASS_GRASS; /* MK8: cap 0.5, friction 0.98 */
    for (unsigned t = 0; t < 2; ++t)
    {
        world.triangles[t + 2] = world.triangles[t];
        world.triangles[t + 2].surface = 1;
        for (unsigned v = 0; v < 3; ++v)
        {
            if (world.triangles[t].vertices_q16[v][0] > 0)
                world.triangles[t].vertices_q16[v][0] = 0;
            if (world.triangles[t + 2].vertices_q16[v][0] < 0)
                world.triangles[t + 2].vertices_q16[v][0] = 0;
        }
    }
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    PortableMeshKartState kart;
    const int32_t position[3] = {-65536, 0, rules->kart_radius_q16 + rules->ride_height_q16},
                  forward[3] = {1073741824, 0, 0};
    CHECK(portable_mesh_kart_init(&kart, rules, position, forward) == PORTABLE_MESH_OK);
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){0}, &events) ==
          PORTABLE_MESH_OK);
    const int32_t top = rules->forward_speed_q16, slow = portable_fixed_3d_mul(top, 32768);
    kart.speed_q16 = kart.velocity_q16[0] = top;
    unsigned off = 0;
    for (unsigned tick = 0; tick < 30; ++tick)
    {
        const int32_t before = kart.speed_q16;
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                      (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                      &events) == PORTABLE_MESH_OK);
        /* MK8 bleeds 2% a tick above the off-road cap, times grass friction 0.98. */
        if (kart.contact_surface == 1 && off++ && before > slow)
            CHECK(kart.speed_q16 <= portable_fixed_3d_mul(portable_fixed_3d_mul(before, MESH_KART_MK8_OVER_CAP_Q16), 64225) + 2 ||
                  kart.speed_q16 <= slow);
    }
    CHECK(off > 10 && kart.speed_q16 < top * 3 / 4);
    kart.forward_q30[0] = -1073741824;
    kart.velocity_q16[0] = -kart.speed_q16;
    unsigned road = 0, recovered = 0;
    for (unsigned tick = 1; tick <= 480; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                                      (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                      &events) == PORTABLE_MESH_OK);
        if (kart.contact_surface == 0 && !road)
            road = tick;
        if (road && kart.speed_q16 >= top - top / 20)
        {
            recovered = tick;
            break;
        }
    }
    CHECK(road && recovered > road && recovered - road <= 240);
    printf("Production offroad: entry_ticks=%u return=%u restored=%u\n", off, road, recovered);
    return 0;
}

/* MK8 surface classes: each drives to its speed share, eases traction to
 * its slide target and keeps friction*T of sideways slip a tick; the
 * off-road traction stat raises slippery speed. */
static int production_surface_classes(const PortableMeshKartRules *rules, int32_t gravity)
{
    static const struct
    {
        unsigned surface_class;
        int32_t speed, slide, friction;
    } classes[] = {
        {MESH_KART_MK8_CLASS_DIRT, 45875, MESH_KART_MK8_OFFROAD_SLIDE_Q16, 65536},
        {MESH_KART_MK8_CLASS_GRASS, 32768, MESH_KART_MK8_OFFROAD_SLIDE_Q16, 64225},
        {MESH_KART_MK8_CLASS_HEAVY, 19661, MESH_KART_MK8_OFFROAD_SLIDE_Q16, 63570},
        {MESH_KART_MK8_CLASS_SLIPPERY, 0, 0, 0},
        {MESH_KART_MK8_CLASS_ICE + 2, 0, 0, 0},
    };
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    PortableMeshKartEvents events;
    for (unsigned i = 0; i < sizeof classes / sizeof classes[0]; ++i)
    {
        int32_t speeds[2] = {0, 0};
        for (unsigned stat = 0; stat < 2u; ++stat)
        {
            PortableMeshKartRules r = *rules;
            r.traction_level_q16 = stat ? 65536 : 0;
            int32_t speed = classes[i].speed, slide = classes[i].slide, friction = classes[i].friction;
            if (!speed)
            {
                const unsigned ice = classes[i].surface_class >= MESH_KART_MK8_CLASS_ICE;
                const unsigned level = classes[i].surface_class -
                                       (ice ? MESH_KART_MK8_CLASS_ICE : MESH_KART_MK8_CLASS_SLIPPERY);
                speed = (ice ? MESH_KART_MK8_ICE_X_Q16 : MESH_KART_MK8_SLIPPERY_X_Q16) +
                        (stat ? MESH_KART_MK8_SLIPPERY_X_SPAN_Q16 : 0);
                slide = r.traction_off_q16 + 655 * (int32_t)level;
                friction = 65536 - portable_fixed_3d_mul(65536 - speed, 65536 - speed);
            }
            for (unsigned t = 0; t < 2u; ++t)
                world.surfaces[t].surface_class = (uint8_t)classes[i].surface_class;
            PortableMeshKartState kart;
            CHECK(production_settle(&r, &course, &kart) == 0);
            for (unsigned tick = 0; tick < 600; ++tick)
                CHECK(portable_mesh_kart_tick(&kart, &r, &course, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                              &events) == PORTABLE_MESH_OK);
            const double cap = (double)portable_fixed_3d_mul(r.forward_speed_q16, speed);
            CHECK(fabs(kart.speed_q16 - cap) <= cap * 0.03 && kart.traction_q16 == slide);
            speeds[stat] = kart.speed_q16;
            /* One tick of sideways slip keeps friction*T of it. */
            kart.velocity_q16[1] = 65536;
            CHECK(portable_mesh_kart_tick(&kart, &r, &course, (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE},
                                          &events) == PORTABLE_MESH_OK);
            const double kept = 65536.0 * friction / 65536 * slide / 65536;
            CHECK(fabs(kart.velocity_q16[1] - kept) <= 65536 * 0.03);
            /* Coasting keeps decelerating toward rest. */
            const int32_t before = kart.speed_q16;
            for (unsigned tick = 0; tick < 60; ++tick)
                CHECK(portable_mesh_kart_tick(&kart, &r, &course, (PortableMeshKartInput){0}, &events) ==
                      PORTABLE_MESH_OK);
            CHECK(kart.speed_q16 < before);
        }
        CHECK(classes[i].speed ? speeds[1] == speeds[0] : speeds[1] > speeds[0]);
    }
    return 0;
}
/* MK8 course walls: a head-on hit stops the engine and knocks the kart
 * back out; a 20-degree scrape keeps (1-sin 20)*0.9 of its speed and kicks
 * the nose away; both orders and both traversals agree; steering escapes. */
static int production_wall_escape(const PortableMeshKartRules *rules, int32_t gravity)
{
    for (unsigned order = 0; order < 2; ++order)
        for (unsigned glance = 0; glance < 2; ++glance)
        {
            PortableMeshKartState expected_stop = {0}, expected_escape = {0};
            for (unsigned indexed = 0; indexed < 2; ++indexed)
            {
                World world;
                production_wide_floor(&world);
                world.count = 4;
                world.surfaces[1].type = 3;
                for (unsigned t = 0; t < 2; ++t)
                {
                    world.triangles[t + 2] = world.triangles[t];
                    world.triangles[t + 2].surface = 1;
                    for (unsigned v = 0; v < 3; ++v)
                    {
                        int32_t *point = world.triangles[t + 2].vertices_q16[v];
                        const int32_t x = point[0];
                        point[0] = 65536;
                        point[2] = x;
                    }
                }
                if (order)
                    for (unsigned t = 0; t < 2; ++t)
                    {
                        PortableMeshTriangle swap = world.triangles[t];
                        world.triangles[t] = world.triangles[t + 2];
                        world.triangles[t + 2] = swap;
                    }
                PortableMeshCourse course = {&world,   world.count, gravity,
                                             triangle, surface,     indexed ? world_next : NULL, NULL};
                PortableMeshKartState kart;
                CHECK(production_settle(rules, &course, &kart) == 0);
                const double angle = glance ? 20.0 * M_PI / 180.0 : M_PI / 2.0;
                const int64_t vector[3] = {(int64_t)lround(sin(angle) * 1e6), (int64_t)lround(cos(angle) * 1e6), 0};
                CHECK(portable_fixed_3d_normalize(vector, kart.forward_q30));
                kart.speed_q16 = 10 * 65536;
                for (unsigned i = 0; i < 2; ++i)
                    kart.velocity_q16[i] = (int32_t)((int64_t)kart.forward_q30[i] * 10 / 16384);
                PortableMeshKartEvents events;
                unsigned hit = 0;
                for (unsigned tick = 1; tick <= 15; ++tick)
                {
                    CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){0},
                                                  &events) == PORTABLE_MESH_OK);
                    if (events.wall)
                    {
                        hit = tick;
                        break;
                    }
                }
                CHECK(hit && kart.grounded && kart.knock_q16[0] < 0);
                CHECK(kart.position_q16[0] <= 65536 - rules->kart_radius_q16 + 2);
                const double kept = glance ? (1.0 - sin(angle)) * 0.9 : 0.0;
                CHECK(fabs(kart.speed_q16 / 65536.0 - 10.0 * pow(0.98, hit) * kept) <= 0.1);
                CHECK(glance ? kart.wall_yaw_q24 && kart.velocity_q16[1] > 0 : !kart.wall_yaw_q24);
                const PortableMeshKartState collision = kart;
                for (unsigned tick = 0; tick < 600; ++tick)
                {
                    CHECK(portable_mesh_kart_tick(&kart, rules, &course, (PortableMeshKartInput){0},
                                                  &events) == PORTABLE_MESH_OK);
                    if (tick == 0 && glance)
                        CHECK(kart.forward_q30[0] < collision.forward_q30[0]); /* kicked away */
                }
                /* The knock carries the kart off the wall before it settles. */
                CHECK(kart.position_q16[0] < collision.position_q16[0] - 655);
                CHECK(abs(kart.speed_q16) <= 16 && abs(kart.velocity_q16[0]) <= 16 &&
                      abs(kart.velocity_q16[1]) <= 16 && abs(kart.knock_q16[0]) <= 16);
                if (!indexed)
                    expected_stop = kart;
                else
                    CHECK(!memcmp(&expected_stop, &kart, sizeof kart));
                kart = collision;
                for (unsigned tick = 0; tick < 120; ++tick)
                    CHECK(portable_mesh_kart_tick(
                              &kart, rules, &course,
                              (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE},
                              &events) == PORTABLE_MESH_OK);
                CHECK(kart.position_q16[0] < 65536 - rules->kart_radius_q16 - 6554);
                CHECK(kart.position_q16[1] > collision.position_q16[1] + 65536 && kart.grounded);
                if (!indexed)
                    expected_escape = kart;
                else
                    CHECK(!memcmp(&expected_escape, &kart, sizeof kart));
                printf("Production wall: order=%u indexed=%u glance=%u contact=%u speed=%d final_xy=%d,%d\n",
                       order, indexed, glance, hit, collision.speed_q16, kart.position_q16[0], kart.position_q16[1]);
            }
        }
    return 0;
}

/* Recovered 0x16fcfc/0x177d18: brake is independent of drift hold/charge,
 * and applies after throttle. Exercise both sides, charge rates and hop edges. */
static int brake_drift(const PortableMeshKartRules *rules, int32_t gravity)
{
    World world;
    production_wide_floor(&world);
    PortableMeshCourse course = {&world, world.count, gravity, triangle, surface, NULL, NULL};
    const uint16_t hold = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_HOP;
    const uint16_t braking = hold | PORTABLE_MESH_BRAKE;
    const int32_t entry = portable_fixed_3d_mul(rules->forward_speed_q16,
                                                rules->minimum_drift_speed_ratio_q16);
    PortableMeshKartEvents events;
    PortableMeshKartState seed;
    CHECK(production_settle(rules, &course, &seed) == 0);
    seed.speed_q16 = seed.velocity_q16[0] = rules->forward_speed_q16;
    seed.previous_buttons = hold;
    for (int side = -1; side <= 1; side += 2)
        for (unsigned inward = 0; inward < 2; ++inward)
        {
            PortableMeshKartState kart = seed, unbraked = seed;
            kart.drift_direction = unbraked.drift_direction = (int8_t)side;
            kart.drift_charge = unbraked.drift_charge = rules->charge_thresholds[0];
            kart.drift_tier = unbraked.drift_tier = 1;
            const int16_t steer = inward ? (int16_t)(side * 32767) : 0;
            CHECK(portable_mesh_kart_tick(&unbraked, rules, &course,
                      (PortableMeshKartInput){steer, hold}, &events) == PORTABLE_MESH_OK);
            CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                      (PortableMeshKartInput){steer, braking}, &events) == PORTABLE_MESH_OK);
            CHECK(kart.drift_direction == side && kart.drift_charge == unbraked.drift_charge &&
                  kart.drift_tier == unbraked.drift_tier && kart.speed_q16 < unbraked.speed_q16 &&
                  !events.hopped && !events.released && !kart.slide_ticks);
            /* A brake tap leaves the same charge to resume and release. */
            CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                      (PortableMeshKartInput){steer, hold}, &events) == PORTABLE_MESH_OK);
            CHECK(kart.drift_direction == side && !events.hopped && !events.released);
            const unsigned tier = kart.drift_tier;
            CHECK(tier > 0);
            CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                      (PortableMeshKartInput){steer, PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_BRAKE},
                      &events) == PORTABLE_MESH_OK);
            CHECK(events.released && !kart.drift_direction && !kart.drift_charge &&
                  kart.boost_ticks[tier - 1] == rules->boost_ticks[tier - 1] - 1);
        }
    /* Neutral steering isolates the ordered speed equation at full alignment. */
    PortableMeshKartState kart = seed;
    kart.drift_direction = 1;
    kart.speed_q16 = kart.velocity_q16[0] = (entry + rules->forward_speed_q16) / 2;
    const int32_t accelerated = portable_mesh_kart_mk8_drive(kart.speed_q16, rules->forward_speed_q16,
        MESH_KART_MK8_DRIFT_RATE_Q16, portable_fixed_3d_dt(rules->acceleration_limit_q16), 65536, 65536, 1);
    const int32_t expected = portable_mesh_kart_mk8_drive(accelerated, rules->reverse_speed_q16,
        MESH_KART_MK8_BRAKE_RATE_Q16, MESH_KART_MK8_BRAKE_LIMIT_Q16, 65536, 65536, -1);
    CHECK(portable_mesh_kart_tick(&kart, rules, &course,
              (PortableMeshKartInput){0, braking}, &events) == PORTABLE_MESH_OK);
    CHECK(abs(kart.speed_q16 - expected) <= 2 && kart.drift_direction == 1);
    /* Brake through the speed gate: lose charge, never cash it as a turbo. */
    kart = seed;
    kart.drift_direction = 1;
    kart.drift_tier = 1;
    kart.drift_charge = rules->charge_thresholds[0];
    kart.speed_q16 = kart.velocity_q16[0] = entry + 1;
    CHECK(portable_mesh_kart_tick(&kart, rules, &course,
              (PortableMeshKartInput){0, braking}, &events) == PORTABLE_MESH_OK);
    CHECK(kart.speed_q16 < entry && kart.drift_direction == 1 && !events.released);
    CHECK(portable_mesh_kart_tick(&kart, rules, &course,
              (PortableMeshKartInput){0, braking}, &events) == PORTABLE_MESH_OK);
    CHECK(!kart.drift_direction && !kart.drift_charge && !kart.drift_tier && !events.released);
    kart = seed;
    kart.drift_direction = 1;
    kart.drift_tier = 1;
    kart.drift_charge = rules->charge_thresholds[0];
    CHECK(portable_mesh_kart_tick(&kart, rules, &course,
              (PortableMeshKartInput){0, PORTABLE_MESH_BRAKE | PORTABLE_MESH_HOP}, &events) == PORTABLE_MESH_OK);
    CHECK(!kart.drift_direction && !kart.drift_charge && !events.released);
    /* Brake does not block a fresh hop or the side chosen during that hop;
     * holding all three through landing must not auto-hop or start a slide. */
    for (unsigned late = 0; late < 2; ++late)
    {
        kart = seed;
        kart.previous_buttons = PORTABLE_MESH_ACCELERATE | PORTABLE_MESH_BRAKE;
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                  (PortableMeshKartInput){late ? 0 : -32767, braking}, &events) == PORTABLE_MESH_OK);
        CHECK(events.hopped && !kart.grounded && kart.drift_direction == (late ? 0 : -1));
        unsigned ticks = 0;
        while (!kart.grounded && ++ticks < 120)
        {
            CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                      (PortableMeshKartInput){-32767, braking}, &events) == PORTABLE_MESH_OK);
            CHECK(!events.hopped && !kart.drift_charge && kart.drift_direction == -1);
        }
        CHECK(kart.grounded && ticks < 120);
        CHECK(portable_mesh_kart_tick(&kart, rules, &course,
                  (PortableMeshKartInput){-32767, braking}, &events) == PORTABLE_MESH_OK);
        CHECK(kart.drift_charge == rules->inward_charge_rate && !events.hopped && !kart.slide_ticks);
    }
    puts("Brake drift: PASS continuation charge speed release cancellation hop");
    return 0;
}

static int production_profile_outcomes(const PortableMeshKartRules *rules, int32_t gravity)
{
    CHECK(rules->revision == 10 && gravity == 1845723);
    CHECK(rules->hop_velocity_q16 == 226617 && rules->ground_snap_q16 == 655);
    CHECK(rules->ground_probe_q16 == 6554 && rules->terminal_fall_speed_q16 == 36 * 65536);
    CHECK(rules->kart_radius_q16 == 32768);
    CHECK(production_hop(rules, gravity) == 0);
    CHECK(production_fall(rules, gravity) == 0);
    CHECK(production_departure(rules, gravity) == 0);
    CHECK(production_panels(rules, gravity) == 0);
    CHECK(production_speed_metrics(rules,gravity) == 0);
    CHECK(production_circles_and_release(rules,gravity) == 0);
    CHECK(production_entry_turbos(rules,gravity) == 0);
    CHECK(production_modern_steering(rules,gravity) == 0);
    CHECK(production_spin_and_slide(rules,gravity) == 0);
    CHECK(brake_drift(rules, gravity) == 0);
    CHECK(production_mk8_radii_and_charge(rules,gravity) == 0);
    CHECK(production_mk8_boosts(rules,gravity) == 0);
    CHECK(production_offroad(rules,gravity) == 0);
    CHECK(production_surface_classes(rules,gravity) == 0);
    CHECK(production_wall_escape(rules,gravity) == 0);
    return 0;
}

static int production_pack(const char *path)
{
    int result = 1;
    FILE *file = fopen(path, "rb");
    uint8_t *bytes = NULL;
    if (!file || fseek(file, 0, SEEK_END) != 0)
        goto cleanup;
    const long length = ftell(file);
    if (length <= 0 || length > 64L * 1024L * 1024L || fseek(file, 0, SEEK_SET) != 0)
        goto cleanup;
    bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length)
        goto cleanup;
    if (fclose(file) != 0)
    {
        file = NULL;
        goto cleanup;
    }
    file = NULL;
    PortableAssetPackView pack;
    PortableCourse3DCatalog catalog;
    PortableCourse3DView course;
    PortableCourse3DMeta meta;
    PortableMeshKartRules rules;
    char error[256] = {0};
    if (portable_asset_pack_open(bytes, (size_t)length, &pack) != PORTABLE_ASSET_PACK_OK ||
        !portable_course_3d_catalog_at(&pack, 0, &catalog, error, sizeof error) ||
        !portable_course_3d_open(&pack, catalog.course_id, &course, error, sizeof error) ||
        !portable_course_3d_meta(&course, &meta) ||
        !portable_mesh_kart_rules_asset_load(&pack, meta.physics_profile_id, &rules))
    {
        fprintf(stderr, "production profile fixture: %s\n", error);
        goto cleanup;
    }
    result = production_profile_outcomes(&rules, meta.gravity_q16);
cleanup:
    if (file)
        fclose(file);
    free(bytes);
    return result;
}

static int cannon_flight(void)
{
    World world;
    floor_world(&world);
    PortableMeshKartRules rules = test_mesh_rules();
    /* A vertical trigger plane across the road at y = -4 m. */
    const int32_t plane[3][3] = {{-5 * 65536, -4 * 65536, 0}, {5 * 65536, -4 * 65536, 0},
                                 {0, -4 * 65536, 6 * 65536}};
    memcpy(world.triangles[2].vertices_q16, plane, sizeof plane);
    world.triangles[2].surface = 1;
    world.triangles[2].respawn = UINT32_MAX;
    world.count = 3;
    world.surfaces[1].type = 9;
    world.surfaces[1].launch_q16 = 30 * 65536;
    const int32_t target[3] = {12 * 65536, 10 * 65536, 0}, heading[3] = {65536, 0, 0};
    memcpy(world.surfaces[1].cannon_target_q16, target, sizeof target);
    memcpy(world.surfaces[1].cannon_heading_q16, heading, sizeof heading);
    PortableMeshCourse course = {&world, world.count, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t lift = rules.kart_radius_q16 + rules.ride_height_q16;
    const int32_t position[3] = {0, 0, lift}, forward[3] = {0, -1073741824, 0};
    PortableMeshKartState kart;
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_init(&kart, &rules, position, forward) == PORTABLE_MESH_OK);
    kart.grounded = 1;
    kart.contact_surface = 0;
    unsigned fired = 0;
    for (unsigned tick = 0; tick < 240u && !fired; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&kart, &rules, &course,
            (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events) == PORTABLE_MESH_OK);
        CHECK(!events.wall); /* the trigger never pushes */
        fired = events.cannon;
    }
    CHECK(fired && kart.cannon_ticks && !kart.cannon_tick && kart.cannon_surface == 1u);
    const uint16_t ticks = kart.cannon_ticks;
    int32_t highest = 0;
    for (unsigned tick = 0; tick < ticks; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
            PORTABLE_MESH_OK);
        if (kart.position_q16[2] > highest)
            highest = kart.position_q16[2];
    }
    /* Arrival on the target, facing the exit heading at engine speed. */
    CHECK(!kart.cannon_ticks && !kart.grounded);
    CHECK(kart.position_q16[0] == target[0] && kart.position_q16[1] == target[1] &&
        kart.position_q16[2] == lift);
    CHECK(highest >= lift + 2 * 65536);
    CHECK(kart.forward_q30[0] == 1073741824 && !kart.forward_q30[1] && !kart.forward_q30[2]);
    CHECK(kart.speed_q16 == rules.forward_speed_q16 && kart.velocity_q16[0] > 0);
    CHECK(portable_mesh_kart_tick(&kart, &rules, &course,
        (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events) == PORTABLE_MESH_OK);
    CHECK(kart.grounded && kart.velocity_q16[0] > 0);
    /* A cannon row without a target is rejected. */
    memset(world.surfaces[1].cannon_heading_q16, 0, sizeof heading);
    PortableMeshKartState probe;
    CHECK(portable_mesh_kart_init(&probe, &rules, (int32_t[3]){0, -4 * 65536, lift}, forward) ==
        PORTABLE_MESH_OK);
    CHECK(portable_mesh_kart_tick(&probe, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
        PORTABLE_MESH_INVALID);
    return 0;
}

static int one_sided_wall(void)
{
    World world;
    floor_world(&world);
    PortableMeshKartRules rules = test_mesh_rules();
    /* A wall across the road at y = -4 m facing +y, back toward -y. */
    const int32_t plane[3][3] = {{-5 * 65536, -4 * 65536, 0}, {0, -4 * 65536, 6 * 65536},
                                 {5 * 65536, -4 * 65536, 0}};
    memcpy(world.triangles[2].vertices_q16, plane, sizeof plane);
    world.triangles[2].surface = 1;
    world.triangles[2].respawn = UINT32_MAX;
    world.count = 3;
    world.surfaces[1].type = 3;
    world.surfaces[1].one_sided = 1;
    PortableMeshCourse course = {&world, world.count, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t lift = rules.kart_radius_q16 + rules.ride_height_q16;
    for (unsigned behind = 0; behind < 2u; ++behind)
    {
        const int32_t position[3] = {0, behind ? -8 * 65536 : 0, lift};
        const int32_t forward[3] = {0, behind ? 1073741824 : -1073741824, 0};
        PortableMeshKartState kart;
        PortableMeshKartEvents events;
        CHECK(portable_mesh_kart_init(&kart, &rules, position, forward) == PORTABLE_MESH_OK);
        unsigned walls = 0;
        for (unsigned tick = 0; tick < 120u; ++tick)
        {
            CHECK(portable_mesh_kart_tick(&kart, &rules, &course,
                (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events) == PORTABLE_MESH_OK);
            walls += events.wall;
        }
        /* From the front the wall stops the kart; from behind it passes (only
         * leaving the face can touch it, pushing the kart onward). */
        CHECK(behind ? walls <= 1u && kart.position_q16[1] > 0 : walls && kart.position_q16[1] > -4 * 65536);
    }
    return 0;
}

static int surface_gravity_wall(void)
{
    World world;
    floor_world(&world);
    PortableMeshKartRules rules = test_mesh_rules();
    /* A vertical surface-gravity wall at x = 10 m facing -x, 40 m long. */
    const int32_t wall[4][3] = {{10 * 65536, -20 * 65536, 0}, {10 * 65536, 20 * 65536, 0},
                                {10 * 65536, 20 * 65536, 20 * 65536}, {10 * 65536, -20 * 65536, 20 * 65536}};
    const unsigned faces[2][3] = {{0, 2, 1}, {0, 3, 2}};
    for (unsigned t = 0; t < 2u; ++t)
    {
        for (unsigned i = 0; i < 3u; ++i)
            memcpy(world.triangles[2 + t].vertices_q16[i], wall[faces[t][i]], 12);
        world.triangles[2 + t].surface = 1;
        world.triangles[2 + t].respawn = UINT32_MAX;
    }
    world.count = 4;
    world.surfaces[1].surface_gravity = 1;
    PortableMeshCourse course = {&world, world.count, 10 * 65536, triangle, surface, NULL, NULL};
    const int32_t lift = rules.kart_radius_q16 + rules.ride_height_q16;
    const int32_t position[3] = {10 * 65536 - lift, -10 * 65536, 8 * 65536}, forward[3] = {0, 1073741824, 0};
    PortableMeshKartState kart;
    PortableMeshKartEvents events;
    CHECK(portable_mesh_kart_init(&kart, &rules, position, forward) == PORTABLE_MESH_OK);
    kart.up_q30[2] = 0;
    kart.up_q30[0] = kart.gravity_up_q30[0] = -1073741824;
    for (unsigned tick = 0; tick < 60u; ++tick)
    {
        CHECK(portable_mesh_kart_tick(&kart, &rules, &course,
            (PortableMeshKartInput){0, PORTABLE_MESH_ACCELERATE}, &events) == PORTABLE_MESH_OK);
        /* Held to the wall: no fall, driving along it. */
        CHECK(kart.grounded && kart.contact_surface == 1u && kart.gravity_up_q30[0] == -1073741824);
        CHECK(kart.position_q16[2] == position[2]);
    }
    CHECK(kart.position_q16[1] > position[1] && kart.speed_q16 > 0);
    /* Leaving a loop onto an ordinary floor restores world gravity. */
    const int32_t ground[3] = {0, 0, lift};
    CHECK(portable_mesh_kart_init(&kart, &rules, ground, forward) == PORTABLE_MESH_OK);
    kart.gravity_up_q30[0] = -322122547;
    kart.gravity_up_q30[2] = 1024284418;
    CHECK(portable_mesh_kart_tick(&kart, &rules, &course, (PortableMeshKartInput){0, 0}, &events) ==
        PORTABLE_MESH_OK);
    CHECK(kart.contact_surface == 0u && !kart.gravity_up_q30[0] && !kart.gravity_up_q30[2]);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2)
        return production_pack(argv[1]);
    if (argc != 1)
        return 2;
    CHECK(coast_and_travel_regressions() == 0);
    CHECK(indexed_corrections() == 0);
    CHECK(automatic_launch_traction() == 0);
    CHECK(jump_distance_ceiling() == 0);
    CHECK(cannon_flight() == 0);
    CHECK(one_sided_wall() == 0);
    CHECK(surface_gravity_wall() == 0);
    CHECK(effects_identity() == 0);
    CHECK(effects_dynamics() == 0);
    CHECK(effects_pairs_and_rollback() == 0);
    CHECK(flat_and_drift() == 0);
    const PortableMeshKartRules brake_rules = test_mesh_rules();
    CHECK(brake_drift(&brake_rules, 1845723) == 0);
    CHECK(slope_air_and_bump() == 0);
    CHECK(contact_speed_survives_coast() == 0);
    CHECK(weighted_bumps() == 0);
    CHECK(slipstream() == 0);
    CHECK(tricks() == 0);
    CHECK(anti_gravity() == 0);
    CHECK(gliding() == 0);
    CHECK(hazard_knock() == 0);
    CHECK(walls_tunnels_and_budget() == 0);
    CHECK(filtered_slope_transition() == 0);
    CHECK(concave_floor_joint() == 0);
    CHECK(faceted_crease_stays_grounded() == 0);
    CHECK(longitudinal_acceleration_independent_of_grip() == 0);
    return 0;
}
