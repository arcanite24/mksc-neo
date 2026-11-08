#include "route_geometry.h"

/* A Q16 point relative to a signed-world route endpoint needs 33 bits;
 * its dot/cross product with a 17-bit edge fits signed 64 bits. Comparing
 * cross^2/length distances needs fewer than 132 unsigned bits. Five limbs
 * preserve those comparisons exactly on targets without a 128-bit type. */
typedef struct
{
    uint32_t limb[5];
} RouteWide;

typedef struct
{
    RouteWide numerator;
    uint64_t denominator;
} RouteDistance;

static RouteWide wide_from_u64(uint64_t value)
{
    const RouteWide result = {{(uint32_t)value, (uint32_t)(value >> 32), 0u, 0u, 0u}};
    return result;
}

static RouteWide wide_multiply(RouteWide left, RouteWide right)
{
    RouteWide result = {{0u}};
    for (unsigned i = 0u; i < 5u; ++i)
    {
        uint64_t carry = 0u;
        for (unsigned j = 0u; i + j < 5u; ++j)
        {
            const uint64_t product =
                (uint64_t)left.limb[i] * right.limb[j] + result.limb[i + j] + carry;
            result.limb[i + j] = (uint32_t)product;
            carry = product >> 32;
        }
    }
    return result;
}

static RouteWide wide_add(RouteWide left, RouteWide right)
{
    RouteWide result;
    uint64_t carry = 0u;
    for (unsigned i = 0u; i < 5u; ++i)
    {
        const uint64_t sum = (uint64_t)left.limb[i] + right.limb[i] + carry;
        result.limb[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return result;
}

static int wide_compare(RouteWide left, RouteWide right)
{
    for (unsigned i = 5u; i != 0u; --i)
    {
        if (left.limb[i - 1u] != right.limb[i - 1u])
            return left.limb[i - 1u] < right.limb[i - 1u] ? -1 : 1;
    }
    return 0;
}

static RouteWide wide_square(int64_t value)
{
    const uint64_t magnitude = value < 0 ? (uint64_t)-value : (uint64_t)value;
    const RouteWide operand = wide_from_u64(magnitude);
    return wide_multiply(operand, operand);
}

static RouteDistance endpoint_distance(int64_t x, int64_t y)
{
    const RouteDistance result = {wide_add(wide_square(x), wide_square(y)), UINT64_C(1)};
    return result;
}

static int distance_less(RouteDistance left, RouteDistance right)
{
    return wide_compare(wide_multiply(left.numerator, wide_from_u64(right.denominator)),
                        wide_multiply(right.numerator, wide_from_u64(left.denominator))) < 0;
}

size_t portable_route_nearest_segment(const PortableRacePoint *route, size_t count, int32_t x_q16,
                                      int32_t y_q16)
{
    RouteDistance nearest = {{{0u}}, 1u};
    size_t selected = 0u;
    int found = 0;
    for (size_t index = 0u; index < count; ++index)
    {
        const size_t next = (index + 1u) % count;
        const int64_t dx = (int64_t)route[next].x - route[index].x;
        const int64_t dy = (int64_t)route[next].y - route[index].y;
        const uint64_t length = (uint64_t)(dx * dx + dy * dy);
        if (length == 0u)
            continue;
        const int64_t px = (int64_t)x_q16 - (int64_t)route[index].x * INT64_C(65536);
        const int64_t py = (int64_t)y_q16 - (int64_t)route[index].y * INT64_C(65536);
        const int64_t dot = px * dx + py * dy;
        RouteDistance candidate;
        size_t segment = index;
        if (dot <= 0)
            candidate = endpoint_distance(px, py);
        else if ((uint64_t)dot >= length * UINT64_C(65536))
        {
            candidate = endpoint_distance(px - dx * INT64_C(65536), py - dy * INT64_C(65536));
            segment = next;
        }
        else
        {
            candidate.numerator = wide_square(px * dy - py * dx);
            candidate.denominator = length;
        }
        if (!found || distance_less(candidate, nearest))
        {
            nearest = candidate;
            selected = segment;
            found = 1;
        }
    }
    return selected;
}

static int64_t signed_shift_right(int64_t value, unsigned shift)
{
    const int64_t divisor = INT64_C(1) << shift;
    if (value >= 0)
        return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static int64_t quadrant_angle(uint64_t north, uint64_t east)
{
    /* round(atan(2^-i) * 2^56 / (2*pi)): mathematical constants, not game
     * content. Normalizing the larger coordinate to [2^60,2^61) leaves
     * ample signed-64 headroom for CORDIC's gain and bounds rounding error. */
    static const int64_t turns[] = {
        INT64_C(9007199254740992),
        INT64_C(5317260203651848),
        INT64_C(2809494260081567),
        INT64_C(1426143154396570),
        INT64_C(715839029270683),
        INT64_C(358268477703266),
        INT64_C(179177955010598),
        INT64_C(89594445026371),
        INT64_C(44797906047171),
        INT64_C(22399038468267),
        INT64_C(11199529914810),
        INT64_C(5599766292493),
        INT64_C(2799883313132),
        INT64_C(1399941677427),
        INT64_C(699970841321),
        INT64_C(349985420986),
        INT64_C(174992710534),
        INT64_C(87496355272),
        INT64_C(43748177637),
        INT64_C(21874088818),
        INT64_C(10937044409),
        INT64_C(5468522205),
        INT64_C(2734261102),
        INT64_C(1367130551),
        INT64_C(683565276),
        INT64_C(341782638),
        INT64_C(170891319),
        INT64_C(85445659),
        INT64_C(42722830),
        INT64_C(21361415),
        INT64_C(10680707),
        INT64_C(5340354),
        INT64_C(2670177),
        INT64_C(1335088),
        INT64_C(667544),
        INT64_C(333772),
        INT64_C(166886),
        INT64_C(83443),
        INT64_C(41722),
        INT64_C(20861),
        INT64_C(10430),
        INT64_C(5215),
        INT64_C(2608),
        INT64_C(1304),
        INT64_C(652),
        INT64_C(326),
        INT64_C(163),
        INT64_C(81),
        INT64_C(41),
        INT64_C(20),
        INT64_C(10),
        INT64_C(5),
        INT64_C(3),
        INT64_C(1),
        INT64_C(1),
    };
    uint64_t largest = north > east ? north : east;
    unsigned shift = 0u;
    while (largest < (UINT64_C(1) << 60))
    {
        largest <<= 1;
        ++shift;
    }
    int64_t x = (int64_t)(north << shift);
    int64_t y = (int64_t)(east << shift);
    int64_t angle = 0;
    for (unsigned i = 0u; i < sizeof turns / sizeof turns[0] && y != 0; ++i)
    {
        const int64_t sx = signed_shift_right(x, i);
        const int64_t sy = signed_shift_right(y, i);
        if (y > 0)
        {
            x += sy;
            y -= sx;
            angle += turns[i];
        }
        else
        {
            x -= sy;
            y += sx;
            angle -= turns[i];
        }
    }
    return angle;
}

uint16_t portable_route_bearing(int32_t delta_x, int32_t delta_y)
{
    if (delta_x == 0)
        return delta_y < 0 ? 0u : UINT16_C(32768);
    if (delta_y == 0)
        return delta_x > 0 ? UINT16_C(16384) : UINT16_C(49152);
    const uint64_t east = delta_x < 0 ? (uint64_t)-(int64_t)delta_x : (uint64_t)delta_x;
    const uint64_t north = delta_y < 0 ? (uint64_t)-(int64_t)delta_y : (uint64_t)delta_y;
    const int64_t quarter = quadrant_angle(north, east);
    const int64_t half_turn = INT64_C(1) << 55;
    const int64_t full_turn = INT64_C(1) << 56;
    const int64_t angle = delta_y < 0 ? (delta_x > 0 ? quarter : full_turn - quarter)
                                      : (delta_x > 0 ? half_turn - quarter : half_turn + quarter);
    return (uint16_t)(angle / (INT64_C(1) << 40));
}
