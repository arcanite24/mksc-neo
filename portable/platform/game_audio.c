#include "game_host_internal.h"

#ifdef PORTABLE_ENABLE_SDL3

enum { ROUTE_AUDIO_CAPTURE_FRAMES=600u };

static void capture_u16(uint8_t *out,uint16_t value)
{
    out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8u);
}
static void capture_u32(uint8_t *out,uint32_t value)
{
    capture_u16(out,(uint16_t)value);capture_u16(out+2u,(uint16_t)(value>>16u));
}
static int capture_header(FILE *file,uint32_t frames)
{
    uint8_t header[44]={0};
    const uint32_t bytes=frames*PORTABLE_AUDIO_PCM_FRAME_SAMPLES*4u;
    memcpy(header,"RIFF",4u);capture_u32(header+4u,36u+bytes);
    memcpy(header+8u,"WAVEfmt ",8u);capture_u32(header+16u,16u);
    capture_u16(header+20u,1u);capture_u16(header+22u,2u);
    capture_u32(header+24u,PORTABLE_AUDIO_PCM_RATE);
    capture_u32(header+28u,PORTABLE_AUDIO_PCM_RATE*4u);
    capture_u16(header+32u,4u);capture_u16(header+34u,16u);
    memcpy(header+36u,"data",4u);capture_u32(header+40u,bytes);
    return fseek(file,0,SEEK_SET)==0 && fwrite(header,1u,sizeof header,file)==sizeof header;
}

int route_audio_capture_close(RouteAudioHost *audio)
{
    if(audio==NULL)return 0;
    if(audio->capture_file==NULL)return 1;
    FILE *file=audio->capture_file;audio->capture_file=NULL;
    const int header_ok=capture_header(file,audio->capture_frames);
    const int close_ok=fclose(file)==0;
    if(!header_ok || !close_ok){fprintf(stderr,"Race audio capture finalize failed\n");return 0;}
    printf("Race audio capture: frames=%u rate=%u stereo16 peak=%u\n",
        audio->capture_frames,PORTABLE_AUDIO_PCM_RATE,audio->capture_peak);
    return 1;
}

/* Capture the actual mixed race output before a fast/dummy SDL queue may
 * drop it. One bounded file per shell run includes the first race's start. */
static int capture_frame(RouteAudioHost *audio,const int16_t *samples)
{
    if((audio->race_course_id==0u && !audio->mesh_audio.active) || audio->race_paused)return 1;
    if(!audio->capture_attempted){
        audio->capture_attempted=1u;
        const char *path=getenv("MKSC_AUDIO_CAPTURE");
        if(path==NULL || !*path)return 1;
        audio->capture_file=fopen(path,"wb");
        if(audio->capture_file==NULL || !capture_header(audio->capture_file,0u)){
            fprintf(stderr,"Race audio capture open failed: %s\n",path);
            if(audio->capture_file!=NULL){fclose(audio->capture_file);audio->capture_file=NULL;}
            return 0;
        }
    }
    if(audio->capture_file==NULL)return 1;
    uint8_t bytes[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*4u];
    for(size_t i=0u;i<PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u;++i){
        capture_u16(bytes+i*2u,(uint16_t)samples[i]);
        const unsigned amplitude=samples[i]<0?(unsigned)(-(int32_t)samples[i]):(unsigned)samples[i];
        if(amplitude>audio->capture_peak)audio->capture_peak=(uint16_t)amplitude;
    }
    if(fwrite(bytes,1u,sizeof bytes,audio->capture_file)!=sizeof bytes){
        fprintf(stderr,"Race audio capture write failed\n");
        (void)route_audio_capture_close(audio);return 0;
    }
    ++audio->capture_frames;
    return audio->capture_frames<ROUTE_AUDIO_CAPTURE_FRAMES || route_audio_capture_close(audio);
}

static int route_audio_apply(RouteAudioHost *audio,uint32_t tick,
    PortableAudioCommandOperation operation,uint8_t player,uint16_t argument)
{
    /* A racing sound set replaces stock cues on the stock cue's player. */
    if(operation==PORTABLE_AUDIO_COMMAND_START_SONG&&audio->mesh_audio.active&&
       audio->mesh_audio.has_sound_set&&!(argument&(PORTABLE_AUDIO_NDS_SONG|PORTABLE_AUDIO_N64_SONG))){
        uint16_t song;
        if(portable_sound_set_cue(&audio->mesh_audio.sound_set,argument,&song)||
           (argument==audio->finish_rules.lap_song&&portable_sound_set_cue(
               &audio->mesh_audio.sound_set,PORTABLE_SOUND_CUE_LAP,&song))){
            if(player==PORTABLE_AUDIO_PLAYER_AUTO)
                player=portable_audio_session_song_player(audio->session.pack,argument);
            printf("3D sound-set cue: course=%u stock=%u song=%u player=%u\n",
                   audio->race_course_id,argument,song,player);
            if(!song)return 1;
            argument=song;
        }
        else
            printf("3D stock song kept: course=%u song=%u player=%u\n",audio->race_course_id,argument,player);
    }
    /* A sound set's final-lap tempo replaces the stock 125%. */
    if(operation==PORTABLE_AUDIO_COMMAND_SET_TEMPO&&player==0u&&argument!=256u&&audio->mesh_audio.active&&
       audio->mesh_audio.has_sound_set&&audio->mesh_audio.sound_set.final_lap_tempo)
        argument=audio->mesh_audio.sound_set.final_lap_tempo;
    PortableAudioCommand command={tick,audio->command_sequence++,argument,
                                  (uint8_t)operation,player};
    PortableAudioSessionStatus status=
        portable_audio_session_apply(&audio->session,&command);
    if(status!=PORTABLE_AUDIO_SESSION_OK)
        fprintf(stderr,"route audio command failed: tick=%u operation=%u "
            "player=%u argument=%u status=%s\n",tick,(unsigned)operation,
            player,argument,portable_audio_session_status_name(status));
    return status==PORTABLE_AUDIO_SESSION_OK;
}

static int route_audio_engine_apply(RouteAudioHost *audio,uint32_t tick,
    const PortableEngineAudioControl *control)
{
    const uint8_t player = portable_audio_session_song_player(audio->session.pack,
        audio->engine_rules.throttle_songs[audio->race_character_id-1u]);
    if (player >= PORTABLE_AUDIO_PLAYER_COUNT) return 0;
    if ((control->stop_engine && !route_audio_apply(audio,tick,
            PORTABLE_AUDIO_COMMAND_STOP_PLAYER,player,0u)) ||
        (control->stop_recovery && !route_audio_apply(audio,tick,
            PORTABLE_AUDIO_COMMAND_STOP_PLAYER,5u,0u))) return 0;
    if (control->apply_controls &&
        (!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_VOLUME,player,control->volume) ||
         !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_PITCH,player,(uint16_t)control->pitch))) return 0;
    if(control->entry_song!=0u&&!route_audio_apply(audio,tick,
            PORTABLE_AUDIO_COMMAND_START_SONG,portable_audio_session_song_player(audio->session.pack,control->entry_song),
            control->entry_song))return 0;
    if (control->start_song != 0u) {
        const uint8_t start_player = portable_audio_session_song_player(audio->session.pack,control->start_song);
        if (start_player >= PORTABLE_AUDIO_PLAYER_COUNT || !route_audio_apply(audio,tick,
                PORTABLE_AUDIO_COMMAND_START_SONG,start_player,control->start_song)) return 0;
    }
    return 1;
}

static int route_audio_hazard_apply(RouteAudioHost *audio, uint8_t character,
    uint8_t kind,uint8_t variant,int rival,uint32_t listener_flags,uint16_t volume)
{
    PortableHazardAudioControl control;
    portable_engine_hazard_audio(&audio->hazard_rules,character,kind,variant,rival,
        listener_flags,volume,&control);
    if(control.song==0u)return 1;
    const uint8_t player=control.player==PORTABLE_AUDIO_PLAYER_AUTO?
        portable_audio_session_song_player(audio->session.pack,control.song):control.player;
    return route_audio_apply(audio,audio->session.rendered_frames,
            PORTABLE_AUDIO_COMMAND_START_SONG,player,control.song)&&
        route_audio_apply(audio,audio->session.rendered_frames,
            PORTABLE_AUDIO_COMMAND_SET_VOLUME,player,control.volume);
}

static int route_audio_driver_events(RouteAudioHost *audio,
    const PortableDriverEngineSoundInput *input,int scene_stop)
{
    PortableJumpAudioControl jump;
    audio->horn_listener_flags=input->state_flags;
    portable_engine_jump_audio(&audio->engine_control,&audio->surface_audio,input,&jump);
    const uint32_t tick=audio->session.rendered_frames;
    if(jump.stop_surface&&!route_audio_apply(audio,tick,
            PORTABLE_AUDIO_COMMAND_STOP_PLAYER,5u,0u))return 0;
    if(jump.song!=0u){
        const uint8_t player=portable_audio_session_song_player(audio->session.pack,jump.song);
        if((!jump.require_idle||!portable_audio_session_player_playing(&audio->session,player))&&
           !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,player,jump.song))return 0;
    }
    if(!(input->state_flags&0x20u)&&
       (input->water_recovery_started||input->retrieval_started)){
        PortableEngineAudioControl recovery;
        if(!portable_engine_audio_control(&audio->engine_control,&audio->engine_rules,
                (uint8_t)(audio->race_character_id-1u),input,scene_stop,1,&recovery)||
           !route_audio_engine_apply(audio,audio->session.rendered_frames,&recovery))return 0;
    }
    /* F9E4/AD68: the icy-theme rescue arrival replaces 161 with 182. */
    if(input->water_rescue_arrived&&audio->surface_rules.theme==8u&&
       !(input->state_flags&0x20u)&&!route_audio_apply(audio,audio->session.rendered_frames,
            PORTABLE_AUDIO_COMMAND_START_SONG,5u,182u))return 0;
    if(input->ice_thaw_started && !(input->state_flags&0x20u)){
        const uint8_t player=portable_audio_session_song_player(audio->session.pack,181u);
        if(!portable_audio_session_player_playing(&audio->session,player)&&
           !route_audio_apply(audio,audio->session.rendered_frames,
                PORTABLE_AUDIO_COMMAND_START_SONG,player,181u))return 0;
    }
    return route_audio_hazard_apply(audio,(uint8_t)(audio->race_character_id-1u),
        input->hazard_audio_kind,input->hazard_audio_variant,0,input->state_flags,256u);
}

static uint8_t route_audio_gp_phase(const PortableGpRaceSnapshot *snapshot)
{
    /* Native GP retains source7D4=0 until its finish transition. The race
     * lifecycle supplies the missing prestart/racing phases to C38C. */
    if(snapshot->finish_transition_phase!=0u)return snapshot->finish_transition_phase;
    if(snapshot->phase==PORTABLE_RACE_PHASE_RACING)return 3u;
    if(snapshot->phase==PORTABLE_RACE_PHASE_COUNTDOWN)return 2u;
    return 0u;
}

static int route_audio_nearby_engine(RouteAudioHost *audio,
    const PortableGpRaceSnapshot *snapshot)
{
    PortableNearbyEngineDriver drivers[PORTABLE_GP_OPPONENT_COUNT];
    for(size_t i=0u;i<PORTABLE_GP_OPPONENT_COUNT;++i){
        const PortableGpOpponentSnapshot *opponent=&snapshot->opponents[i];
        drivers[i]=(PortableNearbyEngineDriver){.x_q16=opponent->position_x_q16,
            .y_q16=opponent->position_y_q16,.input=opponent->engine_sound,
            .doppler=opponent->engine_doppler,.character=opponent->voice_class,
            .visible=opponent->visible};
    }
    PortableNearbyEngineControl control;
    if(!portable_engine_nearby_tick(&audio->nearby_engine,&audio->engine_rules,drivers,
        PORTABLE_GP_OPPONENT_COUNT,snapshot->player_x_q16,snapshot->player_y_q16,
        snapshot->player_height_q16,snapshot->engine_sound.state_flags,
        route_audio_gp_phase(snapshot),snapshot->race_audio_mode,&control))return 0;
    if(control.song==0u)return 1;
    const uint32_t tick=audio->session.rendered_frames;
    const uint8_t player=portable_audio_session_song_player(audio->session.pack,control.song);
    return (!control.stop||route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_STOP_PLAYER,player,0u))&&
        (!control.start||route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,player,control.song))&&
        (!control.apply||(route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_VOLUME,player,control.volume)&&
            route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_PITCH,player,(uint16_t)control.pitch)));
}

/* Shared race audio consumes only audio inputs, borrowing event batches
 * from the immutable GP/TT snapshot instead of copying a complete race. */
typedef struct
{
    PortableRacePhase phase;
    int32_t position_x_q16, position_y_q16;
    PortableDriverEngineSoundInput engine_sound;
    uint16_t laps_completed, lap_count, countdown_remaining, engine_revs, lakitu_state;
    int16_t lakitu_y;
    uint32_t charged_audio_cue_count;
    uint8_t engine_rev_due, engine_scene_stop, engine_revs_owned;
    uint8_t finish_audio_ready, finish_audio_record, finish_audio_replay, haunt_music_active;
    const PortableDriverContactAudio *contact_audio, *boost_voice;
    const PortableCannonballSounds *cannonball_sounds;
    const PortablePiranhaSounds *piranha_sounds;
    const PortableThwompSounds *thwomp_sounds;
    const PortableCoursePropSounds *prop_sounds;
    const PortableCourseImpactSounds *impact_sounds;
} RaceAudioInput;

static RaceAudioInput time_trial_audio_input(const PortableRecoveredTimeTrialSnapshot *snapshot)
{
    return (RaceAudioInput){
        .phase = snapshot->phase,
        .position_x_q16 = snapshot->position_x_q16,
        .position_y_q16 = snapshot->position_y_q16,
        .engine_sound = snapshot->engine_sound,
        .laps_completed = snapshot->laps_completed,
        .lap_count = snapshot->lap_count,
        .countdown_remaining = snapshot->countdown_remaining,
        .engine_revs = snapshot->engine_revs,
        .lakitu_state = snapshot->lakitu_state,
        .lakitu_y = snapshot->lakitu_y,
        .charged_audio_cue_count = snapshot->charged_audio_cue_count,
        .engine_rev_due = snapshot->engine_rev_due,
        .engine_scene_stop = snapshot->engine_scene_stop,
        .engine_revs_owned = snapshot->engine_revs_owned,
        .finish_audio_ready = snapshot->finish_audio_ready,
        .finish_audio_record = snapshot->finish_audio_record,
        .finish_audio_replay = snapshot->finish_audio_replay,
        .haunt_music_active = snapshot->haunt_music_active,
        .contact_audio = &snapshot->contact_audio,
        .boost_voice = &snapshot->boost_voice,
        .cannonball_sounds = &snapshot->cannonball_sounds,
        .piranha_sounds = &snapshot->piranha_sounds,
        .thwomp_sounds = &snapshot->thwomp_sounds,
        .prop_sounds = &snapshot->prop_sounds,
        .impact_sounds = &snapshot->impact_sounds};
}

static int route_audio_course_loop(RouteAudioHost *audio)
{
    /* C38C consumes the song36 MEMACC notification before finish scripts.
     * C12C clears this latch on race entry; no elapsed-time fallback. */
    if(audio->race_course_id!=0u&&!audio->race_paused&&audio->session.memory[2]!=0u){
        audio->session.memory[2]=0u;
        if(!route_audio_apply(audio,audio->session.rendered_frames,
                PORTABLE_AUDIO_COMMAND_START_SONG,0u,28u))return 0;
        ++audio->course_loop_starts;
    }
    return 1;
}

static int route_audio_lap_music(RouteAudioHost *audio, const RaceAudioInput *snapshot, const PortableGpRaceSnapshot *rivals)
{
    if(audio->race_course_id!=0u&&!audio->race_paused&&snapshot!=NULL){
        const uint16_t laps=rivals!=NULL?rivals->laps_completed:snapshot->laps_completed;
        const uint16_t total=rivals!=NULL?rivals->lap_count:snapshot->lap_count;
        const uint8_t finished=rivals!=NULL?rivals->music_finished:(uint8_t)(snapshot->phase==PORTABLE_RACE_PHASE_FINISHED);
        const uint8_t final_lap=(uint8_t)(total>1u&&laps>=total-1u);
        PortableLapAudioControl lap;
        portable_lap_audio_step(&audio->lap_audio,&audio->finish_rules,laps,total,finished,&lap);
        const uint32_t tick=audio->session.rendered_frames;
        if(lap.song!=0u&&!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,
            portable_audio_session_song_player(audio->session.pack,lap.song),lap.song))return 0;
        if(lap.song!=0u)++audio->lap_cues;
        PortableRaceMusicEvent music={0};
        const int changed=lap.request!=0u?
            portable_race_music_request(&audio->race_music,lap.request,finished,
                audio->race_timeline.course_song,&music):lap.release!=0u?
            portable_race_music_release(&audio->race_music,lap.release,finished,
                audio->race_timeline.course_song,final_lap,&music):0;
        if(changed&&(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,0u,music.song_id)||
            !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_TEMPO,0u,music.tempo_scale)))return 0;
        if(changed){
            if(lap.request!=0u)++audio->final_lap_starts;
            else ++audio->final_lap_restores;
        }
        /* GP queues 4886C's effect2 edges; TT exposes its haunt latch. */
        if(rivals==NULL&&snapshot->haunt_music_active!=audio->haunt_music){
            audio->haunt_music=snapshot->haunt_music_active;
            const int haunt=audio->haunt_music?
                portable_race_music_request(&audio->race_music,2u,finished,
                    audio->race_timeline.course_song,&music):
                portable_race_music_release(&audio->race_music,2u,finished,
                    audio->race_timeline.course_song,final_lap,&music);
            if(haunt&&(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,0u,music.song_id)||
                !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_TEMPO,0u,music.tempo_scale)))return 0;
        }
    }
    return 1;
}

static int route_audio_finish_music(RouteAudioHost *audio, const RaceAudioInput *snapshot, const PortableGpRaceSnapshot *rivals)
{
    if(!audio->race_paused&&(rivals!=NULL||(snapshot!=NULL&&snapshot->finish_audio_ready))){
        PortableFinishAudioControl finish;
        uint8_t racers=1u;
        if(rivals!=NULL)
            for(size_t i=0u;i<PORTABLE_GP_OPPONENT_COUNT;++i)racers+=rivals->opponents[i].visible!=0u;
        portable_finish_audio_step(&audio->finish_audio,&audio->finish_rules,
            rivals!=NULL?rivals->music_finished:1u,
            rivals!=NULL?rivals->finish_transition_phase:4u,
            rivals!=NULL?rivals->race_audio_mode:(uint16_t)(0x200u|(snapshot->finish_audio_replay?0x4000u:0u)),
            rivals!=NULL?rivals->player_rank:0u,racers,
            rivals!=NULL?0u:snapshot->finish_audio_record,&finish);
        const uint32_t tick=audio->session.rendered_frames;
        for(uint8_t player=0u;player<PORTABLE_AUDIO_PLAYER_COUNT;++player)
            if((finish.stop_mask&(1u<<player))&&!route_audio_apply(audio,tick,
                PORTABLE_AUDIO_COMMAND_STOP_PLAYER,player,0u))return 0;
        if(finish.song!=0u&&!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,
            portable_audio_session_song_player(audio->session.pack,finish.song),finish.song))return 0;

    }
    return 1;
}

static int route_audio_countdown(RouteAudioHost *audio, const RaceAudioInput *snapshot, const PortableGpRaceSnapshot *rivals)
{
    if(audio->race_timeline.active&&!audio->race_paused){
        const uint32_t tick=audio->session.rendered_frames;
        PortableRaceAudioEvent events[2];
        /* Lights follow the published fixed-tick countdown in every mode;
         * GP/local starts have no TT prelude and cannot share absolute ticks. */
        if(snapshot!=NULL){
            const size_t count=portable_race_audio_countdown_step(&audio->race_timeline,
                snapshot->phase,rivals!=NULL?rivals->countdown_remaining:snapshot->countdown_remaining,
                audio->race_paused,events);
            for(size_t index=0u;index<count;++index){
                if(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                    events[index].player,events[index].song_id))return 0;
                if(getenv("MKSC_AUDIO_CAPTURE") && events[index].song_id>=114u && events[index].song_id<=115u)
                    printf("Race countdown cue: song=%u player=%u tick=%u remaining=%u\n",
                        events[index].song_id,events[index].player,tick,
                        rivals!=NULL?rivals->countdown_remaining:snapshot->countdown_remaining);
            }
        }
        const size_t count=portable_race_audio_timeline_step(
            &audio->race_timeline,rivals!=NULL?rivals->race_audio_mode:0x200u,events);
        for(size_t index=0u;index<count;++index)
            if(!route_audio_apply(audio,tick,
               PORTABLE_AUDIO_COMMAND_START_SONG,events[index].player,
               events[index].song_id))return 0;
    }
    return 1;
}

static int route_audio_driver_cues(RouteAudioHost *audio, const RaceAudioInput *snapshot)
{
    if(audio->race_course_id!=0u&&!audio->race_paused&&snapshot!=NULL){
        /* Driver9DAC/99A4 runs before contact producers and C38C. Consume
         * these one-frame events here, preserving shared player replacement. */
        PortableDriverEngineSoundInput driver_events=snapshot->engine_sound;
        if(snapshot->finish_audio_ready)driver_events.state_flags|=0x20u;
        if(!route_audio_driver_events(audio,&driver_events,snapshot->engine_scene_stop))return 0;
        if(snapshot->charged_audio_cue_count!=
           audio->last_charged_audio_cue_count){
            audio->last_charged_audio_cue_count=
                snapshot->charged_audio_cue_count;
            if(!route_audio_apply(audio,audio->session.rendered_frames,
               PORTABLE_AUDIO_COMMAND_START_SONG,6u,140u))return 0;
        }
        const PortableDriverContactAudio *cues[2] = {
            snapshot->boost_voice, snapshot->contact_audio,
        };
        uint32_t *serials[2] = {
            &audio->last_boost_voice_serial, &audio->last_contact_audio_serial,
        };
        for (size_t cue = 0u; cue < 2u; ++cue) {
            const PortableDriverContactAudio *contact = cues[cue];
            if (contact->serial == *serials[cue]) continue;
            *serials[cue] = contact->serial;
            const uint8_t player=portable_audio_session_song_player(audio->session.pack,contact->song_id);
            if(player>=PORTABLE_AUDIO_PLAYER_COUNT)return 0;
            if(contact->stop_first&&!route_audio_apply(audio,audio->session.rendered_frames,
                    PORTABLE_AUDIO_COMMAND_STOP_PLAYER,
                    portable_audio_session_song_player(audio->session.pack,contact->stop_song),0u))return 0;
            if(contact->restart||!portable_audio_session_song_playing(
                    &audio->session,contact->song_id)){
                const uint32_t tick=audio->session.rendered_frames;
                if(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                        player,contact->song_id)||
                   !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_VOLUME,
                        player,contact->volume)||
                   !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_PITCH,
                        player,(uint16_t)contact->pitch))return 0;
            }
            if(!contact->stop_first&&contact->stop_song!=0u&&portable_audio_session_song_playing(
                    &audio->session,contact->stop_song)&&!route_audio_apply(audio,
                    audio->session.rendered_frames,PORTABLE_AUDIO_COMMAND_STOP_PLAYER,
                    portable_audio_session_song_player(audio->session.pack,contact->stop_song),0u))return 0;
        }
        const PortableCourseImpactSounds *impacts=snapshot->impact_sounds;
        if(impacts->serial!=audio->last_impact_audio_serial){
            audio->last_impact_audio_serial=impacts->serial;
            if(impacts->count>PORTABLE_COURSE_IMPACT_CAPACITY)return 0;
            for(uint8_t i=0u;i<impacts->count;++i){
                const PortableCourseImpactSound *event=&impacts->events[i];
                if(event->character>=8u || event->rival>1u || event->variant>1u)return 0;
                const uint16_t song=audio->hazard_rules.impact_voices[event->character][event->rival][event->variant];
                const uint8_t player=portable_audio_session_song_player(audio->session.pack,song);
                if(!route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_START_SONG,player,song) ||
                   !route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_SET_VOLUME,player,event->volume))return 0;
            }
        }
        const PortableCoursePropSounds *props=snapshot->prop_sounds;
        if(props->serial!=audio->last_prop_audio_serial){
            audio->last_prop_audio_serial=props->serial;
            if(props->count>PORTABLE_COURSE_PROP_SOUND_CAPACITY)return 0;
            for(uint8_t i=0u;i<props->count;++i){
                const uint8_t player=portable_audio_session_song_player(audio->session.pack,props->songs[i]);
                if(player>=PORTABLE_AUDIO_PLAYER_COUNT ||
                   !route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_START_SONG,player,props->songs[i]) ||
                   !route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_SET_VOLUME,player,props->volumes[i]))return 0;
            }
        }
        const PortableThwompSounds *thwomps=snapshot->thwomp_sounds;
        if(thwomps->serial!=audio->last_thwomp_audio_serial){
            audio->last_thwomp_audio_serial=thwomps->serial;
            if(thwomps->count>PORTABLE_THWOMP_CAPACITY)return 0;
            const uint8_t player=portable_audio_session_song_player(audio->session.pack,384u);
            for(uint8_t i=0u;i<thwomps->count;++i){
                if(!route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_START_SONG,player,384u) ||
                   !route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_SET_VOLUME,player,thwomps->volumes[i]))return 0;
            }
        }
        const PortablePiranhaSounds *plants=snapshot->piranha_sounds;
        if(plants->serial!=audio->last_piranha_audio_serial){
            audio->last_piranha_audio_serial=plants->serial;
            if(plants->count>PORTABLE_PIRANHA_CAPACITY)return 0;
            for(uint8_t i=0u;i<plants->count;++i){
                const uint16_t song=plants->songs[i];
                if(song!=373u && song!=378u && song!=162u && song!=183u)return 0;
                const uint8_t player=portable_audio_session_song_player(audio->session.pack,song);
                /* E490 does not restart a squeal that is still sounding. */
                if(song==162u && portable_audio_session_song_playing(&audio->session,162u))continue;
                /* C19C starts183 only on an idle player or over itself. */
                if(song==183u && portable_audio_session_player_playing(&audio->session,player) &&
                   !portable_audio_session_song_playing(&audio->session,183u))continue;
                if(player>=PORTABLE_AUDIO_PLAYER_COUNT ||
                   !route_audio_apply(audio,audio->session.rendered_frames,
                       PORTABLE_AUDIO_COMMAND_START_SONG,player,song))return 0;
            }
        }
        const PortableCannonballSounds *sounds=snapshot->cannonball_sounds;
        if(sounds->serial!=audio->last_cannonball_audio_serial){
            audio->last_cannonball_audio_serial=sounds->serial;
            if(sounds->count>PORTABLE_CANNONBALL_CAPACITY)return 0;
            for(uint8_t i=0u;i<sounds->count;++i){
                const PortableCannonballSound *event=&sounds->events[i];
                const uint8_t player=portable_audio_session_song_player(audio->session.pack,event->song_id);
                if(event->require_idle && portable_audio_session_player_playing(
                    &audio->session,player))continue;
                const uint32_t tick=audio->session.rendered_frames;
                if(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                        player,event->song_id) ||
                   !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_VOLUME,
                        player,event->volume) ||
                   !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_PITCH,
                        player,0u))return 0;
            }
        }

    }
    return 1;
}

static int route_audio_engine(RouteAudioHost *audio, uint16_t buttons, int prestart, const RaceAudioInput *snapshot, const PortableGpRaceSnapshot *rivals)
{
    if(audio->race_course_id!=0u&&!audio->race_paused){
        PortableEngineAudioControl control;
        PortableDriverEngineSoundInput input = snapshot != NULL ?
            snapshot->engine_sound : (PortableDriverEngineSoundInput){0};
        if(snapshot!=NULL&&snapshot->finish_audio_ready)input.state_flags|=0x20u;
        input.water_recovery_started=input.retrieval_started=input.water_rescue_arrived=0u;
        const int held = prestart || (snapshot != NULL &&
            snapshot->phase == PORTABLE_RACE_PHASE_COUNTDOWN);
        if (held)
            input.control_word = portable_driver_control_word_from_semantic_buttons(buttons);
        if (snapshot != NULL && snapshot->engine_revs_owned) {
            audio->engine_control.accumulator = snapshot->engine_revs;
        } else {
            if (held)
                portable_engine_control_prestart_tick(&audio->engine_control,input.control_word);
            else if (snapshot != NULL && snapshot->engine_rev_due)
                portable_engine_control_race_tick(&audio->engine_control,
                    &audio->engine_rules.profiles[audio->race_engine_class],&input);
        }
        if (!portable_engine_audio_control(&audio->engine_control,&audio->engine_rules,
                (uint8_t)(audio->race_character_id-1u),&input,
                snapshot != NULL && snapshot->engine_scene_stop,!held,&control)) return 0;
        const uint32_t tick=audio->session.rendered_frames;
        if(!route_audio_engine_apply(audio,tick,&control))return 0;
        if(rivals!=NULL&&!route_audio_nearby_engine(audio,rivals))return 0;
        if(snapshot!=NULL){
            PortableSurfaceAudioControl surface;
            portable_engine_surface_audio_tick(&audio->engine_control,&audio->surface_audio,
                &audio->surface_rules,&input,snapshot->position_x_q16,snapshot->position_y_q16,
                snapshot->engine_scene_stop,&surface);
            if((surface.stop&&!route_audio_apply(audio,tick,
                    PORTABLE_AUDIO_COMMAND_STOP_PLAYER,5u,0u))||
               (surface.start_song&&!route_audio_apply(audio,tick,
                    PORTABLE_AUDIO_COMMAND_START_SONG,portable_audio_session_song_player(audio->session.pack,surface.start_song),
                    surface.start_song)))return 0;
        }
        PortableTireAudioControl tires;
        portable_engine_tire_audio_tick(&audio->engine_control,&audio->engine_rules,
            &input,snapshot != NULL && snapshot->engine_scene_stop,&tires);
        if (tires.stop_song != 0u) {
            const uint8_t slot=portable_audio_session_song_player(audio->session.pack,tires.stop_song);
            if (slot>=PORTABLE_AUDIO_PLAYER_COUNT) return 0;
            if ((!tires.stop_if_playing || audio->session.players[slot].song_id==tires.stop_song) &&
                !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_STOP_PLAYER,slot,0u)) return 0;
        }
        if (tires.start_song != 0u) {
            const uint8_t slot=portable_audio_session_song_player(audio->session.pack,tires.start_song);
            if (slot>=PORTABLE_AUDIO_PLAYER_COUNT || !route_audio_apply(audio,tick,
                    PORTABLE_AUDIO_COMMAND_START_SONG,slot,tires.start_song)) return 0;
        }
        if (tires.apply_volume && !route_audio_apply(audio,tick,
                PORTABLE_AUDIO_COMMAND_SET_VOLUME,portable_audio_session_song_player(audio->session.pack,164u),tires.volume)) return 0;
    }
    return 1;
}

static int route_audio_lakitu(RouteAudioHost *audio, int prestart, const RaceAudioInput *snapshot, const PortableGpRaceSnapshot *rivals)
{
    if(audio->race_course_id!=0u&&!audio->race_paused&&snapshot!=NULL){
        const uint8_t phase=rivals!=NULL?route_audio_gp_phase(rivals):
            !prestart&&snapshot->phase==PORTABLE_RACE_PHASE_RACING?3u:2u;
        const uint16_t song=portable_lakitu_audio_step(&audio->lakitu_audio,
            phase,snapshot->lakitu_state,snapshot->lakitu_y);
        if(song){
            if(!route_audio_apply(audio,audio->session.rendered_frames,
                    PORTABLE_AUDIO_COMMAND_START_SONG,
                    portable_audio_session_song_player(audio->session.pack,song),song))return 0;
            ++audio->lakitu_pulses;
        }
    }
    return 1;
}

/* Both recovered and mesh scenes mix into the same output owner. */
static int route_audio_submit_frame(RouteAudioHost *audio)
{
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    PortableSdlAudioStatus status;
    PortableAudioSessionStatus session_status=
        portable_audio_session_render_frame(&audio->session,output);
    if(session_status!=PORTABLE_AUDIO_SESSION_OK){
        fprintf(stderr,"route audio render failed: frame=%u status=%s\n",
            audio->session.rendered_frames,
            portable_audio_session_status_name(session_status));
        for(unsigned i=0u;i<PORTABLE_AUDIO_PLAYER_COUNT;++i)
            if(audio->session.players[i].n64&&audio->session.n64[i].status!=PORTABLE_N64_OK)
                fprintf(stderr,"  N64 song 0x%04x on player %u: %s at sequence offset 0x%x\n",
                    audio->session.players[i].song_id,i,
                    portable_n64_status_name(audio->session.n64[i].status),audio->session.n64[i].failed_at);
        return 0;
    }
    if(!capture_frame(audio,output))return 0;
    status=portable_sdl_audio_queue_submit(&audio->queue,output);
    if(status!=PORTABLE_SDL_AUDIO_OK&&status!=PORTABLE_SDL_AUDIO_DROPPED)
        fprintf(stderr,"route audio queue failed: frame=%u status=%s\n",
            audio->session.rendered_frames,portable_sdl_audio_status_name(status));
    return status==PORTABLE_SDL_AUDIO_OK||status==PORTABLE_SDL_AUDIO_DROPPED;
}

/* Preserve original producer order: a later cue may replace an earlier song. */
static int route_audio_render_scene(void *user,uint16_t buttons,int prestart,
    const RaceAudioInput *snapshot,const PortableGpRaceSnapshot *rivals)
{
    RouteAudioHost *audio=user;
    if(audio==NULL)return 0;
    if (!route_audio_course_loop(audio)) return 0;
    if (!route_audio_lap_music(audio, snapshot, rivals)) return 0;
    if (!route_audio_finish_music(audio, snapshot, rivals)) return 0;
    if (!route_audio_countdown(audio, snapshot, rivals)) return 0;
    if (!route_audio_driver_cues(audio, snapshot)) return 0;
    if (!route_audio_engine(audio, buttons, prestart, snapshot, rivals)) return 0;
    if (!route_audio_lakitu(audio, prestart, snapshot, rivals)) return 0;
    return route_audio_submit_frame(audio);
}

int route_audio_render(void *user,uint16_t buttons,int prestart,
    const PortableRecoveredTimeTrialSnapshot *snapshot)
{
    RaceAudioInput input = {0};
    if (snapshot != NULL)
        input = time_trial_audio_input(snapshot);
    return route_audio_render_scene(user,buttons,prestart,snapshot != NULL ? &input : NULL,NULL);
}

int route_audio_render_gp(RouteAudioHost *audio,uint16_t buttons,
                                 const PortableGpRaceSnapshot *snapshot)
{
    RaceAudioInput shared={0};
    if(audio==NULL)return 1;
    if(snapshot!=NULL){
        if(!audio->race_paused&&!route_audio_driver_events(audio,&snapshot->engine_sound,
                snapshot->engine_scene_stop))return 0;
        if(!audio->race_paused){
            const PortableRaceMusicView view={snapshot->player_star_uses,snapshot->lightning_uses,
                audio->race_timeline.course_song,snapshot->player_star_active,snapshot->lightning_music_active,
                snapshot->music_finished,snapshot->music_final_lap,snapshot->player_boo_active};
            PortableRaceMusicEvent events[6];
            const size_t count=portable_race_music_step(&audio->race_music,&view,events);
            for(size_t i=0u;i<count;++i)
                if(!route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_START_SONG,0u,events[i].song_id) ||
                   !route_audio_apply(audio,audio->session.rendered_frames,
                        PORTABLE_AUDIO_COMMAND_SET_TEMPO,0u,events[i].tempo_scale))return 0;
        }
        shared.phase=snapshot->phase;
        shared.charged_audio_cue_count=snapshot->modern_drift_release_serial;
        shared.position_x_q16=snapshot->player_x_q16;
        shared.position_y_q16=snapshot->player_y_q16;
        if(!audio->race_paused)
            for(size_t i=0u;i<PORTABLE_GP_OPPONENT_COUNT;++i){
                const PortableGpOpponentSnapshot *opponent=&snapshot->opponents[i];
                if(opponent->fall_audio_serial==audio->last_fall_audio_serial[i])continue;
                audio->last_fall_audio_serial[i]=opponent->fall_audio_serial;
                if(opponent->visible&&!route_audio_hazard_apply(audio,opponent->voice_class,
                    PORTABLE_DRIVER_HAZARD_VOID,opponent->fall_audio_variant,1,
                    snapshot->engine_sound.state_flags,portable_gp_item_spatial_volume(
                        opponent->position_x_q16,opponent->position_y_q16,
                        snapshot->player_x_q16,snapshot->player_y_q16)))return 0;
            }
        shared.engine_sound=snapshot->engine_sound;
        shared.engine_sound.hazard_audio_kind=0u;
        shared.engine_sound.water_recovery_started=shared.engine_sound.retrieval_started=shared.engine_sound.water_rescue_arrived=0u;
        shared.engine_sound.surface_jump_started=shared.engine_sound.protected_hop_started=shared.engine_sound.landed=0u;
        shared.engine_revs=snapshot->engine_revs;
        shared.engine_revs_owned=snapshot->engine_revs_owned;
        shared.engine_scene_stop=snapshot->engine_scene_stop;
        shared.contact_audio=&snapshot->contact_audio;
        shared.boost_voice=&snapshot->boost_voice;
        shared.cannonball_sounds=&snapshot->cannonball_sounds;
        shared.piranha_sounds=&snapshot->piranha_sounds;
        shared.thwomp_sounds=&snapshot->thwomp_sounds;
        shared.prop_sounds=&snapshot->prop_sounds;
        shared.impact_sounds=&snapshot->impact_sounds;
        shared.lakitu_state=snapshot->lakitu_state;
        shared.lakitu_y=snapshot->lakitu_y;
        for(uint8_t index=0u;index<snapshot->audio_event_count;++index){
            const PortableGpRaceAudioEvent *event=&snapshot->audio_events[index];
            const uint32_t tick=audio->session.rendered_frames;
            if(event->operation!=PORTABLE_GP_AUDIO_SONG){
                if(event->operation!=PORTABLE_GP_AUDIO_MUSIC_REQUEST&&
                    event->operation!=PORTABLE_GP_AUDIO_MUSIC_RELEASE)return 0;
                PortableRaceMusicEvent music={0};
                const int changed=event->operation==PORTABLE_GP_AUDIO_MUSIC_REQUEST?
                    portable_race_music_request(&audio->race_music,event->music_effect,
                        snapshot->music_finished,audio->race_timeline.course_song,&music):
                    portable_race_music_release(&audio->race_music,event->music_effect,
                        snapshot->music_finished,audio->race_timeline.course_song,snapshot->music_final_lap,&music);
                if(changed&&(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,0u,music.song_id)||
                    !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_TEMPO,0u,music.tempo_scale)))return 0;
                continue;
            }
            /* 805B7D0 starts 102 only if player7 is idle or already on it. */
            if(event->unless_busy&&event->player<PORTABLE_AUDIO_PLAYER_COUNT&&
               portable_audio_session_player_playing(&audio->session,event->player)&&
               audio->session.players[event->player].song_id!=event->song_id)continue;
            if(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_START_SONG,
                                  event->player,event->song_id)||
               !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_VOLUME,
                                  event->player,event->volume)||
               !route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_SET_PITCH,
                                  event->player,(uint16_t)event->pitch))
                return 0;

        }
    }
    if(snapshot!=NULL && !audio->race_paused){
        PortableStarSoundSource sources[PORTABLE_GP_OPPONENT_COUNT];
        for(size_t i=0u;i<PORTABLE_GP_OPPONENT_COUNT;++i){
            const PortableGpOpponentSnapshot *opponent=&snapshot->opponents[i];
            sources[i]=(PortableStarSoundSource){opponent->position_x_q16,
                opponent->position_y_q16,(uint8_t)(opponent->visible&&opponent->star_active)};
        }
        PortableNearbyStarControl nearby;
        /* Native racing camera anchor follows the immutable player pose. */
        portable_nearby_star_audio_step(&audio->nearby_star_audio,
            snapshot->player_x_q16,snapshot->player_y_q16,snapshot->engine_sound.state_flags,
            sources,PORTABLE_GP_OPPONENT_COUNT,&nearby);
        const uint32_t tick=audio->session.rendered_frames;
        const uint8_t slot=portable_audio_session_song_player(audio->session.pack,136u);
        if((nearby.start && !route_audio_apply(audio,tick,
                PORTABLE_AUDIO_COMMAND_START_SONG,slot,136u)) ||
           (nearby.apply_volume && !route_audio_apply(audio,tick,
                PORTABLE_AUDIO_COMMAND_SET_VOLUME,slot,nearby.volume)))return 0;
    }
    return route_audio_render_scene(audio,buttons,0,snapshot!=NULL?&shared:NULL,snapshot);
}

int route_audio_set_paused(void *user,int paused)
{
    RouteAudioHost *audio=user;
    const uint32_t tick=audio!=NULL?audio->session.rendered_frames:0u;
    if(audio==NULL)return 0;
    audio->race_paused=paused!=0;
    if(paused){
        for(uint8_t player=0u;player<PORTABLE_AUDIO_PLAYER_COUNT;++player)
            if(!route_audio_apply(audio,tick,
               PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER,player,0u))return 0;
        portable_sdl_audio_queue_pause(&audio->queue,1);
        portable_sdl_audio_queue_pause(&audio->queue,0);
        return route_audio_apply(audio,tick+1u,
            PORTABLE_AUDIO_COMMAND_START_SONG,9u,142u);
    }
    if(!route_audio_apply(audio,tick,PORTABLE_AUDIO_COMMAND_STOP_PLAYER,
                          9u,0u))return 0;
    for(uint8_t player=0u;player<PORTABLE_AUDIO_PLAYER_COUNT;++player)
        if(player!=9u&&!route_audio_apply(audio,tick,
           PORTABLE_AUDIO_COMMAND_RESUME_PLAYER,player,0u))return 0;
    return 1;
}

int route_audio_begin_race(RouteAudioHost *audio,uint32_t course_id,
                                  uint32_t character_id, uint8_t engine_class)
{
    uint16_t song;
    if(audio==NULL||character_id<1u||character_id>PORTABLE_ENGINE_DRIVER_COUNT||
       engine_class>=PORTABLE_ENGINE_CLASS_COUNT||
       !portable_engine_sound_rules_load(audio->session.pack,&audio->engine_rules)||
       !portable_surface_sound_rules_load(audio->session.pack,course_id,&audio->surface_rules)||
       !portable_hazard_sound_rules_load(audio->session.pack,&audio->hazard_rules)||
       !portable_finish_sound_rules_load(audio->session.pack,&audio->finish_rules)||
       !portable_frontend_course_song(audio->session.pack,course_id,&song)||
       !portable_race_audio_timeline_begin(
       &audio->race_timeline,song,audio->engine_rules.coast_songs[character_id-1u]))return 0;
    audio->race_course_id=course_id;
    audio->race_character_id=character_id;
    audio->race_engine_class=engine_class;
    audio->race_paused=0;
    audio->race_music=(PortableRaceMusic){0};
    audio->haunt_music=0u;
    audio->finish_audio=(PortableFinishAudio){0};
    audio->result_replay_active=0u;
    audio->lap_audio=(PortableLapAudio){0};
    audio->nearby_star_audio=(PortableNearbyStarAudio){0};
    audio->lakitu_audio=(PortableLakituAudio){0};
    audio->lakitu_pulses=0u;
    audio->course_loop_starts=0u;
    audio->lap_cues=audio->final_lap_starts=audio->final_lap_restores=0u;
    audio->session.memory[2]=0u;
    portable_engine_control_init(&audio->engine_control);
    audio->nearby_engine=(PortableNearbyEngineState){0};
    audio->surface_audio=(PortableSurfaceAudioState){0};
    audio->last_charged_audio_cue_count=0u;
    audio->last_contact_audio_serial=0u;
    audio->last_boost_voice_serial=0u;
    memset(audio->last_fall_audio_serial,0,sizeof audio->last_fall_audio_serial);
    audio->last_cannonball_audio_serial=0u;
    audio->last_piranha_audio_serial=0u;
    audio->last_thwomp_audio_serial=0u;
    audio->last_prop_audio_serial=0u;
    audio->last_impact_audio_serial=0u;
    return 1;
}

static int mesh_audio_failure(uint32_t course, uint32_t tick, const char *detail)
{
    fprintf(stderr, "3D audio course=%u mode=mesh tick=%u: %s\n", course, tick, detail);
    return 0;
}

int route_audio_rebind_pack(RouteAudioHost *audio, const PortableAssetPackView *pack)
{
    if (!audio || !pack)
        return 0;
    /* The large mixer stays heap-owned. Validate a clean candidate before
     * retiring any active songs, and repair its two internal tuning borrows
     * after publishing it. Queue and capture ownership never move. */
    PortableAudioSession *candidate = malloc(sizeof *candidate);
    if (!candidate)
        return mesh_audio_failure(audio->race_course_id, 0u, "mixer allocation failed");
    const PortableAudioSessionStatus status = portable_audio_session_init(candidate, pack);
    if (status != PORTABLE_AUDIO_SESSION_OK || !candidate->pcm.tuning || !candidate->psg.tuning)
    {
        free(candidate);
        return mesh_audio_failure(audio->race_course_id, 0u, "invalid audio player/tuning profile");
    }
    audio->session = *candidate;
    free(candidate);
    audio->session.pcm.tuning = &audio->session.tuning;
    audio->session.psg.tuning = &audio->session.tuning;
    portable_sdl_audio_queue_pause(&audio->queue, 1);
    portable_sdl_audio_queue_pause(&audio->queue, 0);
    audio->mesh_audio = (PortableMeshAudioState){0};
    audio->race_paused = 0;
    audio->race_course_id = 0;
    return 1;
}

static int mesh_audio_song_valid(PortableAudioPackSong *scratch,
                                  const PortableAssetPackView *pack, uint16_t song)
{
    return song != 0u && portable_audio_session_song_player(pack, song) <
        PORTABLE_AUDIO_PLAYER_COUNT &&
        ((song & (PORTABLE_AUDIO_NDS_SONG | PORTABLE_AUDIO_N64_SONG)) ||
         portable_audio_pack_song_init(scratch, pack, song) == PORTABLE_AUDIO_PACK_SONG_OK);
}

int route_audio_mesh_begin(RouteAudioHost *audio, const PortableMeshAudioConfig *configuration)
{
    if (!audio || !configuration || !configuration->pack || !configuration->course_id ||
        !configuration->driver_id || configuration->voice_class >= 8u ||
        configuration->engine_class >= PORTABLE_ENGINE_CLASS_COUNT ||
        (configuration->mode != 1u && configuration->mode != 2u && configuration->mode != 4u &&
         configuration->mode != 8u && configuration->mode != 16u))
        return mesh_audio_failure(configuration ? configuration->course_id : 0u, 0u,
                                   "invalid audio configuration");
    uint16_t music = 0;
    if (configuration->music_kind != UINT32_MAX || configuration->music_id != UINT32_MAX)
    {
        const int nds = configuration->music_kind == PORTABLE_ASSET_SECTION_NDS_SEQUENCE;
        const int n64 = configuration->music_kind == PORTABLE_ASSET_SECTION_N64_SEQUENCE;
        if ((!nds && !n64 && configuration->music_kind != PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE) ||
            configuration->music_id == 0u ||
            configuration->music_id > (nds ? 0x7fffu : n64 ? 0x1fffu : UINT16_MAX))
            return mesh_audio_failure(configuration->course_id, 0u,
                                       "unsupported META music kind/id");
        music = (uint16_t)(configuration->music_id |
                           (nds ? PORTABLE_AUDIO_NDS_SONG : n64 ? PORTABLE_AUDIO_N64_SONG : 0u));
    }
    PortableEngineSoundRules engine;
    PortableFinishSoundRules finish;
    PortableAssetSectionView engine_section, finish_section;
    if (portable_asset_pack_find_section(configuration->pack,
            PORTABLE_ASSET_SECTION_ENGINE_SOUND_RULES, 1u, &engine_section) != PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(configuration->pack,
            PORTABLE_ASSET_SECTION_FINISH_SOUND_RULES, 1u, &finish_section) != PORTABLE_ASSET_PACK_OK ||
        !engine_section.bytes || !finish_section.bytes ||
        !portable_engine_sound_rules_load(configuration->pack, &engine) ||
        !portable_finish_sound_rules_load(configuration->pack, &finish))
        return mesh_audio_failure(configuration->course_id, 0u, "missing engine/finish audio rules");
    PortableHazardSoundRules hazard;
    if (!portable_hazard_sound_rules_load(configuration->pack, &hazard))
        return mesh_audio_failure(configuration->course_id, 0u, "missing hazard impact voices");
    uint16_t mushroom_voices[8][2] = {{0}};
    if (configuration->items_enabled &&
        (!configuration->human_mask || !(configuration->human_mask & 1u) ||
         !portable_recovered_mushroom_voices(configuration->pack, mushroom_voices)))
        return mesh_audio_failure(configuration->course_id, 0u, "missing/ambiguous item voice table");
    PortableAudioPackSong *scratch = malloc(sizeof *scratch);
    if (!scratch)
        return mesh_audio_failure(configuration->course_id, 0u, "song preflight allocation failed");
    const unsigned driver = configuration->voice_class;
    const uint16_t cues[] = {375u, 376u, 374u, 371u, 372u, 101u, 114u, 115u, 19u, 142u, 157u, 159u, 140u, 138u, 161u, 133u, 169u, 384u, 170u, 382u, 383u, 380u, 386u, 387u, 388u, 389u,
        engine.idle_songs[driver], engine.coast_songs[driver], engine.throttle_songs[driver]};
    int valid = music == 0u || mesh_audio_song_valid(scratch, configuration->pack, music);
    for (size_t i = 0; valid && i < sizeof cues / sizeof cues[0]; ++i)
        valid = mesh_audio_song_valid(scratch, configuration->pack, cues[i]);
    for (unsigned i = 0; valid && i < 4u; ++i)
        valid = (finish.sequences[i].cue == 0u ||
                 mesh_audio_song_valid(scratch, configuration->pack, finish.sequences[i].cue)) &&
                mesh_audio_song_valid(scratch, configuration->pack, finish.sequences[i].music);
    if (valid)
        valid = mesh_audio_song_valid(scratch, configuration->pack, finish.lap_song) &&
                mesh_audio_song_valid(scratch, configuration->pack, finish.battle_songs[0]) &&
                mesh_audio_song_valid(scratch, configuration->pack, finish.battle_songs[1]);
    if (configuration->items_enabled)
    {
        const uint16_t item_cues[] = {10, 11, 12, 13, 103, 104, 121, 122, 123, 124, 125,
                                     126, 129, 130, 131, 132, 134, 135};
        for (size_t i = 0; valid && i < sizeof item_cues / sizeof item_cues[0]; ++i)
            valid = mesh_audio_song_valid(scratch, configuration->pack, item_cues[i]);
        for (unsigned i = 0; valid && i < 8u; ++i)
            if (configuration->human_mask & (1u << i))
            {
                const unsigned donor = configuration->donor[i];
                valid = donor < 8u && mesh_audio_song_valid(scratch, configuration->pack,
                    mushroom_voices[donor][0]) && mesh_audio_song_valid(scratch,
                    configuration->pack, mushroom_voices[donor][1]);
            }
    }
    PortableSoundSet sound_set = {0};
    if (valid && configuration->sound_set_id)
    {
        if (!portable_sound_set_find(configuration->pack, configuration->sound_set_id, &sound_set))
        {
            free(scratch);
            return mesh_audio_failure(configuration->course_id, 0u, "META mksc:sound-set names no valid sound set");
        }
        for (uint32_t i = 0; valid && i < sound_set.cue_count; ++i)
        {
            const uint16_t song = (uint16_t)(sound_set.cues[i * 4u + 2u] | sound_set.cues[i * 4u + 3u] << 8);
            valid = song == 0u || mesh_audio_song_valid(scratch, configuration->pack, song);
        }
        uint16_t engine;
        if (valid && portable_sound_set_engine(&sound_set, configuration->driver_id, &engine))
            valid = mesh_audio_song_valid(scratch, configuration->pack, engine);
        for (unsigned event = 0; valid && event < PORTABLE_SOUND_VOICE_EVENTS; ++event)
            if (portable_sound_set_voice_event(&sound_set, configuration->driver_id, (PortableSoundVoiceEvent)event,
                                               &engine))
                valid = mesh_audio_song_valid(scratch, configuration->pack, engine);
        for (unsigned i = 0; valid && i < 8u; ++i)
        {
            uint16_t boost, struck;
            if ((configuration->human_mask & (1u << i)) &&
                portable_sound_set_voice(&sound_set, configuration->driver_ids[i], &boost, &struck))
            {
                valid = mesh_audio_song_valid(scratch, configuration->pack, boost) &&
                        mesh_audio_song_valid(scratch, configuration->pack, struck);
                /* Voices follow the donor class; a set voices each human's own driver. */
                const unsigned donor = configuration->donor[i];
                if (valid && donor < 8u)
                {
                    mushroom_voices[donor][0] = mushroom_voices[donor][1] = boost;
                    for (unsigned a = 0; a < 2u; ++a)
                        for (unsigned b = 0; b < 2u; ++b)
                            hazard.impact_voices[donor][a][b] = struck;
                }
            }
        }
    }
    free(scratch);
    if (!valid)
        return mesh_audio_failure(configuration->course_id, 0u,
                                   "missing/unsupported song, voice bank or sample");
    if (!route_audio_rebind_pack(audio, configuration->pack))
        return 0;
    audio->engine_rules = engine;
    audio->finish_rules = finish;
    audio->race_course_id = configuration->course_id;
    audio->race_character_id = configuration->voice_class + 1u;
    audio->race_engine_class = configuration->engine_class;
    audio->race_timeline = (PortableRaceAudioTimeline){.course_song = music, .active = 1};
    audio->finish_audio = (PortableFinishAudio){0};
    audio->lap_audio = (PortableLapAudio){0};
    audio->race_music = (PortableRaceMusic){0};
    audio->mesh_audio = (PortableMeshAudioState){.mode = configuration->mode,
                                                .active = 1, .engine_player = UINT8_MAX,
                                                .items_enabled = configuration->items_enabled,
                                                .human_mask = configuration->human_mask};
    memcpy(audio->mesh_audio.donor, configuration->donor, sizeof audio->mesh_audio.donor);
    memcpy(audio->mesh_audio.driver_ids, configuration->driver_ids, sizeof audio->mesh_audio.driver_ids);
    audio->mesh_audio.driver_ids[0] = configuration->driver_id;
    audio->mesh_audio.last_rank = UINT8_MAX;
    /* Surface run loops and positional sounds take players no stock race
     * sound holds: 3 and the pause jingle's 9 (silent while racing). */
    audio->mesh_audio.run_player = 3u;
    audio->mesh_audio.emitter_player = 9u;
    audio->mesh_audio.emitter_index = UINT32_MAX;
    audio->mesh_audio.sound_set = sound_set;
    audio->mesh_audio.has_sound_set = configuration->sound_set_id != 0u;
    if (audio->mesh_audio.has_sound_set)
    {
        uint16_t song = 0;
        if (portable_sound_set_engine(&sound_set, configuration->driver_id, &song))
            audio->mesh_audio.driver_engine = song;
        if (portable_sound_set_cue(&sound_set, PORTABLE_RACE_FINAL_LAP_SONG, &song) && song)
            audio->mesh_audio.final_lap_song = song;
        printf("3D sound set: course=%u set=0x%08x cues=%u voices=%u engine=0x%04x final_lap=0x%04x\n",
               configuration->course_id, configuration->sound_set_id, sound_set.cue_count, sound_set.voice_count,
               audio->mesh_audio.driver_engine, audio->mesh_audio.final_lap_song);
    }
    /* Drift holds the hop's player once landed; sparks share the boost's,
     * which the release replaces them on. */
    audio->mesh_audio.drift_player = portable_audio_session_song_player(configuration->pack, 157u);
    audio->mesh_audio.spark_player = portable_audio_session_song_player(configuration->pack, 140u);
    audio->hazard_rules = hazard;
    memcpy(audio->mesh_audio.mushroom_voices, mushroom_voices, sizeof mushroom_voices);
    if (configuration->items_enabled)
        for (unsigned i = 0; i < 8u; ++i)
            if (configuration->human_mask & (1u << i))
                printf("3D item voice: racer=%u donor=%u normal=%u small=%u\n", i,
                       configuration->donor[i], mushroom_voices[configuration->donor[i]][0],
                       mushroom_voices[configuration->donor[i]][1]);
    printf("3D audio begin: course=%u driver=%u voice=%u music=%u\n",
           configuration->course_id, configuration->driver_id, driver, music);
    return 1;
}

static int mesh_audio_start(RouteAudioHost *audio, uint16_t song)
{
    return song == 0u || route_audio_apply(audio, audio->session.rendered_frames,
        PORTABLE_AUDIO_COMMAND_START_SONG, PORTABLE_AUDIO_PLAYER_AUTO, song);
}

static int mesh_audio_stop_engine(RouteAudioHost *audio)
{
    if (audio->mesh_audio.engine_player < PORTABLE_AUDIO_PLAYER_COUNT &&
        !route_audio_apply(audio, audio->session.rendered_frames,
            PORTABLE_AUDIO_COMMAND_STOP_PLAYER, audio->mesh_audio.engine_player, 0u))
        return 0;
    audio->mesh_audio.engine_song = 0u;
    audio->mesh_audio.engine_player = UINT8_MAX;
    return 1;
}

static int mesh_audio_engine(RouteAudioHost *audio, const PortableMeshAudioFrame *frame)
{
    const unsigned driver = audio->race_character_id - 1u;
    const unsigned state = (frame->buttons & 1u) ? 2u : frame->speed_q16 != 0 ? 1u : 0u;
    uint16_t song = state == 2u ? audio->engine_rules.throttle_songs[driver] :
        state == 1u ? audio->engine_rules.coast_songs[driver] : audio->engine_rules.idle_songs[driver];
    /* A sound set's engine plays on the stock engine's player. */
    const uint8_t player = portable_audio_session_song_player(audio->session.pack, song);
    uint16_t replaced = 0u;
    const int set_engine = audio->mesh_audio.has_sound_set &&
        portable_sound_set_cue(&audio->mesh_audio.sound_set,
            (uint16_t)(PORTABLE_SOUND_CUE_ENGINE_IDLE + state), &replaced) && replaced;
    if (set_engine)
        song = audio->mesh_audio.driver_engine ? audio->mesh_audio.driver_engine : replaced;
    if (player >= PORTABLE_AUDIO_PLAYER_COUNT)
        return 0;
    if (audio->mesh_audio.engine_song != song)
    {
        if (!mesh_audio_stop_engine(audio) ||
            !route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_START_SONG, player, song))
            return 0;
        audio->mesh_audio.engine_song = song;
        audio->mesh_audio.engine_player = player;
    }
    /* Presentation-only RPM: use a dimensionless speed/cap ratio to select
     * the imported volume curve and pitch range. Metres never enter original
     * driver's speed/rev update equations or feed back into mesh physics. */
    const int64_t speed = frame->speed_q16 < 0 ? -(int64_t)frame->speed_q16 : frame->speed_q16;
    int64_t ratio = speed * 256 / frame->target_speed_q16;
    if (ratio > 256)
        ratio = 256;
    const unsigned bucket = (unsigned)(ratio * (PORTABLE_ENGINE_VOLUME_BUCKETS - 1u) / 256);
    const int32_t range = set_engine && audio->mesh_audio.sound_set.engine_pitch_range
        ? audio->mesh_audio.sound_set.engine_pitch_range
        : audio->engine_rules.profiles[audio->race_engine_class].limit;
    const int16_t pitch = (int16_t)((set_engine ? audio->mesh_audio.sound_set.engine_pitch_base : 0) +
                                    ratio * range / 256);
    return route_audio_apply(audio, audio->session.rendered_frames,
               PORTABLE_AUDIO_COMMAND_SET_VOLUME, player, audio->engine_rules.volumes[driver][bucket]) &&
           route_audio_apply(audio, audio->session.rendered_frames,
               PORTABLE_AUDIO_COMMAND_SET_PITCH, player, (uint16_t)pitch);
}

/* A set's song for surface cue `base` on the floor's sound class, else 0. */
static uint16_t mesh_surface_song(const PortableMeshAudioState *m, uint16_t base, uint8_t surface)
{
    uint16_t song = 0;
    return m->has_sound_set && surface && surface < PORTABLE_SOUND_SURFACE_CLASSES &&
                   portable_sound_set_cue(&m->sound_set, (uint16_t)(base | surface), &song)
               ? song
               : 0u;
}

/* Holds `song` on `player`: restarts it when it changes or has ended (DS
 * slip and run effects are short and the game retriggers them), stops the
 * player for 0. */
static int mesh_audio_hold(RouteAudioHost *audio, uint8_t player, uint16_t *held, uint16_t song)
{
    if (player >= PORTABLE_AUDIO_PLAYER_COUNT || player == audio->mesh_audio.engine_player)
        return 1;
    if (!song)
    {
        if (*held && !route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_STOP_PLAYER,
                                        player, 0u))
            return 0;
        *held = 0u;
        return 1;
    }
    if (*held == song && portable_audio_session_player_playing(&audio->session, player) &&
        audio->session.players[player].song_id == song)
        return 1;
    *held = song;
    return route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_START_SONG, player, song);
}

/* Sound-set drift: a held slip sound while a grounded drift lasts (the
 * floor's own when the set has one) and a cue as each mini-turbo tier is
 * reached. Stock sounds have neither. */
static int mesh_audio_drift(RouteAudioHost *audio, const PortableMeshAudioFrame *frame)
{
    PortableMeshAudioState *m = &audio->mesh_audio;
    uint16_t song = 0;
    const uint8_t drifting = frame->drifting && !frame->boost_released;
    if (m->has_sound_set && drifting)
    {
        song = mesh_surface_song(m, PORTABLE_SOUND_CUE_SURFACE_DRIFT, frame->surface_sound);
        if (!song && !portable_sound_set_cue(&m->sound_set, PORTABLE_SOUND_CUE_DRIFT, &song))
            song = 0;
    }
    if (m->has_sound_set && m->drift_player != m->engine_player && song != m->drift_song)
        printf("3D drift sound: course=%u tick=%u surface=%u song=0x%04x\n", audio->race_course_id, frame->tick,
               frame->surface_sound, song);
    if (m->has_sound_set && !mesh_audio_hold(audio, m->drift_player, &m->drift_song, song))
        return 0;
    const uint8_t tier = drifting ? frame->drift_tier : 0u;
    if (m->has_sound_set && tier > m->drift_tier && m->spark_player < PORTABLE_AUDIO_PLAYER_COUNT &&
        m->spark_player != m->engine_player &&
        portable_sound_set_cue(&m->sound_set,
            tier == 1u ? PORTABLE_SOUND_CUE_SPARK_FIRST : PORTABLE_SOUND_CUE_SPARK_SECOND, &song) && song &&
        !route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_START_SONG,
            m->spark_player, song))
        return 0;
    if (m->has_sound_set && tier > m->drift_tier)
        printf("3D drift sound: course=%u tick=%u drifting=%u tier=%u\n", audio->race_course_id, frame->tick,
               drifting, tier);
    m->drifting = drifting;
    m->drift_tier = tier;
    return 1;
}

/* The floor's landing sound, else the stock landing cue (which the set may
 * replace), and the floor's held driving sound while rolling over it. */
static int mesh_audio_surface(RouteAudioHost *audio, const PortableMeshAudioFrame *frame)
{
    PortableMeshAudioState *m = &audio->mesh_audio;
    if (frame->landed)
    {
        const uint16_t land = mesh_surface_song(m, PORTABLE_SOUND_CUE_SURFACE_LAND, frame->surface_sound);
        const uint8_t player = portable_audio_session_song_player(audio->session.pack, 159u);
        if (land)
            printf("3D landing sound: course=%u tick=%u surface=%u song=0x%04x\n", audio->race_course_id,
                   frame->tick, frame->surface_sound, land);
        if (!(land ? player < PORTABLE_AUDIO_PLAYER_COUNT &&
                         route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_START_SONG,
                                           player, land)
                   : mesh_audio_start(audio, 159u)))
            return 0;
    }
    const int rolling = frame->grounded && (frame->speed_q16 > 65536 || frame->speed_q16 < -65536);
    const uint16_t run = rolling ? mesh_surface_song(m, PORTABLE_SOUND_CUE_SURFACE_RUN, frame->surface_sound) : 0u;
    if (run != m->run_song && (run || m->run_song))
        printf("3D run sound: course=%u tick=%u surface=%u song=0x%04x\n", audio->race_course_id, frame->tick,
               frame->surface_sound, run);
    return mesh_audio_hold(audio, m->run_player, &m->run_song, run);
}

/* One v3 voice for racer `racer` (a human), on the stock voice's player. */
static int mesh_audio_voice(RouteAudioHost *audio, const PortableMeshAudioFrame *frame, unsigned racer,
                            PortableSoundVoiceEvent event)
{
    PortableMeshAudioState *m = &audio->mesh_audio;
    uint16_t song = 0;
    if (!m->has_sound_set || racer >= 8u ||
        !portable_sound_set_voice_event(&m->sound_set, m->driver_ids[racer], event, &song))
        return 1;
    const uint8_t player = portable_audio_session_song_player(audio->session.pack, audio->hazard_rules.impact_voices[0][0][0]);
    printf("3D voice: course=%u tick=%u racer=%u event=%u song=0x%04x\n", audio->race_course_id, frame->tick, racer,
           (unsigned)event, song);
    return player < PORTABLE_AUDIO_PLAYER_COUNT &&
           route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_START_SONG, player, song);
}

/* The set's struck voice (a spin), as stock hazard impacts play it. */
static int mesh_audio_struck(RouteAudioHost *audio, const PortableMeshAudioFrame *frame, unsigned racer)
{
    uint16_t boost, struck;
    const PortableMeshAudioState *m = &audio->mesh_audio;
    if (!m->has_sound_set || racer >= 8u || !portable_sound_set_voice(&m->sound_set, m->driver_ids[racer], &boost, &struck))
        return 1;
    printf("3D voice: course=%u tick=%u racer=%u event=struck song=0x%04x\n", audio->race_course_id, frame->tick,
           racer, struck);
    return mesh_audio_start(audio, struck);
}

/* Voices the set gives racers beyond boost: the rocket start, spins and
 * tumbles, hitting a rival, gaining a place and the finish. */
static int mesh_audio_voices(RouteAudioHost *audio, const PortableMeshAudioFrame *frame)
{
    PortableMeshAudioState *m = &audio->mesh_audio;
    if (!m->has_sound_set || m->sound_set.voice_size != 12u + 2u * PORTABLE_SOUND_VOICE_EVENTS)
        return 1;
    const uint8_t rocket = frame->start_boost != 0;
    if (rocket && !m->start_seen && !mesh_audio_voice(audio, frame, 0u, PORTABLE_SOUND_VOICE_START_BOOST))
        return 0;
    m->start_seen = rocket;
    if (frame->hazard_hit && !mesh_audio_struck(audio, frame, 0u))
        return 0;
    for (uint32_t i = 0; frame->item_events && i < frame->item_events->count; ++i)
    {
        const PortableMeshItemEvent *e = &frame->item_events->values[i];
        const int victim = e->target < 8u && (frame->human_mask & (1u << e->target));
        if (e->kind == PORTABLE_MESH_EVENT_HIT || e->kind == PORTABLE_MESH_EVENT_BOMB_CONTACT)
        {
            /* Bananas spin a kart out; shells and bombs tumble it. */
            if (victim && !(e->kind == PORTABLE_MESH_EVENT_HIT && e->item == 0x1005u
                                ? mesh_audio_struck(audio, frame, e->target)
                                : mesh_audio_voice(audio, frame, e->target, PORTABLE_SOUND_VOICE_HEAVY_HIT)))
                return 0;
            if (e->kind == PORTABLE_MESH_EVENT_HIT && e->racer < 8u && e->racer != e->target &&
                (frame->human_mask & (1u << e->racer)) && !victim &&
                !mesh_audio_voice(audio, frame, e->racer, PORTABLE_SOUND_VOICE_ATTACK))
                return 0;
        }
    }
    /* Gaining a place speaks at most every two seconds. */
    if (frame->phase == PORTABLE_MESH_AUDIO_RACING && !frame->finished && m->last_rank != UINT8_MAX &&
        frame->rank < m->last_rank && frame->tick >= m->voice_tick + 120u)
    {
        if (!mesh_audio_voice(audio, frame, 0u, PORTABLE_SOUND_VOICE_OVERTAKE))
            return 0;
        m->voice_tick = frame->tick;
    }
    m->last_rank = frame->rank;
    if ((frame->finished || frame->phase == PORTABLE_MESH_AUDIO_FINISHED) && !m->finish_voiced)
    {
        m->finish_voiced = 1u;
        const PortableSoundVoiceEvent event =
            m->mode == 16u ? (frame->rank == 0u ? PORTABLE_SOUND_VOICE_BATTLE_WIN : PORTABLE_SOUND_VOICE_BATTLE_LOSE)
            : frame->rank == 0u ? PORTABLE_SOUND_VOICE_FINISH_FIRST
            : frame->rank <= 2u ? PORTABLE_SOUND_VOICE_FINISH_PLACED
                                : PORTABLE_SOUND_VOICE_FINISH_LOST;
        if (!mesh_audio_voice(audio, frame, 0u, event))
            return 0;
    }
    return 1;
}

/* Positional sounds: the nearest emitter in range plays on the emitter
 * player, louder as the listener nears it; it restarts each period (or
 * when it ends) and stops out of range. */
static int mesh_audio_emitters(RouteAudioHost *audio, const PortableMeshAudioFrame *frame)
{
    PortableMeshAudioState *m = &audio->mesh_audio;
    const uint8_t player = m->emitter_player;
    if (player >= PORTABLE_AUDIO_PLAYER_COUNT)
        return 1;
    uint32_t best = UINT32_MAX;
    uint64_t best_q = 0;
    uint16_t volume = 0;
    for (uint32_t i = 0; frame->emitters && i < frame->emitter_count; ++i)
    {
        const PortableMeshSoundEmitter *e = &frame->emitters[i];
        uint64_t d2 = 0;
        for (unsigned a = 0; a < 3u; ++a)
        {
            const int64_t d = ((int64_t)e->position_q16[a] - frame->listener_q16[a]) >> 8; /* 1/256 m */
            d2 += (uint64_t)(d * d);
        }
        const uint64_t r = (uint64_t)(e->distance_q16 >> 8);
        if (!e->song || !r || d2 >= r * r)
            continue;
        /* Volume falls linearly with distance: q = 1 - d/r in 1/256. */
        uint64_t d = 0;
        for (uint64_t bit = (uint64_t)1 << 30; bit; bit >>= 1)
            if ((d + bit) * (d + bit) <= d2)
                d += bit;
        const uint64_t q = 256u - d * 256u / r;
        if (best == UINT32_MAX || q > best_q)
        {
            best = i;
            best_q = q;
            volume = (uint16_t)q;
        }
    }
    if (best == UINT32_MAX)
    {
        if (m->emitter_song && !route_audio_apply(audio, audio->session.rendered_frames,
                                                  PORTABLE_AUDIO_COMMAND_STOP_PLAYER, player, 0u))
            return 0;
        m->emitter_song = 0u;
        m->emitter_index = UINT32_MAX;
        return 1;
    }
    const PortableMeshSoundEmitter *e = &frame->emitters[best];
    const int playing = portable_audio_session_player_playing(&audio->session, player) &&
                        audio->session.players[player].song_id == e->song;
    const int restart = m->emitter_index != best || m->emitter_song != e->song ||
                        (e->period ? frame->tick >= m->emitter_started + e->period : !playing);
    if (restart)
    {
        if (m->emitter_index != best || m->emitter_song != e->song)
            printf("3D emitter sound: course=%u tick=%u emitter=%u song=0x%04x\n", audio->race_course_id,
                   frame->tick, best, e->song);
        if (!route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_START_SONG, player,
                               e->song))
            return 0;
        m->emitter_index = best;
        m->emitter_song = e->song;
        m->emitter_started = frame->tick;
    }
    return route_audio_apply(audio, audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_SET_VOLUME, player, volume);
}

static int mesh_audio_items(RouteAudioHost *audio, const PortableMeshAudioFrame *frame)
{
    if (!audio->mesh_audio.items_enabled)
        return frame->items == NULL;
    if (!frame->items || !frame->item_events || !frame->racers ||
        frame->human_mask != audio->mesh_audio.human_mask)
        return 0;
    PortableMeshItemAudioView view = {.world = frame->items, .events = frame->item_events,
        .actor_events = frame->actor_events,
        .source_actor_config = frame->source_actor_config, .source_actors = frame->source_actors,
        .impact_voices = (const uint16_t (*)[2][2])audio->hazard_rules.impact_voices,
        .racers = frame->racers, .mushroom_voices = (const uint16_t (*)[2])audio->mesh_audio.mushroom_voices,
        .ordinal = frame->items->tick, .course_song = audio->race_timeline.course_song,
        .human_mask = frame->human_mask, .primary_human = 0,
        .final_lap = (uint8_t)(frame->lap_count > 1u && frame->laps_completed >= frame->lap_count - 1u),
        .finished = frame->finished};
    memcpy(view.donor, audio->mesh_audio.donor, sizeof view.donor);
    PortableMeshItemAudioOutput output;
    char error[192];
    if (!portable_mesh_item_audio_step(&audio->mesh_audio.item_audio, &view, &output,
                                       error, sizeof error))
    {
        fprintf(stderr, "%s\n", error);
        return 0;
    }
    if (output.stop_164_mask)
    {
        const uint8_t player = portable_audio_session_song_player(audio->session.pack, 164u);
        if (player < PORTABLE_AUDIO_PLAYER_COUNT && audio->session.players[player].song_id == 164u &&
            !route_audio_apply(audio, audio->session.rendered_frames,
                PORTABLE_AUDIO_COMMAND_STOP_PLAYER, player, 0u)) return 0;
    }
    for (size_t i = 0; i < output.count; ++i)
    {
        const PortableMeshItemAudioCue *cue = &output.cues[i];
        const uint8_t player = cue->player == PORTABLE_MESH_ITEM_AUDIO_SONG_PLAYER
            ? portable_audio_session_song_player(audio->session.pack, cue->song) : cue->player;
        if (cue->require_idle && player < PORTABLE_AUDIO_PLAYER_COUNT &&
            portable_audio_session_player_playing(&audio->session, player)) continue;
        /* Capture uses the source spin squeal without restarting a live voice. */
        if (cue->song == 162u && portable_audio_session_song_playing(&audio->session, 162u))
            continue;
        if (player >= PORTABLE_AUDIO_PLAYER_COUNT ||
            !route_audio_apply(audio, audio->session.rendered_frames,
                PORTABLE_AUDIO_COMMAND_START_SONG, player, cue->song) ||
            !route_audio_apply(audio, audio->session.rendered_frames,
                PORTABLE_AUDIO_COMMAND_SET_VOLUME, player, cue->volume))
            return 0;
        printf("3D item cue: course=%u tick=%u song=%u player=%u volume=%u\n",
               audio->race_course_id, frame->tick, cue->song, player, cue->volume);
    }
    PortableRaceMusicEvent music[7];
    size_t count = 0;
    if (output.haunt_active != audio->mesh_audio.haunt_active)
    {
        count += (size_t)(output.haunt_active
            ? portable_race_music_request(&audio->race_music, 2u, frame->finished,
                view.course_song, &music[count])
            : portable_race_music_release(&audio->race_music, 2u, frame->finished,
                view.course_song, view.final_lap, &music[count]));
        audio->mesh_audio.haunt_active = output.haunt_active;
    }
    count += portable_race_music_step(&audio->race_music, &output.music, music + count);
    for (size_t i = 0; i < count; ++i)
    {
        if (!music[i].song_id)
        {
            if (!route_audio_apply(audio, audio->session.rendered_frames,
                    PORTABLE_AUDIO_COMMAND_STOP_PLAYER, 0u, 0u))
                return 0;
        }
        else if (!route_audio_apply(audio, audio->session.rendered_frames,
                     PORTABLE_AUDIO_COMMAND_START_SONG, 0u, music[i].song_id) ||
                 !route_audio_apply(audio, audio->session.rendered_frames,
                     PORTABLE_AUDIO_COMMAND_SET_TEMPO, 0u, music[i].tempo_scale))
            return 0;
    }
    return 1;
}

int route_audio_mesh_tick(RouteAudioHost *audio, const PortableMeshAudioFrame *frame)
{
    if (!audio || !frame || !audio->mesh_audio.active || frame->phase > 2u ||
        frame->revenge_bomb > 1u || (frame->revenge_bomb && audio->mesh_audio.mode != 16u) ||
        frame->target_speed_q16 <= 0 || frame->target_speed_q16 > 256 * 65536 ||
        frame->speed_q16 < -256 * 65536 || frame->speed_q16 > 256 * 65536 ||
        frame->countdown_remaining > UINT16_MAX || frame->laps_completed > UINT16_MAX ||
        (frame->lap_count == 0u && audio->mesh_audio.mode != 16u) || frame->lap_count > UINT16_MAX ||
        frame->laps_completed > frame->lap_count || frame->racer_count == 0u ||
        frame->racer_count > 8u || frame->rank >= frame->racer_count || (frame->buttons & ~15u))
        return mesh_audio_failure(audio ? audio->race_course_id : 0u, frame ? frame->tick : 0u,
                                   "invalid mesh audio frame");
    if (audio->race_paused != (frame->paused != 0u) &&
        !route_audio_set_paused(audio, frame->paused))
        return mesh_audio_failure(audio->race_course_id, frame->tick, "pause transition failed");
    if (frame->paused)
        return 1;
    if (audio->mesh_audio.seen_tick)
    {
        if (frame->tick == audio->mesh_audio.last_tick)
            return 1;
        if (frame->tick < audio->mesh_audio.last_tick)
            return mesh_audio_failure(audio->race_course_id, frame->tick, "accepted tick regressed");
    }
    const PortableRacePhase phase = frame->phase == PORTABLE_MESH_AUDIO_COUNTDOWN ?
        PORTABLE_RACE_PHASE_COUNTDOWN : frame->phase == PORTABLE_MESH_AUDIO_RACING ?
        PORTABLE_RACE_PHASE_RACING : PORTABLE_RACE_PHASE_FINISHED;
    if (!route_audio_course_loop(audio))
        return mesh_audio_failure(audio->race_course_id, frame->tick, "course music loop failed");
    PortableRaceAudioEvent countdown[2];
    const size_t cues = portable_race_audio_countdown_step(&audio->race_timeline, phase,
        (uint16_t)frame->countdown_remaining, 0, countdown);
    for (size_t i = 0; i < cues; ++i)
    {
        if (countdown[i].song_id != 0u && !route_audio_apply(audio, audio->session.rendered_frames,
            PORTABLE_AUDIO_COMMAND_START_SONG, countdown[i].player, countdown[i].song_id))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "countdown/music command failed");
        if (countdown[i].song_id)
            printf("3D countdown cue: course=%u tick=%u remaining=%u song=%u\n",
                   audio->race_course_id, frame->tick, frame->countdown_remaining, countdown[i].song_id);
    }
    PortableLapAudioControl lap = {0};
    if (audio->mesh_audio.mode != 16u)
    {
        /* A sound set's final-lap jingle restarts the course music when it
         * ends, as MK64 does, instead of after the stock wait. */
        const uint16_t jingle = audio->mesh_audio.final_lap_song;
        const int jingle_on = jingle && audio->session.players[0].song_id == jingle;
        if (jingle_on && audio->lap_audio.countdown > 1u &&
            !portable_audio_session_player_playing(&audio->session, 0u))
            audio->lap_audio.countdown = 1u;
        portable_lap_audio_step(&audio->lap_audio, &audio->finish_rules,
            (uint16_t)frame->laps_completed, (uint16_t)frame->lap_count, frame->finished, &lap);
        if (lap.release && jingle_on && portable_audio_session_player_playing(&audio->session, 0u))
        {
            lap.release = 0u;
            audio->lap_audio.countdown = 1u;
        }
    }
    if (!mesh_audio_start(audio, lap.song))
        return mesh_audio_failure(audio->race_course_id, frame->tick, "lap cue failed");
    PortableRaceMusicEvent music = {0};
    const int music_changed = lap.request ? portable_race_music_request(&audio->race_music,
        lap.request, frame->finished, audio->race_timeline.course_song, &music) :
        lap.release ? portable_race_music_release(&audio->race_music, lap.release,
            frame->finished, audio->race_timeline.course_song,
            (uint8_t)(frame->lap_count > 1u && frame->laps_completed >= frame->lap_count - 1u),
            &music) : 0;
    if (music_changed)
    {
        if (music.song_id)
        {
            if (!route_audio_apply(audio, audio->session.rendered_frames,
                    PORTABLE_AUDIO_COMMAND_START_SONG, 0u, music.song_id) ||
                !route_audio_apply(audio, audio->session.rendered_frames,
                    PORTABLE_AUDIO_COMMAND_SET_TEMPO, 0u, music.tempo_scale))
                return mesh_audio_failure(audio->race_course_id, frame->tick, "lap music failed");
        }
        else if (!route_audio_apply(audio, audio->session.rendered_frames,
                      PORTABLE_AUDIO_COMMAND_STOP_PLAYER, 0u, 0u))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "lap music silence failed");
    }
    if (frame->respawning && !audio->mesh_audio.respawning)
    {
        if (!mesh_audio_stop_engine(audio) || !mesh_audio_start(audio, 138u) ||
            !mesh_audio_start(audio, 161u))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "respawn cue failed");
    }
    else if (!frame->respawning && audio->mesh_audio.respawning)
    {
        const uint8_t player = portable_audio_session_song_player(audio->session.pack, 161u);
        if (player < PORTABLE_AUDIO_PLAYER_COUNT &&
            audio->session.players[player].song_id == 161u &&
            !route_audio_apply(audio, audio->session.rendered_frames,
                PORTABLE_AUDIO_COMMAND_STOP_PLAYER, player, 0u))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "respawn stop failed");
    }
    audio->mesh_audio.respawning = frame->respawning != 0u;
    /* Source class8 returns before ordinary engine/rev/hop control. Its
     * footsteps need an explicit accepted SI cadence, not a pixel-speed cast. */
    if (frame->revenge_bomb && audio->mesh_audio.engine_player < PORTABLE_AUDIO_PLAYER_COUNT &&
        !mesh_audio_stop_engine(audio))
        return mesh_audio_failure(audio->race_course_id, frame->tick, "Bomb engine stop failed");
    if (!frame->revenge_bomb && !frame->finished &&
        frame->phase != PORTABLE_MESH_AUDIO_FINISHED && !frame->respawning)
    {
        if (!mesh_audio_engine(audio, frame) ||
            (frame->hopped && !mesh_audio_start(audio, 157u)) ||
            !mesh_audio_surface(audio, frame) ||
            ((frame->boost_released || frame->panel_started) && !mesh_audio_start(audio, 140u)) ||
            !mesh_audio_drift(audio, frame))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "engine/hop/boost cue failed");
    }
    else if (audio->mesh_audio.drifting || audio->mesh_audio.drift_song || audio->mesh_audio.run_song)
    {
        PortableMeshAudioFrame still = *frame;
        still.drifting = still.grounded = still.landed = 0u;
        if (!mesh_audio_drift(audio, &still) || !mesh_audio_surface(audio, &still))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "drift stop failed");
    }
    if (!mesh_audio_items(audio, frame))
        return mesh_audio_failure(audio->race_course_id, frame->tick, "item audio frame failed");
    if (!mesh_audio_voices(audio, frame) || !mesh_audio_emitters(audio, frame))
        return mesh_audio_failure(audio->race_course_id, frame->tick, "voice/positional sound failed");
    if (frame->finished || frame->phase == PORTABLE_MESH_AUDIO_FINISHED)
    {
        if (!audio->finish_audio.started && !mesh_audio_stop_engine(audio))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "finish engine stop failed");
        const uint16_t flags = audio->mesh_audio.mode == 1u ? 0x100u :
            audio->mesh_audio.mode == 2u ? 0x200u : audio->mesh_audio.mode == 4u ? 0x110u :
            audio->mesh_audio.mode == 8u ? 0x400u : 0x800u;
        PortableFinishAudioControl finish;
        portable_finish_audio_step(&audio->finish_audio, &audio->finish_rules, 1u, 4u, flags,
            frame->rank, frame->racer_count, frame->record_changed, &finish);
        for (uint8_t player = 0; player < PORTABLE_AUDIO_PLAYER_COUNT; ++player)
            if ((finish.stop_mask & (1u << player)) && !route_audio_apply(audio,
                audio->session.rendered_frames, PORTABLE_AUDIO_COMMAND_STOP_PLAYER, player, 0u))
                return mesh_audio_failure(audio->race_course_id, frame->tick, "finish stop failed");
        if (!mesh_audio_start(audio, finish.song))
            return mesh_audio_failure(audio->race_course_id, frame->tick, "finish cue failed");
    }
    if (!route_audio_submit_frame(audio))
        return mesh_audio_failure(audio->race_course_id, frame->tick, "mixer/queue failed");
    audio->mesh_audio.last_tick = frame->tick;
    audio->mesh_audio.seen_tick = 1;
    return 1;
}

int route_audio_cue(void *user,uint16_t song)
{
    return user!=NULL&&route_audio_menu_cue(user,song);
}

/* 7FD8: Select (control 0x2000) honks the local character's ECB78 horn. */
int route_audio_horn(void *user)
{
    RouteAudioHost *audio=user;
    if(audio==NULL)return 0;
    const uint16_t song=portable_engine_horn_song(&audio->hazard_rules.character_cues,(uint8_t)(audio->race_character_id-1u),
        audio->horn_listener_flags);
    if(audio->race_course_id==0u||audio->race_paused||song==0u)return 1;
    return route_audio_apply(audio,audio->session.rendered_frames,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO,song);
}

int route_audio_restart(void *user)
{
    RouteAudioHost *audio=user;
    if(audio==NULL)return 0;
    portable_audio_session_stop_all(&audio->session);
    portable_sdl_audio_queue_pause(&audio->queue,1);
    portable_sdl_audio_queue_pause(&audio->queue,0);
    audio->race_paused=0;
    audio->race_music=(PortableRaceMusic){0};
    audio->haunt_music=0u;
    audio->finish_audio=(PortableFinishAudio){0};
    audio->result_replay_active=0u;
    audio->lap_audio=(PortableLapAudio){0};
    audio->nearby_star_audio=(PortableNearbyStarAudio){0};
    audio->lakitu_audio=(PortableLakituAudio){0};
    portable_engine_control_init(&audio->engine_control);
    audio->nearby_engine=(PortableNearbyEngineState){0};
    audio->surface_audio=(PortableSurfaceAudioState){0};
    audio->last_charged_audio_cue_count=0u;
    audio->last_contact_audio_serial=0u;
    audio->last_boost_voice_serial=0u;
    memset(audio->last_fall_audio_serial,0,sizeof audio->last_fall_audio_serial);
    audio->last_cannonball_audio_serial=0u;
    audio->last_piranha_audio_serial=0u;
    audio->last_thwomp_audio_serial=0u;
    audio->last_prop_audio_serial=0u;
    audio->last_impact_audio_serial=0u;
    return route_audio_begin_race(audio,audio->race_course_id,audio->race_character_id,audio->race_engine_class);
}

int route_audio_result_replay(void *user,int active)
{
    RouteAudioHost *audio=user;
    if(audio==NULL)return 0;
    if(active){
        if(audio->result_replay_active)return 0;
        const PortableFinishAudio finish=audio->finish_audio;
        const PortableRaceMusic music=audio->race_music;
        if(!route_audio_restart(audio))return 0;
        audio->result_finish_audio=finish;audio->result_race_music=music;
        audio->result_replay_active=1u;
        return 1;
    }
    if(!audio->result_replay_active)return 1;
    /* Returning to the retained menu must not restart its finish-jingle wait. */
    portable_audio_session_stop_all(&audio->session);
    portable_sdl_audio_queue_pause(&audio->queue,1);
    portable_sdl_audio_queue_pause(&audio->queue,0);
    audio->finish_audio=audio->result_finish_audio;
    audio->race_music=audio->result_race_music;
    audio->result_replay_active=0u;
    return !audio->finish_audio.started || route_audio_apply(audio,
        audio->session.rendered_frames,PORTABLE_AUDIO_COMMAND_START_SONG,
        portable_audio_session_song_player(audio->session.pack,audio->finish_audio.music),audio->finish_audio.music);
}

int route_audio_return_menu(RouteAudioHost *audio)
{
    if(audio==NULL || !route_audio_capture_close(audio))return 0;
    audio->race_timeline.active=0u;
    audio->race_course_id=0u;
    audio->race_paused=0;
    audio->race_music=(PortableRaceMusic){0};
    audio->haunt_music=0u;
    audio->finish_audio=(PortableFinishAudio){0};
    audio->result_replay_active=0u;
    audio->lap_audio=(PortableLapAudio){0};
    audio->nearby_star_audio=(PortableNearbyStarAudio){0};
    audio->lakitu_audio=(PortableLakituAudio){0};
    audio->last_charged_audio_cue_count=0u;
    audio->last_contact_audio_serial=0u;
    audio->last_boost_voice_serial=0u;
    memset(audio->last_fall_audio_serial,0,sizeof audio->last_fall_audio_serial);
    audio->last_cannonball_audio_serial=0u;
    audio->last_piranha_audio_serial=0u;
    audio->last_thwomp_audio_serial=0u;
    audio->last_prop_audio_serial=0u;
    audio->last_impact_audio_serial=0u;
    portable_audio_session_stop_all(&audio->session);
    portable_sdl_audio_queue_pause(&audio->queue,1);
    portable_sdl_audio_queue_pause(&audio->queue,0);
    return route_audio_apply(audio,audio->session.rendered_frames,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO,2u);
}

int route_audio_menu_cue(RouteAudioHost *audio,uint16_t song_id)
{
    return audio==NULL || route_audio_apply(audio,audio->session.rendered_frames,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO,song_id);
}

int route_audio_begin_ceremony(RouteAudioHost *audio,uint16_t song_id)
{
    if(audio==NULL)return 1;
    if(!portable_hazard_sound_rules_load(audio->session.pack,&audio->hazard_rules))return 0;
    audio->race_timeline.active=0u;
    audio->race_course_id=0u;
    audio->race_paused=0;
    audio->race_music=(PortableRaceMusic){0};
    audio->haunt_music=0u;
    audio->finish_audio=(PortableFinishAudio){0};
    audio->result_replay_active=0u;
    audio->lap_audio=(PortableLapAudio){0};
    audio->nearby_star_audio=(PortableNearbyStarAudio){0};
    audio->lakitu_audio=(PortableLakituAudio){0};
    audio->last_charged_audio_cue_count=0u;
    audio->last_contact_audio_serial=0u;
    audio->last_boost_voice_serial=0u;
    memset(audio->last_fall_audio_serial,0,sizeof audio->last_fall_audio_serial);
    audio->last_cannonball_audio_serial=0u;
    audio->last_piranha_audio_serial=0u;
    audio->last_thwomp_audio_serial=0u;
    audio->last_prop_audio_serial=0u;
    audio->last_impact_audio_serial=0u;
    portable_audio_session_stop_all(&audio->session);
    portable_sdl_audio_queue_pause(&audio->queue,1);
    portable_sdl_audio_queue_pause(&audio->queue,0);
    return route_audio_apply(audio,audio->session.rendered_frames,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO,song_id);
}

int route_audio_fade_ceremony(RouteAudioHost *audio)
{
    const uint32_t tick=audio!=NULL?audio->session.rendered_frames:0u;
    if(audio==NULL)return 1;
    for(uint8_t player=0u;player<PORTABLE_AUDIO_PLAYER_COUNT;++player)
        if(player!=7u&&!route_audio_apply(audio,tick,
           PORTABLE_AUDIO_COMMAND_FADE_PLAYER,player,1u))return 0;
    return 1;
}

#endif
