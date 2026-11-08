#ifndef PORTABLE_FRONTEND_INPUT_REPLAY_H
#define PORTABLE_FRONTEND_INPUT_REPLAY_H

#include "frontend_flow.h"

#include "replay_stream.h"

typedef enum
{
    PORTABLE_FRONTEND_INPUT_REPLAY_OK = 0,
    PORTABLE_FRONTEND_INPUT_REPLAY_INVALID_ARGUMENT,
    PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR,
    PORTABLE_FRONTEND_INPUT_REPLAY_BAD_HEADER,
    PORTABLE_FRONTEND_INPUT_REPLAY_BAD_RECORD,
    PORTABLE_FRONTEND_INPUT_REPLAY_BAD_SEQUENCE,
    PORTABLE_FRONTEND_INPUT_REPLAY_BAD_BUTTONS,
    PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CHECKSUM,
    PORTABLE_FRONTEND_INPUT_REPLAY_BAD_CANCEL
} PortableFrontendInputReplayStatus;

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_header(
    PortableReplayWriter *writer);
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_ghost_choices(
    PortableReplayWriter *writer,uint8_t mask);
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_tick(
    PortableReplayWriter *writer, uint32_t index, uint16_t buttons,
    const PortableFrontendFlow *flow);
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_cancel(
    PortableReplayWriter *writer, const PortableFrontendFlow *flow);
/* Version2 records an explicit consumed event batch; version1 remains readable. */
PortableFrontendInputReplayStatus portable_frontend_input_replay_write_consume(
    PortableReplayWriter *writer, PortableFrontendFlow *flow);
PortableFrontendInputReplayStatus portable_frontend_input_replay_read(
    PortableReplayReader *reader, PortableFrontendFlow *flow, uint32_t *out_tick_count,
    int *out_cancelled);
const char *portable_frontend_input_replay_status_name(
    PortableFrontendInputReplayStatus status);

#endif
