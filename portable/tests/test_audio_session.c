#include "audio_tuning_fixture.h"
#include "audio_session.h"
#include "engine_sound_asset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"audio session failed line %d: %s\n",__LINE__,#condition); \
    return 1; } } while(0)

static int test_player_map(void)
{
    /* Authored mappings exercise nonstandard IDs and mod-selected players. */
    uint8_t bytes[3][48]={{0}};
    const uint16_t songs[3]={17u,139u,301u};
    const uint8_t players[3]={8u,1u,4u};
    PortableAssetSectionView sections[3];
    for(unsigned i=0;i<3u;++i){
        memcpy(bytes[i],"MKSCAUD1",8u);
        bytes[i][8]=32u;bytes[i][10]=2u;
        bytes[i][12]=(uint8_t)songs[i];bytes[i][13]=(uint8_t)(songs[i]>>8);
        bytes[i][14]=1u;bytes[i][17]=players[i];bytes[i][18]=1u;
        bytes[i][20]=1u;bytes[i][24]=32u;bytes[i][28]=48u;
        bytes[i][43]=PORTABLE_AUDIO_EVENT_END;
        sections[i]=(PortableAssetSectionView){.kind=PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE,
            .content_id=songs[i],.version=2u,.bytes=bytes[i],.size=48u};
    }
    PortableAssetPackView pack={.resolved_sections=sections,.section_count=3u};
    for(unsigned i=0;i<3u;++i)
        CHECK(portable_audio_session_song_player(&pack,songs[i])==players[i]);
    CHECK(portable_audio_session_song_player(&pack,302u)==PORTABLE_AUDIO_PLAYER_AUTO);
    CHECK(portable_audio_session_song_player(NULL,17u)==PORTABLE_AUDIO_PLAYER_AUTO);
    bytes[0][17]=10u;
    CHECK(portable_audio_session_song_player(&pack,17u)==10u);
    CHECK(portable_audio_session_song_player(&pack,139u)==1u);
    bytes[0][17]=11u;
    CHECK(portable_audio_session_song_player(&pack,17u)==PORTABLE_AUDIO_PLAYER_AUTO);
    bytes[0][17]=0u;bytes[0][10]=1u;sections[0].version=1u;
    CHECK(portable_audio_session_song_player(&pack,17u)==PORTABLE_AUDIO_PLAYER_AUTO);
    /* Busy queries find explicit producers independently of default routing. */
    PortableAudioSession session={0};
    session.players[6].active=1u;session.players[6].song_id=139u;
    session.players[6].song.runtime.player.tracks[0].active=1u;
    CHECK(portable_audio_session_song_playing(&session,139u));
    CHECK(portable_audio_session_song_loaded(&session,139u));
    CHECK(!portable_audio_session_song_playing(&session,301u));
    session.players[6].song.runtime.player.tracks[0].active=0u;
    CHECK(!portable_audio_session_song_playing(&session,139u));
    CHECK(portable_audio_session_song_loaded(&session,139u));
    session.players[6].active=0u;
    CHECK(!portable_audio_session_song_loaded(&session,139u));
    return 0;
}

static int test_player_rules(void)
{
    uint8_t data[155]={'M','K','S','C','P','R','I','1',2,0,11,0};
    audio_test_tuning_bytes(data+23u);
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_AUDIO_PLAYER_RULES,
        .content_id=1u,.version=2u,.bytes=data,.size=sizeof data};
    PortableAssetPackView pack={.resolved_sections=&section,.section_count=1u};
    PortableAudioSession session;
    data[19]=1u;data[20]=1u;
    CHECK(portable_audio_session_init(&session,&pack)==PORTABLE_AUDIO_SESSION_OK);
    for(size_t i=0u;i<PORTABLE_AUDIO_PLAYER_COUNT;++i)
        CHECK(session.priority_guard[i]==(i==7u||i==8u));
    portable_audio_session_stop_all(&session);
    CHECK(session.pcm.tuning==&session.tuning&&session.psg.tuning==&session.tuning);
    PortableAudioSession moved=session;
    memset(&session,0,sizeof session);
    int16_t silence[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    CHECK(portable_audio_session_render_frame(&moved,silence)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(moved.pcm.tuning==&moved.tuning&&moved.psg.tuning==&moved.tuning);
    CHECK(moved.tuning.pcm_frequency[1]==audio_test_tuning()->pcm_frequency[1]);
    portable_audio_session_stop_all(&moved);
    CHECK(moved.pcm.tuning==&moved.tuning&&moved.psg.tuning==&moved.tuning);
    section.version=1u;section.size=23u;data[8]=1u;
    CHECK(portable_audio_session_init(&session,&pack)==PORTABLE_AUDIO_SESSION_OK);
    /* Explicit routing isolates the missing tuning/priority profile check
     * from AUTO's independent requirement for imported sequence metadata. */
    PortableAudioCommand play={.operation=PORTABLE_AUDIO_COMMAND_START_SONG,
        .argument=2u,.player=0u};
    CHECK(portable_audio_session_apply(&session,&play)==PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES);
    section.version=2u;section.size=sizeof data;data[8]=2u;
    data[12]=2u;
    CHECK(portable_audio_session_init(&session,&pack)==PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES);
    data[12]=0u;section.size--;
    CHECK(portable_audio_session_init(&session,&pack)==PORTABLE_AUDIO_SESSION_BAD_PLAYER_RULES);
    return 0;
}

static int test_control_commands(void)
{
    PortableAudioSession session;
    PortableAssetPackView pack={0};
    PortableAudioCommand command={0};
    CHECK(portable_audio_session_init(&session,&pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(!portable_audio_session_song_playing(&session,168u));
    session.players[6].active=1u;
    session.players[6].song_id=168u;
    CHECK(!portable_audio_session_song_playing(&session,168u));
    session.players[6].song.runtime.player.tracks[1].active=1u;
    CHECK(portable_audio_session_song_playing(&session,168u));
    CHECK(!portable_audio_session_song_playing(&session,169u));
    CHECK(portable_audio_session_player_playing(&session,6u));
    CHECK(!portable_audio_session_player_playing(&session,10u));
    session.players[6].paused=1u;
    CHECK(portable_audio_session_song_playing(&session,168u));
    session.players[6].song.runtime.player.tracks[1].active=0u;
    CHECK(!portable_audio_session_song_playing(&session,168u));
    session.players[6].active=0u;
    session.players[0].active=1u;
    session.players[0].volume=0x100u;
    command.operation=PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[0].paused&&session.applied_commands==1u);
    command.operation=PORTABLE_AUDIO_COMMAND_RESUME_PLAYER;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(!session.players[0].paused&&session.applied_commands==2u);
    command.operation=PORTABLE_AUDIO_COMMAND_STOP_PLAYER;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(!session.players[0].active&&session.applied_commands==3u);
    command.operation=PORTABLE_AUDIO_COMMAND_FADE_PLAYER;
    command.argument=1u;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[0].fade_volume==0x100u&&
          session.players[0].fade_interval==1u&&
          session.players[0].fade_counter==1u&&
          session.applied_commands==4u);
    command.operation=PORTABLE_AUDIO_COMMAND_SET_TEMPO;command.argument=320u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK &&
        session.players[0].song.runtime.tempo_scale==320u);
    return 0;
}

static int test_silence(void)
{
    PortableAudioSession session;
    PortableAssetPackView pack={0};
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    memset(output,0x7f,sizeof output);
    CHECK(portable_audio_session_init(&session,&pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_render_frame(&session,output)==
          PORTABLE_AUDIO_SESSION_OK);
    for(size_t index=0u;index<sizeof output/sizeof output[0];++index)
        CHECK(output[index]==0);
    CHECK(session.rendered_frames==1u);
    return 0;
}

static uint8_t *load(const char *path,size_t *size)
{
    FILE *file=fopen(path,"rb");
    uint8_t *bytes;
    long length;
    if(file==NULL||fseek(file,0L,SEEK_END)!=0)return NULL;
    length=ftell(file);
    if(length<=0||fseek(file,0L,SEEK_SET)!=0){fclose(file);return NULL;}
    bytes=malloc((size_t)length);
    if(bytes==NULL||fread(bytes,1u,(size_t)length,file)!=(size_t)length){
        free(bytes);fclose(file);return NULL;
    }
    fclose(file);*size=(size_t)length;return bytes;
}

static uint32_t hash_samples(uint32_t hash,const int16_t *samples,size_t count)
{
    for(size_t index=0u;index<count;++index){
        const uint16_t value=(uint16_t)samples[index];
        hash=(hash^(uint8_t)value)*UINT32_C(16777619);
        hash=(hash^(uint8_t)(value>>8))*UINT32_C(16777619);
    }
    return hash;
}

static int render_file(const PortableAssetPackView *pack,uint32_t *out)
{
    PortableAudioSession session;
    PortableAudioCommand music={0u,0u,2u,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO};
    PortableAudioCommand cue={60u,1u,141u,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO};
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    uint32_t hash=UINT32_C(2166136261);
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_apply(&session,&music)==PORTABLE_AUDIO_SESSION_OK);
    for(unsigned frame=0u;frame<600u;++frame){
        if(frame==60u)
            CHECK(portable_audio_session_apply(&session,&cue)==
                  PORTABLE_AUDIO_SESSION_OK);
        CHECK(portable_audio_session_render_frame(&session,output)==
              PORTABLE_AUDIO_SESSION_OK);
        hash=hash_samples(hash,output,sizeof output/sizeof output[0]);
    }
    CHECK(session.players[0].active&&session.players[0].song_id==2u);
    CHECK(session.players[8].active&&session.players[8].song_id==141u);
    *out=hash;
    return 0;
}

static int render_voice_file(const PortableAssetPackView *pack,uint16_t song_id,
                             uint32_t *out)
{
    PortableAudioSession session;
    PortableAudioCommand voice={0u,0u,song_id,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO};
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    uint32_t hash=UINT32_C(2166136261);
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_apply(&session,&voice)==
          PORTABLE_AUDIO_SESSION_OK);
    for(unsigned frame=0u;frame<120u;++frame){
        CHECK(portable_audio_session_render_frame(&session,output)==
              PORTABLE_AUDIO_SESSION_OK);
        hash=hash_samples(hash,output,sizeof output/sizeof output[0]);
    }
    CHECK(session.players[2].active&&session.players[2].song_id==song_id);
    *out=hash;
    return 0;
}

static int render_reaction_file(const PortableAssetPackView *pack,
    uint16_t song_id,uint8_t player_id,uint32_t *out)
{
    PortableAudioSession session;
    PortableAudioCommand command={0u,0u,song_id,
        PORTABLE_AUDIO_COMMAND_START_SONG,player_id};
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    int audible=0;
    uint32_t hash=UINT32_C(2166136261);
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    for(unsigned frame=0u;frame<240u;++frame){
        CHECK(portable_audio_session_render_frame(&session,output)==
              PORTABLE_AUDIO_SESSION_OK);
        for(size_t i=0u;i<sizeof output/sizeof output[0];++i)audible|=output[i]!=0;
        hash=hash_samples(hash,output,sizeof output/sizeof output[0]);
    }
    CHECK(audible);
    CHECK(session.players[player_id].active&&
          session.players[player_id].song_id==song_id);
    *out=hash;
    return 0;
}

static int control_engine_file(const PortableAssetPackView *pack)
{
    PortableAudioSession session;
    PortableAudioCommand command={0u,0u,88u,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO};
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    command.operation=PORTABLE_AUDIO_COMMAND_SET_VOLUME;
    command.player=1u; command.argument=0xe2u;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    command.operation=PORTABLE_AUDIO_COMMAND_SET_PITCH;
    command.argument=0x0fc0u;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[1].active&&session.players[1].song_id==88u&&
          session.players[1].volume==0xe2u&&
          session.players[1].pitch==0x0fc0&&
          session.players[1].song.runtime.tracks[0].volume_x==0x38u&&
          session.players[1].song.runtime.tracks[0].pitch_x==0x0fc0);
    return 0;
}

static int check_ceremony_fade(const PortableAssetPackView *pack,
                               uint16_t song_id)
{
    PortableAudioSession session;
    PortableAudioCommand command={0u,0u,song_id,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO};
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[0].active&&session.players[0].song_id==song_id);
    command.argument=114u;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[7].active&&session.players[7].song_id==114u);
    command.operation=PORTABLE_AUDIO_COMMAND_FADE_PLAYER;
    command.argument=1u;
    for(uint8_t player=0u;player<PORTABLE_AUDIO_PLAYER_COUNT;++player){
        if(player==7u)continue;
        command.player=player;
        CHECK(portable_audio_session_apply(&session,&command)==
              PORTABLE_AUDIO_SESSION_OK);
    }
    for(unsigned frame=0u;frame<15u;++frame){
        CHECK(portable_audio_session_render_frame(&session,output)==
              PORTABLE_AUDIO_SESSION_OK);
        CHECK(session.players[0].active);
    }
    CHECK(session.players[0].fade_volume==0x10u);
    CHECK(portable_audio_session_render_frame(&session,output)==
          PORTABLE_AUDIO_SESSION_OK);
    CHECK(!session.players[0].active);
    CHECK(session.players[7].active&&session.players[7].song_id==114u&&
          session.players[7].fade_interval==0u);
    return 0;
}

static int check_loop_contract(const PortableAssetPackView *pack,
    uint16_t song_id,uint8_t track_count,uint32_t target_tick,
    uint32_t duration)
{
    PortableAssetSectionView section;
    PortableAudioSequenceView sequence;
    uint8_t loops[PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS]={0};
    CHECK(portable_asset_pack_find_section(pack,
          PORTABLE_ASSET_SECTION_AUDIO_SEQUENCE,song_id,&section)==
          PORTABLE_ASSET_PACK_OK);
    CHECK(portable_audio_sequence_open(&sequence,section.bytes,section.size)==
          PORTABLE_AUDIO_SEQUENCE_OK);
    CHECK(sequence.track_count==track_count);
    for(uint32_t index=0u;index<sequence.event_count;++index){
        PortableAudioSequenceEvent event,target;
        CHECK(portable_audio_sequence_event(&sequence,index,&event)==
              PORTABLE_AUDIO_SEQUENCE_OK);
        if(event.operation!=PORTABLE_AUDIO_EVENT_LOOP)continue;
        CHECK(event.track<track_count&&loops[event.track]==0u);
        CHECK(portable_audio_sequence_event(&sequence,event.target,&target)==
              PORTABLE_AUDIO_SEQUENCE_OK);
        CHECK(target.track==event.track&&target.tick==target_tick);
        CHECK(event.tick-target.tick==duration);
        loops[event.track]=1u;
    }
    for(uint8_t track=0u;track<track_count;++track)CHECK(loops[track]==1u);
    return 0;
}

static int check_pack_pause_position(const PortableAssetPackView *pack,
                                     uint16_t song_id)
{
    PortableAudioSession session;
    PortableAudioCommand command={0u,0u,song_id,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO};
    PortableAudioSongRuntime paused;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    uint32_t before_tick;
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    for(unsigned frame=0u;frame<97u;++frame)
        CHECK(portable_audio_session_render_frame(&session,output)==
              PORTABLE_AUDIO_SESSION_OK);
    command.operation=PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER;
    command.argument=0u;
    command.player=0u;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    paused=session.players[0].song.runtime;
    before_tick=paused.next_tick;
    for(unsigned frame=0u;frame<70u;++frame){
        memset(output,0x7f,sizeof output);
        CHECK(portable_audio_session_render_frame(&session,output)==
              PORTABLE_AUDIO_SESSION_OK);
        for(size_t sample=0u;sample<sizeof output/sizeof output[0];++sample)
            CHECK(output[sample]==0);
    }
    CHECK(memcmp(&paused,&session.players[0].song.runtime,sizeof paused)==0);
    command.operation=PORTABLE_AUDIO_COMMAND_RESUME_PLAYER;
    CHECK(portable_audio_session_apply(&session,&command)==
          PORTABLE_AUDIO_SESSION_OK);
    for(unsigned frame=0u;frame<4u&&
        session.players[0].song.runtime.next_tick==before_tick;++frame)
        CHECK(portable_audio_session_render_frame(&session,output)==
              PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[0].song.runtime.next_tick>before_tick);
    return 0;
}

static int check_reference_loops_and_pause(const PortableAssetPackView *pack)
{
    static const struct {
        uint16_t song_id;
        uint8_t tracks;
        uint32_t target_tick;
        uint32_t duration;
    } cases[]={
        {2u,8u,144u,1152u},
        {21u,6u,96u,3072u},
        {25u,6u,192u,2688u},
        {52u,8u,72u,1632u},
    };
    for(size_t index=0u;index<sizeof cases/sizeof cases[0];++index){
        CHECK(check_loop_contract(pack,cases[index].song_id,
              cases[index].tracks,cases[index].target_tick,
              cases[index].duration)==0);
        CHECK(check_pack_pause_position(pack,cases[index].song_id)==0);
        printf("song=%u loop=%u+%u pause_frames=70 position=preserved\n",
               cases[index].song_id,cases[index].target_tick,
               cases[index].duration);
    }
    return 0;
}

static int check_finite_finish_owner(const PortableAssetPackView *pack)
{
    PortableFinishSoundRules rules;
    PortableAudioSession session;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    CHECK(portable_finish_sound_rules_load(pack,&rules));
    const uint16_t song=rules.sequences[0].music;
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    PortableAudioCommand command={.operation=PORTABLE_AUDIO_COMMAND_START_SONG,
        .player=PORTABLE_AUDIO_PLAYER_AUTO,.argument=song};
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_song_loaded(&session,song));
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_song_playing(&session,song));
    unsigned frames=1u;
    while(portable_audio_session_song_playing(&session,song)&&frames++<1200u)
        CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(!portable_audio_session_song_playing(&session,song));
    CHECK(portable_audio_session_song_loaded(&session,song));
    command.operation=PORTABLE_AUDIO_COMMAND_STOP_PLAYER;
    command.player=portable_audio_session_song_player(pack,song);command.argument=0u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(!portable_audio_session_song_loaded(&session,song));
    return 0;
}

static int check_driver_sound(const PortableAssetPackView *pack,uint16_t song)
{
    const uint8_t slot=portable_audio_session_song_player(pack,song);
    PortableAudioSession session;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    int audible=0;
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    const PortableAudioCommand command={0u,0u,song,
        PORTABLE_AUDIO_COMMAND_START_SONG,PORTABLE_AUDIO_PLAYER_AUTO};
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[slot].song_id==song);
    for(unsigned frame=0u;frame<16u;++frame){
        CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
        for(size_t i=0u;i<sizeof output/sizeof output[0];++i)
            audible|=output[i]!=0;
    }
    CHECK(audible);
    printf("driver sound=%u player=%u audible\n",song,slot);
    return 0;
}

static int check_driver_tire_sounds(const PortableAssetPackView *pack)
{
    const uint16_t songs[]={385u,103u,104u,121u,130u,131u,70u,71u,72u,74u,75u,76u,77u,80u,81u,82u,83u,84u,94u,95u,96u,97u,98u,99u,100u,107u,108u,19u,12u,13u,150u,5u,6u,7u,33u,34u,136u,139u,155u,156u,157u,158u,159u,163u,164u,177u,178u,179u,180u};
    for(size_t i=0u;i<sizeof songs/sizeof songs[0];++i)
        CHECK(check_driver_sound(pack,songs[i])==0);
    PortableHazardSoundRules rules;
    CHECK(portable_hazard_sound_rules_load(pack,&rules));
    for(unsigned driver=0;driver<8u;++driver)
        for(unsigned listener=0;listener<2u;++listener)
            for(unsigned size=0;size<2u;++size)
                CHECK(check_driver_sound(pack,rules.voices[driver][listener][size])==0);
    return 0;
}

static int check_countdown_cues(const PortableAssetPackView *pack)
{
    PortableEngineSoundRules engine;
    CHECK(portable_engine_sound_rules_load(pack,&engine));
    for(uint16_t cue=114u;cue<=115u;++cue){
        PortableAudioSession session;int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
        CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
        PortableAudioCommand command={.argument=cue,.operation=PORTABLE_AUDIO_COMMAND_START_SONG,
            .player=PORTABLE_AUDIO_PLAYER_AUTO};
        CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
        CHECK(session.players[7].active && session.players[7].song_id==cue &&
            session.players[7].song.sequence.priority==(cue==114u?100u:101u));
        /* Guarded player7 keeps a light cue against a lower-priority request,
         * including the pending first update. GO101 replaces red100. */
        command.player=7u;command.argument=164u;
        CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK && session.players[7].song_id==cue);
        uint64_t energy=0u;
        for(unsigned frame=0u;frame<12u;++frame){
            CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
            for(unsigned sample=0u;sample<PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u;++sample)
                energy+=(uint64_t)((int32_t)output[sample]*(int32_t)output[sample]);
        }
        CHECK(energy>0u);
        if(cue==114u){
            command.player=PORTABLE_AUDIO_PLAYER_AUTO;command.argument=115u;
            CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK && session.players[7].song_id==115u);
        }
        /* Independent sessions must bind their own mixer pools. Warm the
         * source GP intro and engine together before adding the cue to one;
         * subtracting the unchanged baseline isolates the audible mix delta. */
        PortableAudioSession concurrent[2];
        int16_t mixed[2][PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
        for(size_t side=0u;side<2u;++side){
            CHECK(portable_audio_session_init(&concurrent[side],pack)==PORTABLE_AUDIO_SESSION_OK);
            command=(PortableAudioCommand){.argument=3u,
                .operation=PORTABLE_AUDIO_COMMAND_START_SONG,.player=PORTABLE_AUDIO_PLAYER_AUTO};
            CHECK(portable_audio_session_apply(&concurrent[side],&command)==PORTABLE_AUDIO_SESSION_OK);
            command.argument=engine.coast_songs[0];
            CHECK(portable_audio_session_apply(&concurrent[side],&command)==PORTABLE_AUDIO_SESSION_OK);
            for(unsigned frame=0u;frame<60u;++frame)
                CHECK(portable_audio_session_render_frame(&concurrent[side],mixed[side])==PORTABLE_AUDIO_SESSION_OK);
        }
        CHECK(memcmp(mixed[0],mixed[1],sizeof mixed[0])==0);
        CHECK(portable_audio_session_song_playing(&concurrent[0],engine.coast_songs[0]));
        command.argument=cue;
        CHECK(portable_audio_session_apply(&concurrent[1],&command)==PORTABLE_AUDIO_SESSION_OK);
        int64_t delta_sum=0,delta_squares=0;
        const int64_t samples=12u*PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u;
        for(unsigned frame=0u;frame<12u;++frame){
            for(size_t side=0u;side<2u;++side)
                CHECK(portable_audio_session_render_frame(&concurrent[side],mixed[side])==PORTABLE_AUDIO_SESSION_OK);
            for(size_t sample=0u;sample<PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u;++sample){
                const int32_t delta=(int32_t)mixed[1][sample]-mixed[0][sample];
                delta_sum+=delta;
                delta_squares+=(int64_t)delta*delta;
            }
        }
        /* Source cue square starts at envelope7: +/-7*384=2688 with
         * quarter duty. Even its decaying twelve-frame mix must exceed
         * 1024 AC RMS. The old +/-127 square/raw-wave mix cannot meet this;
         * subtract the mean so DC from low-valued wave nibbles cannot pass. */
        const int64_t centered=delta_squares*samples-delta_sum*delta_sum;
        printf("countdown concurrent cue=%u delta_ac_rms_squared=%lld\n",
            cue,(long long)(centered/(samples*samples)));
        CHECK(centered>samples*samples*INT64_C(1024)*1024);
    }
    return 0;
}

static int check_song_priorities(const PortableAssetPackView *pack)
{
    PortableAudioSession session;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    PortableAudioCommand start={.operation=PORTABLE_AUDIO_COMMAND_START_SONG,
        .player=8u,.argument=136u};
    PortableAudioCommand pause={.operation=PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER,.player=8u};
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.priority_guard[8]&&session.priority_guard[7]&&!session.priority_guard[6]);
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_apply(&session,&pause)==PORTABLE_AUDIO_SESSION_OK);
    start.argument=141u;
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[8].song_id==136u&&session.players[8].paused);
    pause.operation=PORTABLE_AUDIO_COMMAND_RESUME_PLAYER;
    CHECK(portable_audio_session_apply(&session,&pause)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(!session.players[8].start_pending);
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[8].song_id==136u);
    /* Equal priority restarts the song and its controls. */
    start.argument=136u;
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[8].start_pending);
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    pause.operation=PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER;
    CHECK(portable_audio_session_apply(&session,&pause)==PORTABLE_AUDIO_SESSION_OK);
    start.argument=141u;
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[8].song_id==141u&&!session.players[8].paused);
    start.argument=136u;
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    for(unsigned i=0u;i<240u&&portable_audio_session_player_playing(&session,8u);++i)
        CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(!portable_audio_session_player_playing(&session,8u));
    start.argument=141u;
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[8].song_id==141u);
    /* A configured unguarded slot accepts lower priority immediately. */
    start.player=6u;start.argument=136u;
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    start.argument=141u;
    CHECK(portable_audio_session_apply(&session,&start)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.players[6].song_id==141u);
    puts("song priority: pending, playing, equal, paused, ended, unguarded PASS");
    return 0;
}

static unsigned pcm_owned(const PortableAudioSession *session,uint8_t player)
{
    const uint16_t base=(uint16_t)(1u+player*PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS);
    unsigned count=0u;
    for(unsigned i=0u;i<PORTABLE_AUDIO_PCM_VOICES;++i){
        const PortableAudioPcmVoice *voice=&session->pcm.voices[i];
        count+=voice->active&&voice->owner_order>=base&&
            voice->owner_order<base+PORTABLE_AUDIO_SEQUENCE_MAX_TRACKS;
    }
    return count;
}

static int check_shared_voice_pool(const PortableAssetPackView *pack)
{
    PortableAudioSession session;
    PortableAudioCommand command={.operation=PORTABLE_AUDIO_COMMAND_START_SONG,.argument=88u};
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.pcm.voice_count==6u);
    /* Six separate sequencers play one real looping PCM engine each. */
    for(uint8_t player=1u;player<=6u;++player){
        command.player=player;
        CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
        session.players[player].song.sequence.priority=2u;
    }
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    for(uint8_t player=1u;player<=6u;++player)CHECK(pcm_owned(&session,player)==1u);
    command.player=10u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    session.players[10].song.sequence.priority=1u;
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(pcm_owned(&session,10u)==0u&&session.players[10].song.runtime.notes_dropped==1u);
    command.player=0u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    session.players[0].song.sequence.priority=3u;
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(pcm_owned(&session,0u)==1u&&pcm_owned(&session,6u)==0u);
    command.operation=PORTABLE_AUDIO_COMMAND_STOP_PLAYER;command.argument=0u;command.player=6u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(pcm_owned(&session,0u)==1u); /* old owner cannot stop its stolen voice */
    const uint32_t before=session.pcm.voices[0].position;
    const uint32_t other=session.pcm.voices[1].position;
    command.operation=PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER;command.player=1u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.pcm.voices[0].position==before&&session.pcm.voices[1].position!=other);
    command.operation=PORTABLE_AUDIO_COMMAND_RESUME_PLAYER;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.pcm.voices[0].position!=before);
    command.operation=PORTABLE_AUDIO_COMMAND_STOP_PLAYER;command.player=0u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(pcm_owned(&session,0u)==0u&&pcm_owned(&session,1u)==1u);
    portable_audio_session_stop_all(&session);
    for(unsigned i=0u;i<PORTABLE_AUDIO_PCM_VOICES;++i)CHECK(!session.pcm.voices[i].active);
    for(unsigned i=0u;i<PORTABLE_AUDIO_PSG_VOICES;++i)CHECK(!session.psg.voices[i].active);
    puts("shared voice pool: six PCM voices, cross-player priority/ownership/pause PASS");
    return 0;
}

static int check_sequence_notifications(const PortableAssetPackView *pack)
{
    PortableAudioSession session;
    int16_t output[PORTABLE_AUDIO_PCM_FRAME_SAMPLES*2u];
    PortableAudioCommand command={.operation=PORTABLE_AUDIO_COMMAND_START_SONG,
        .player=0u,.argument=36u};
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    session.memory[255]=77u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    unsigned frames=0u;
    while(session.players[0].song.runtime.next_tick<670u&&frames++<2000u){
        CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
        CHECK(session.memory[2]==0u);
    }
    CHECK(frames<2000u&&session.players[0].song.runtime.next_tick<=671u);
    const uint32_t paused_tick=session.players[0].song.runtime.next_tick;
    command.operation=PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER;command.argument=0u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    for(unsigned i=0u;i<100u;++i)
        CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.memory[2]==0u&&session.players[0].song.runtime.next_tick==paused_tick);
    command.operation=PORTABLE_AUDIO_COMMAND_RESUME_PLAYER;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    for(unsigned i=0u;i<10u&&session.memory[2]==0u;++i)
        CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.memory[2]==1u&&session.players[0].song.runtime.next_tick==672u);
    CHECK(session.memory[255]==77u);
    command.operation=PORTABLE_AUDIO_COMMAND_START_SONG;command.argument=105u;
    command.player=9u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.memory[2]==1u&&session.memory[255]==77u);
    command.operation=PORTABLE_AUDIO_COMMAND_STOP_PLAYER;command.argument=0u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.memory[2]==1u&&session.memory[255]==77u);
    session.memory[2]=0u; /* C38C consumes the latch; the old song cannot rewrite it. */
    command.operation=PORTABLE_AUDIO_COMMAND_START_SONG;command.argument=28u;command.player=0u;
    CHECK(portable_audio_session_apply(&session,&command)==PORTABLE_AUDIO_SESSION_OK);
    for(unsigned i=0u;i<100u;++i)
        CHECK(portable_audio_session_render_frame(&session,output)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.memory[2]==0u&&session.memory[255]==77u&&
        portable_audio_session_song_playing(&session,28u));
    portable_audio_session_stop_all(&session);
    CHECK(session.memory[255]==77u);
    CHECK(portable_audio_session_init(&session,pack)==PORTABLE_AUDIO_SESSION_OK);
    CHECK(session.memory[255]==0u);
    puts("sequence notifications: shared, paused, consumed, stop/start lifetime PASS");
    return 0;
}

static int test_file(const char *path)
{
    static const uint16_t voice_songs[]={202u,207u,212u,217u,
        222u,227u,231u,237u};
    static const uint32_t voice_hashes[]={UINT32_C(0xbcaff4fd),
        UINT32_C(0x34929cdd),UINT32_C(0xac0459f5),UINT32_C(0x756fa6f1),
        UINT32_C(0x97b66f29),UINT32_C(0x8c88adc5),UINT32_C(0x9bef91b5),
        UINT32_C(0x50a91cc9)};
    static const uint16_t reaction_songs[]={105u,124u,126u,132u,135u,137u,138u,
        161u,162u,318u,358u};
    static const uint8_t reaction_players[]={9u,4u,10u,4u,6u,5u,1u,5u,5u,2u,4u};
    /* PSG reactions105/126 use the full source DAC gain; PCM goldens stay. */
    static const uint32_t reaction_hashes[]={UINT32_C(0x0e0bd025),
        UINT32_C(0xf54486ed),UINT32_C(0xce173ce5),UINT32_C(0x69e0a1cd),
        UINT32_C(0xa40ffc05),
        UINT32_C(0xf44cd0cd),UINT32_C(0x7563babd),
        UINT32_C(0x43b22141),UINT32_C(0xdc4f8731),
        UINT32_C(0xd3161519),UINT32_C(0xbca5fce5)};
    PortableAssetPackView pack;
    size_t size;
    uint8_t *bytes=load(path,&size);
    uint32_t first,second;
    CHECK(bytes!=NULL);
    CHECK(portable_asset_pack_open(bytes,size,&pack)==PORTABLE_ASSET_PACK_OK);
    CHECK(render_file(&pack,&first)==0&&render_file(&pack,&second)==0);
    PortableHazardSoundRules hazard_rules;
    CHECK(portable_hazard_sound_rules_load(&pack,&hazard_rules));
    for(unsigned character=0u;character<8u;++character)
      for(unsigned rival=0u;rival<2u;++rival)
        for(unsigned variant=0u;variant<2u;++variant){
            uint32_t a,b;
            const uint16_t song=hazard_rules.impact_voices[character][rival][variant];
            CHECK(render_reaction_file(&pack,song,rival?4u:2u,&a)==0 &&
                  render_reaction_file(&pack,song,rival?4u:2u,&b)==0 && a==b);
        }
    puts("course impact voices: PASS all32 imported variants audible and deterministic");
    uint32_t landing_first,landing_second;
    CHECK(render_reaction_file(&pack,384u,6u,&landing_first)==0 &&
          render_reaction_file(&pack,384u,6u,&landing_second)==0);
    CHECK(landing_first==landing_second);
    printf("reaction=384 player=6 checksum=%08x frames=240\n",landing_first);
    for(uint16_t song=375u;song<=376u;++song){
        uint32_t a,b;
        CHECK(render_reaction_file(&pack,song,6u,&a)==0 &&
              render_reaction_file(&pack,song,6u,&b)==0 && a==b);
        printf("umbrella reaction=%u player=6 checksum=%08x frames=240\n",song,a);
    }
    for(size_t index=0u;index<sizeof voice_songs/sizeof voice_songs[0];++index){
        uint32_t voice_first,voice_second;
        CHECK(render_voice_file(&pack,voice_songs[index],&voice_first)==0&&
              render_voice_file(&pack,voice_songs[index],&voice_second)==0);
        CHECK(voice_first==voice_second);
        CHECK(voice_first==voice_hashes[index]);
        printf("voice=%u checksum=%08x frames=120\n",voice_songs[index],
               voice_first);
    }
    for(size_t index=0u;index<sizeof reaction_songs/
        sizeof reaction_songs[0];++index){
        uint32_t reaction_first,reaction_second;
        CHECK(render_reaction_file(&pack,reaction_songs[index],
              reaction_players[index],&reaction_first)==0&&
              render_reaction_file(&pack,reaction_songs[index],
              reaction_players[index],&reaction_second)==0);
        printf("reaction=%u player=%u checksum=%08x frames=240\n",
               reaction_songs[index],reaction_players[index],reaction_first);
        CHECK(reaction_first==reaction_second&&
              reaction_first==reaction_hashes[index]);
    }
    CHECK(check_shared_voice_pool(&pack)==0);
    CHECK(check_sequence_notifications(&pack)==0);
    CHECK(check_song_priorities(&pack)==0);
    CHECK(check_countdown_cues(&pack)==0);
    CHECK(control_engine_file(&pack)==0);
    CHECK(check_driver_tire_sounds(&pack)==0);
    CHECK(check_finite_finish_owner(&pack)==0);
    CHECK(check_ceremony_fade(&pack,56u)==0);
    CHECK(check_ceremony_fade(&pack,59u)==0);
    CHECK(check_reference_loops_and_pause(&pack)==0);
    CHECK(first==second);
    printf("portable audio session file: music=2 cue=141 checksum=%08x "
           "frames=600\n",first);
    free(bytes);
    return 0;
}

static unsigned capture_le32(const uint8_t *p)
{ return (unsigned)p[0]|(unsigned)p[1]<<8u|(unsigned)p[2]<<16u|(unsigned)p[3]<<24u; }
static int test_race_capture(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file!=NULL);
    uint8_t header[44];CHECK(fread(header,1u,sizeof header,file)==sizeof header);
    CHECK(!memcmp(header,"RIFF",4u) && !memcmp(header+8u,"WAVEfmt ",8u) &&
        capture_le32(header+16u)==16u && header[20u]==1u && header[22u]==2u &&
        capture_le32(header+24u)==PORTABLE_AUDIO_PCM_RATE && header[34u]==16u &&
        !memcmp(header+36u,"data",4u));
    const unsigned size=600u*PORTABLE_AUDIO_PCM_FRAME_SAMPLES*4u;
    CHECK(capture_le32(header+40u)==size && capture_le32(header+4u)==size+36u);
    unsigned nonzero=0u;
    for(unsigned i=0u;i<size;++i){const int byte=fgetc(file);CHECK(byte!=EOF);nonzero+=(byte!=0);}
    CHECK(fgetc(file)==EOF && nonzero>size/10u);CHECK(fclose(file)==0);
    puts("bounded race audio capture: PASS stereo16 non-silent complete WAV");return 0;
}

int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--capture"))return test_race_capture(argv[2]);
    if(argc==2)return test_file(argv[1]);
    CHECK(argc==1);
    CHECK(test_player_map()==0);
    CHECK(test_player_rules()==0);
    CHECK(test_control_commands()==0);
    CHECK(test_silence()==0);
    puts("portable audio session: PASS");
    return 0;
}
