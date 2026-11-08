#ifndef MKSC_PORTABLE_DRIVER_ACCELERATION_H
#define MKSC_PORTABLE_DRIVER_ACCELERATION_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_DRIVER_ACCELERATION_OK = 0,
    PORTABLE_DRIVER_ACCELERATION_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_ACCELERATION_TABLE_TOO_SMALL = 2,
    PORTABLE_DRIVER_ACCELERATION_UNSUPPORTED_MODE = 3,
    PORTABLE_DRIVER_ACCELERATION_UNSUPPORTED_CONTROL = 4,
};

/*
 * This is the bounded normal-mode slice of sub_8038710. The comments name
 * original driver-record offsets; the native object contains no original
 * pointer or padding contract.
 */
typedef struct
{
    int32_t speed_q16;          /* original +0xbc */
    int32_t acceleration_q16;   /* original +0xc0 */
    uint16_t control_word;      /* original +0x96 */
    uint8_t driver_mode;        /* original +0x7e; accepted values 0..4 */
    uint8_t character_class;    /* original +0x4c; only the low 3 bits index */
} PortableDriverAccelerationState;

/*
 * Apply the source's normal-mode auto-acceleration table. The original ROM
 * table is 64 signed 16-bit entries and remains a caller-owned dependency.
 * A/B control branches and modes 5..15 are intentionally outside this seam.
 */
int portable_driver_acceleration_tick(
    PortableDriverAccelerationState *state,
    const int16_t *auto_acceleration_table,
    size_t table_count);

#endif
