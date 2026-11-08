#ifndef PORTABLE_TIME_TRIAL_RESULT_H
#define PORTABLE_TIME_TRIAL_RESULT_H

#include "personal_best.h"
#include "time_trial_record_defaults.h"
#include "recovered_time_trial_session.h"

/* A complete, internally consistent finish, with positive laps and an exact total. */
int portable_time_trial_result_is_valid(
    const PortableRecoveredTimeTrialSnapshot *snapshot);

/* C38C finish theme: matching either pre-race record also qualifies. */
int portable_time_trial_finish_record(const PortableRecoveredTimeTrialSnapshot *snapshot,
    const PortablePersonalBestTable *records);

/* Imported initial targets participate in the original inclusive jingle test;
 * they are never written into the actual-run personal-best file. */
int portable_time_trial_finish_record_with_defaults(const PortableRecoveredTimeTrialSnapshot *snapshot,
    const PortablePersonalBestTable *records,const PortableTimeTrialRecordDefaults *defaults);

PortablePersonalBestStatus portable_time_trial_personal_best_update(
    const char *path,
    const PortableRecoveredTimeTrialSnapshot *snapshot,
    uint32_t *out_best_ticks,
    int *out_updated);

#endif
