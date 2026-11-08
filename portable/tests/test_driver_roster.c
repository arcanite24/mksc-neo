#include "character_audio_fixture.h"
/* Roster drivers: registry rows, class lookup, menu pages, voices and saves. */
#include "content_registry.h"
#include "frontend_audio.h"
#include "frontend_config.h"
#include "personal_best.h"
#include "time_trial_ghost.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"driver roster failed line %d: %s\n",__LINE__,#condition); \
    return 1; } } while(0)

enum { TABLE = 112u, MAX_SECTIONS = 40u, PAYLOAD = TABLE + MAX_SECTIONS * 40u };

typedef struct
{
    uint8_t bytes[8192];
    size_t used;
    uint32_t count;
} Pack;

static void put32(uint8_t *p,uint32_t value)
{
    for(unsigned byte=0u;byte<4u;++byte)p[byte]=(uint8_t)(value>>(byte*8u));
}

static uint8_t *add(Pack *pack,uint32_t kind,uint32_t id,uint32_t size)
{
    uint8_t *entry=pack->bytes+TABLE+pack->count++*40u;
    uint8_t *payload=pack->bytes+pack->used;
    put32(entry,kind);put32(entry+4u,id);put32(entry+8u,1u);put32(entry+12u,3u);
    put32(entry+16u,(uint32_t)pack->used);put32(entry+24u,size);
    pack->used+=(size+7u)&~7u;
    return payload;
}

static void manifest(Pack *pack,uint32_t id,uint32_t driver_class)
{
    uint8_t *p=add(pack,PORTABLE_ASSET_SECTION_CHARACTER_MANIFEST,id,24u);
    put32(p,UINT32_C(0x31524843));put32(p+4u,id);put32(p+8u,1u);
    put32(p+12u,id);put32(p+16u,id);put32(p+20u,driver_class);
    add(pack,PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS,id,8u);
    add(pack,PORTABLE_ASSET_SECTION_MOTION_TABLES,id,8u);
}

/* Stock IDs 1..8 with the imported class order, one course, then roster rows. */
static PortableAssetPackView build(Pack *pack,const uint32_t (*rows)[3],uint32_t row_count,
                                   uint32_t roster_class)
{
    static const uint32_t classes[8]={0u,1u,3u,6u,7u,4u,5u,2u};
    memset(pack,0,sizeof *pack);
    pack->used=PAYLOAD;
    for(uint32_t id=1u;id<=8u;++id)manifest(pack,id,classes[id-1u]);
    add(pack,PORTABLE_ASSET_SECTION_COURSE_MANIFEST,24u,8u);
    manifest(pack,16u,roster_class);
    if(row_count!=0u){
        uint8_t *p=add(pack,PORTABLE_ASSET_SECTION_DRIVER_ROSTER,1u,8u+row_count*8u);
        put32(p,UINT32_C(0x31565244));put32(p+4u,row_count);
        for(uint32_t row=0u;row<row_count;++row){
            put32(p+8u+row*8u,rows[row][0]);p[12u+row*8u]=(uint8_t)rows[row][1];
            p[13u+row*8u]=(uint8_t)rows[row][2];
        }
    }
    return (PortableAssetPackView){.bytes=pack->bytes,.size=pack->used,
        .section_count=pack->count,.section_table_offset=TABLE};
}

static int press(PortableFrontendFlow *flow,uint16_t button)
{
    return portable_frontend_flow_tick(flow,button)&&portable_frontend_flow_tick(flow,0u);
}

static const PortableFrontendEvent *last_event(const PortableFrontendFlow *flow,uint8_t type)
{
    for(size_t index=flow->event_count;index>0u;--index)
        if(flow->events[index-1u].type==type)return &flow->events[index-1u];
    return NULL;
}

static int test_registry(void)
{
    static Pack pack;
    static const uint32_t one[1][3]={{16u,1u,0u}};
    PortableAssetPackView view=build(&pack,one,1u,5u);
    PortableDriverRoster roster;
    PortableCharacterContent driver;
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_OK);
    CHECK(roster.count==1u&&roster.entries[0].character_id==16u&&roster.entries[0].art_slot==1u);
    /* Wario's seat (class5) still answers with stock driver7. */
    CHECK(portable_content_registry_find_character_class(&view,5u,&driver)==PORTABLE_CONTENT_OK&&
          driver.id==7u);
    CHECK(portable_content_registry_saved_driver(&view,5u,0u,&driver)==PORTABLE_CONTENT_OK&&
          driver.id==7u);
    CHECK(portable_content_registry_saved_driver(&view,5u,16u,&driver)==PORTABLE_CONTENT_OK&&
          driver.id==16u&&driver.graphics==16u&&driver.motion==16u);
    CHECK(portable_content_registry_saved_driver(&view,3u,16u,&driver)==PORTABLE_CONTENT_INCOMPATIBLE);
    CHECK(portable_content_registry_saved_driver(&view,5u,7u,&driver)==PORTABLE_CONTENT_NOT_FOUND);
    /* Without a roster, a second class5 manifest is an ambiguity. */
    view=build(&pack,NULL,0u,5u);
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_OK&&roster.count==0u);
    CHECK(portable_content_registry_find_character_class(&view,5u,&driver)==PORTABLE_CONTENT_INCOMPATIBLE);
    static const uint32_t random_slot[1][3]={{16u,4u,0u}},missing[1][3]={{17u,0u,0u}},
        twice[2][3]={{16u,0u,0u},{16u,2u,0u}};
    view=build(&pack,random_slot,1u,5u);
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_BAD_MANIFEST);
    view=build(&pack,missing,1u,5u);
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_MISSING_REFERENCE);
    view=build(&pack,twice,2u,5u);
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_INCOMPATIBLE);
    /* Byte 5 picks the stock driver lending voices; zero keeps the seat's. */
    static const uint32_t voiced[1][3]={{16u,1u,3u}},bad_voice[1][3]={{16u,1u,9u}},
        roster_voice[2][3]={{16u,1u,8u},{8u,2u,0u}};
    view=build(&pack,one,1u,5u);
    CHECK(portable_content_registry_voice_driver(&view,16u,&driver)==PORTABLE_CONTENT_OK&&driver.id==7u);
    CHECK(portable_content_registry_voice_driver(&view,3u,&driver)==PORTABLE_CONTENT_OK&&driver.id==3u);
    view=build(&pack,voiced,1u,5u);
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_OK&&
          roster.entries[0].voice_driver==3u);
    CHECK(portable_content_registry_voice_driver(&view,16u,&driver)==PORTABLE_CONTENT_OK&&
          driver.id==3u&&driver.character_class==3u);
    CHECK(portable_content_registry_voice_driver(&view,7u,&driver)==PORTABLE_CONTENT_OK&&driver.id==7u);
    view=build(&pack,bad_voice,1u,5u);
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_BAD_MANIFEST);
    view=build(&pack,roster_voice,2u,5u);
    CHECK(portable_content_registry_driver_roster(&view,&roster)==PORTABLE_CONTENT_INCOMPATIBLE);
    return 0;
}

static int test_menu_pages(void)
{
    static Pack pack;
    static const uint32_t one[1][3]={{16u,1u,0u}},stock[1][3]={{7u,0u,0u}};
    PortableAssetPackView view=build(&pack,one,1u,5u);
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    PortableFrontendFlow flow;
    CHECK(build_frontend_config(&view,drivers,courses,&config));
    CHECK(config.driver_count==18u);
    CHECK(drivers[9].content_id==16u&&drivers[9].available&&drivers[9].voice_id==7u&&
          drivers[9].seat_slot==7u&&drivers[9].art_slot==1u);
    for(size_t index=10u;index<18u;++index)CHECK(drivers[index].content_id==0u);
    CHECK(portable_frontend_flow_init(&flow,&config));
    flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;
    CHECK(portable_frontend_flow_driver_pages(&flow)==2u);
    CHECK(portable_frontend_flow_voice_id(&flow,16u)==7u&&
          portable_frontend_flow_voice_id(&flow,3u)==3u);
    /* Random (4) -> 5, then past the right column onto page two's left column. */
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.character_cursor==5u&&flow.character_page==0u);
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.character_cursor==3u&&flow.character_page==1u);
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.character_cursor==5u&&flow.character_page==0u);
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.character_page==1u);
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_UP)&&flow.character_cursor==0u);
    /* Empty roster slots are unavailable; the roster driver honks its seat. */
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_RIGHT)&&flow.character_cursor==1u);
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_A)&&flow.character_locked==0u&&
          last_event(&flow,PORTABLE_FRONTEND_EVENT_UNAVAILABLE)!=NULL);
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_LEFT)&&flow.character_cursor==0u);
    CHECK(press(&flow,PORTABLE_FRONTEND_BUTTON_SELECT)&&
          last_event(&flow,PORTABLE_FRONTEND_EVENT_HORN)->value==7u);
    {
        PortableFrontendAudio audio;
        PortableAudioCommand storage[64];
        PortableAudioCommandStream stream;
        portable_frontend_flow_consume_events(&flow);
        CHECK(portable_audio_command_stream_init(&stream,storage,64u)==PORTABLE_AUDIO_STREAM_OK);
        flow.page=PORTABLE_FRONTEND_PAGE_TITLE_MENU; /* Audio attaches at the title. */
        CHECK(portable_frontend_audio_init(&audio,&flow,&stream));
        const PortableCharacterAudioRules cues=character_audio_fixture();
        uint8_t players[2][8];memset(players,1,sizeof players);
        CHECK(portable_frontend_audio_set_character_cues(&audio,&cues,(const uint8_t (*)[8])players));
        flow.page=PORTABLE_FRONTEND_PAGE_CHARACTER;
        CHECK(portable_frontend_flow_tick(&flow,PORTABLE_FRONTEND_BUTTON_A));
        CHECK(flow.character_locked&&flow.selected_character==16u&&
              last_event(&flow,PORTABLE_FRONTEND_EVENT_CHARACTER_LOCK)->value==16u);
        CHECK(portable_frontend_audio_tick(&audio,&flow,&stream));
        CHECK(stream.count!=0u&&stream.commands[stream.count-1u].argument==106u);
    }
    {
        static const uint32_t voiced[1][3]={{16u,1u,3u}};
        view=build(&pack,voiced,1u,5u);
        CHECK(build_frontend_config(&view,drivers,courses,&config));
        CHECK(drivers[9].voice_id==3u&&drivers[9].seat_slot==7u);
    }
    /* A roster may not list a stock grid driver. */
    view=build(&pack,stock,1u,5u);
    CHECK(!build_frontend_config(&view,drivers,courses,&config));
    /* One page keeps the original wrap. */
    view=build(&pack,NULL,0u,5u);
    CHECK(build_frontend_config(&view,drivers,courses,&config)&&config.driver_count==9u);
    return 0;
}

static int test_saves(const char *path)
{
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    portable_time_trial_ghost_record_init(record,5u,24u);
    CHECK(portable_time_trial_ghost_record_roster_driver(record)==0u);
    portable_time_trial_ghost_record_set_roster_driver(record,16u);
    CHECK(portable_time_trial_ghost_record_roster_driver(record)==16u&&
          portable_time_trial_ghost_record_driver(record)==5u);
    if(path==NULL)return 1;
    PortableRaceCompatibilityId identity={24u,1u,24u,1u,2u,1u,5u,1u,{0},16u};
    PortablePersonalBestTable table;
    int best=0;
    remove(path);
    CHECK(portable_personal_best_update_table(path,&identity,4000u,1200u,&table,&best)==
          PORTABLE_PERSONAL_BEST_OK&&best);
    identity.roster_driver_id=0u;identity.character_class=1u;
    CHECK(portable_personal_best_update_table(path,&identity,3900u,1300u,&table,&best)==
          PORTABLE_PERSONAL_BEST_OK);
    CHECK(portable_personal_best_load_table(path,&identity,&table)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(table.races[0].ticks==3900u&&table.races[0].roster_driver_id==0u&&
          table.races[1].ticks==4000u&&table.races[1].character_class==5u&&
          table.races[1].roster_driver_id==16u&&table.best_lap.roster_driver_id==16u);
    FILE *file=fopen(path,"r");
    char header[16]={0};
    CHECK(file!=NULL&&fgets(header,sizeof header,file)!=NULL);
    fclose(file);remove(path);
    CHECK(strncmp(header,"MKSC-PB 5 ",10u)==0);
    return 0;
}

int main(int argc,char **argv)
{
    if(argc!=2||test_registry()||test_menu_pages()||test_saves(argv[1]))return 1;
    puts("driver roster: PASS");
    return 0;
}
