#include "recovered_replay_file.h"

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
    return fwrite(bytes, 1u, size, context);
}

int portable_recovered_replay_inspect_header_file(FILE *input, PortableRecoveredReplayHeader *out)
{
    PortableReplayReader stream = {input, file_read, 0};
    return portable_recovered_replay_inspect_header(input != NULL ? &stream : NULL, out);
}

int portable_recovered_replay_write_header_file(
    FILE *output, const PortableRecoveredTimeTrialSession *session)
{
    PortableReplayWriter output_stream = {output, file_write, 0};
    return portable_recovered_replay_write_header(output != NULL ? &output_stream : NULL, session);
}

int portable_recovered_replay_write_action_file(
    FILE *output, PortableRecoveredReplayAction action, int value)
{
    PortableReplayWriter output_stream = {output, file_write, 0};
    return portable_recovered_replay_write_action(output != NULL ? &output_stream : NULL, action, value);
}

int portable_recovered_replay_write_tick_file(
    FILE *output, uint32_t tick, uint16_t semantic_buttons,
    PortableRaceTickDisposition disposition,
    const PortableRecoveredTimeTrialSession *session)
{
    PortableReplayWriter output_stream = {output, file_write, 0};
    return portable_recovered_replay_write_tick(
        output != NULL ? &output_stream : NULL, tick, semantic_buttons, disposition, session);
}

int portable_recovered_replay_run_file(
    FILE *input, PortableRecoveredTimeTrialSession *session,
    FILE *diagnostics, size_t *out_tick_count)
{
    PortableReplayReader input_stream = {input, file_read, 0};
    PortableReplayWriter diagnostics_stream = {diagnostics, file_write, 0};
    return portable_recovered_replay_run(
        input != NULL ? &input_stream : NULL, session, diagnostics != NULL ? &diagnostics_stream : NULL, out_tick_count);
}
