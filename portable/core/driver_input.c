#include "driver_input.h"

#include <stddef.h>
#include <stdint.h>

static int32_t signed32_from_bits(uint32_t bits)
{
    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

static int32_t add_wrap32(int32_t left, int32_t right)
{
    return signed32_from_bits((uint32_t)left + (uint32_t)right);
}

static int32_t speed_offset(uint16_t parameter)
{
    const uint32_t clamped = parameter > 0x40u ? 0x40u : parameter;
    uint32_t offset;

    if (parameter <= 4u)
        offset = clamped << 15;
    else if (parameter <= 0x14u)
        offset = (clamped << 17) + UINT32_C(0xfff60000);
    else if (parameter <= 0x24u)
        offset = (clamped << 16) + UINT32_C(0x00120000);
    else if (parameter <= 0x40u)
        offset = (clamped << 15) + UINT32_C(0x00220000);
    else
        offset = 0;
    return signed32_from_bits(offset);
}

int portable_driver_input_tick(PortableDriverInputState *state,
                               uint16_t control_word)
{
    if (state == NULL)
        return PORTABLE_DRIVER_INPUT_INVALID_ARGUMENT;
    if (state->state_flags != 0)
        return PORTABLE_DRIVER_INPUT_UNSUPPORTED_FLAGS;

    state->speed_target_q16 = add_wrap32(
        state->base_speed_q16, speed_offset(state->speed_parameter));
    state->control_word = control_word;
    return PORTABLE_DRIVER_INPUT_OK;
}

uint16_t portable_driver_control_word_from_gba_keys(uint16_t raw_keys)
{
    uint16_t control_word = 0;

    if ((raw_keys & UINT16_C(0x0010)) != 0)
        control_word |= UINT16_C(0x0100);
    if ((raw_keys & UINT16_C(0x0020)) != 0)
        control_word |= UINT16_C(0x0200);
    if ((raw_keys & UINT16_C(0x0040)) != 0)
        control_word |= UINT16_C(0x0800);
    if ((raw_keys & UINT16_C(0x0080)) != 0)
        control_word |= UINT16_C(0x0400);
    if ((raw_keys & UINT16_C(0x0001)) != 0)
        control_word |= UINT16_C(0x8000);
    if ((raw_keys & UINT16_C(0x0002)) != 0)
        control_word |= UINT16_C(0x4000);
    if ((raw_keys & UINT16_C(0x0100)) != 0)
        control_word |= UINT16_C(0x0010);
    if ((raw_keys & UINT16_C(0x0200)) != 0)
        control_word |= UINT16_C(0x0040);
    return control_word;
}

uint16_t portable_driver_control_word_from_semantic_buttons(uint16_t buttons)
{
    uint16_t control_word = 0;

    if ((buttons & PORTABLE_INPUT_ACCELERATE) != 0)
        control_word |= UINT16_C(0x8000);
    if ((buttons & PORTABLE_INPUT_BRAKE) != 0)
        control_word |= UINT16_C(0x4000);
    if ((buttons & PORTABLE_INPUT_RIGHT) != 0)
        control_word |= UINT16_C(0x0100);
    if ((buttons & PORTABLE_INPUT_LEFT) != 0)
        control_word |= UINT16_C(0x0200);
    if ((buttons & PORTABLE_INPUT_HOP) != 0)
        control_word |= UINT16_C(0x0010);
    if ((buttons & PORTABLE_INPUT_ITEM) != 0)
        control_word |= UINT16_C(0x0040);
    if ((buttons & PORTABLE_INPUT_UP) != 0)
        control_word |= UINT16_C(0x0800);
    if ((buttons & PORTABLE_INPUT_DOWN) != 0)
        control_word |= UINT16_C(0x0400);
    return control_word;
}
