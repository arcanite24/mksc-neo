#include "driver_brake.h"

#include <stdint.h>

static int32_t signed32_from_bits(uint32_t bits)
{
    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

int portable_driver_brake_acceleration_tick(
    PortableDriverBrakeState *state,
    const int16_t *brake_table,
    size_t brake_table_count,
    const int16_t *brake_accelerate_table,
    size_t brake_accelerate_table_count)
{
    const int16_t *table;
    uint32_t index;
    uint32_t acceleration_bits;

    if (state == NULL || brake_table == NULL ||
        brake_accelerate_table == NULL)
        return PORTABLE_DRIVER_BRAKE_INVALID_ARGUMENT;
    if (brake_table_count < 8u || brake_accelerate_table_count < 8u)
        return PORTABLE_DRIVER_BRAKE_TABLE_TOO_SMALL;
    if ((state->control_word & UINT16_C(0x4000)) == 0)
        return PORTABLE_DRIVER_BRAKE_UNSUPPORTED_CONTROL;

    table = (state->control_word & UINT16_C(0x8000)) != 0
                ? brake_accelerate_table
                : brake_table;
    index = ((uint32_t)state->speed_q16 >> 24) & 7u;
    acceleration_bits = (uint32_t)(uint16_t)state->acceleration_q16;
    acceleration_bits |=
        (uint32_t)(uint16_t)table[index] << 16;
    state->acceleration_q16 = signed32_from_bits(acceleration_bits);
    return PORTABLE_DRIVER_BRAKE_OK;
}
