#ifndef MKSC_PORTABLE_CAMERA_STATE_H
#define MKSC_PORTABLE_CAMERA_STATE_H

#include <stdint.h>

typedef struct
{
    int32_t screen_x;
    int32_t screen_y;
    int8_t projection_y_offset; /* signed source camera +380, independent of road perspective */
    int32_t elevation_q16;
    int32_t distance_q16;
    int32_t horizon_q16;
    int32_t position_x;
    int32_t position_y;
    int32_t temporal_x;
    int32_t temporal_y;
    uint16_t pitch;
    uint16_t yaw;
    uint16_t temporal_yaw;
    uint16_t temporal_pitch;
    uint8_t hdma_phase;
    uint32_t race_camera_field;
    /* Optional presentation translation; source integer projection stays on
     * projection_y_offset. Precise renderers may retain physical-row phase. */
    int32_t projection_y_offset_q16;
    uint8_t projection_y_offset_precise;
} PortableCameraState;

void portable_camera_state_update(PortableCameraState *state,
                                  int32_t driver_x,
                                  int32_t driver_y,
                                  uint16_t driver_heading);

#endif
