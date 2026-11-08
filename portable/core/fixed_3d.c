#include "fixed_3d.h"
#include <limits.h>
uint64_t portable_fixed_3d_sqrt(uint64_t n)
{
    uint64_t result = 0, bit = UINT64_C(1) << 62u;
    while (bit > n)
        bit >>= 2u;
    while (bit)
    {
        if (n >= result + bit)
        {
            n -= result + bit;
            result = (result >> 1u) + bit;
        }
        else
            result >>= 1u;
        bit >>= 2u;
    }
    return result;
}
int32_t portable_fixed_3d_clamp(int64_t n)
{
    return n > INT32_MAX ? INT32_MAX : n < INT32_MIN ? INT32_MIN : (int32_t)n;
}
int32_t portable_fixed_3d_mul(int32_t a, int32_t b)
{
    return portable_fixed_3d_clamp((int64_t)a * b / 65536);
}
int32_t portable_fixed_3d_dt(int32_t n)
{
    return portable_fixed_3d_clamp((int64_t)n * 4389 / 262144);
}
int64_t portable_fixed_3d_dot(const int32_t a[3], const int32_t b[3])
{
    return (int64_t)a[0] * b[0] + (int64_t)a[1] * b[1] + (int64_t)a[2] * b[2];
}
int portable_fixed_3d_normalize(const int64_t input[3], int32_t out[3])
{
    uint64_t largest = 0;
    int64_t v[3];
    for (unsigned i = 0; i < 3u; ++i)
    {
        const uint64_t n = input[i] < 0 ? (uint64_t)(-(input[i] + 1)) + 1u : (uint64_t)input[i];
        if (n > largest)
            largest = n;
    }
    if (!largest)
        return 0;
    unsigned shift = 0;
    while ((largest >> shift) > UINT64_C(536870911))
        ++shift;
    const int64_t divisor = INT64_C(1) << shift;
    uint64_t square = 0;
    unsigned lift = 0;
    while (largest && largest < UINT64_C(268435456))
    {
        largest *= 2u;
        ++lift;
    }
    const int64_t multiplier = INT64_C(1) << lift;
    for (unsigned i = 0; i < 3u; ++i)
    {
        v[i] = (input[i] / divisor) * multiplier;
        square += (uint64_t)(v[i] * v[i]);
    }
    const uint64_t length = portable_fixed_3d_sqrt(square);
    if (!length)
        return 0;
    for (unsigned i = 0; i < 3u; ++i)
        out[i] = (int32_t)(v[i] * 1073741824 / (int64_t)length);
    return 1;
}
void portable_fixed_3d_sincos(uint16_t angle, int32_t *sine, int32_t *cosine)
{
    /* atan(2^-i) in binary turns, mathematical CORDIC constants. */ static const int32_t
        angles[16] = {8192, 4836, 2555, 1297, 651, 326, 163, 81, 41, 20, 10, 5, 3, 1, 1, 0};
    int32_t z = (int32_t)angle;
    if (z >= 32768)
        z -= 65536;
    int32_t sign = 1;
    if (z > 16384)
    {
        z -= 32768;
        sign = -1;
    }
    else if (z < -16384)
    {
        z += 32768;
        sign = -1;
    }
    int64_t x = 652032874, y = 0;
    for (unsigned i = 0; i < 16u; ++i)
    {
        const int64_t d = INT64_C(1) << i, old = x;
        if (z >= 0)
        {
            x -= y / d;
            y += old / d;
            z -= angles[i];
        }
        else
        {
            x += y / d;
            y -= old / d;
            z += angles[i];
        }
    }
    *sine = portable_fixed_3d_clamp(y * sign);
    *cosine = portable_fixed_3d_clamp(x * sign);
    if (angle == 0)
    {
        *sine = 0;
        *cosine = 1073741824;
    }
    else if (angle == 16384)
    {
        *sine = 1073741824;
        *cosine = 0;
    }
    else if (angle == 32768)
    {
        *sine = 0;
        *cosine = -1073741824;
    }
    else if (angle == 49152)
    {
        *sine = -1073741824;
        *cosine = 0;
    }
}
