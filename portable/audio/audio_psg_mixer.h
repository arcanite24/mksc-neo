#ifndef PORTABLE_AUDIO_PSG_MIXER_H
#define PORTABLE_AUDIO_PSG_MIXER_H

#include "audio_pcm_mixer.h"

#include <stdint.h>

#define PORTABLE_AUDIO_PSG_VOICES 4u

typedef struct
{
    PortableAudioSampleView sample;
    uint64_t phase_q32;
    uint64_t step_q32;
    uint32_t generation;
    uint32_t source_id;
    uint16_t owner_order;
    uint8_t paused;
    uint16_t lfsr;
    uint8_t source_kind;
    uint8_t envelope;
    uint8_t envelope_goal;
    uint8_t envelope_counter;
    uint8_t sustain_goal;
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
    uint8_t priority;
    uint8_t stage;
    uint8_t active;
    uint8_t left_enabled;
    uint8_t right_enabled;
} PortableAudioPsgVoice;

typedef struct
{
    PortableAudioPsgVoice voices[PORTABLE_AUDIO_PSG_VOICES];
    const PortableAudioTuning *tuning; /* borrowed immutable imported profile */
    uint32_t next_generation;
    uint8_t envelope_frame;
    uint8_t paused;
} PortableAudioPsgMixer;

void portable_audio_psg_mixer_init(PortableAudioPsgMixer *mixer);
PortableAudioMixerStatus portable_audio_psg_mixer_start(
    PortableAudioPsgMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t velocity,
    uint8_t volume, uint8_t pan, uint8_t priority,
    PortableAudioVoiceHandle *handle);
PortableAudioMixerStatus portable_audio_psg_mixer_start_pitch(
    PortableAudioPsgMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t pan, uint8_t priority,
    PortableAudioVoiceHandle *handle);
PortableAudioMixerStatus portable_audio_psg_mixer_start_control(
    PortableAudioPsgMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t volume_x, uint8_t pan,
    uint8_t priority, PortableAudioVoiceHandle *handle);
/* ply_note orders equal priorities by the original track address. Callers
 * provide a stable ordered track ID; no host pointer participates. */
PortableAudioMixerStatus portable_audio_psg_mixer_start_owned_control(
    PortableAudioPsgMixer *mixer, const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t volume_x, uint8_t pan,
    uint8_t priority, uint16_t owner_order, PortableAudioVoiceHandle *handle);
PortableAudioMixerStatus portable_audio_psg_mixer_update(
    PortableAudioPsgMixer *mixer, PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t pan,
    int8_t volume_mod, int8_t pan_mod);
PortableAudioMixerStatus portable_audio_psg_mixer_update_control(
    PortableAudioPsgMixer *mixer, PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice, uint8_t key, uint8_t fine,
    uint8_t velocity, uint8_t volume, uint8_t volume_x, uint8_t pan,
    int8_t volume_mod, int8_t pan_mod);
PortableAudioMixerStatus portable_audio_psg_mixer_release(
    PortableAudioPsgMixer *mixer, PortableAudioVoiceHandle handle);
PortableAudioMixerStatus portable_audio_psg_mixer_stop(
    PortableAudioPsgMixer *mixer, PortableAudioVoiceHandle handle);
void portable_audio_psg_mixer_pause(PortableAudioPsgMixer *mixer, int paused);
void portable_audio_psg_mixer_render_frame(PortableAudioPsgMixer *mixer,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u]);

#endif
