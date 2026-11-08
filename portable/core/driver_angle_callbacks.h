#ifndef MKSC_PORTABLE_DRIVER_ANGLE_CALLBACKS_H
#define MKSC_PORTABLE_DRIVER_ANGLE_CALLBACKS_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_DRIVER_ANGLE_CALLBACK_OK = 0,
    PORTABLE_DRIVER_ANGLE_CALLBACK_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_ANGLE_CALLBACK_TABLE_TOO_SMALL = 2,
};

enum
{
    PORTABLE_DRIVER_ANGLE_LIMIT_POSITION_UPDATE = 1,
    PORTABLE_DRIVER_ANGLE_LIMIT_DECREMENT = 2,
    PORTABLE_DRIVER_ANGLE_LIMIT_BOUNDARY_RESET = 3,
};

enum
{
    PORTABLE_DRIVER_ANGLE_TARGET_SNAP = 1,
    PORTABLE_DRIVER_ANGLE_TARGET_TABLE_POSITIVE = 2,
    PORTABLE_DRIVER_ANGLE_TARGET_TABLE_NEGATIVE = 3,
};

enum
{
    PORTABLE_DRIVER_ANGLE_TARGET_TABLE_OFFSET = 0x120,
    PORTABLE_DRIVER_ANGLE_TARGET_TABLE_BYTES = 0x1a0,
};

typedef struct
{
    uint16_t source_flags_b4;
    uint16_t next_flags_b4;
    uint16_t limit_b6;
    uint16_t next_limit_b6;
    uint16_t position_82;
    uint16_t next_position_82;
    int32_t amount;
    int32_t position_after_amount;
    uint16_t next_angle_delta_80;
    uint8_t next_timer_84;
    uint8_t next_response_status_7e;
    uint8_t branch;
} PortableDriverAngleLimitResult;

/* Reproduce sub_803BA98 with caller-owned halfword fields. */
int portable_driver_angle_limit_callback(
    uint16_t limit_b6,
    uint16_t source_flags_b4,
    uint16_t position_82,
    PortableDriverAngleLimitResult *out);

typedef struct
{
    uint16_t target_angle_ca;
    uint16_t current_angle_7a;
    uint8_t table_nibble_9a;
    int16_t difference;
    uint8_t table_index;
    uint8_t table_value;
    uint16_t next_angle_7a;
    uint16_t next_angle_56;
    uint8_t branch;
} PortableDriverAngleTargetCallbackResult;

/* Reproduce sub_803BB48. The source table is caller-owned; its first usable
 * byte is the source table pointer plus 0x120. Near-angle branches do not
 * dereference the table. */
int portable_driver_angle_target_callback(
    uint16_t target_angle_ca,
    uint16_t current_angle_7a,
    uint8_t table_nibble_9a,
    const uint8_t *table,
    size_t table_count,
    PortableDriverAngleTargetCallbackResult *out);

#endif
