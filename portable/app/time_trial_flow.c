#include "time_trial_flow.h"

static void move_index(size_t *index,size_t count,int direction)
{
    if(direction<0)*index=*index==0u?count-1u:*index-1u;
    else *index=(*index+1u)%count;
}

int portable_time_trial_flow_init(PortableTimeTrialFlow *flow,
                                  const uint32_t *course_ids,
                                  size_t course_count,
                                  const uint32_t *character_ids,
                                  size_t character_count)
{
    if(flow==NULL||course_ids==NULL||character_ids==NULL||course_count==0u||
       character_count==0u)return 0;
    for(size_t i=0;i<course_count;++i)if(course_ids[i]==0u)return 0;
    for(size_t i=0;i<character_count;++i)if(character_ids[i]==0u)return 0;
    *flow=(PortableTimeTrialFlow){course_ids,course_count,character_ids,
        character_count,0u,0u,PORTABLE_TIME_TRIAL_CHARACTER_SELECT,0u};
    return 1;
}

int portable_time_trial_flow_event(PortableTimeTrialFlow *flow,
                                   PortableTimeTrialEvent event,
                                   uint32_t result_ticks)
{
    if(flow==NULL||flow->course_ids==NULL||flow->character_ids==NULL)return 0;
    switch(flow->phase){
    case PORTABLE_TIME_TRIAL_CHARACTER_SELECT:
        if(event==PORTABLE_TIME_TRIAL_EVENT_PREVIOUS)
            move_index(&flow->character_index,flow->character_count,-1);
        else if(event==PORTABLE_TIME_TRIAL_EVENT_NEXT)
            move_index(&flow->character_index,flow->character_count,1);
        else if(event==PORTABLE_TIME_TRIAL_EVENT_CONFIRM)
            flow->phase=PORTABLE_TIME_TRIAL_COURSE_SELECT;
        else return 0;
        break;
    case PORTABLE_TIME_TRIAL_COURSE_SELECT:
        if(event==PORTABLE_TIME_TRIAL_EVENT_PREVIOUS)
            move_index(&flow->course_index,flow->course_count,-1);
        else if(event==PORTABLE_TIME_TRIAL_EVENT_NEXT)
            move_index(&flow->course_index,flow->course_count,1);
        else if(event==PORTABLE_TIME_TRIAL_EVENT_CONFIRM)
            flow->phase=PORTABLE_TIME_TRIAL_RACE;
        else if(event==PORTABLE_TIME_TRIAL_EVENT_BACK)
            flow->phase=PORTABLE_TIME_TRIAL_CHARACTER_SELECT;
        else return 0;
        break;
    case PORTABLE_TIME_TRIAL_RACE:
        if(event==PORTABLE_TIME_TRIAL_EVENT_RACE_FINISHED&&result_ticks!=0u){
            flow->result_ticks=result_ticks;
            flow->phase=PORTABLE_TIME_TRIAL_RESULTS;
        }else if(event==PORTABLE_TIME_TRIAL_EVENT_RESTART){
            flow->result_ticks=0u;
        }else if(event==PORTABLE_TIME_TRIAL_EVENT_BACK){
            flow->result_ticks=0u;
            flow->phase=PORTABLE_TIME_TRIAL_COURSE_SELECT;
        }else return 0;
        break;
    case PORTABLE_TIME_TRIAL_RESULTS:
        if(event==PORTABLE_TIME_TRIAL_EVENT_RESTART){
            flow->result_ticks=0u;
            flow->phase=PORTABLE_TIME_TRIAL_RACE;
        }else if(event==PORTABLE_TIME_TRIAL_EVENT_BACK){
            flow->result_ticks=0u;
            flow->phase=PORTABLE_TIME_TRIAL_COURSE_SELECT;
        }else return 0;
        break;
    default:return 0;
    }
    return 1;
}

uint32_t portable_time_trial_flow_course(const PortableTimeTrialFlow *flow)
{return flow&&flow->course_ids?flow->course_ids[flow->course_index]:0u;}

uint32_t portable_time_trial_flow_character(const PortableTimeTrialFlow *flow)
{return flow&&flow->character_ids?flow->character_ids[flow->character_index]:0u;}
