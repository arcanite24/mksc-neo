#include "race_audio_timeline.h"
#include "audio_command_stream.h"

#include <stdio.h>

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"race audio timeline failed line %d: %s\n",__LINE__, \
            #condition);return 1;} } while(0)

static int test_course(uint16_t engine,uint16_t music)
{
    PortableRaceAudioTimeline timeline;PortableRaceAudioEvent events[2];
    CHECK(portable_race_audio_timeline_begin(&timeline,music,engine));
    for(unsigned frame=0u;frame<=424u;++frame){
        const size_t count=portable_race_audio_timeline_step(&timeline,0x200u,events);
        CHECK(count==(frame==0u||frame==144u?1u:0u));
        if(count)CHECK(events[0].song_id==(frame?engine:4u) && events[0].player==PORTABLE_AUDIO_PLAYER_AUTO);
    }
    CHECK(timeline.active && timeline.frame==425u); /* waits for actual GO */
    return 0;
}

static int test_countdown_boundaries(void)
{
    const uint16_t modes[]={0x100u,0x110u,0x200u,0x400u,0x800u};
    for(unsigned mode=0u;mode<sizeof modes/sizeof modes[0];++mode){
        const unsigned prelude=modes[mode]==0x200u?244u:0u;
        PortableRaceAudioTimeline timeline;PortableRaceAudioEvent events[2];
        CHECK(portable_race_audio_timeline_begin(&timeline,25u,88u));
        unsigned found=0u;
        for(unsigned frame=0u;frame<=prelude+259u;++frame){
            const PortableRacePhase phase=frame<prelude+179u?PORTABLE_RACE_PHASE_COUNTDOWN:PORTABLE_RACE_PHASE_RACING;
            const uint16_t remaining=frame<prelude?180u:phase==PORTABLE_RACE_PHASE_COUNTDOWN?
                (uint16_t)(179u-(frame-prelude)):0u;
            const PortableRaceAudioTimeline before=timeline;
            CHECK(!portable_race_audio_countdown_step(&timeline,phase,remaining,1,events));
            CHECK(timeline.countdown_cues==before.countdown_cues && timeline.music_wait==before.music_wait);
            const size_t count=portable_race_audio_countdown_step(&timeline,phase,remaining,0,events);
            const int red=frame==prelude+59u || frame==prelude+119u;
            const int go=frame==prelude+179u,music=frame==prelude+259u;
            CHECK(count==(size_t)(red||go||music));
            if(count){
                CHECK(events[0].song_id==(red?114u:go?115u:25u));
                CHECK(events[0].player==(music?0u:7u));++found;
            }
            (void)portable_race_audio_timeline_step(&timeline,modes[mode],events);
        }
        CHECK(found==4u && !timeline.active && !timeline.music_wait);
        CHECK(portable_race_audio_timeline_begin(&timeline,25u,88u));
        CHECK(portable_race_audio_countdown_step(&timeline,PORTABLE_RACE_PHASE_COUNTDOWN,120u,0,events)==1u);
        CHECK(!portable_race_audio_countdown_step(&timeline,PORTABLE_RACE_PHASE_COUNTDOWN,120u,0,events));
        CHECK(!portable_race_audio_countdown_step(&timeline,PORTABLE_RACE_PHASE_FINISHED,0u,0,events) && !timeline.active);
        CHECK(portable_race_audio_timeline_begin(&timeline,25u,88u));
        CHECK(portable_race_audio_countdown_step(&timeline,PORTABLE_RACE_PHASE_RACING,0u,0,events)==1u);
        CHECK(!portable_race_audio_countdown_step(&timeline,PORTABLE_RACE_PHASE_FINISHED,0u,0,events));
        for(unsigned frame=0u;frame<100u;++frame)
            CHECK(!portable_race_audio_countdown_step(&timeline,PORTABLE_RACE_PHASE_RACING,0u,0,events));
    }
    return 0;
}

static int test_intro_modes(void)
{
    for(uint16_t flags=0u;flags<0x1000u;++flags){
        PortableRaceAudioTimeline timeline={.frame=144u,.course_song=25u,
            .engine_song=88u,.active=1u};
        PortableRaceAudioEvent events[2];
        const size_t count=portable_race_audio_timeline_step(&timeline,flags,events);
        const int gp=(flags&0xf00u)==0x100u;
        CHECK(count==(gp?2u:1u));
        if(gp)CHECK(events[0].song_id==150u&&events[0].player==0u);
        CHECK(events[count-1u].song_id==88u&&events[count-1u].player==PORTABLE_AUDIO_PLAYER_AUTO);
        CHECK(timeline.frame==145u&&timeline.active);
        timeline.frame=0u;
        CHECK(portable_race_audio_timeline_step(&timeline,flags,events)==1u);
        CHECK(events[0].song_id==((flags&0xf10u)==0x100u?3u:4u));
    }
    return 0;
}

static int test_finish(void)
{
    const PortableFinishSoundRules rules={{{108u,110u,5u},{108u,110u,6u},
        {108u,110u,7u},{0u,110u,7u}},{33u,34u},107u,172u};
    PortableFinishAudio state={0};PortableFinishAudioControl out;
    portable_finish_audio_step(&state,&rules,0u,3u,0x100u,0u,8u,0u,&out);
    CHECK(!out.song&&!out.stop_mask&&!state.started);
    portable_finish_audio_step(&state,&rules,1u,2u,0x100u,0u,8u,0u,&out);
    CHECK(!out.song&&!state.started);
    portable_finish_audio_step(&state,&rules,1u,3u,0x800u,1u,4u,0u,&out);
    CHECK(!out.song&&!state.started);
    for(unsigned mode=0u;mode<4u;++mode)for(unsigned rank=0u;rank<8u;++rank){
        state=(PortableFinishAudio){0};
        const uint16_t flags=(uint16_t)(mode==0u?0x100u:mode==1u?0x110u:mode==2u?0x400u:0x800u);
        portable_finish_audio_step(&state,&rules,1u,4u,flags,(uint8_t)rank,8u,0u,&out);
        CHECK(state.started&&out.stop_mask==0x6e1u);
        if(mode==3u){
            CHECK(out.song==(rank==0u?33u:34u));
            portable_finish_audio_step(&state,&rules,1u,4u,flags,(uint8_t)rank,8u,0u,&out);
            CHECK(!out.song&&!out.stop_mask);continue;
        }
        CHECK(out.song==(mode==2u&&rank==7u?0u:108u));
        const uint16_t expected=rank==0u?5u:mode==0u&&rank>3u?7u:mode==2u&&rank==7u?7u:6u;
        for(unsigned tick=1u;tick<110u;++tick){
            portable_finish_audio_step(&state,&rules,1u,5u,flags,0u,8u,0u,&out);
            CHECK(!out.song&&!out.stop_mask);
        }
        portable_finish_audio_step(&state,&rules,1u,5u,flags,0u,8u,0u,&out);
        CHECK(out.song==expected&&!out.stop_mask);
        portable_finish_audio_step(&state,&rules,1u,5u,flags,0u,8u,0u,&out);
        CHECK(!out.song);
    }
    for(unsigned record=0u;record<2u;++record)for(unsigned remote=0u;remote<2u;++remote){
        state=(PortableFinishAudio){0};
        portable_finish_audio_step(&state,&rules,1u,4u,(uint16_t)(0x200u|(remote?0x4000u:0u)),
            0u,1u,(uint8_t)record,&out);
        CHECK(state.music==(record&&!remote?5u:6u));
    }
    return 0;
}

static int test_laps(void)
{
    const PortableFinishSoundRules rules={.lap_song=107u,.final_lap_wait=172u};
    for(uint16_t total=3u;total<=5u;total+=2u){
        PortableLapAudio state={0};PortableLapAudioControl out;
        PortableRaceMusic music={0};PortableRaceMusicEvent event;
        for(uint16_t lap=0u;lap<total;++lap){
            portable_lap_audio_step(&state,&rules,lap,total,0u,&out);
            CHECK(out.song==(lap!=0u&&lap<total-1u?107u:0u));
            CHECK(out.request==(lap==total-1u?32u:0u)&&!out.release);
            if(out.request){
                CHECK(portable_race_music_request(&music,out.request,0u,36u,&event)&&event.song_id==19u);
                CHECK(!portable_race_music_request(&music,16u,0u,36u,&event));
            }
            portable_lap_audio_step(&state,&rules,lap,total,0u,&out);
            CHECK(!out.song&&!out.request&&!out.release);
        }
        /* One wait turn was consumed by the repeated snapshot above. */
        for(unsigned tick=1u;tick<171u;++tick){
            portable_lap_audio_step(&state,&rules,total-1u,total,0u,&out);
            CHECK(!out.song&&!out.request&&!out.release);
        }
        portable_lap_audio_step(&state,&rules,total-1u,total,0u,&out);
        CHECK(out.release==32u&&state.countdown==0u);
        CHECK(portable_race_music_release(&music,out.release,0u,36u,1u,&event)&&event.song_id==10u);
        CHECK(portable_race_music_release(&music,16u,0u,36u,1u,&event)&&event.song_id==28u&&event.tempo_scale==320u);
        portable_lap_audio_step(&state,&rules,total,total,1u,&out);
        CHECK(!out.song&&!out.request&&!out.release);
    }
    PortableLapAudio state={0};PortableLapAudioControl out;
    portable_lap_audio_step(&state,&rules,2u,3u,1u,&out);
    CHECK(!out.song&&!out.request&&!out.release&&!state.countdown);
    portable_lap_audio_step(&state,&rules,0u,0u,0u,&out);
    CHECK(!out.song&&!out.request&&!out.release);
    return 0;
}

static int test_lakitu(void)
{
    PortableLakituAudio state={0};
    for(unsigned kind=0u;kind<=0xf000u;kind+=0x1000u){
        for(unsigned low=0u;low<5u;++low){
            state=(PortableLakituAudio){0};
            for(unsigned tick=0u;tick<67u;++tick){
                const uint16_t song=portable_lakitu_audio_step(&state,3u,
                    (uint16_t)(kind|low),-23);
                CHECK(song==((kind==0xc000u||kind==0xe000u)&&tick%22u==0u?105u:0u));
            }
        }
    }
    for(unsigned phase=0u;phase<6u;++phase){
        state.counter=10u;
        CHECK(portable_lakitu_audio_step(&state,(uint8_t)phase,0xe004u,100)==0u);
        CHECK(state.counter==(phase==3u?11u:0u));
    }
    state.counter=21u;
    CHECK(portable_lakitu_audio_step(&state,3u,0xe000u,-24)==0u&&state.counter==0u);
    CHECK(portable_lakitu_audio_step(&state,3u,0xe000u,-23)==105u&&state.counter==1u);
    CHECK(portable_lakitu_audio_step(&state,3u,0xe000u,INT16_MIN)==0u&&state.counter==0u);
    CHECK(portable_lakitu_audio_step(&state,3u,0xe004u,INT16_MAX)==105u);
    /* Switching between active actor substates preserves the pulse interval. */
    CHECK(portable_lakitu_audio_step(&state,3u,0xc000u,88)==0u&&state.counter==2u);
    CHECK(portable_lakitu_audio_step(&state,4u,0xc000u,88)==0u&&state.counter==0u);
    return 0;
}

static int test_music_priority(void)
{
    PortableRaceMusic music={0};PortableRaceMusicEvent event,events[6];
    CHECK(portable_race_music_request(&music,4u,0u,25u,&event) && event.song_id==11u);
    CHECK(!portable_race_music_request(&music,4u,0u,25u,&event));
    CHECK(portable_race_music_request(&music,16u,0u,25u,&event) && event.song_id==10u);
    CHECK(!portable_race_music_request(&music,4u,0u,25u,&event) && music.effects==20u);
    CHECK(portable_race_music_release(&music,16u,0u,25u,0u,&event) && event.song_id==11u);
    CHECK(portable_race_music_request(&music,16u,0u,25u,&event));
    CHECK(portable_race_music_request(&music,16u,0u,25u,&event)); /* repeated star restarts */
    CHECK(!portable_race_music_release(&music,4u,0u,25u,0u,&event) && music.effects==16u);
    CHECK(portable_race_music_release(&music,16u,0u,36u,1u,&event) &&
        event.song_id==28u && event.tempo_scale==320u);
    for(unsigned flags=0u;flags<64u;++flags)for(unsigned bit=2u;bit<=32u;bit<<=1u){
        music.effects=(uint8_t)flags;
        CHECK(!portable_race_music_request(&music,(uint8_t)bit,1u,25u,&event) && music.effects==flags);
        CHECK(!portable_race_music_release(&music,(uint8_t)bit,1u,25u,0u,&event) && music.effects==flags);
        const unsigned remaining=flags&~bit;
        const int emitted=portable_race_music_release(&music,(uint8_t)bit,0u,25u,0u,&event);
        CHECK(emitted==(remaining<bit) && music.effects==remaining);
        if(emitted){
            const uint16_t song=(remaining&32u)?19u:(remaining&16u)?10u:(remaining&8u)?12u:
                (remaining&4u)?11u:(remaining&2u)?13u:25u;
            CHECK(event.song_id==song && event.tempo_scale==256u);
        }
    }
    music=(PortableRaceMusic){0};
    PortableRaceMusicView view={.course_song=25u,.lightning_uses=1u,.lightning_active=1u};
    CHECK(portable_race_music_step(&music,&view,events)==1u && events[0].song_id==11u);
    CHECK(portable_race_music_step(&music,&view,events)==0u);
    view.star_uses=1u;view.star_active=1u;
    CHECK(portable_race_music_step(&music,&view,events)==1u && events[0].song_id==10u);
    view.lightning_uses=2u;
    CHECK(portable_race_music_step(&music,&view,events)==0u && music.effects==20u);
    view.star_active=0u;
    CHECK(portable_race_music_step(&music,&view,events)==1u && events[0].song_id==11u);
    view.lightning_active=0u;view.final_lap=1u;
    CHECK(portable_race_music_step(&music,&view,events)==1u && events[0].song_id==25u &&
        events[0].tempo_scale==320u);
    music.effects=20u;view.star_active=0u;view.lightning_active=0u;
    CHECK(portable_race_music_step(&music,&view,events)==1u && events[0].song_id==25u);
    music=(PortableRaceMusic){0};
    view=(PortableRaceMusicView){.course_song=25u,.boo_active=1u};
    CHECK(portable_race_music_step(&music,&view,events)==1u&&events[0].song_id==12u);
    CHECK(portable_race_music_step(&music,&view,events)==0u);
    CHECK(!portable_race_music_request(&music,2u,0u,25u,&event)&&music.effects==10u);
    view.boo_active=0u;
    CHECK(portable_race_music_step(&music,&view,events)==1u&&events[0].song_id==13u);
    CHECK(portable_race_music_release(&music,2u,0u,25u,1u,&event)&&event.song_id==25u&&event.tempo_scale==320u);
    view.boo_active=1u;
    CHECK(portable_race_music_step(&music,&view,events)==1u&&events[0].song_id==12u);
    view.boo_active=0u;view.star_active=1u;view.star_uses=1u;
    CHECK(portable_race_music_step(&music,&view,events)==2u&&events[0].song_id==25u&&events[1].song_id==10u);
    return 0;
}

static int test_nearby_star(void)
{
    PortableNearbyStarAudio state={0};
    PortableNearbyStarControl control;
    PortableStarSoundSource sources[2]={{182*65536,0,1u},{0,0,0u}};
    portable_nearby_star_audio_step(&state,0,0,0u,sources,2u,&control);
    CHECK(!control.start && !control.apply_volume && state.cooldown==0u);
    sources[0].x_q16=181*65536;
    portable_nearby_star_audio_step(&state,0,0,0u,sources,2u,&control);
    CHECK(control.start && control.apply_volume && control.volume==225u && state.cooldown==24u);
    sources[0].x_q16=128*65536;
    portable_nearby_star_audio_step(&state,0,0,0u,sources,2u,&control);
    CHECK(!control.start && control.volume==240u && state.cooldown==23u);
    portable_nearby_star_audio_step(&state,0,0,0x20u,sources,2u,&control);
    CHECK(!control.apply_volume && state.cooldown==23u);
    sources[0].active=0u;
    portable_nearby_star_audio_step(&state,0,0,0u,sources,2u,&control);
    CHECK(!control.apply_volume && state.cooldown==23u);
    sources[0]=(PortableStarSoundSource){300*65536,300*65536,1u};
    sources[1]=(PortableStarSoundSource){128*65536,0,1u};
    portable_nearby_star_audio_step(&state,0,0,0u,sources,2u,&control);
    CHECK(control.volume==240u);
    sources[0]=(PortableStarSoundSource){31*65536+32768,0,1u};
    sources[1].active=0u;
    portable_nearby_star_audio_step(&state,0,0,0u,sources,2u,&control);
    CHECK(control.volume==256u); /* negative -31.5 delta truncates to -31 */
    sources[0].x_q16=INT32_MIN;
    portable_nearby_star_audio_step(&state,INT32_MAX,0,0u,sources,2u,&control);
    CHECK(control.volume==256u); /* wrapped Q16 subtraction gives -1 */
    sources[0].x_q16=0;state=(PortableNearbyStarAudio){0};
    for(unsigned tick=0u;tick<26u;++tick){
        portable_nearby_star_audio_step(&state,0,0,0u,sources,2u,&control);
        CHECK(control.apply_volume && control.volume==256u);
        CHECK(control.start==(tick==0u || tick==25u));
    }
    return 0;
}

int main(void)
{
    CHECK(!test_finish());
    CHECK(!test_lakitu());
    CHECK(!test_laps());
    CHECK(!test_intro_modes());
    CHECK(!test_countdown_boundaries());
    static const uint16_t engines[8]={88u,88u,89u,90u,89u,89u,90u,90u};
    PortableRaceAudioTimeline timeline;
    CHECK(test_music_priority()==0);
    CHECK(test_nearby_star()==0);
    for(uint32_t character=1u;character<=8u;++character)
        CHECK(test_course(engines[character-1u],25u)==0);
    CHECK(test_course(90u,21u)==0);
    CHECK(test_course(88u,27u)==0);
    CHECK(!portable_race_audio_timeline_begin(&timeline,0u,1u));
    CHECK(!portable_race_audio_timeline_begin(&timeline,24u,0u));
    CHECK(!portable_race_audio_timeline_begin(NULL,24u,88u));
    puts("portable race audio timeline: PASS");
    return 0;
}
