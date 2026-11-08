#include "gp_result_clock.h"

#include <stddef.h>
#include <string.h>

enum { SOURCE_CLOCK_LIMIT = 0xea5f };

static uint16_t capped(uint64_t value)
{
    return value > SOURCE_CLOCK_LIMIT ? SOURCE_CLOCK_LIMIT : (uint16_t)value;
}

int portable_gp_source_clock_init(PortableGpSourceClock *clock,uint8_t lap_count)
{
    if (clock == NULL || lap_count > PORTABLE_RACE_MAX_LAPS)
        return 0;
    memset(clock, 0, sizeof *clock);
    clock->lap_count=lap_count;
    return 1;
}

int portable_gp_source_clock_enable(PortableGpSourceClock *clock)
{
    if (clock == NULL || clock->enabled)
        return 0;
    clock->enabled = 1u;
    return 1;
}

int portable_gp_source_clock_tick(PortableGpSourceClock *clock,
    const uint8_t *steps,size_t step_count)
{
    if (clock == NULL || clock->phase >= 60u)
        return 0;
    if (clock->enabled)
    {
        if(steps==NULL||step_count!=60u)return 0;
        /* The original global timer is u32; overflow is deliberate. */
        clock->centiseconds += steps[clock->phase];
        clock->phase = (uint8_t)((clock->phase + 1u) % 60u);
    }
    return 1;
}

int portable_gp_source_clock_record_lap(PortableGpSourceClock *clock,
                                        uint8_t lap_index)
{
    if (clock == NULL || !clock->enabled || clock->lap_count == 0u ||
        clock->lap_count > PORTABLE_RACE_MAX_LAPS || lap_index >= clock->lap_count ||
        clock->laps_recorded != lap_index)
        return 0;
    clock->lap_centiseconds[lap_index] = capped(
        (uint32_t)(clock->centiseconds - clock->lap_timestamp));
    clock->lap_timestamp = clock->centiseconds;
    clock->laps_recorded = (uint8_t)(lap_index + 1u);
    if (clock->laps_recorded == clock->lap_count)
        clock->finish_centiseconds = clock->centiseconds;
    return 1;
}

int portable_gp_source_clock_times(const PortableGpSourceClock *clock,
                                   uint16_t *times_centiseconds)
{
    if (clock == NULL || times_centiseconds == NULL ||
        clock->lap_count == 0u || clock->lap_count > PORTABLE_RACE_MAX_LAPS ||
        clock->laps_recorded != clock->lap_count)
        return 0;
    memcpy(times_centiseconds, clock->lap_centiseconds,
           clock->lap_count*sizeof clock->lap_centiseconds[0]);
    times_centiseconds[clock->lap_count] = capped(clock->finish_centiseconds);
    return 1;
}
