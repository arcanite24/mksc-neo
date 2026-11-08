#ifndef MKSC_PORTABLE_DRIVER_BRAKE_H
#define MKSC_PORTABLE_DRIVER_BRAKE_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_DRIVER_BRAKE_OK = 0,
    PORTABLE_DRIVER_BRAKE_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_BRAKE_TABLE_TOO_SMALL = 2,
    PORTABLE_DRIVER_BRAKE_UNSUPPORTED_CONTROL = 3,
};

typedef struct
{
    int32_t speed_q16;       /* original +0xbc */
    int32_t acceleration_q16; /* original +0xc0 */
    uint16_t control_word;   /* original +0x96 */
} PortableDriverBrakeState;

/* Reproduce the B branch of sub_8038710, preserving +0xc0's low half. */
int portable_driver_brake_acceleration_tick(
    PortableDriverBrakeState *state,
    const int16_t *brake_table,
    size_t brake_table_count,
    const int16_t *brake_accelerate_table,
    size_t brake_accelerate_table_count);

#endif
