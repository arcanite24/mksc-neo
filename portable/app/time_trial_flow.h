#ifndef PORTABLE_TIME_TRIAL_FLOW_H
#define PORTABLE_TIME_TRIAL_FLOW_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    PORTABLE_TIME_TRIAL_CHARACTER_SELECT = 1,
    PORTABLE_TIME_TRIAL_COURSE_SELECT,
    PORTABLE_TIME_TRIAL_RACE,
    PORTABLE_TIME_TRIAL_RESULTS
} PortableTimeTrialPhase;

typedef enum
{
    PORTABLE_TIME_TRIAL_EVENT_PREVIOUS = 1,
    PORTABLE_TIME_TRIAL_EVENT_NEXT,
    PORTABLE_TIME_TRIAL_EVENT_CONFIRM,
    PORTABLE_TIME_TRIAL_EVENT_BACK,
    PORTABLE_TIME_TRIAL_EVENT_RACE_FINISHED,
    PORTABLE_TIME_TRIAL_EVENT_RESTART
} PortableTimeTrialEvent;

typedef struct
{
    const uint32_t *course_ids;
    size_t course_count;
    const uint32_t *character_ids;
    size_t character_count;
    size_t course_index;
    size_t character_index;
    PortableTimeTrialPhase phase;
    uint32_t result_ticks;
} PortableTimeTrialFlow;

int portable_time_trial_flow_init(PortableTimeTrialFlow *flow,
                                  const uint32_t *course_ids,
                                  size_t course_count,
                                  const uint32_t *character_ids,
                                  size_t character_count);
int portable_time_trial_flow_event(PortableTimeTrialFlow *flow,
                                   PortableTimeTrialEvent event,
                                   uint32_t result_ticks);
uint32_t portable_time_trial_flow_course(const PortableTimeTrialFlow *flow);
uint32_t portable_time_trial_flow_character(const PortableTimeTrialFlow *flow);

#endif
