#ifndef MKSC_PORTABLE_CAMERA_PROJECTION_SOURCE_H
#define MKSC_PORTABLE_CAMERA_PROJECTION_SOURCE_H
#include "camera_projection.h"
enum {
    PORTABLE_SOURCE_PROJECTION_OK = 0,
    PORTABLE_SOURCE_PROJECTION_INVALID = 1,
    PORTABLE_SOURCE_PROJECTION_DIVISION_ZERO = 2,
};
/* Actual30C3C DATA contract: rejection preserves XY but publishes depth -1.
 * Borrow the live basis/reference, never rebuild them. Screen outputs narrow;
 * original intermediate products/sums wrap32 and depth narrows before its gate.
 * Division-zero behavior is unqualified; error leaves the result unchanged. */
int portable_camera_projection_source(const PortableCameraState *camera,
    const PortableCameraProjection *projection, int32_t actor_x, int32_t actor_y,
    int32_t bias_cache, PortableCameraProjectionResult *result);
/* Actual30F88 signed world-height and signed depth arithmetic. Zero height
 * needs no denominator; nonnegative/positive branch reuses the native helper. */
int portable_camera_projection_height_source(int32_t height, int16_t depth,
                                              int16_t *result);
#endif
