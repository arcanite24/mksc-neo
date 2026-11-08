#ifndef PORTABLE_TEST_GP_ROSTER_FIXTURE_H
#define PORTABLE_TEST_GP_ROSTER_FIXTURE_H
#include "gp_race_roster.h"
#include "item_rules_fixture.h"
#include "../core/driver_engine_sound.h"
/* Authored profiles exercise content ownership; no donor tables are embedded. */
static inline const PortableDriverRosterRules *test_roster_rules(void)
{
    static PortableDriverRosterRules rules;
    for(unsigned c=0;c<8u;++c){
        for(unsigned i=0;i<8u;++i)rules.rival_classes[c][i]=(uint8_t)((c+7u-i)&7u);
        rules.contact_flags[c]=(uint16_t)(0x50u+c);
        rules.recovery_acceleration[c]=-(int32_t)(c+1u)*3;
        rules.pacing_last_place[c]=(uint8_t)(12u+c*3u);
    }
    for(unsigned e=0;e<3u;++e)for(unsigned row=0;row<3u;++row)for(unsigned i=0;i<4u;++i)
        rules.pacing_progress[e][row][i]=(uint8_t)(18u-i*4u);
    for(unsigned e=0;e<3u;++e)for(unsigned i=0;i<4u;++i)rules.pacing_route[e][i]=(uint8_t)(7u+i);
    for(unsigned i=0;i<6u;++i){rules.contact_script[i][0]=(uint8_t)(20u+i);rules.contact_script[i][1]=(uint8_t)(i==0u?1u:2u+i);}
    rules.contact_script[6][0]=rules.contact_script[6][1]=0u;
    rules.contact_script[7][0]=1u;rules.contact_script[7][1]=5u;
    return &rules;
}
static inline const PortableGpCpuTuning *test_cpu_tuning(void)
{
    static PortableGpCpuTuning tuning;
    for(unsigned c=0;c<8u;++c){
        for(unsigned i=0;i<16u;++i)tuning.acceleration[c][i]=(uint8_t)(24u+c*2u+i);
        for(unsigned row=0;row<5u;++row)for(unsigned i=0;i<4u;++i)
            tuning.target_speeds[c][row][i]=(uint8_t)(24u+row*3u+i);
    }
    for(unsigned row=0;row<5u;++row)for(unsigned i=0;i<8u;++i)
        tuning.heading_steps[row][i]=(uint8_t)(2u+row+i);
    return &tuning;
}
static inline void test_roster_rules_attach(PortableRaceRulesView *rules)
{
    static const int16_t deceleration[8]={-3,-6,-9,-12,-15,-18,-21,-24};
    rules->roster_rules=test_roster_rules();
    if(rules->cpu_tuning==NULL)rules->cpu_tuning=test_cpu_tuning();
    rules->source_auto_deceleration_table=deceleration;
    rules->source_auto_deceleration_count=8u;
}
static inline void test_item_roster_profiles(PortableGpRaceRoster *roster)
{
    roster->item_status_rules=test_item_status_rules();roster->boo_rules=test_boo_rules();
    for(unsigned i=0u;i<8u;++i){
        roster->racers[i].mushroom_rules=test_mushroom_rules();
        roster->racers[i].green_shell_rules=test_green_shell_rules();
        roster->racers[i].red_shell_rules=test_red_shell_rules();
        roster->racers[i].banana_rules=test_banana_rules();
        roster->racers[i].shell_orbit_rules=test_shell_orbit_rules();
    }
}
static inline PortableGpRosterStatus test_roster_init_for_rules(PortableGpRaceRoster *roster,
    const PortableRaceRulesView *rules,uint8_t driver)
{
    if(rules==NULL)return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableRaceRulesView owned=*rules;test_roster_rules_attach(&owned);
    const PortableGpRosterStatus status=portable_gp_race_roster_init_for_rules(roster,&owned,driver);
    if(status==PORTABLE_GP_ROSTER_OK)test_item_roster_profiles(roster);
    return status;
}
static inline PortableGpRosterStatus test_roster_init(PortableGpRaceRoster *roster,
    uint32_t course,uint8_t driver)
{
    if(course!=24u)return PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE;
    PortableRaceRulesView rules={.compatibility={.course_id=course},
        .start_x_q16=888*65536,.start_y_q16=618*65536,.start_route_index=44};
    test_roster_rules_attach(&rules);
    const PortableGpRosterStatus status=portable_gp_race_roster_init_for_rules(roster,&rules,driver);
    if(status==PORTABLE_GP_ROSTER_OK)test_item_roster_profiles(roster);
    return status;
}
static inline const PortableHazardSoundRules *test_hazard_rules(void)
{
    static PortableHazardSoundRules rules;
    for(unsigned driver=0u;driver<8u;++driver){
        for(unsigned listener=0u;listener<2u;++listener)for(unsigned size=0u;size<2u;++size){
            rules.voices[driver][listener][size]=(uint16_t)(180u+driver*4u+listener*2u+size);
            rules.impact_voices[driver][listener][size]=(uint16_t)(220u+driver*4u+listener*2u+size);
        }
        for(unsigned size=0u;size<2u;++size){
            rules.taunt[driver][size]=(uint16_t)(270u+driver*2u+size);
            rules.laugh[driver][size]=(uint16_t)(300u+driver*2u+size);
        }
    }
    return &rules;
}
#endif
