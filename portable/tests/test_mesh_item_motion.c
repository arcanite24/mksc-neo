#include "mesh_item_motion.h"
#include "mesh_query.h"
#include "fixed_3d.h"
#include <stdio.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh item motion line %d: %s\n", __LINE__, #x);                       \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
typedef struct
{
    PortableMeshTriangle triangles[16];
    PortableMeshSurface surfaces[4];
    uint32_t count, fail;
} World;
static int triangle(const void *p, uint32_t i, PortableMeshTriangle *out)
{
    const World *w = p;
    if (i >= w->count || i == w->fail)
        return 0;
    *out = w->triangles[i];
    return 1;
}
static int surface(const void *p, uint32_t i, PortableMeshSurface *out)
{
    const World *w = p;
    if (i >= 4)
        return 0;
    *out = w->surfaces[i];
    return 1;
}
static void reset_world(World *w)
{
    memset(w, 0, sizeof *w);
    w->fail = UINT32_MAX;
}
static void quad(World *w, const int32_t vertices[4][3], uint32_t kind)
{
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned face = 0; face < 2; ++face)
    {
        PortableMeshTriangle *t = &w->triangles[w->count++];
        t->surface = kind;
        t->respawn = 70000u + kind;
        for (unsigned v = 0; v < 3; ++v)
            memcpy(t->vertices_q16[v], vertices[indices[face][v]], sizeof t->vertices_q16[v]);
    }
}
static void floor_at(World *w, int32_t z, uint32_t kind)
{
    const int32_t v[4][3] = {
        {-4 * Q, -4 * Q, z}, {4 * Q, -4 * Q, z}, {4 * Q, 4 * Q, z}, {-4 * Q, 4 * Q, z}};
    quad(w, v, kind);
}
static void wall_at(World *w, int32_t x)
{
    const int32_t v[4][3] = {
        {x, -4 * Q, -4 * Q}, {x, 4 * Q, -4 * Q}, {x, 4 * Q, 4 * Q}, {x, -4 * Q, 4 * Q}};
    w->surfaces[3].type = 3;
    quad(w, v, 3);
}
static PortableMeshCourse course(World *w, int32_t gravity)
{
    return (PortableMeshCourse){w, w->count, gravity, triangle, surface, NULL, NULL};
}
static PortableMeshItemRules rules(void)
{
    PortableMeshItemRules r = {.profile_id = 41, .revision = 1};
#define MINIMUM(name, lo, hi) r.name = lo;
    PORTABLE_MESH_ITEM_FIELDS(MINIMUM)
#undef MINIMUM
    r.actors_radius_q16 = Q / 2;
    r.shell_restitution_q16 = Q / 2;
    r.shell_max_bounces = 8;
    r.shell_ground_probe_q16 = Q;
    r.shell_ground_snap_q16 = Q / 4;
    r.banana_gravity_q16 = 10 * Q;
    for (unsigned i = 0; i < 10; ++i)
        r.roulette.preview[i] = 0x1000;
    for (unsigned i = 0; i < 27; ++i)
        r.roulette.timeline[i] = 2;
    for (unsigned i = 0; i < 8; ++i)
    {
        r.player_weights[i][0] = (PortableGpItemWeight){0x1000, 1};
        r.cpu_weights[i][0] = (PortableGpItemWeight){0x1000, 1};
    }
    return r;
}
static int ballistic_and_landing(void)
{
    World w;
    reset_world(&w);
    PortableMeshCourse c = course(&w, 10 * Q);
    PortableMeshItemRules r = rules();
    CHECK(portable_mesh_item_rules_valid(&r));
    PortableMeshItemMotionState s = {0};
    PortableMeshItemMotionEvents e = {0};
    const int32_t p[3] = {0, 0, 2 * Q}, zero[3] = {0, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_BANANA, p, zero) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK);
    /* 10m/s² at 4389/262144s: v=-10972 Q16, then dz=-183 Q16. */
    CHECK(s.velocity_q16[2] == -10972 && s.position_q16[2] == 2 * Q - 183 && !s.grounded);
    floor_at(&w, 0, 0);
    c = course(&w, 10 * Q);
    const int32_t falling[3] = {0, 0, -60 * Q};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_BANANA, p, falling) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    for (unsigned i = 0; i < 3 && !s.grounded; ++i)
        CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) ==
              PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(s.grounded && e.landed && e.resting && s.position_q16[2] == Q / 2 && !s.velocity_q16[0] &&
          !s.velocity_q16[1] && !s.velocity_q16[2] && s.contact_surface == 0 &&
          s.respawn_index == 70000);
    const PortableMeshItemMotionState resting = s;
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK &&
          !memcmp(s.position_q16, resting.position_q16, sizeof s.position_q16) &&
          !memcmp(s.velocity_q16, resting.velocity_q16, sizeof s.velocity_q16) && s.grounded &&
          s.contact_triangle == resting.contact_triangle &&
          s.contact_surface == resting.contact_surface && !e.landed && e.resting);
    return 0;
}
static int walls_and_oblique(void)
{
    World w;
    reset_world(&w);
    wall_at(&w, 0);
    PortableMeshCourse c = course(&w, 0);
    PortableMeshItemRules r = rules();
    PortableMeshItemMotionState s = {0};
    PortableMeshItemMotionEvents e = {0};
    const int32_t p[3] = {-3 * Q / 4, 0, 0}, v[3] = {60 * Q, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, p, v) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(s.velocity_q16[0] == -30 * Q && s.bounce_count == 1 && e.wall_impacts == 1 &&
          s.position_q16[0] >= -57505 && s.position_q16[0] <= -57485);
    /* Remaining tick travel follows the reflected direction, not stale +X. */
    CHECK(s.position_q16[0] < -3 * Q / 4);
    const int32_t departing[3] = {-60 * Q, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, p, departing) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK &&
          s.bounce_count == 0 && s.velocity_q16[0] == -60 * Q);
    reset_world(&w);
    w.surfaces[3].type = 3;
    const int32_t plane[4][3] = {{-4 * Q, 4 * Q, -4 * Q},
                                 {4 * Q, -4 * Q, -4 * Q},
                                 {4 * Q, -4 * Q, 4 * Q},
                                 {-4 * Q, 4 * Q, 4 * Q}};
    quad(&w, plane, 3);
    c = course(&w, 0);
    r.shell_restitution_q16 = Q;
    const int32_t op[3] = {-Q, 0, 0}, ov[3] = {120 * Q, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, op, ov) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK);
    /* Elastic reflection from x+y=0 maps +X to -Y. */
    CHECK(s.velocity_q16[0] >= -8 && s.velocity_q16[0] <= 8 && s.velocity_q16[1] >= -120 * Q - 8 &&
          s.velocity_q16[1] <= -120 * Q + 8 && s.bounce_count == 1 && s.position_q16[1] < 0);
    return 0;
}
static int ramp_stack_ceiling(void)
{
    World w;
    reset_world(&w);
    const int32_t ramp[4][3] = {{-4 * Q, -4 * Q, -4 * Q},
                                {4 * Q, -4 * Q, 4 * Q},
                                {4 * Q, 4 * Q, 4 * Q},
                                {-4 * Q, 4 * Q, -4 * Q}};
    quad(&w, ramp, 0);
    PortableMeshCourse c = course(&w, 0);
    PortableMeshItemRules r = rules();
    PortableMeshItemMotionState s = {0};
    PortableMeshItemMotionEvents e = {0};
    const int32_t p[3] = {0, 0, 46342}, v[3] = {2 * Q, 0, 2 * Q};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, p, v) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(s.grounded && s.position_q16[0] > 0 && s.position_q16[2] - s.position_q16[0] >= 46339 &&
          s.position_q16[2] - s.position_q16[0] <= 46344 && s.support_normal_q30[0] < 0 &&
          s.velocity_q16[0] - s.velocity_q16[2] >= -2 &&
          s.velocity_q16[0] - s.velocity_q16[2] <= 2);
    reset_world(&w);
    floor_at(&w, 0, 0);
    floor_at(&w, 4 * Q, 1);
    floor_at(&w, 8 * Q, 2);
    c = course(&w, 10 * Q);
    const int32_t middle[3] = {0, 0, 5 * Q}, down[3] = {0, 0, -60 * Q};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_BANANA, middle, down) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(s.grounded && s.contact_surface == 1 && s.position_q16[2] == 9 * Q / 2 && e.resting);
    reset_world(&w);
    floor_at(&w, 0, 0);
    floor_at(&w, 2 * Q, 1);
    c = course(&w, 10 * Q);
    const int32_t under[3] = {0, 0, Q}, up[3] = {0, 0, 120 * Q};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_BANANA, under, up) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(!s.grounded && !e.resting && e.wall_impacts == 1 && s.velocity_q16[2] == 0 &&
          s.position_q16[2] <= 3 * Q / 2 && s.position_q16[2] >= 3 * Q / 2 - 3);
    return 0;
}
static int corners_and_falloff(void)
{
    World w;
    reset_world(&w);
    wall_at(&w, 0);
    const int32_t wall_y[4][3] = {
        {-4 * Q, 0, -4 * Q}, {4 * Q, 0, -4 * Q}, {4 * Q, 0, 4 * Q}, {-4 * Q, 0, 4 * Q}};
    quad(&w, wall_y, 3);
    PortableMeshCourse c = course(&w, 0);
    PortableMeshItemRules r = rules();
    PortableMeshItemMotionState s = {0};
    PortableMeshItemMotionEvents e = {0};
    const int32_t corner[3] = {-3 * Q / 4, -3 * Q / 4, 0}, velocity[3] = {60 * Q, 60 * Q, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, corner, velocity) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(s.bounce_count == 2 && e.wall_impacts == 2 && s.velocity_q16[0] == -30 * Q &&
          s.velocity_q16[1] == -30 * Q && s.position_q16[0] < corner[0] &&
          s.position_q16[1] < corner[1]);
    reset_world(&w);
    floor_at(&w, 0, 0);
    c = course(&w, 10 * Q);
    const int32_t lip[3] = {4 * Q - Q / 8, 0, Q / 2}, outward[3] = {60 * Q, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, lip, outward) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    s.grounded = 1;
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK &&
          !s.grounded && s.position_q16[0] > 4 * Q && s.velocity_q16[2] == 0);
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) == PORTABLE_MESH_ITEM_MOTION_OK &&
          !s.grounded && s.velocity_q16[2] == -10972 && s.position_q16[2] < Q / 2);
    return 0;
}
/* A translated, hand-authored shallow heightfield: shared .8m cells, no
 * imported source geometry. Its near-tangent shell motion exposed repeated
 * rounded face hits on an ordinary road seam. */
static int heightfield_next(const void *context, const int32_t low[3], const int32_t high[3],
                            uint32_t first, uint32_t *out)
{
    const World *w = context;
    for (uint32_t i = first; i < w->count; ++i)
    {
        int intersects = 1;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            int32_t minimum = w->triangles[i].vertices_q16[0][axis];
            int32_t maximum = minimum;
            for (unsigned v = 1; v < 3; ++v)
            {
                const int32_t value = w->triangles[i].vertices_q16[v][axis];
                if (value < minimum)
                    minimum = value;
                if (value > maximum)
                    maximum = value;
            }
            if (minimum > high[axis] || maximum < low[axis])
                intersects = 0;
        }
        if (intersects)
        {
            *out = i;
            return 1;
        }
    }
    return 0;
}
static int shallow_heightfield_convergence(void)
{
    static const int32_t facets[8][3][3] = {
        {{-61327, -50537, -8003}, {-8898, -50537, -14290}, {-8898, 1892, -15482}},
        {{-61327, -50537, -8003}, {-8898, 1892, -15482}, {-61327, 1892, -9195}},
        {{-61327, 1892, -9195}, {-8898, 1892, -15482}, {-8898, 54320, -16570}},
        {{-61327, 1892, -9195}, {-8898, 54320, -16570}, {-61327, 54320, -11051}},
        {{-8898, -50537, -14290}, {43531, -50537, -20578}, {43531, 1892, -21770}},
        {{-8898, -50537, -14290}, {43531, 1892, -21770}, {-8898, 1892, -15482}},
        {{-8898, 1892, -15482}, {43531, 1892, -21770}, {43531, 54320, -22857}},
        {{-8898, 1892, -15482}, {43531, 54320, -22857}, {-8898, 54320, -16570}}};
    World w;
    reset_world(&w);
    w.count = 8;
    for (unsigned i = 0; i < 8; ++i)
        memcpy(w.triangles[i].vertices_q16, facets[i], sizeof facets[i]);
    PortableMeshCourse linear = course(&w, 10 * Q), indexed = linear;
    indexed.next_triangle = heightfield_next;
    PortableMeshItemRules r = rules();
    r.actors_radius_q16 = Q / 4;
    for (unsigned reverse = 0; reverse < 2; ++reverse)
        for (unsigned grounded = 0; grounded < 2; ++grounded)
            for (unsigned gravity_adjusted = 0; gravity_adjusted < 2; ++gravity_adjusted)
                for (int32_t dz = -2; dz <= 2; ++dz)
                {
                    const int32_t position[3] = {0, 0, dz};
                    int32_t velocity[3] = {-1177387, 1036252, 117650};
                    if (reverse)
                        for (unsigned axis = 0; axis < 3; ++axis)
                            velocity[axis] = -velocity[axis];
                    if (gravity_adjusted)
                        velocity[2] += portable_fixed_3d_dt(linear.gravity_q16);
                    PortableMeshItemMotionState a;
                    CHECK(portable_mesh_item_motion_init(&a, PORTABLE_MESH_ITEM_SHELL, position,
                                                         velocity) == PORTABLE_MESH_ITEM_MOTION_OK);
                    a.grounded = (uint8_t)grounded;
                    const PortableMeshItemMotionState before = a;
                    PortableMeshItemMotionState b = a;
                    PortableMeshItemMotionEvents ea = {17, 1, 1}, eb = ea;
                    PortableMeshItemMotionFault fa = {0}, fb = {0};
                    const PortableMeshItemMotionStatus sa =
                        portable_mesh_item_motion_tick_diagnostic(&a, &r, &linear, U / 2, &ea, &fa);
                    const PortableMeshItemMotionStatus sb =
                        portable_mesh_item_motion_tick_diagnostic(&b, &r, &indexed, U / 2, &eb,
                                                                  &fb);
                    CHECK(sa == sb && !memcmp(&a, &b, sizeof a) && !memcmp(&ea, &eb, sizeof ea));
                    CHECK(sa == PORTABLE_MESH_ITEM_MOTION_OK && !ea.wall_impacts &&
                          !a.bounce_count);
                    CHECK(a.position_q16[0] != before.position_q16[0] &&
                          a.position_q16[1] != before.position_q16[1]);
                    CHECK(!memcmp(&fa, &fb, sizeof fa));
                }
    /* A genuinely resting banana on the same facets must not gain energy or
     * exhaust iterations while support removes the outward rounding skin. */
    const int32_t above[3] = {0, 0, Q}, zero[3] = {0, 0, 0};
    PortableMeshSupportHit support;
    CHECK(portable_mesh_query_support(&linear, above, r.actors_radius_q16, 2 * Q, U / 2,
                                      &support) == PORTABLE_MESH_QUERY_HIT);
    PortableMeshItemMotionState banana;
    CHECK(portable_mesh_item_motion_init(&banana, PORTABLE_MESH_ITEM_BANANA, support.center_q16,
                                         zero) == PORTABLE_MESH_ITEM_MOTION_OK);
    banana.grounded = 1;
    PortableMeshItemMotionState indexed_banana = banana;
    for (unsigned tick = 0; tick < 12; ++tick)
    {
        PortableMeshItemMotionEvents a, b;
        CHECK(portable_mesh_item_motion_tick(&banana, &r, &linear, U / 2, &a) ==
              PORTABLE_MESH_ITEM_MOTION_OK);
        CHECK(portable_mesh_item_motion_tick(&indexed_banana, &r, &indexed, U / 2, &b) ==
              PORTABLE_MESH_ITEM_MOTION_OK);
        CHECK(!memcmp(&banana, &indexed_banana, sizeof banana) && !memcmp(&a, &b, sizeof a));
        CHECK(!banana.position_q16[0] && !banana.position_q16[1]);
        CHECK(!banana.velocity_q16[0] && !banana.velocity_q16[1] && !banana.velocity_q16[2] &&
              !a.wall_impacts);
    }
    return 0;
}
static int failures(void)
{
    World w;
    reset_world(&w);
    floor_at(&w, 0, 0);
    w.surfaces[0].type = 6;
    PortableMeshCourse c = course(&w, 0);
    PortableMeshItemRules r = rules();
    PortableMeshItemMotionState s = {0};
    PortableMeshItemMotionEvents e = {17, 1, 1};
    const PortableMeshItemMotionEvents before_events = e;
    const int32_t p[3] = {0, 0, Q / 2}, v[3] = {Q, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, p, v) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    PortableMeshItemMotionState before = s;
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) ==
              PORTABLE_MESH_ITEM_MOTION_OUTSIDE &&
          !memcmp(&s, &before, sizeof s) && !memcmp(&e, &before_events, sizeof e));
    w.surfaces[0].type = 0;
    w.fail = 1;
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) ==
              PORTABLE_MESH_ITEM_MOTION_INVALID &&
          !memcmp(&s, &before, sizeof s) && !memcmp(&e, &before_events, sizeof e));
    w.fail = UINT32_MAX;
    r.actors_radius_q16 = 0;
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) ==
              PORTABLE_MESH_ITEM_MOTION_INVALID &&
          !memcmp(&s, &before, sizeof s) && !memcmp(&e, &before_events, sizeof e));
    r = rules();
    reset_world(&w);
    wall_at(&w, 0);
    wall_at(&w, 3 * Q / 4);
    c = course(&w, 0);
    const int32_t trapped[3] = {3 * Q / 8, 0, 0}, zero[3] = {0, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, trapped, zero) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    before = s;
    PortableMeshItemMotionFault fault;
    memset(&fault, 0xa5, sizeof fault);
    CHECK(portable_mesh_item_motion_tick_diagnostic(&s, &r, &c, U / 2, &e, &fault) ==
              PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET &&
          !memcmp(&s, &before, sizeof s) && !memcmp(&e, &before_events, sizeof e));
    CHECK(fault.phase == PORTABLE_MESH_ITEM_FAULT_OVERLAP && fault.iteration == 8 &&
          fault.triangle < w.count && fault.radius_q16 == r.actors_radius_q16 &&
          !memcmp(fault.position_q16, before.position_q16, sizeof fault.position_q16) &&
          !memcmp(fault.velocity_q16, before.velocity_q16, sizeof fault.velocity_q16));
    reset_world(&w);
    wall_at(&w, 0);
    c = course(&w, 0);
    const int32_t incoming[3] = {-3 * Q / 4, 0, 0}, fast[3] = {60 * Q, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, incoming, fast) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    s.bounce_count = (uint32_t)r.shell_max_bounces;
    before = s;
    CHECK(portable_mesh_item_motion_tick(&s, &r, &c, U / 2, &e) ==
              PORTABLE_MESH_ITEM_MOTION_BOUNCE_LIMIT &&
          !memcmp(&s, &before, sizeof s) && !memcmp(&e, &before_events, sizeof e));
    reset_world(&w);
    c = course(&w, 0);
    r.actors_radius_q16 = 3277;
    const int32_t diagonal[3] = {256 * Q, 256 * Q, 256 * Q};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, p, diagonal) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    before = s;
    CHECK(portable_mesh_item_motion_tick_diagnostic(&s, &r, &c, U / 2, &e, &fault) ==
              PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET &&
          !memcmp(&s, &before, sizeof s) && !memcmp(&e, &before_events, sizeof e));
    CHECK(fault.phase == PORTABLE_MESH_ITEM_FAULT_SUBSTEPS && fault.iteration > 256 &&
          fault.triangle == UINT32_MAX && fault.radius_q16 == r.actors_radius_q16);
    r = rules();
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, p, zero) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    PortableMeshItemMotionState regular = s;
    PortableMeshItemMotionEvents regular_events;
    const PortableMeshItemMotionFault saved_fault = fault;
    CHECK(portable_mesh_item_motion_tick_diagnostic(&s, &r, &c, U / 2, &e, &fault) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(portable_mesh_item_motion_tick(&regular, &r, &c, U / 2, &regular_events) ==
          PORTABLE_MESH_ITEM_MOTION_OK);
    CHECK(!memcmp(&s, &regular, sizeof s) && !memcmp(&e, &regular_events, sizeof e) &&
          !memcmp(&fault, &saved_fault, sizeof fault));
    r.actors_radius_q16 = 0;
    CHECK(portable_mesh_item_motion_tick_diagnostic(&s, &r, &c, U / 2, &e, &fault) ==
          PORTABLE_MESH_ITEM_MOTION_INVALID);
    CHECK(!memcmp(&fault, &saved_fault, sizeof fault));
    before = s;
    const int32_t outside[3] = {PORTABLE_FIXED_3D_POSITION_LIMIT + 1, 0, 0};
    CHECK(portable_mesh_item_motion_init(&s, PORTABLE_MESH_ITEM_SHELL, outside, zero) ==
              PORTABLE_MESH_ITEM_MOTION_INVALID &&
          !memcmp(&s, &before, sizeof s));
    CHECK(!strcmp(portable_mesh_item_motion_status_name(PORTABLE_MESH_ITEM_MOTION_COLLISION_BUDGET),
                  "COLLISION_BUDGET"));
    return 0;
}
int main(void)
{
    CHECK(ballistic_and_landing() == 0);
    CHECK(walls_and_oblique() == 0);
    CHECK(ramp_stack_ceiling() == 0);
    CHECK(corners_and_falloff() == 0);
    CHECK(failures() == 0);
    CHECK(shallow_heightfield_convergence() == 0);
    puts("Mesh item motion: PASS");
    return 0;
}
