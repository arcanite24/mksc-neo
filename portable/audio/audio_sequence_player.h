#ifndef PORTABLE_AUDIO_SEQUENCE_PLAYER_H
#define PORTABLE_AUDIO_SEQUENCE_PLAYER_H

#include "audio_sequence_asset.h"

#include <stdint.h>

typedef enum
{
    PORTABLE_AUDIO_PLAYER_OK = 0,
    PORTABLE_AUDIO_PLAYER_INVALID_ARGUMENT,
    PORTABLE_AUDIO_PLAYER_TICK_REGRESSION,
    PORTABLE_AUDIO_PLAYER_TICK_OVERFLOW,
    PORTABLE_AUDIO_PLAYER_EVENT_BUDGET,
    PORTABLE_AUDIO_PLAYER_BAD_SEQUENCE,
    PORTABLE_AUDIO_PLAYER_EVENT_SINK
} PortableAudioPlayerStatus;

typedef struct
{
    uint32_t cursor;
    uint32_t tick_bias;
    uint32_t note_count;
    uint16_t voice;
    uint8_t volume;
    uint8_t pan;
    uint8_t last_key;
    uint8_t last_velocity;
    uint8_t active;
} PortableAudioTrackState;

typedef int (*PortableAudioEventSink)(void *user,uint32_t tick,
    const PortableAudioSequenceEvent *event,
    const PortableAudioTrackState *track_state);

typedef struct
{
    const PortableAudioSequenceView *sequence;
    PortableAudioTrackState tracks[PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS];
    uint8_t memory[256]; /* standalone sequence fallback */
    uint8_t *memory_area; /* optional shared MPlayMemAccArea; native pointer only */
    uint32_t current_tick;
    uint32_t events_processed;
    uint16_t tempo;
    PortableAudioPlayerStatus status;
} PortableAudioSequencePlayer;

PortableAudioPlayerStatus portable_audio_sequence_player_init(
    PortableAudioSequencePlayer *player,
    const PortableAudioSequenceView *sequence);
PortableAudioPlayerStatus portable_audio_sequence_player_advance_to(
    PortableAudioSequencePlayer *player, uint32_t target_tick,
    uint32_t event_budget);
PortableAudioPlayerStatus portable_audio_sequence_player_advance_to_sink(
    PortableAudioSequencePlayer *player, uint32_t target_tick,
    uint32_t event_budget, PortableAudioEventSink sink, void *user);
uint32_t portable_audio_sequence_player_checksum(
    const PortableAudioSequencePlayer *player);
const char *portable_audio_player_status_name(PortableAudioPlayerStatus status);

#endif
