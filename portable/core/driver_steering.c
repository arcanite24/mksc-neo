#include "driver_steering.h"

#include <stdint.h>


int portable_driver_response_row_select(PortableDriverSteeringState *state,
                                        uint16_t control_word,const PortableDriverResponseRules *rules)
{
    int64_t row;

    if (state == NULL || rules == NULL)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    if ((state->state_flags & UINT32_C(0x20)) != 0u)
    {
        state->response_slot = 2u;
        state->response_row = 0u;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if ((control_word & UINT16_C(0x4010)) != 0u)
    {
        state->response_slot = 2u;
        state->response_row = 7u;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if ((state->response_flags & UINT16_C(0x0002)) != 0u)
    {
        state->response_slot = 1u;
        state->response_row = 1u;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if (state->response_selector_index >= 16u)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;

    state->response_slot = 0u;
    row = (int64_t)rules->selector_rows[state->response_selector_index] +
        rules->character_offsets[
            state->response_selector_flags & UINT8_C(7)];
    /* The source stores through a byte and maps negative table sums to zero
     * by testing the stored high bit. */
    if(row>7)return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    state->response_row = row < 0 ? 0u : (uint8_t)row;
    return PORTABLE_DRIVER_STEERING_OK;
}

static int16_t signed16_from_bits(uint16_t bits)
{
    if (bits < UINT16_C(0x8000))
        return (int16_t)bits;
    return (int16_t)(-((int32_t)(UINT16_C(0x10000) - bits)));
}

static int16_t steering_response_delta(int16_t response_q8)
{
    int32_t value = response_q8;

    if (value < 0)
        value += 7;
    /* sub_8038A08 shifts the adjusted signed value left by 13 and then
     * performs a logical right shift by 16. Keep the low-halfword result
     * explicit so the small negative cases retain the source rounding. */
    return signed16_from_bits((uint16_t)(((uint32_t)value << 13) >> 16));
}

static int16_t signed16_from_value(int32_t value)
{
    return signed16_from_bits((uint16_t)value);
}

int portable_driver_steering_response_tick(
    PortableDriverSteeringState *state,
    const int16_t *response_data,
    size_t response_data_count)
{
    int32_t response;
    int32_t next_response;

    if (state == NULL || response_data == NULL)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    if (response_data_count < 4u)
        return PORTABLE_DRIVER_STEERING_RESPONSE_TABLE_TOO_SMALL;

    if ((state->state_flags & UINT32_C(0x80)) != 0)
    {
        state->response_q8 = 0;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if ((state->response_flags & UINT16_C(0x0600)) == UINT16_C(0x0200))
        return PORTABLE_DRIVER_STEERING_OK;
    if ((uint8_t)(state->response_status - 5u) <= 3u ||
        state->response_status == 0x0du)
        return PORTABLE_DRIVER_STEERING_OK;

    response = state->response_q8;
    if ((state->control_word & UINT16_C(0x0200)) != 0)
    {
        if (response <= 0)
        {
            if (-response < response_data[0])
                next_response = response - response_data[2];
            else
                next_response = -response_data[0];
        }
        else
        {
            next_response = response - response_data[1];
        }
    }
    else if ((state->control_word & UINT16_C(0x0100)) != 0)
    {
        if (response >= 0)
        {
            if (response < response_data[0])
                next_response = response + response_data[2];
            else
                next_response = response_data[0];
        }
        else
        {
            next_response = response + response_data[1];
        }
    }
    else if (response >= 0)
    {
        next_response = response - response_data[3];
        if (next_response < 0)
            next_response = 0;
    }
    else
    {
        next_response = response + response_data[3];
        if (next_response > 0)
            next_response = 0;
    }

    state->response_q8 = signed16_from_value(next_response);
    return PORTABLE_DRIVER_STEERING_OK;
}

int portable_driver_steering_tick(
    PortableDriverSteeringState *state,
    const int16_t *steering_delta_table,
    size_t steering_delta_count)
{
    uint32_t absolute_speed;
    int32_t delta = 0;
    uint32_t speed_class;

    if (state == NULL || steering_delta_table == NULL)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    if (steering_delta_count < 10u)
        return PORTABLE_DRIVER_STEERING_TABLE_TOO_SMALL;

    if (state->speed_q16 < 0)
        absolute_speed = 0u - (uint32_t)state->speed_q16;
    else
        absolute_speed = (uint32_t)state->speed_q16;

    if (absolute_speed > INT32_C(0x006fffff) ||
        (state->response_flags & UINT16_C(0x8000)) != 0)
    {
        delta = steering_response_delta(state->response_q8);
    }
    else
    {
        if (state->mode != 4u)
        {
            state->response_q8 = 0;
            speed_class = (uint32_t)absolute_speed >> 20;
        }
        else
        {
            speed_class = 8u;
        }
        if (speed_class >= steering_delta_count)
            speed_class = (uint32_t)(steering_delta_count - 1u);
        if ((state->state_flags & UINT32_C(0x2000)) == 0)
        {
            if ((state->control_word & UINT16_C(0x0200)) != 0)
                delta = -(int32_t)steering_delta_table[speed_class];
            else if ((state->control_word & UINT16_C(0x0100)) != 0)
                delta = steering_delta_table[speed_class];
        }
    }

    if (state->speed_q16 < 0)
        delta = -delta;
    state->steering_offset = signed16_from_bits(
        (uint16_t)((uint32_t)(uint16_t)state->steering_offset +
                   (uint32_t)(uint16_t)delta));
    if ((state->state_flags & UINT32_C(0x2000)) == 0)
    {
        state->angle = signed16_from_bits(
            (uint16_t)((uint32_t)(uint16_t)state->base_angle +
                       (uint32_t)(uint16_t)state->steering_offset));
        state->source_angle_56 = signed16_from_bits(
            (uint16_t)((uint32_t)(uint16_t)state->steering_offset -
                       (uint32_t)state->position_82));
    }
    return PORTABLE_DRIVER_STEERING_OK;
}

/* A9AC: 0x200 and 0x800 have priority over ordinary FF80 hits. */
int portable_driver_queued_hit(PortableDriverSteeringState *state)
{
    if (state == NULL) return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    if (state->response_flags & UINT16_C(0x0200))
    {
        state->response_status = 13u;
        state->driver_mode = 10u;
        state->response_q8 = 0;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if (!(state->response_flags & UINT16_C(0x1000)) ||
        (state->response_flags & UINT16_C(0x0a00)))
        return PORTABLE_DRIVER_STEERING_OK;
    if (state->speed_q16 > INT32_C(0x03800000))
        state->speed_q16 = INT32_C(0x03800000);
    state->driver_mode = 0u;
    state->drift_angle = 60u;
    state->response_status = signed16_from_bits(state->position_82) < 0 ? 5u : 6u;
    state->base_angle = 0;
    state->response_q8 = 0;
    state->spin_delay = 60u;
    state->response_flags &= UINT16_C(0xe7ff);
    return PORTABLE_DRIVER_STEERING_OK;
}

int portable_driver_strong_spin_tick(PortableDriverSteeringState *state,
                                     uint16_t *spin_rate)
{
    if (state == NULL || spin_rate == NULL)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    if (state->response_status != 13u)
        return PORTABLE_DRIVER_STEERING_OK;
    /* 3956C: signed B6 caps at 0x80 << 5, with explicit halfword wrap. */
    const int32_t rate = signed16_from_bits(*spin_rate);
    const int32_t step = rate > 0x0fff ? 0x1000 : rate;
    const int32_t direction = state->response_flags & UINT16_C(0x0100) ? 1 : -1;
    state->position_82 = (uint16_t)((int32_t)state->position_82 + direction * step);
    if (rate > 0xff)
    {
        *spin_rate = (uint16_t)(rate - 0x40);
        return PORTABLE_DRIVER_STEERING_OK;
    }
    const int32_t position = signed16_from_bits(state->position_82);
    if (position > 0x017f)
        state->position_82 = (uint16_t)(position - 0x0480);
    else if (position <= -0x0180)
        state->position_82 = (uint16_t)(position + 0x0480);
    else
    {
        state->driver_mode = 0u;
        state->position_82 = 0u;
        state->base_angle = 0;
        state->response_status = 0u;
        state->response_flags = (state->response_flags & UINT16_C(0x40ff)) |
                                UINT16_C(0x0400);
    }
    return PORTABLE_DRIVER_STEERING_OK;
}

int portable_driver_surface_spin(PortableDriverSteeringState *state,
                                uint8_t ground_mode, uint8_t material)
{
    if (state == NULL) return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    const uint8_t kind = material & UINT8_C(0x0f);
    if (ground_mode != 0u || material > UINT8_C(0x1f) ||
        (kind != 8u && kind != 12u) ||
        (kind == 12u && (state->state_flags & UINT32_C(0x2000))))
        return PORTABLE_DRIVER_STEERING_OK;
    if (!(state->response_flags & UINT16_C(2)) &&
        state->speed_q16 > INT32_C(0x02ffffff))
    {
        state->response_status = signed16_from_bits(state->position_82) < 0 ? 7u : 8u;
        state->base_angle = 0;
        state->response_q8 = 0;
        if (state->spin_delay == 0u) state->spin_delay = 60u;
    }
    state->response_flags &= UINT16_C(0xe7ff);
    return PORTABLE_DRIVER_STEERING_OK;
}

/* sub_8038BF4: an expired/speed-cancelled delay falls through to spin;
 * braking instead consumes the turn after selecting response 14. */
static int spin_delay_tick(PortableDriverSteeringState *state)
{
    if (state->spin_delay == 0u) return 0;
    state->spin_delay = (uint16_t)(state->spin_delay - 1u);
    state->spin_delay_cue = 0u;
    int32_t delay = signed16_from_bits(state->spin_delay);
    if ((delay == 59 && (uint16_t)(state->position_82 + UINT16_C(0x12ff)) >
                        UINT16_C(0x25fe)) ||
        state->speed_q16 <= INT32_C(0x02800000))
        state->spin_delay = 0u;
    if (!(state->state_flags & UINT32_C(0x20)) &&
        (state->control_word & UINT16_C(0x4300)) == UINT16_C(0x4000))
    {
        state->spin_delay = 0u;
        state->response_status = 14u;
        state->position_82 = 0u;
        state->spin_delay_cue = 165u;
        return 1;
    }
    delay = signed16_from_bits(state->spin_delay);
    if (delay == 0)
    {
        state->spin_delay_cue = 162u;
        return 0;
    }
    int32_t step = 0;
    if (delay > 55) step = 0x600;
    else if (delay > 47) step = -0x600;
    else if (delay > 39) step = 0x600;
    else if (delay > 31) step = -0x600;
    else if (delay > 23) step = 0x600;
    else if (delay > 15)
    {
        if (signed16_from_bits(state->position_82) <= 0)
            state->position_82 = 0u;
        else step = -0x600;
    }
    state->position_82 = (uint16_t)(state->position_82 + step);
    return 1;
}

static int approach_charged_exit(PortableDriverSteeringState *state,
    int32_t speed_target_q16, const int16_t response_row_data[8],
    int is_player, int is_primary_effect_driver,
    PortableDriverChargedExitEvent *event);

int portable_driver_source_left_turn_response_tick(
    PortableDriverSteeringState *state, int32_t speed_target_q16,
    const PortableDriverResponseRules *rules)
{
    PortableDriverChargedExitEvent event;
    return portable_driver_source_turn_response_tick(state, speed_target_q16,rules, &event);
}

int portable_driver_source_turn_response_tick(
    PortableDriverSteeringState *state, int32_t speed_target_q16,
    const PortableDriverResponseRules *rules,PortableDriverChargedExitEvent *event)
{
    const int16_t *row;
    int32_t angle;
    int32_t position;
    int positive_response;

    if (state == NULL || event == NULL || rules == NULL ||
        state->response_row >= PORTABLE_DRIVER_RESPONSE_ROW_COUNT)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    *event = (PortableDriverChargedExitEvent){0};
    if ((state->response_flags & UINT16_C(1)) == 0u)
        state->response_flags &= UINT16_C(0xffd3);
    if (state->response_status == 5u || state->response_status == 6u)
    {
        /* 8E34 cases5/6: wobble B8 first, then C8's mandatory full spins,
         * then a low-speed crossing through zero. Halfword wrap is deliberate. */
        state->response_flags |= UINT16_C(8);
        if (spin_delay_tick(state)) return PORTABLE_DRIVER_STEERING_OK;
        const int left = state->response_status == 5u;
        if (state->response_flags & UINT16_C(2))
        {
            state->response_flags |= UINT16_C(0x400);
            state->response_status = 14u;
            return PORTABLE_DRIVER_STEERING_OK;
        }
        const uint16_t prior = state->position_82;
        state->position_82 = (uint16_t)(prior + (left ? -0xa00 : 0xa00));
        if (signed16_from_bits(state->drift_angle) != 0)
            --state->drift_angle;
        else if (state->speed_q16 <= INT32_C(0x02ffffff) &&
                 (left ? prior <= UINT16_C(0x09ff) :
                         state->position_82 <= UINT16_C(0x09ff)))
        {
            state->response_flags |= UINT16_C(0x400);
            state->response_status = 14u;
        }
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if (state->response_status == 7u || state->response_status == 8u)
    {
        state->response_flags |= UINT16_C(8);
        if (spin_delay_tick(state)) return PORTABLE_DRIVER_STEERING_OK;
        /* A delay that ended still reaches the ordinary spin branch. */
        if (state->response_flags & UINT16_C(2))
            state->response_status = 14u;
        else
        {
            const uint16_t prior = state->position_82;
            const int left = state->response_status == 7u;
            state->position_82 = (uint16_t)(prior + (left ? -0x480 : 0x480));
            if (state->speed_q16 <= INT32_C(0x017fffff) &&
                (left ? prior <= UINT16_C(0x047f) : prior > UINT16_C(0xfb80)))
                state->response_status = 14u;
        }
        return PORTABLE_DRIVER_STEERING_OK;
    }
    /* 38EC6/38F96 cases0..4 enter the low-speed A+B response before row/speed gates. */
    if (state->response_status <= 4u &&
        (state->control_word & UINT16_C(0xc000)) == UINT16_C(0xc000) &&
        state->speed_q16 <= INT32_C(0x01000000) && state->driver_mode != 4u)
    {
        state->response_status = 15u;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if (state->response_status == 15u)
    {
        /* 39636: release drops88 immediately; held steering selects slot3.
         * Its9690 relaxation always uses row0 and retains response15. */
        if ((state->control_word & UINT16_C(0xc000)) != UINT16_C(0xc000))
        {
            state->response_status = 0u;
            state->response_q8 = 0;
            return PORTABLE_DRIVER_STEERING_OK;
        }
        if ((state->control_word & UINT16_C(0x0300)) != 0u)
            state->response_slot = 3u;
        else
            state->response_q8 = 0;
        const int result = approach_charged_exit(state, speed_target_q16,
            rules->rows[0], 1, 1, event);
        state->response_status = 15u;
        return result;
    }
    /* 8E34 case 1 checks +0x54 before its speed/turn branches. Beach
     * frame1897 selects row zero and enters14 even with neutral controls. */
    if (state->response_status == 1u && state->response_row == 0u &&
        (state->control_word & UINT16_C(0xc000)) != UINT16_C(0xc000))
    {
        state->response_status = 14u;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    row = rules->rows[state->response_row];
    if (state->response_status == 0u)
    {
        /* 38EF4 clears C8 even when the low-speed branch goes directly
         * to9690. Relaxation is not gated by the status1 turn threshold. */
        state->drift_angle = 0u;
        if (!state->time_trial_mode && state->speed_q16 <= INT32_C(0x00ffffff))
            return approach_charged_exit(state, speed_target_q16, row, 1, 1, event);
        const int64_t delta =
            (int64_t)state->speed_q16 - speed_target_q16 +
            (state->time_trial_mode ? INT32_C(0x01200000) : 0);
        const uint16_t response_gate = (uint16_t)(
            UINT16_C(0x0300) + (uint16_t)state->response_q8);

        /* Source 8E34 case 0 arms at/above +0xa8. Below target it also
         * requires row[0]'s speed margin and the unsigned +0x88 gate
         * (0x300 + response) > 0x5ff after halfword wrap. The straight
         * frame 1899 has response zero and must stay in status 0; the
         * A+Left frame 1877 has response 0xf63a and enters status 1. */
        if (delta >= 0 ||
            (delta >= (int64_t)row[0] * INT64_C(0x10000) &&
             response_gate > UINT16_C(0x05ff)))
        {
            state->response_status = 1u;
            state->drift_angle = 0u;
        }
        else
        {
            return approach_charged_exit(state, speed_target_q16, row, 1, 1, event);
        }
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if (state->response_status != 1u)
        return PORTABLE_DRIVER_STEERING_OK;
    /* 38FD6 ->3955E retires status1 below256, including sinking mode4.
     * The following response dispatch owns angle relaxation. */
    if (state->speed_q16 <= INT32_C(0x00ffffff))
    {
        state->response_status = 14u;
        return PORTABLE_DRIVER_STEERING_OK;
    }
    /* 38FE4: without A, only R above 448 keeps the turn response
     * active. B+steering follows 39192's neutral relaxation. */
    if (((state->control_word & UINT16_C(0x8000)) == 0u &&
         ((state->control_word & UINT16_C(0x0010)) == 0u ||
          state->speed_q16 <= INT32_C(0x01bfffff))) ||
        (state->control_word & UINT16_C(0x0300)) == 0u)
        return portable_driver_charged_steering_exit_tick(
            state, speed_target_q16, row, 1, 1, event);
    /* 3900E tests Left first when both direction bits are held. */
    if ((state->control_word & UINT16_C(0x0200)) == 0u)
    {
        /* sub_8038E34 case 1 takes the right-control branch through
         * sub_803DBD0. The frame-3059 source call starts at zero and
         * writes +0x82=0xfeb0, +0x80=0xff00, +0xc8=0xfe20. */
        const int32_t prior_position = signed16_from_bits(state->position_82);
        const int32_t prior_angle = state->base_angle;
        int32_t position_step = row[6];
        int32_t right_angle;

        if (prior_position < 0)
        {
            uint16_t flags = state->response_flags & UINT16_C(0xffda);
            if ((uint16_t)(state->position_82 + UINT16_C(0x0c00)) >
                UINT16_C(0x17ff))
                flags |= UINT16_C(0x0004);
            if ((uint16_t)(state->position_82 + UINT16_C(0x1800)) >
                UINT16_C(0x2fff))
                flags |= UINT16_C(0x0020);
            state->response_flags = flags;
            if (-prior_position > row[7])
                position_step = -position_step;
        }
        state->position_82 = (uint16_t)(prior_position - position_step);
        if (prior_angle < 0)
        {
            state->drift_angle = (uint16_t)(state->drift_angle + row[1]);
            right_angle = prior_angle - row[3];
            if (signed16_from_bits(state->drift_angle) > INT16_C(0x79ff))
            {
                state->response_status = 7u;
                state->response_q8 = 0;
                state->response_flags &= UINT16_C(0xffbf);
            }
        }
        else
        {
            state->drift_angle = (uint16_t)(
                state->drift_angle - UINT16_C(0x00e0) - (uint16_t)row[1]);
            right_angle = prior_angle - row[5];
        }
        if (right_angle > row[2]) right_angle = row[2];
        if (right_angle < -row[2]) right_angle = -row[2];
        state->base_angle = signed16_from_value(right_angle);
        return PORTABLE_DRIVER_STEERING_OK;
    }
    /* sub_803DB84 updates +0x82 before case 1 updates +0xc8/+0x80. */
    position = signed16_from_bits(state->position_82);
    if (position > 0)
    {
        uint16_t flags = state->response_flags & UINT16_C(0xffda);
        if ((uint16_t)(state->position_82 + UINT16_C(0x0c00)) >
            UINT16_C(0x17ff))
            flags |= UINT16_C(0x0004);
        if ((uint16_t)(state->position_82 + UINT16_C(0x1800)) >
            UINT16_C(0x2fff))
            flags |= UINT16_C(0x0020);
        state->response_flags = flags;
    }
    position += position > row[7] ? -(int32_t)row[6] : (int32_t)row[6];
    state->position_82 = (uint16_t)position;

    angle = state->base_angle;
    positive_response = angle >= 0;
    if (angle >= 0)
    {
        state->drift_angle = (uint16_t)(state->drift_angle - row[1]);
        angle += row[3];
    }
    else
    {
        state->drift_angle =
            (uint16_t)(state->drift_angle + UINT16_C(0x00e0) + row[1]);
        angle += row[5];
    }
    if (angle > row[2]) angle = row[2];
    if (angle < -row[2]) angle = -row[2];
    state->base_angle = signed16_from_value(angle);
    /* 8E34 case 1 enters status 8 after the positive-angle drift counter
     * crosses -0x7a00. The frame-2542 source call reaches 0x85a0 here. */
    if (positive_response &&
        signed16_from_bits(state->drift_angle) < -INT32_C(0x7a00))
    {
        state->response_status = 8u;
        state->response_q8 = 0;
        state->control_word &= UINT16_C(0xffbf);
    }
    return PORTABLE_DRIVER_STEERING_OK;
}

int portable_driver_collision_response_tick(PortableDriverSteeringState *state)
{
    if (state == NULL) return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    if (state->response_status != 11u) return PORTABLE_DRIVER_STEERING_OK;
    if (!(state->response_flags & UINT16_C(1)))
        state->response_flags &= UINT16_C(0xffd3);
    if (!(state->control_word & UINT16_C(0x8000)))
    {
        /* Released A keeps the impact angle until signed speed falls below
         * sixteen units; this branch does not approach +0x82. */
        if (state->speed_q16 > INT32_C(0x000fffff))
            return PORTABLE_DRIVER_STEERING_OK;
        state->base_angle = 0;
    }
    else
    {
        /* The low-speed A branch clears only +0x80. 8710/DAEC observes
         * that zero afterward and selects response 14 without rewriting C0. */
        if (state->speed_q16 <= INT32_C(0x00ffffff))
        {
            state->base_angle = 0;
            return PORTABLE_DRIVER_STEERING_OK;
        }
        int32_t position = signed16_from_bits(state->position_82);
        if (position < 0)
        {
            position += 0x400;
            if (position >= 0) position = 0;
        }
        else if (position > 0)
        {
            position -= 0x400;
            if (position < 0) position = 0;
        }
        state->position_82 = (uint16_t)position;
        int32_t angle = state->base_angle;
        if (angle < 0)
        {
            angle += 0x400;
            state->base_angle = (int16_t)(angle < 0 ? angle : 0);
            if (angle < 0) return PORTABLE_DRIVER_STEERING_OK;
        }
        else if (angle > 0)
        {
            angle -= 0x400;
            state->base_angle = (int16_t)(angle >= 0 ? angle : 0);
            /* The source positive branch retains status 11 at exact zero. */
            if (angle >= 0) return PORTABLE_DRIVER_STEERING_OK;
        }
    }
    state->response_status = 14u;
    state->driver_mode = 0u;
    return PORTABLE_DRIVER_STEERING_OK;
}

static uint16_t add_halfword(uint16_t left, int32_t right)
{
    return (uint16_t)((uint32_t)left + (uint32_t)right);
}

/* sub_8039690, shared by neutral drift release and status14's row-zero call. */
static int approach_charged_exit(PortableDriverSteeringState *state,
    int32_t speed_target_q16, const int16_t response_row_data[8],
    int is_player, int is_primary_effect_driver,
    PortableDriverChargedExitEvent *event)
{
    int16_t value, step, candidate;
    int32_t threshold;
    /* sub_8039690 first approaches +0x82 and clamps a crossing to zero. */
    value = signed16_from_bits(state->position_82);
    step = response_row_data[6];
    if (value > 0)
    {
        candidate = signed16_from_value((int32_t)value - step);
        state->position_82 = candidate < 0 ? 0u : (uint16_t)candidate;
    }
    else if (value < 0)
    {
        candidate = signed16_from_value((int32_t)value + step);
        state->position_82 = candidate > 0 ? 0u : (uint16_t)candidate;
    }

    /* The +0x80 approach returns before charged-exit handling while the
     * next step stays on the same side of zero. A crossing preserves the
     * last value and falls through to the charged flag consumer. */
    value = state->base_angle;
    step = response_row_data[4];
    if (value > 0)
    {
        candidate = signed16_from_value((int32_t)value - step);
        if (candidate >= 0)
        {
            state->base_angle = candidate;
            return PORTABLE_DRIVER_STEERING_OK;
        }
    }
    else if (value < 0)
    {
        candidate = signed16_from_value((int32_t)value + step);
        if (candidate <= 0)
        {
            state->base_angle = candidate;
            return PORTABLE_DRIVER_STEERING_OK;
        }
    }

    if ((state->response_flags & UINT16_C(0x0040)) == 0u)
    {
        state->response_status = 0u;
        return PORTABLE_DRIVER_STEERING_OK;
    }

    threshold = (int32_t)((uint32_t)speed_target_q16 + UINT32_C(0xff800000));
    if (state->speed_q16 < threshold || state->driver_mode == 8u)
    {
        if ((state->state_flags & UINT32_C(0x00000800)) == 0u)
            state->response_flags &= UINT16_C(0xffbf);
        return PORTABLE_DRIVER_STEERING_OK;
    }
    if ((state->state_flags & UINT32_C(0x00000800)) != 0u)
        return PORTABLE_DRIVER_STEERING_OK;

    event->audio_cue_140 = (uint8_t)(is_player != 0 &&
        (state->state_flags & UINT32_C(0x00000020)) == 0u);
    event->particle_effect = (uint8_t)(state->response_slot != 3u &&
        is_primary_effect_driver != 0);
    event->strong_exit = 1u;
    state->response_status = 14u;
    state->base_angle = 0;
    state->exit_timer = UINT16_C(0x0030);
    state->driver_mode = 12u;
    state->response_flags &= UINT16_C(0xffbf);
    return PORTABLE_DRIVER_STEERING_OK;
}

int portable_driver_charged_steering_exit_tick(
    PortableDriverSteeringState *state,
    int32_t speed_target_q16,
    const int16_t response_row_data[8],
    int is_player,
    int is_primary_effect_driver,
    PortableDriverChargedExitEvent *event)
{
    int16_t value;
    int16_t step;

    if (state == NULL || response_row_data == NULL || event == NULL)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    *event = (PortableDriverChargedExitEvent){0};
    if (state->response_status != 1u)
        return PORTABLE_DRIVER_STEERING_OK;

    /* sub_8038E34's common prologue clears bits 0x2c unless bit 0 is set. */
    if ((state->response_flags & UINT16_C(0x0001)) == 0u)
        state->response_flags &= UINT16_C(0xffd3);

    /* .L08039192 approaches +0xc8 without clamping through zero. */
    value = signed16_from_bits(state->drift_angle);
    if (value != 0)
    {
        step = (int16_t)((int32_t)response_row_data[1] + INT32_C(0x00e0));
        state->drift_angle = add_halfword(
            state->drift_angle, value < 0 ? step : -(int32_t)step);
    }

    return approach_charged_exit(state, speed_target_q16, response_row_data,
                                 is_player, is_primary_effect_driver, event);
}

int portable_driver_source_status14_tick(PortableDriverSteeringState *state,
    int32_t speed_target_q16, int is_player, int is_primary_effect_driver,
    const PortableDriverResponseRules *rules,PortableDriverChargedExitEvent *event)
{
    if (state == NULL || event == NULL || rules == NULL)
        return PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT;
    *event = (PortableDriverChargedExitEvent){0};
    if (state->response_status != 14u)
        return PORTABLE_DRIVER_STEERING_OK;
    if ((state->response_flags & UINT16_C(1)) == 0u)
        state->response_flags &= UINT16_C(0xffd3);
    const int result = approach_charged_exit(state, speed_target_q16,
        rules->rows[0], is_player, is_primary_effect_driver, event);
    if (result != PORTABLE_DRIVER_STEERING_OK)
        return result;
    /* 8E34 checks +80 again after9690, including its charged-exit branch.
     * Signed halfword addition preserves the asymmetric +/-0x4000 window. */
    if (state->base_angle == 0)
        state->response_status = 0u;
    else if (((uint16_t)((uint16_t)state->base_angle + UINT16_C(0x4000)) &
              UINT16_C(0x8000)) != 0u)
    {
        state->response_status = 11u;
        state->driver_mode = 11u;
    }
    return PORTABLE_DRIVER_STEERING_OK;
}
