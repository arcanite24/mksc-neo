#ifndef PORTABLE_TIME_TRIAL_GHOST_EXCHANGE_H
#define PORTABLE_TIME_TRIAL_GHOST_EXCHANGE_H

#include "asset_pack.h"
#include "time_trial_ghost_store.h"

enum { PORTABLE_TIME_TRIAL_RECEIVED_GHOST_SLOTS = 2 };

typedef struct
{
    PortableTimeTrialGhostStoreStatus status;
    PortableRaceCompatibilityId identity;
    uint16_t ticks;
    uint8_t mods_differ; /* incompatible because it was recorded with other mods */
} PortableGhostExchangeInfo;

/* Source DC28 reserves slots 10 and 11 independently of the ten local slots. */
int portable_time_trial_ghost_received_path(char path[4096],const char *directory,unsigned slot);
PortableGhostExchangeInfo portable_time_trial_ghost_exchange_inspect(
    const PortableAssetPackView *pack,const char *path,
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);
PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_receive(
    const PortableAssetPackView *pack,const char *incoming,const char *directory,unsigned slot);
PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_export(
    const PortableAssetPackView *pack,const char *directory,uint32_t course);
int portable_time_trial_ghost_received_erase(const char *directory,unsigned slot);

#endif
