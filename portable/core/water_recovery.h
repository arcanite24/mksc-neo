#ifndef PORTABLE_WATER_RECOVERY_H
#define PORTABLE_WATER_RECOVERY_H

#include <stdint.h>

typedef enum
{
    PORTABLE_WATER_RECOVERY_NONE = 0,
    PORTABLE_WATER_RECOVERY_SINK,
    PORTABLE_WATER_RECOVERY_LIFT,
    PORTABLE_WATER_RECOVERY_CARRY,
    PORTABLE_WATER_RECOVERY_LOWER,
} PortableWaterRecoveryPhase;

enum
{
    /* Observed Shy Guy Beach source frames, inclusive of the entry frame. */
    PORTABLE_WATER_RECOVERY_SINK_TICKS = 163,
    PORTABLE_WATER_RECOVERY_LIFT_TICKS = 59,
    PORTABLE_WATER_RECOVERY_CARRY_TICKS = 58,
    PORTABLE_WATER_RECOVERY_LOWER_TICKS = 15,
    PORTABLE_WATER_RECOVERY_TOTAL_TICKS =
        PORTABLE_WATER_RECOVERY_SINK_TICKS +
        PORTABLE_WATER_RECOVERY_LIFT_TICKS +
        PORTABLE_WATER_RECOVERY_CARRY_TICKS +
        PORTABLE_WATER_RECOVERY_LOWER_TICKS,
};

typedef struct
{
    PortableWaterRecoveryPhase phase;
    uint16_t elapsed_ticks;
    uint16_t phase_ticks;
    int32_t entry_x_q16;
    int32_t entry_y_q16;
    int32_t target_x_q16;
    int32_t target_y_q16;
    uint16_t target_heading;
} PortableWaterRecoveryState;

int portable_water_recovery_begin(PortableWaterRecoveryState *state,
                                  int32_t entry_x_q16,
                                  int32_t entry_y_q16,
                                  int32_t target_x_q16,
                                  int32_t target_y_q16,
                                  uint16_t target_heading);

/* Advances one source recovery frame after the already-counted entry frame.
 * Returns one while recovery remains active and zero on the re-entry frame. */
int portable_water_recovery_tick(PortableWaterRecoveryState *state,
                                 int32_t *position_x_q16,
                                 int32_t *position_y_q16,
                                 uint16_t *heading);

/* Source sub_8039894: move a recovery heading toward its route target by
 * 0x140 with signed-16 wrap comparison and a 0x1ff snap window. */
uint16_t portable_water_recovery_approach_heading(uint16_t current,
                                                  uint16_t target);

#endif
