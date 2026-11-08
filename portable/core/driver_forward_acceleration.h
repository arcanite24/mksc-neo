#ifndef MKSC_PORTABLE_DRIVER_FORWARD_ACCELERATION_H
#define MKSC_PORTABLE_DRIVER_FORWARD_ACCELERATION_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_DRIVER_FORWARD_ACCELERATION_OK = 0,
    PORTABLE_DRIVER_FORWARD_ACCELERATION_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_FORWARD_ACCELERATION_GLOBAL_TABLE_TOO_SMALL = 2,
    PORTABLE_DRIVER_FORWARD_ACCELERATION_LOCAL_TABLE_TOO_SMALL = 3,
};

/*
 * This is the bounded source helper sub_8038640. The comments identify the
 * original driver-record fields; the native state has no ROM pointer or
 * padding contract.
 */
typedef struct
{
    int32_t speed_q16;          /* original +0xbc */
    int16_t response_q8;        /* original +0x88 */
    uint16_t state_flags;       /* original +0xb4; bit 0 selects the fixed path */
    int32_t acceleration_q16;   /* original +0xc0 */
} PortableDriverForwardAccelerationState;

/*
 * Apply the source's speed-indexed response update and acceleration table.
 * Both tables remain caller-owned ROM/data dependencies. The global curve has
 * eight signed entries; the per-driver acceleration table has sixteen.
 */
int portable_driver_forward_acceleration_tick(
    PortableDriverForwardAccelerationState *state,
    const int16_t *global_response_curve,
    size_t global_curve_count,
    const int16_t *local_acceleration_table,
    size_t local_table_count);

#endif
