#ifndef PORTABLE_AUDIO_SEQUENCE_ASSET_H
#define PORTABLE_AUDIO_SEQUENCE_ASSET_H

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_SEQUENCE_HEADER_SIZE 32u
#define PORTABLE_AUDIO_SEQUENCE_EVENT_SIZE 16u
#define PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS 16u

typedef enum
{
    PORTABLE_AUDIO_EVENT_END = 1,
    PORTABLE_AUDIO_EVENT_TEMPO,
    PORTABLE_AUDIO_EVENT_PRIORITY,
    PORTABLE_AUDIO_EVENT_KEY_SHIFT,
    PORTABLE_AUDIO_EVENT_VOICE,
    PORTABLE_AUDIO_EVENT_VOLUME,
    PORTABLE_AUDIO_EVENT_PAN,
    PORTABLE_AUDIO_EVENT_BEND,
    PORTABLE_AUDIO_EVENT_BEND_RANGE,
    PORTABLE_AUDIO_EVENT_LFO_SPEED,
    PORTABLE_AUDIO_EVENT_LFO_DELAY,
    PORTABLE_AUDIO_EVENT_MOD_DEPTH,
    PORTABLE_AUDIO_EVENT_MOD_TYPE,
    PORTABLE_AUDIO_EVENT_TUNE,
    PORTABLE_AUDIO_EVENT_NOTE,
    PORTABLE_AUDIO_EVENT_TIE,
    PORTABLE_AUDIO_EVENT_END_TIE,
    PORTABLE_AUDIO_EVENT_MEMORY_SET,
    PORTABLE_AUDIO_EVENT_LOOP,
    PORTABLE_AUDIO_EVENT_LOOP_POINT
} PortableAudioSequenceOperation;

typedef enum
{
    PORTABLE_AUDIO_SEQUENCE_OK = 0,
    PORTABLE_AUDIO_SEQUENCE_INVALID_ARGUMENT,
    PORTABLE_AUDIO_SEQUENCE_BAD_MAGIC,
    PORTABLE_AUDIO_SEQUENCE_BAD_VERSION,
    PORTABLE_AUDIO_SEQUENCE_BAD_SIZE,
    PORTABLE_AUDIO_SEQUENCE_BAD_SONG,
    PORTABLE_AUDIO_SEQUENCE_BAD_TRACK,
    PORTABLE_AUDIO_SEQUENCE_BAD_ORDER,
    PORTABLE_AUDIO_SEQUENCE_BAD_OPERATION,
    PORTABLE_AUDIO_SEQUENCE_BAD_PARAMETER,
    PORTABLE_AUDIO_SEQUENCE_BAD_TARGET,
    PORTABLE_AUDIO_SEQUENCE_UNTERMINATED_TRACK
} PortableAudioSequenceStatus;

typedef struct
{
    const uint8_t *data;
    size_t size;
    uint16_t song_id;
    uint16_t bank_id;
    uint8_t track_count;
    uint8_t priority;
    uint8_t reverb;
    uint8_t player_index; /* v2 imported routing; v1 is AUTO */
    uint32_t event_count;
    uint32_t events_offset;
} PortableAudioSequenceView;

typedef struct
{
    uint32_t tick;
    uint32_t target;
    uint16_t value;
    uint8_t track;
    uint8_t operation;
    uint8_t arg0;
    uint8_t arg1;
} PortableAudioSequenceEvent;

PortableAudioSequenceStatus portable_audio_sequence_open(
    PortableAudioSequenceView *view, const uint8_t *data, size_t size);
PortableAudioSequenceStatus portable_audio_sequence_event(
    const PortableAudioSequenceView *view, uint32_t index,
    PortableAudioSequenceEvent *event);
const char *portable_audio_sequence_status_name(
    PortableAudioSequenceStatus status);

#endif
