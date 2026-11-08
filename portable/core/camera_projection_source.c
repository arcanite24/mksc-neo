#include "camera_projection_source.h"
#include <stddef.h>

static int32_t signed32(uint32_t v)
{
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v);
}
static int16_t signed16(uint32_t v)
{
    v &= UINT32_C(65535);
    return v <= INT16_MAX ? (int16_t)v : (int16_t)(-1 - (int32_t)(65535u - v));
}
static int32_t add(int32_t a, int32_t b) { return signed32((uint32_t)a + (uint32_t)b); }
static int32_t subtract(int32_t a, int32_t b) { return signed32((uint32_t)a - (uint32_t)b); }
static int32_t multiply(int32_t a, int32_t b) { return signed32((uint32_t)a * (uint32_t)b); }
static int32_t divide(int32_t a, int32_t b)
{
    return signed32((uint32_t)((int64_t)a / b));
}
static int32_t product(int32_t coefficient, int32_t relative)
{
    return multiply(coefficient / 16, relative / 128) / 32;
}
int portable_camera_projection_source(const PortableCameraState *camera,
    const PortableCameraProjection *projection, int32_t actor_x, int32_t actor_y,
    int32_t bias_cache, PortableCameraProjectionResult *result)
{
    int32_t relative[3], point[3], depth, denominator, quotient;
    PortableCameraProjectionResult next;
    uint16_t narrowed;
    if (!camera || !projection || !result) return PORTABLE_SOURCE_PROJECTION_INVALID;
    next = *result; next.distance = -1;
    relative[0] = subtract(actor_x, projection->reference_x);
    relative[1] = subtract(actor_y, projection->reference_y);
    relative[2] = subtract(0, projection->reference_z);
    for (unsigned i = 0; i < 3; ++i)
        if (relative[i] < -INT32_C(0x3ffffff) || relative[i] > INT32_C(0x3ffffff))
            goto rejected;
    point[0] = add(product(projection->basis[0][0], relative[0]),
                   product(projection->basis[1][0], relative[1]));
    for (unsigned i = 1; i < 3; ++i)
        point[i] = add(add(product(projection->basis[0][i], relative[0]),
                           product(projection->basis[1][i], relative[1])),
                       product(projection->basis[2][i], relative[2]));
    depth = subtract(camera->elevation_q16, point[2]);
    if (depth <= 0) goto rejected;
    denominator = depth < INT32_C(0x200000) ? camera->elevation_q16 :
                                                            camera->elevation_q16 / 256;
    if (denominator == 0) return PORTABLE_SOURCE_PROJECTION_DIVISION_ZERO;
    quotient = divide(depth < INT32_C(0x200000) ? multiply(depth, 256) : depth,
                       denominator);
    narrowed = (uint16_t)quotient;
    if (narrowed < 32 || narrowed > 1024) goto rejected;
    next.x = signed16((uint32_t)add(divide(point[0], (int32_t)narrowed) / 256,
                                   camera->screen_x));
    next.y = signed16((uint32_t)add(add(divide(point[1], (int32_t)narrowed) / 256,
                                       camera->screen_y), bias_cache));
    next.distance = (int16_t)(narrowed * 4u);
rejected:
    *result = next;
    return PORTABLE_SOURCE_PROJECTION_OK;
}
int portable_camera_projection_height_source(int32_t height, int16_t depth,
                                              int16_t *result)
{
    int32_t magnitude, units, shift;
    if (!result) return PORTABLE_SOURCE_PROJECTION_INVALID;
    if (height == 0) { *result = 0; return PORTABLE_SOURCE_PROJECTION_OK; }
    if (depth == 0) return PORTABLE_SOURCE_PROJECTION_DIVISION_ZERO;
    if (height >= 0 && depth > 0)
        return portable_camera_projection_height_shift(height, depth, result);
    magnitude = height < 0 ? subtract(0, height) : height;
    units = magnitude / 16384;
    shift = depth == 256 ? units : divide(multiply(units, 256), depth);
    if (height < 0) shift = subtract(0, shift);
    *result = signed16((uint32_t)shift);
    return PORTABLE_SOURCE_PROJECTION_OK;
}
