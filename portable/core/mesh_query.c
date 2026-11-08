#include "mesh_query.h"
#include "fixed_3d.h"
#include <string.h>
#define Q PORTABLE_FIXED_3D_ONE
#define U PORTABLE_FIXED_3D_UNIT
int portable_mesh_query_bounds(const int32_t before[3], const int32_t after[3], int64_t extent,
                               int32_t low[3], int32_t high[3])
{
    if (!before || !after || !low || !high || extent < 0 || extent > INT32_MAX)
        return 0;
    int32_t lower[3], upper[3];
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        const int64_t a = (before[axis] < after[axis] ? before[axis] : after[axis]) - extent;
        const int64_t b = (before[axis] > after[axis] ? before[axis] : after[axis]) + extent;
        lower[axis] = a < INT32_MIN ? INT32_MIN : (int32_t)a;
        upper[axis] = b > INT32_MAX ? INT32_MAX : (int32_t)b;
    }
    memcpy(low, lower, sizeof lower);
    memcpy(high, upper, sizeof upper);
    return 1;
}

PortableMeshQueryStatus portable_mesh_query_next_triangle(const PortableMeshCourse *course,
                                                          const int32_t low[3],
                                                          const int32_t high[3], uint32_t first,
                                                          uint32_t *out)
{
    if (!course || !low || !high || !out || course->triangle_count > 262144u)
        return PORTABLE_MESH_QUERY_INVALID;
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (low[axis] > high[axis])
            return PORTABLE_MESH_QUERY_INVALID;
    if (first >= course->triangle_count)
        return PORTABLE_MESH_QUERY_MISS;
    uint32_t candidate = first;
    const int result = course->next_triangle
                           ? course->next_triangle(course->context, low, high, first, &candidate)
                           : 1;
    if (!result)
        return PORTABLE_MESH_QUERY_MISS;
    if (result != 1 || candidate < first || candidate >= course->triangle_count)
        return PORTABLE_MESH_QUERY_INVALID;
    *out = candidate;
    return PORTABLE_MESH_QUERY_HIT;
}

static int32_t unit_product(int32_t a, int32_t b)
{
    return (int32_t)((int64_t)a * b / U);
}
static int32_t fraction(int64_t numerator, int64_t denominator)
{
    if (numerator <= 0)
        return 0;
    if (numerator >= denominator)
        return Q;
    uint64_t n = (uint64_t)numerator, d = (uint64_t)denominator;
    uint32_t result = 0;
    for (unsigned i = 0; i < 16u; ++i)
    {
        n *= 2u;
        result *= 2u;
        if (n >= d)
        {
            n -= d;
            ++result;
        }
    }
    return (int32_t)result;
}
int portable_mesh_query_normal(const PortableMeshTriangle *t, int32_t n[3])
{
    int64_t a[3], b[3], cross[3];
    for (unsigned i = 0; i < 3u; ++i)
    {
        a[i] = (int64_t)t->vertices_q16[1][i] - t->vertices_q16[0][i];
        b[i] = (int64_t)t->vertices_q16[2][i] - t->vertices_q16[0][i];
    }
    cross[0] = a[1] * b[2] - a[2] * b[1];
    cross[1] = a[2] * b[0] - a[0] * b[2];
    cross[2] = a[0] * b[1] - a[1] * b[0];
    return portable_fixed_3d_normalize(cross, n);
}
int portable_mesh_query_inside(const PortableMeshTriangle *t, const int32_t p[3],
                               const int32_t n[3])
{
    unsigned drop = 0;
    for (unsigned i = 1; i < 3u; ++i)
        if ((int64_t)n[i] * n[i] > (int64_t)n[drop] * n[drop])
            drop = i;
    const unsigned x = (drop + 1u) % 3u, y = (drop + 2u) % 3u;
    int positive = 0, negative = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int32_t *a = t->vertices_q16[i], *b = t->vertices_q16[(i + 1u) % 3u];
        const int64_t cross = ((int64_t)b[x] - a[x]) * ((int64_t)p[y] - a[y]) -
                              ((int64_t)b[y] - a[y]) * ((int64_t)p[x] - a[x]);
        positive |= cross > 0;
        negative |= cross < 0;
    }
    return !(positive && negative);
}
static uint64_t distance_squared(const int32_t a[3], const int32_t b[3])
{
    uint64_t sum = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int64_t d = (int64_t)a[i] - b[i];
        sum += (uint64_t)(d * d);
    }
    return sum;
}
void portable_mesh_query_closest(const PortableMeshTriangle *t, const int32_t p[3],
                                 const int32_t n[3], int32_t out[3])
{
    int32_t delta[3];
    for (unsigned i = 0; i < 3u; ++i)
        delta[i] = p[i] - t->vertices_q16[0][i];
    const int32_t plane = (int32_t)(portable_fixed_3d_dot(delta, n) / U);
    for (unsigned i = 0; i < 3u; ++i)
        out[i] = p[i] - unit_product(plane, n[i]);
    if (portable_mesh_query_inside(t, out, n))
        return;
    uint64_t best = UINT64_MAX;
    for (unsigned edge = 0; edge < 3u; ++edge)
    {
        const int32_t *a = t->vertices_q16[edge], *b = t->vertices_q16[(edge + 1u) % 3u];
        int32_t d[3], rel[3], point[3];
        for (unsigned i = 0; i < 3u; ++i)
        {
            d[i] = b[i] - a[i];
            rel[i] = p[i] - a[i];
        }
        const int64_t den = portable_fixed_3d_dot(d, d);
        const int32_t f = den ? fraction(portable_fixed_3d_dot(rel, d), den) : 0;
        for (unsigned i = 0; i < 3u; ++i)
            point[i] = a[i] + portable_fixed_3d_mul(d[i], f);
        const uint64_t square = distance_squared(point, p);
        if (square < best)
        {
            best = square;
            memcpy(out, point, sizeof(point));
        }
    }
}

/* Four base 2^32 limbs cover the largest comparison below (<2^126). Only
 * unsigned 64-bit products/carries are used; this is identical on 32/64-bit hosts.
 * A limb product plus an existing limb and carry fits exactly in uint64. */
typedef struct
{
    uint32_t limb[4];
} QueryWide;
static QueryWide query_product(uint64_t a, uint64_t b)
{
    const uint32_t left[2] = {(uint32_t)a, (uint32_t)(a >> 32u)};
    const uint32_t right[2] = {(uint32_t)b, (uint32_t)(b >> 32u)};
    QueryWide result = {{0}};
    for (unsigned i = 0; i < 2u; ++i)
    {
        uint64_t carry = 0;
        for (unsigned j = 0; j < 2u; ++j)
        {
            const uint64_t value = (uint64_t)left[i] * right[j] + result.limb[i + j] + carry;
            result.limb[i + j] = (uint32_t)value;
            carry = value >> 32u;
        }
        result.limb[i + 2u] = (uint32_t)carry;
    }
    return result;
}
static QueryWide query_add(QueryWide a, QueryWide b)
{
    QueryWide result;
    uint64_t carry = 0;
    for (unsigned i = 0; i < 4u; ++i)
    {
        const uint64_t value = (uint64_t)a.limb[i] + b.limb[i] + carry;
        result.limb[i] = (uint32_t)value;
        carry = value >> 32u;
    }
    return result;
}
static int query_compare(QueryWide a, QueryWide b)
{
    for (unsigned i = 4u; i > 0; --i)
        if (a.limb[i - 1u] != b.limb[i - 1u])
            return a.limb[i - 1u] < b.limb[i - 1u] ? -1 : 1;
    return 0;
}
static uint64_t query_magnitude(int64_t value)
{
    return value < 0 ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
}
static int query_position(const int32_t point[3], int32_t limit)
{
    if (!point)
        return 0;
    for (unsigned axis = 0; axis < 3u; ++axis)
        if (point[axis] < -limit || point[axis] > limit)
            return 0;
    return 1;
}
static int sphere_entry_reached(const int64_t relative[3], const int64_t direction[3],
                                uint64_t length_squared, uint64_t towards, uint64_t radius_squared,
                                uint64_t fraction_q32)
{
    const uint64_t one = PORTABLE_MESH_QUERY_FRACTION_ONE;
    /* The first entry lies before the closest point. Past that point the
     * predicate stays true, including a tangent with no inside Q32 sample. */
    if (query_compare(query_product(fraction_q32, length_squared), query_product(towards, one)) >=
        0)
        return 1;
    QueryWide distance = {{0}};
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        const int64_t residual =
            relative[axis] * (int64_t)one + direction[axis] * (int64_t)fraction_q32;
        const uint64_t magnitude = query_magnitude(residual);
        distance = query_add(distance, query_product(magnitude, magnitude));
    }
    const QueryWide radius = {{0, 0, (uint32_t)radius_squared, (uint32_t)(radius_squared >> 32u)}};
    return query_compare(distance, radius) <= 0;
}
PortableMeshQueryStatus portable_mesh_query_segment_sphere(const int32_t before[3],
                                                           const int32_t after[3],
                                                           const int32_t center[3], int32_t radius,
                                                           uint64_t *out)
{
    const int32_t limit = 2 * PORTABLE_FIXED_3D_POSITION_LIMIT;
    if (!out || radius <= 0 || radius > limit || !query_position(before, limit) ||
        !query_position(after, limit) || !query_position(center, limit))
        return PORTABLE_MESH_QUERY_INVALID;
    int64_t relative[3], direction[3];
    uint64_t start_squared = 0, length_squared = 0, end_squared = 0;
    int64_t projection = 0;
    for (unsigned axis = 0; axis < 3u; ++axis)
    {
        relative[axis] = (int64_t)before[axis] - center[axis];
        direction[axis] = (int64_t)after[axis] - before[axis];
        const int64_t end = (int64_t)after[axis] - center[axis];
        start_squared += (uint64_t)(relative[axis] * relative[axis]);
        length_squared += (uint64_t)(direction[axis] * direction[axis]);
        end_squared += (uint64_t)(end * end);
        projection += relative[axis] * direction[axis];
    }
    /* Differences are at most 16384m Q16 (2^30): each sum/dot fits signed 64 bits.
     * Scaled interpolation residuals fit signed 63 bits; their squared sum fits 128 bits. */
    const uint64_t radius_squared = (uint64_t)((int64_t)radius * radius);
    if (start_squared <= radius_squared)
    {
        *out = 0;
        return PORTABLE_MESH_QUERY_HIT;
    }
    if (!length_squared || projection >= 0)
        return PORTABLE_MESH_QUERY_MISS;
    const uint64_t towards = query_magnitude(projection);
    if (towards >= length_squared)
    {
        if (end_squared > radius_squared)
            return PORTABLE_MESH_QUERY_MISS;
    }
    else if (query_compare(query_product(towards, towards),
                           query_product(length_squared, start_squared - radius_squared)) < 0)
        return PORTABLE_MESH_QUERY_MISS;
    /* Exact discriminant sign above proves a continuous hit. Search the
     * monotone entry predicate instead of taking a lossy integer square root. */
    uint64_t low = 0, high = PORTABLE_MESH_QUERY_FRACTION_ONE;
    while (low + 1u < high)
    {
        const uint64_t middle = low + (high - low) / 2u;
        if (sphere_entry_reached(relative, direction, length_squared, towards, radius_squared,
                                 middle))
            high = middle;
        else
            low = middle;
    }
    *out = high;
    return PORTABLE_MESH_QUERY_HIT;
}
PortableMeshQueryStatus portable_mesh_query_support(const PortableMeshCourse *course,
                                                    const int32_t center[3], int32_t radius,
                                                    int32_t maximum_drop, int32_t cutoff,
                                                    PortableMeshSupportHit *out)
{
    const int32_t limit = PORTABLE_FIXED_3D_POSITION_LIMIT;
    if (!course || !out || !course->triangle || !course->surface ||
        course->triangle_count > 262144u || !query_position(center, limit) || radius <= 0 ||
        radius > limit || maximum_drop < 0 || maximum_drop > 2 * limit || cutoff <= 0 || cutoff > U)
        return PORTABLE_MESH_QUERY_INVALID;
    PortableMeshSupportHit selected = {0};
    uint64_t selected_numerator = 0, selected_denominator = 1;
    int found = 0;
    int32_t low[3], high[3], below[3] = {center[0], center[1], 0};
    const int64_t lowest = (int64_t)center[2] - maximum_drop;
    below[2] = lowest < INT32_MIN ? INT32_MIN : (int32_t)lowest;
    if (!portable_mesh_query_bounds(center, below, (int64_t)radius + 2, low, high))
        return PORTABLE_MESH_QUERY_INVALID;
    uint32_t first = 0, index;
    for (;;)
    {
        const PortableMeshQueryStatus next =
            portable_mesh_query_next_triangle(course, low, high, first, &index);
        if (next == PORTABLE_MESH_QUERY_INVALID)
            return next;
        if (next == PORTABLE_MESH_QUERY_MISS)
            break;
        first = index + 1u;
        PortableMeshTriangle triangle;
        PortableMeshSurface surface;
        int32_t n[3];
        if (!course->triangle(course->context, index, &triangle) ||
            !course->surface(course->context, triangle.surface, &surface) || surface.type > 9u)
            return PORTABLE_MESH_QUERY_INVALID;
        for (unsigned v = 0; v < 3u; ++v)
            if (!query_position(triangle.vertices_q16[v], limit))
                return PORTABLE_MESH_QUERY_INVALID;
        if (!portable_mesh_query_normal(&triangle, n))
            return PORTABLE_MESH_QUERY_INVALID;
        if (surface.type == 3u || n[2] < cutoff)
            continue;
        int32_t relative[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
            relative[axis] = center[axis] - triangle.vertices_q16[0][axis];
        const int64_t plane = portable_fixed_3d_dot(relative, n);
        const int64_t numerator = plane - (int64_t)radius * U;
        /* A support plane must require a DOWNWARD correction of the center.
         * Never select a road overhead or pull an intersecting sphere upward. */
        if (numerator < 0 || numerator > (int64_t)maximum_drop * n[2])
            continue;
        if (found && query_compare(query_product((uint64_t)numerator, selected_denominator),
                                   query_product(selected_numerator, (uint64_t)n[2])) >= 0)
            continue;
        const int32_t drop = (int32_t)(numerator / n[2]);
        int32_t snapped[3] = {center[0], center[1], center[2] - drop}, point[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
            relative[axis] = snapped[axis] - triangle.vertices_q16[0][axis];
        const int32_t distance = (int32_t)(portable_fixed_3d_dot(relative, n) / U);
        for (unsigned axis = 0; axis < 3u; ++axis)
            point[axis] = snapped[axis] - unit_product(distance, n[axis]);
        if (!portable_mesh_query_inside(&triangle, point, n))
            continue;
        selected = (PortableMeshSupportHit){.triangle = index,
                                            .surface = triangle.surface,
                                            .type = surface.type,
                                            .respawn = triangle.respawn,
                                            .drop_q16 = drop};
        memcpy(selected.normal_q30, n, sizeof n);
        memcpy(selected.center_q16, snapped, sizeof snapped);
        selected_numerator = (uint64_t)numerator;
        selected_denominator = (uint64_t)n[2];
        found = 1;
    }
    if (!found)
        return PORTABLE_MESH_QUERY_MISS;
    *out = selected;
    return PORTABLE_MESH_QUERY_HIT;
}

static int query_course(const PortableMeshCourse *course)
{
    return course && course->triangle && course->surface && course->triangle_count <= 262144u;
}
static int query_triangle(const PortableMeshCourse *course, uint32_t index,
                          PortableMeshTriangle *triangle, PortableMeshSurface *surface,
                          int32_t normal[3])
{
    if (index >= course->triangle_count || !course->triangle(course->context, index, triangle) ||
        !course->surface(course->context, triangle->surface, surface) || surface->type > 9u)
        return 0;
    for (unsigned vertex = 0; vertex < 3u; ++vertex)
        if (!query_position(triangle->vertices_q16[vertex], PORTABLE_FIXED_3D_POSITION_LIMIT))
            return 0;
    return portable_mesh_query_normal(triangle, normal);
}
static uint64_t query_entry_fraction(uint64_t numerator, uint64_t denominator)
{
    /* Plane-distance differences are below 2^62 in this domain, so doubling each
     * remainder is safe. Division builds Q32 without numerator << 32. */
    uint64_t result = 0;
    for (unsigned bit = 0; bit < 32u; ++bit)
    {
        numerator *= 2u;
        result *= 2u;
        if (numerator >= denominator)
        {
            numerator -= denominator;
            ++result;
        }
    }
    return result + (numerator != 0);
}
PortableMeshQueryStatus portable_mesh_query_sweep_faces(const PortableMeshCourse *course,
                                                        const int32_t before[3],
                                                        const int32_t after[3], int32_t radius,
                                                        PortableMeshFaceHit *out)
{
    const int32_t limit = PORTABLE_FIXED_3D_POSITION_LIMIT;
    if (!out || !query_course(course) || !query_position(before, limit) ||
        !query_position(after, limit) || radius <= 0 || radius > limit)
        return PORTABLE_MESH_QUERY_INVALID;
    PortableMeshFaceHit selected = {0};
    uint64_t selected_numerator = 0, selected_denominator = 1;
    int found = 0;
    int32_t low[3], high[3];
    if (!portable_mesh_query_bounds(before, after, (int64_t)radius + 2, low, high))
        return PORTABLE_MESH_QUERY_INVALID;
    uint32_t first = 0, index;
    for (;;)
    {
        const PortableMeshQueryStatus next =
            portable_mesh_query_next_triangle(course, low, high, first, &index);
        if (next == PORTABLE_MESH_QUERY_INVALID)
            return next;
        if (next == PORTABLE_MESH_QUERY_MISS)
            break;
        first = index + 1u;
        PortableMeshTriangle triangle;
        PortableMeshSurface surface;
        int32_t normal[3], relative[3];
        if (!query_triangle(course, index, &triangle, &surface, normal))
            return PORTABLE_MESH_QUERY_INVALID;
        for (unsigned axis = 0; axis < 3u; ++axis)
            relative[axis] = before[axis] - triangle.vertices_q16[0][axis];
        int64_t start = portable_fixed_3d_dot(relative, normal);
        for (unsigned axis = 0; axis < 3u; ++axis)
            relative[axis] = after[axis] - triangle.vertices_q16[0][axis];
        int64_t end = portable_fixed_3d_dot(relative, normal);
        if (start < 0)
        {
            start = -start;
            end = -end;
            for (unsigned axis = 0; axis < 3u; ++axis)
                normal[axis] = -normal[axis];
        }
        const int64_t clearance = (int64_t)radius * U;
        if (start < clearance || end > clearance || start <= end)
            continue;
        const uint64_t numerator = (uint64_t)(start - clearance);
        const uint64_t denominator = (uint64_t)(start - end);
        if (found && query_compare(query_product(numerator, selected_denominator),
                                   query_product(selected_numerator, denominator)) >= 0)
            continue;
        const uint64_t entry = query_entry_fraction(numerator, denominator);
        PortableMeshFaceHit candidate = {.fraction_q32 = entry,
                                         .triangle = index,
                                         .surface = triangle.surface,
                                         .type = surface.type,
                                         .respawn = triangle.respawn};
        for (unsigned axis = 0; axis < 3u; ++axis)
        {
            const int64_t movement = (int64_t)after[axis] - before[axis];
            candidate.center_q16[axis] =
                before[axis] +
                (int32_t)(movement * (int64_t)entry / (int64_t)PORTABLE_MESH_QUERY_FRACTION_ONE);
            relative[axis] = candidate.center_q16[axis] - triangle.vertices_q16[0][axis];
        }
        /* Project the rounded center back to the plane, rather than assuming
         * rounded radius * normal has landed exactly on it. */
        const int32_t distance = (int32_t)(portable_fixed_3d_dot(relative, normal) / U);
        for (unsigned axis = 0; axis < 3u; ++axis)
            candidate.point_q16[axis] =
                candidate.center_q16[axis] - unit_product(distance, normal[axis]);
        if (!portable_mesh_query_inside(&triangle, candidate.point_q16, normal))
            continue;
        memcpy(candidate.normal_q30, normal, sizeof normal);
        selected = candidate;
        selected_numerator = numerator;
        selected_denominator = denominator;
        found = 1;
    }
    if (!found)
        return PORTABLE_MESH_QUERY_MISS;
    *out = selected;
    return PORTABLE_MESH_QUERY_HIT;
}
PortableMeshQueryStatus portable_mesh_query_overlap(const PortableMeshCourse *course,
                                                    uint32_t index, const int32_t center[3],
                                                    int32_t radius, PortableMeshOverlapHit *out)
{
    const int32_t limit = PORTABLE_FIXED_3D_POSITION_LIMIT;
    if (!out || !query_course(course) || !query_position(center, limit) || radius <= 0 ||
        radius > limit)
        return PORTABLE_MESH_QUERY_INVALID;
    PortableMeshTriangle triangle;
    PortableMeshSurface surface;
    int32_t normal[3], relative[3], projected[3];
    if (!query_triangle(course, index, &triangle, &surface, normal))
        return PORTABLE_MESH_QUERY_INVALID;
    PortableMeshOverlapHit candidate = {.triangle = index,
                                        .surface = triangle.surface,
                                        .type = surface.type,
                                        .respawn = triangle.respawn};
    for (unsigned axis = 0; axis < 3u; ++axis)
        relative[axis] = center[axis] - triangle.vertices_q16[0][axis];
    const int32_t distance = (int32_t)(portable_fixed_3d_dot(relative, normal) / U);
    for (unsigned axis = 0; axis < 3u; ++axis)
        projected[axis] = center[axis] - unit_product(distance, normal[axis]);
    candidate.face_interior = (uint8_t)portable_mesh_query_inside(&triangle, projected, normal);
    portable_mesh_query_closest(&triangle, center, normal, candidate.point_q16);
    const uint64_t squared = distance_squared(center, candidate.point_q16);
    if (squared > (uint64_t)((int64_t)radius * radius))
        return PORTABLE_MESH_QUERY_MISS;
    const int32_t length = (int32_t)portable_fixed_3d_sqrt(squared);
    if (length)
    {
        int64_t direction[3];
        for (unsigned axis = 0; axis < 3u; ++axis)
            direction[axis] = (int64_t)center[axis] - candidate.point_q16[axis];
        if (!portable_fixed_3d_normalize(direction, normal))
            return PORTABLE_MESH_QUERY_INVALID;
    }
    memcpy(candidate.normal_q30, normal, sizeof normal);
    candidate.penetration_q16 = radius - length;
    *out = candidate;
    return PORTABLE_MESH_QUERY_HIT;
}

/* Fractions remain exact until final height publication. XY orientations fit
 * signed64 in the +/-4096m domain; comparing two fractions needs128 bits. */
typedef struct
{
    uint64_t n, d;
} FloorFraction;
typedef struct
{
    PortableMeshTriangle triangle;
    uint32_t id;
    int32_t normal[3];
    FloorFraction low, high, reached;
    uint8_t known, settled;
} FloorInterval;
#define FLOOR_INTERVAL_CAP 512u
static int floor_compare(FloorFraction a, FloorFraction b)
{
    return query_compare(query_product(a.n, b.d), query_product(b.n, a.d));
}
static FloorFraction floor_max(FloorFraction a, FloorFraction b)
{
    return floor_compare(a, b) < 0 ? b : a;
}
static FloorFraction floor_min(FloorFraction a, FloorFraction b)
{
    return floor_compare(a, b) < 0 ? a : b;
}
static int64_t floor_cross(int64_t ax, int64_t ay, int64_t bx, int64_t by)
{
    return ax * by - ay * bx;
}
static int floor_clip(const PortableMeshTriangle *t, const int32_t start[3],
                      const int32_t target[2], FloorFraction *low, FloorFraction *high)
{
    *low = (FloorFraction){0, 1};
    *high = (FloorFraction){1, 1};
    for (unsigned i = 0; i < 3u; ++i)
    {
        const int32_t *a = t->vertices_q16[i], *b = t->vertices_q16[(i + 1u) % 3u];
        const int64_t dx = (int64_t)b[0] - a[0], dy = (int64_t)b[1] - a[1];
        const int64_t p = floor_cross(dx, dy, (int64_t)start[0] - a[0], (int64_t)start[1] - a[1]);
        const int64_t q = floor_cross(dx, dy, (int64_t)target[0] - a[0], (int64_t)target[1] - a[1]);
        if (p < 0 && q < 0)
            return 0;
        if (p < 0)
            *low = floor_max(*low, (FloorFraction){(uint64_t)-p, (uint64_t)(q - p)});
        else if (q < 0)
            *high = floor_min(*high, (FloorFraction){(uint64_t)p, (uint64_t)(p - q)});
    }
    return floor_compare(*low, *high) <= 0;
}
/* Return the exact common positive-length subedge using authored endpoints.
 * No rounded height/proximity test may establish connectivity between decks. */
static int floor_subedge(const int32_t a[3], const int32_t b[3], const int32_t c[3],
                         const int32_t d[3], int32_t low[3], int32_t high[3])
{
    int64_t v[3], w[3], z[3];
    unsigned axis = 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        v[i] = (int64_t)b[i] - a[i];
        w[i] = (int64_t)c[i] - a[i];
        z[i] = (int64_t)d[i] - a[i];
        if (query_magnitude(v[i]) > query_magnitude(v[axis]))
            axis = i;
    }
    if (!v[axis])
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        for (unsigned j = i + 1u; j < 3u; ++j)
            if (v[i] * w[j] != v[j] * w[i] || v[i] * z[j] != v[j] * z[i])
                return 0;
    const int32_t *al = a[axis] < b[axis] ? a : b, *ah = a[axis] < b[axis] ? b : a;
    const int32_t *cl = c[axis] < d[axis] ? c : d, *ch = c[axis] < d[axis] ? d : c;
    const int32_t *l = al[axis] > cl[axis] ? al : cl, *h = ah[axis] < ch[axis] ? ah : ch;
    if (l[axis] >= h[axis])
        return 0;
    memcpy(low, l, 3 * sizeof *low);
    memcpy(high, h, 3 * sizeof *high);
    return 1;
}
static int floor_edge_interval(const int32_t start[3], const int32_t target[2], const int32_t a[3],
                               const int32_t b[3], FloorFraction *low, FloorFraction *high)
{
    const int64_t rx = (int64_t)target[0] - start[0], ry = (int64_t)target[1] - start[1];
    const int64_t sx = (int64_t)b[0] - a[0], sy = (int64_t)b[1] - a[1];
    const int64_t ax = (int64_t)a[0] - start[0], ay = (int64_t)a[1] - start[1];
    int64_t den = floor_cross(rx, ry, sx, sy);
    if (den)
    {
        int64_t n = floor_cross(ax, ay, sx, sy), u = floor_cross(ax, ay, rx, ry);
        if (den < 0)
        {
            den = -den;
            n = -n;
            u = -u;
        }
        if (n < 0 || n > den || u < 0 || u > den)
            return 0;
        *low = *high = (FloorFraction){(uint64_t)n, (uint64_t)den};
        return 1;
    }
    if (floor_cross(ax, ay, rx, ry))
        return 0;
    const unsigned axis = query_magnitude(rx) >= query_magnitude(ry) ? 0u : 1u;
    int64_t direction = axis ? ry : rx;
    if (!direction)
        return 0;
    int64_t p = (int64_t)a[axis] - start[axis], q = (int64_t)b[axis] - start[axis];
    if (direction < 0)
    {
        direction = -direction;
        p = -p;
        q = -q;
    }
    if (p > q)
    {
        int64_t swap = p;
        p = q;
        q = swap;
    }
    if (q < 0 || p > direction)
        return 0;
    *low = (FloorFraction){(uint64_t)(p < 0 ? 0 : p), (uint64_t)direction};
    *high = (FloorFraction){(uint64_t)(q > direction ? direction : q), (uint64_t)direction};
    return 1;
}
static int floor_connection(const FloorInterval *a, const FloorInterval *b, const int32_t start[3],
                            const int32_t target[2], FloorFraction minimum, FloorFraction maximum,
                            FloorFraction *entry, int *covers)
{
    int found = 0;
    *covers = 0;
    for (unsigned i = 0; i < 3u; ++i)
        for (unsigned j = 0; j < 3u; ++j)
        {
            int32_t l[3], h[3];
            FloorFraction lo, hi;
            if (!floor_subedge(a->triangle.vertices_q16[i], a->triangle.vertices_q16[(i + 1u) % 3u],
                               b->triangle.vertices_q16[j], b->triangle.vertices_q16[(j + 1u) % 3u],
                               l, h) ||
                !floor_edge_interval(start, target, l, h, &lo, &hi))
                continue;
            if (floor_compare(lo, minimum) <= 0 && floor_compare(hi, maximum) >= 0)
                *covers = 1;
            lo = floor_max(lo, minimum);
            hi = floor_min(hi, maximum);
            if (floor_compare(lo, hi) > 0)
                continue;
            if (!found || floor_compare(lo, *entry) < 0)
                *entry = lo;
            found = 1;
        }
    return found;
}
static int floor_duplicate(const PortableMeshTriangle *a, const PortableMeshTriangle *b)
{
    if (a->surface != b->surface || a->respawn != b->respawn)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
    {
        int found = 0;
        for (unsigned j = 0; j < 3u; ++j)
            found |= !memcmp(a->vertices_q16[i], b->vertices_q16[j], 3 * sizeof(int32_t));
        if (!found)
            return 0;
    }
    return 1;
}
static int floor_height(const FloorInterval *row, const int32_t xy[2], int32_t *out)
{
    const int32_t *p = row->triangle.vertices_q16[0];
    const int64_t numerator =
        -((int64_t)xy[0] - p[0]) * row->normal[0] - ((int64_t)xy[1] - p[1]) * row->normal[1];
    /* Round upward, matching support's on/above plane convention. */
    const int64_t divisor = row->normal[2];
    const int64_t rise = numerator / divisor + (numerator > 0 && numerator % divisor != 0);
    const int64_t height = (int64_t)p[2] + rise;
    if (height < -PORTABLE_FIXED_3D_POSITION_LIMIT || height > PORTABLE_FIXED_3D_POSITION_LIMIT)
        return 0;
    *out = (int32_t)height;
    return 1;
}
PortableMeshStatus portable_mesh_query_floor_trace(const PortableMeshCourse *course,
                                                   const PortableMeshFloorWitness *start,
                                                   const int32_t target[2], int32_t cutoff,
                                                   PortableMeshFloorTraceHit *out)
{
    const int32_t limit = PORTABLE_FIXED_3D_POSITION_LIMIT;
    if (!query_course(course) || !start || !out || !target ||
        !query_position(start->point_q16, limit) || target[0] < -limit || target[0] > limit ||
        target[1] < -limit || target[1] > limit || cutoff <= 0 || cutoff > U ||
        start->triangle >= course->triangle_count)
        return PORTABLE_MESH_INVALID;
    FloorInterval rows[FLOOR_INTERVAL_CAP];
    unsigned count = 0, start_row = FLOOR_INTERVAL_CAP;
    int32_t low[3] = {start->point_q16[0] < target[0] ? start->point_q16[0] : target[0],
                      start->point_q16[1] < target[1] ? start->point_q16[1] : target[1], -limit};
    int32_t high[3] = {start->point_q16[0] > target[0] ? start->point_q16[0] : target[0],
                       start->point_q16[1] > target[1] ? start->point_q16[1] : target[1], limit};
    uint32_t first = 0, id;
    for (;;)
    {
        const PortableMeshQueryStatus next =
            portable_mesh_query_next_triangle(course, low, high, first, &id);
        if (next == PORTABLE_MESH_QUERY_INVALID)
            return PORTABLE_MESH_INVALID;
        if (next == PORTABLE_MESH_QUERY_MISS)
            break;
        first = id + 1u;
        FloorInterval row = {.id = id};
        PortableMeshSurface surface;
        if (!query_triangle(course, id, &row.triangle, &surface, row.normal))
            return PORTABLE_MESH_INVALID;
        if (surface.type == 3u || row.normal[2] < cutoff ||
            !floor_clip(&row.triangle, start->point_q16, target, &row.low, &row.high))
            continue;
        if (count == FLOOR_INTERVAL_CAP)
            return PORTABLE_MESH_COLLISION_BUDGET;
        if (id == start->triangle)
            start_row = count;
        rows[count++] = row;
    }
    if (start_row == FLOOR_INTERVAL_CAP || rows[start_row].low.n)
        return PORTABLE_MESH_INVALID;
    int32_t initial_height;
    if (!floor_height(&rows[start_row], start->point_q16, &initial_height) ||
        start->point_q16[2] != initial_height)
        return PORTABLE_MESH_INVALID;
    rows[start_row].known = 1;
    rows[start_row].reached = (FloorFraction){0, 1};
    const int stationary = start->point_q16[0] == target[0] && start->point_q16[1] == target[1];
    unsigned selected = start_row;
    if (!stationary)
    {
        for (unsigned step = 0; step < count; ++step)
        {
            unsigned current = count;
            for (unsigned i = 0; i < count; ++i)
                if (rows[i].known && !rows[i].settled &&
                    (current == count || floor_compare(rows[i].reached, rows[current].reached) < 0))
                    current = i;
            if (current == count)
                break;
            rows[current].settled = 1;
            for (unsigned i = 0; i < count; ++i)
            {
                if (i == current)
                    continue;
                const FloorFraction lo = floor_max(rows[current].reached, rows[i].low);
                const FloorFraction hi = floor_min(rows[current].high, rows[i].high);
                if (floor_compare(lo, hi) > 0)
                    continue;
                FloorFraction entry = {0, 1};
                int covers;
                if (!floor_connection(&rows[current], &rows[i], start->point_q16, target, lo, hi,
                                      &entry, &covers))
                    continue;
                if (!rows[i].known || floor_compare(entry, rows[i].reached) < 0)
                {
                    rows[i].known = 1;
                    rows[i].reached = entry;
                }
            }
        }
        selected = count;
        for (unsigned i = 0; i < count; ++i)
            if (rows[i].known)
            {
                for (unsigned j = i + 1u; j < count; ++j)
                    if (rows[j].known)
                    {
                        const FloorFraction lo = floor_max(rows[i].reached, rows[j].reached);
                        const FloorFraction hi = floor_min(rows[i].high, rows[j].high);
                        if (floor_compare(lo, hi) < 0 &&
                            !floor_duplicate(&rows[i].triangle, &rows[j].triangle))
                        {
                            FloorFraction entry = {0, 1};
                            int covers;
                            floor_connection(&rows[i], &rows[j], start->point_q16, target, lo, hi,
                                             &entry, &covers);
                            if (!covers)
                                return PORTABLE_MESH_UNSUPPORTED;
                        }
                    }
                if (rows[i].high.n == rows[i].high.d &&
                    (selected == count || rows[i].id < rows[selected].id))
                    selected = i;
            }
        if (selected == count)
            return PORTABLE_MESH_OUTSIDE;
    }
    const FloorInterval *row = &rows[selected];
    PortableMeshFloorTraceHit hit = {
        .witness = {.triangle = row->id, .point_q16 = {target[0], target[1], 0}},
        .surface = row->triangle.surface,
        .respawn = row->triangle.respawn};
    PortableMeshSurface surface;
    if (!course->surface(course->context, row->triangle.surface, &surface) || surface.type > 9u ||
        !floor_height(row, target, &hit.witness.point_q16[2]))
        return PORTABLE_MESH_INVALID;
    hit.type = surface.type;
    memcpy(hit.normal_q30, row->normal, sizeof hit.normal_q30);
    *out = hit;
    return PORTABLE_MESH_OK;
}
