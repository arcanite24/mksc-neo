#include "kart_motion.h"

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

static int32_t multiply_wrap32(int32_t left, int16_t right)
{
    const int64_t product = (int64_t)left * right;

    return signed32_from_bits((uint32_t)product);
}

static int32_t negate_wrap32(int32_t value)
{
    return signed32_from_bits(0u - (uint32_t)value);
}

static int32_t arithmetic_shift_right(int32_t value, unsigned shift)
{
    const int64_t divisor = INT64_C(1) << shift;
    if (value >= 0)
        return (int32_t)(value / divisor);
    return (int32_t)(-((-(int64_t)value + divisor - 1) / divisor));
}

static int16_t signed16_from_bits(uint16_t bits)
{
    if (bits < UINT16_C(0x8000))
        return (int16_t)bits;
    return (int16_t)(-((int32_t)(UINT16_C(0x10000) - bits)));
}

static int16_t signed16_from_speed(int32_t speed_q16)
{
    return signed16_from_bits((uint16_t)((uint32_t)speed_q16 >> 16));
}

static int32_t rounded_component(int32_t product)
{
    if (product < 0)
        product = add_wrap32(product, 32767);
    return arithmetic_shift_right(product, 15u);
}

static int16_t stored_component(int32_t product)
{
    return signed16_from_bits((uint16_t)rounded_component(product));
}

int portable_kart_motion_tick(PortableKartMotionState *state,
                              const int32_t *sine_table,
                              size_t sine_count)
{
    int32_t adjusted_angle;
    uint32_t first_index;
    uint32_t second_index;
    int16_t scale;
    int32_t product;

    if (state == NULL || sine_table == NULL)
        return PORTABLE_KART_MOTION_INVALID_ARGUMENT;
    if (sine_count < 1024u)
        return PORTABLE_KART_MOTION_TABLE_TOO_SMALL;

    state->speed_q16 = add_wrap32(state->speed_q16,
                                  state->acceleration_q16);
    if (state->speed_q16 > INT32_C(0x07e00000))
        state->speed_q16 = INT32_C(0x07e00000);

    scale = signed16_from_speed(state->speed_q16);
    adjusted_angle = state->angle;
    if (adjusted_angle < 0)
        adjusted_angle += 0x3f;
    first_index = ((uint32_t)adjusted_angle >> 6) & UINT32_C(0x3ff);
    product = multiply_wrap32(sine_table[first_index], scale);
    state->component_x = stored_component(product);

    adjusted_angle = state->angle;
    if (adjusted_angle < 0)
        adjusted_angle += 0x3f;
    second_index =
        (UINT32_C(0x100) - (uint32_t)arithmetic_shift_right(adjusted_angle, 6u)) & UINT32_C(0x3ff);
    product = multiply_wrap32(negate_wrap32(sine_table[second_index]), scale);
    state->component_y = stored_component(product);
    return PORTABLE_KART_MOTION_OK;
}
