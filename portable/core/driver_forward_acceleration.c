#include "driver_forward_acceleration.h"

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

static int32_t multiply_wrap32(int16_t left, int16_t right)
{
    return signed32_from_bits((uint32_t)((int64_t)left * right));
}

static int32_t arithmetic_shift16(int32_t value)
{
    if (value >= 0)
        return value / 65536;
    return (int32_t)(-((-(int64_t)value + 65535) / 65536));
}

static int32_t arithmetic_shift22(int32_t value)
{
    if (value >= 0)
        return value / 4194304;
    return (int32_t)(-((-(int64_t)value + 4194303) / 4194304));
}

static uint16_t add_low16(uint16_t left, int32_t right)
{
    return (uint16_t)((uint32_t)left + (uint32_t)right);
}

int portable_driver_forward_acceleration_tick(
    PortableDriverForwardAccelerationState *state,
    const int16_t *global_response_curve,
    size_t global_curve_count,
    const int16_t *local_acceleration_table,
    size_t local_table_count)
{
    int32_t normalized_speed;
    int32_t speed_high_signed;
    uint32_t speed_high_unsigned;
    uint32_t global_index;
    int32_t response_product;
    int32_t response_delta;
    int32_t local_index_source;
    uint32_t local_index;

    if (state == NULL || global_response_curve == NULL ||
        local_acceleration_table == NULL)
        return PORTABLE_DRIVER_FORWARD_ACCELERATION_INVALID_ARGUMENT;
    if (global_curve_count < 8u)
        return PORTABLE_DRIVER_FORWARD_ACCELERATION_GLOBAL_TABLE_TOO_SMALL;
    if (local_table_count < 16u)
        return PORTABLE_DRIVER_FORWARD_ACCELERATION_LOCAL_TABLE_TOO_SMALL;

    /* Match the source's negative-speed +0xffff before the high-half read. */
    normalized_speed = state->speed_q16;
    if (normalized_speed < 0)
        normalized_speed = add_wrap32(normalized_speed, 0xffff);
    speed_high_unsigned = (uint32_t)normalized_speed >> 16;
    speed_high_signed = (int16_t)speed_high_unsigned;

    if (speed_high_signed > 0x3fe)
        global_index = 7u;
    else if (speed_high_signed - 0x300 < 0)
        global_index = 0u;
    else
        global_index = (uint32_t)((speed_high_signed - 0x300) >> 5) & 7u;

    response_product = multiply_wrap32(
        global_response_curve[global_index], state->response_q8);
    if (response_product < 0)
        response_product = add_wrap32(response_product, 0xffff);
    response_delta = arithmetic_shift16(response_product);
    state->response_q8 = (int16_t)add_low16(
        (uint16_t)state->response_q8, response_delta);

    if ((state->state_flags & UINT16_C(1)) != 0)
    {
        state->acceleration_q16 = INT32_C(0x0000c000);
        return PORTABLE_DRIVER_FORWARD_ACCELERATION_OK;
    }

    /* The source takes the signed high half, shifts it by six, then masks. */
    local_index_source = arithmetic_shift22(
        signed32_from_bits(speed_high_unsigned << 16));
    local_index = (uint32_t)local_index_source & 0xfu;
    state->acceleration_q16 = signed32_from_bits(
        (uint32_t)(int32_t)local_acceleration_table[local_index] << 8);
    return PORTABLE_DRIVER_FORWARD_ACCELERATION_OK;
}
