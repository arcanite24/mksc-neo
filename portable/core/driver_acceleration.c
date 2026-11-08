#include "driver_acceleration.h"

#include <stdint.h>

static int32_t signed32_from_bits(uint32_t bits)
{
    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

int portable_driver_acceleration_tick(
    PortableDriverAccelerationState *state,
    const int16_t *auto_acceleration_table,
    size_t table_count)
{
    uint32_t speed_class;
    uint32_t character_class;
    uint32_t table_index;
    uint32_t acceleration_bits;

    if (state == NULL || auto_acceleration_table == NULL)
        return PORTABLE_DRIVER_ACCELERATION_INVALID_ARGUMENT;
    if (table_count < 64u)
        return PORTABLE_DRIVER_ACCELERATION_TABLE_TOO_SMALL;
    if (state->driver_mode > 4u)
        return PORTABLE_DRIVER_ACCELERATION_UNSUPPORTED_MODE;
    if ((state->control_word & UINT16_C(0xC000)) != 0)
        return PORTABLE_DRIVER_ACCELERATION_UNSUPPORTED_CONTROL;

    /* sub_8038710 uses signed byte (+0xbf) then masks it to three bits. */
    speed_class = ((uint32_t)state->speed_q16 >> 24) & 7u;
    character_class = (uint32_t)state->character_class & 7u;
    table_index = character_class * 8u + speed_class;

    /* ldrsh/lsl #16 replaces the high half and preserves +0xc0's low half. */
    acceleration_bits = (uint32_t)(uint16_t)state->acceleration_q16;
    acceleration_bits |=
        (uint32_t)(uint16_t)auto_acceleration_table[table_index] << 16;
    state->acceleration_q16 = signed32_from_bits(acceleration_bits);
    return PORTABLE_DRIVER_ACCELERATION_OK;
}
