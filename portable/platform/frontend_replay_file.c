#include "frontend_replay_file.h"

static int file_read(void *context, uint8_t *byte)
{
    FILE *file = context;
    const int value = fgetc(file);
    if (value == EOF) return ferror(file) ? -1 : 0;
    *byte = (uint8_t)value;
    return 1;
}

static size_t file_write(void *context, const char *bytes, size_t size)
{
    FILE *file = context;
    const size_t written = fwrite(bytes, 1u, size, file);
    return ferror(file) ? 0u : written;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_header_file(
    FILE *file)
{
    PortableReplayWriter file_stream = {file, file_write, 0};
    const PortableFrontendInputReplayStatus status = portable_frontend_input_replay_write_header(
        file != NULL ? &file_stream : NULL);
    return status == PORTABLE_FRONTEND_INPUT_REPLAY_OK && ferror(file) ?
        PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR : status;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_ghost_choices_file(
    FILE *file,uint8_t mask)
{
    PortableReplayWriter file_stream = {file, file_write, 0};
    const PortableFrontendInputReplayStatus status = portable_frontend_input_replay_write_ghost_choices(
        file != NULL ? &file_stream : NULL, mask);
    return status == PORTABLE_FRONTEND_INPUT_REPLAY_OK && ferror(file) ?
        PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR : status;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_tick_file(
    FILE *file, uint32_t index, uint16_t buttons,
    const PortableFrontendFlow *flow)
{
    PortableReplayWriter file_stream = {file, file_write, 0};
    const PortableFrontendInputReplayStatus status = portable_frontend_input_replay_write_tick(
        file != NULL ? &file_stream : NULL, index, buttons, flow);
    return status == PORTABLE_FRONTEND_INPUT_REPLAY_OK && ferror(file) ?
        PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR : status;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_cancel_file(
    FILE *file, const PortableFrontendFlow *flow)
{
    PortableReplayWriter file_stream = {file, file_write, 0};
    const PortableFrontendInputReplayStatus status = portable_frontend_input_replay_write_cancel(
        file != NULL ? &file_stream : NULL, flow);
    return status == PORTABLE_FRONTEND_INPUT_REPLAY_OK && ferror(file) ?
        PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR : status;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_write_consume_file(
    FILE *file, PortableFrontendFlow *flow)
{
    PortableReplayWriter file_stream = {file, file_write, 0};
    const PortableFrontendInputReplayStatus status = portable_frontend_input_replay_write_consume(
        file != NULL ? &file_stream : NULL, flow);
    return status == PORTABLE_FRONTEND_INPUT_REPLAY_OK && ferror(file) ?
        PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR : status;
}

PortableFrontendInputReplayStatus portable_frontend_input_replay_read_file(
    FILE *file, PortableFrontendFlow *flow, uint32_t *out_tick_count,
    int *out_cancelled)
{
    PortableReplayReader file_stream = {file, file_read, 0};
    return portable_frontend_input_replay_read(
        file != NULL ? &file_stream : NULL, flow, out_tick_count, out_cancelled);
}
