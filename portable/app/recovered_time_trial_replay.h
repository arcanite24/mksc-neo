#ifndef PORTABLE_RECOVERED_TIME_TRIAL_REPLAY_H
#define PORTABLE_RECOVERED_TIME_TRIAL_REPLAY_H

#include <stddef.h>
#include <stdint.h>
#include "replay_stream.h"

#include "recovered_time_trial_session.h"

#define PORTABLE_RECOVERED_REPLAY_VERSION 66u
#define PORTABLE_RECOVERED_REPLAY_MODERN_VERSION 67u
#define PORTABLE_RECOVERED_REPLAY_AIR_VERSION 68u

typedef struct {
    PortableRaceCompatibilityId compatibility;
    uint32_t version,initial_checksum;
} PortableRecoveredReplayHeader;
/* Consume only the first line. On failure out is unchanged. The caller can
 * select content/rules from compatibility, then reset its reader before run. */
int portable_recovered_replay_inspect_header(PortableReplayReader *input,
    PortableRecoveredReplayHeader *out);

typedef enum
{
    PORTABLE_RECOVERED_REPLAY_PAUSE = 1,
    PORTABLE_RECOVERED_REPLAY_RESTART,
    PORTABLE_RECOVERED_REPLAY_RESCUE
} PortableRecoveredReplayAction;

uint32_t portable_recovered_replay_state_checksum(
    const PortableRecoveredTimeTrialSession *session);

int portable_recovered_replay_write_header(
    PortableReplayWriter *output, const PortableRecoveredTimeTrialSession *session);
int portable_recovered_replay_write_action(
    PortableReplayWriter *output, PortableRecoveredReplayAction action, int value);
int portable_recovered_replay_write_tick(
    PortableReplayWriter *output, uint32_t tick, uint16_t semantic_buttons,
    PortableRaceTickDisposition disposition,
    const PortableRecoveredTimeTrialSession *session);

/* Replays semantic input through portable_recovered_time_trial_tick and stops
 * at the first malformed record or named state-field divergence. */
int portable_recovered_replay_run(
    PortableReplayReader *input, PortableRecoveredTimeTrialSession *session,
    PortableReplayWriter *diagnostics, size_t *out_tick_count);

#endif
