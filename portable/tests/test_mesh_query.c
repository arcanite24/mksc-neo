#include "mesh_query.h"
#include "fixed_3d.h"
#include <stdio.h>
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "mesh query line%d: %s\n", __LINE__, #c);                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int face_edge_vertex(void)
{
    const PortableMeshTriangle triangle = {
        .vertices_q16 = {{0, 0, 0}, {4 * Q, 0, 0}, {0, 4 * Q, 0}}, .surface = 17, .respawn = 23};
    const PortableMeshTriangle original = triangle;
    int32_t normal[3];
    CHECK(portable_mesh_query_normal(&triangle, normal));
    CHECK(normal[0] == 0 && normal[1] == 0 && normal[2] == U);
    const struct
    {
        int32_t point[3], closest[3];
        int inside;
    } cases[] = {
        {{Q, Q, 2 * Q}, {Q, Q, 0}, 1},     {{2 * Q, 3 * Q, 2 * Q}, {3 * Q / 2, 5 * Q / 2, 0}, 0},
        {{-Q, -Q, 2 * Q}, {0, 0, 0}, 0},   {{2 * Q, -Q, 2 * Q}, {2 * Q, 0, 0}, 0},
        {{4 * Q, 0, 0}, {4 * Q, 0, 0}, 1}, {{2 * Q, 2 * Q, -2 * Q}, {2 * Q, 2 * Q, 0}, 1},
        {{0, 0, Q}, {0, 0, 0}, 1}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        int32_t closest[3] = {123, 456, 789};
        portable_mesh_query_closest(&triangle, cases[i].point, normal, closest);
        CHECK(!memcmp(closest, cases[i].closest, sizeof closest));
        CHECK(portable_mesh_query_inside(&triangle, cases[i].point, normal) == cases[i].inside);
        CHECK(!memcmp(&triangle, &original, sizeof triangle));
    }
    PortableMeshTriangle reversed = triangle;
    memcpy(reversed.vertices_q16[1], triangle.vertices_q16[2], sizeof triangle.vertices_q16[2]);
    memcpy(reversed.vertices_q16[2], triangle.vertices_q16[1], sizeof triangle.vertices_q16[1]);
    CHECK(portable_mesh_query_normal(&reversed, normal) && normal[2] == -U);
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        int32_t closest[3];
        portable_mesh_query_closest(&reversed, cases[i].point, normal, closest);
        CHECK(!memcmp(closest, cases[i].closest, sizeof closest));
        CHECK(portable_mesh_query_inside(&reversed, cases[i].point, normal) == cases[i].inside);
    }
    return 0;
}
static int vertical_and_bounds(void)
{
    const PortableMeshTriangle vertical = {
        .vertices_q16 = {{2 * Q, 0, 0}, {2 * Q, 4 * Q, 0}, {2 * Q, 0, 4 * Q}}};
    const int32_t point[3] = {5 * Q, Q, Q}, expected[3] = {2 * Q, Q, Q};
    int32_t normal[3], closest[3];
    CHECK(portable_mesh_query_normal(&vertical, normal) && normal[0] == U);
    CHECK(portable_mesh_query_inside(&vertical, point, normal));
    portable_mesh_query_closest(&vertical, point, normal, closest);
    CHECK(!memcmp(closest, expected, sizeof closest));
    const int32_t bound = PORTABLE_FIXED_3D_POSITION_LIMIT;
    const PortableMeshTriangle large = {
        .vertices_q16 = {{-bound, -bound, bound}, {bound, -bound, bound}, {-bound, bound, bound}}};
    const int32_t opposite[3] = {0, 0, -bound}, on_edge[3] = {0, 0, bound};
    CHECK(portable_mesh_query_normal(&large, normal) && normal[2] == U);
    CHECK(portable_mesh_query_inside(&large, opposite, normal));
    portable_mesh_query_closest(&large, opposite, normal, closest);
    CHECK(!memcmp(closest, on_edge, sizeof closest));
    const int32_t vertex[3] = {-bound, -bound, -bound}, on_vertex[3] = {-bound, -bound, bound};
    portable_mesh_query_closest(&large, vertex, normal, closest);
    CHECK(!memcmp(closest, on_vertex, sizeof closest));
    return 0;
}
static int degenerates(void)
{
    PortableMeshTriangle triangle = {.vertices_q16 = {{0, 0, 0}, {Q, 0, 0}, {2 * Q, 0, 0}}};
    int32_t normal[3] = {17, 23, 29};
    const int32_t before[3] = {17, 23, 29};
    CHECK(!portable_mesh_query_normal(&triangle, normal));
    CHECK(!memcmp(normal, before, sizeof normal));
    memset(&triangle, 0, sizeof triangle);
    CHECK(!portable_mesh_query_normal(&triangle, normal));
    CHECK(!memcmp(normal, before, sizeof normal));
    return 0;
}
static int segment_spheres(void)
{
    const uint64_t one = PORTABLE_MESH_QUERY_FRACTION_ONE;
    const struct
    {
        int32_t before[3], after[3], center[3], radius;
        PortableMeshQueryStatus status;
        uint64_t entry;
    } cases[] = {
        {{-2 * Q, 0, 0},
         {2 * Q, 0, 0},
         {0, 0, 0},
         Q,
         PORTABLE_MESH_QUERY_HIT,
         UINT64_C(1073741824)},
        {{0, 0, 0}, {2 * Q, 0, 0}, {0, 0, 0}, Q, PORTABLE_MESH_QUERY_HIT, 0},
        {{-2 * Q, 0, 0}, {-Q, 0, 0}, {0, 0, 0}, Q, PORTABLE_MESH_QUERY_HIT, UINT64_C(4294967296)},
        {{-2 * Q, Q, 0},
         {2 * Q, Q, 0},
         {0, 0, 0},
         Q,
         PORTABLE_MESH_QUERY_HIT,
         UINT64_C(2147483648)},
        /* Tangency at 1/3 cannot be represented by Q32; it still hits. */
        {{0, Q, 0}, {3 * Q, Q, 0}, {Q, 0, 0}, Q, PORTABLE_MESH_QUERY_HIT, UINT64_C(1431655766)},
        {{-2 * Q, Q + 1, 0}, {2 * Q, Q + 1, 0}, {0, 0, 0}, Q, PORTABLE_MESH_QUERY_MISS, 0},
        {{2 * Q, 0, 0}, {3 * Q, 0, 0}, {0, 0, 0}, Q, PORTABLE_MESH_QUERY_MISS, 0},
        {{2 * Q, 0, 0}, {2 * Q, 0, 0}, {0, 0, 0}, Q, PORTABLE_MESH_QUERY_MISS, 0},
        {{Q, 0, 0}, {Q, 0, 0}, {0, 0, 0}, Q, PORTABLE_MESH_QUERY_HIT, 0},
        {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, 1, PORTABLE_MESH_QUERY_HIT, 0},
        {{0, 0, -2 * Q},
         {0, 0, 2 * Q},
         {0, 0, 0},
         Q,
         PORTABLE_MESH_QUERY_HIT,
         UINT64_C(1073741824)},
        /* Same XY segment below a different road must miss. */
        {{-2 * Q, 0, 0}, {2 * Q, 0, 0}, {0, 0, 2 * Q}, Q, PORTABLE_MESH_QUERY_MISS, 0}};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        uint64_t entry = UINT64_C(0x123456789abcdef0);
        CHECK(portable_mesh_query_segment_sphere(cases[i].before, cases[i].after, cases[i].center,
                                                 cases[i].radius, &entry) == cases[i].status);
        CHECK(entry == (cases[i].status == PORTABLE_MESH_QUERY_HIT ? cases[i].entry
                                                                   : UINT64_C(0x123456789abcdef0)));
    }
    const int32_t limit = PORTABLE_FIXED_3D_POSITION_LIMIT;
    const int32_t before[3] = {-limit, 1, 0}, after[3] = {limit, 1, 0}, center[3] = {12345, 0, 0};
    const int32_t before_copy[3] = {-limit, 1, 0}, after_copy[3] = {limit, 1, 0},
                  center_copy[3] = {12345, 0, 0};
    uint64_t entry = 0;
    /* A 1 LSB sphere far along an 8192m segment falls between old Q16 samples. */
    CHECK(portable_mesh_query_segment_sphere(before, after, center, 1, &entry) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(entry == ((uint64_t)limit + 12345u) * 8u);
    CHECK(!memcmp(before, before_copy, sizeof before) && !memcmp(after, after_copy, sizeof after) &&
          !memcmp(center, center_copy, sizeof center));
    const int32_t diagonal_start[3] = {-limit, -limit, 0}, diagonal_end[3] = {limit, limit, 0},
                  diagonal_center[3] = {12345, 12345, 1};
    CHECK(portable_mesh_query_segment_sphere(diagonal_start, diagonal_end, diagonal_center, 1,
                                             &entry) == PORTABLE_MESH_QUERY_HIT);
    CHECK(entry == ((uint64_t)limit + 12345u) * 8u);
    const int32_t axis_start[3] = {-limit, 0, 0}, axis_end[3] = {limit, 0, 0};
    CHECK(portable_mesh_query_segment_sphere(axis_start, axis_end, axis_end, 1, &entry) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(entry == one - 8u); /* Initial relative X is -8192m. */
    /* A full-domain three-axis line has direction (2,1,-2). The offset
     * (1,2,2) is perpendicular and exactly three Q16 LSBs long. */
    const int32_t full_start[3] = {-2 * limit, -limit, 2 * limit};
    const int32_t full_end[3] = {2 * limit, limit, -2 * limit};
    const int32_t tangent_center[3] = {1, 2, 2};
    CHECK(portable_mesh_query_segment_sphere(full_start, full_end, tangent_center, 3, &entry) ==
              PORTABLE_MESH_QUERY_HIT &&
          entry == one / 2u);
    CHECK(portable_mesh_query_segment_sphere(full_start, full_end, tangent_center, 2, &entry) ==
              PORTABLE_MESH_QUERY_MISS &&
          entry == one / 2u);
    const uint64_t untouched = entry;
    CHECK(portable_mesh_query_segment_sphere(NULL, after, center, 1, &entry) ==
              PORTABLE_MESH_QUERY_INVALID &&
          entry == untouched);
    CHECK(portable_mesh_query_segment_sphere(before, NULL, center, 1, &entry) ==
              PORTABLE_MESH_QUERY_INVALID &&
          entry == untouched);
    CHECK(portable_mesh_query_segment_sphere(before, after, NULL, 1, &entry) ==
              PORTABLE_MESH_QUERY_INVALID &&
          entry == untouched);
    CHECK(portable_mesh_query_segment_sphere(before, after, center, 0, &entry) ==
              PORTABLE_MESH_QUERY_INVALID &&
          entry == untouched);
    CHECK(portable_mesh_query_segment_sphere(before, after, center, -1, &entry) ==
              PORTABLE_MESH_QUERY_INVALID &&
          entry == untouched);
    CHECK(portable_mesh_query_segment_sphere(before, after, center, 1, NULL) ==
          PORTABLE_MESH_QUERY_INVALID);
    const int32_t invalid[3] = {2 * limit + 1, 0, 0};
    CHECK(portable_mesh_query_segment_sphere(invalid, after, center, 1, &entry) ==
              PORTABLE_MESH_QUERY_INVALID &&
          entry == untouched);
    return 0;
}
typedef struct
{
    PortableMeshTriangle triangles[6];
    PortableMeshSurface surfaces[3];
    uint32_t invalid_triangle, invalid_surface;
} SupportWorld;
static int support_triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    const SupportWorld *world = context;
    if (index >= 6u || index == world->invalid_triangle)
        return 0;
    *out = world->triangles[index];
    return 1;
}
static int support_surface(const void *context, uint32_t index, PortableMeshSurface *out)
{
    const SupportWorld *world = context;
    if (index >= 3u || index == world->invalid_surface)
        return 0;
    *out = world->surfaces[index];
    return 1;
}
static void stacked_world(SupportWorld *world)
{
    memset(world, 0, sizeof *world);
    world->invalid_triangle = world->invalid_surface = UINT32_MAX;
    const int32_t xy[4][2] = {{-4 * Q, -4 * Q}, {4 * Q, -4 * Q}, {4 * Q, 4 * Q}, {-4 * Q, 4 * Q}};
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned floor = 0; floor < 3u; ++floor)
        for (unsigned face = 0; face < 2u; ++face)
        {
            PortableMeshTriangle *triangle = &world->triangles[floor * 2u + face];
            triangle->surface = floor;
            triangle->respawn = 70000u + floor;
            for (unsigned v = 0; v < 3u; ++v)
            {
                triangle->vertices_q16[v][0] = xy[indices[face][v]][0];
                triangle->vertices_q16[v][1] = xy[indices[face][v]][1];
                triangle->vertices_q16[v][2] = (int32_t)floor * 4 * Q;
            }
        }
    world->surfaces[1].type = 6; /* Hazard reported, not acted on by query. */
}
static int sphere_support(void)
{
    SupportWorld world;
    stacked_world(&world);
    const PortableMeshCourse course = {&world, 6, 10 * Q, support_triangle, support_surface, NULL, NULL};
    const SupportWorld original = world;
    PortableMeshSupportHit hit = {0};
    int32_t center[3] = {0, 0, 5 * Q};
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(hit.triangle == 2 && hit.surface == 1 && hit.type == 6 && hit.respawn == 70001 &&
          hit.center_q16[0] == 0 && hit.center_q16[1] == 0 && hit.center_q16[2] == 9 * Q / 2 &&
          hit.drop_q16 == Q / 2 && hit.normal_q30[2] == U);
    CHECK(!memcmp(&world, &original, sizeof world) && center[2] == 5 * Q);
    center[2] = 3 * Q;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.triangle == 0 && hit.center_q16[2] == Q / 2 && hit.type == 0);
    /* Even a partly intersected overhead floor cannot pull the center up. */
    center[2] = 8 * Q + Q / 4;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.triangle == 2 && hit.center_q16[2] == 9 * Q / 2);
    center[2] = 9 * Q / 2;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 0, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.drop_q16 == 0);
    const PortableMeshSupportHit before = hit;
    center[2] = 5 * Q;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, Q / 4, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_MISS &&
          !memcmp(&hit, &before, sizeof hit));
    center[0] = 8 * Q;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_MISS &&
          !memcmp(&hit, &before, sizeof hit));
    center[0] = 0;
    world.surfaces[1].type = 3;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.surface == 0);
    world = original;
    hit = before;
    world.invalid_triangle = 5; /* Failure after earlier valid support is atomic. */
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    world = original;
    world.invalid_surface = 2;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    world = original;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, -1, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    CHECK(portable_mesh_query_support(&course, center, 0, Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    CHECK(portable_mesh_query_support(&course, center, Q / 2, Q, 0, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    CHECK(portable_mesh_query_support(NULL, center, Q / 2, Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    PortableMeshCourse invalid_callback = course;
    invalid_callback.triangle = NULL;
    CHECK(portable_mesh_query_support(&invalid_callback, center, Q / 2, Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    invalid_callback = course;
    invalid_callback.surface = NULL;
    CHECK(portable_mesh_query_support(&invalid_callback, center, Q / 2, Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    PortableMeshCourse empty = course;
    empty.triangle_count = 0;
    CHECK(portable_mesh_query_support(&empty, center, Q / 2, Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_MISS &&
          !memcmp(&hit, &before, sizeof hit));
    world.triangles[5].vertices_q16[0][0] = PORTABLE_FIXED_3D_POSITION_LIMIT + 1;
    CHECK(portable_mesh_query_support(&course, center, Q / 2, 6 * Q, U / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &before, sizeof hit));
    return 0;
}
static int face_sweeps(void)
{
    SupportWorld world;
    stacked_world(&world);
    PortableMeshCourse course = {&world, 6, 0, support_triangle, support_surface, NULL, NULL};
    const uint64_t one = PORTABLE_MESH_QUERY_FRACTION_ONE;
    const int32_t above[3] = {0, 0, 10 * Q}, below[3] = {0, 0, -2 * Q};
    PortableMeshFaceHit hit = {0};
    CHECK(portable_mesh_query_sweep_faces(&course, above, below, Q / 2, &hit) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(hit.triangle == 4 && hit.surface == 2 && hit.respawn == 70002 &&
          hit.fraction_q32 == one / 8 && hit.normal_q30[2] == U &&
          hit.center_q16[2] == 17 * Q / 2 && hit.point_q16[2] == 8 * Q);
    CHECK(portable_mesh_query_sweep_faces(&course, below, above, Q / 2, &hit) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(hit.triangle == 0 && hit.normal_q30[2] == -U && hit.fraction_q32 == one / 8 &&
          hit.center_q16[2] == -Q / 2 && hit.point_q16[2] == 0);
    /* Endpoint contact and a touching inward start are both inclusive. */
    course.triangle_count = 2;
    const int32_t start[3] = {0, 0, 2 * Q}, touch[3] = {0, 0, Q / 2};
    CHECK(portable_mesh_query_sweep_faces(&course, start, touch, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.fraction_q32 == one);
    CHECK(portable_mesh_query_sweep_faces(&course, touch, below, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.fraction_q32 == 0);
    const PortableMeshFaceHit preserved = hit;
    const int32_t penetrating[3] = {0, 0, Q / 4}, outside_start[3] = {5 * Q, 0, 2 * Q},
                  outside_end[3] = {5 * Q, 0, -2 * Q};
    CHECK(portable_mesh_query_sweep_faces(&course, penetrating, below, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_MISS &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_sweep_faces(&course, touch, start, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_MISS &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_sweep_faces(&course, outside_start, outside_end, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_MISS &&
          !memcmp(&hit, &preserved, sizeof hit));
    /* A thin vertical wall cannot be skipped by a long crossing. */
    for (unsigned face = 0; face < 2; ++face)
        for (unsigned vertex = 0; vertex < 3; ++vertex)
        {
            int32_t *p = world.triangles[face].vertices_q16[vertex];
            p[2] = p[0];
            p[0] = 0;
        }
    world.surfaces[0].type = 3;
    const int32_t left[3] = {-2 * Q, 0, 0}, right[3] = {2 * Q, 0, 0};
    CHECK(portable_mesh_query_sweep_faces(&course, left, right, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.normal_q30[0] == -U && hit.fraction_q32 == 3 * one / 8 &&
          hit.center_q16[0] == -Q / 2 && hit.point_q16[0] == 0 && hit.type == 3);
    CHECK(portable_mesh_query_sweep_faces(&course, right, left, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.normal_q30[0] == U && hit.fraction_q32 == 3 * one / 8);
    /* Winding changes do not change the approached side. */
    for (unsigned face = 0; face < 2; ++face)
    {
        int32_t swap[3];
        memcpy(swap, world.triangles[face].vertices_q16[1], sizeof swap);
        memcpy(world.triangles[face].vertices_q16[1], world.triangles[face].vertices_q16[2],
               sizeof swap);
        memcpy(world.triangles[face].vertices_q16[2], swap, sizeof swap);
    }
    CHECK(portable_mesh_query_sweep_faces(&course, left, right, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.normal_q30[0] == -U);
    /* An oblique plane z=x: center clearance is radius * sqrt(2). */
    stacked_world(&world);
    for (unsigned face = 0; face < 2; ++face)
        for (unsigned vertex = 0; vertex < 3; ++vertex)
        {
            int32_t *p = world.triangles[face].vertices_q16[vertex];
            p[2] = p[0];
        }
    CHECK(portable_mesh_query_sweep_faces(&course, start, below, Q, &hit) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(hit.normal_q30[0] < -759250000 && hit.normal_q30[2] > 759250000 &&
          hit.center_q16[2] >= 92680 && hit.center_q16[2] <= 92683 &&
          hit.point_q16[0] - hit.point_q16[2] >= -2 && hit.point_q16[0] - hit.point_q16[2] <= 2);
    /* Later invalid geometry must discard an earlier matching face. */
    stacked_world(&world);
    course.triangle_count = 6;
    hit = preserved;
    world.invalid_triangle = 5;
    CHECK(portable_mesh_query_sweep_faces(&course, above, below, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    world.invalid_triangle = UINT32_MAX;
    world.invalid_surface = 2;
    CHECK(portable_mesh_query_sweep_faces(&course, above, below, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    world.invalid_surface = UINT32_MAX;
    world.triangles[5].vertices_q16[0][0] = PORTABLE_FIXED_3D_POSITION_LIMIT + 1;
    CHECK(portable_mesh_query_sweep_faces(&course, above, below, Q / 2, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    stacked_world(&world);
    const int32_t invalid[3] = {PORTABLE_FIXED_3D_POSITION_LIMIT + 1, 0, 0};
    CHECK(portable_mesh_query_sweep_faces(&course, invalid, below, Q, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_sweep_faces(&course, above, below, 0, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_sweep_faces(&course, above, below,
                                          PORTABLE_FIXED_3D_POSITION_LIMIT + 1,
                                          &hit) == PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    /* Inclusive world bounds and a one-LSB sphere crossing the entire domain. */
    const int32_t bound = PORTABLE_FIXED_3D_POSITION_LIMIT;
    for (unsigned face = 0; face < 2; ++face)
        for (unsigned vertex = 0; vertex < 3; ++vertex)
        {
            int32_t *p = world.triangles[face].vertices_q16[vertex];
            p[0] = p[0] < 0 ? -bound : bound;
            p[1] = p[1] < 0 ? -bound : bound;
        }
    course.triangle_count = 2;
    const int32_t full_start[3] = {bound, -bound, bound};
    const int32_t full_end[3] = {bound, -bound, -bound};
    CHECK(portable_mesh_query_sweep_faces(&course, full_start, full_end, 1, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.fraction_q32 == one / 2 - 8u && hit.center_q16[2] == 1 && hit.point_q16[2] == 0 &&
          hit.triangle == 0);
    CHECK(portable_mesh_query_sweep_faces(&course, full_start, full_end, bound, &hit) ==
              PORTABLE_MESH_QUERY_HIT &&
          hit.fraction_q32 == 0);
    hit = preserved;
    course.triangle = NULL;
    CHECK(portable_mesh_query_sweep_faces(&course, above, below, Q, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    return 0;
}
static int endpoint_overlaps(void)
{
    SupportWorld world;
    stacked_world(&world);
    const PortableMeshCourse course = {&world, 6, 0, support_triangle, support_surface, NULL, NULL};
    /* Lower-right half of the square: face, diagonal edge, corner. */
    const struct
    {
        int32_t center[3], point[3], radius, penetration;
        int interior;
    } cases[] = {{{Q, -Q, Q / 4}, {Q, -Q, 0}, Q / 2, Q / 4, 1},
                 {{Q, -Q, -Q / 4}, {Q, -Q, 0}, Q / 2, Q / 4, 1},
                 {{0, 0, 0}, {0, 0, 0}, 1, 1, 1},
                 {{-Q, Q, 0}, {0, 0, 0}, 2 * Q, 2 * Q - 92681, 0},
                 {{5 * Q, -4 * Q, 0}, {4 * Q, -4 * Q, 0}, Q, 0, 0}};
    PortableMeshOverlapHit hit = {0};
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        CHECK(portable_mesh_query_overlap(&course, 0, cases[i].center, cases[i].radius, &hit) ==
              PORTABLE_MESH_QUERY_HIT);
        CHECK(!memcmp(hit.point_q16, cases[i].point, sizeof cases[i].point) &&
              hit.penetration_q16 == cases[i].penetration &&
              hit.face_interior == cases[i].interior && hit.triangle == 0 && hit.surface == 0 &&
              hit.respawn == 70000);
        if (i == 0)
            CHECK(hit.normal_q30[2] == U);
        if (i == 1)
            CHECK(hit.normal_q30[2] == -U);
        if (i == 2)
            CHECK(hit.normal_q30[2] == U);
        if (i == 3)
            CHECK(hit.normal_q30[0] < 0 && hit.normal_q30[1] > 0 && hit.normal_q30[2] == 0);
        if (i == 4)
            CHECK(hit.normal_q30[0] == U);
    }
    const PortableMeshOverlapHit preserved = hit;
    const int32_t miss[3] = {Q, -Q, Q}, invalid[3] = {0, 0, PORTABLE_FIXED_3D_POSITION_LIMIT + 1};
    CHECK(portable_mesh_query_overlap(&course, 0, miss, Q / 2, &hit) == PORTABLE_MESH_QUERY_MISS &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_overlap(&course, 6, miss, Q, &hit) == PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_overlap(&course, 0, invalid, Q, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_overlap(&course, 0, miss, -1, &hit) == PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_overlap(&course, 0, miss, PORTABLE_FIXED_3D_POSITION_LIMIT + 1,
                                      &hit) == PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    CHECK(portable_mesh_query_overlap(&course, 0, miss, Q, NULL) == PORTABLE_MESH_QUERY_INVALID);
    PortableMeshCourse missing_callback = course;
    missing_callback.surface = NULL;
    CHECK(portable_mesh_query_overlap(&missing_callback, 0, miss, Q, &hit) ==
              PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    world.invalid_surface = 0;
    CHECK(portable_mesh_query_overlap(&course, 0, miss, Q, &hit) == PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    world.invalid_surface = UINT32_MAX;
    memset(&world.triangles[0], 0, sizeof world.triangles[0]);
    CHECK(portable_mesh_query_overlap(&course, 0, miss, Q, &hit) == PORTABLE_MESH_QUERY_INVALID &&
          !memcmp(&hit, &preserved, sizeof hit));
    return 0;
}
typedef struct
{
    SupportWorld world;
    unsigned calls;
    int invalid;
} IndexedWorld;

static int indexed_next(const void *context, const int32_t low[3], const int32_t high[3],
                        uint32_t first, uint32_t *out)
{
    IndexedWorld *world = (IndexedWorld *)context;
    ++world->calls;
    if (world->invalid)
    {
        *out = world->invalid == 1 && first ? first - 1u : 6u;
        return world->invalid == 3 ? -1 : world->invalid == 4 ? 2 : 1;
    }
    for (uint32_t i = first; i < 6u; ++i)
    {
        int overlaps = 1;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            int32_t minimum = INT32_MAX, maximum = INT32_MIN;
            for (unsigned v = 0; v < 3u; ++v)
            {
                const int32_t value = world->world.triangles[i].vertices_q16[v][axis];
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

static int indexed_queries(void)
{
    IndexedWorld world = {0};
    stacked_world(&world.world);
    PortableMeshCourse linear = {&world, 6, 0, support_triangle, support_surface, NULL, NULL};
    PortableMeshCourse indexed = linear;
    indexed.next_triangle = indexed_next;
    const int32_t above[3] = {0, 0, 10 * Q}, below[3] = {0, 0, -2 * Q};
    PortableMeshFaceHit a = {0}, b = {0};
    CHECK(portable_mesh_query_sweep_faces(&linear, above, below, Q / 2, &a) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(portable_mesh_query_sweep_faces(&indexed, above, below, Q / 2, &b) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(!memcmp(&a, &b, sizeof a) && b.triangle == 4);
    PortableMeshSupportHit c = {0}, d = {0};
    const int32_t center[3] = {0, 0, 3 * Q};
    CHECK(portable_mesh_query_support(&linear, center, Q / 2, 3 * Q, U / 2, &c) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(portable_mesh_query_support(&indexed, center, Q / 2, 3 * Q, U / 2, &d) ==
          PORTABLE_MESH_QUERY_HIT);
    CHECK(!memcmp(&c, &d, sizeof c) && d.triangle == 0 && world.calls);
    /* Linear owners still visit invalid distant rows, even after finding a
     * near floor. An index is only legal after all rows were prevalidated. */
    world.world.invalid_triangle = 5;
    const PortableMeshSupportHit preserved = c;
    CHECK(portable_mesh_query_support(&linear, center, Q / 2, 3 * Q, U / 2, &c) ==
          PORTABLE_MESH_QUERY_INVALID);
    CHECK(!memcmp(&c, &preserved, sizeof c));
    world.world.invalid_triangle = UINT32_MAX;
    for (world.invalid = 1; world.invalid <= 4; ++world.invalid)
    {
        CHECK(portable_mesh_query_sweep_faces(&indexed, above, below, Q / 2, &b) ==
              PORTABLE_MESH_QUERY_INVALID);
        CHECK(!memcmp(&a, &b, sizeof a));
    }
    int32_t low[3], high[3];
    const int32_t maximum[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    const int32_t minimum[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    CHECK(portable_mesh_query_bounds(minimum, maximum, INT32_MAX, low, high));
    CHECK(!memcmp(low, minimum, sizeof low) && !memcmp(high, maximum, sizeof high));
    CHECK(!portable_mesh_query_bounds(center, center, -1, low, high));
    CHECK(!memcmp(low, minimum, sizeof low) && !memcmp(high, maximum, sizeof high));
    uint32_t output = 123;
    world.invalid = 1;
    CHECK(portable_mesh_query_next_triangle(&indexed, low, high, 2, &output) ==
              PORTABLE_MESH_QUERY_INVALID &&
          output == 123);
    indexed.next_triangle = NULL;
    CHECK(portable_mesh_query_next_triangle(&indexed, low, high, 2, &output) ==
              PORTABLE_MESH_QUERY_HIT &&
          output == 2);
    CHECK(portable_mesh_query_next_triangle(&indexed, low, high, 6, &output) ==
              PORTABLE_MESH_QUERY_MISS &&
          output == 2);
    return 0;
}

/* Floor continuation uses exact authored connectivity, not a vertical ray at
 * each new position. A bridge deliberately covers the same XY as the hill. */
typedef struct
{
    PortableMeshTriangle triangles[520];
    uint32_t count, invalid_triangle;
    int invalid_index;
} FloorWorld;
static int floor_triangle(const void *context, uint32_t index, PortableMeshTriangle *out)
{
    const FloorWorld *w = context;
    if (index >= w->count || index == w->invalid_triangle)
        return 0;
    *out = w->triangles[index];
    return 1;
}
static int floor_surface(const void *context, uint32_t id, PortableMeshSurface *out)
{
    (void)context;
    if (id > 1u)
        return 0;
    *out = (PortableMeshSurface){.type = id ? 3u : 0u};
    return 1;
}
static int floor_index(const void *context, const int32_t low[3], const int32_t high[3],
                       uint32_t first, uint32_t *out)
{
    const FloorWorld *w = context;
    if (w->invalid_index)
    {
        *out = w->count;
        return 1;
    }
    for (uint32_t i = first; i < w->count; ++i)
    {
        int overlaps = 1;
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            int32_t lo = INT32_MAX, hi = INT32_MIN;
            for (unsigned v = 0; v < 3u; ++v)
            {
                const int32_t n = w->triangles[i].vertices_q16[v][axis];
                if (n < lo)
                    lo = n;
                if (n > hi)
                    hi = n;
            }
            overlaps &= hi >= low[axis] && lo <= high[axis];
        }
        if (overlaps)
        {
            *out = i;
            return 1;
        }
    }
    return 0;
}
static void floor_quad(FloorWorld *w, int32_t x0, int32_t x1, int32_t y0, int32_t y1, int32_t z0,
                       int32_t z1)
{
    PortableMeshTriangle a = {.vertices_q16 = {{x0, y0, z0}, {x1, y0, z1}, {x1, y1, z1}},
                              .respawn = 77};
    PortableMeshTriangle b = {.vertices_q16 = {{x0, y0, z0}, {x1, y1, z1}, {x0, y1, z0}},
                              .respawn = 77};
    w->triangles[w->count++] = a;
    w->triangles[w->count++] = b;
}
static int floor_traces(void)
{
    FloorWorld w = {.invalid_triangle = UINT32_MAX};
    floor_quad(&w, -4 * Q, 0, -2 * Q, 2 * Q, 0, 0);
    floor_quad(&w, 0, 4 * Q, -2 * Q, 2 * Q, 0, 2 * Q);
    floor_quad(&w, 4 * Q, 8 * Q, -2 * Q, 2 * Q, 2 * Q, 2 * Q);
    floor_quad(&w, -4 * Q, 8 * Q, -2 * Q, 2 * Q, 8 * Q, 8 * Q);
    PortableMeshCourse course = {&w, w.count, 0, floor_triangle, floor_surface, NULL, NULL};
    const PortableMeshFloorWitness lower = {1, {-3 * Q, 0, 0}}, upper = {7, {-3 * Q, 0, 8 * Q}};
    const int32_t target[2] = {7 * Q, 0};
    PortableMeshFloorTraceHit hit = {0}, indexed = {0};
    CHECK(portable_mesh_query_floor_trace(&course, &lower, target, U / 4, &hit) ==
          PORTABLE_MESH_OK);
    CHECK(hit.witness.point_q16[0] == 7 * Q && hit.witness.point_q16[2] == 2 * Q &&
          hit.respawn == 77);
    course.next_triangle = floor_index;
    CHECK(portable_mesh_query_floor_trace(&course, &lower, target, U / 4, &indexed) ==
          PORTABLE_MESH_OK);
    CHECK(!memcmp(&hit, &indexed, sizeof hit));
    CHECK(portable_mesh_query_floor_trace(&course, &upper, target, U / 4, &indexed) ==
          PORTABLE_MESH_OK);
    CHECK(indexed.witness.point_q16[2] == 8 * Q);
    const int32_t slope[2] = {3 * Q, 0};
    CHECK(portable_mesh_query_floor_trace(&course, &lower, slope, U / 4, &hit) == PORTABLE_MESH_OK);
    CHECK(hit.witness.point_q16[2] >= 3 * Q / 2 && hit.witness.point_q16[2] <= 3 * Q / 2 + 1);
    const int32_t back[2] = {-3 * Q, 0};
    CHECK(portable_mesh_query_floor_trace(&course, &hit.witness, back, U / 4, &indexed) ==
          PORTABLE_MESH_OK);
    CHECK(indexed.witness.point_q16[2] == 0);
    CHECK(portable_mesh_query_floor_trace(&course, &lower, back, U / 4, &hit) == PORTABLE_MESH_OK);
    CHECK(hit.witness.triangle == lower.triangle && hit.witness.point_q16[2] == 0);
    /* A supported endpoint across a missing quad is still disconnected. */
    w.triangles[2].surface = w.triangles[3].surface = 1;
    const PortableMeshFloorTraceHit sentinel = hit;
    CHECK(portable_mesh_query_floor_trace(&course, &lower, target, U / 4, &hit) ==
          PORTABLE_MESH_OUTSIDE);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    w.triangles[2].surface = w.triangles[3].surface = 0;
    w.invalid_index = 1;
    CHECK(portable_mesh_query_floor_trace(&course, &lower, target, U / 4, &hit) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    w.invalid_index = 0;
    course.next_triangle = NULL;
    w.invalid_triangle = 7;
    CHECK(portable_mesh_query_floor_trace(&course, &lower, target, U / 4, &hit) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    w.invalid_triangle = UINT32_MAX;
    PortableMeshFloorWitness wrong = lower;
    w.triangles[7].vertices_q16[0][2] = PORTABLE_FIXED_3D_POSITION_LIMIT + 1;
    CHECK(portable_mesh_query_floor_trace(&course, &lower, target, U / 4, &hit) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    w.triangles[7].vertices_q16[0][2] = 8 * Q;
    wrong.point_q16[2] = Q;
    CHECK(portable_mesh_query_floor_trace(&course, &wrong, target, U / 4, &hit) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    return 0;
}
static int floor_boundaries(void)
{
    FloorWorld w = {.invalid_triangle = UINT32_MAX};
    floor_quad(&w, -4 * Q, 0, -2 * Q, 2 * Q, 0, 0);
    floor_quad(&w, 0, 4 * Q, -2 * Q, 0, 0, 0);
    floor_quad(&w, 0, 4 * Q, 0, 2 * Q, 0, 0);
    PortableMeshCourse course = {&w, w.count, 0, floor_triangle, floor_surface, NULL, NULL};
    const PortableMeshFloorWitness start = {1, {-3 * Q, 0, 0}};
    PortableMeshFloorTraceHit hit = {0};
    const int32_t targets[][2] = {{3 * Q, -Q}, {3 * Q, Q}, {3 * Q, 0}, {0, 0}};
    for (unsigned i = 0; i < 4u; ++i)
    {
        CHECK(portable_mesh_query_floor_trace(&course, &start, targets[i], U / 4, &hit) ==
              PORTABLE_MESH_OK);
        CHECK(hit.witness.point_q16[2] == 0);
    }
    /* Exact duplicates do not create a different floor. */
    w.triangles[w.count++] = w.triangles[1];
    course.triangle_count = w.count;
    CHECK(portable_mesh_query_floor_trace(&course, &start, targets[1], U / 4, &hit) ==
          PORTABLE_MESH_OK);
    const PortableMeshFloorTraceHit sentinel = hit;
    /* Coplanar overlapping interiors with a shared edge are explicitly refused,
     * rather than silently treating malformed triangulation as a new route. */
    w.count = 2;
    w.triangles[2] = (PortableMeshTriangle){
        .vertices_q16 = {{0, -2 * Q, 0}, {0, 2 * Q, 0}, {-2 * Q, 0, 0}}, .respawn = 77};
    w.count = 3;
    course.triangle_count = w.count;
    const int32_t left[2] = {-Q, 0};
    const PortableMeshFloorWitness ambiguous = {0, {0, 0, 0}};
    CHECK(portable_mesh_query_floor_trace(&course, &ambiguous, left, U / 4, &hit) ==
          PORTABLE_MESH_UNSUPPORTED);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    /* Islands meeting only at a vertex cannot exchange floor authority. */
    memset(&w, 0, sizeof w);
    w.invalid_triangle = UINT32_MAX;
    floor_quad(&w, -2 * Q, 0, -2 * Q, 0, 0, 0);
    floor_quad(&w, 0, 2 * Q, 0, 2 * Q, 0, 0);
    course.triangle_count = w.count;
    const PortableMeshFloorWitness island = {0, {-Q, -Q, 0}};
    const int32_t far[2] = {Q, Q};
    CHECK(portable_mesh_query_floor_trace(&course, &island, far, U / 4, &hit) ==
          PORTABLE_MESH_OUTSIDE);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    /* The same corner is traversable when a real edge-connected fan fills it. */
    floor_quad(&w, -2 * Q, 0, 0, 2 * Q, 0, 0);
    course.triangle_count = w.count;
    CHECK(portable_mesh_query_floor_trace(&course, &island, far, U / 4, &hit) == PORTABLE_MESH_OK);
    /* Even a one-LSB gap is not rounded into a connected seam. */
    w.count = 0;
    floor_quad(&w, -2 * Q, 0, -Q, Q, 0, 0);
    floor_quad(&w, 1, 2 * Q, -Q, Q, 0, 0);
    course.triangle_count = w.count;
    const PortableMeshFloorWitness gap = {1, {-Q, 0, 0}};
    const int32_t across[2] = {Q, 0};
    const PortableMeshFloorTraceHit before_gap = hit;
    CHECK(portable_mesh_query_floor_trace(&course, &gap, across, U / 4, &hit) ==
          PORTABLE_MESH_OUTSIDE);
    CHECK(!memcmp(&hit, &before_gap, sizeof hit));
    return 0;
}
static int floor_limits(void)
{
    FloorWorld w = {.invalid_triangle = UINT32_MAX};
    const int32_t bound = PORTABLE_FIXED_3D_POSITION_LIMIT;
    floor_quad(&w, -bound, bound, -bound, bound, -Q, -Q);
    PortableMeshCourse course = {&w, w.count, 0, floor_triangle, floor_surface, floor_index, NULL};
    const PortableMeshFloorWitness start = {1, {-bound + 1, 0, -Q}};
    const int32_t target[2] = {bound - 1, 0};
    PortableMeshFloorTraceHit hit = {0};
    CHECK(portable_mesh_query_floor_trace(&course, &start, target, U / 4, &hit) ==
          PORTABLE_MESH_OK);
    CHECK(hit.witness.point_q16[2] == -Q);
    const PortableMeshFloorTraceHit sentinel = hit;
    for (unsigned i = 2; i < 513u; ++i)
        w.triangles[i] = w.triangles[0];
    w.count = 513;
    course.triangle_count = w.count;
    CHECK(portable_mesh_query_floor_trace(&course, &start, target, U / 4, &hit) ==
          PORTABLE_MESH_COLLISION_BUDGET);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    CHECK(portable_mesh_query_floor_trace(&course, &start, target, 0, &hit) ==
          PORTABLE_MESH_INVALID);
    CHECK(!memcmp(&hit, &sentinel, sizeof hit));
    return 0;
}
int main(void)
{
    CHECK(face_edge_vertex() == 0);
    CHECK(vertical_and_bounds() == 0);
    CHECK(degenerates() == 0);
    CHECK(segment_spheres() == 0);
    CHECK(sphere_support() == 0);
    CHECK(face_sweeps() == 0);
    CHECK(endpoint_overlaps() == 0);
    CHECK(indexed_queries() == 0);
    CHECK(floor_traces() == 0);
    CHECK(floor_boundaries() == 0);
    CHECK(floor_limits() == 0);
    puts("Mesh query primitives: PASS");
    return 0;
}
