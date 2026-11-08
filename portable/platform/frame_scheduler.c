#include "frame_scheduler.h"

#include <limits.h>

#define NS_PER_SECOND UINT64_C(1000000000)
#define TICK_PHASE ((uint64_t)PORTABLE_FRAME_TICK_NUMERATOR * NS_PER_SECOND)

void portable_frame_scheduler_init(PortableFrameScheduler *scheduler)
{
    if (scheduler == NULL)
        return;
    scheduler->last_ns = 0;
    scheduler->phase = 0;
    scheduler->initialized = 0;
    scheduler->active = 0;
}

void portable_frame_scheduler_reset(PortableFrameScheduler *scheduler,
                                    uint64_t now_ns, int active)
{
    if (scheduler == NULL)
        return;
    scheduler->last_ns = now_ns;
    scheduler->phase = 0;
    scheduler->initialized = 1;
    scheduler->active = active != 0;
}

int portable_frame_scheduler_update(PortableFrameScheduler *scheduler,
                                    uint64_t now_ns, int active,
                                    uint16_t sampled_buttons,
                                    uint16_t *tick_buttons, size_t capacity,
                                    size_t *out_count, int *out_dropped_debt)
{
    if (out_count != NULL)
        *out_count = 0;
    if (out_dropped_debt != NULL)
        *out_dropped_debt = 0;
    if (scheduler == NULL || tick_buttons == NULL ||
        capacity < PORTABLE_FRAME_MAX_CATCH_UP || out_count == NULL)
        return PORTABLE_FRAME_SCHEDULER_INVALID_ARGUMENT;

    active = active != 0;
    if (!scheduler->initialized) {
        portable_frame_scheduler_reset(scheduler, now_ns, active);
        return PORTABLE_FRAME_SCHEDULER_OK;
    }
    if (now_ns < scheduler->last_ns)
        return PORTABLE_FRAME_SCHEDULER_NON_MONOTONIC;
    if (!active) {
        portable_frame_scheduler_reset(scheduler, now_ns, 0);
        return PORTABLE_FRAME_SCHEDULER_OK;
    }
    if (!scheduler->active) {
        portable_frame_scheduler_reset(scheduler, now_ns, 1);
        return PORTABLE_FRAME_SCHEDULER_OK;
    }

    uint64_t elapsed = now_ns - scheduler->last_ns;
    scheduler->last_ns = now_ns;
    const uint64_t saturation = TICK_PHASE *
        (PORTABLE_FRAME_MAX_CATCH_UP + UINT64_C(1));
    uint64_t elapsed_phase;
    if (elapsed > saturation / PORTABLE_FRAME_TICK_DENOMINATOR)
        elapsed_phase = saturation;
    else
        elapsed_phase = elapsed * PORTABLE_FRAME_TICK_DENOMINATOR;
    uint64_t total = scheduler->phase;
    if (elapsed_phase > saturation - total)
        total = saturation;
    else
        total += elapsed_phase;

    size_t count = (size_t)(total / TICK_PHASE);
    if (count > PORTABLE_FRAME_MAX_CATCH_UP) {
        count = PORTABLE_FRAME_MAX_CATCH_UP;
        scheduler->phase = 0;
        if (out_dropped_debt != NULL)
            *out_dropped_debt = 1;
    } else {
        scheduler->phase = total - count * TICK_PHASE;
    }
    for (size_t i = 0; i < count; ++i)
        tick_buttons[i] = sampled_buttons;
    *out_count = count;
    return PORTABLE_FRAME_SCHEDULER_OK;
}

uint32_t portable_frame_scheduler_alpha(const PortableFrameScheduler *scheduler)
{
    if(scheduler==NULL||!scheduler->initialized||!scheduler->active)return 65536u;
    return (uint32_t)(scheduler->phase*65536u/TICK_PHASE);
}
