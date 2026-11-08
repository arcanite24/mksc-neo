#include "driver_hop.h"

#include <stddef.h>

static int16_t signed16_from_bits(uint16_t bits)
{
    if (bits < UINT16_C(0x8000))
        return (int16_t)bits;
    return (int16_t)(-((int32_t)UINT32_C(0x10000) - bits));
}

static int32_t height_q8(int32_t height_q16)
{
    if (height_q16 >= 0)
        return height_q16 / INT32_C(256);
    return (int32_t)(-((-(int64_t)height_q16 + INT64_C(255)) / INT64_C(256)));
}

static int32_t height_q16_from_q8(int32_t height)
{
    const uint32_t bits = (uint32_t)height << 8;
    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)((int64_t)bits - INT64_C(4294967296));
}

int portable_driver_hop_tick(PortableDriverHopState *state, uint16_t source_control_word)
{
    int32_t next_height_q8;

    if (state == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    if (state->mode > 1u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    state->landed = 0u;
    state->surface_jump_started = 0u;
    state->protected_hop_started = 0u;
    state->recovery_started = 0u;
    state->retrieval_started = 0u;
    state->recovery_touchdown = 0u;

    if (state->mode == 0u)
    {
        /* The source clears the prior airborne state on the normal-mode frame
         * after landing, then may arm a new hop from the R rising-edge bit. */
        state->state_flags &= ~UINT32_C(0x00000800);
        if ((source_control_word & UINT16_C(0x0004)) != 0u)
        {
            state->velocity_q8 = INT16_C(0x00e0);
            state->mode = 1u;
            state->animation_flags |= UINT16_C(0x8000);
            state->state_flags |= UINT32_C(0x00000800);
        }
        return PORTABLE_DRIVER_HOP_OK;
    }

    state->velocity_q8 =
        signed16_from_bits((uint16_t)((uint16_t)state->velocity_q8 - UINT16_C(0x001a)));
    next_height_q8 = height_q8(state->height_q16) + state->velocity_q8;
    if (next_height_q8 > 0)
    {
        state->height_q16 = height_q16_from_q8(next_height_q8);
        return PORTABLE_DRIVER_HOP_OK;
    }

    state->height_q16 = 0;
    state->velocity_q8 = 0;
    state->animation_flags &= UINT16_C(0x7fff);
    state->mode = 0u;
    state->landed = 1u;
    return PORTABLE_DRIVER_HOP_OK;
}

static void surface_hop_launch(PortableDriverHopState *state,
    int32_t *speed_q16, const int32_t *sine, unsigned index)
{
    const int32_t high = *speed_q16 / INT32_C(65536);
    const int32_t magnitude = high < 0 ? -high : high;
    state->velocity_q8 = signed16_from_bits(
        (uint16_t)((int64_t)sine[index] * magnitude / 32768));
    *speed_q16 = (int32_t)((int64_t)sine[(256u-index)&1023u] * high / 32768 * 65536 +
                          (uint16_t)*speed_q16);
    state->mode = 1u;
    state->animation_flags |= UINT16_C(0x8000);
}

int portable_driver_surface_jump(PortableDriverHopState *state,
    uint8_t mode_at_entry, uint8_t material, int32_t *speed_q16,
    int32_t ramp_speed_limit_q16, const int32_t *sine, size_t sine_count)
{
    if (state == NULL || speed_q16 == NULL || ramp_speed_limit_q16 < 0)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->surface_jump_started = 0u;
    if (mode_at_entry != 0u || material > UINT8_C(0x1f))
        return PORTABLE_DRIVER_HOP_OK;
    const unsigned kind = material & 15u;
    if (kind != 0u && kind != 2u && kind != 14u &&
        !(kind == 12u && (state->state_flags & UINT32_C(0x2000))))
        return PORTABLE_DRIVER_HOP_OK;
    if (kind == 0u || kind == 2u)
    {
        if (sine == NULL || sine_count < 1024u)
            return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
        if (kind == 0u)
        {
            /* A198 uses an unsigned positive-floor test, then a signed
             * negative floor. A1BC's course policy caps only positive speed. */
            if ((uint32_t)*speed_q16 <= UINT32_C(0x02dfffff))
                *speed_q16 = INT32_C(0x02e00000);
            else if (*speed_q16 < 0 && *speed_q16 > -INT32_C(0x02e00000))
                *speed_q16 = -INT32_C(0x02e00000);
            if (ramp_speed_limit_q16 != 0)
            {
                if (*speed_q16 > ramp_speed_limit_q16)
                    *speed_q16 = ramp_speed_limit_q16;
            }
            else if ((state->state_flags & UINT32_C(0x20)) &&
                     *speed_q16 <= INT32_C(0x057fffff))
                *speed_q16 = INT32_C(0x05800000);
        }
        else if (*speed_q16 <= INT32_C(0x017fffff))
            return PORTABLE_DRIVER_HOP_OK;
        surface_hop_launch(state, speed_q16, sine, kind == 0u ? 56u : 52u);
        if (kind == 0u)
            state->height_q16 = INT32_C(0x28000);
    }
    else if (kind == 12u)
    {
        if (*speed_q16 <= INT32_C(0x00ffffff))
            *speed_q16 = INT32_C(0x01000000);
        state->velocity_q8 = INT16_C(0x100);
        state->mode = 1u;
        state->animation_flags |= UINT16_C(0x8000);
    }
    else
    {
        if (*speed_q16 <= INT32_C(0x00ffffff))
            return PORTABLE_DRIVER_HOP_OK;
        state->velocity_q8 = INT16_C(0x80);
        state->height_q16 = INT32_C(0x100);
        state->mode = 1u;
        state->animation_flags |= UINT16_C(0x8000);
    }
    state->surface_jump_started = 1u;
    state->state_flags |= UINT32_C(0x800);
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_protected_surface_hop(PortableDriverHopState *state,
    uint8_t mode_at_entry,uint8_t material,uint8_t selector,int32_t *speed_q16,
    const uint16_t *thresholds,size_t threshold_count)
{
    if (state == NULL || speed_q16 == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->protected_hop_started=0u;
    if (mode_at_entry != 0u || material != UINT8_C(0x28) ||
        !(state->state_flags & UINT32_C(0x2000)))
        return PORTABLE_DRIVER_HOP_OK;
    if (selector > 15u || (selector > 9u &&
        (thresholds == NULL || threshold_count < 6u || thresholds[selector-10u] > 2016u)))
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    if (selector > 9u && *speed_q16 >= (int32_t)thresholds[selector-10u] * 65536)
        *speed_q16 -= INT32_C(0x00400000);
    state->protected_hop_started=1u;
    state->velocity_q8=INT16_C(0xe0);
    state->mode=1u;
    state->animation_flags=(state->animation_flags & UINT16_C(0x4000)) | UINT16_C(0x8000);
    state->state_flags|=UINT32_C(0x800);
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_terrain22_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, uint8_t material_flags, int32_t *speed_q16, uint16_t *timer_9c,
    const int32_t *sine, size_t sine_count)
{
    if (state == NULL || speed_q16 == NULL || timer_9c == NULL ||
        sine == NULL || sine_count < 1024u)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->recovery_started = 0u;
    state->retrieval_started = 0u;
    state->lakitu_arrived = 0u;
    if (state->lakitu_ticks != 0u && --state->lakitu_ticks == 0u)
        state->lakitu_arrived = 1u;
    if (mode_at_entry == 1u)
        return PORTABLE_DRIVER_HOP_OK;
    if (mode_at_entry == 4u)
    {
        state->landed = 0u;
        if (*timer_9c != 0u)
        {
            --*timer_9c;
            if (*timer_9c > 119u)
                return PORTABLE_DRIVER_HOP_OK;
            if (*timer_9c == 60u)
            {
                state->state_flags |= UINT32_C(0x100);
                state->recovery_requests |= UINT16_C(0x400);
                /* 50E78 starts F980 next turn; its sprite falls 2px from
                 * y=-16 to y=88, arriving 52 turns after this request. */
                state->lakitu_ticks = 52u;
            }
            if (material_flags != UINT8_C(0x24))
            {
                if (material_flags != UINT8_C(0x22) && material_flags < UINT8_C(0x80))
                {
                    state->state_flags &= ~UINT32_C(0x100);
                    state->animation_flags |= UINT16_C(0x10);
                    state->recovery_requests |= UINT16_C(0x200);
                    surface_hop_launch(state, speed_q16, sine, 248u);
                }
                return PORTABLE_DRIVER_HOP_OK;
            }
        }
        /* 99A4 follows8200 without replacing already generated components. */
        *speed_q16 = 0;
        state->state_flags = (state->state_flags & ~UINT32_C(0x100)) | UINT32_C(0x800);
        if (!(state->state_flags & UINT32_C(0x8000)))
            state->height_q16 = INT32_C(0x00300000);
        state->mode = 3u;
        state->recovery_requests |= UINT16_C(0x10);
        state->retrieval_started = 1u;
        return PORTABLE_DRIVER_HOP_OK;
    }
    if (mode_at_entry == 3u)
    {
        if (state->state_flags & UINT32_C(0x2000))
            state->height_q16 = INT32_C(0x70000);
        if (state->height_q16 > INT32_C(0x0006ffff))
            state->mode = 6u;
        *speed_q16 = 0;
        state->landed = 0u;
        return PORTABLE_DRIVER_HOP_OK;
    }
    if (mode_at_entry == 6u || mode_at_entry == 7u)
    {
        *speed_q16 = 0;
        state->landed = 0u;
        return PORTABLE_DRIVER_HOP_OK;
    }
    if (mode_at_entry != 0u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    state->animation_flags &= UINT16_C(0x4000);
    state->state_flags |= UINT32_C(0x800);
    if (*speed_q16 > INT32_C(0x01ffffff))
    {
        const int32_t adjusted = *speed_q16 -
            (*speed_q16 > INT32_C(0x04000000) ?
                 INT32_C(0x02c00000) : INT32_C(0x00a00000));
        *speed_q16 = adjusted;
        surface_hop_launch(state, speed_q16, sine, 32u);
        state->animation_flags |= UINT16_C(0x10);
    }
    else
    {
        *speed_q16 = 0;
        state->height_q16 = 0;
        state->velocity_q8 = 0;
        state->mode = 4u;
        state->recovery_started = 1u;
        *timer_9c = 144u;
        state->animation_flags &= UINT16_C(0x4002);
    }
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_fall_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, int32_t *speed_q16, uint16_t *timer_9c,
    uint16_t *contact34)
{
    if (state == NULL || speed_q16 == NULL || timer_9c == NULL || contact34 == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->landed = 0u;
    state->recovery_started = 0u;
    state->retrieval_started = 0u;
    if (mode_at_entry == 0u)
    {
        *speed_q16 = 0;
        *timer_9c = 60u;
        state->height_q16 = INT32_C(0x100);
        state->mode = 2u;
        state->state_flags |= UINT32_C(0x800);
        state->animation_flags &= UINT16_C(0x4000);
        *contact34 &= UINT16_C(0xfffd);
        state->recovery_started = 1u;
    }
    else if (mode_at_entry == 2u)
    {
        if (*timer_9c != 0u)
            --*timer_9c;
        else
        {
            state->height_q16 = INT32_C(0x00300000);
            *contact34 |= UINT16_C(2);
            state->mode = 6u;
            state->retrieval_started = 1u;
        }
    }
    else if (mode_at_entry != 6u && mode_at_entry != 7u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_bomb_respawn_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, uint16_t *timer_9c)
{
    if (state == NULL || timer_9c == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->landed = 0u;
    state->recovery_started = 0u;
    state->retrieval_started = 0u;
    if (mode_at_entry == 9u) {
        if (*timer_9c != 0u) --*timer_9c;
        else {
            state->height_q16 = INT32_C(0x300000);
            state->mode = 6u;
        }
    } else if (mode_at_entry != 6u && mode_at_entry != 7u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_lava_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, int32_t *speed_q16, uint16_t *flags74)
{
    if (state == NULL || speed_q16 == NULL || flags74 == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->landed = 0u;
    state->recovery_started = 0u;
    state->retrieval_started = 0u;
    *speed_q16 = 0;
    if (mode_at_entry == 0u)
    {
        if (!(state->state_flags & UINT32_C(0x8000)))
            state->height_q16 = INT32_C(0x300000);
        state->state_flags |= UINT32_C(0x800);
        state->mode = 3u;
        state->recovery_requests |= UINT16_C(0x10);
        *flags74 |= UINT16_C(0x2000);
        state->recovery_started = 1u;
        state->retrieval_started = 1u;
    }
    else if (mode_at_entry == 3u)
    {
        if (state->state_flags & UINT32_C(0x2000))
            state->height_q16 = INT32_C(0x70000);
        if (state->height_q16 > INT32_C(0x6ffff))
            state->mode = 6u;
    }
    else if (mode_at_entry != 6u && mode_at_entry != 7u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_bounds_tick(PortableDriverHopState *state,
    uint8_t mode_at_entry, int32_t *speed_q16)
{
    if (state == NULL || speed_q16 == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->landed = 0u;
    state->recovery_started = 0u;
    state->retrieval_started = 0u;
    *speed_q16 = 0;
    if (mode_at_entry == 0u)
    {
        state->mode = 5u;
        state->state_flags |= UINT32_C(0x800);
        state->recovery_requests |= UINT16_C(0x20);
        state->recovery_started = 1u;
        state->retrieval_started = 1u;
    }
    else if (mode_at_entry == 5u)
    {
        if (state->state_flags & UINT32_C(0x2000))
            state->height_q16 = INT32_C(0x70000);
        if (state->height_q16 > INT32_C(0x6ffff))
            state->mode = 6u;
    }
    else if (mode_at_entry != 6u && mode_at_entry != 7u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    return PORTABLE_DRIVER_HOP_OK;
}

static int32_t recovery_axis_step(int32_t value, int16_t target)
{
    const int16_t delta = signed16_from_bits(
        (uint16_t)(value / INT32_C(65536) - (uint16_t)target));
    uint32_t next;
    if (value < 0)
        next = (uint32_t)value + UINT32_C(0x30000);
    else if (delta >= -3 && delta <= 3)
        return (int32_t)target * INT32_C(65536);
    else
        next = (uint32_t)value +
            (delta < 0 ? UINT32_C(0x30000) : UINT32_C(0xfffd0000));
    return next < UINT32_C(0x80000000) ? (int32_t)next :
        (int32_t)((int64_t)next - INT64_C(0x100000000));
}

static uint16_t recovery_heading_step(uint16_t current, uint16_t target)
{
    /* ROM39894: cached source C has the positive branch and snap wrong. */
    const uint16_t delta = (uint16_t)(current - target);
    if (delta == 0u)
        return current;
    if (signed16_from_bits(delta) > 0)
        return delta <= UINT16_C(0x01ff) ? target :
            (uint16_t)(current - UINT16_C(0x0140));
    return delta > UINT16_C(0xfdff) ? target :
        (uint16_t)(current + UINT16_C(0x0140));
}

int portable_driver_recovery_relocate(int32_t *x_q16, int32_t *y_q16,
    uint16_t *heading, int16_t target_x, int16_t target_y,
    uint16_t target_heading)
{
    if (x_q16 == NULL || y_q16 == NULL || heading == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    /* A800 invokes39894 before moving X, then Y on later turns. Arrival
     * raises request40/mode7; that branch has not yet been qualified. */
    if (*x_q16 / INT32_C(65536) == target_x && *y_q16 >= 0)
    {
        const int16_t delta = signed16_from_bits((uint16_t)(
            *y_q16 / INT32_C(65536) - (uint16_t)target_y));
        if (delta >= -3 && delta <= 3)
            return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    }
    *heading = recovery_heading_step(*heading, target_heading);
    if (*x_q16 / INT32_C(65536) != target_x)
        *x_q16 = recovery_axis_step(*x_q16, target_x);
    else
        *y_q16 = recovery_axis_step(*y_q16, target_y);
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_recovery_tick(PortableDriverHopState *state,
    int32_t *x_q16, int32_t *y_q16, uint16_t *heading,
    uint16_t *flags74, uint16_t *contact34,
    int16_t target_x, int16_t target_y, uint16_t target_heading)
{
    if (state == NULL || x_q16 == NULL || y_q16 == NULL || heading == NULL ||
        flags74 == NULL || contact34 == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    if (state->mode != 6u && state->mode != 7u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    const uint16_t prior_heading = *heading;
    *heading = recovery_heading_step(prior_heading, target_heading);
    state->recovery_touchdown = 0u;
    state->recovery_checkpoint_sample = 0u;
    if (state->mode == 6u)
    {
        if (*x_q16 / INT32_C(65536) != target_x)
            *x_q16 = recovery_axis_step(*x_q16, target_x);
        else
        {
            const int16_t delta = signed16_from_bits((uint16_t)(
                *y_q16 / INT32_C(65536) - (uint16_t)target_y));
            if (*y_q16 >= 0 && delta >= -3 && delta <= 3)
            {
                *y_q16 = (int32_t)target_y * INT32_C(65536);
                state->mode = 7u;
                state->recovery_requests |= UINT16_C(0x40);
                *flags74 &= UINT16_C(0xdfff);
                *contact34 |= UINT16_C(3);
            }
            else
            {
                *y_q16 = recovery_axis_step(*y_q16, target_y);
                state->recovery_checkpoint_sample = 1u;
            }
        }
    }
    else if (prior_heading == target_heading)
    {
        const uint32_t bits = (uint32_t)state->height_q16 - UINT32_C(0x8000);
        const int64_t height = bits < UINT32_C(0x80000000) ? (int64_t)bits :
            (int64_t)bits - INT64_C(0x100000000);
        if (height > 0)
            state->height_q16 = (int32_t)height;
        else
        {
            state->height_q16 = 0;
            state->mode = 0u;
            *flags74 |= UINT16_C(1);
            state->recovery_touchdown = 1u;
        }
    }
    return PORTABLE_DRIVER_HOP_OK;
}

static int drift_charge_held(uint16_t control_word)
{
    return (control_word & UINT16_C(0x8000)) != 0u &&
           (control_word & UINT16_C(0x0010)) != 0u &&
           (control_word & UINT16_C(0x0300)) != 0u;
}

int portable_driver_drift_charge_pre_tick(
    PortableDriverDriftChargeState *state,
    uint16_t prior_source_control_word,
    uint8_t hop_mode_at_entry)
{
    if (state == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    if (hop_mode_at_entry > 1u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    if (hop_mode_at_entry != 0u)
        return PORTABLE_DRIVER_HOP_OK;

    if (drift_charge_held(prior_source_control_word))
        state->charge_counter = (uint16_t)(state->charge_counter + 1u);
    else
        state->charge_counter = 0u;
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_drift_charge_post_tick(
    PortableDriverDriftChargeState *state,
    uint16_t current_source_control_word,
    int32_t speed_q16,
    int32_t speed_target_q16,
    uint16_t *animation_flags)
{
    int32_t threshold;

    if (state == NULL || animation_flags == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    if (!drift_charge_held(current_source_control_word))
    {
        state->charge_counter = 0u;
        return PORTABLE_DRIVER_HOP_OK;
    }

    state->charge_counter = (uint16_t)(state->charge_counter + 1u);
    threshold = (int32_t)((uint32_t)speed_target_q16 + UINT32_C(0xff800000));
    if (state->charge_counter > UINT16_C(0x77) && speed_q16 >= threshold)
        *animation_flags |= UINT16_C(0x0040);
    return PORTABLE_DRIVER_HOP_OK;
}

static int capture_tile_near(int32_t kart_tile, int32_t actor_q16)
{
    const int32_t delta = kart_tile - actor_q16 / INT32_C(0x80000);
    return delta >= -4 && delta <= 4;
}

int portable_driver_capture_begin(PortableDriverCaptureState *state,
    uint16_t cue_flags_58, int16_t protection_6a, int16_t guard_66,
    const PortableDriverCaptureActor *actors, size_t actor_count)
{
    const PortableDriverCaptureActor *target = NULL;

    if (state == NULL || (actor_count != 0u && actors == NULL))
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    state->captured = 0u;
    state->launched = 0u;
    if ((cue_flags_58 & UINT16_C(0x20)) != 0u || protection_6a != 0 || guard_66 != 0)
        return PORTABLE_DRIVER_HOP_OK;
    const int32_t kart_x = state->x_q16 / INT32_C(0x10000) / 8;
    const int32_t kart_y = state->y_q16 / INT32_C(0x10000) / 8;
    for (size_t i = 0u; i < actor_count && target == NULL; ++i)
        if (capture_tile_near(kart_x, actors[i].x_q16) &&
            capture_tile_near(kart_y, actors[i].y_q16))
            target = &actors[i];
    /* With an empty search the ROM reads +0x34 through a null pointer; the
     * BIOS open-bus halfword there has bit 2 clear, so nothing captures. */
    if (target == NULL || (target->flags_34 & UINT16_C(2)) == 0u)
        return PORTABLE_DRIVER_HOP_OK;
    state->timer_9c = 92u;
    state->mode = PORTABLE_DRIVER_CAPTURE_MODE;
    state->timer_62 = 1u;
    state->state_flags |= UINT32_C(0x4000) | UINT32_C(0x800);
    state->response_88 = 0u;
    state->response_status_7e = 0u;
    state->component_x = (int16_t)((target->x_q16 - state->x_q16) / 8192);
    state->component_y = (int16_t)((target->y_q16 - state->y_q16) / 8192);
    if ((state->state_flags & UINT32_C(0x2000)) != 0u)
    {
        state->animation_b4 |= UINT16_C(0x300);
        state->spin_rate_b6 = UINT16_C(0x1000);
    }
    state->captured = 1u;
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_capture_tick(PortableDriverCaptureState *state)
{
    if (state == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    if (state->mode != PORTABLE_DRIVER_CAPTURE_MODE)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    state->captured = 0u;
    state->launched = 0u;
    state->timer_62 = 1u;
    if (state->timer_9c > 59u)
    {
        state->animation_b4 |= UINT16_C(8);
        --state->timer_9c;
        state->spin_82 = (uint16_t)(state->spin_82 + UINT16_C(0xc00));
        return PORTABLE_DRIVER_HOP_OK;
    }
    if (state->timer_9c != 0u)
    {
        state->animation_b4 &= UINT16_C(0xfff7);
        --state->timer_9c;
        state->speed_q16 = 0;
        state->component_x = 0;
        state->component_y = 0;
        if (state->timer_9c != 0u)
            return PORTABLE_DRIVER_HOP_OK;
    }
    state->component_x = 0;
    state->component_y = 0;
    state->velocity_q8 = INT16_C(0x2f0);
    state->height_q16 = INT32_C(0x100);
    state->animation_b4 = UINT16_C(0x8000);
    state->mode = 1u;
    state->driver_mode_84 = 1u;
    state->response_status_7e = 12u;
    state->hit_angle_80 = 0u;
    state->spin_82 = 0u;
    state->acceleration_q16 = 0;
    state->speed_q16 = INT32_C(0x01f00000);
    state->launched = 1u;
    return PORTABLE_DRIVER_HOP_OK;
}

int portable_driver_launch_flip_tick(uint8_t *response_status_7e,
    uint16_t *animation_b4, uint16_t *spin_82, int16_t velocity_q8)
{
    if (response_status_7e == NULL || animation_b4 == NULL || spin_82 == NULL)
        return PORTABLE_DRIVER_HOP_INVALID_ARGUMENT;
    if (*response_status_7e != 12u)
        return PORTABLE_DRIVER_HOP_UNSUPPORTED_MODE;
    if ((*animation_b4 & UINT16_C(1)) == 0u)
        *animation_b4 &= UINT16_C(0xffd3);
    if ((*animation_b4 & UINT16_C(0x8000)) == 0u)
    {
        *response_status_7e = 14u;
        return PORTABLE_DRIVER_HOP_OK;
    }
    const uint16_t prior = *spin_82;
    *spin_82 = (uint16_t)(prior + ((*animation_b4 & UINT16_C(0x2000)) != 0u ?
        UINT16_C(0x800) : UINT16_C(0xf800)));
    if (prior <= UINT16_C(0x7ff) && velocity_q8 < 0)
    {
        *spin_82 = 0u;
        *response_status_7e = 14u;
    }
    return PORTABLE_DRIVER_HOP_OK;
}
