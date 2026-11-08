#include "camera_projection.h"

#include <limits.h>
#include <stddef.h>

static int64_t div_trunc(int64_t numerator, int64_t denominator)
{
    return numerator / denominator;
}

static int64_t abs64(int64_t value)
{
    return value < 0 ? -value : value;
}

static int32_t signed32_from_bits(uint32_t bits)
{
    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

static int32_t source_subtract32(int32_t left, int32_t right)
{
    return signed32_from_bits((uint32_t)left - (uint32_t)right);
}

static int32_t source_negate32(int32_t value)
{
    return signed32_from_bits(0u - (uint32_t)value);
}

static int64_t arithmetic_shift_right(int64_t value, unsigned shift)
{
    const int64_t divisor = INT64_C(1) << shift;

    if (value >= 0)
        return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static int32_t source_basis_product(int32_t left, int32_t right)
{
    return (int32_t)div_trunc((int64_t)left * right * 2, 0x8000);
}

static int32_t source_reference_component(int32_t position,
                                          int32_t delta,
                                          int32_t basis_z)
{
    unsigned shift_left;
    unsigned shift_right;
    int64_t scaled;

    if (abs64(delta) <= 0xffff)
    {
        shift_left = 0;
        shift_right = 16;
    }
    else if (abs64(delta) <= 0xfffff)
    {
        shift_left = 5;
        shift_right = 11;
    }
    else if (abs64(delta) <= 0xffffff)
    {
        shift_left = 9;
        shift_right = 7;
    }
    else
    {
        shift_left = 13;
        shift_right = 3;
    }
    scaled = arithmetic_shift_right(delta, shift_left);
    scaled = arithmetic_shift_right(scaled * basis_z, shift_right);
    return signed32_from_bits((uint32_t)((int64_t)position - scaled));
}

int portable_camera_projection_build(
    const PortableCameraState *state,
    int32_t yaw_sin_q15,
    int32_t yaw_cos_q15,
    int32_t pitch_sin_q15,
    int32_t pitch_cos_q15,
    PortableCameraProjection *out)
{
    int32_t camera_delta;

    if (state == NULL || out == NULL)
        return PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT;

    /* This is unk_updateCamAngle's three source basis rows. */
    out->basis[0][0] = yaw_cos_q15 * 2;
    out->basis[0][1] = source_basis_product(-yaw_sin_q15, pitch_cos_q15);
    out->basis[0][2] = source_basis_product(-yaw_sin_q15, pitch_sin_q15);
    out->basis[1][0] = yaw_sin_q15 * 2;
    out->basis[1][1] = source_basis_product(yaw_cos_q15, pitch_cos_q15);
    out->basis[1][2] = source_basis_product(yaw_cos_q15, pitch_sin_q15);
    out->basis[2][0] = 0;
    out->basis[2][1] = -pitch_sin_q15 * 2;
    out->basis[2][2] = pitch_cos_q15 * 2;

    camera_delta = state->elevation_q16 - state->distance_q16;
    out->reference_x = source_reference_component(
        state->position_x, camera_delta, out->basis[0][2]);
    out->reference_y = source_reference_component(
        state->position_y, camera_delta, out->basis[1][2]);
    out->reference_z = source_reference_component(0, camera_delta,
                                                   out->basis[2][2]);
    return PORTABLE_CAMERA_PROJECTION_OK;
}

int portable_camera_projection_project(
    const PortableCameraState *state,
    const PortableCameraProjection *projection,
    int32_t actor_x,
    int32_t actor_y,
    int32_t actor_z,
    PortableCameraProjectionResult *out)
{
    int64_t rel_x;
    int64_t rel_y;
    int64_t rel_z;
    int64_t dist1_x;
    int64_t dist1_y;
    int64_t dist1_z;
    int64_t dist2_x;
    int64_t dist2_y;
    int64_t dist2_z;
    int64_t dist3_y;
    int64_t dist3_z;
    int64_t point_x;
    int64_t point_y;
    int64_t point_z;
    int64_t relative_depth;
    int64_t distance;
    int64_t draw_x;
    int64_t draw_y;

    (void)actor_z;
    if (state == NULL || projection == NULL || out == NULL)
        return PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT;

    /* cam_CalcProjectActor stores these differences in s32 locals before
     * applying IN_RANGE. Preserve the original 32-bit subtraction when a
     * wrapped native pose crosses the signed boundary. */
    rel_x = source_subtract32(actor_x, projection->reference_x);
    rel_y = source_subtract32(actor_y, projection->reference_y);
    rel_z = source_negate32(projection->reference_z);
    if (rel_x < -0x3ffffff || rel_x > 0x3ffffff ||
        rel_y < -0x3ffffff || rel_y > 0x3ffffff ||
        rel_z < -0x3ffffff || rel_z > 0x3ffffff)
        return PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE;

    dist1_x = div_trunc(projection->basis[0][0], 16);
    dist1_x = div_trunc(dist1_x * div_trunc(rel_x, 128), 32);
    dist1_y = div_trunc(projection->basis[0][1], 16);
    dist1_y = div_trunc(dist1_y * div_trunc(rel_x, 128), 32);
    dist1_z = div_trunc(projection->basis[0][2], 16);
    dist1_z = div_trunc(dist1_z * div_trunc(rel_x, 128), 32);

    dist2_x = div_trunc(projection->basis[1][0], 16);
    dist2_x = div_trunc(dist2_x * div_trunc(rel_y, 128), 32);
    dist2_y = div_trunc(projection->basis[1][1], 16);
    dist2_y = div_trunc(dist2_y * div_trunc(rel_y, 128), 32);
    dist2_z = div_trunc(projection->basis[1][2], 16);
    dist2_z = div_trunc(dist2_z * div_trunc(rel_y, 128), 32);

    dist3_y = div_trunc(projection->basis[2][1], 16);
    dist3_y = div_trunc(dist3_y * div_trunc(rel_z, 128), 32);
    dist3_z = div_trunc(projection->basis[2][2], 16);
    dist3_z = div_trunc(dist3_z * div_trunc(rel_z, 128), 32);

    point_x = dist1_x + dist2_x;
    point_y = dist1_y + dist2_y + dist3_y;
    point_z = dist1_z + dist2_z + dist3_z;
    relative_depth = (int64_t)state->elevation_q16 - point_z;
    if (relative_depth <= 0)
        return PORTABLE_CAMERA_PROJECTION_BEHIND_CAMERA;

    if (relative_depth < 0x200000)
        distance = div_trunc(relative_depth * 0x100,
                             state->elevation_q16);
    else
        distance = div_trunc(relative_depth,
                             div_trunc(state->elevation_q16, 0x100));
    if (distance < 0x20 || distance > 0x400)
        return PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE;

    draw_x = div_trunc(div_trunc(point_x, distance), 0x100) +
             state->screen_x;
    draw_y = div_trunc(div_trunc(point_y, distance), 0x100) +
             state->screen_y + state->projection_y_offset;
    if (draw_x < INT16_MIN || draw_x > INT16_MAX ||
        draw_y < INT16_MIN || draw_y > INT16_MAX)
        return PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE;

    out->x = (int16_t)draw_x;
    out->y = (int16_t)draw_y;
    distance *= 4;
    if (distance < INT16_MIN || distance > INT16_MAX)
        return PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE;
    out->distance = (int16_t)distance;
    return PORTABLE_CAMERA_PROJECTION_OK;
}

int portable_camera_projection_project_precise(
    const PortableCameraState *state, const PortableCameraProjection *projection,
    int32_t actor_x, int32_t actor_y, int32_t actor_z,
    PortableCameraProjectionPreciseResult *out)
{
    if (state == NULL || projection == NULL || out == NULL ||
        state->elevation_q16 <= 0 || state->projection_y_offset_precise > 1u)
        return PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT;
    PortableCameraProjectionResult source;
    const int status = portable_camera_projection_project(state,projection,
        actor_x,actor_y,actor_z,&source);
    if (status != PORTABLE_CAMERA_PROJECTION_OK) return status;
    const int32_t relative[3] = {
        source_subtract32(actor_x,projection->reference_x),
        source_subtract32(actor_y,projection->reference_y),
        source_negate32(projection->reference_z)};
    int64_t point[3];
    for (unsigned axis = 0u; axis < 3u; ++axis) {
        int64_t sum = 0;
        for (unsigned row = 0u; row < 3u; ++row)
            sum += (int64_t)projection->basis[row][axis] * relative[row];
        point[axis] = sum / 65536;
    }
    const int64_t depth = (int64_t)state->elevation_q16 - point[2];
    if (depth <= 0) return PORTABLE_CAMERA_PROJECTION_BEHIND_CAMERA;
    if (abs64(point[0]) > INT64_MAX / state->elevation_q16 ||
        abs64(point[1]) > INT64_MAX / state->elevation_q16 ||
        depth > INT64_MAX / INT64_C(67108864))
        return PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE;
    const int64_t x = (int64_t)state->screen_x * 65536 +
        point[0] * state->elevation_q16 / depth;
    const int64_t offset = state->projection_y_offset_precise
        ? state->projection_y_offset_q16 : (int64_t)state->projection_y_offset * 65536;
    const int64_t y = (int64_t)state->screen_y * 65536 + offset +
        point[1] * state->elevation_q16 / depth;
    const int64_t distance = depth * INT64_C(67108864) / state->elevation_q16;
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
        distance <= 0 || distance > INT32_MAX)
        return PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE;
    *out = (PortableCameraProjectionPreciseResult){(int32_t)x,(int32_t)y,(int32_t)distance};
    return PORTABLE_CAMERA_PROJECTION_OK;
}

int portable_camera_projection_body_precise(
    const PortableCameraProjectionPreciseResult *ground, int32_t height_q16,
    PortableCameraProjectionPreciseResult *body)
{
    if (ground == NULL || body == NULL || ground->distance_q16 <= 0)
        return PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT;
    const int64_t shift = (int64_t)height_q16 * INT64_C(67108864) / ground->distance_q16;
    const int64_t y = (int64_t)ground->y_q16 - shift;
    if (y < INT32_MIN || y > INT32_MAX) return PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE;
    PortableCameraProjectionPreciseResult next = *ground;
    next.y_q16 = (int32_t)y;
    *body = next;
    return PORTABLE_CAMERA_PROJECTION_OK;
}

int portable_camera_projection_height_shift(
    int32_t height_q16, int16_t distance, int16_t *out)
{
    if (out == NULL || height_q16 < 0 || distance <= 0)
        return PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT;
    /* 30F88 discards fourteen fractional bits before perspective division.
     * Combining the two divisions moves distant airborne sprites too high. */
    const int32_t units = height_q16 / INT32_C(16384);
    const int32_t shift = distance == 256 ? units :
        (units * INT32_C(256)) / distance;
    const uint16_t bits = (uint16_t)shift;
    *out = bits < UINT16_C(0x8000) ? (int16_t)bits :
        (int16_t)(-((int32_t)UINT32_C(0x10000) - bits));
    return PORTABLE_CAMERA_PROJECTION_OK;
}

int portable_camera_projection_body(const PortableCameraProjectionResult *ground,
    int32_t height_q16,PortableCameraProjectionResult *body)
{
    if(!ground || !body || (height_q16!=0 && ground->distance==0))
        return PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT;
    int32_t shift=0;
    if(height_q16!=0){
        /* Source ABS wraps INT32_MIN, then adds 0x3fff before ASR14 for a
         * negative magnitude. Division by 16384 truncates equivalently. */
        const int32_t magnitude=height_q16<0?source_negate32(height_q16):height_q16;
        const int32_t units=magnitude/16384;
        shift=ground->distance==256?units:(units*256)/ground->distance;
        if(height_q16<0)shift=source_negate32(shift);
    }
    const uint16_t bits=(uint16_t)((uint32_t)(int32_t)ground->y-(uint16_t)shift);
    PortableCameraProjectionResult next=*ground;
    next.y=bits<=INT16_MAX?(int16_t)bits:(int16_t)(-1-(int32_t)(UINT16_MAX-bits));
    *body=next;
    return PORTABLE_CAMERA_PROJECTION_OK;
}
