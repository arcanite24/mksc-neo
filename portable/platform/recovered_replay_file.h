#ifndef PORTABLE_RECOVERED_REPLAY_FILE_H
#define PORTABLE_RECOVERED_REPLAY_FILE_H

#include "../app/recovered_time_trial_replay.h"
#include <stdio.h>

int portable_recovered_replay_inspect_header_file(FILE *input, PortableRecoveredReplayHeader *out);

int portable_recovered_replay_write_header_file(
    FILE *output, const PortableRecoveredTimeTrialSession *session);
int portable_recovered_replay_write_action_file(
    FILE *output, PortableRecoveredReplayAction action, int value);
int portable_recovered_replay_write_tick_file(
    FILE *output, uint32_t tick, uint16_t semantic_buttons,
    PortableRaceTickDisposition disposition,
    const PortableRecoveredTimeTrialSession *session);
int portable_recovered_replay_run_file(
    FILE *input, PortableRecoveredTimeTrialSession *session,
    FILE *diagnostics, size_t *out_tick_count);

#endif
