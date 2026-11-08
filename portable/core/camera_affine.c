#include "camera_affine.h"

#include <stddef.h>

static int64_t div_trunc(int64_t numerator, int64_t denominator)
{
    return numerator / denominator;
}

static int32_t wrap_s32(int64_t value)
{
    uint32_t bits = (uint32_t)value;
    int64_t signed_bits = bits < UINT32_C(0x80000000)
                              ? (int64_t)bits
                              : (int64_t)bits - INT64_C(0x100000000);

    return (int32_t)signed_bits;
}

static int32_t s32_add(int32_t left, int32_t right)
{
    return wrap_s32((int64_t)left + right);
}

static int32_t s32_mul(int32_t left, int32_t right)
{
    return wrap_s32((int64_t)left * right);
}

static int32_t s32_div(int32_t numerator, int32_t denominator)
{
    return numerator / denominator;
}

int portable_camera_affine_build_scaled_rows(
    const PortableCameraState *state,
    int32_t field_0x30_q16,
    int32_t pitch_sin_q15,
    int32_t pitch_cos_q15,
    int32_t yaw_sin_q15,
    int32_t yaw_cos_q15,
    PortableCameraAffineLine *lines,
    size_t line_capacity,
    uint8_t *phase, uint8_t scale,uint16_t native_rows)
{
    int64_t field_0x34_q16;
    int64_t elevation_units;
    int32_t world_x;
    int32_t world_y;
    int32_t screen_x;

    if (state == NULL || lines == NULL || phase == NULL || scale<1u || scale>8u ||
        native_rows<PORTABLE_CAMERA_AFFINE_LINES || native_rows>PORTABLE_CAMERA_AFFINE_MAX_LINES)
        return PORTABLE_CAMERA_AFFINE_INVALID_ARGUMENT;
    if (line_capacity < native_rows*(size_t)scale)
        return PORTABLE_CAMERA_AFFINE_DESTINATION_TOO_SMALL;

    field_0x34_q16 = (int64_t)state->distance_q16 + field_0x30_q16;
    elevation_units = div_trunc(state->elevation_q16, 0x10000);
    world_x = (int32_t)div_trunc(state->position_x, 256);
    world_y = (int32_t)div_trunc(state->position_y, 256);
    screen_x = -(int64_t)state->screen_x;

    for (int32_t line = 0x40*scale; line < (0x40+native_rows)*scale; ++line)
    {
        const int64_t numerator =
            elevation_units * scale * pitch_cos_q15 +
            ((int64_t)line - (int64_t)state->screen_y*scale) * pitch_sin_q15;
        const int64_t denominator = div_trunc(numerator, 0x80*scale);
        int32_t source_x;
        int32_t source_y;
        int32_t pa;
        int32_t pb;
        int32_t pc;
        int32_t pd;
        const int32_t scanline = line - state->screen_y*scale;
        PortableCameraAffineLine *out = &lines[(size_t)line - 0x40u*scale];

        if (denominator == 0)
            return PORTABLE_CAMERA_AFFINE_DIVISION_BY_ZERO;
        source_x = (int32_t)div_trunc(field_0x34_q16, denominator);
        source_y = s32_div(s32_mul(pitch_cos_q15, source_x), 0x8000);
        pa = s32_div(s32_mul(source_y, yaw_cos_q15), 0x8000);
        pb = s32_div(s32_mul(wrap_s32(-(int64_t)source_x), yaw_sin_q15),
                     0x8000);
        pc = s32_div(s32_mul(source_y, yaw_sin_q15), 0x8000);
        pd = s32_div(s32_mul(source_x, yaw_cos_q15), 0x8000);
        out->dx = s32_add(
            s32_add(world_x, s32_mul(screen_x, pa)),
            wrap_s32(div_trunc((int64_t)scanline*pb,scale)));
        out->dy = s32_add(
            s32_add(world_y, s32_mul(screen_x, pc)),
            wrap_s32(div_trunc((int64_t)scanline*pd,scale)));
        /* BgAffineDstData stores each matrix term as a signed 16-bit field. */
        out->pa = (int32_t)(int16_t)pa;
        out->pb = (int32_t)(int16_t)pb;
        out->pc = (int32_t)(int16_t)pc;
        out->pd = (int32_t)(int16_t)pd;
    }
    *phase = (uint8_t)(1u - *phase);
    return PORTABLE_CAMERA_AFFINE_OK;
}

int portable_camera_affine_build_scaled(
    const PortableCameraState *state,int32_t field_0x30_q16,
    int32_t pitch_sin_q15,int32_t pitch_cos_q15,int32_t yaw_sin_q15,int32_t yaw_cos_q15,
    PortableCameraAffineLine *lines,size_t line_capacity,uint8_t *phase,uint8_t scale)
{
    return portable_camera_affine_build_scaled_rows(state,field_0x30_q16,pitch_sin_q15,
        pitch_cos_q15,yaw_sin_q15,yaw_cos_q15,lines,line_capacity,phase,scale,PORTABLE_CAMERA_AFFINE_LINES);
}

int portable_camera_affine_build(
    const PortableCameraState *state,int32_t field_0x30_q16,
    int32_t pitch_sin_q15,int32_t pitch_cos_q15,
    int32_t yaw_sin_q15,int32_t yaw_cos_q15,
    PortableCameraAffineLine *lines,size_t line_capacity,uint8_t *phase)
{
    return portable_camera_affine_build_scaled(state,field_0x30_q16,
        pitch_sin_q15,pitch_cos_q15,yaw_sin_q15,yaw_cos_q15,
        lines,line_capacity,phase,1u);
}
