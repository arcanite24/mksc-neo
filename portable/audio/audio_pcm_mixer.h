#ifndef PORTABLE_AUDIO_PCM_MIXER_H
#define PORTABLE_AUDIO_PCM_MIXER_H

#include "audio_sample_asset.h"
#include "audio_tuning.h"
#include "audio_voice_bank_asset.h"

#include <stddef.h>
#include <stdint.h>

#define PORTABLE_AUDIO_PCM_VOICES 12u
#define PORTABLE_AUDIO_PCM_RATE 10512u
#define PORTABLE_AUDIO_PCM_FRAME_SAMPLES 176u

typedef struct
{
    uint8_t slot;
    uint32_t generation;
} PortableAudioVoiceHandle;

typedef enum
{
    PORTABLE_AUDIO_MIXER_OK = 0,
    PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT,
    PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE,
    PORTABLE_AUDIO_MIXER_SAMPLE_MISMATCH,
    PORTABLE_AUDIO_MIXER_NO_CHANNEL,
    PORTABLE_AUDIO_MIXER_STALE_HANDLE
} PortableAudioMixerStatus;

typedef struct
{
    PortableAudioSampleView sample;
    uint32_t position;
    uint32_t phase_q23;
    uint32_t step_q23;
    uint32_t generation;
    uint16_t owner_order;
    uint8_t paused;
    uint16_t left_gain;
    uint16_t right_gain;
    uint8_t envelope;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
    uint8_t priority;
    uint8_t stage;
    uint8_t active;
} PortableAudioPcmVoice;

typedef struct
{
    PortableAudioPcmVoice voices[PORTABLE_AUDIO_PCM_VOICES];
    const PortableAudioTuning *tuning; /* borrowed immutable imported profile */
    uint32_t next_generation;
    uint8_t voice_count;
    uint8_t paused;
} PortableAudioPcmMixer;

void portable_audio_pcm_mixer_init(PortableAudioPcmMixer *mixer);
PortableAudioMixerStatus portable_audio_pcm_mixer_start(
    PortableAudioPcmMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t velocity,
    uint8_t volume, uint8_t pan, uint8_t priority,
    PortableAudioVoiceHandle *handle);
PortableAudioMixerStatus portable_audio_pcm_mixer_start_pitch(
    PortableAudioPcmMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t pan, uint8_t priority,
    PortableAudioVoiceHandle *handle);
PortableAudioMixerStatus portable_audio_pcm_mixer_start_control(
    PortableAudioPcmMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t volume_x, uint8_t pan,
    uint8_t priority, PortableAudioVoiceHandle *handle);
/* ply_note orders equal priorities by the original track address. Callers
 * provide a stable ordered track ID; no host pointer participates. */
PortableAudioMixerStatus portable_audio_pcm_mixer_start_owned_control(
    PortableAudioPcmMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t volume_x, uint8_t pan,
    uint8_t priority, uint16_t owner_order, PortableAudioVoiceHandle *handle);
PortableAudioMixerStatus portable_audio_pcm_mixer_update(
    PortableAudioPcmMixer *mixer, PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t pan,
    int8_t volume_mod, int8_t pan_mod);
PortableAudioMixerStatus portable_audio_pcm_mixer_update_control(
    PortableAudioPcmMixer *mixer, PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t volume_x, uint8_t pan,
    int8_t volume_mod, int8_t pan_mod);
PortableAudioMixerStatus portable_audio_pcm_mixer_release(
    PortableAudioPcmMixer *mixer, PortableAudioVoiceHandle handle);
PortableAudioMixerStatus portable_audio_pcm_mixer_stop(
    PortableAudioPcmMixer *mixer, PortableAudioVoiceHandle handle);
void portable_audio_pcm_mixer_pause(PortableAudioPcmMixer *mixer, int paused);
void portable_audio_pcm_mixer_render_frame(PortableAudioPcmMixer *mixer,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);
uint32_t portable_audio_pcm_checksum(const int16_t *samples, size_t count);
const char *portable_audio_mixer_status_name(PortableAudioMixerStatus status);

#endif
