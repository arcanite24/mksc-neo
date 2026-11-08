#include "character_camera.h"

#include <stddef.h>

static int16_t signed16(uint16_t value)
{
    return value < UINT16_C(0x8000)
        ? (int16_t)value
        : (int16_t)((int32_t)value - INT32_C(0x10000));
}

static uint16_t approach_angle(uint16_t current, uint16_t target,
                               uint16_t step)
{
    const int16_t delta = signed16((uint16_t)(target - current));
    uint16_t next;

    if (delta >= 0)
    {
        next = (uint16_t)(current + step);
        return signed16((uint16_t)(target - next)) < 0 ? target : next;
    }
    next = (uint16_t)(current - step);
    return signed16((uint16_t)(target - next)) > 0 ? target : next;
}

void portable_character_camera_init(
    PortableCharacterCameraState *state, uint16_t driver_heading_7c)
{
    if (state == NULL)
        return;
    *state = (PortableCharacterCameraState){
        .current_yaw = driver_heading_7c,
    };
}

uint16_t portable_character_camera_target_adjustment(
    const PortableCharacterCameraInput *input)
{
    int32_t position;

    if (input == NULL || input->response_status_7e != 1u)
        return 0u;
    position = signed16(input->steering_position_82);
    if (position < 0)
        position = -position;
    if (position <= INT32_C(0x1000))
        return 0u;
    if ((input->control_word_96 & UINT16_C(0x0200)) != 0u)
        return UINT16_C(0xfa00);
    if ((input->control_word_96 & UINT16_C(0x0100)) != 0u)
        return UINT16_C(0x0600);
    return 0u;
}

int portable_character_camera_tick(
    PortableCharacterCameraState *state,
    const PortableCharacterCameraInput *input)
{
    uint16_t target_adjustment;

    if (state == NULL || input == NULL)
        return 0;
    state->current_yaw = approach_angle(
        state->current_yaw, input->driver_heading_7c, UINT16_C(0x0480));
    target_adjustment = portable_character_camera_target_adjustment(input);
    if (state->adjustment_step < 40)
        state->adjustment_step = (int16_t)(state->adjustment_step + 8);
    else
        state->adjustment_step = 48;
    if (state->current_adjustment == 0u && target_adjustment == 0u)
        state->adjustment_step = 0;
    state->current_adjustment = approach_angle(
        state->current_adjustment, target_adjustment,
        (uint16_t)state->adjustment_step);
    return 1;
}

uint16_t portable_character_camera_yaw(
    const PortableCharacterCameraState *state)
{
    if (state == NULL)
        return 0u;
    return (uint16_t)(state->current_yaw + state->current_adjustment);
}
