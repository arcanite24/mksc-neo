#ifndef MKSC_PORTABLE_DRIVER_B550_RESULTANT_H
#define MKSC_PORTABLE_DRIVER_B550_RESULTANT_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_DRIVER_B550_RESULTANT_OK = 0,
    PORTABLE_DRIVER_B550_RESULTANT_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_B550_RESULTANT_DIRECTION_OUT_OF_RANGE = 2,
};

enum
{
    PORTABLE_DRIVER_B550_BRANCH_TABLE = 0,
    PORTABLE_DRIVER_B550_BRANCH_QUARTER = 1,
    PORTABLE_DRIVER_B550_BRANCH_CLAMP = 2,
};

typedef struct
{
    uint8_t branch;
    uint8_t direction_index;
} PortableDriverB550Resultant;

/*
 * Apply the normal +0x8000 arm of sub_803B550 to the already-written
 * +0x4e/+0x50 components.  This keeps the original signed halfword widths,
 * source arithmetic shifts and the caller-owned +0x5c direction index
 * explicit.  It does not schedule the handler or mutate state+0x60.
 */
int portable_driver_b550_normal_resultant(
    int16_t component_x,
    int16_t component_y,
    int32_t speed_q16,
    uint16_t source_direction,
    const int16_t *x_table,
    size_t x_count,
    const int16_t *y_table,
    size_t y_count,
    int16_t *out_x,
    int16_t *out_y,
    PortableDriverB550Resultant *out);

/* The original B550 tail owns +0x48 bit 0x4000 and two distinct unsigned
 * countdowns. Keep them separate from response timers +0x7e/+0x84. */
typedef struct
{
    uint32_t flags;
    uint16_t timer_38;
    uint16_t timer_62;
} PortableDriverB550Flags;

/* Run after the +0x60 scheduler update, before C4C0 tests motion suppression.
 * normal_scheduled corresponds to B5B8's 0x1000 OR, not a guessed flag seed. */
int portable_driver_b550_finish_flags(
    uint16_t next_scheduler_state,
    uint8_t normal_scheduled,
    PortableDriverB550Flags *state);

#endif
