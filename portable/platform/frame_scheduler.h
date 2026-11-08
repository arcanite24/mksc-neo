#ifndef PORTABLE_FRAME_SCHEDULER_H
#define PORTABLE_FRAME_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

/* One GBA frame is 280896 master-clock cycles at 2^24 Hz, reduced to
 * 4389 / 262144 seconds (about 59.7275 Hz). */
#define PORTABLE_FRAME_TICK_NUMERATOR 4389u
#define PORTABLE_FRAME_TICK_DENOMINATOR 262144u
#define PORTABLE_FRAME_MAX_CATCH_UP 4u

enum {
    PORTABLE_FRAME_SCHEDULER_OK = 0,
    PORTABLE_FRAME_SCHEDULER_INVALID_ARGUMENT = 1,
    PORTABLE_FRAME_SCHEDULER_NON_MONOTONIC = 2,
};

typedef struct {
    uint64_t last_ns;
    uint64_t phase;
    int initialized;
    int active;
} PortableFrameScheduler;

void portable_frame_scheduler_init(PortableFrameScheduler *scheduler);
void portable_frame_scheduler_reset(PortableFrameScheduler *scheduler,
                                    uint64_t now_ns, int active);

/* The caller supplies monotonic nanoseconds and one current semantic input
 * sample. Every produced tick receives that sample. Inactive time creates no
 * debt; the first update after activation also produces no tick. If more than
 * four ticks are overdue, four are emitted and the excess debt is discarded. */
int portable_frame_scheduler_update(PortableFrameScheduler *scheduler,
                                    uint64_t now_ns, int active,
                                    uint16_t sampled_buttons,
                                    uint16_t *tick_buttons, size_t capacity,
                                    size_t *out_count, int *out_dropped_debt);

uint32_t portable_frame_scheduler_alpha(const PortableFrameScheduler *scheduler);

#endif
