#include "camera_state.h"

#include <stddef.h>

enum
{
    PORTABLE_CAMERA_SCREEN_X = 0x78,
    PORTABLE_CAMERA_SCREEN_Y = 0x8C,
    PORTABLE_CAMERA_DISTANCE = 0x40,
    PORTABLE_CAMERA_ELEVATION = 0x100,
    PORTABLE_CAMERA_PITCH = 0x347B,
};

void portable_camera_state_update(PortableCameraState *state,
                                  int32_t driver_x,
                                  int32_t driver_y,
                                  uint16_t driver_heading)
{
    if (state == NULL)
        return;

    /* sub_804C030 -> sub_8030E80 and the camera setters. */
    state->screen_x = PORTABLE_CAMERA_SCREEN_X;
    state->screen_y = PORTABLE_CAMERA_SCREEN_Y;
    state->projection_y_offset = 0;
    state->projection_y_offset_q16 = 0;
    state->projection_y_offset_precise = 0;
    state->distance_q16 = PORTABLE_CAMERA_DISTANCE << 16;
    state->elevation_q16 = PORTABLE_CAMERA_ELEVATION << 16;
    state->horizon_q16 = state->distance_q16;
    state->pitch = PORTABLE_CAMERA_PITCH;
    state->yaw = driver_heading;
    state->position_x = driver_x;
    state->position_y = driver_y;

    /* cam_SetPos -> sub_8031100: reset temporal state to this snapshot. */
    state->temporal_x = driver_x;
    state->temporal_y = driver_y;
    state->temporal_yaw = driver_heading;
    state->temporal_pitch = 0;
    state->hdma_phase = 0;
    state->race_camera_field = 0;
}
