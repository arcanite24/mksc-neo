#include "race_audio_timeline.h"

#include "audio_command_stream.h"

int portable_race_audio_timeline_begin(PortableRaceAudioTimeline *timeline,
    uint16_t course_song, uint16_t engine_song)
{
    if(timeline==NULL || course_song==0u || engine_song==0u)return 0;
    *timeline=(PortableRaceAudioTimeline){.course_song=course_song,.engine_song=engine_song,.active=1u};
    return 1;
}

size_t portable_race_audio_timeline_step(PortableRaceAudioTimeline *timeline,
    uint16_t mode_flags,PortableRaceAudioEvent events[2])
{
    size_t count=0u;
    uint16_t frame;
    if(timeline==NULL||events==NULL||!timeline->active)return 0u;
    frame=timeline->frame;
    if(frame==0u)
        /* race_main+0x182: GP without the Quick bit10 plays song_gp_intro. */
        events[count++]=(PortableRaceAudioEvent){
            (mode_flags&0xf10u)==0x100u?3u:4u,PORTABLE_AUDIO_PLAYER_AUTO};
    if(frame==144u){
        /* EF210 invokes50B38 before507D4 in the same decode turn. Quick
         * Run retains GP mode100 beneath its option bit10. */
        if((mode_flags&0xf00u)==0x100u)
            events[count++]=(PortableRaceAudioEvent){150u,0u};
        events[count++]=(PortableRaceAudioEvent){timeline->engine_song,
            PORTABLE_AUDIO_PLAYER_AUTO};
    }
    timeline->frame++;
    return count;
}

size_t portable_race_audio_countdown_step(PortableRaceAudioTimeline *timeline,
    PortableRacePhase phase,uint16_t remaining,int paused,PortableRaceAudioEvent events[2])
{
    if(!timeline || !events || !timeline->active || paused)return 0u;
    if(phase==PORTABLE_RACE_PHASE_FINISHED){
        timeline->active=0u;timeline->music_wait=0u;return 0u;
    }
    if(phase==PORTABLE_RACE_PHASE_COUNTDOWN){
        const uint8_t cue=remaining==120u?1u:remaining==60u?2u:0u;
        if(cue && !(timeline->countdown_cues&cue)){
            timeline->countdown_cues|=cue;
            events[0]=(PortableRaceAudioEvent){114u,7u};return 1u;
        }
        return 0u;
    }
    if(phase!=PORTABLE_RACE_PHASE_RACING)return 0u;
    if(!(timeline->countdown_cues&4u)){
        timeline->countdown_cues|=4u;timeline->music_wait=80u;
        events[0]=(PortableRaceAudioEvent){115u,7u};return 1u;
    }
    if(timeline->music_wait && --timeline->music_wait==0u){
        events[0]=(PortableRaceAudioEvent){timeline->course_song,0u};
        timeline->active=0u;return 1u;
    }
    return 0u;
}

static uint16_t effect_song(uint8_t effect,uint16_t course_song)
{
    switch(effect){
    case 1u:return course_song;
    case 2u:return 13u;
    case 4u:return 11u;
    case 8u:return 12u;
    case 16u:return 10u;
    case 32u:return PORTABLE_RACE_FINAL_LAP_SONG;
    default:return 0u;
    }
}

int portable_race_music_request(PortableRaceMusic *music,uint8_t effect,
    uint8_t finished,uint16_t course_song,PortableRaceMusicEvent *event)
{
    if(music==NULL || event==NULL || finished)return 0;
    const uint8_t before=music->effects;
    music->effects|=effect;
    /* Star's dedicated branch restarts song10 even when effect16 is already
     * present, provided no effect32 or higher has priority. */
    if(before>=effect && !(effect==16u && before<=31u))return 0;
    const uint16_t song=effect_song(effect,course_song);
    if(!song)return 0;
    *event=(PortableRaceMusicEvent){song,256u};return 1;
}

int portable_race_music_release(PortableRaceMusic *music,uint8_t effect,
    uint8_t finished,uint16_t course_song,uint8_t final_lap,PortableRaceMusicEvent *event)
{
    if(music==NULL || event==NULL || finished)return 0;
    music->effects&=(uint8_t)~effect;
    if(music->effects>=effect)return 0;
    for(uint8_t bit=32u;bit>=2u;bit>>=1u)if(music->effects&bit){
        *event=(PortableRaceMusicEvent){effect_song(bit,course_song),256u};return 1;
    }
    /* 523DC restores36 as28 and applies the player's final-lap tempo. */
    *event=(PortableRaceMusicEvent){course_song==36u?28u:course_song,final_lap?320u:256u};
    return 1;
}

size_t portable_race_music_step(PortableRaceMusic *music,const PortableRaceMusicView *view,
    PortableRaceMusicEvent events[6])
{
    if(music==NULL || view==NULL || events==NULL)return 0u;
    size_t count=0u;
    /* Global484E0 expires before the driver can release star16. */
    if(!view->lightning_active && (music->effects&4u))count+=(size_t)portable_race_music_release(
        music,4u,view->finished,view->course_song,view->final_lap,&events[count]);
    if(!view->star_active && (music->effects&16u))count+=(size_t)portable_race_music_release(
        music,16u,view->finished,view->course_song,view->final_lap,&events[count]);
    /* 7B30/C4C0 expire invisibility; 485DC clears it before requesting star. */
    if(!view->boo_active && (music->effects&8u))count+=(size_t)portable_race_music_release(
        music,8u,view->finished,view->course_song,view->final_lap,&events[count]);
    if(view->boo_active && !(music->effects&8u))count+=(size_t)portable_race_music_request(
        music,8u,view->finished,view->course_song,&events[count]);
    if(music->star_uses!=view->star_uses){
        music->star_uses=view->star_uses;
        count+=(size_t)portable_race_music_request(music,16u,view->finished,view->course_song,&events[count]);
    }
    if(music->lightning_uses!=view->lightning_uses){
        music->lightning_uses=view->lightning_uses;
        count+=(size_t)portable_race_music_request(music,4u,view->finished,view->course_song,&events[count]);
    }
    return count;
}

static int32_t source_sound_delta(int32_t listener,int32_t source)
{
    const uint32_t bits=(uint32_t)listener-(uint32_t)source;
    const int64_t value=bits<=INT32_MAX?(int64_t)bits:
        (int64_t)bits-INT64_C(0x100000000);
    /* Original ADDffff/ASR16 truncates negative Q16 deltas toward zero. */
    return (int32_t)(value/65536);
}

void portable_nearby_star_audio_step(PortableNearbyStarAudio *state,
    int32_t listener_x_q16,int32_t listener_y_q16,uint32_t listener_flags,
    const PortableStarSoundSource *sources,size_t count,PortableNearbyStarControl *out)
{
    if(state==NULL || out==NULL || (sources==NULL && count!=0u))return;
    *out=(PortableNearbyStarControl){0};
    if(listener_flags&0x20u)return;
    int32_t nearest=0x40000;
    for(size_t i=0u;i<count;++i){
        if(!sources[i].active)continue;
        const int32_t dx=source_sound_delta(listener_x_q16,sources[i].x_q16);
        const int32_t dy=source_sound_delta(listener_y_q16,sources[i].y_q16);
        const uint32_t bits=(uint32_t)dx*(uint32_t)dx+(uint32_t)dy*(uint32_t)dy;
        const int32_t distance=bits<=INT32_MAX?(int32_t)bits:
            (int32_t)((int64_t)bits-INT64_C(0x100000000));
        if(distance<nearest)nearest=distance;
    }
    if(nearest>0x7fff)return;
    if(state->cooldown==0u){state->cooldown=24u;out->start=1u;}
    else --state->cooldown;
    out->apply_volume=1u;
    out->volume=(uint16_t)(256-nearest/1024);
}

void portable_lap_audio_step(PortableLapAudio *state,
    const PortableFinishSoundRules *rules,uint16_t laps_completed,
    uint16_t lap_count,uint8_t finished,PortableLapAudioControl *out)
{
    if(!state||!rules||!out)return;
    *out=(PortableLapAudioControl){0};
    /* EC790 invokes DDD8 after its F1 wait, even if the driver finished;
     * 523DC itself suppresses the finished driver's music change. */
    if(state->countdown!=0u&&--state->countdown==0u)out->release=32u;
    if(laps_completed==state->laps_completed)return;
    state->laps_completed=laps_completed;
    if(finished||lap_count<2u||laps_completed==0u||laps_completed>=lap_count)return;
    if(laps_completed==lap_count-1u){
        out->request=32u;state->countdown=rules->final_lap_wait;
    }else out->song=rules->lap_song;
}

void portable_finish_audio_step(PortableFinishAudio *state,
    const PortableFinishSoundRules *rules,uint8_t finished,uint8_t phase,
    uint16_t flags,uint8_t rank,uint8_t racer_count,uint8_t record_changed,
    PortableFinishAudioControl *out)
{
    if(!state||!rules||!out)return;
    *out=(PortableFinishAudioControl){0};
    if(state->started){
        if(state->countdown!=0u&&--state->countdown==0u)out->song=state->music;
        return;
    }
    if(!finished||phase<3u||phase>4u)return;
    const uint16_t mode=flags&0xf00u;
    if(mode==0x800u&&phase!=4u)return;
    state->started=1u;
    /* C38C stops music and players5..10 except8; local41244 stops player1. */
    out->stop_mask=0x6e1u;
    if(mode==0x800u){out->song=rules->battle_songs[rank!=0u];return;}
    unsigned sequence;
    if(mode==0x100u)sequence=(flags&0x10u)?(rank==0u?0u:1u):
        (rank==0u?0u:rank<=3u?1u:2u);
    else if(mode==0x200u)sequence=!(flags&0x4000u)&&record_changed?0u:1u;
    else if(mode==0x400u)sequence=rank==0u?0u:
        (racer_count!=0u&&rank>=racer_count-1u?3u:1u);
    else return;
    const PortableFinishSoundSequence *script=&rules->sequences[sequence];
    out->song=script->cue;
    state->countdown=script->delay;
    state->music=script->music;
    if(script->delay==0u)out->song=script->music;
}

uint16_t portable_lakitu_audio_step(PortableLakituAudio *state,
    uint8_t scene_phase,uint16_t actor_state,int16_t actor_y)
{
    const uint16_t kind=actor_state&UINT16_C(0xf000);
    if(scene_phase!=3u || (kind!=0xc000u && kind!=0xe000u) || actor_y<=-24){
        state->counter=0u;
        return 0u;
    }
    const uint16_t song=state->counter==0u?105u:0u;
    state->counter=(uint16_t)(state->counter+1u);
    if(state->counter>21u)state->counter=0u;
    return song;
}
