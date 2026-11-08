#include "frontend_audio.h"
#include "character_audio_fixture.h"

#include <stdio.h>

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"frontend audio failed line %d: %s\n",__LINE__,#condition); \
    return 1; } } while(0)

static const PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_SLOTS]={
    {1u,1u,0u,0u,0u,0u,0u},{2u,0u,0u,0u,0u,0u,0u},{3u,0u,0u,0u,0u,0u,0u},{4u,0u,0u,0u,0u,0u,0u},{0u,1u,0u,0u,0u,0u,0u},
    {5u,0u,0u,0u,0u,0u,0u},{6u,0u,0u,0u,0u,0u,0u},{7u,0u,0u,0u,0u,0u,0u},{8u,1u,0u,0u,0u,0u,0u},
};
static const PortableFrontendCourseSlot courses[]={{24u,0u,0u,1u}};
static const PortableFrontendConfig config={drivers,
    PORTABLE_FRONTEND_DRIVER_SLOTS,courses,1u};

static int init_audio(PortableFrontendAudio *audio,const PortableFrontendFlow *flow,
    PortableAudioCommandStream *stream)
{
    static PortableCharacterAudioRules rules;
    rules=character_audio_fixture();
    uint8_t players[2][8];
    for(unsigned i=0;i<8u;++i){players[0][i]=3u;players[1][i]=4u;}
    return portable_frontend_audio_init(audio,flow,stream)&&
        portable_frontend_audio_set_character_cues(audio,&rules,(const uint8_t (*)[8])players);
}

static int tick(PortableFrontendFlow *flow,PortableFrontendAudio *audio,
                PortableAudioCommandStream *stream,uint16_t buttons)
{
    return portable_frontend_flow_tick(flow,buttons)&&
           portable_frontend_audio_tick(audio,flow,stream);
}

static int ticks(PortableFrontendFlow *flow,PortableFrontendAudio *audio,
                 PortableAudioCommandStream *stream,unsigned count)
{
    while(count--!=0u)if(!tick(flow,audio,stream,0u))return 0;
    return 1;
}

static int settle(PortableFrontendFlow *flow,PortableFrontendAudio *audio,
                  PortableAudioCommandStream *stream)
{
    while(flow->transition_ticks!=0u)
        if(!tick(flow,audio,stream,0u))return 0;
    return 1;
}

static int test_title_route(void)
{
    PortableFrontendFlow flow;
    PortableFrontendAudio audio;
    PortableAudioCommand storage[32];
    PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_audio_command_stream_init(&stream,storage,32u)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(init_audio(&audio,&flow,&stream));
    CHECK(stream.count==1u&&storage[0].tick==0u&&
          storage[0].argument==53u);
    CHECK(ticks(&flow,&audio,&stream,481u));
    CHECK(settle(&flow,&audio,&stream));
    CHECK(flow.page==PORTABLE_FRONTEND_PAGE_TITLE_MENU);
    CHECK(stream.commands[stream.count-1u].argument==52u);
    CHECK(ticks(&flow,&audio,&stream,70u));
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(tick(&flow,&audio,&stream,0u));
    {
        const uint32_t accept_tick=flow.tick-1u;
        CHECK(stream.commands[stream.count-1u].argument==143u);
        CHECK(ticks(&flow,&audio,&stream,16u));
        CHECK(stream.commands[stream.count-1u].argument==0u&&
              stream.commands[stream.count-1u].operation==
                  PORTABLE_AUDIO_COMMAND_PAUSE_PLAYER&&
              stream.commands[stream.count-1u].tick==accept_tick+17u);
        CHECK(ticks(&flow,&audio,&stream,4u));
        CHECK(stream.commands[stream.count-1u].argument==2u&&
              stream.commands[stream.count-1u].tick==accept_tick+21u);
    }
    return 0;
}

static int test_event_cues(void)
{
    PortableFrontendFlow flow;
    PortableFrontendAudio audio;
    PortableAudioCommand storage[16];
    PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_audio_command_stream_init(&stream,storage,16u)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(init_audio(&audio,&flow,&stream));
    flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    flow.character_cursor=4u;
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_UP));
    CHECK(storage[stream.count-1u].argument==141u);
    CHECK(tick(&flow,&audio,&stream,0u));
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_LEFT));
    CHECK(storage[stream.count-1u].argument==141u);
    CHECK(tick(&flow,&audio,&stream,0u));
    {
        const size_t before=stream.count;
        CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_A));
        CHECK(stream.count==before+1u);
        CHECK(storage[stream.count-1u].argument==100u);
        CHECK(storage[stream.count-1u].player==PORTABLE_AUDIO_PLAYER_AUTO);
    }
    CHECK(tick(&flow,&audio,&stream,0u));
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_B));
    CHECK(storage[stream.count-1u].argument==144u);
    return 0;
}

static int test_character_voices(void)
{
    const PortableCharacterAudioRules rules=character_audio_fixture();
    for(uint32_t character=1u;character<=8u;++character){
        PortableFrontendFlow flow;
        PortableFrontendAudio audio;
        PortableAudioCommand storage[4];
        PortableAudioCommandStream stream;
        CHECK(portable_frontend_flow_init(&flow,&config));
        CHECK(portable_audio_command_stream_init(&stream,storage,4u)==
              PORTABLE_AUDIO_STREAM_OK);
        CHECK(init_audio(&audio,&flow,&stream));
        flow.events[flow.event_count++]=(PortableFrontendEvent){
            flow.tick,character,PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK,
            PORTABLE_FRONTEND_PAGE_CHARACTER};
        CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
        CHECK(stream.count==2u&&storage[1].argument==rules.lock_voices[character-1u]&&
              storage[1].player==PORTABLE_AUDIO_PLAYER_AUTO);
    }
    {
        PortableFrontendFlow flow;
        PortableFrontendAudio audio;
        PortableAudioCommand storage[4];
        PortableAudioCommandStream stream;
        CHECK(portable_frontend_flow_init(&flow,&config));
        CHECK(portable_audio_command_stream_init(&stream,storage,4u)==
              PORTABLE_AUDIO_STREAM_OK);
        CHECK(init_audio(&audio,&flow,&stream));
        flow.events[flow.event_count++]=(PortableFrontendEvent){flow.tick,0u,
            PORTABLE_FRONTEND_EVENT_RECEIVED_GHOST_ERASE,PORTABLE_FRONTEND_PAGE_RECORDS};
        flow.events[flow.event_count++]=(PortableFrontendEvent){flow.tick,8u,
            PORTABLE_FRONTEND_EVENT_HORN,PORTABLE_FRONTEND_PAGE_CHARACTER};
        flow.events[flow.event_count++]=(PortableFrontendEvent){flow.tick,4u,
            PORTABLE_FRONTEND_EVENT_HORN,PORTABLE_FRONTEND_PAGE_CHARACTER};
        flow.events[flow.event_count++]=(PortableFrontendEvent){flow.tick,20u,
            PORTABLE_FRONTEND_EVENT_TRACK_SET,PORTABLE_FRONTEND_PAGE_COURSE};
        CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
        CHECK(stream.count==4u&&storage[1].argument==143u&&
              storage[2].argument==208u&&storage[2].player==PORTABLE_AUDIO_PLAYER_AUTO&&
              storage[3].argument==147u);
    }
    return 0;
}

static int test_kart_cues(void)
{
    PortableFrontendFlow flow;
    PortableFrontendAudio audio;
    PortableAudioCommand storage[12];
    PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_audio_command_stream_init(&stream,storage,12u)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(init_audio(&audio,&flow,&stream));
    /* Donor4 uses authored row3, independently for menu and launch revs. */
    flow.selected_character=4u;
    flow.events[flow.event_count++]=(PortableFrontendEvent){100u,0u,
        PORTABLE_FRONTEND_EVENT_KART_TRICK,PORTABLE_FRONTEND_PAGE_CHARACTER};
    flow.events[flow.event_count++]=(PortableFrontendEvent){110u,
        PORTABLE_FRONTEND_PAGE_COURSE,PORTABLE_FRONTEND_EVENT_ACCEPT,
        PORTABLE_FRONTEND_PAGE_CHARACTER};
    flow.events[flow.event_count++]=(PortableFrontendEvent){120u,1u,
        PORTABLE_FRONTEND_EVENT_KART_TRICK,PORTABLE_FRONTEND_PAGE_CHARACTER};
    flow.tick=144u;
    CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
    /* 156@100, 143@110, 159@118, 123@120; rev waits for 145. */
    CHECK(stream.count==5u&&storage[1].argument==156u&&storage[1].tick==100u&&
          storage[2].argument==143u&&storage[3].argument==159u&&
          storage[3].tick==118u&&storage[4].argument==123u);
    flow.tick=145u;
    CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
    CHECK(stream.count==7u&&storage[5].argument==143u&&storage[5].tick==145u&&
          storage[6].operation==PORTABLE_AUDIO_COMMAND_SET_VOLUME&&
          storage[6].player==3u&&storage[6].argument==303u);
    flow.mode_cursor=1u;
    flow.events[flow.event_count++]=(PortableFrontendEvent){200u,
        PORTABLE_FRONTEND_PAGE_RACE,PORTABLE_FRONTEND_EVENT_ACCEPT,
        PORTABLE_FRONTEND_PAGE_COURSE};
    flow.tick=237u;
    CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
    CHECK(stream.count==11u&&storage[7].argument==143u&&
          storage[8].argument==163u&&storage[8].tick==219u&&
          storage[9].argument==403u&&storage[9].player==4u&&storage[10].argument==156u&&
          storage[10].tick==237u);
    return 0;
}

static int test_title_intro_skip(void)
{
    PortableFrontendFlow flow;
    PortableFrontendAudio audio;
    PortableAudioCommand storage[8];
    PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_audio_command_stream_init(&stream,storage,8u)==
          PORTABLE_AUDIO_STREAM_OK);
    flow.title_variant=2u;
    CHECK(init_audio(&audio,&flow,&stream)&&storage[0].argument==51u);
    CHECK(ticks(&flow,&audio,&stream,32u));
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(stream.count==2u&&storage[1].argument==52u&&
          storage[1].tick==flow.tick);
    CHECK(tick(&flow,&audio,&stream,0u));
    CHECK(settle(&flow,&audio,&stream));
    CHECK(stream.count==2u);
    return 0;
}

static int test_course_stages(void)
{
    PortableFrontendFlow flow;
    PortableFrontendAudio audio;
    PortableAudioCommand storage[8];
    PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_audio_command_stream_init(&stream,storage,8u)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(init_audio(&audio,&flow,&stream));
    flow.mode_cursor=1u;
    flow.page=PORTABLE_FRONTEND_PAGE_COURSE;
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.course_phase==1u&&storage[stream.count-1u].argument==142u);
    CHECK(tick(&flow,&audio,&stream,0u));
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.course_phase==2u&&storage[stream.count-1u].argument==142u);
    CHECK(tick(&flow,&audio,&stream,0u));
    CHECK(tick(&flow,&audio,&stream,PORTABLE_FRONTEND_BUTTON_A));
    CHECK(flow.pending_page==PORTABLE_FRONTEND_PAGE_RACE&&
          storage[stream.count-1u].argument==143u);
    return 0;
}

static int test_settled_title_boot(void)
{
    PortableFrontendFlow flow;
    PortableFrontendAudio audio;
    PortableAudioCommand storage[4];
    PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_frontend_flow_settle_title(&flow));
    CHECK(portable_audio_command_stream_init(&stream,storage,4u)==
          PORTABLE_AUDIO_STREAM_OK);
    CHECK(init_audio(&audio,&flow,&stream));
    CHECK(stream.count==1u&&storage[0].argument==52u&&
          audio.title_loop_started==1u);
    return 0;
}

static int test_consumed_menu_batches(void)
{
    PortableFrontendFlow flow;
    PortableFrontendAudio audio;
    PortableAudioCommand storage[4];
    PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_audio_command_stream_init(&stream,storage,4u)==PORTABLE_AUDIO_STREAM_OK);
    CHECK(init_audio(&audio,&flow,&stream));
    portable_frontend_flow_consume_events(&flow);audio.next_event=0u;stream.count=0u;
    flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;flow.character_cursor=0u;
    unsigned cues=0u;
    for(unsigned i=0u;i<2400u;++i){
        CHECK(tick(&flow,&audio,&stream,(i&1u)?0u:PORTABLE_FRONTEND_BUTTON_RIGHT));
        for(size_t j=0u;j<stream.count;++j){CHECK(storage[j].argument==141u);++cues;}
        portable_frontend_flow_consume_events(&flow);audio.next_event=0u;stream.count=0u;
    }
    CHECK(cues==1200u&&!flow.event_overflow&&stream.status==PORTABLE_AUDIO_STREAM_OK);
    return 0;
}

static int test_modded_rev_volume_and_missing_profile(void)
{
    PortableFrontendFlow flow;PortableFrontendAudio audio;
    PortableAudioCommand storage[8];PortableAudioCommandStream stream;
    CHECK(portable_frontend_flow_init(&flow,&config));
    CHECK(portable_audio_command_stream_init(&stream,storage,8u)==PORTABLE_AUDIO_STREAM_OK);
    CHECK(portable_frontend_audio_init(&audio,&flow,&stream));
    flow.selected_character=1u;
    flow.events[flow.event_count++]=(PortableFrontendEvent){flow.tick,1u,
        PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK,PORTABLE_FRONTEND_PAGE_CHARACTER};
    CHECK(portable_frontend_audio_tick(&audio,&flow,&stream)&&stream.count==1u);
    PortableCharacterAudioRules rules=character_audio_fixture();
    rules.menu_revs[0]=(PortableCharacterRevCue){301u,0u};
    uint8_t players[2][8];for(unsigned t=0;t<2u;++t)for(unsigned i=0;i<8u;++i)players[t][i]=9u;
    CHECK(portable_frontend_audio_set_character_cues(&audio,&rules,(const uint8_t (*)[8])players));
    flow.events[flow.event_count++]=(PortableFrontendEvent){flow.tick,PORTABLE_FRONTEND_PAGE_COURSE,
        PORTABLE_FRONTEND_EVENT_ACCEPT,PORTABLE_FRONTEND_PAGE_CHARACTER};
    CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
    flow.tick+=35u;CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
    CHECK(stream.count==4u&&storage[2].argument==301u&&
        storage[3].operation==PORTABLE_AUDIO_COMMAND_SET_VOLUME&&storage[3].argument==0u&&storage[3].player==9u);
    players[0][0]=11u;
    CHECK(!portable_frontend_audio_set_character_cues(&audio,&rules,(const uint8_t (*)[8])players));
    return 0;
}

int main(void)
{
    CHECK(test_modded_rev_volume_and_missing_profile()==0);
    CHECK(test_consumed_menu_batches()==0);
    CHECK(test_title_route()==0);
    CHECK(test_event_cues()==0);
    CHECK(test_character_voices()==0);
    CHECK(test_title_intro_skip()==0);
    CHECK(test_kart_cues()==0);
    CHECK(test_course_stages()==0);
    CHECK(test_settled_title_boot()==0);
    puts("portable frontend audio: PASS");
    return 0;
}
