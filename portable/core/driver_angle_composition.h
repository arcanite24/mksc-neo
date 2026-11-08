#ifndef MKSC_PORTABLE_DRIVER_ANGLE_COMPOSITION_H
#define MKSC_PORTABLE_DRIVER_ANGLE_COMPOSITION_H

#include <stddef.h>
#include <stdint.h>

#include "driver_angle_callbacks.h"

enum
{
    PORTABLE_DRIVER_ANGLE_COMPOSITION_OK = 0,
    PORTABLE_DRIVER_ANGLE_COMPOSITION_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_ANGLE_COMPOSITION_LIMIT_CALLBACK_ERROR = 2,
    PORTABLE_DRIVER_ANGLE_COMPOSITION_TARGET_CALLBACK_ERROR = 3,
};

enum
{
    PORTABLE_DRIVER_ANGLE_COMPOSITION_STATE_CALLBACK = 1,
    PORTABLE_DRIVER_ANGLE_COMPOSITION_EQUAL_CALLBACK = 2,
    PORTABLE_DRIVER_ANGLE_COMPOSITION_SNAP_CURRENT = 3,
    PORTABLE_DRIVER_ANGLE_COMPOSITION_RETARGET_FAR = 4,
};

/* Caller-owned views of the fields consumed by sub_803BC00 and its two
 * callback bodies. Pointers are used only for the call; no host pointer is
 * stored in a source-shaped state field. */
typedef struct
{
    uint16_t current_angle_56;
    uint16_t target_angle_7a;
    uint16_t source_flags_b4;
    uint16_t limit_b6;
    uint16_t position_82;
    uint16_t angle_delta_80;
    uint8_t timer_84;
    uint8_t response_status_7e;
    uint16_t target_angle_ca;
    uint8_t table_nibble_9a;
    const uint8_t *target_table;
    size_t target_table_count;
} PortableDriverAngleCompositionInput;

typedef struct
{
    uint16_t current_angle_56;
    uint16_t target_angle_7a;
    uint16_t source_flags_b4;
    uint16_t next_current_angle_56;
    uint16_t next_target_angle_7a;
    uint16_t next_flags_b4;
    uint16_t next_limit_b6;
    uint16_t next_position_82;
    uint16_t next_angle_delta_80;
    uint8_t next_timer_84;
    uint8_t next_response_status_7e;
    uint8_t branch;
    int callback_status;
    PortableDriverAngleLimitResult limit_callback;
    PortableDriverAngleTargetCallbackResult target_callback;
} PortableDriverAngleCompositionResult;

/* Compose one source-shaped sub_803BC00 call without mutating caller state.
 * The callback-owned fields are returned explicitly so a later owner can
 * apply them at the correct runtime boundary. */
int portable_driver_angle_compose(
    const PortableDriverAngleCompositionInput *input,
    PortableDriverAngleCompositionResult *out);

#endif
