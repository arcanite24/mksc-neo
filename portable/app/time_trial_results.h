#ifndef PORTABLE_TIME_TRIAL_RESULTS_H
#define PORTABLE_TIME_TRIAL_RESULTS_H
#include "gp_results_flow.h"
#include "time_trial_record_defaults.h"

/* 21BEC/21AFC insert into the first empty or strictly slower slot.
 * Highlight the new row only: an identical prior time is a different record. */
static inline void portable_time_trial_results_emphasis(const uint32_t old[6],
    uint32_t race,uint32_t lap,uint8_t out[6])
{
    for(unsigned row=0u;row<6u;++row)out[row]=0u;
    for(unsigned row=0u;row<5u;++row)if(old[row]==0u||race<old[row]){out[row]=1u;break;}
    out[5]=(uint8_t)(lap!=0u&&(old[5]==0u||lap<old[5]));
}

/* 5D30C seeds placeholders; actual saved runs displace them using strict
 * time order. Equal targets stay ahead of a new run, as in21BEC. The caller
 * persists only its real PB table; these target rows remain content data. */
static inline void portable_time_trial_results_merge(const PortableTimeTrialRecordDefaults *defaults,
    const PortableGpResultsRecord actual[6],PortableGpResultsRecord out[6])
{
    if(!defaults){for(unsigned row=0u;row<6u;++row)out[row]=actual[row];return;}
    unsigned target=0u,saved=0u;
    for(unsigned row=0u;row<5u;++row){
        if(saved<5u&&actual[saved].time_centiseconds!=0u&&
           (target>=5u||actual[saved].time_centiseconds<defaults->rows[target].centiseconds))
            out[row]=actual[saved++];
        else{
            const PortableTimeTrialRecordTarget value=defaults->rows[target++];
            out[row]=(PortableGpResultsRecord){.flags=(uint32_t)(value.character_class|0x10u),
                .time_centiseconds=value.centiseconds};
        }
    }
    if(actual[5].time_centiseconds!=0u&&actual[5].time_centiseconds<defaults->rows[5].centiseconds)out[5]=actual[5];
    else out[5]=(PortableGpResultsRecord){.flags=(uint32_t)(defaults->rows[5].character_class|0x10u),
        .time_centiseconds=defaults->rows[5].centiseconds};
}

/* 22704 owns the TT lap summary, record reveal and five ordinary actions.
 * The shared map commands are also used by GP's first lap summary. */
static inline PortableGpResultsStatus portable_time_trial_results_tick(
    PortableGpResultsFlow *flow,uint8_t laps,const PortableGpResultsRecord records[6],const uint8_t record_identities[8],const uint8_t option_codes[5],
    uint16_t repeated,uint16_t triggered,uint8_t replay_unavailable)
{
    if(!flow||!records||laps==0u||laps>5u)return PORTABLE_GP_RESULTS_INVALID_ARGUMENT;
    const uint8_t confirm=(uint8_t)((triggered&9u)!=0u);
    const uint16_t flags=replay_unavailable?2u:0u;
    switch(flow->state){
    case 0:case 1:return portable_gp_results_flow_tick_entry(flow,records[0].emphasized,records[5].emphasized);
    case 2:return portable_gp_results_flow_tick_reveal(flow,laps);
    case 3:return portable_gp_results_flow_tick_hold(flow,0x200u);
    case 4:return portable_gp_results_flow_tick_exit_wipe(flow,confirm);
    case 5:return portable_gp_results_flow_tick_followup_setup(flow);
    case 6:return portable_gp_results_flow_tick_record_setup(flow,records,record_identities);
    case 7:return portable_gp_results_flow_tick_record_slide(flow,confirm);
    case 8:return portable_gp_results_flow_tick_menu_reveal(flow,confirm);
    case 9:return portable_gp_results_flow_tick_menu_setup(flow,flags);
    case 10:return portable_gp_results_flow_tick_menu_record_setup(flow);
    case 11:return portable_gp_results_flow_tick_menu_selection(flow,repeated,triggered,flags,option_codes);
    case 12:return portable_gp_results_flow_tick_action_dispatch(flow);
    case 13:return PORTABLE_GP_RESULTS_OK;
    default:return PORTABLE_GP_RESULTS_STATE_UNSUPPORTED;
    }
}
#endif
