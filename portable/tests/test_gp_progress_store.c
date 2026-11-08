#include "gp_progress_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if(!(condition)){ \
    fprintf(stderr,"GP progress store failed line %d: %s\n",__LINE__,#condition); \
    return 1; } } while(0)

static int test_title_variant(void)
{
    PortableGpProgress progress;
    portable_gp_progress_init(&progress);
    CHECK(portable_gp_progress_title_variant(&progress)==0u);
    memset(progress.grade[0],0,sizeof progress.grade[0]);
    CHECK(portable_gp_progress_title_variant(&progress)==1u);
    memset(progress.rating[0],0,sizeof progress.rating[0]);
    CHECK(portable_gp_progress_title_variant(&progress)==2u);
    progress.rating[0][4][2]=1u;
    CHECK(portable_gp_progress_title_variant(&progress)==1u);
    progress.grade[0][4][2]=1;
    CHECK(portable_gp_progress_title_variant(&progress)==0u);
    return 0;
}

static int test_extra_access(void)
{
    PortableGpProgress progress;
    portable_gp_progress_init(&progress);
    progress.unlock_flags=UINT16_C(0x7fff);
    for(uint8_t speed=0u;speed<3u;++speed){
        CHECK(portable_gp_progress_extra_cup_access(&progress,speed)==0u);
        for(unsigned cup=0u;cup<4u;++cup)progress.grade[0][cup][speed]=0;
        CHECK(portable_gp_progress_extra_cup_access(&progress,speed)==31u);
    }
    progress.unlock_flags=(uint16_t)((1u<<2u)|(1u<<14u)|(1u<<7u));
    CHECK(portable_gp_progress_extra_cup_access(&progress,0u)==0u);
    CHECK(portable_gp_progress_extra_cup_access(&progress,1u)==4u);
    CHECK(portable_gp_progress_extra_cup_access(&progress,2u)==17u);
    progress.grade[0][3][2]=1;
    CHECK(portable_gp_progress_extra_cup_access(&progress,2u)==0u);
    CHECK(portable_gp_progress_extra_cup_access(NULL,0u)==0u);
    CHECK(portable_gp_progress_extra_cup_access(&progress,3u)==0u);
    return 0;
}

int main(int argc,char **argv)
{
    CHECK(test_extra_access()==0&&test_title_variant()==0);
    PortableGpProgress progress,loaded,before;
    PortableGpProgressUpdate update;
    PortableGpCeremonySession ceremony={0};
    const char *path=argc==2?argv[1]:
        argc==3?argv[2]:"portable-gp-progress-test.bin";
    if(argc==3&&strcmp(argv[1],"--write")==0){
        portable_gp_progress_init(&progress);
        ceremony.cup_index=0u;ceremony.player_standing_rank=1u;
        ceremony.performance_rating=4u;
        CHECK(portable_gp_progress_apply_ceremony(
              &progress,&ceremony,0u,0u,&update)==PORTABLE_GP_PROGRESS_OK);
        CHECK(portable_gp_progress_save(path,&progress)==PORTABLE_GP_PROGRESS_OK);
        puts("portable GP progress cross-process write: PASS");return 0;
    }
    if(argc==3&&strcmp(argv[1],"--read")==0){
        CHECK(portable_gp_progress_load(path,&progress)==PORTABLE_GP_PROGRESS_OK&&
              progress.grade[0][0][0]==1&&progress.rating[0][0][0]==4u);
        remove(path);
        puts("portable GP progress cross-process reload: PASS");return 0;
    }
    CHECK(argc<=2);
    remove(path);
    portable_gp_progress_init(&progress);
    CHECK(progress.grade[0][0][0]==-1&&progress.rating[0][0][0]==0xffu);
    CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_NOT_FOUND);
    ceremony.cup_index=0u;ceremony.player_standing_rank=1u;
    ceremony.performance_rating=4u;
    before=progress;ceremony.performance_rating=8u;
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,0u,
          &update)==PORTABLE_GP_PROGRESS_INVALID_ARGUMENT&&
          memcmp(&progress,&before,sizeof progress)==0);
    ceremony.performance_rating=4u;
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,0u,
          &update)==PORTABLE_GP_PROGRESS_OK&&update.grade_changed&&
          update.rating_changed&&update.file_changed);
    CHECK(progress.grade[0][0][0]==1&&progress.rating[0][0][0]==4u);
    CHECK(portable_gp_progress_save(path,&progress)==PORTABLE_GP_PROGRESS_OK);
    CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK&&
          memcmp(&loaded,&progress,sizeof progress)==0);
    before=loaded;ceremony.player_standing_rank=4u;
    ceremony.performance_rating=7u;
    CHECK(portable_gp_progress_apply_ceremony(&loaded,&ceremony,0u,0u,
          &update)==PORTABLE_GP_PROGRESS_OK&&!update.file_changed&&
          memcmp(&loaded,&before,sizeof loaded)==0);
    ceremony.player_standing_rank=0u;ceremony.performance_rating=2u;
    CHECK(portable_gp_progress_apply_ceremony(&loaded,&ceremony,0u,0u,
          &update)==PORTABLE_GP_PROGRESS_OK&&update.file_changed&&
          loaded.grade[0][0][0]==0&&loaded.rating[0][0][0]==2u);
    CHECK(portable_gp_progress_save(path,&loaded)==PORTABLE_GP_PROGRESS_OK);
    memset(&progress,0,sizeof progress);
    CHECK(portable_gp_progress_load(path,&progress)==PORTABLE_GP_PROGRESS_OK&&
          progress.grade[0][0][0]==0&&progress.rating[0][0][0]==2u);
    {
        FILE *file=fopen(path,"r+b");
        unsigned char value;
        CHECK(file!=NULL&&fseek(file,20L,SEEK_SET)==0&&
              fread(&value,1u,1u,file)==1u&&fseek(file,20L,SEEK_SET)==0);
        value^=0x5au;
        CHECK(fwrite(&value,1u,1u,file)==1u&&fclose(file)==0);
        memset(&loaded,0xa5,sizeof loaded);before=loaded;
        CHECK(portable_gp_progress_load(path,&loaded)==
              PORTABLE_GP_PROGRESS_BAD_FORMAT&&
              memcmp(&loaded,&before,sizeof loaded)==0);
    }
    remove(path);
    /* Missing the podium must not award a medal, but its rating still saves. */
    portable_gp_progress_init(&progress);
    ceremony.player_standing_rank=3u;ceremony.performance_rating=5u;
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,0u,&update)==
        PORTABLE_GP_PROGRESS_OK&&!update.grade_changed&&update.rating_changed&&
        progress.grade[0][0][0]==-1&&progress.rating[0][0][0]==5u);
    /* Exercise the exact live load/update/save operation and its class selection. */
    ceremony.cup_index=4u;ceremony.engine_class=2u;ceremony.player_standing_rank=0u;
    ceremony.performance_rating=1u;ceremony.save_evaluation_pending=1u;
    CHECK(portable_gp_progress_record_ceremony(path,&ceremony)==PORTABLE_GP_PROGRESS_OK&&
        !ceremony.save_evaluation_pending);
    CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK&&
        loaded.grade[0][4][2]==0&&loaded.rating[0][4][2]==1u&&
        loaded.grade[0][4][0]==-1&&loaded.rating[0][4][0]==255u);
    ceremony.track_set=1u;ceremony.player_standing_rank=1u;
    CHECK(portable_gp_progress_record_ceremony(path,&ceremony)==PORTABLE_GP_PROGRESS_OK);
    CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK&&
        loaded.grade[1][4][2]==1&&loaded.rating[1][4][2]==1u&&
        loaded.grade[0][4][2]==0);
    /* Custom cups never touch the save file and never unlock anything. */
    {
        FILE *before_file=fopen(path,"rb");uint8_t old_bytes[256],new_bytes[256];
        CHECK(before_file!=NULL);const size_t old_size=fread(old_bytes,1u,sizeof old_bytes,before_file);fclose(before_file);
        ceremony.track_set=3u;ceremony.player_standing_rank=0u;ceremony.finishing_coin_total=396u;
        ceremony.save_evaluation_pending=1u;ceremony.unlock_evaluation_pending=1u;
        CHECK(portable_gp_progress_record_ceremony(path,&ceremony)==PORTABLE_GP_PROGRESS_OK&&
              !ceremony.save_evaluation_pending&&!ceremony.unlock_evaluation_pending&&
              !ceremony.extra_cup_unlocked&&!ceremony.special_cup_unlocked);
        FILE *after=fopen(path,"rb");CHECK(after!=NULL);
        const size_t new_size=fread(new_bytes,1u,sizeof new_bytes,after);fclose(after);
        CHECK(old_size==new_size&&memcmp(old_bytes,new_bytes,old_size)==0);
        ceremony.finishing_coin_total=0u;
    }
    ceremony.track_set=0u;
    ceremony.save_evaluation_pending=1u;
    CHECK(portable_gp_progress_record_ceremony("",&ceremony)==
        PORTABLE_GP_PROGRESS_INVALID_ARGUMENT&&ceremony.save_evaluation_pending);
    remove(path);
    portable_gp_progress_init(&progress);
    for(uint8_t speed=0u;speed<3u;++speed){
        CHECK(portable_gp_progress_cup_access(&progress,speed)==15u);
        ceremony.engine_class=speed;ceremony.player_standing_rank=0u;
        for(uint8_t cup=0u;cup<4u;++cup){
            ceremony.cup_index=cup;
            CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,speed,
                &update)==PORTABLE_GP_PROGRESS_OK);
            CHECK(portable_gp_progress_cup_access(&progress,speed)==(cup==3u?31u:15u));
        }
        CHECK(portable_gp_progress_save(path,&progress)==PORTABLE_GP_PROGRESS_OK&&
              portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK&&
              portable_gp_progress_cup_access(&loaded,speed)==31u);
        for(uint8_t other=speed+1u;other<3u;++other)
            CHECK(portable_gp_progress_cup_access(&loaded,other)==15u);
        progress.grade[0][2][speed]=1;
        CHECK(portable_gp_progress_cup_access(&progress,speed)==15u);
        progress.grade[0][2][speed]=0;
    }
    /* The fifth new gold does not satisfy the prior-five-golds prerequisite. */
    portable_gp_progress_init(&progress);
    for(unsigned cup=0u;cup<4u;++cup)progress.grade[0][cup][1]=0;
    ceremony.cup_index=4u;ceremony.finishing_coin_total=100u;
    ceremony.player_standing_rank=0u;
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,1u,&update)==
        PORTABLE_GP_PROGRESS_OK&&!update.unlock_changed&&progress.unlock_flags==0u);
    ceremony.finishing_coin_total=99u;
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,1u,&update)==
        PORTABLE_GP_PROGRESS_OK&&!update.unlock_changed);
    ceremony.finishing_coin_total=100u;
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,1u,&update)==
        PORTABLE_GP_PROGRESS_OK&&update.unlock_changed&&update.file_changed&&
        progress.unlock_flags==(1u<<13u));
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,1u,&update)==
        PORTABLE_GP_PROGRESS_OK&&!update.unlock_changed&&!update.file_changed);
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,2u,&update)==
        PORTABLE_GP_PROGRESS_OK&&!update.unlock_changed);
    CHECK(portable_gp_progress_save(path,&progress)==PORTABLE_GP_PROGRESS_OK&&
          portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK&&
          loaded.unlock_flags==(1u<<13u));
    ceremony.engine_class=1u;ceremony.cup_index=2u;
    ceremony.unlock_evaluation_pending=1u;
    CHECK(portable_gp_progress_record_ceremony(path,&ceremony)==PORTABLE_GP_PROGRESS_OK&&
          !ceremony.unlock_evaluation_pending&&ceremony.extra_cup_unlocked);
    CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK&&
          loaded.unlock_flags==((1u<<13u)|(1u<<7u)));
    ceremony.finishing_coin_total=397u;
    CHECK(portable_gp_progress_apply_ceremony(&progress,&ceremony,0u,1u,&update)==
        PORTABLE_GP_PROGRESS_INVALID_ARGUMENT);
    CHECK(portable_gp_progress_cup_access(NULL,0u)==0u&&
          portable_gp_progress_cup_access(&progress,3u)==0u);
    remove(path);
    for(uint8_t speed=0u;speed<3u;++speed){
        portable_gp_progress_init(&progress);
        for(unsigned cup=0u;cup<3u;++cup)progress.grade[0][cup][speed]=0;
        CHECK(portable_gp_progress_save(path,&progress)==PORTABLE_GP_PROGRESS_OK);
        ceremony=(PortableGpCeremonySession){.cup_index=3u,.engine_class=speed,
            .player_standing_rank=1u,.performance_rating=3u};
        CHECK(portable_gp_progress_record_ceremony(path,&ceremony)==PORTABLE_GP_PROGRESS_OK&&
            !ceremony.special_cup_unlocked&&!ceremony.extra_cup_unlocked);
        ceremony.player_standing_rank=0u;
        CHECK(portable_gp_progress_record_ceremony(path,&ceremony)==PORTABLE_GP_PROGRESS_OK&&
            ceremony.special_cup_unlocked&&!ceremony.extra_cup_unlocked);
        CHECK(portable_gp_progress_load(path,&loaded)==PORTABLE_GP_PROGRESS_OK&&
            portable_gp_progress_cup_access(&loaded,speed)==31u);
        CHECK(portable_gp_progress_record_ceremony(path,&ceremony)==PORTABLE_GP_PROGRESS_OK&&
            !ceremony.special_cup_unlocked);
        remove(path);
    }
    puts("portable GP progress store: PASS new/worse/better/reload");
    return 0;
}
