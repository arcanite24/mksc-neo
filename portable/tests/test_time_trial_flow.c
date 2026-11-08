#include "../app/time_trial_flow.h"
#include <stdio.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"time trial flow failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)

int main(void)
{
    const uint32_t courses[]={24u,25u};
    const uint32_t characters[]={1u,2u};
    PortableTimeTrialFlow f;
    CHECK(!portable_time_trial_flow_init(NULL,courses,2u,characters,2u));
    CHECK(portable_time_trial_flow_init(&f,courses,2u,characters,2u));
    CHECK(f.phase==PORTABLE_TIME_TRIAL_CHARACTER_SELECT&&
          portable_time_trial_flow_character(&f)==1u);
    CHECK(!portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_RESTART,0u));
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_PREVIOUS,0u));
    CHECK(portable_time_trial_flow_character(&f)==2u);
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_NEXT,0u));
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_CONFIRM,0u));
    CHECK(f.phase==PORTABLE_TIME_TRIAL_COURSE_SELECT);
    CHECK(!portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_RESTART,0u));
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_PREVIOUS,0u));
    CHECK(portable_time_trial_flow_course(&f)==25u);
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_NEXT,0u));
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_NEXT,0u));
    CHECK(portable_time_trial_flow_course(&f)==25u);
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_CONFIRM,0u));
    CHECK(f.phase==PORTABLE_TIME_TRIAL_RACE);
    CHECK(!portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_NEXT,0u));
    CHECK(!portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_RACE_FINISHED,0u));
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_RACE_FINISHED,8085u));
    CHECK(f.phase==PORTABLE_TIME_TRIAL_RESULTS&&f.result_ticks==8085u);
    CHECK(!portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_CONFIRM,0u));
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_RESTART,0u));
    CHECK(f.phase==PORTABLE_TIME_TRIAL_RACE&&f.result_ticks==0u);
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_BACK,0u));
    CHECK(f.phase==PORTABLE_TIME_TRIAL_COURSE_SELECT);
    CHECK(portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_BACK,0u));
    CHECK(f.phase==PORTABLE_TIME_TRIAL_CHARACTER_SELECT);
    CHECK(!portable_time_trial_flow_event(&f,PORTABLE_TIME_TRIAL_EVENT_BACK,0u));
    puts("portable time trial flow: PASS");
    return 0;
}
