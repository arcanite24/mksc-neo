#ifndef PORTABLE_TIME_TRIAL_RECORDS_H
#define PORTABLE_TIME_TRIAL_RECORDS_H

#include "frontend_presentation.h"
#include "personal_best.h"
#include "gp_results_flow.h"
#include "time_trial_ghost_exchange.h"
#include "time_trial_ghost_library.h"
#include "records_ghost_asset.h"

typedef struct
{
    uint32_t course_id;
    PortablePersonalBestTable table; /* Actual runs only; never seeded targets. */
    PortableGpResultsRecord displayed_records[6];
    uint8_t displayed_records_ready;
    PortablePersonalBestStatus status;
    PortableTimeTrialGhostStoreStatus ghost_status;
    PortablePersonalBestRecord ghost; /* CD00: saved ghost driver and total. */
    PortableGhostExchangeInfo received[PORTABLE_TIME_TRIAL_RECEIVED_GHOST_SLOTS];
    const char *notice;
    uint8_t erase_failed;
    uint8_t ghost_mods_differ; /* local ghost was recorded with other mods */
    PortableGhostLibraryInventory local_ghosts;
    PortableRecordsGhostRules ghost_rules;
    uint8_t ghost_rules_ready,received_used;
} PortableTimeTrialRecords;

int portable_time_trial_records_load(const PortableAssetPackView *pack,
    uint32_t course_id,uint32_t character_id,const char *directory,
    PortableTimeTrialRecords *out);
int portable_time_trial_records_erase(const char *directory,uint32_t course_id);
int portable_time_trial_records_erase_ghost(const char *directory,uint32_t course_id);
uint8_t portable_time_trial_records_ghost_choices(const PortableTimeTrialRecords *records);
PortableFrontendPresentationStatus portable_time_trial_ghost_choice_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableTimeTrialRecords *records,PortableSoftwareFrame *frame);
PortableFrontendPresentationStatus portable_time_trial_records_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableTimeTrialRecords *records,PortableSoftwareFrame *frame);

#endif
