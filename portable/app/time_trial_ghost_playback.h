#ifndef PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_H
#define PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_H

#include <stdint.h>

#include "recovered_time_trial_session.h"
#include "time_trial_ghost.h"

typedef enum
{
    PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK = 0,
    PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_INVALID_ARGUMENT,
    PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_INCOMPATIBLE_RECORD,
    PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_RUNTIME_ERROR,
} PortableTimeTrialGhostPlaybackStatus;

/* A ghost owns a complete race/session copy. Its track response, checkpoints
 * and finish clock are therefore deterministic without ever sharing mutable
 * player, item, rank or result state. */
typedef struct
{
    PortableRecoveredTimeTrialSession session;
    PortableTimeTrialGhostDecoder decoder;
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint32_t decoded_ticks;
    uint8_t paused;
    uint8_t ended;
} PortableTimeTrialGhostPlayback;

/* Initialization writes the supplied owner in place. After failure, discard
 * or reinitialize it before use. Race views and their borrowed payloads must
 * not overlap the destination and must remain valid for its lifetime. */
PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_init(
    PortableTimeTrialGhostPlayback *playback,
    const PortableRecoveredRaceView *race,
    const uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);

PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_tick(
    PortableTimeTrialGhostPlayback *playback,
    PortableRaceTickDisposition *disposition);

PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_pause(
    PortableTimeTrialGhostPlayback *playback, int paused);

PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_restart(
    PortableTimeTrialGhostPlayback *playback);

const char *portable_time_trial_ghost_playback_status_name(
    PortableTimeTrialGhostPlaybackStatus status);

#endif
