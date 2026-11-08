#ifndef PORTABLE_GP_OPENING_CAMERA_H
#define PORTABLE_GP_OPENING_CAMERA_H

#include <stdint.h>

typedef struct {
    int32_t position_x_q16;
    int32_t position_y_q16;
    uint16_t yaw;
    int32_t target_angle;
    int32_t target_distance;
} PortableGpOpeningCamera;

/* sub_080527E0 -> sub_08052658: source grid camera during the first
 * 89 Shy Guy Beach opening turns. The caller supplies the recovered sine
 * table and rank-zero grid anchor; no host or captured frame is consumed. */
int portable_gp_opening_camera_build(uint32_t turn,
    int32_t grid_x_q16, int32_t grid_y_q16,
    const int32_t sine[1024], PortableGpOpeningCamera *out);

/* Same source orbit/held-focus owner, bounded through the hit continuation.
 * The caller owns current yaw and camera bob independently. */
int portable_gp_opening_camera_build_hit_continuation(uint32_t turn,
    int32_t grid_x_q16, int32_t grid_y_q16,
    const int32_t sine[1024], PortableGpOpeningCamera *out);

#endif
