#include "audio_command_stream.h"

static int valid_player(uint8_t player)
{
    return player < PORTABLE_AUDIO_PLAYER_COUNT;
}

static PortableAudioStreamStatus validate(
    PortableAudioCommandOperation operation, uint8_t player,
    uint16_t argument)
{
    switch (operation) {
    case PORTABLE_AUDIO_COMMAND_START_SONG:
        if (player != PORTABLE_AUDIO_PLAYER_AUTO && !valid_player(player))
            return PORTABLE_AUDIO_STREAM_BAD_PLAYER;
        if (argument >= PORTABLE_AUDIO_SONG_COUNT && !(argument & (PORTABLE_AUDIO_NDS_SONG | PORTABLE_AUDIO_N64_SONG)))
            return PORTABLE_AUDIO_STREAM_BAD_SONG;
        return PORTABLE_AUDIO_STREAM_OK;
    case PORTABLE_AUDIO_COMMAND_STOP_PLAYER:
    case PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER:
    case PORTABLE_AUDIO_COMMAND_RESUME_PLAYER:
        if (!valid_player(player))
            return PORTABLE_AUDIO_STREAM_BAD_PLAYER;
        return argument == 0u ? PORTABLE_AUDIO_STREAM_OK :
            PORTABLE_AUDIO_STREAM_BAD_PARAMETER;
    case PORTABLE_AUDIO_COMMAND_FADE_PLAYER:
        if (!valid_player(player))
            return PORTABLE_AUDIO_STREAM_BAD_PLAYER;
        return argument != 0u ? PORTABLE_AUDIO_STREAM_OK :
            PORTABLE_AUDIO_STREAM_BAD_PARAMETER;
    case PORTABLE_AUDIO_COMMAND_SET_VOLUME:
        if (!valid_player(player))
            return PORTABLE_AUDIO_STREAM_BAD_PLAYER;
        return argument <= UINT16_C(0x03ff) ? PORTABLE_AUDIO_STREAM_OK :
            PORTABLE_AUDIO_STREAM_BAD_PARAMETER;
    case PORTABLE_AUDIO_COMMAND_SET_TEMPO:
    case PORTABLE_AUDIO_COMMAND_SET_PITCH:
        return valid_player(player) ? PORTABLE_AUDIO_STREAM_OK :
            PORTABLE_AUDIO_STREAM_BAD_PLAYER;
    default:
        return PORTABLE_AUDIO_STREAM_BAD_OPERATION;
    }
}

PortableAudioStreamStatus portable_audio_command_stream_init(
    PortableAudioCommandStream *stream, PortableAudioCommand *storage,
    size_t capacity)
{
    if (stream == NULL || (capacity != 0u && storage == NULL))
        return PORTABLE_AUDIO_STREAM_INVALID_ARGUMENT;
    stream->commands = storage;
    stream->capacity = capacity;
    stream->count = 0u;
    stream->last_tick = 0u;
    stream->status = PORTABLE_AUDIO_STREAM_OK;
    return PORTABLE_AUDIO_STREAM_OK;
}

PortableAudioStreamStatus portable_audio_command_stream_emit(
    PortableAudioCommandStream *stream, uint32_t tick,
    PortableAudioCommandOperation operation, uint8_t player,
    uint16_t argument)
{
    PortableAudioStreamStatus status;
    PortableAudioCommand *command;
    if (stream == NULL || (stream->capacity != 0u && stream->commands == NULL))
        return PORTABLE_AUDIO_STREAM_INVALID_ARGUMENT;
    if (stream->status != PORTABLE_AUDIO_STREAM_OK)
        return stream->status;
    status = validate(operation, player, argument);
    if (status != PORTABLE_AUDIO_STREAM_OK)
        return status;
    if (stream->count != 0u && tick < stream->last_tick)
        return PORTABLE_AUDIO_STREAM_TICK_REGRESSION;
    if (stream->count == stream->capacity) {
        stream->status = PORTABLE_AUDIO_STREAM_FULL;
        return stream->status;
    }
    command = &stream->commands[stream->count];
    command->tick = tick;
    command->sequence = (uint32_t)stream->count;
    command->argument = argument;
    command->operation = (uint8_t)operation;
    command->player = player;
    ++stream->count;
    stream->last_tick = tick;
    return PORTABLE_AUDIO_STREAM_OK;
}

static uint32_t hash_byte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * UINT32_C(16777619);
}

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
    hash = hash_byte(hash, (uint8_t)value);
    hash = hash_byte(hash, (uint8_t)(value >> 8));
    hash = hash_byte(hash, (uint8_t)(value >> 16));
    return hash_byte(hash, (uint8_t)(value >> 24));
}

uint32_t portable_audio_command_stream_checksum(
    const PortableAudioCommandStream *stream)
{
    uint32_t hash = UINT32_C(2166136261);
    size_t i;
    if (stream == NULL || (stream->count != 0u && stream->commands == NULL) ||
        stream->count > stream->capacity)
        return 0u;
    hash = hash_u32(hash, (uint32_t)stream->count);
    for (i = 0u; i < stream->count; ++i) {
        const PortableAudioCommand *command = &stream->commands[i];
        hash = hash_u32(hash, command->tick);
        hash = hash_u32(hash, command->sequence);
        hash = hash_byte(hash, command->operation);
        hash = hash_byte(hash, command->player);
        hash = hash_byte(hash, (uint8_t)command->argument);
        hash = hash_byte(hash, (uint8_t)(command->argument >> 8));
    }
    return hash;
}

const char *portable_audio_stream_status_name(PortableAudioStreamStatus status)
{
    static const char *const names[] = {
        "ok", "invalid argument", "bad operation", "bad player", "bad song",
        "bad parameter", "tick regression", "full"
    };
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] :
        "unknown status";
}
