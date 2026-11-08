#include "personal_best.h"
#include "time_trial_result.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_temp.h"

#define CHECK(x) do{if(!(x)){fprintf(stderr,"personal best failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)

static PortableRaceCompatibilityId identity(void)
{
    PortableRaceCompatibilityId value={24u,3u,7u,2u,11u,4u,1u,5u,{0},0u};
    for(size_t i=0;i<sizeof value.deterministic_digest;++i)
        value.deterministic_digest[i]=(uint8_t)(i*7u+3u);
    return value;
}

static int test_ranked_records(void)
{
    char path[128];
    int descriptor=test_temp_path(path,sizeof path,"test_personal_best",0);CHECK(descriptor>=0);test_close(descriptor);CHECK(remove(path)==0);
    PortableRaceCompatibilityId id=identity();
    PortablePersonalBestTable table;
    int best=0;
    const uint32_t totals[]={900u,700u,800u,600u,1000u,750u,700u};
    for(unsigned i=0u;i<7u;++i){
        id.character_class=i;
        CHECK(portable_personal_best_update_table(path,&id,totals[i],300u-i,
            &table,&best)==PORTABLE_PERSONAL_BEST_OK);
        CHECK(best==(i==0u||i==1u||i==3u));
    }
    const uint32_t times[]={600u,700u,700u,750u,800u},drivers[]={3u,1u,6u,5u,2u};
    CHECK(portable_personal_best_load_table(path,&id,&table)==PORTABLE_PERSONAL_BEST_OK);
    for(unsigned i=0u;i<5u;++i)CHECK(table.races[i].ticks==times[i]&&
        table.races[i].character_class==drivers[i]);
    CHECK(table.best_lap.ticks==294u&&table.best_lap.character_class==6u);
    /* A race outside the top five can still improve the independent lap. */
    id.character_class=7u;
    CHECK(portable_personal_best_update_table(path,&id,2000u,200u,&table,&best)==
        PORTABLE_PERSONAL_BEST_OK&&!best&&table.races[4].ticks==800u&&
        table.best_lap.ticks==200u&&table.best_lap.character_class==7u);
    FILE *file=fopen(path,"a");CHECK(file!=NULL&&fputs("extra\n",file)>=0&&fclose(file)==0);
    CHECK(portable_personal_best_load_table(path,&id,&table)==PORTABLE_PERSONAL_BEST_INVALID&&
        table.races[0].ticks==0u);
    /* Read v3 without discarding the previous winner; upgrade on the next finish. */
    file=fopen(path,"w");CHECK(file!=NULL);
    CHECK(fprintf(file,"MKSC-PB 3 500 24 3 7 2 11 4 5 2 ")>0);
    for(unsigned i=0u;i<32u;++i)CHECK(fprintf(file,"%02x",id.deterministic_digest[i])>0);
    CHECK(fputc('\n',file)!=EOF&&fclose(file)==0);
    CHECK(portable_personal_best_load_table(path,&id,&table)==PORTABLE_PERSONAL_BEST_OK&&
        table.races[0].ticks==500u&&table.races[0].character_class==2u&&table.best_lap.ticks==0u);
    CHECK(portable_personal_best_update_table(path,&id,600u,150u,&table,&best)==
        PORTABLE_PERSONAL_BEST_OK&&!best&&table.races[0].ticks==500u&&table.races[1].ticks==600u);
    CHECK(remove(path)==0);
    PortableRecoveredTimeTrialSnapshot result={0};
    result.phase=PORTABLE_RACE_PHASE_FINISHED;result.compatibility=id;
    result.compatibility.course_id=64u;result.lap_count=5u;result.laps_completed=5u;
    const uint32_t laps[5]={200u,250u,300u,150u,50u};
    memcpy(result.lap_ticks,laps,sizeof laps);result.total_ticks=950u;
    uint32_t fastest=0u;
    CHECK(portable_time_trial_personal_best_update(path,&result,&fastest,&best)==
        PORTABLE_PERSONAL_BEST_OK&&best&&fastest==950u);
    CHECK(portable_personal_best_load_table(path,&result.compatibility,&table)==
        PORTABLE_PERSONAL_BEST_OK&&table.best_lap.ticks==50u);
    CHECK(remove(path)==0);return 0;
}

static int test_slow_finish_save(void)
{
    char path[128];
    int descriptor=test_temp_path(path,sizeof path,"test_slow_best",0);
    CHECK(descriptor>=0);test_close(descriptor);CHECK(remove(path)==0);
    PortableRecoveredTimeTrialSnapshot result={.phase=PORTABLE_RACE_PHASE_FINISHED,
        .lap_count=3u,.laps_completed=3u,.total_ticks=18000u,.lap_ticks={6000u,6000u,6000u}};
    result.compatibility=identity();
    uint32_t best=0u;int updated=0;
    CHECK(portable_time_trial_personal_best_update(path,&result,&best,&updated)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(updated&&best==18000u);
    PortablePersonalBestTable records;
    CHECK(portable_personal_best_load_table(path,&result.compatibility,&records)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(records.races[0].ticks==18000u&&records.best_lap.ticks==6000u);
    CHECK(remove(path)==0);return 0;
}

static int test_finish_record(void)
{
    PortableRecoveredTimeTrialSnapshot result={.phase=PORTABLE_RACE_PHASE_FINISHED,
        .lap_count=3u,.laps_completed=3u,.total_ticks=750u,.lap_ticks={200u,250u,300u}};
    PortablePersonalBestTable records={0};
    CHECK(portable_time_trial_finish_record(&result,&records));
    records.races[0].ticks=750u;records.best_lap.ticks=199u;
    CHECK(portable_time_trial_finish_record(&result,&records)); /* equal total */
    records.races[0].ticks=749u;
    CHECK(!portable_time_trial_finish_record(&result,&records));
    records.best_lap.ticks=200u;
    CHECK(portable_time_trial_finish_record(&result,&records)); /* equal lap */
    records.best_lap.ticks=201u;
    CHECK(portable_time_trial_finish_record(&result,&records)); /* lap only */
    records.best_lap.ticks=199u;records.races[0].ticks=751u;
    CHECK(portable_time_trial_finish_record(&result,&records)); /* total only */
    PortableTimeTrialRecordDefaults defaults={.rows={{0u,1255u},{1u,2000u},{2u,3000u},
        {3u,4000u},{4u,5000u},{7u,1u}}};
    const PortablePersonalBestTable empty={0};
    /* floor(750*4389*100/262144)=1255: keep the last tick within the
     * displayed centisecond, rather than floor-inverting a target limit. */
    CHECK(portable_time_trial_finish_record_with_defaults(&result,&empty,&defaults));
    defaults.rows[0].centiseconds=1254u;
    CHECK(!portable_time_trial_finish_record_with_defaults(&result,&empty,&defaults));
    defaults.rows[5].centiseconds=334u;
    CHECK(portable_time_trial_finish_record_with_defaults(&result,&empty,&defaults));
    defaults.rows[5].centiseconds=333u;
    CHECK(!portable_time_trial_finish_record_with_defaults(&result,&empty,&defaults));
    CHECK(portable_time_trial_finish_record_with_defaults(&result,&records,NULL)==
        portable_time_trial_finish_record(&result,&records));
    result.laps_completed=2u;
    CHECK(!portable_time_trial_finish_record(&result,&records));
    return 0;
}

static int test_bounded_record_numbers(void)
{
    char path[128];
    const int descriptor = test_temp_path(path, sizeof path, "test_pb_numbers", 0);
    CHECK(descriptor >= 0);
    test_close(descriptor);
    const PortableRaceCompatibilityId id = identity();
    static const struct { unsigned field; const char *token; } bad[] = {
        {0u, "4294967301"}, {0u, "18446744073709551616"},
        {1u, "4294967320"}, {1u, "-4294967272"},
        {2u, "4294967296"}, {2u, "18446744073709551616"},
        {2u, "99999999999999999999999999999999999999999999999999"},
        {2u, "-1"}, {2u, "100x"},
        {3u, "4294967296"}, {3u, "-0"},
        {4u, "4294967296"}, {4u, "-0"}};
    for (size_t test = 0u; test <= sizeof bad / sizeof bad[0]; ++test)
    {
        const int valid = test == sizeof bad / sizeof bad[0];
        const unsigned field = valid ? 99u : bad[test].field;
        const char *token = valid ? "" : bad[test].token;
        FILE *file = fopen(path, "w");
        CHECK(file != NULL);
        CHECK(fprintf(file, "MKSC-PB %s %s 3 7 2 11 4 5 ",
            field == 0u ? token : "5", field == 1u ? token : "24") > 0);
        for (unsigned i = 0u; i < 32u; ++i)
            CHECK(fprintf(file, "%02x", id.deterministic_digest[i]) > 0);
        CHECK(fprintf(file, "\n%s %s %s\n",
            field == 2u ? token : "4294967295", field == 3u ? token : "0",
            field == 4u ? token : "255") > 0);
        for (unsigned i = 1u; i <= PORTABLE_PERSONAL_BEST_COUNT; ++i)
            CHECK(fputs("0 0 0\n", file) >= 0);
        CHECK(fclose(file) == 0);
        PortablePersonalBestTable table;
        CHECK(portable_personal_best_load_table(path, &id, &table) ==
            (valid ? PORTABLE_PERSONAL_BEST_OK : PORTABLE_PERSONAL_BEST_INVALID));
        CHECK(valid ? table.races[0].ticks == UINT32_MAX && table.races[0].roster_driver_id == 255u
                    : table.races[0].ticks == 0u);
    }
    CHECK(remove(path) == 0);
    return 0;
}

int main(void)
{
    CHECK(test_bounded_record_numbers() == 0);
    CHECK(test_ranked_records()==0);
    CHECK(test_finish_record()==0);
    CHECK(test_slow_finish_save()==0);
    char path[128];
    int descriptor=test_temp_path(path,sizeof path,"test_personal_best",0);
    uint32_t ticks=0;
    int updated=0;
    PortableRaceCompatibilityId expected=identity(),changed;
    PortableRecoveredTimeTrialSnapshot result={0};
    PortablePersonalBestRecord record={0};
    CHECK(descriptor>=0);
    test_close(descriptor);
    CHECK(remove(path)==0);
    CHECK(portable_personal_best_load(path,&expected,&ticks)==PORTABLE_PERSONAL_BEST_NOT_FOUND);
    CHECK(portable_personal_best_update(path,&expected,900,&ticks,&updated)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(updated&&ticks==900);
    CHECK(portable_personal_best_update(path,&expected,1000,&ticks,&updated)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(!updated&&ticks==900);
    CHECK(portable_personal_best_update(path,&expected,800,&ticks,&updated)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(updated&&ticks==800);
    CHECK(portable_personal_best_load(path,&expected,&ticks)==PORTABLE_PERSONAL_BEST_OK&&ticks==800);
    CHECK(portable_personal_best_load_record(path,&expected,&record)==
              PORTABLE_PERSONAL_BEST_OK&&record.ticks==800u&&
              record.character_class==expected.character_class);

    changed=expected;changed.character_class=6u;
    CHECK(portable_personal_best_load_record(path,&changed,&record)==
              PORTABLE_PERSONAL_BEST_OK&&record.ticks==800u&&
              record.character_class==expected.character_class);
    CHECK(portable_personal_best_update_record(
              path,&changed,800u,&record,&updated)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(!updated&&record.ticks==800u&&
          record.character_class==expected.character_class);
    CHECK(portable_personal_best_update_record(
              path,&changed,799u,&record,&updated)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(updated&&record.ticks==799u&&record.character_class==6u);

    result.phase=PORTABLE_RACE_PHASE_FINISHED;
    result.compatibility=expected;
    result.lap_count=3u;
    result.laps_completed=3u;
    result.lap_ticks[0]=200u;
    result.lap_ticks[1]=250u;
    result.lap_ticks[2]=300u;
    result.total_ticks=750u;
    CHECK(portable_time_trial_result_is_valid(&result));
    CHECK(portable_time_trial_personal_best_update(
              path,&result,&ticks,&updated)==PORTABLE_PERSONAL_BEST_OK);
    CHECK(updated&&ticks==750u);

    result.compatibility.course_id=64u;result.lap_count=5u;result.laps_completed=5u;
    result.lap_ticks[3]=200u;result.lap_ticks[4]=250u;result.total_ticks=1200u;
    CHECK(portable_time_trial_result_is_valid(&result));
    --result.total_ticks;CHECK(!portable_time_trial_result_is_valid(&result));
    result.compatibility=expected;result.lap_count=3u;result.laps_completed=3u;
    result.lap_ticks[3]=result.lap_ticks[4]=0u;result.total_ticks=750u;

    result.lap_ticks[0]=6000u;
    result.lap_ticks[1]=6000u;
    result.lap_ticks[2]=5800u;
    result.total_ticks=17800u;
    CHECK(portable_time_trial_result_is_valid(&result));
    CHECK(portable_time_trial_personal_best_update(
              path,&result,&ticks,&updated)==PORTABLE_PERSONAL_BEST_OK&&!updated);
    CHECK(portable_personal_best_load(path,&expected,&ticks)==
              PORTABLE_PERSONAL_BEST_OK&&ticks==750u);
    ++result.lap_ticks[2];++result.total_ticks;
    CHECK(portable_time_trial_result_is_valid(&result));
    CHECK(portable_time_trial_personal_best_update(
              path,&result,&ticks,&updated)==PORTABLE_PERSONAL_BEST_OK&&!updated);
    result.lap_ticks[0]=200u;
    result.lap_ticks[1]=250u;
    result.lap_ticks[2]=300u;
    result.total_ticks=750u;

    result.phase=PORTABLE_RACE_PHASE_RACING;
    result.total_ticks=700u;
    CHECK(!portable_time_trial_result_is_valid(&result));
    CHECK(portable_time_trial_personal_best_update(
              path,&result,&ticks,&updated)==PORTABLE_PERSONAL_BEST_INVALID);
    CHECK(portable_personal_best_load(path,&expected,&ticks)==
              PORTABLE_PERSONAL_BEST_OK&&ticks==750u);
    result.phase=PORTABLE_RACE_PHASE_FINISHED;
    result.laps_completed=2u;
    CHECK(!portable_time_trial_result_is_valid(&result));
    result.laps_completed=3u;
    result.lap_ticks[1]=0u;
    CHECK(!portable_time_trial_result_is_valid(&result));
    result.lap_ticks[1]=250u;
    result.total_ticks=751u;
    CHECK(!portable_time_trial_result_is_valid(&result));

    changed=expected;changed.course_id++;
    CHECK(portable_personal_best_load(path,&changed,&ticks)==PORTABLE_PERSONAL_BEST_INCOMPATIBLE);
    changed=expected;changed.handling_revision++;
    CHECK(portable_personal_best_load(path,&changed,&ticks)==PORTABLE_PERSONAL_BEST_INCOMPATIBLE);
    changed=expected;changed.deterministic_digest[17]^=0xffu;
    CHECK(portable_personal_best_load(path,&changed,&ticks)==PORTABLE_PERSONAL_BEST_INCOMPATIBLE);

    FILE *file=fopen(path,"w");CHECK(file!=NULL);
    CHECK(fputs("MKSC-PB 1 700\n",file)>=0&&fclose(file)==0);
    CHECK(portable_personal_best_load(path,&expected,&ticks)==PORTABLE_PERSONAL_BEST_INCOMPATIBLE);
    file=fopen(path,"w");CHECK(file!=NULL);
    CHECK(fputs("MKSC-PB 2 700 24 3 7 2 11 4 1 5 "
                "0000000000000000000000000000000000000000000000000000000000000000\n",
                file)>=0&&fclose(file)==0);
    CHECK(portable_personal_best_load(path,&expected,&ticks)==
              PORTABLE_PERSONAL_BEST_INCOMPATIBLE);
    file=fopen(path,"w");CHECK(file!=NULL);
    CHECK(fputs("broken\n",file)>=0&&fclose(file)==0);
    CHECK(portable_personal_best_load(path,&expected,&ticks)==PORTABLE_PERSONAL_BEST_INVALID);
    CHECK(remove(path)==0);
    puts("portable personal best: PASS");
    return 0;
}
