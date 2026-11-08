#include "time_trial_result.h"
#include "frame_scheduler.h"

int portable_time_trial_result_is_valid(
    const PortableRecoveredTimeTrialSnapshot *snapshot)
{
    uint64_t total = 0u;

    if (snapshot == NULL ||
        snapshot->phase != PORTABLE_RACE_PHASE_FINISHED ||
        snapshot->lap_count == 0u ||
        snapshot->lap_count > PORTABLE_RACE_MAX_LAPS ||
        snapshot->laps_completed != snapshot->lap_count ||
        snapshot->total_ticks == 0u)
        return 0;
    for (uint16_t lap = 0u; lap < snapshot->lap_count; ++lap)
    {
        if (snapshot->lap_ticks[lap] == 0u)
            return 0;
        total += snapshot->lap_ticks[lap];
    }
    return total == snapshot->total_ticks;
}

int portable_time_trial_finish_record(const PortableRecoveredTimeTrialSnapshot *snapshot,
    const PortablePersonalBestTable *records)
{
    if(records==NULL||!portable_time_trial_result_is_valid(snapshot))return 0;
    uint32_t lap=snapshot->lap_ticks[0];
    for(uint16_t i=1u;i<snapshot->lap_count;++i)
        if(snapshot->lap_ticks[i]<lap)lap=snapshot->lap_ticks[i];
    /* 5D864/5D8DC compare the best total and independent lap inclusively.
     * Native records retain authoritative ticks; an empty entry is unbounded. */
    return records->races[0].ticks==0u||records->races[0].ticks>=snapshot->total_ticks||
        records->best_lap.ticks==0u||records->best_lap.ticks>=lap;
}

static uint32_t result_centiseconds(uint32_t ticks)
{
    const uint64_t value=(uint64_t)ticks*PORTABLE_FRAME_TICK_NUMERATOR*100u/PORTABLE_FRAME_TICK_DENOMINATOR;
    return (uint32_t)(value>59999u?59999u:value);
}

int portable_time_trial_finish_record_with_defaults(const PortableRecoveredTimeTrialSnapshot *snapshot,
    const PortablePersonalBestTable *records,const PortableTimeTrialRecordDefaults *defaults)
{
    if(!defaults)return portable_time_trial_finish_record(snapshot,records);
    if(!records||!portable_time_trial_record_defaults_valid(defaults)||!portable_time_trial_result_is_valid(snapshot))return 0;
    uint32_t lap=snapshot->lap_ticks[0];
    for(unsigned i=1u;i<snapshot->lap_count;++i)if(snapshot->lap_ticks[i]<lap)lap=snapshot->lap_ticks[i];
    uint32_t total_limit=defaults->rows[0].centiseconds,lap_limit=defaults->rows[5].centiseconds;
    if(records->races[0].ticks&&result_centiseconds(records->races[0].ticks)<total_limit)
        total_limit=result_centiseconds(records->races[0].ticks);
    if(records->best_lap.ticks&&result_centiseconds(records->best_lap.ticks)<lap_limit)
        lap_limit=result_centiseconds(records->best_lap.ticks);
    return result_centiseconds(snapshot->total_ticks)<=total_limit||result_centiseconds(lap)<=lap_limit;
}

PortablePersonalBestStatus portable_time_trial_personal_best_update(
    const char *path,
    const PortableRecoveredTimeTrialSnapshot *snapshot,
    uint32_t *out_best_ticks,
    int *out_updated)
{
    if (!portable_time_trial_result_is_valid(snapshot))
        return PORTABLE_PERSONAL_BEST_INVALID;
    if(out_best_ticks==NULL||out_updated==NULL)return PORTABLE_PERSONAL_BEST_INVALID;
    uint32_t lap_ticks=snapshot->lap_ticks[0];
    for(uint16_t lap=1u;lap<snapshot->lap_count;++lap)
        if(snapshot->lap_ticks[lap]<lap_ticks)lap_ticks=snapshot->lap_ticks[lap];
    PortablePersonalBestTable table;
    const PortablePersonalBestStatus status=portable_personal_best_update_table(
        path,&snapshot->compatibility,snapshot->total_ticks,lap_ticks,&table,out_updated);
    *out_best_ticks=status==PORTABLE_PERSONAL_BEST_OK?table.races[0].ticks:0u;
    return status;
}
