#ifndef PORTABLE_AUDIO_SONG_RUNTIME_H
#define PORTABLE_AUDIO_SONG_RUNTIME_H

#include "audio_pcm_mixer.h"
#include "audio_psg_mixer.h"
#include "audio_sequence_player.h"

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_RUNTIME_NOTES 16u

typedef enum
{
    PORTABLE_AUDIO_RUNTIME_OK = 0,
    PORTABLE_AUDIO_RUNTIME_INVALID_ARGUMENT,
    PORTABLE_AUDIO_RUNTIME_BAD_BANK,
    PORTABLE_AUDIO_RUNTIME_BAD_VOICE,
    PORTABLE_AUDIO_RUNTIME_MISSING_SAMPLE,
    PORTABLE_AUDIO_RUNTIME_PLAYER_ERROR
} PortableAudioRuntimeStatus;

typedef struct
{
    PortableAudioVoice voice;
    PortableAudioVoiceHandle handle;
    uint32_t release_tick;
    uint8_t track;
    uint8_t key;
    uint8_t pitch_key;
    uint8_t velocity;
    uint8_t mixer_kind;
    uint8_t tied;
    uint8_t released;
    uint8_t active;
} PortableAudioRuntimeNote;

typedef struct
{
    int16_t bend;
    int8_t key_shift;
    int8_t tune;
    uint8_t bend_range;
    uint8_t priority;
    uint8_t lfo_speed;
    uint8_t lfo_delay;
    uint8_t lfo_delay_counter;
    uint8_t lfo_phase;
    uint8_t mod_depth;
    uint8_t mod_type;
    int8_t mod_value;
    int16_t pitch_x;
    uint8_t volume_x;
} PortableAudioRuntimeTrack;

typedef struct
{
    PortableAudioSequencePlayer player;
    const PortableAudioVoiceBankView *bank;
    const PortableAudioSampleView *samples;
    size_t sample_count;
    PortableAudioPcmMixer pcm;
    PortableAudioPsgMixer psg;
    PortableAudioPcmMixer *shared_pcm;
    PortableAudioPsgMixer *shared_psg;
    uint16_t owner_base; /* ordered source tracks, zero reserved for detached tails */
    PortableAudioRuntimeTrack tracks[PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS];
    PortableAudioRuntimeNote notes[PORTABLE_AUDIO_RUNTIME_NOTES];
    uint32_t next_tick;
    uint32_t notes_started;
    uint32_t notes_released;
    uint32_t notes_dropped;
    uint16_t tempo_accumulator;
    uint16_t tempo_scale; /* mPlayTempoControl Q8 multiplier; normal256. */
    uint8_t paused;
    PortableAudioRuntimeStatus status;
} PortableAudioSongRuntime;

PortableAudioRuntimeStatus portable_audio_song_runtime_init(
    PortableAudioSongRuntime *runtime,
    const PortableAudioSequenceView *sequence,
    const PortableAudioVoiceBankView *bank,
    const PortableAudioSampleView *samples,size_t sample_count);
PortableAudioRuntimeStatus portable_audio_song_runtime_render_frame(
    PortableAudioSongRuntime *runtime,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);
/* Session advances all players, then mixes its shared voices exactly once. */
PortableAudioRuntimeStatus portable_audio_song_runtime_advance_frame(PortableAudioSongRuntime *runtime);
void portable_audio_song_runtime_mix(PortableAudioPcmMixer *pcm,PortableAudioPsgMixer *psg,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u]);
void portable_audio_song_runtime_stop(PortableAudioSongRuntime *runtime);
void portable_audio_song_runtime_pause(PortableAudioSongRuntime *runtime,
                                       int paused);
PortableAudioRuntimeStatus portable_audio_song_runtime_control(
    PortableAudioSongRuntime *runtime, uint16_t track_bits,
    uint16_t volume, int16_t pitch);
const char *portable_audio_runtime_status_name(PortableAudioRuntimeStatus status);

#endif
