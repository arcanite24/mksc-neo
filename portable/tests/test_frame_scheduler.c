#include "../platform/frame_scheduler.h"
#include "../core/driver_input.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "frame scheduler test failed line %d: %s\n", \
                __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static size_t update(PortableFrameScheduler *scheduler, uint64_t now,
                     int active, uint16_t buttons, int *dropped)
{
    uint16_t output[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t count = 99;
    CHECK(portable_frame_scheduler_update(
        scheduler, now, active, buttons, output,
        PORTABLE_FRAME_MAX_CATCH_UP, &count, dropped) ==
        PORTABLE_FRAME_SCHEDULER_OK);
    for (size_t i = 0; i < count; ++i)
        CHECK(output[i] == buttons);
    return count;
}

int main(void)
{
    const uint64_t tick_ns = UINT64_C(16742707);
    PortableFrameScheduler scheduler;
    portable_frame_scheduler_init(&scheduler);
    int dropped = 0;
    CHECK(update(&scheduler, 0, 1, PORTABLE_INPUT_ACCELERATE, &dropped) == 0);
    CHECK(update(&scheduler, tick_ns - 1, 1, PORTABLE_INPUT_ACCELERATE,
                 &dropped) == 0);
    CHECK(update(&scheduler, tick_ns, 1, PORTABLE_INPUT_ACCELERATE,
                 &dropped) == 1);
    CHECK(!dropped);

    /* Equal elapsed time is partition-independent while the catch-up cap is
     * not reached. This is the render-rate independence boundary. */
    PortableFrameScheduler fine, coarse;
    portable_frame_scheduler_init(&fine);
    portable_frame_scheduler_init(&coarse);
    CHECK(update(&fine, 0, 1, PORTABLE_INPUT_LEFT, &dropped) == 0);
    CHECK(update(&coarse, 0, 1, PORTABLE_INPUT_LEFT, &dropped) == 0);
    size_t fine_ticks = 0, coarse_ticks = 0;
    for (uint64_t now = UINT64_C(5000000); now <= UINT64_C(1000000000);
         now += UINT64_C(5000000))
        fine_ticks += update(&fine, now, 1, PORTABLE_INPUT_LEFT, &dropped);
    for (uint64_t now = UINT64_C(25000000); now <= UINT64_C(1000000000);
         now += UINT64_C(25000000))
        coarse_ticks += update(&coarse, now, 1, PORTABLE_INPUT_LEFT, &dropped);
    CHECK(fine_ticks == 59 && coarse_ticks == fine_ticks);
    CHECK(fine.phase == coarse.phase);

    /* A long stall emits at most four ticks and cannot cause a later burst. */
    portable_frame_scheduler_init(&scheduler);
    CHECK(update(&scheduler, 0, 1, 0, &dropped) == 0);
    CHECK(update(&scheduler, UINT64_C(1000000000), 1, 0, &dropped) == 4);
    CHECK(dropped);
    CHECK(update(&scheduler, UINT64_C(1000000000), 1, 0, &dropped) == 0);
    CHECK(update(&scheduler, UINT64_C(1000000000) + tick_ns, 1, 0,
                 &dropped) == 1);

    /* Inactive time and reactivation discard debt and held state. */
    portable_frame_scheduler_init(&scheduler);
    CHECK(update(&scheduler, 0, 1, 0, &dropped) == 0);
    CHECK(update(&scheduler, tick_ns / 2, 1, 0, &dropped) == 0);
    CHECK(update(&scheduler, tick_ns, 0, PORTABLE_INPUT_ACCELERATE,
                 &dropped) == 0);
    CHECK(update(&scheduler, UINT64_C(900000000), 0,
                 PORTABLE_INPUT_ACCELERATE, &dropped) == 0);
    CHECK(update(&scheduler, UINT64_C(1000000000), 1,
                 PORTABLE_INPUT_ACCELERATE, &dropped) == 0);
    CHECK(update(&scheduler, UINT64_C(1000000000) + tick_ns, 1,
                 PORTABLE_INPUT_ACCELERATE | UINT16_C(0x8000), &dropped) == 1);

    /* The scheduler transports caller-defined inputs. Frontend Up/Down occupy
     * bits outside the six-button driver mask and must survive scheduling. */
    portable_frame_scheduler_reset(&scheduler, UINT64_C(1100000000), 1);
    CHECK(update(&scheduler, UINT64_C(1100000000) + tick_ns, 1,
                 UINT16_C(0x00c0), &dropped) == 1);

    portable_frame_scheduler_reset(&scheduler, UINT64_C(2000000000), 1);
    CHECK(update(&scheduler, UINT64_C(2000000000) + tick_ns / 2, 1, 0,
                 &dropped) == 0);
    portable_frame_scheduler_reset(&scheduler,
                                   UINT64_C(2000000000) + tick_ns / 2, 1);
    CHECK(update(&scheduler, UINT64_C(2000000000) + tick_ns, 1, 0,
                 &dropped) == 0);
    CHECK(portable_frame_scheduler_update(
        &scheduler, 0, 1, 0, (uint16_t[4]){0}, 4,
        &(size_t){0}, &dropped) == PORTABLE_FRAME_SCHEDULER_NON_MONOTONIC);

    puts("frame scheduler: PASS");
    return 0;
}
