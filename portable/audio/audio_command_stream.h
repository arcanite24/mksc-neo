#ifndef PORTABLE_AUDIO_COMMAND_STREAM_H
#define PORTABLE_AUDIO_COMMAND_STREAM_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_PLAYER_COUNT 11u
#define PORTABLE_AUDIO_SONG_COUNT 403u
#define PORTABLE_AUDIO_PLAYER_AUTO UINT8_C(0xff)
/* START_SONG arguments with this bit name a kind-112 NDS sequence section
 * (content ID = argument & 0x7fff) instead of a kind-16 song. */
#define PORTABLE_AUDIO_NDS_SONG UINT16_C(0x8000)
/* Arguments 0x4000..0x7fff name a kind-115 N64 song (content ID = argument
 * & 0x1fff); with 0x2000 also set, sound `argument & 0xff` of bank
 * `(argument >> 8) & 0x1f` played by the effect sequence (content ID 1). */
#define PORTABLE_AUDIO_N64_SONG UINT16_C(0x4000)
#define PORTABLE_AUDIO_N64_EFFECT UINT16_C(0x2000)
#define PORTABLE_AUDIO_IS_N64_SONG(argument) (((argument) & 0xc000u) == PORTABLE_AUDIO_N64_SONG)

typedef enum
{
    PORTABLE_AUDIO_COMMAND_START_SONG = 1,
    PORTABLE_AUDIO_COMMAND_STOP_PLAYER,
    PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER,
    PORTABLE_AUDIO_COMMAND_RESUME_PLAYER,
    PORTABLE_AUDIO_COMMAND_FADE_PLAYER,
    PORTABLE_AUDIO_COMMAND_SET_VOLUME,
    PORTABLE_AUDIO_COMMAND_SET_PITCH,
    PORTABLE_AUDIO_COMMAND_SET_TEMPO
} PortableAudioCommandOperation;

typedef struct
{
    uint32_t tick;
    uint32_t sequence;
    uint16_t argument;
    uint8_t operation;
    uint8_t player;
} PortableAudioCommand;

typedef enum
{
    PORTABLE_AUDIO_STREAM_OK = 0,
    PORTABLE_AUDIO_STREAM_INVALID_ARGUMENT,
    PORTABLE_AUDIO_STREAM_BAD_OPERATION,
    PORTABLE_AUDIO_STREAM_BAD_PLAYER,
    PORTABLE_AUDIO_STREAM_BAD_SONG,
    PORTABLE_AUDIO_STREAM_BAD_PARAMETER,
    PORTABLE_AUDIO_STREAM_TICK_REGRESSION,
    PORTABLE_AUDIO_STREAM_FULL
} PortableAudioStreamStatus;

typedef struct
{
    PortableAudioCommand *commands;
    size_t capacity;
    size_t count;
    uint32_t last_tick;
    PortableAudioStreamStatus status;
} PortableAudioCommandStream;

PortableAudioStreamStatus portable_audio_command_stream_init(
    PortableAudioCommandStream *stream, PortableAudioCommand *storage,
    size_t capacity);
PortableAudioStreamStatus portable_audio_command_stream_emit(
    PortableAudioCommandStream *stream, uint32_t tick,
    PortableAudioCommandOperation operation, uint8_t player,
    uint16_t argument);
uint32_t portable_audio_command_stream_checksum(
    const PortableAudioCommandStream *stream);
const char *portable_audio_stream_status_name(PortableAudioStreamStatus status);

#endif
