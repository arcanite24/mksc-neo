#include "character_audio_fixture.h"
#include "engine_control.h"
#include "engine_sound_asset.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"engine control failed line %d: %s\n",__LINE__,#x); \
    return 1; } } while (0)

static void put16(uint8_t *bytes, uint16_t value)
{
    bytes[0]=(uint8_t)value; bytes[1]=(uint8_t)(value>>8u);
}
static int load_rules(PortableEngineSoundRules *rules)
{
    uint8_t data[380]={0};
    memcpy(data,"ENG1\x03\x00\x08\x00\x10\x00\x04\x00\x06\x00\x04\x00",16u);
    const int16_t profiles[3][6]={{2048,16,-128,-48,-32,0},
        {2560,20,-160,-60,-40,0},{3072,24,-192,-72,-48,0}};
    size_t offset=16u;
    for(unsigned i=0u;i<3u;++i)
        for(unsigned j=0u;j<6u;++j,offset+=2u)put16(data+offset,(uint16_t)profiles[i][j]);
    for(unsigned i=0u;i<128u;++i,offset+=2u)put16(data+offset,(uint16_t)(256u-i));
    for(unsigned i=0u;i<32u;++i,offset+=2u)put16(data+offset,(uint16_t)(85u+i));
    for(unsigned i=0u;i<4u;++i,offset+=2u)put16(data+offset,(uint16_t)(180u-i));
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_ENGINE_SOUND_RULES,
        .content_id=1u,.version=3u,.bytes=data,.size=sizeof data};
    PortableAssetPackView pack={.section_count=1u,.resolved_sections=&section};
    CHECK(portable_engine_sound_rules_load(&pack,rules));
    CHECK(rules->profiles[1].limit==2560 && rules->profiles[1].release==-160);
    CHECK(rules->volumes[7][15]==129u && rules->throttle_songs[7]==108u);
    CHECK(rules->brake_songs[0]==180u && rules->brake_songs[3]==177u);
    const PortableEngineSoundRules saved=*rules;
    section.version=1u;
    CHECK(!portable_engine_sound_rules_load(&pack,rules));
    section.version=3u;
    data[4]=4u;
    CHECK(!portable_engine_sound_rules_load(&pack,rules));
    CHECK(memcmp(rules,&saved,sizeof saved)==0);
    data[4]=3u;put16(data+18u,0xffffu);
    CHECK(!portable_engine_sound_rules_load(&pack,rules));
    put16(data+18u,16u);put16(data+52u,257u);
    CHECK(!portable_engine_sound_rules_load(&pack,rules));
    put16(data+52u,256u);put16(data+308u,0u);
    CHECK(!portable_engine_sound_rules_load(&pack,rules));
    put16(data+308u,85u);--section.size;
    CHECK(!portable_engine_sound_rules_load(&pack,rules));
    pack.section_count=0u;
    CHECK(!portable_engine_sound_rules_load(&pack,rules));
    return 0;
}

static int check_revs(const PortableEngineSoundRules *rules)
{
    PortableEngineControl control;
    portable_engine_control_init(&control);
    for(unsigned tick=0u;tick<126u;++tick)
        portable_engine_control_prestart_tick(&control,0x8000u);
    CHECK(control.accumulator==0xfc0u && control.prestart_mode==0xffffu);
    portable_engine_control_prestart_tick(&control,0x8000u);
    CHECK(control.accumulator==0xdc0u && control.prestart_mode==0xffffu);
    portable_engine_control_prestart_tick(&control,0x8000u);
    CHECK(control.accumulator==0xbc0u && control.prestart_mode==0u);
    portable_engine_control_prestart_tick(&control,0u);
    CHECK(control.accumulator==0xac0u);
    for(unsigned engine=0u;engine<3u;++engine){
        const PortableEngineRevProfile *profile=&rules->profiles[engine];
        PortableDriverEngineSoundInput input={.control_word=0x8000u};
        portable_engine_control_init(&control);
        for(unsigned tick=0u;tick<256u;++tick)
            portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==(uint16_t)profile->limit);
        input.control_word=0xc000u; /* simultaneous brake wins */
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==(uint16_t)(profile->limit+profile->release));
        input.control_word=0x8000u;input.flattened=1u;
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==(uint16_t)(profile->limit+2*profile->release));
        input.flattened=0u;input.response_flags=4u;control.accumulator=(uint16_t)profile->limit;
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==(uint16_t)(profile->limit+profile->drift));
        input.response_flags=0u;input.selector_index=10u;control.accumulator=(uint16_t)(profile->limit/4);
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==(uint16_t)(profile->limit/4+profile->skid));
        control.accumulator=0u;
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==(uint16_t)profile->acceleration);
        input.selector_index=0u;
        for(unsigned mode=1u;mode<=6u;++mode){
            input.mode=(uint8_t)mode;control.accumulator=1000u;
            portable_engine_control_race_tick(&control,profile,&input);
            CHECK(control.accumulator==1000u);
        }
        input.mode=8u;control.accumulator=4000u;
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==(uint16_t)(4000+profile->skid*8));
        input.mode=0u;input.response_status=13u;control.accumulator=10u;
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==0u);
        for(unsigned status=5u;status<=7u;++status){
            input.response_status=(uint8_t)status;control.accumulator=1000u;
            portable_engine_control_race_tick(&control,profile,&input);
            CHECK(control.accumulator==(uint16_t)(1000+profile->skid*8));
            input.spin_delay=1u;control.accumulator=1000u;
            portable_engine_control_race_tick(&control,profile,&input);
            CHECK(control.accumulator==(uint16_t)(1000+profile->acceleration));
            input.spin_delay=0u;
        }
        input.response_status=0u;input.mode=7u;control.accumulator=0xf00u;
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==0xfc0u && control.prestart_mode==0xffffu);
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==0u && control.prestart_mode==0u);
        /* Halfword narrowing precedes the signed cap comparison. */
        input.mode=0u;control.accumulator=0x7fffu;
        portable_engine_control_race_tick(&control,profile,&input);
        CHECK(control.accumulator==0u);
    }
    return 0;
}

static int check_tires(const PortableEngineSoundRules *rules)
{
    PortableEngineControl control={0};
    PortableTireAudioControl tires;
    PortableDriverEngineSoundInput input={.control_word=0x4000u};
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song && !tires.stop_song);
    for(unsigned bucket=0u;bucket<5u;++bucket){
        control=(PortableEngineControl){0};
        input.speed_q16=(int32_t)((bucket<<24u)+1u);
        portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
        CHECK(tires.start_song==rules->brake_songs[bucket>3u?3u:bucket] && control.tire_active);
        input.speed_q16=1;
        portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
        CHECK(!tires.start_song && !tires.stop_song); /* speed changes do not restart */
        input.control_word=0u;
        portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
        CHECK(tires.stop_song==180u && !tires.stop_if_playing && !control.tire_active);
        input.control_word=0x4000u;
    }
    input.speed_q16=-1;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song && !control.tire_active);
    input.speed_q16=1;input.mode=1u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song);
    input.mode=0u;input.state_flags=0x4000u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song);
    input.state_flags=0u;input.control_word=0xc000u;input.response_status=15u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(tires.start_song==164u && tires.apply_volume && tires.volume==256u);
    CHECK(control.tire_active && (control.flags&0x10u));
    input.control_word|=0x100u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song && tires.volume==478u);
    input.control_word=0xc200u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song && tires.volume==478u);
    input.mode=4u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song && !tires.stop_song && !tires.apply_volume);
    input.mode=0u;
    portable_engine_tire_audio_tick(&control,rules,&input,1,&tires);
    CHECK(tires.stop_song==164u && tires.stop_if_playing && control.tire_active);
    CHECK(!(control.flags&0x10u));
    input.control_word=0x4000u;input.response_status=0u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(!tires.start_song && !tires.stop_song); /* source keeps ownership */
    input.control_word=0u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(tires.stop_song==180u && !control.tire_active);
    input.state_flags=0x20u;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(tires.stop_song==164u && tires.stop_if_playing);
    input.state_flags=0u;input.control_word=0x4000u;input.speed_q16=1;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    input.speed_q16=0;
    portable_engine_tire_audio_tick(&control,rules,&input,0,&tires);
    CHECK(tires.stop_song==180u && !control.tire_active);
    return 0;
}

static int check_audio(const PortableEngineSoundRules *rules)
{
    PortableEngineControl control={0};
    PortableEngineAudioControl audio;
    PortableDriverEngineSoundInput input={0};
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,0,&audio));
    CHECK(audio.curve==0 && audio.bucket==0u && audio.volume==256u && audio.pitch==0 && !audio.start_song);
    control.accumulator=0xc00u;input.control_word=0x8000u;
    CHECK(portable_engine_audio_control(&control,rules,3u,&input,0,1,&audio));
    CHECK(audio.curve==0xfc0 && audio.bucket==15u && audio.volume==193u && audio.pitch==0xfc0);
    CHECK(audio.start_song==rules->throttle_songs[3] && control.flags==1u);
    CHECK(portable_engine_audio_control(&control,rules,3u,&input,0,1,&audio) && !audio.start_song);
    input.driver_mode=8u;input.lightning_timer=1;input.speed_parameter=0x20u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.curve==0x1600 && audio.bucket==15u && audio.pitch==0x1600);
    input=(PortableDriverEngineSoundInput){0};
    for(unsigned tick=0u;tick<64u;++tick){
        CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
        if(tick<63u) CHECK(!audio.start_song);
        else CHECK(audio.start_song==rules->idle_songs[0]);
    }
    CHECK(control.fade==32u && control.flags==2u);
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.volume==80u && control.fade==33u);
    for(unsigned tick=0u;tick<100u;++tick)
        CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(control.fade==96u);
    input.control_word=0x8000u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.start_song==rules->throttle_songs[0] && control.fade==0u);
    input.response_status=13u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.start_song==rules->idle_songs[0] && control.fade==0u);
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio) && !audio.start_song);
    input.mode=2u;input.recovery_scale=32u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.volume==120u && audio.pitch==60);
    input.mode=4u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.stop_engine && audio.start_song==161u && !audio.apply_controls && control.accumulator==0u);
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio) && !audio.start_song);
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,1,1,&audio));
    CHECK(audio.stop_engine && audio.stop_recovery && control.flags==0u);
    input.mode=0u;input.response_status=0u;input.control_word=0u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.start_song==rules->coast_songs[0] && control.fade==96u);
    input.state_flags=0x20u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(audio.stop_engine && !audio.apply_controls && control.flags==0u);
    CHECK(!portable_engine_audio_control(&control,rules,8u,&input,0,1,&audio));
    /* CC80 supplies one-frame19A cues;41244 plays only grounded local bombs.
     * The walk phase and cadence are also used by the rendered bomb sprite. */
    input=(PortableDriverEngineSoundInput){.character_flags=8u};
    control=(PortableEngineControl){0};
    uint8_t walk=1u;
    for(unsigned tick=0u;tick<21u;++tick){
        input.bomb_walk_audio=portable_battle_bomb_walk_tick(&walk,0);
        CHECK((walk&1u)==1u);
        CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
        CHECK(!audio.apply_controls);
        CHECK(audio.start_song==(tick==9u?98u:tick==20u?97u:0u));
    }
    input.height_q16=1;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(!audio.start_song);
    input.height_q16=0;input.bomb_walk_audio=0u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&audio));
    CHECK(!audio.start_song);
    walk=1u;
    for(unsigned tick=0u;tick<22u;++tick){
        const uint8_t cue=portable_battle_bomb_walk_tick(&walk,-1);
        CHECK(cue==(tick==9u?4u:tick==21u?8u:0u));
    }
    /* Exercise every signed input rev: cubic products must wrap like ARM,
     * including synthetic negative values rather than invoking C overflow. */
    input=(PortableDriverEngineSoundInput){.speed_parameter=65535u,.lightning_timer=1};
    for(unsigned value=0u;value<65536u;++value){
        control.accumulator=(uint16_t)value;
        CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,0,&audio));
        CHECK(audio.bucket<16u);
    }
    return 0;
}
static int check_jump_audio(void)
{
    PortableEngineControl control={.flags=0x11u};
    PortableSurfaceAudioState surface={.song=81u,.cell_key=5u};
    PortableDriverEngineSoundInput input={.surface_jump_started=1u,.material=0u};
    PortableJumpAudioControl out;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(out.song==157u&&out.stop_surface&&!out.require_idle&&surface.song==0u&&control.flags==1u);
    input.driver_mode=8u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(out.song==158u&&!out.stop_surface);
    input.material=2u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(out.song==155u);
    input.material=14u;surface.song=81u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(!out.song&&out.stop_surface);
    input.material=12u;surface.song=81u;control.flags=0x11u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(!out.song&&!out.stop_surface&&surface.song==81u&&control.flags==0x11u);
    input.surface_jump_started=0u;input.protected_hop_started=1u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(out.song==156u&&!out.stop_surface&&surface.song==81u&&control.flags==1u);
    input.protected_hop_started=0u;input.landed=1u;input.material=0x40u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(out.song==159u&&out.require_idle&&!out.stop_surface);
    input.material=0x22u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(!out.song&&!out.require_idle);
    input.material=0u;input.surface_jump_started=1u;input.state_flags=0x20u;
    portable_engine_jump_audio(&control,&surface,&input,&out);
    CHECK(!out.song&&!out.stop_surface&&surface.song==81u);
    return 0;
}

static int check_recovery(const PortableEngineSoundRules *rules)
{
    PortableDriverPilotState driver={0};
    driver.source_terrain22_active=1u;
    driver.hop.recovery_started=1u;
    PortableDriverEngineSoundInput input=portable_driver_engine_sound_input(&driver);
    CHECK(input.water_recovery_started&&!input.retrieval_started);
    input.mode=4u;
    PortableEngineControl control={.flags=1u,.accumulator=900u,.fade=32u};
    PortableEngineAudioControl out;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&out));
    CHECK(out.stop_engine&&!out.stop_recovery&&out.entry_song==138u&&out.start_song==161u);
    CHECK(control.flags==0x20u&&control.accumulator==0u&&control.fade==0u);
    input.water_recovery_started=0u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&out));
    CHECK(!out.start_song&&!out.entry_song&&!out.stop_engine);
    input.mode=3u;input.retrieval_started=1u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&out));
    CHECK(out.stop_engine&&out.stop_recovery&&!out.start_song&&control.flags==0u);
    input.retrieval_started=0u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&out));
    CHECK(!out.stop_engine&&!out.stop_recovery&&!out.start_song);
    input.mode=0u;input.control_word=0x8000u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&out));
    CHECK(out.start_song==rules->throttle_songs[0]);
    /* Disabled racers don't start recovery voices or stop the shared tire slot. */
    input.state_flags=0x20u;input.water_recovery_started=1u;input.retrieval_started=1u;
    CHECK(portable_engine_audio_control(&control,rules,0u,&input,0,1,&out));
    CHECK(out.stop_engine&&!out.stop_recovery&&!out.entry_song&&!out.start_song);
    driver.hop.retrieval_started=1u;driver.source_terrain22_active=0u;
    input=portable_driver_engine_sound_input(&driver);
    CHECK(!input.water_recovery_started&&input.retrieval_started);
    return 0;
}

static int check_horn(void)
{
    PortableCharacterAudioRules rules=character_audio_fixture();
    CHECK(portable_engine_horn_song(&rules,0u,0x800u)==220u&&
          portable_engine_horn_song(&rules,2u,1u)==222u&&portable_engine_horn_song(&rules,7u,1u)==227u);
    CHECK(!portable_engine_horn_song(&rules,0u,0u)&&!portable_engine_horn_song(&rules,0u,0x820u)&&
          !portable_engine_horn_song(&rules,0u,0x4800u)&&!portable_engine_horn_song(&rules,8u,1u));
    rules.race_horns[2]=301u;
    CHECK(portable_engine_horn_song(&rules,2u,1u)==301u);
    CHECK(!portable_engine_horn_song(NULL,2u,1u));
    return 0;
}

static int check_surface(void)
{
    uint8_t bytes[60]={'S','R','F','1',2,0,16,0};
    for(unsigned i=0u;i<16u;++i)put16(bytes+8u+i*2u,(uint16_t)(70u+i));
    bytes[48]=6u;bytes[56]=8u;
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_SURFACE_SOUND_RULES,
        .content_id=24u,.version=2u,.bytes=bytes,.size=sizeof bytes};
    PortableAssetPackView pack={.section_count=1u,.resolved_sections=&section};
    PortableSurfaceSoundRules rules;
    CHECK(portable_surface_sound_rules_load(&pack,24u,&rules));
    CHECK(rules.songs[8]==78u&&rules.restart_ticks[8]==6u&&rules.theme==8u);
    PortableSurfaceSoundRules saved=rules;
    bytes[48]=16u;
    CHECK(!portable_surface_sound_rules_load(&pack,24u,&rules));
    CHECK(memcmp(&rules,&saved,sizeof saved)==0);
    bytes[48]=6u;
    put16(bytes+8u,403u);
    CHECK(!portable_surface_sound_rules_load(&pack,24u,&rules));
    CHECK(!portable_surface_sound_rules_load(&pack,25u,&rules));
    rules=saved;
    PortableEngineControl control={0};
    PortableSurfaceAudioState state={0};
    PortableSurfaceAudioControl out;
    PortableDriverEngineSoundInput input={.speed_q16=65536,.material=0x50u};
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(out.start_song==78u&&!out.stop&&state.cell_key==5u);
    /* The minimum span counts down even while moving to another cell. */
    for(unsigned i=0u;i<5u;++i){
        portable_engine_surface_audio_tick(&control,&state,&rules,&input,4*65536,0,0,&out);
        CHECK(!out.start_song&&!out.stop&&state.song==78u);
    }
    CHECK(state.cell_key==0u);
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,4*65536,0,0,&out);
    CHECK(!out.start_song&&state.song==0u);
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,4*65536,0,0,&out);
    CHECK(out.start_song==78u&&state.cell_key==(1024u|5u));
    /* Negative cells truncate toward zero, then wrap their six-bit keys. */
    state=(PortableSurfaceAudioState){0};
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,-4*65536+1,-4*65536,0,&out);
    CHECK(state.cell_key==((63u<<4u)|5u));
    input.material=0x52u;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(out.start_song==79u&&state.cell_key==0u);
    input.mode=4u;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,1,&out);
    CHECK(!out.stop&&state.song==79u);
    input.mode=0u;
    control.flags=0x10u;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,1,&out);
    CHECK(out.stop&&state.song==0u&&!(control.flags&0x10u));
    /* Tire ownership suppresses a surface stop, retaining the burnout flag. */
    state.song=79u;control.tire_active=1u;control.flags=0x10u;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(!out.stop&&!out.start_song&&state.song==0u&&control.flags==0x10u);
    input.speed_q16=-65536;input.response_q8=1;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(out.start_song==79u);
    control.tire_active=0u;input.response_flags=0x8000u;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(!out.stop&&state.song==0u&&control.flags==0x10u);
    input.response_flags=0u;input.response_q8=0;input.speed_q16=0;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(!out.start_song);
    input.response_status=1u;input.response_flags=4u;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(out.start_song==79u);
    const uint8_t blocked[]={5u,6u,7u,8u,13u,15u};
    for(size_t i=0u;i<sizeof blocked;++i){
        state.song=79u;input.response_status=blocked[i];
        portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
        CHECK(out.stop&&state.song==0u);
    }
    input.response_status=1u;input.material=0u;state.song=79u;
    portable_engine_surface_audio_tick(&control,&state,&rules,&input,0,0,0,&out);
    CHECK(out.stop&&!out.start_song&&state.song==0u);
    return 0;
}

static int check_hazards(void)
{
    uint8_t bytes[140]={'H','A','Z','1',2,0,8,0,2,0,2,0};
    for(unsigned i=0u;i<64u;++i)put16(bytes+12u+i*2u,(uint16_t)(200u+i));
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_HAZARD_SOUND_RULES,
        .content_id=1u,.version=1u,.bytes=bytes,.size=sizeof bytes};
    PortableAssetPackView pack={.section_count=1u,.resolved_sections=&section};
    PortableHazardSoundRules rules;
    CHECK(portable_hazard_sound_rules_load(&pack,&rules));
    for(unsigned driver=0u;driver<8u;++driver)
        for(unsigned listener=0u;listener<2u;++listener)
            for(unsigned variant=0u;variant<2u;++variant)
                CHECK(rules.impact_voices[driver][listener][variant]==232u+driver*4u+listener*2u+variant);
    uint8_t extended[204]={0};memcpy(extended,bytes,sizeof bytes);extended[4]=3u;
    for(unsigned i=0u;i<32u;++i)put16(extended+140u+i*2u,(uint16_t)(270u+i));
    section.bytes=extended;section.size=sizeof extended;
    CHECK(portable_hazard_sound_rules_load(&pack,&rules));
    CHECK(rules.taunt[7][1]==285u && rules.laugh[0][0]==286u && rules.laugh[7][1]==301u);
    uint8_t complete[334];memcpy(complete,extended,sizeof extended);complete[4]=4u;
    character_audio_fixture_bytes(complete+204u);
    section.bytes=complete;section.size=sizeof complete;
    CHECK(portable_hazard_sound_rules_load(&pack,&rules));
    CHECK(rules.character_cues.lock_voices[7]==107u&&rules.character_cues.menu_revs[2].volume==302u&&
        rules.character_cues.launch_revs[2].song==162u&&rules.character_cues.menu_horns[4]==0u&&
        rules.character_cues.race_horns[5]==225u&&rules.character_cues.ceremony_voices[6]==246u);
    const PortableHazardSoundRules complete_saved=rules;
    const unsigned malformed[]={204u,222u,284u,332u};
    for(unsigned i=0;i<sizeof malformed/sizeof malformed[0];++i){
        const unsigned at=malformed[i];const uint16_t previous=(uint16_t)(complete[at]|(uint16_t)complete[at+1u]<<8);
        put16(complete+at,at==222u?1024u:403u);
        CHECK(!portable_hazard_sound_rules_load(&pack,&rules));
        CHECK(memcmp(&rules,&complete_saved,sizeof rules)==0);
        put16(complete+at,previous);
    }
    section.size--;CHECK(!portable_hazard_sound_rules_load(&pack,&rules));
    section.bytes=extended;section.size=sizeof extended;
    put16(extended+202u,403u);
    CHECK(!portable_hazard_sound_rules_load(&pack,&rules));
    section.bytes=bytes;section.size=sizeof bytes;
    CHECK(portable_hazard_sound_rules_load(&pack,&rules));
    PortableHazardSoundRules saved=rules;
    section.size=76u;bytes[4]=1u;
    CHECK(!portable_hazard_sound_rules_load(&pack,&rules));
    CHECK(memcmp(&rules,&saved,sizeof rules)==0);
    section.size=sizeof bytes;bytes[4]=2u;
    put16(bytes+138u,403u);
    CHECK(!portable_hazard_sound_rules_load(&pack,&rules));
    CHECK(memcmp(&rules,&saved,sizeof rules)==0);
    for(unsigned driver=0u;driver<8u;++driver)
        for(unsigned variant=0u;variant<2u;++variant){
            PortableHazardAudioControl out;
            portable_engine_hazard_audio(&rules,(uint8_t)driver,PORTABLE_DRIVER_HAZARD_VOID,
                (uint8_t)variant,0,0u,0u,&out);
            CHECK(out.song==200u+driver*4u+variant&&out.volume==256u);
            portable_engine_hazard_audio(&rules,(uint8_t)driver,PORTABLE_DRIVER_HAZARD_VOID,
                (uint8_t)variant,1,0u,0u,&out);
            CHECK(out.song==202u+driver*4u+variant&&out.volume==64u);
            portable_engine_hazard_audio(&rules,(uint8_t)driver,PORTABLE_DRIVER_HAZARD_VOID,
                (uint8_t)variant,1,0u,192u,&out);
            CHECK(out.volume==192u);
            portable_engine_hazard_audio(&rules,(uint8_t)driver,PORTABLE_DRIVER_HAZARD_VOID,
                (uint8_t)variant,1,0x20u,256u,&out);
            CHECK(out.song==0u);
        }
    PortableHazardAudioControl out;
    portable_engine_hazard_audio(&rules,0u,PORTABLE_DRIVER_HAZARD_LAVA,0u,0,0u,256u,&out);
    CHECK(out.song==139u&&out.volume==256u&&out.player==6u);
    portable_engine_hazard_audio(&rules,0u,PORTABLE_DRIVER_HAZARD_LAVA,0u,1,0u,256u,&out);
    CHECK(!out.song);
    portable_engine_hazard_audio(&rules,0u,PORTABLE_DRIVER_HAZARD_BOUNDS,0u,0,0u,256u,&out);
    CHECK(!out.song);
    return 0;
}

static int check_finish_asset(void)
{
    uint8_t data[40]={0};
    memcpy(data,"FIN1\x02\x00\x04\x00",8u);
    for(unsigned i=0u;i<4u;++i){
        put16(data+8u+i*6u,i==3u?0u:108u);
        put16(data+10u+i*6u,110u);put16(data+12u+i*6u,(uint16_t)(5u+(i>2u?2u:i)));
    }
    put16(data+32u,33u);put16(data+34u,34u);
    put16(data+36u,107u);put16(data+38u,172u);
    PortableAssetSectionView section={.kind=PORTABLE_ASSET_SECTION_FINISH_SOUND_RULES,
        .content_id=1u,.version=1u,.bytes=data,.size=sizeof data};
    PortableAssetPackView pack={.section_count=1u,.resolved_sections=&section};
    PortableFinishSoundRules rules={0};
    CHECK(portable_finish_sound_rules_load(&pack,&rules));
    CHECK(rules.sequences[3].cue==0u&&rules.sequences[0].delay==110u&&rules.battle_songs[1]==34u);
    const PortableFinishSoundRules saved=rules;
    data[4]=1u;CHECK(!portable_finish_sound_rules_load(&pack,&rules));data[4]=2u;
    put16(data+10u,0u);CHECK(!portable_finish_sound_rules_load(&pack,&rules));put16(data+10u,110u);
    put16(data+34u,403u);CHECK(!portable_finish_sound_rules_load(&pack,&rules));
    CHECK(memcmp(&rules,&saved,sizeof rules)==0);
    return 0;
}

static int check_nearby(const PortableEngineSoundRules *rules)
{
    PortableNearbyEngineState state={0};
    PortableNearbyEngineControl out;
    PortableNearbyEngineDriver drivers[2]={
        {.visible=1u,.character=0u,.input={.state_flags=0x2000u}},
        {.visible=1u,.character=1u,.input={.state_flags=0x2000u}},
    };
#define STEP(phase,mode,flags,height) CHECK(portable_engine_nearby_tick(&state,rules,drivers,2u,0,0,height,flags,phase,mode,&out))
    STEP(1u,0x100u,0u,0);
    CHECK(!out.start&&!out.apply&&state.song==0u);
    STEP(2u,0x100u,0u,0);
    CHECK(!out.start&&!out.apply);
    STEP(3u,0x100u,0u,0);
    CHECK(out.start&&out.apply&&out.song==rules->nearby_songs[0]&&out.pitch==0&&out.volume==192u&&state.target==1u);
    STEP(3u,0x100u,0u,0);
    CHECK(!out.start&&out.apply);
    drivers[0].input.speed_q16=0x03000000;
    STEP(3u,0x100u,0u,0);
    CHECK(out.pitch==3096&&out.volume==186u);
    drivers[0].input.driver_mode=8u;drivers[0].input.lightning_timer=8;
    drivers[0].doppler=-512;
    STEP(3u,0x100u,0u,0);
    CHECK(out.pitch==4120&&out.volume==184u);
    drivers[0].input.lightning_timer=7;
    STEP(3u,0x100u,0u,0);
    CHECK(out.pitch==3096);
    drivers[0].x_q16=65536;
    STEP(3u,0x100u,0u,0);
    CHECK(out.start&&state.target==2u&&out.song==rules->nearby_songs[1]);
    drivers[0].visible=0u;drivers[1].x_q16=128*65536;
    STEP(3u,0x100u,0u,0);
    CHECK(out.stop&&!out.apply&&!state.active);
    drivers[1].x_q16=127*65536;
    STEP(3u,0x100u,0u,0);
    CHECK(out.start&&out.volume==0u);
    drivers[1].x_q16=-65535;
    STEP(3u,0x100u,0x20u,0);
    CHECK(out.apply&&out.volume==184u); /* truncates -0.999 to zero; GP remains audible */
    STEP(3u,0x800u,0x20u,0);
    CHECK(out.stop&&!out.apply);
    STEP(5u,0x100u,0u,0);
    CHECK(out.stop&&!out.apply);
    STEP(6u,0x100u,0u,0);
    CHECK(!out.stop&&!out.apply);
    state=(PortableNearbyEngineState){0};
    drivers[1].input.character_flags=8u;drivers[1].input.bomb_walk_audio=4u;
    STEP(3u,0x800u,0u,65536);
    CHECK(!out.start&&!out.apply);
    STEP(3u,0x800u,0u,0);
    CHECK(out.start&&out.song==100u&&out.pitch==256&&!state.active);
    drivers[1].input.bomb_walk_audio=8u;
    STEP(3u,0x800u,0u,0);
    CHECK(out.start&&out.song==99u);
    drivers[1].input.bomb_walk_audio=0u;
    STEP(3u,0x800u,0u,0);
    CHECK(!out.start&&!out.apply&&!state.song);
#undef STEP
    return 0;
}

int main(void)
{
    PortableEngineSoundRules rules;
    CHECK(!load_rules(&rules));
    CHECK(!check_revs(&rules));
    CHECK(!check_audio(&rules));
    CHECK(!check_tires(&rules));
    CHECK(!check_surface());
    CHECK(!check_horn());
    CHECK(!check_recovery(&rules));
    CHECK(!check_jump_audio());
    CHECK(!check_hazards());
    CHECK(!check_nearby(&rules));
    CHECK(!check_finish_asset());
    puts("portable engine control: PASS");
    return 0;
}
