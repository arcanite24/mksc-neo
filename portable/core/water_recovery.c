#include "water_recovery.h"

#include <stddef.h>

static int32_t interpolate_q16(int32_t from, int32_t to,
                               uint16_t step, uint16_t count)
{
    const int64_t delta = (int64_t)to - from;

    return (int32_t)((int64_t)from + delta * step / count);
}

static void select_phase(PortableWaterRecoveryState *state)
{
    uint16_t tick = (uint16_t)(state->elapsed_ticks - 1u);

    if (tick < PORTABLE_WATER_RECOVERY_SINK_TICKS)
    {
        state->phase = PORTABLE_WATER_RECOVERY_SINK;
        state->phase_ticks = tick;
    }
    else if ((tick -= PORTABLE_WATER_RECOVERY_SINK_TICKS) <
             PORTABLE_WATER_RECOVERY_LIFT_TICKS)
    {
        state->phase = PORTABLE_WATER_RECOVERY_LIFT;
        state->phase_ticks = tick;
    }
    else if ((tick -= PORTABLE_WATER_RECOVERY_LIFT_TICKS) <
             PORTABLE_WATER_RECOVERY_CARRY_TICKS)
    {
        state->phase = PORTABLE_WATER_RECOVERY_CARRY;
        state->phase_ticks = tick;
    }
    else
    {
        tick = (uint16_t)(tick - PORTABLE_WATER_RECOVERY_CARRY_TICKS);
        state->phase = PORTABLE_WATER_RECOVERY_LOWER;
        state->phase_ticks = tick;
    }
}

uint16_t portable_water_recovery_approach_heading(uint16_t current,
                                                  uint16_t target)
{
    const uint16_t delta = (uint16_t)(current - target);

    if (delta == 0u)
        return current;
    if ((int16_t)delta > 0)
    {
        if (delta <= UINT16_C(0x01ff))
            return target;
        return (uint16_t)(current - UINT16_C(0x0140));
    }
    if (delta > UINT16_C(0xfdff))
        return target;
    return (uint16_t)(current + UINT16_C(0x0140));
}

int portable_water_recovery_begin(PortableWaterRecoveryState *state,
                                  int32_t entry_x_q16,
                                  int32_t entry_y_q16,
                                  int32_t target_x_q16,
                                  int32_t target_y_q16,
                                  uint16_t target_heading)
{
    if (state == NULL || state->phase != PORTABLE_WATER_RECOVERY_NONE)
        return 0;
    *state = (PortableWaterRecoveryState){
        .phase = PORTABLE_WATER_RECOVERY_SINK,
        .elapsed_ticks = 1u,
        .phase_ticks = 0u,
        .entry_x_q16 = entry_x_q16,
        .entry_y_q16 = entry_y_q16,
        .target_x_q16 = target_x_q16,
        .target_y_q16 = target_y_q16,
        .target_heading = target_heading,
    };
    return 1;
}

int portable_water_recovery_tick(PortableWaterRecoveryState *state,
                                 int32_t *position_x_q16,
                                 int32_t *position_y_q16,
                                 uint16_t *heading)
{
    uint16_t carry_step;

    if (state == NULL || position_x_q16 == NULL || position_y_q16 == NULL ||
        heading == NULL || state->phase == PORTABLE_WATER_RECOVERY_NONE)
        return 0;
    ++state->elapsed_ticks;
    if (state->elapsed_ticks >= PORTABLE_WATER_RECOVERY_TOTAL_TICKS)
    {
        *position_x_q16 = state->target_x_q16;
        *position_y_q16 = state->target_y_q16;
        *heading = state->target_heading;
        state->phase = PORTABLE_WATER_RECOVERY_NONE;
        state->phase_ticks = 0u;
        return 0;
    }
    select_phase(state);
    if (state->phase == PORTABLE_WATER_RECOVERY_CARRY)
    {
        carry_step = (uint16_t)(state->phase_ticks + 1u);
        *position_x_q16 = interpolate_q16(
            state->entry_x_q16, state->target_x_q16, carry_step,
            PORTABLE_WATER_RECOVERY_CARRY_TICKS);
        *position_y_q16 = interpolate_q16(
            state->entry_y_q16, state->target_y_q16, carry_step,
            PORTABLE_WATER_RECOVERY_CARRY_TICKS);
        /* The source enters mode 6 with its prior heading intact; the first
         * sub_8039894 approach happens on the following mode-6 frame. */
        if (state->phase_ticks != 0u)
            *heading = portable_water_recovery_approach_heading(
                *heading, state->target_heading);
    }
    else if (state->phase == PORTABLE_WATER_RECOVERY_LOWER)
    {
        *position_x_q16 = state->target_x_q16;
        *position_y_q16 = state->target_y_q16;
        *heading = state->target_heading;
    }
    return 1;
}
