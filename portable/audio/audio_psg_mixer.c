#include "audio_psg_mixer.h"

#include <limits.h>
#include <string.h>

enum {
    ENVELOPE_RELEASE = 0,
    ENVELOPE_SUSTAIN = 1,
    ENVELOPE_DECAY = 2,
    ENVELOPE_ATTACK = 3
};

static PortableAudioMixerStatus owned(PortableAudioPsgMixer *mixer,
    PortableAudioVoiceHandle handle,PortableAudioPsgVoice **out);

static int32_t tone_value(const PortableAudioTuning *tuning,uint8_t key)
{
    unsigned adjusted;
    unsigned octave;
    unsigned note;
    int32_t value;
    if (key <= 35u) {
        adjusted = 0u;
    } else {
        adjusted = key - 36u;
        if (adjusted > 130u)
            adjusted = 130u;
    }
    octave = adjusted / 12u;
    note = adjusted % 12u;
    value = tuning->psg_frequency[note] >> octave;
    return value;
}

static uint16_t tone_register(const PortableAudioTuning *tuning,uint8_t key,uint8_t fine)
{
    int32_t first=tone_value(tuning,key);
    int32_t second=tone_value(tuning,key==255u?255u:(uint8_t)(key+1u));
    return (uint16_t)(first+((fine*(second-first))>>8)+2048);
}

static uint64_t tone_step(const PortableAudioTuning *tuning,uint8_t source_kind,uint8_t key,uint8_t fine)
{
    uint32_t denominator = 2048u - tone_register(tuning,key,fine);
    uint32_t numerator = source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE ?
        65536u : 131072u;
    return ((uint64_t)numerator << 32) /
        (denominator * UINT32_C(10512));
}

static uint64_t noise_step(const PortableAudioTuning *tuning,uint8_t key)
{
    uint8_t value;
    uint32_t divisor;
    uint32_t shift;
    uint32_t index;
    if (key <= 20u) {
        index = 0u;
    } else {
        index = key - 21u;
        if (index > 59u)
            index = 59u;
    }
    value = tuning->noise[index];
    shift = value >> 4;
    divisor = value & 7u;
    divisor = (divisor != 0u ? 2u * divisor : 1u) << shift;
    return ((uint64_t)UINT32_C(524288) << 32) /
        (divisor * UINT32_C(10512));
}

static int slot_for(const PortableAudioVoice *voice)
{
    if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_SQUARE) {
        uint8_t channel = voice->original_type & 7u;
        if (channel == 1u)
            return 0;
        if (channel == 2u)
            return 1;
        return -1;
    }
    if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE)
        return 2;
    if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_NOISE)
        return 3;
    return -1;
}

static void configure_volume(PortableAudioPsgVoice *channel,
                             const PortableAudioVoice *voice,
                             uint8_t velocity,uint8_t volume,uint8_t volume_x,
                             uint8_t pan,
                             int8_t volume_mod,int8_t pan_mod)
{
    uint32_t x;
    uint32_t right;
    uint32_t left;
    uint32_t goal;
    int32_t y = 2 * ((int32_t)pan - 64);
    int32_t rhythm = 0;
    x = ((uint32_t)volume * volume_x) >> 5;
    x = (uint32_t)((int32_t)x * (volume_mod + 128)) >> 7;
    y += pan_mod;
    if (y < -128)
        y = -128;
    else if (y > 127)
        y = 127;
    right = (uint32_t)((y + 128) * (int32_t)x) >> 8;
    left = (uint32_t)((127 - y) * (int32_t)x) >> 8;
    if ((voice->pan_sweep & 0x80u) != 0u)
        rhythm = 2 * ((int32_t)voice->pan_sweep - 192);
    right = (uint32_t)((int32_t)velocity * (128 + rhythm) *
        (int32_t)right) >> 14;
    left = (uint32_t)((int32_t)velocity * (127 - rhythm) *
        (int32_t)left) >> 14;
    channel->left_enabled = 1u;
    channel->right_enabled = 1u;
    if (right >= left && right / 2u >= left)
        channel->left_enabled = 0u;
    else if (left > right && left / 2u >= right)
        channel->right_enabled = 0u;
    goal = (left + right) / 16u;
    channel->envelope_goal = (uint8_t)(goal > 15u ? 15u : goal);
    channel->sustain_goal = (uint8_t)((channel->envelope_goal *
        voice->sustain + 15u) >> 4);
}

void portable_audio_psg_mixer_init(PortableAudioPsgMixer *mixer)
{
    if (mixer == NULL)
        return;
    memset(mixer,0,sizeof *mixer);
    mixer->next_generation = 1u;
}

PortableAudioMixerStatus portable_audio_psg_mixer_start(
    PortableAudioPsgMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t velocity,
    uint8_t volume,uint8_t pan,uint8_t priority,PortableAudioVoiceHandle *handle)
{
    return portable_audio_psg_mixer_start_pitch(mixer,voice,sample,key,0u,
        velocity,volume,pan,priority,handle);
}

PortableAudioMixerStatus portable_audio_psg_mixer_start_pitch(
    PortableAudioPsgMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t pan,uint8_t priority,
    PortableAudioVoiceHandle *handle)
{
    return portable_audio_psg_mixer_start_control(mixer,voice,sample,key,fine,
        velocity,volume,64u,pan,priority,handle);
}

PortableAudioMixerStatus portable_audio_psg_mixer_start_control(
    PortableAudioPsgMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t volume_x,uint8_t pan,
    uint8_t priority,PortableAudioVoiceHandle *handle)
{
    return portable_audio_psg_mixer_start_owned_control(mixer,voice,sample,key,fine,
        velocity,volume,volume_x,pan,priority,0u,handle);
}

PortableAudioMixerStatus portable_audio_psg_mixer_start_owned_control(
    PortableAudioPsgMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t volume_x,uint8_t pan,
    uint8_t priority,uint16_t owner_order,PortableAudioVoiceHandle *handle)
{
    PortableAudioPsgVoice *channel=NULL;
    int slot;
    if (mixer == NULL || voice == NULL || handle == NULL || key > 178u ||
        velocity > 127u || volume > 127u || pan > 127u)
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    if (mixer->tuning == NULL)
        return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    slot = slot_for(voice);
    if (slot < 0)
        return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    channel = &mixer->voices[slot];
    if (channel->active && channel->stage!=ENVELOPE_RELEASE &&
        (channel->priority>priority ||
         (channel->priority==priority && channel->owner_order<owner_order)))
        return PORTABLE_AUDIO_MIXER_NO_CHANNEL;
    if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE) {
        if (sample == NULL || sample->data == NULL || sample->data_size != 16u ||
            sample->sample_count != 32u ||
            sample->encoding != PORTABLE_AUDIO_SAMPLE_PSG_WAVE4)
            return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
        if (sample->sample_id != voice->source_id)
            return PORTABLE_AUDIO_MIXER_SAMPLE_MISMATCH;
    }
    memset(channel,0,sizeof *channel);
    if (sample != NULL)
        channel->sample = *sample;
    channel->source_kind = voice->source_kind;
    channel->source_id = voice->source_id;
    channel->step_q32 = voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_NOISE ?
        noise_step(mixer->tuning,key) : tone_step(mixer->tuning,voice->source_kind,key,fine);
    channel->generation = mixer->next_generation++;
    if (mixer->next_generation == 0u)
        mixer->next_generation = 1u;
    channel->lfsr = UINT16_C(0x7fff);
    channel->attack = voice->attack;
    channel->decay = voice->decay;
    channel->sustain = voice->sustain;
    channel->release = voice->release;
    channel->priority = priority;
    channel->owner_order = owner_order;
    channel->stage = ENVELOPE_ATTACK;
    channel->envelope_counter = channel->attack;
    channel->active = 1u;
    configure_volume(channel,voice,velocity,volume,volume_x,pan,0,0);
    if (channel->attack == 0u) {
        channel->envelope = channel->envelope_goal;
        channel->stage = ENVELOPE_DECAY;
        channel->envelope_counter = channel->decay;
    }
    handle->slot = (uint8_t)slot;
    handle->generation = channel->generation;
    return PORTABLE_AUDIO_MIXER_OK;
}

PortableAudioMixerStatus portable_audio_psg_mixer_update(
    PortableAudioPsgMixer *mixer,PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t pan,int8_t volume_mod,
    int8_t pan_mod)
{
    return portable_audio_psg_mixer_update_control(mixer,handle,voice,key,fine,
        velocity,volume,64u,pan,volume_mod,pan_mod);
}

PortableAudioMixerStatus portable_audio_psg_mixer_update_control(
    PortableAudioPsgMixer *mixer,PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t volume_x,uint8_t pan,
    int8_t volume_mod,int8_t pan_mod)
{
    PortableAudioPsgVoice *channel=NULL;
    PortableAudioMixerStatus status=owned(mixer,handle,&channel);
    if(voice==NULL||key>178u||velocity>127u||volume>127u||pan>127u)
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    if(status!=PORTABLE_AUDIO_MIXER_OK)return status;
    if(mixer->tuning==NULL)return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    if(voice->source_kind!=channel->source_kind||
       voice->source_id!=channel->source_id)
        return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    channel->step_q32=voice->source_kind==PORTABLE_AUDIO_VOICE_PSG_NOISE?
        noise_step(mixer->tuning,key):tone_step(mixer->tuning,voice->source_kind,key,fine);
    configure_volume(channel,voice,velocity,volume,volume_x,pan,volume_mod,
                     pan_mod);
    return PORTABLE_AUDIO_MIXER_OK;
}

static PortableAudioMixerStatus owned(PortableAudioPsgMixer *mixer,
    PortableAudioVoiceHandle handle,PortableAudioPsgVoice **out)
{
    if (mixer == NULL || out == NULL ||
        handle.slot >= PORTABLE_AUDIO_PSG_VOICES)
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    *out = &mixer->voices[handle.slot];
    if (!(*out)->active || (*out)->generation != handle.generation)
        return PORTABLE_AUDIO_MIXER_STALE_HANDLE;
    return PORTABLE_AUDIO_MIXER_OK;
}

PortableAudioMixerStatus portable_audio_psg_mixer_release(
    PortableAudioPsgMixer *mixer,PortableAudioVoiceHandle handle)
{
    PortableAudioPsgVoice *voice=NULL;
    PortableAudioMixerStatus status = owned(mixer,handle,&voice);
    if (status == PORTABLE_AUDIO_MIXER_OK) {
        voice->stage = ENVELOPE_RELEASE;
        voice->envelope_counter = voice->release;
        if (voice->release == 0u)
            voice->active = 0u;
    }
    return status;
}

PortableAudioMixerStatus portable_audio_psg_mixer_stop(
    PortableAudioPsgMixer *mixer,PortableAudioVoiceHandle handle)
{
    PortableAudioPsgVoice *voice=NULL;
    PortableAudioMixerStatus status = owned(mixer,handle,&voice);
    if (status == PORTABLE_AUDIO_MIXER_OK)
        voice->active = 0u;
    return status;
}

void portable_audio_psg_mixer_pause(PortableAudioPsgMixer *mixer,int paused)
{
    if (mixer != NULL)
        mixer->paused = paused ? 1u : 0u;
}

static void envelope_step(PortableAudioPsgVoice *voice)
{
    if (voice->stage == ENVELOPE_SUSTAIN || !voice->active)
        return;
    if (voice->envelope_counter != 0u) {
        voice->envelope_counter--;
        return;
    }
    if (voice->stage == ENVELOPE_ATTACK) {
        if (voice->envelope < voice->envelope_goal)
            voice->envelope++;
        if (voice->envelope >= voice->envelope_goal) {
            voice->envelope = voice->envelope_goal;
            voice->stage = ENVELOPE_DECAY;
            voice->envelope_counter = voice->decay;
        } else {
            voice->envelope_counter = voice->attack;
        }
    } else if (voice->stage == ENVELOPE_DECAY) {
        if (voice->decay == 0u || voice->envelope <= voice->sustain_goal) {
            if (voice->sustain == 0u) {
                voice->active = 0u;
            } else {
                voice->envelope = voice->sustain_goal;
                voice->stage = ENVELOPE_SUSTAIN;
            }
        } else {
            voice->envelope--;
            voice->envelope_counter = voice->decay;
        }
    } else if (voice->stage == ENVELOPE_RELEASE) {
        if (voice->envelope == 0u) {
            voice->active = 0u;
        } else {
            voice->envelope--;
            voice->envelope_counter = voice->release;
            if (voice->envelope == 0u)
                voice->active = 0u;
        }
    }
}

static int32_t oscillator(PortableAudioPsgVoice *voice)
{
    if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_SQUARE) {
        static const uint8_t duty[4] = {1u,2u,4u,6u};
        uint8_t position = (uint8_t)(voice->phase_q32 >> 29);
        return position < duty[voice->source_id & 3u] ? 1 : -1;
    }
    if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE) {
        uint8_t position = (uint8_t)(voice->phase_q32 >> 27);
        uint8_t packed = voice->sample.data[position >> 1];
        uint8_t value = (position & 1u) != 0u ? packed & 15u : packed >> 4;
        return value;
    }
    return (voice->lfsr & 1u) != 0u ? -1 : 1;
}

static void advance(PortableAudioPsgVoice *voice)
{
    uint64_t phase = voice->phase_q32 + voice->step_q32;
    uint32_t clocks = (uint32_t)(phase >> 32);
    voice->phase_q32 = phase & UINT32_MAX;
    if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_NOISE) {
        while (clocks-- != 0u) {
            uint16_t feedback = (uint16_t)((voice->lfsr ^
                (voice->lfsr >> 1)) & 1u);
            voice->lfsr = (uint16_t)((voice->lfsr >> 1) | (feedback << 14));
            if (voice->source_id != 0u)
                voice->lfsr = (uint16_t)((voice->lfsr &
                    ~UINT16_C(0x40)) | (feedback << 6));
        }
    }
}

static int16_t clamp16(int32_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

static unsigned wave_volume(uint8_t envelope)
{
    static const uint8_t scale[16] = {
        0u,0u,1u,1u,1u,1u,2u,2u,2u,2u,3u,3u,3u,3u,4u,4u
    };
    return scale[envelope & 15u];
}

void portable_audio_psg_mixer_render_frame(PortableAudioPsgMixer *mixer,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u])
{
    unsigned slot;
    unsigned frame;
    int double_step;
    if (output == NULL)
        return;
    memset(output,0,PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u * sizeof *output);
    if (mixer == NULL || mixer->paused)
        return;
    double_step = mixer->envelope_frame == 14u;
    mixer->envelope_frame = double_step ? 0u :
        (uint8_t)(mixer->envelope_frame + 1u);
    for (slot = 0u;slot < PORTABLE_AUDIO_PSG_VOICES;++slot) {
        if (mixer->voices[slot].active&&!mixer->voices[slot].paused) {
            envelope_step(&mixer->voices[slot]);
            if (double_step)
                envelope_step(&mixer->voices[slot]);
        }
    }
    for (frame = 0u;frame < PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++frame) {
        int32_t left = 0;
        int32_t right = 0;
        for (slot = 0u;slot < PORTABLE_AUDIO_PSG_VOICES;++slot) {
            PortableAudioPsgVoice *voice = &mixer->voices[slot];
            int32_t sample;
            unsigned level;
            if (!voice->active||voice->paused)
                continue;
            sample = oscillator(voice);
            level = voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE ?
                wave_volume(voice->envelope) : voice->envelope;
            /* MPlayExtender sets NR50=0x77 and full CGB mixing. In the
             * reference DAC path one nibble unit is (8 * 8 / 4) *
             * (256 * 3 / 16) = 768 signed-16 units. Center the DC level
             * here; no register/IRQ emulation is needed. Square/noise
             * span envelope DAC units, hence half that gain per polarity.
             * CGB3's quarter/half/75% levels truncate the unsigned nibble
             * before centering, as the source volume register does. */
            if (voice->source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE)
                sample = ((sample * (int32_t)level) / 4 -
                    (8 * (int32_t)level) / 4) * 768;
            else
                sample *= (int32_t)level * 384;
            if (voice->left_enabled)
                left += sample;
            if (voice->right_enabled)
                right += sample;
            advance(voice);
        }
        output[frame * 2u] = clamp16(left);
        output[frame * 2u + 1u] = clamp16(right);
    }
}
