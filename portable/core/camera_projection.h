#ifndef MKSC_PORTABLE_CAMERA_PROJECTION_H
#define MKSC_PORTABLE_CAMERA_PROJECTION_H

#include <stdint.h>

#include "camera_state.h"

enum
{
    PORTABLE_CAMERA_PROJECTION_OK = 0,
    PORTABLE_CAMERA_PROJECTION_INVALID_ARGUMENT = 1,
    PORTABLE_CAMERA_PROJECTION_BEHIND_CAMERA = 2,
    PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE = 3,
};

typedef struct
{
    int32_t basis[3][3];
    int32_t reference_x;
    int32_t reference_y;
    int32_t reference_z;
} PortableCameraProjection;

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t distance;
} PortableCameraProjectionResult;

/* Optional rendering coordinates. This separate type leaves source snapshots
 * and their integer ordering/culling unchanged. Units are native pixels Q16. */
typedef struct
{
    int32_t x_q16;
    int32_t y_q16;
    int32_t distance_q16;
} PortableCameraProjectionPreciseResult;

/*
 * Build the source camera basis from the same Q1.15 sine/cosine samples used
 * by unk_updateCamAngle. The samples are caller-owned so the native path does
 * not silently replace the game's fixed-point table with host libm.
 */
int portable_camera_projection_build(
    const PortableCameraState *state,
    int32_t yaw_sin_q15,
    int32_t yaw_cos_q15,
    int32_t pitch_sin_q15,
    int32_t pitch_cos_q15,
    PortableCameraProjection *out);

/* Source-shaped cam_CalcProjectActor for one Q16.16 actor position. */
int portable_camera_projection_project(
    const PortableCameraState *state,
    const PortableCameraProjection *projection,
    int32_t actor_x,
    int32_t actor_y,
    int32_t actor_z,
    PortableCameraProjectionResult *out);

/* Rendering-only perspective with full basis products and fractional depth.
 * Source project() still determines eligibility; use its result at native1x.
 * Like source project(), this projects the ground; body_precise applies height. */
int portable_camera_projection_project_precise(
    const PortableCameraState *state, const PortableCameraProjection *projection,
    int32_t actor_x, int32_t actor_y, int32_t actor_z,
    PortableCameraProjectionPreciseResult *out);
int portable_camera_projection_body_precise(
    const PortableCameraProjectionPreciseResult *ground, int32_t height_q16,
    PortableCameraProjectionPreciseResult *body);

/* 30F88/4AF24: qualified nonnegative world height, rounded before division.
 * Returns the signed source halfword used to move body Y above ground Y. */
int portable_camera_projection_height_shift(
    int32_t height_q16, int16_t distance, int16_t *out);

/* 2F638/30F88: derive body Y from a ground projection without changing depth.
 * Supports below-ground heights and signed source-halfword wrapping. */
int portable_camera_projection_body(const PortableCameraProjectionResult *ground,
    int32_t height_q16,PortableCameraProjectionResult *body);

#endif
