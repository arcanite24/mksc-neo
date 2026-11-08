#ifndef MKSC_PORTABLE_CAMERA_AFFINE_H
#define MKSC_PORTABLE_CAMERA_AFFINE_H

#include <stddef.h>
#include <stdint.h>

#include "camera_state.h"

enum
{
    PORTABLE_CAMERA_AFFINE_LINES = 96u,
    PORTABLE_CAMERA_AFFINE_MAX_LINES = 170u,
    PORTABLE_CAMERA_AFFINE_OK = 0,
    PORTABLE_CAMERA_AFFINE_INVALID_ARGUMENT = 1,
    PORTABLE_CAMERA_AFFINE_DESTINATION_TOO_SMALL = 2,
    PORTABLE_CAMERA_AFFINE_DIVISION_BY_ZERO = 3,
};

typedef struct
{
    int32_t dx;
    int32_t dy;
    int32_t pa;
    int32_t pb;
    int32_t pc;
    int32_t pd;
} PortableCameraAffineLine;

/*
 * Reproduce cam_8030918 and sub_SetHDMA into a caller-owned scanline buffer.
 * The output is indexed by visible scanline minus 0x40, and the phase is
 * toggled exactly once after the 96 records are written.
 */
int portable_camera_affine_build(
    const PortableCameraState *state,
    int32_t field_0x30_q16,
    int32_t pitch_sin_q15,
    int32_t pitch_cos_q15,
    int32_t yaw_sin_q15,
    int32_t yaw_cos_q15,
    PortableCameraAffineLine *lines,
    size_t line_capacity,
    uint8_t *phase);

/* Presentation-only resolution extension: fractional native rows, unchanged FOV.
 * Coefficients retain Q8 precision; the raster divides physical X by scale. */
int portable_camera_affine_build_scaled(
    const PortableCameraState *state,int32_t field_0x30_q16,
    int32_t pitch_sin_q15,int32_t pitch_cos_q15,
    int32_t yaw_sin_q15,int32_t yaw_cos_q15,
    PortableCameraAffineLine *lines,size_t line_capacity,uint8_t *phase,
    uint8_t scale);

/* Extra rows continue the same perspective for upward camera translations. */
int portable_camera_affine_build_scaled_rows(
    const PortableCameraState *state,int32_t field_0x30_q16,
    int32_t pitch_sin_q15,int32_t pitch_cos_q15,int32_t yaw_sin_q15,int32_t yaw_cos_q15,
    PortableCameraAffineLine *lines,size_t line_capacity,uint8_t *phase,
    uint8_t scale,uint16_t native_rows);

#endif
