#ifndef MKSC_PORTABLE_CHARACTER_CAMERA_H
#define MKSC_PORTABLE_CHARACTER_CAMERA_H

#include <stdint.h>

typedef struct
{
    uint16_t current_yaw;
    uint16_t current_adjustment;
    int16_t adjustment_step;
} PortableCharacterCameraState;

typedef struct
{
    uint16_t driver_heading_7c;
    uint8_t response_status_7e;
    uint16_t steering_position_82;
    uint16_t control_word_96;
} PortableCharacterCameraInput;

void portable_character_camera_init(
    PortableCharacterCameraState *state, uint16_t driver_heading_7c);

uint16_t portable_character_camera_target_adjustment(
    const PortableCharacterCameraInput *input);

int portable_character_camera_tick(
    PortableCharacterCameraState *state,
    const PortableCharacterCameraInput *input);

uint16_t portable_character_camera_yaw(
    const PortableCharacterCameraState *state);

#endif
