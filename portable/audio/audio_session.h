#ifndef PORTABLE_AUDIO_SESSION_H
#define PORTABLE_AUDIO_SESSION_H

#include "audio_command_stream.h"
#include "audio_pack_song.h"
#include "nds_sequence.h"
#include "n64_sequence.h"

#include <stdint.h>

typedef enum
{
    PORTABLE_AUDIO_SESSION_OK = 0,
    PORTABLE_AUDIO_SESSION_INVALID_ARGUMENT,
    PORTABLE_AUDIO_SESSION_BAD_COMMAND,
    PORTABLE_AUDIO_SESSION_BAD_SONG,
    PORTABLE_AUDIO_SESSION_RENDER_ERROR,
    PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES
} PortableAudioSessionStatus;

typedef struct
{
    PortableAudioPackSong song;
    uint16_t song_id;
    uint16_t volume;
    uint16_t fade_volume;
    uint16_t fade_interval;
    uint16_t fade_counter;
    int16_t pitch;
    uint8_t active;
    uint8_t paused;
    uint8_t start_pending;
    uint8_t nds; /* plays the session's NDS sequence instead of `song` */
    uint8_t n64; /* plays the session's N64 sequence instead of `song` */
    uint16_t follow; /* N64/NDS: the song that starts when this one ends */
} PortableAudioSessionPlayer;

typedef struct
{
    PortableAudioTuning tuning;
    const PortableAssetPackView *pack;
    PortableAudioSessionPlayer players[PORTABLE_AUDIO_PLAYER_COUNT];
    uint8_t priority_guard[PORTABLE_AUDIO_PLAYER_COUNT];
    PortableAudioPcmMixer pcm;
    PortableAudioPsgMixer psg;
    uint8_t memory[256]; /* shared sequencer bytes; preserved by song/player stops */
    PortableNdsPlayer nds[PORTABLE_AUDIO_PLAYER_COUNT]; /* each slot's NDS sequence */
    PortableN64Player n64[PORTABLE_AUDIO_PLAYER_COUNT]; /* each slot's N64 sequence */
    uint32_t rendered_frames;
    uint32_t applied_commands;
    PortableAudioSessionStatus status;
} PortableAudioSession;

uint8_t portable_audio_session_song_player(const PortableAssetPackView *pack,
    uint16_t song_id);
/* Source mPlay low-halfword status: a loaded song alone is not busy. */
int portable_audio_session_player_playing(
    const PortableAudioSession *session, uint8_t player);
/* Loaded ownership survives a finite sequence ending; busy queries below do not. */
int portable_audio_session_song_loaded(const PortableAudioSession *session,
    uint16_t song_id);
int portable_audio_session_song_playing(
    const PortableAudioSession *session, uint16_t song_id);
PortableAudioSessionStatus portable_audio_session_init(
    PortableAudioSession *session,const PortableAssetPackView *pack);
PortableAudioSessionStatus portable_audio_session_apply(
    PortableAudioSession *session,const PortableAudioCommand *command);
PortableAudioSessionStatus portable_audio_session_render_frame(
    PortableAudioSession *session,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);
void portable_audio_session_stop_all(PortableAudioSession *session);
const char *portable_audio_session_status_name(
    PortableAudioSessionStatus status);

#endif
