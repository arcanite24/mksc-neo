#include "audio_pcm_mixer.h"

#include <limits.h>
#include <string.h>

enum { ENVELOPE_RELEASE=0, ENVELOPE_SUSTAIN=1, ENVELOPE_DECAY=2,
       ENVELOPE_ATTACK=3 };

static uint8_t scale_value(uint8_t key)
{
    unsigned octave;
    unsigned note;
    if(key>179u)key=179u;
    octave=14u-key/12u;note=key%12u;
    return (uint8_t)((octave<<4)|note);
}

static uint32_t midi_frequency(const PortableAudioTuning *tuning,uint32_t source_frequency_q10,uint8_t key,
                               uint8_t fine)
{
    uint8_t scale=scale_value(key),next_scale=scale_value((uint8_t)(key+1u));
    uint32_t first=tuning->pcm_frequency[scale&15u]>>(scale>>4);
    uint32_t second=tuning->pcm_frequency[next_scale&15u]>>(next_scale>>4);
    uint32_t factor=first+(uint32_t)(((uint64_t)fine*(second-first))>>8);
    return (uint32_t)(((uint64_t)source_frequency_q10*factor)>>32);
}

static uint32_t phase_step(const PortableAudioTuning *tuning,const PortableAudioVoice *voice,
                           const PortableAudioSampleView *sample,uint8_t key,
                           uint8_t fine)
{
    if((voice->original_type&8u)!=0u)return UINT32_C(1)<<23;
    return midi_frequency(tuning,sample->frequency_q10,key,fine)*UINT32_C(798);
}

void portable_audio_pcm_mixer_init(PortableAudioPcmMixer *mixer)
{
    if(mixer==NULL)return;
    memset(mixer,0,sizeof *mixer);mixer->next_generation=1u;
    mixer->voice_count=PORTABLE_AUDIO_PCM_VOICES;
}

static void set_gains(PortableAudioPcmVoice *channel,
                      const PortableAudioVoice *voice,uint8_t velocity,
                      uint8_t volume,uint8_t volume_x,uint8_t pan,int8_t volume_mod,
                      int8_t pan_mod)
{
    uint32_t x,right,left;int32_t y,rhythm_pan=0;
    x=((uint32_t)volume*volume_x)>>5;
    x=(uint32_t)((int32_t)x*(volume_mod+128))>>7;
    y=2*((int32_t)pan-64)+pan_mod;
    if(y < -128)y=-128;else if(y > 127)y=127;
    right=(uint32_t)((y+128)*(int32_t)x)>>8;
    left=(uint32_t)((127-y)*(int32_t)x)>>8;
    if((voice->pan_sweep&0x80u)!=0u)
        rhythm_pan=2*((int32_t)voice->pan_sweep-192);
    right=(uint32_t)((int32_t)velocity*(128+rhythm_pan)*(int32_t)right)>>14;
    left=(uint32_t)((int32_t)velocity*(127-rhythm_pan)*(int32_t)left)>>14;
    channel->right_gain=(uint16_t)(right>255u?255u:right);
    channel->left_gain=(uint16_t)(left>255u?255u:left);
}

static int select_slot(const PortableAudioPcmMixer *mixer,uint8_t priority,
                       uint16_t owner_order)
{
    int candidate=-1,releasing=0;
    uint8_t best_priority=priority;
    uint16_t best_owner=owner_order;
    for(unsigned index=0u;index<mixer->voice_count;++index){
        const PortableAudioPcmVoice *voice=&mixer->voices[index];
        if(!voice->active)return (int)index;
        if(voice->stage==ENVELOPE_RELEASE){
            if(!releasing){
                releasing=1;candidate=(int)index;
                best_priority=voice->priority;best_owner=voice->owner_order;
                continue;
            }
        }else if(releasing)continue;
        if(voice->priority<best_priority ||
           (voice->priority==best_priority && voice->owner_order>=best_owner)){
            candidate=(int)index;best_priority=voice->priority;
            best_owner=voice->owner_order;
        }
    }
    return candidate;
}

PortableAudioMixerStatus portable_audio_pcm_mixer_start(
    PortableAudioPcmMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t velocity,
    uint8_t volume,uint8_t pan,uint8_t priority,
    PortableAudioVoiceHandle *handle)
{
    return portable_audio_pcm_mixer_start_pitch(mixer,voice,sample,key,0u,
        velocity,volume,pan,priority,handle);
}

PortableAudioMixerStatus portable_audio_pcm_mixer_start_pitch(
    PortableAudioPcmMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t pan,uint8_t priority,
    PortableAudioVoiceHandle *handle)
{
    return portable_audio_pcm_mixer_start_control(mixer,voice,sample,key,fine,
        velocity,volume,64u,pan,priority,handle);
}

PortableAudioMixerStatus portable_audio_pcm_mixer_start_control(
    PortableAudioPcmMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t volume_x,uint8_t pan,
    uint8_t priority,PortableAudioVoiceHandle *handle)
{
    return portable_audio_pcm_mixer_start_owned_control(mixer,voice,sample,key,fine,
        velocity,volume,volume_x,pan,priority,0u,handle);
}

PortableAudioMixerStatus portable_audio_pcm_mixer_start_owned_control(
    PortableAudioPcmMixer *mixer,const PortableAudioVoice *voice,
    const PortableAudioSampleView *sample,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t volume_x,uint8_t pan,
    uint8_t priority,uint16_t owner_order,PortableAudioVoiceHandle *handle)
{
    PortableAudioPcmVoice *channel;
    int slot;
    if(mixer==NULL||voice==NULL||sample==NULL||handle==NULL||
       velocity>127u||volume>127u||pan>127u||key>178u||
       mixer->voice_count==0u||mixer->voice_count>PORTABLE_AUDIO_PCM_VOICES)
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    if(sample->data==NULL||sample->sample_count==0u||
       sample->data_size!=sample->sample_count||sample->frequency_q10<1024u||
       (((sample->flags&PORTABLE_AUDIO_SAMPLE_LOOP)!=0u)&&
        sample->loop_start>=sample->sample_count))
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    if(voice->source_kind!=PORTABLE_AUDIO_VOICE_PCM||
       sample->encoding!=PORTABLE_AUDIO_SAMPLE_PCM_S8)
        return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    if(voice->source_id!=sample->sample_id)
        return PORTABLE_AUDIO_MIXER_SAMPLE_MISMATCH;
    if ((voice->original_type & 8u) == 0u && mixer->tuning == NULL)
        return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    slot=select_slot(mixer,priority,owner_order);
    if(slot<0)return PORTABLE_AUDIO_MIXER_NO_CHANNEL;
    channel=&mixer->voices[slot];memset(channel,0,sizeof *channel);
    channel->sample=*sample;channel->step_q23=phase_step(mixer->tuning,voice,sample,key,fine);
    channel->generation=mixer->next_generation++;
    if(mixer->next_generation==0u)mixer->next_generation=1u;
    channel->attack=voice->attack;
    channel->decay=voice->decay;channel->sustain=voice->sustain;
    channel->release=voice->release;channel->priority=priority;
    channel->stage=ENVELOPE_ATTACK;channel->active=1u;channel->owner_order=owner_order;
    set_gains(channel,voice,velocity,volume,volume_x,pan,0,0);
    handle->slot=(uint8_t)slot;handle->generation=channel->generation;
    return PORTABLE_AUDIO_MIXER_OK;
}

PortableAudioMixerStatus portable_audio_pcm_mixer_update(
    PortableAudioPcmMixer *mixer,PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t pan,int8_t volume_mod,
    int8_t pan_mod)
{
    return portable_audio_pcm_mixer_update_control(mixer,handle,voice,key,fine,
        velocity,volume,64u,pan,volume_mod,pan_mod);
}

PortableAudioMixerStatus portable_audio_pcm_mixer_update_control(
    PortableAudioPcmMixer *mixer,PortableAudioVoiceHandle handle,
    const PortableAudioVoice *voice,uint8_t key,uint8_t fine,
    uint8_t velocity,uint8_t volume,uint8_t volume_x,uint8_t pan,
    int8_t volume_mod,int8_t pan_mod)
{
    PortableAudioPcmVoice *channel;
    if(mixer==NULL||voice==NULL||handle.slot>=PORTABLE_AUDIO_PCM_VOICES||
       key>178u||velocity>127u||volume>127u||pan>127u)
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    channel=&mixer->voices[handle.slot];
    if(!channel->active||channel->generation!=handle.generation)
        return PORTABLE_AUDIO_MIXER_STALE_HANDLE;
    if(voice->source_kind!=PORTABLE_AUDIO_VOICE_PCM||
       voice->source_id!=channel->sample.sample_id)
        return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    if ((voice->original_type & 8u) == 0u && mixer->tuning == NULL)
        return PORTABLE_AUDIO_MIXER_UNSUPPORTED_VOICE;
    channel->step_q23=phase_step(mixer->tuning,voice,&channel->sample,key,fine);
    set_gains(channel,voice,velocity,volume,volume_x,pan,volume_mod,pan_mod);
    return PORTABLE_AUDIO_MIXER_OK;
}

PortableAudioMixerStatus portable_audio_pcm_mixer_release(
    PortableAudioPcmMixer *mixer,PortableAudioVoiceHandle handle)
{
    PortableAudioPcmVoice *voice;
    if(mixer==NULL||handle.slot>=PORTABLE_AUDIO_PCM_VOICES)
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    voice=&mixer->voices[handle.slot];
    if(!voice->active||voice->generation!=handle.generation)
        return PORTABLE_AUDIO_MIXER_STALE_HANDLE;
    voice->stage=ENVELOPE_RELEASE;return PORTABLE_AUDIO_MIXER_OK;
}

PortableAudioMixerStatus portable_audio_pcm_mixer_stop(
    PortableAudioPcmMixer *mixer,PortableAudioVoiceHandle handle)
{
    PortableAudioPcmVoice *voice;
    if(mixer==NULL||handle.slot>=PORTABLE_AUDIO_PCM_VOICES)
        return PORTABLE_AUDIO_MIXER_INVALID_ARGUMENT;
    voice=&mixer->voices[handle.slot];
    if(!voice->active||voice->generation!=handle.generation)
        return PORTABLE_AUDIO_MIXER_STALE_HANDLE;
    voice->active=0u;return PORTABLE_AUDIO_MIXER_OK;
}

void portable_audio_pcm_mixer_pause(PortableAudioPcmMixer *mixer,int paused)
{if(mixer!=NULL)mixer->paused=paused?1u:0u;}

static void update_envelope(PortableAudioPcmVoice *voice)
{
    uint32_t next;
    switch(voice->stage){
    case ENVELOPE_ATTACK:
        next=(uint32_t)voice->envelope+voice->attack;
        if(next>=255u){voice->envelope=255u;voice->stage=ENVELOPE_DECAY;}
        else voice->envelope=(uint8_t)next;
        break;
    case ENVELOPE_DECAY:
        next=((uint32_t)voice->envelope*voice->decay)>>8;
        if(next<=voice->sustain){voice->envelope=voice->sustain;
            voice->stage=ENVELOPE_SUSTAIN;}
        else voice->envelope=(uint8_t)next;
        break;
    case ENVELOPE_RELEASE:
        next=((uint32_t)voice->envelope*voice->release)>>8;
        voice->envelope=(uint8_t)next;
        if(next==0u)voice->active=0u;
        break;
    default:break;
    }
}

static int32_t next_sample(PortableAudioPcmVoice *voice)
{
    uint32_t index=voice->position;
    uint32_t fraction=voice->phase_q23;
    int32_t first,second;
    if(index>=voice->sample.sample_count)return 0;
    first=(int8_t)voice->sample.data[index];
    if(index+1u<voice->sample.sample_count)
        second=(int8_t)voice->sample.data[index+1u];
    else if((voice->sample.flags&PORTABLE_AUDIO_SAMPLE_LOOP)!=0u)
        second=(int8_t)voice->sample.data[voice->sample.loop_start];
    else second=first;
    return first+(int32_t)(((int64_t)(second-first)*fraction)>>23);
}

static void advance_phase(PortableAudioPcmVoice *voice)
{
    uint64_t phase=(uint64_t)voice->phase_q23+voice->step_q23;
    uint32_t advance=(uint32_t)(phase>>23);
    uint64_t index=(uint64_t)voice->position+advance;
    uint32_t fraction=(uint32_t)phase&UINT32_C(0x7fffff);
    if(index>=voice->sample.sample_count){
        if((voice->sample.flags&PORTABLE_AUDIO_SAMPLE_LOOP)!=0u){
            uint32_t length=voice->sample.sample_count-voice->sample.loop_start;
            index=voice->sample.loop_start+
                (index-voice->sample.sample_count)%length;
            voice->position=(uint32_t)index;voice->phase_q23=fraction;
        }else{voice->active=0u;voice->position=0u;voice->phase_q23=0u;}
    }else{voice->position=(uint32_t)index;voice->phase_q23=fraction;}
}

static int16_t clamp_i16(int32_t value)
{
    if(value>INT16_MAX)return INT16_MAX;
    if(value<INT16_MIN)return INT16_MIN;
    return (int16_t)value;
}

void portable_audio_pcm_mixer_render_frame(PortableAudioPcmMixer *mixer,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u])
{
    unsigned frame,index;
    if(output==NULL)return;
    memset(output,0,PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u*sizeof *output);
    if(mixer==NULL||mixer->paused)return;
    for(index=0u;index<PORTABLE_AUDIO_PCM_VOICES;++index)
        if(mixer->voices[index].active&&!mixer->voices[index].paused)update_envelope(&mixer->voices[index]);
    for(frame=0u;frame<PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++frame){
        int32_t left=0,right=0;
        for(index=0u;index<PORTABLE_AUDIO_PCM_VOICES;++index){
            PortableAudioPcmVoice *voice=&mixer->voices[index];int32_t sample;
            if(!voice->active||voice->paused)continue;
            sample=next_sample(voice);
            left+=(int32_t)(((int64_t)sample*voice->envelope*
                            voice->left_gain)>>8);
            right+=(int32_t)(((int64_t)sample*voice->envelope*
                             voice->right_gain)>>8);
            advance_phase(voice);
        }
        output[frame*2u]=clamp_i16(left);
        output[frame*2u+1u]=clamp_i16(right);
    }
}

uint32_t portable_audio_pcm_checksum(const int16_t *samples,size_t count)
{
    uint32_t hash=UINT32_C(2166136261);
    if(samples==NULL&&count!=0u)return 0u;
    for(size_t index=0u;index<count;++index){uint16_t value=(uint16_t)samples[index];
        hash=(hash^(uint8_t)value)*UINT32_C(16777619);
        hash=(hash^(uint8_t)(value>>8))*UINT32_C(16777619);}
    return hash;
}

const char *portable_audio_mixer_status_name(PortableAudioMixerStatus status)
{
    static const char *const names[]={"ok","invalid argument",
        "unsupported voice","sample mismatch","no channel","stale handle"};
    return (unsigned)status<sizeof names/sizeof names[0]?names[status]:
        "unknown status";
}
