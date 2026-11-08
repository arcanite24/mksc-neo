#include "time_trial_ghost_playback.h"

#include <stddef.h>
#include <string.h>

static int init_decoder(PortableTimeTrialGhostPlayback *playback)
{
    return portable_time_trial_ghost_decoder_init(
        &playback->decoder,
        playback->record + PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
        PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE);
}

PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_init(
    PortableTimeTrialGhostPlayback *playback,
    const PortableRecoveredRaceView *race,
    const uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE])
{
    if (playback == NULL || race == NULL || record == NULL)
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_INVALID_ARGUMENT;
    if (portable_time_trial_ghost_record_time(record) == 0u ||
        portable_time_trial_ghost_record_course(record) !=
            race->rules.compatibility.course_id ||
        portable_time_trial_ghost_record_driver(record) !=
            race->rules.compatibility.character_class ||
        portable_time_trial_ghost_record_roster_driver(record) !=
            race->rules.compatibility.roster_driver_id)
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_INCOMPATIBLE_RECORD;
    /* Accept an owned record too; clear the remaining destination only
     * after preserving the validated input without a second session owner. */
    memmove(playback->record, record, sizeof playback->record);
    memset(playback, 0, offsetof(PortableTimeTrialGhostPlayback, record));
    memset(&playback->decoded_ticks, 0,
           sizeof *playback - offsetof(PortableTimeTrialGhostPlayback, decoded_ticks));
    if (!init_decoder(playback) ||
        portable_recovered_time_trial_init(&playback->session, race) !=
            PORTABLE_RECOVERED_TIME_TRIAL_OK)
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_RUNTIME_ERROR;

    /* Playback owns the same initial inventory as the recorded driver.
     * Source item edges must reproduce boosts without touching player state.
     * Only nested ghost capture is disabled. */
    playback->session.ghost_recording_valid = 0u;
    playback->session.runtime.recovered_driver = &playback->session.driver;
    if (!init_decoder(playback))
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_RUNTIME_ERROR;
    return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK;
}

PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_tick(
    PortableTimeTrialGhostPlayback *playback,
    PortableRaceTickDisposition *disposition)
{
    uint16_t source_control;

    if (playback == NULL || disposition == NULL)
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_INVALID_ARGUMENT;
    if (playback->paused)
    {
        *disposition = playback->session.runtime.session.phase ==
                               PORTABLE_RACE_PHASE_FINISHED
                           ? PORTABLE_RACE_TICK_FINISHED
                           : PORTABLE_RACE_TICK_INVALID;
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK;
    }
    if (playback->session.runtime.session.phase ==
        PORTABLE_RACE_PHASE_FINISHED)
    {
        *disposition = PORTABLE_RACE_TICK_FINISHED;
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK;
    }
    if (playback->ended ||
        !portable_time_trial_ghost_decoder_next(
            &playback->decoder, &source_control))
    {
        playback->ended = 1u;
        source_control = 0u;
    }
    if (portable_recovered_time_trial_tick_source(
            &playback->session, source_control, disposition) !=
        PORTABLE_RECOVERED_TIME_TRIAL_OK)
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_RUNTIME_ERROR;
    if (!playback->ended)
        ++playback->decoded_ticks;
    return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK;
}

PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_pause(
    PortableTimeTrialGhostPlayback *playback, int paused)
{
    if (playback == NULL)
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_INVALID_ARGUMENT;
    playback->paused = (uint8_t)(paused != 0);
    return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK;
}

PortableTimeTrialGhostPlaybackStatus portable_time_trial_ghost_playback_restart(
    PortableTimeTrialGhostPlayback *playback)
{
    if (playback == NULL)
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_INVALID_ARGUMENT;
    if (portable_recovered_time_trial_restart(&playback->session) !=
            PORTABLE_RECOVERED_TIME_TRIAL_OK ||
        !init_decoder(playback))
        return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_RUNTIME_ERROR;
    playback->session.ghost_recording_valid = 0u;
    playback->decoded_ticks = 0u;
    playback->paused = 0u;
    playback->ended = 0u;
    return PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK;
}

const char *portable_time_trial_ghost_playback_status_name(
    PortableTimeTrialGhostPlaybackStatus status)
{
    static const char *const names[] = {
        "ok", "invalid_argument", "incompatible_record",
        "runtime_error",
    };
    return (unsigned)status < sizeof names / sizeof names[0]
        ? names[status] : "invalid_status";
}
