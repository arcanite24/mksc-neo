#include "time_trial_ghost_library.h"
#include "time_trial_ghost_save.h"
#include "time_trial_records.h"
#include "frontend_flow.h"
#include "mod_identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_temp.h"

#define CHECK(x) do{if(!(x)){fprintf(stderr,"ghost library line %d: %s\n",__LINE__,#x);return 1;}}while(0)

static PortableRaceCompatibilityId identity(uint32_t course)
{
    return (PortableRaceCompatibilityId){.course_id=course,.course_revision=1,
        .rules_id=1,.rules_revision=1,.handling_profile_id=1,.handling_revision=1,
        .character_class=0,.state_schema_version=1};
}
static void candidate(uint8_t *record,uint32_t course,uint16_t time)
{
    portable_time_trial_ghost_record_init(record,0u,(uint8_t)course);
    portable_time_trial_ghost_record_set_time(record,time);
}
static int test_menu(void)
{
    PortableGhostSaveMenu menu={.status=PORTABLE_GHOST_SAVE_READY};
    CHECK(portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A)==PORTABLE_GHOST_SAVE_ACTION_NONE&&menu.prompt&&!menu.yes);
    CHECK(portable_time_trial_ghost_save_tick(&menu,0u)==PORTABLE_GHOST_SAVE_ACTION_NONE&&menu.prompt);
    CHECK(portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A)==PORTABLE_GHOST_SAVE_ACTION_NONE&&!menu.prompt);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_RIGHT);
    CHECK(portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_B)==PORTABLE_GHOST_SAVE_ACTION_NONE&&!menu.prompt);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A);
    CHECK(!menu.yes);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_RIGHT);
    CHECK(portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A)==PORTABLE_GHOST_SAVE_ACTION_WRITE);
    menu.status=PORTABLE_GHOST_SAVE_UNAVAILABLE;
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A);
    CHECK(!menu.prompt);
    CHECK(portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_B)==PORTABLE_GHOST_SAVE_ACTION_BACK);
    menu=(PortableGhostSaveMenu){.status=PORTABLE_GHOST_SAVE_FULL,.used_slots=10u};
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A);
    CHECK(menu.full_list&&!menu.prompt);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_UP);
    CHECK(menu.erase_cursor==9u);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_DOWN);
    CHECK(menu.erase_cursor==0u);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A);
    CHECK(menu.prompt&&!menu.yes);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_RIGHT);
    CHECK(portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_A)==PORTABLE_GHOST_SAVE_ACTION_ERASE);
    CHECK(menu.full_list&&!menu.prompt);
    portable_time_trial_ghost_save_tick(&menu,PORTABLE_FRONTEND_BUTTON_B);
    CHECK(!menu.full_list);
    return 0;
}

static int test_content_libraries(const char *directory)
{
    const PortableRaceCompatibilityId id=identity(2u);
    PortableModIdentity mods={.api_version=PORTABLE_MOD_API_VERSION,.count=1u};
    strcpy(mods.mods[0].id,"library-test");
    strcpy(mods.mods[0].version,"1.0.0");
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],loaded[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    char paths[4][512];
    PortableGhostLibraryInventory inventory;
    /* The same course has independent stock, two content revisions and mirror
     * libraries. Inventory capacity and erase apply only to the selected set. */
    for(unsigned set=0u;set<4u;++set){
        mods.mods[0].fingerprint[0]=(uint8_t)(set==3u?1u:set);
        portable_mod_identity_set_current(set?&mods:NULL);
        portable_mod_identity_set_mirror(set==3u);
        CHECK(portable_time_trial_ghost_library_query(directory,2u,&inventory)==
            PORTABLE_GHOST_LIBRARY_AVAILABLE && inventory.used==0u);
        PortableRaceCompatibilityId selected=id;
        if(set==3u)selected.rules_revision=2u; /* Mirrored race compatibility. */
        candidate(record,2u,(uint16_t)(100u+set));
        CHECK(portable_time_trial_ghost_library_save(directory,&selected,record,loaded)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
        snprintf(paths[set],sizeof paths[set],"%s/course-2-solo%s.ghost",directory,
            portable_mod_identity_record_suffix());
        CHECK(portable_time_trial_ghost_library_query(directory,2u,&inventory)==
            PORTABLE_GHOST_LIBRARY_REUSE && inventory.used==1u);
    }
    for(unsigned set=0u;set<4u;++set){
        mods.mods[0].fingerprint[0]=(uint8_t)(set==3u?1u:set);
        portable_mod_identity_set_current(set?&mods:NULL);
        portable_mod_identity_set_mirror(set==3u);
        PortableRaceCompatibilityId selected=id;
        if(set==3u)selected.rules_revision=2u;
        CHECK(portable_time_trial_ghost_store_load(paths[set],&selected,loaded)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
        CHECK(portable_time_trial_ghost_record_time(loaded)==100u+set);
        for(unsigned other=set+1u;other<4u;++other)
            CHECK(portable_time_trial_ghost_store_load(paths[other],&selected,loaded)==
                PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE);
        CHECK(portable_time_trial_records_erase_ghost(directory,2u));
        for(unsigned remaining=set+1u;remaining<4u;++remaining)CHECK(test_path_exists(paths[remaining]));
    }
    portable_mod_identity_set_current(NULL);
    portable_mod_identity_set_mirror(0u);
    return 0;
}

int main(void)
{
    CHECK(test_menu()==0);
    char directory[128],path[512];
    CHECK(test_temp_path(directory,sizeof directory,"mksc-ghost-library",1)==0);
    PortableGhostLibraryInventory inventory;
    CHECK(portable_time_trial_ghost_library_query(NULL,1,&inventory)==PORTABLE_GHOST_LIBRARY_IO_ERROR);
    CHECK(portable_time_trial_ghost_library_query(directory,0,&inventory)==PORTABLE_GHOST_LIBRARY_IO_ERROR);
    CHECK(portable_time_trial_ghost_library_query(directory,1,NULL)==PORTABLE_GHOST_LIBRARY_IO_ERROR);
    CHECK(portable_time_trial_ghost_library_query(directory,1,&inventory)==PORTABLE_GHOST_LIBRARY_AVAILABLE&&inventory.used==0u);
    const unsigned order[]={9,2,8,1,7,3,6,4,5,10};
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],loaded[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    for(unsigned i=0;i<10;++i){
        const PortableRaceCompatibilityId id=identity(order[i]);candidate(record,order[i],100u);
        CHECK(portable_time_trial_ghost_library_save(directory,&id,record,loaded)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    }
    CHECK(portable_time_trial_ghost_library_query(directory,11,&inventory)==PORTABLE_GHOST_LIBRARY_FULL&&inventory.used==10u);
    for(unsigned i=0;i<10;++i)CHECK(inventory.courses[i]==i+1u);
    /* Pre-limit directories keep every file; reuse still works above capacity. */
    snprintf(path,sizeof path,"%s/course-12-solo.ghost",directory);
    FILE *legacy=fopen(path,"wb");CHECK(legacy&&fputs("legacy",legacy)>=0&&fclose(legacy)==0);
    CHECK(portable_time_trial_ghost_library_query(directory,12,&inventory)==PORTABLE_GHOST_LIBRARY_REUSE&&inventory.used==11u);
    CHECK(portable_time_trial_ghost_library_query(directory,13,&inventory)==PORTABLE_GHOST_LIBRARY_FULL&&inventory.used==11u);
    for(unsigned i=0;i<10;++i)CHECK(inventory.courses[i]==i+1u);
    CHECK(test_path_exists(path)&&remove(path)==0);
    PortableRaceCompatibilityId id=identity(11);candidate(record,11,200u);
    CHECK(portable_time_trial_ghost_library_save(directory,&id,record,loaded)!=PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(snprintf(path,sizeof path,"%s/course-11-solo.ghost",directory)>0&&!test_path_exists(path));
    CHECK(portable_time_trial_ghost_library_query(directory,3,&inventory)==PORTABLE_GHOST_LIBRARY_REUSE);
    id=identity(3);candidate(record,3,200u);
    CHECK(portable_time_trial_ghost_library_save(directory,&id,record,loaded)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    CHECK(portable_time_trial_ghost_record_time(loaded)==200u); /* Explicit save may replace a faster run. */
    snprintf(path,sizeof path,"%s/course-3-solo.ghost",directory);
    CHECK(portable_time_trial_ghost_store_load(path,&id,loaded)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK&&portable_time_trial_ghost_record_time(loaded)==200u);
    CHECK(portable_time_trial_records_erase_ghost(directory,3));
    id=identity(11);candidate(record,11,300u);
    CHECK(portable_time_trial_ghost_library_save(directory,&id,record,loaded)==PORTABLE_TIME_TRIAL_GHOST_STORE_OK);
    for(unsigned i=1;i<=11;++i)CHECK(portable_time_trial_records_erase_ghost(directory,i));
    const char *ignored[]={"course-0-solo.ghost","course-01-solo.ghost","course-4294967296-solo.ghost","course-1-solo.ghost.tmp","gp-progress.bin"};
    for(unsigned i=0;i<sizeof ignored/sizeof ignored[0];++i){
        snprintf(path,sizeof path,"%s/%s",directory,ignored[i]);FILE *f=fopen(path,"wb");CHECK(f&&fclose(f)==0);
    }
    CHECK(portable_time_trial_ghost_library_query(directory,1,&inventory)==PORTABLE_GHOST_LIBRARY_AVAILABLE&&inventory.used==0u);
    snprintf(path,sizeof path,"%s/course-1-solo.ghost",directory);FILE *f=fopen(path,"wb");CHECK(f&&fputs("invalid",f)>=0&&fclose(f)==0);
    CHECK(portable_time_trial_ghost_library_query(directory,1,&inventory)==PORTABLE_GHOST_LIBRARY_REUSE&&inventory.used==1u);
    id=identity(1);candidate(record,1,300u);
    CHECK(portable_time_trial_ghost_library_save(directory,&id,record,loaded)==PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID);
    CHECK(portable_time_trial_records_erase_ghost(directory,1));
    for(unsigned i=0;i<sizeof ignored/sizeof ignored[0];++i){snprintf(path,sizeof path,"%s/%s",directory,ignored[i]);CHECK(remove(path)==0);}
    CHECK(test_content_libraries(directory)==0);
    CHECK(test_rmdir(directory)==0);
    puts("portable Time Trial ghost library: PASS");return 0;
}
