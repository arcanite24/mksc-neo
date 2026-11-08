#ifndef PORTABLE_TIME_TRIAL_RECORD_DEFAULTS_H
#define PORTABLE_TIME_TRIAL_RECORD_DEFAULTS_H
#include <stdint.h>
typedef struct { uint8_t character_class;uint16_t centiseconds; } PortableTimeTrialRecordTarget;
typedef struct { PortableTimeTrialRecordTarget rows[6]; } PortableTimeTrialRecordDefaults;
static inline int portable_time_trial_record_defaults_valid(const PortableTimeTrialRecordDefaults *rules)
{
    if(!rules)return 0;
    for(unsigned row=0u;row<6u;++row){
        if(rules->rows[row].character_class>7u||rules->rows[row].centiseconds==0u||
           rules->rows[row].centiseconds>59999u)return 0;
        if(row>0u&&row<5u&&rules->rows[row-1u].centiseconds>rules->rows[row].centiseconds)return 0;
    }
    return 1;
}
#endif
