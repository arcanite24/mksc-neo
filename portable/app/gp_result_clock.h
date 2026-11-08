#ifndef MKSC_PORTABLE_GP_RESULT_CLOCK_H
#define MKSC_PORTABLE_GP_RESULT_CLOCK_H

#include <stdint.h>
#include <stddef.h>
#include "../core/race_session.h"

/* Source 0x03005c80 and player +0xdc/+0xe8, without host-frame assumptions.
 * The frontend owns the initial state; enable follows sub_804D8F8 on the
 * launch frame, so the first increment occurs on the following frame. */
typedef struct
{
    uint32_t centiseconds;
    uint32_t lap_timestamp;
    uint32_t finish_centiseconds;
    uint16_t lap_centiseconds[PORTABLE_RACE_MAX_LAPS];
    uint8_t phase;
    uint8_t enabled;
    uint8_t laps_recorded;
    uint8_t lap_count;
} PortableGpSourceClock;

/* Zero laps retains the scene timer for Battle, without lap/result splits. */
int portable_gp_source_clock_init(PortableGpSourceClock *clock,uint8_t lap_count);
int portable_gp_source_clock_enable(PortableGpSourceClock *clock);
int portable_gp_source_clock_tick(PortableGpSourceClock *clock,
    const uint8_t *steps,size_t step_count);
int portable_gp_source_clock_record_lap(PortableGpSourceClock *clock,
                                        uint8_t lap_index);
/* Caller supplies lap_count+1 entries; total follows the final split. */
int portable_gp_source_clock_times(const PortableGpSourceClock *clock,
                                   uint16_t *times_centiseconds);

#endif
