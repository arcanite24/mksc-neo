#ifndef PORTABLE_TIME_TRIAL_GHOST_STORE_H
#define PORTABLE_TIME_TRIAL_GHOST_STORE_H

#include <stdint.h>

#include "mod_identity.h"
#include "race_session.h"
#include "time_trial_ghost.h"

typedef enum
{
    PORTABLE_TIME_TRIAL_GHOST_STORE_OK = 0,
    PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND,
    PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID,
    PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR,
    PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE,
} PortableTimeTrialGhostStoreStatus;

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_load(
    const char *path,
    const PortableRaceCompatibilityId *expected,
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);

PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_update(
    const char *path,
    const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    int *out_updated);

/* Explicit results Save Ghost replaces this course even with a slower run.
 * Invalid/incompatible prior files remain errors until the player erases them. */
PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_save(
    const char *path,const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);

/* Inspect validates the envelope without assuming a course. Callers must still
 * check the returned identity against installed content before receiving it. */
PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_inspect(
    const char *path,PortableRaceCompatibilityId *identity,
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);
/* The mod set a ghost file was recorded with (empty for stock/version 1). */
PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_mods(
    const char *path,PortableModIdentity *mods);
/* When a valid ghost file's mod set differs from the enabled one, writes
 * "recorded with mods [...]; enabled mods [...]" and returns 1. */
int portable_time_trial_ghost_store_describe_refusal(const char *path,char *text,size_t capacity);
/* Only for a player-confirmed received-slot replacement or exported copy. */
PortableTimeTrialGhostStoreStatus portable_time_trial_ghost_store_replace(
    const char *path,const PortableRaceCompatibilityId *identity,
    const uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);

#endif
