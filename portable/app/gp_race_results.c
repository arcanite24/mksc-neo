#include "gp_race_results.h"

#include <string.h>

enum
{
    PORTABLE_GP_RESULTS_EMPTY_TIME = 0xea5f
};

static uint16_t ticks_to_centiseconds(uint32_t ticks)
{
    /* One authoritative tick is one GBA frame: 4389 / 262144 seconds. */
    uint64_t value = (uint64_t)ticks * UINT64_C(4389) * UINT64_C(100) /
                     UINT64_C(262144);
    if (value > PORTABLE_GP_RESULTS_EMPTY_TIME)
        value = PORTABLE_GP_RESULTS_EMPTY_TIME;
    return (uint16_t)value;
}

PortableGpRaceResultsStatus portable_gp_race_results_config(
    const PortableGpRaceSession *race,
    PortableGpResultsSessionConfig *config)
{
    uint32_t best_lap;
    uint8_t driver_class;
    if (race == NULL || config == NULL)
        return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
    if (race->runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED ||
        race->runtime.session.laps_completed != race->runtime.session.lap_count ||
        (race->runtime.session.lap_count == 0u || race->runtime.session.lap_count > PORTABLE_RACE_MAX_LAPS) ||
        race->runtime.session.elapsed_ticks == 0u ||
        (race->roster.race_phase_7d4 & UINT8_C(0x0f)) != 5u ||
        (race->roster.racers[0].source_flags & UINT32_C(0x20)) == 0u)
        return PORTABLE_GP_RACE_RESULTS_NOT_READY;
    for (size_t index = 0u; index < race->runtime.session.lap_count; ++index)
        if (race->runtime.session.lap_ticks[index] == 0u)
            return PORTABLE_GP_RACE_RESULTS_NOT_READY;

    memset(config, 0, sizeof *config);
    config->race_flags = race->roster.race_mode_flags_7da;
    config->course_in_cup = 0u;
    config->highlighted_row = 0u;
    config->course_record_changed = 1u;
    for (size_t index = 0u; index < PORTABLE_GP_RESULTS_MAX_ROWS; ++index)
        config->times_centiseconds[index] = PORTABLE_GP_RESULTS_EMPTY_TIME;
    config->times_centiseconds[0] = ticks_to_centiseconds(
        race->runtime.session.elapsed_ticks);

    for (size_t index = 0u;
         index < PORTABLE_GP_RESULTS_RECORD_ROW_COUNT; ++index)
    {
        config->records[index].flags = UINT32_C(0x10);
        config->records[index].time_centiseconds =
            PORTABLE_GP_RESULTS_EMPTY_TIME;
    }
    driver_class = race->roster.racers[0].driver_class;
    config->records[0].flags = driver_class;
    config->records[0].time_centiseconds = config->times_centiseconds[0];
    config->records[0].emphasized = 1u;
    best_lap = race->runtime.session.lap_ticks[0];
    for (size_t index = 1u; index < race->runtime.session.lap_count; ++index)
        if (race->runtime.session.lap_ticks[index] < best_lap)
            best_lap = race->runtime.session.lap_ticks[index];
    config->records[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT - 1u].flags =
        driver_class;
    config->records[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT - 1u]
        .time_centiseconds = ticks_to_centiseconds(best_lap);
    config->records[PORTABLE_GP_RESULTS_RECORD_ROW_COUNT - 1u].emphasized = 1u;
    return PORTABLE_GP_RACE_RESULTS_OK;
}

PortableGpRaceResultsStatus portable_quick_run_results_build(
    const PortableGpRaceSession *race, PortableQuickRunResults *results)
{
    if(race==NULL || results==NULL || race->rules.route_count==0u ||
       race->rules.route_count>UINT16_MAX || race->engine_class>=3u || race->rules.lap_count==0u ||
       race->rules.lap_count>PORTABLE_RACE_MAX_LAPS)
        return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
    if((race->roster.racers[0].source_flags&UINT32_C(0x20))==0u ||
       race->source_result_clock.laps_recorded!=race->rules.lap_count)
        return PORTABLE_GP_RACE_RESULTS_NOT_READY;
    PortableQuickRunResults next={0};
    next.engine_class=race->engine_class;
    uint8_t seen=0u;
    for(unsigned rank=0u;rank<PORTABLE_GP_RACER_COUNT;++rank){
        const unsigned index=race->roster.standings[rank];
        if(index>=PORTABLE_GP_RACER_COUNT || (seen&(1u<<index))!=0u ||
           race->roster.racers[index].driver_class>=8u)
            return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
        seen|=(uint8_t)(1u<<index);
        const PortableGpRacer *r=&race->roster.racers[index];
        PortableQuickRunResultRow *row=&next.rows[rank];
        row->racer_index=(uint8_t)index;row->driver_class=r->driver_class;
        uint32_t time=0u;
        if((r->source_flags&UINT32_C(0x20))!=0u){
            const uint16_t *splits=index==0u?race->source_result_clock.lap_centiseconds:
                race->rival_lap_centiseconds[index-1u];
            for(unsigned lap=0u;lap<PORTABLE_RACE_MAX_LAPS;++lap)time+=splits[lap];
        }else{
            /* D7B8 deliberately narrows remaining checkpoints before the
             * 13-centisecond estimate and unsigned32 addition/clamp. */
            const uint32_t factor=UINT32_C(0x80)+race->rules.lap_count-
                ((uint32_t)r->route_wrap_91+1u);
            const uint16_t remaining=(uint16_t)((uint32_t)race->rules.route_count*factor+
                (uint32_t)race->rules.route_count-((uint32_t)r->route_index+1u));
            time=race->source_result_clock.centiseconds+13u*remaining+(r->rank_flags_ba>>1u);
            row->estimated=1u;
        }
        row->centiseconds=time>PORTABLE_GP_RESULTS_EMPTY_TIME?
            PORTABLE_GP_RESULTS_EMPTY_TIME:(uint16_t)time;
    }
    *results=next;
    return PORTABLE_GP_RACE_RESULTS_OK;
}

/* Freeze source Battle standings after the shared180-tick finish transition;
 * no GP points, checkpoint estimates or lap records belong to this mode. */
PortableGpRaceResultsStatus portable_battle_results_build(
    const PortableGpRaceSession *race,PortableBattleResults *results)
{
    if (race==NULL || results==NULL || !race->player.battle_enabled || race->rules.lap_count!=0u ||
        (race->roster.race_mode_flags_7da&0x0f00u)!=0x0800u)
        return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
    if (race->runtime.session.phase!=PORTABLE_RACE_PHASE_FINISHED ||
        (race->roster.race_phase_7d4&15u)!=5u) return PORTABLE_GP_RACE_RESULTS_NOT_READY;
    const size_t count=portable_gp_race_roster_count(&race->roster);
    if (count<2u || count>4u) return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
    PortableBattleResults next={.count=(uint8_t)count};
    unsigned seen=0u;
    for (size_t rank=0u;rank<count;++rank) {
        const unsigned owner=race->roster.standings[rank];
        if (owner>=count || (seen&(1u<<owner))!=0u) return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
        seen|=1u<<owner;
        const PortableGpRacer *r=&race->roster.racers[owner];
        if (r->driver_class>=8u || r->balloons_remaining>3u || (r->rank_flags_ba>>1u)>=count)
            return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
        next.rows[rank]=(PortableBattleResultRow){(uint8_t)owner,r->driver_class,
            r->balloons_remaining,(uint8_t)(r->rank_flags_ba>>1u)};
    }
    *results=next;
    return PORTABLE_GP_RACE_RESULTS_OK;
}

PortableGpRaceResultsStatus portable_vs_results_build(
    const PortableGpRaceSession *race,PortableVsResults *results)
{
    if (race==NULL || results==NULL || race->local_count<2u || race->local_count>PORTABLE_GP_MAX_LOCALS ||
        race->roster.race_mode_flags_7da!=0x0400u ||
        portable_gp_race_roster_count(&race->roster)!=race->local_count)
        return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
    if ((race->roster.race_phase_7d4&15u)!=5u) return PORTABLE_GP_RACE_RESULTS_NOT_READY;
    const unsigned count=race->local_count;
    PortableVsResults next={.count=(uint8_t)count};
    unsigned seen=0u,row=0u;
    for (unsigned pass=0u;pass<2u;++pass)
        for (unsigned rank=0u;rank<count;++rank) {
            const unsigned owner=race->roster.standings[rank];
            if (owner>=count) return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
            const PortableGpRacer *r=&race->roster.racers[owner];
            const unsigned finished=(r->source_flags&UINT32_C(0x20))!=0u && !race->vs_retired[owner];
            if (finished!=(unsigned)(pass==0u)) continue;
            if ((seen&(1u<<owner))!=0u || r->driver_class>=8u)
                return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
            seen|=1u<<owner;
            next.rows[row++]=(PortableVsResultRow){finished?race->vs_finish_centiseconds[owner]:0u,
                (uint8_t)owner,r->driver_class,(uint8_t)!finished};
        }
    if (row!=count) return PORTABLE_GP_RACE_RESULTS_INVALID_ARGUMENT;
    *results=next;
    return PORTABLE_GP_RACE_RESULTS_OK;
}
