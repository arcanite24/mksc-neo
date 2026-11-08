#include "audio_song_runtime.h"

#include <limits.h>
#include <string.h>

enum { MIXER_PCM = 1, MIXER_PSG = 2 };

static PortableAudioPcmMixer *pcm_mixer(PortableAudioSongRuntime *runtime)
{
    return runtime->shared_pcm!=NULL?runtime->shared_pcm:&runtime->pcm;
}

static PortableAudioPsgMixer *psg_mixer(PortableAudioSongRuntime *runtime)
{
    return runtime->shared_psg!=NULL?runtime->shared_psg:&runtime->psg;
}

static int owns_order(const PortableAudioSongRuntime *runtime,uint16_t order)
{
    return order>=runtime->owner_base &&
        order<runtime->owner_base+PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS;
}

/* A global replacement invalidates the prior track's generation immediately. */
static void prune_notes(PortableAudioSongRuntime *runtime)
{
    for(unsigned i=0u;i<PORTABLE_AUDIO_RUNTIME_NOTES;++i){
        PortableAudioRuntimeNote *note=&runtime->notes[i];
        if(!note->active)continue;
        if(note->mixer_kind==MIXER_PCM){
            const PortableAudioPcmVoice *voice=&pcm_mixer(runtime)->voices[note->handle.slot];
            if(!voice->active||voice->generation!=note->handle.generation)note->active=0u;
        }else{
            const PortableAudioPsgVoice *voice=&psg_mixer(runtime)->voices[note->handle.slot];
            if(!voice->active||voice->generation!=note->handle.generation)note->active=0u;
        }
    }
}

static const PortableAudioSampleView *find_sample(
    const PortableAudioSongRuntime *runtime,uint32_t sample_id)
{
    size_t index;
    for (index = 0u;index < runtime->sample_count;++index)
        if (runtime->samples[index].sample_id == sample_id)
            return &runtime->samples[index];
    return NULL;
}

static void release_note(PortableAudioSongRuntime *runtime,
                         PortableAudioRuntimeNote *note)
{
    PortableAudioMixerStatus status;
    if (!note->active || note->released)
        return;
    if (note->mixer_kind == MIXER_PCM)
        status = portable_audio_pcm_mixer_release(pcm_mixer(runtime),note->handle);
    else
        status = portable_audio_psg_mixer_release(psg_mixer(runtime),note->handle);
    if (status == PORTABLE_AUDIO_MIXER_OK)
        runtime->notes_released++;
    note->released = 1u;
    if(status!=PORTABLE_AUDIO_MIXER_OK)note->active=0u;
}

static void release_track(PortableAudioSongRuntime *runtime,uint8_t track,
                          int tied_only,uint8_t key)
{
    unsigned index;
    for (index = 0u;index < PORTABLE_AUDIO_RUNTIME_NOTES;++index) {
        PortableAudioRuntimeNote *note = &runtime->notes[index];
        if (note->active && note->track == track &&
            (!tied_only || (note->tied && note->key == key)))
        {
            release_note(runtime,note);
            /* ply_fine unlinks released tails; gate expiry/endtie keep them
             * attached so later volume, pause and TrackStop still own them. */
            if(!tied_only&&note->active){
                if(note->mixer_kind==MIXER_PCM)
                    pcm_mixer(runtime)->voices[note->handle.slot].owner_order=0u;
                else psg_mixer(runtime)->voices[note->handle.slot].owner_order=0u;
                note->active=0u;
            }
        }
    }
}

static void release_due(PortableAudioSongRuntime *runtime,uint32_t tick)
{
    unsigned index;
    for (index = 0u;index < PORTABLE_AUDIO_RUNTIME_NOTES;++index) {
        PortableAudioRuntimeNote *note = &runtime->notes[index];
        if (note->active && !note->released && !note->tied && note->release_tick <= tick)
            release_note(runtime,note);
    }
}

static PortableAudioRuntimeNote *note_slot(PortableAudioSongRuntime *runtime)
{
    unsigned index;
    prune_notes(runtime);
    for (index = 0u;index < PORTABLE_AUDIO_RUNTIME_NOTES;++index)
        if (!runtime->notes[index].active)
            return &runtime->notes[index];
    return NULL;
}

static void remove_replaced(PortableAudioSongRuntime *runtime,uint8_t mixer_kind,
                            PortableAudioVoiceHandle handle)
{
    unsigned index;
    for (index = 0u;index < PORTABLE_AUDIO_RUNTIME_NOTES;++index) {
        PortableAudioRuntimeNote *note = &runtime->notes[index];
        if (note->active && note->mixer_kind == mixer_kind &&
            note->handle.slot == handle.slot &&
            note->handle.generation != handle.generation)
            note->active = 0u;
    }
}

static void pitch(const PortableAudioRuntimeTrack *track,uint8_t note_key,
                  uint8_t *key,uint8_t *fine)
{
    int32_t value = ((int32_t)track->tune +
        track->bend * track->bend_range) * 4 +
        (int32_t)track->key_shift * 256 + track->pitch_x;
    if (track->mod_type == 0u)
        value += 16 * track->mod_value;
    int32_t result = (int32_t)note_key + (value >> 8);
    if (result < 0) {
        result = 0;
        value = 0;
    } else if (result > 178) {
        result = 178;
        value = 255;
    }
    *key = (uint8_t)result;
    *fine = (uint8_t)value;
}

static PortableAudioMixerStatus update_note(PortableAudioSongRuntime *runtime,
    PortableAudioRuntimeNote *note,const PortableAudioTrackState *state)
{
    PortableAudioRuntimeTrack *track=&runtime->tracks[note->track];
    int8_t volume_mod=track->mod_type==1u?track->mod_value:0;
    int8_t pan_mod=track->mod_type==2u?track->mod_value:0;
    uint8_t key,fine;
    pitch(track,note->pitch_key,&key,&fine);
    if(note->mixer_kind==MIXER_PCM)
        return portable_audio_pcm_mixer_update_control(pcm_mixer(runtime),
            note->handle,&note->voice,key,fine,note->velocity,state->volume,
            track->volume_x,state->pan,volume_mod,pan_mod);
    return portable_audio_psg_mixer_update_control(psg_mixer(runtime),note->handle,
        &note->voice,key,fine,note->velocity,state->volume,track->volume_x,
        state->pan,volume_mod,pan_mod);
}

static void update_track_notes(PortableAudioSongRuntime *runtime,uint8_t track,
                               const PortableAudioTrackState *state)
{
    unsigned index;
    for(index=0u;index<PORTABLE_AUDIO_RUNTIME_NOTES;++index){
        PortableAudioRuntimeNote *note=&runtime->notes[index];
        PortableAudioMixerStatus status;
        if(!note->active||note->track!=track)continue;
        status=update_note(runtime,note,state);
        if(status==PORTABLE_AUDIO_MIXER_STALE_HANDLE)note->active=0u;
        else if(status!=PORTABLE_AUDIO_MIXER_OK)
            runtime->status=PORTABLE_AUDIO_RUNTIME_BAD_VOICE;
    }
}

static int start_note(PortableAudioSongRuntime *runtime,uint32_t tick,
                      const PortableAudioSequenceEvent *event,
                      const PortableAudioTrackState *state)
{
    PortableAudioRuntimeTrack *track = &runtime->tracks[event->track];
    PortableAudioRuntimeNote *owner;
    PortableAudioVoice voice;
    PortableAudioVoice root;
    PortableAudioVoiceHandle handle;
    const PortableAudioSampleView *sample = NULL;
    PortableAudioVoiceStatus voice_status;
    PortableAudioMixerStatus mixer_status;
    uint16_t resolved;
    uint16_t priority;
    uint8_t key;
    uint8_t fine;
    uint8_t mixer_kind;
    uint8_t pitch_key;
    voice_status=portable_audio_voice_bank_voice(runtime->bank,state->voice,&root);
    if(voice_status!=PORTABLE_AUDIO_VOICE_OK){
        runtime->status=PORTABLE_AUDIO_RUNTIME_BAD_VOICE;
        return 0;
    }
    voice_status = portable_audio_voice_bank_resolve(runtime->bank,state->voice,
        event->arg0,&voice,&resolved);
    if (voice_status != PORTABLE_AUDIO_VOICE_OK) {
        runtime->status = PORTABLE_AUDIO_RUNTIME_BAD_VOICE;
        return 0;
    }
    (void)resolved;
    if (voice.source_kind == PORTABLE_AUDIO_VOICE_PCM ||
        voice.source_kind == PORTABLE_AUDIO_VOICE_PSG_WAVE) {
        sample = find_sample(runtime,voice.source_id);
        if (sample == NULL) {
            runtime->status = PORTABLE_AUDIO_RUNTIME_MISSING_SAMPLE;
            return 0;
        }
    }
    pitch_key=root.source_kind==PORTABLE_AUDIO_VOICE_DRUM?voice.key:event->arg0;
    if(track->lfo_delay!=0u){
        track->lfo_phase=0u;track->mod_value=0;
        track->lfo_delay_counter=track->lfo_delay;
    }
    pitch(track,pitch_key,&key,&fine);
    priority = runtime->player.sequence->priority + track->priority;
    if (priority > 255u)
        priority = 255u;
    if (voice.source_kind == PORTABLE_AUDIO_VOICE_PCM) {
        mixer_kind = MIXER_PCM;
        mixer_status = portable_audio_pcm_mixer_start_owned_control(pcm_mixer(runtime),
            &voice,sample,key,fine,event->arg1,state->volume,track->volume_x,
            state->pan,(uint8_t)priority,(uint16_t)(runtime->owner_base+event->track),&handle);
    } else {
        mixer_kind = MIXER_PSG;
        mixer_status = portable_audio_psg_mixer_start_owned_control(psg_mixer(runtime),
            &voice,sample,key,fine,event->arg1,state->volume,track->volume_x,
            state->pan,(uint8_t)priority,(uint16_t)(runtime->owner_base+event->track),&handle);
    }
    if (mixer_status == PORTABLE_AUDIO_MIXER_NO_CHANNEL) {
        runtime->notes_dropped++;
        return 1;
    }
    if (mixer_status != PORTABLE_AUDIO_MIXER_OK) {
        runtime->status = PORTABLE_AUDIO_RUNTIME_BAD_VOICE;
        return 0;
    }
    remove_replaced(runtime,mixer_kind,handle);
    owner = note_slot(runtime);
    if (owner == NULL) {
        runtime->status = PORTABLE_AUDIO_RUNTIME_PLAYER_ERROR;
        return 0;
    }
    memset(owner,0,sizeof *owner);
    owner->voice = voice;
    owner->handle = handle;
    owner->release_tick = tick + event->value;
    if (owner->release_tick < tick) {
        runtime->status = PORTABLE_AUDIO_RUNTIME_PLAYER_ERROR;
        return 0;
    }
    owner->track = event->track;
    owner->key = event->arg0;
    owner->pitch_key = pitch_key;
    owner->velocity = event->arg1;
    owner->mixer_kind = mixer_kind;
    owner->tied = event->operation == PORTABLE_AUDIO_EVENT_TIE;
    owner->active = 1u;
    runtime->notes_started++;
    return 1;
}

static int event_sink(void *user,uint32_t tick,
                      const PortableAudioSequenceEvent *event,
                      const PortableAudioTrackState *state)
{
    PortableAudioSongRuntime *runtime = (PortableAudioSongRuntime *)user;
    PortableAudioRuntimeTrack *track = &runtime->tracks[event->track];
    switch ((PortableAudioSequenceOperation)event->operation) {
    case PORTABLE_AUDIO_EVENT_END:
        release_track(runtime,event->track,0,0u);
        break;
    case PORTABLE_AUDIO_EVENT_PRIORITY:
        track->priority = (uint8_t)event->value;
        break;
    case PORTABLE_AUDIO_EVENT_KEY_SHIFT:
        track->key_shift = (int8_t)event->value;
        update_track_notes(runtime,event->track,state);
        break;
    case PORTABLE_AUDIO_EVENT_VOLUME:
    case PORTABLE_AUDIO_EVENT_PAN:
        update_track_notes(runtime,event->track,state);
        break;
    case PORTABLE_AUDIO_EVENT_BEND:
        track->bend = (int16_t)event->value - 64;
        update_track_notes(runtime,event->track,state);
        break;
    case PORTABLE_AUDIO_EVENT_BEND_RANGE:
        track->bend_range = (uint8_t)event->value;
        update_track_notes(runtime,event->track,state);
        break;
    case PORTABLE_AUDIO_EVENT_LFO_SPEED:
        track->lfo_speed=(uint8_t)event->value;
        if(track->lfo_speed==0u){track->lfo_phase=0u;track->mod_value=0;
            update_track_notes(runtime,event->track,state);}
        break;
    case PORTABLE_AUDIO_EVENT_LFO_DELAY:
        track->lfo_delay=(uint8_t)event->value;
        break;
    case PORTABLE_AUDIO_EVENT_MOD_DEPTH:
        track->mod_depth=(uint8_t)event->value;
        if(track->mod_depth==0u){track->mod_value=0;
            update_track_notes(runtime,event->track,state);}
        break;
    case PORTABLE_AUDIO_EVENT_MOD_TYPE:
        track->mod_type=(uint8_t)event->value;
        update_track_notes(runtime,event->track,state);
        break;
    case PORTABLE_AUDIO_EVENT_TUNE:
        track->tune = (int8_t)((int)event->value - 64);
        update_track_notes(runtime,event->track,state);
        break;
    case PORTABLE_AUDIO_EVENT_NOTE:
    case PORTABLE_AUDIO_EVENT_TIE:
        return start_note(runtime,tick,event,state);
    case PORTABLE_AUDIO_EVENT_END_TIE:
        release_track(runtime,event->track,1,event->arg0);
        break;
    default:
        break;
    }
    return 1;
}

static void modulation_tick(PortableAudioSongRuntime *runtime)
{
    uint8_t track_index;
    for(track_index=0u;track_index<runtime->player.sequence->track_count;
        ++track_index){
        PortableAudioRuntimeTrack *track=&runtime->tracks[track_index];
        int32_t triangle;
        int8_t next;
        if(track->lfo_speed==0u||track->mod_depth==0u)continue;
        if(track->lfo_delay_counter!=0u){track->lfo_delay_counter--;continue;}
        track->lfo_phase=(uint8_t)(track->lfo_phase+track->lfo_speed);
        triangle=(track->lfo_phase<64u||track->lfo_phase>=192u)?
            (int8_t)track->lfo_phase:128-(int32_t)track->lfo_phase;
        next=(int8_t)((track->mod_depth*triangle)>>6);
        if(next==track->mod_value)continue;
        track->mod_value=next;
        update_track_notes(runtime,track_index,
                           &runtime->player.tracks[track_index]);
    }
}

PortableAudioRuntimeStatus portable_audio_song_runtime_init(
    PortableAudioSongRuntime *runtime,const PortableAudioSequenceView *sequence,
    const PortableAudioVoiceBankView *bank,
    const PortableAudioSampleView *samples,size_t sample_count)
{
    uint8_t track;
    if (runtime == NULL || sequence == NULL || bank == NULL ||
        (sample_count != 0u && samples == NULL))
        return PORTABLE_AUDIO_RUNTIME_INVALID_ARGUMENT;
    if (sequence->bank_id != bank->bank_id)
        return PORTABLE_AUDIO_RUNTIME_BAD_BANK;
    memset(runtime,0,sizeof *runtime);
    runtime->tempo_scale=256u;
    runtime->owner_base=1u;
    runtime->bank = bank;
    runtime->samples = samples;
    runtime->sample_count = sample_count;
    if (portable_audio_sequence_player_init(&runtime->player,sequence) !=
        PORTABLE_AUDIO_PLAYER_OK)
        return PORTABLE_AUDIO_RUNTIME_PLAYER_ERROR;
    portable_audio_pcm_mixer_init(&runtime->pcm);
    portable_audio_psg_mixer_init(&runtime->psg);
    for (track = 0u;track < sequence->track_count;++track){
        runtime->tracks[track].bend_range = 2u;
        runtime->tracks[track].lfo_speed = 22u;
        runtime->tracks[track].volume_x = 64u;
    }
    return PORTABLE_AUDIO_RUNTIME_OK;
}

PortableAudioRuntimeStatus portable_audio_song_runtime_control(
    PortableAudioSongRuntime *runtime,uint16_t track_bits,
    uint16_t volume,int16_t pitch)
{
    uint8_t track;
    if(runtime==NULL||volume>UINT16_C(0x03ff))
        return PORTABLE_AUDIO_RUNTIME_INVALID_ARGUMENT;
    if(runtime->status!=PORTABLE_AUDIO_RUNTIME_OK)return runtime->status;
    for(track=0u;track<runtime->player.sequence->track_count;++track){
        if((track_bits&(uint16_t)(1u<<track))==0u)continue;
        runtime->tracks[track].volume_x=(uint8_t)(volume/4u);
        runtime->tracks[track].pitch_x=pitch;
        update_track_notes(runtime,track,&runtime->player.tracks[track]);
    }
    return runtime->status;
}

void portable_audio_song_runtime_pause(PortableAudioSongRuntime *runtime,
                                       int paused)
{
    if (runtime == NULL)
        return;
    runtime->paused = paused ? 1u : 0u;
    PortableAudioPcmMixer *pcm=pcm_mixer(runtime);
    PortableAudioPsgMixer *psg=psg_mixer(runtime);
    for(unsigned i=0u;i<PORTABLE_AUDIO_PCM_VOICES;++i)
        if(owns_order(runtime,pcm->voices[i].owner_order))pcm->voices[i].paused=runtime->paused;
    for(unsigned i=0u;i<PORTABLE_AUDIO_PSG_VOICES;++i)
        if(owns_order(runtime,psg->voices[i].owner_order))psg->voices[i].paused=runtime->paused;
    if(runtime->shared_pcm==NULL)portable_audio_pcm_mixer_pause(pcm,paused);
    if(runtime->shared_psg==NULL)portable_audio_psg_mixer_pause(psg,paused);
}

static int16_t clamp16(int32_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

static int32_t stock_pcm_output(int16_t left,int16_t right)
{
    /* MKSC sends one mono MP2K FIFO to both speakers. SoundMain applies
     * (masterVolume + 1) / 16 = 13 / 16, then the GBA DAC path applies 3 / 4.
     * Folding the two gain accumulators also divides by two: 39 / 128. */
    int32_t scaled=((int32_t)left+(int32_t)right)*39;
    if(scaled>=0)return scaled/128;
    return -((-scaled+127)/128);
}

PortableAudioRuntimeStatus portable_audio_song_runtime_advance_frame(PortableAudioSongRuntime *runtime)
{
    if(runtime==NULL)return PORTABLE_AUDIO_RUNTIME_INVALID_ARGUMENT;
    if(runtime->status!=PORTABLE_AUDIO_RUNTIME_OK)return runtime->status;
    if(runtime->paused)return PORTABLE_AUDIO_RUNTIME_OK;
    prune_notes(runtime);
    runtime->tempo_accumulator = (uint16_t)(runtime->tempo_accumulator +
        (uint16_t)(((uint32_t)runtime->player.tempo*runtime->tempo_scale)>>8u));
    while (runtime->tempo_accumulator >= 150u) {
        PortableAudioPlayerStatus player_status;
        runtime->tempo_accumulator -= 150u;
        release_due(runtime,runtime->next_tick);
        player_status = portable_audio_sequence_player_advance_to_sink(
            &runtime->player,runtime->next_tick,UINT32_C(100000),event_sink,
            runtime);
        if (player_status != PORTABLE_AUDIO_PLAYER_OK) {
            if (runtime->status == PORTABLE_AUDIO_RUNTIME_OK)
                runtime->status = PORTABLE_AUDIO_RUNTIME_PLAYER_ERROR;
            return runtime->status;
        }
        modulation_tick(runtime);
        if(runtime->status!=PORTABLE_AUDIO_RUNTIME_OK)return runtime->status;
        if (runtime->next_tick == UINT32_MAX) {
            runtime->status = PORTABLE_AUDIO_RUNTIME_PLAYER_ERROR;
            return runtime->status;
        }
        runtime->next_tick++;
    }
    return PORTABLE_AUDIO_RUNTIME_OK;
}

void portable_audio_song_runtime_stop(PortableAudioSongRuntime *runtime)
{
    if(runtime==NULL)return;
    PortableAudioPcmMixer *pcm=pcm_mixer(runtime);
    PortableAudioPsgMixer *psg=psg_mixer(runtime);
    for(unsigned i=0u;i<PORTABLE_AUDIO_PCM_VOICES;++i)
        if(owns_order(runtime,pcm->voices[i].owner_order))pcm->voices[i].active=0u;
    for(unsigned i=0u;i<PORTABLE_AUDIO_PSG_VOICES;++i)
        if(owns_order(runtime,psg->voices[i].owner_order))psg->voices[i].active=0u;
    memset(runtime->notes,0,sizeof runtime->notes);
}

void portable_audio_song_runtime_mix(PortableAudioPcmMixer *pcm_pool,PortableAudioPsgMixer *psg_pool,
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u])
{
    int16_t pcm[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u],psg[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    size_t index;
    portable_audio_pcm_mixer_render_frame(pcm_pool,pcm);
    portable_audio_psg_mixer_render_frame(psg_pool,psg);
    for (index = 0u;index < PORTABLE_AUDIO_PCM_FRAME_SAMPLES;++index) {
        int32_t mono=stock_pcm_output(pcm[index*2u],pcm[index*2u+1u]);
        output[index*2u]=clamp16(mono+psg[index*2u]);
        output[index*2u+1u]=clamp16(mono+psg[index*2u+1u]);
    }
}

PortableAudioRuntimeStatus portable_audio_song_runtime_render_frame(
    PortableAudioSongRuntime *runtime,int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u])
{
    if(runtime==NULL||output==NULL)return PORTABLE_AUDIO_RUNTIME_INVALID_ARGUMENT;
    const PortableAudioRuntimeStatus status=portable_audio_song_runtime_advance_frame(runtime);
    if(status!=PORTABLE_AUDIO_RUNTIME_OK)return status;
    if(runtime->paused)memset(output,0,PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u*sizeof *output);
    else portable_audio_song_runtime_mix(pcm_mixer(runtime),psg_mixer(runtime),output);
    return PORTABLE_AUDIO_RUNTIME_OK;
}

const char *portable_audio_runtime_status_name(PortableAudioRuntimeStatus status)
{
    static const char *const names[] = {
        "ok","invalid argument","bad bank","bad voice","missing sample",
        "player error"
    };
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] :
        "unknown status";
}
