#ifndef PORTABLE_FRONTEND_REPLAY_FILE_H
#define PORTABLE_FRONTEND_REPLAY_FILE_H

#include "../app/frontend_input_replay.h"
#include <stdio.h>

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_header_file(
    FILE *file);
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_ghost_choices_file(
    FILE *file,uint8_t mask);
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_tick_file(
    FILE *file, uint32_t index, uint16_t buttons,
    const PortableFrontendFlow *flow);
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_cancel_file(
    FILE *file, const PortableFrontendFlow *flow);
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_consume_file(
    FILE *file, PortableFrontendFlow *flow);
PortableFrontendInputReplayStatus portable_frontend_input_replay_read_file(
    FILE *file, PortableFrontendFlow *flow, uint32_t *out_tick_count,
    int *out_cancelled);

#endif
