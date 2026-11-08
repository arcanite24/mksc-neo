#ifndef PORTABLE_AUDIO_PACK_SONG_H
#define PORTABLE_AUDIO_PACK_SONG_H

#include "asset_pack.h"
#include "audio_song_runtime.h"

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_PACK_MAX_SAMPLES 256u

typedef enum
{
    PORTABLE_AUDIO_PACK_SONG_OK = 0,
    PORTABLE_AUDIO_PACK_SONG_INVALID_ARGUMENT,
    PORTABLE_AUDIO_PACK_SONG_MISSING_SEQUENCE,
    PORTABLE_AUDIO_PACK_SONG_BAD_SEQUENCE,
    PORTABLE_AUDIO_PACK_SONG_MISSING_BANK,
    PORTABLE_AUDIO_PACK_SONG_BAD_BANK,
    PORTABLE_AUDIO_PACK_SONG_TOO_MANY_SAMPLES,
    PORTABLE_AUDIO_PACK_SONG_BAD_SAMPLE,
    PORTABLE_AUDIO_PACK_SONG_RUNTIME_ERROR,
    PORTABLE_AUDIO_PACK_SONG_BAD_TUNING
} PortableAudioPackSongStatus;

typedef struct
{
    PortableAudioTuning tuning;
    PortableAudioSequenceView sequence;
    PortableAudioVoiceBankView bank;
    PortableAudioSampleView samples[PORTABLE_AUDIO_PACK_MAX_SAMPLES];
    size_t sample_count;
    PortableAudioSongRuntime runtime;
    PortableAudioPackSongStatus status;
} PortableAudioPackSong;

/* Repair owned views after moving/copying this owner; borrowed pack bytes and
 * explicitly shared mixer/memory contexts keep their existing lifetimes. */
void portable_audio_pack_song_rebind(PortableAudioPackSong *song);
PortableAudioPackSongStatus portable_audio_pack_song_init(
    PortableAudioPackSong *song,const PortableAssetPackView *pack,
    uint16_t song_id);
PortableAudioPackSongStatus portable_audio_pack_song_render_frame(
    PortableAudioPackSong *song,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);
void portable_audio_pack_song_pause(PortableAudioPackSong *song,int paused);
PortableAudioPackSongStatus portable_audio_pack_song_control(
    PortableAudioPackSong *song,uint16_t track_bits,uint16_t volume,
    int16_t pitch);
const char *portable_audio_pack_song_status_name(
    PortableAudioPackSongStatus status);

#endif
