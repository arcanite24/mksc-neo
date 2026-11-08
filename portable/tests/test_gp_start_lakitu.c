#include "gp_start_lakitu.h"

#include <stdio.h>

#define CHECK(test) do { if(!(test)){ \
    fprintf(stderr,"lamp check failed at %d: %s\n",__LINE__,#test); \
    return 1; } } while(0)

int main(void)
{
    /* EF050's source ROM Y offsets, not captured actor positions. */
    static const int32_t offsets[16]={
        2,1,0,-1,-2,-1,0,1,2,1,0,-1,-2,-1,0,1};
    uint8_t data[PORTABLE_GP_START_LAKITU_BOB_BYTES]={0};
    int32_t sine[1024]={0};
    static const int32_t samples[20]={0,2410,5006,7571,10087,12539,
        14732,17018,19195,21250,23170,24812,26438,27897,29178,
        30273,31114,31834,32351,32663};
    for(unsigned i=0u;i<16u;++i){
        const uint32_t bits=(uint32_t)offsets[i];
        for(unsigned b=0u;b<4u;++b)data[i*8u+4u+b]=(uint8_t)(bits>>(b*8u));
    }
    for(unsigned p=0u;p<20u;++p)sine[p*256u/20u]=samples[p];
    int16_t y=0;
    CHECK(portable_gp_start_lakitu_y(1471u,data,sine,&y)==1&&y==45);
    CHECK(portable_gp_start_lakitu_y(1701u,data,sine,&y)==1&&y==43);
    CHECK(portable_gp_start_lakitu_y(1702u,data,sine,&y)==1&&y==42);
    CHECK(portable_gp_start_lakitu_y(1710u,data,sine,&y)==1&&y==42);
    CHECK(portable_gp_start_lakitu_y(1711u,data,sine,&y)==1&&y==41);
    CHECK(portable_gp_start_lakitu_y(1725u,data,sine,&y)==1&&y==-1);
    CHECK(portable_gp_start_lakitu_y(1726u,data,sine,&y)==1&&y==-6);
    /* Negative cell Y wraps in OAM; negative actor Y culls first. */
    CHECK(portable_gp_start_lakitu_y(1727u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(1732u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(1789u,data,sine,&y)==0);
    /* Repeated rendering cannot mutate a clock or motion state. */
    CHECK(portable_gp_start_lakitu_y(1711u,data,sine,&y)==1&&y==41);
    CHECK(portable_gp_start_lakitu_y(1470u,data,sine,&y)==-1);
    CHECK(portable_gp_start_lakitu_y(1790u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(1909u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2029u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2269u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2270u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2389u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2390u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2509u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2510u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2629u,data,sine,&y)==0);
    CHECK(portable_gp_start_lakitu_y(2630u,data,sine,&y)==-1);
    CHECK(portable_gp_start_lakitu_y(1711u,NULL,sine,&y)==-1);
    {
        PortableGpRecoveryLakituState recovery={0};
        uint16_t requests=UINT16_C(0x400),effect=13u;
        int32_t height=0;
        sine[8]=1608;
        sine[256]=32768;
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,140,
            &height,&effect,data,sine)==1);
        CHECK(requests==0u && recovery.phase==1u && recovery.counter==31 &&
              recovery.x==120 && recovery.y==-13 && recovery.art_bank==19u &&
              recovery.flags==0u && recovery.visible==1u);
        for(unsigned turn=1u;turn<32u;++turn)
            CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,140,
                &height,&effect,data,sine)==1);
        CHECK(recovery.callback==PORTABLE_GP_RECOVERY_LAKITU_BOB &&
              recovery.counter==0 && recovery.y==48);
        for(unsigned turn=0u;turn<8u;++turn)
            CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,140,
                &height,&effect,data,sine)==1);
        CHECK(recovery.counter==8 && recovery.y==49);
        requests=UINT16_C(0x10);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,140,
            &height,&effect,data,sine)==1);
        CHECK(requests==0u && recovery.phase==2u && recovery.y==51);
        while(recovery.callback==PORTABLE_GP_RECOVERY_LAKITU_RETRIEVE)
            CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,140,
                &height,&effect,data,sine)==1);
        CHECK(recovery.y==88 && recovery.retrieval_effect_started &&
              recovery.script==PORTABLE_GP_RECOVERY_SCRIPT_LIFT_WAIT &&
              recovery.wait==0u && effect==14u && height==0);
        /* The new F1(10) runs next turn; ten turns hold the height. */
        for(unsigned turn=0u;turn<10u;++turn){
            CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,140,
                &height,&effect,data,sine)==1);
            CHECK(height==0 && !recovery.retrieval_effect_started &&
                  recovery.wait==9u-turn);
        }
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,140,
            &height,&effect,data,sine)==1);
        CHECK(height==INT32_C(0x4000) && recovery.y==88 &&
              recovery.art_bank==20u && recovery.flags==0u);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,139,
            &height,&effect,data,sine)==1);
        CHECK(height==INT32_C(0x8000) && recovery.y==87);
        height=INT32_C(0x6c000);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,138,
            &height,&effect,data,sine)==1);
        CHECK(height==INT32_C(0x70000) && recovery.y==86 &&
              recovery.callback==PORTABLE_GP_RECOVERY_LAKITU_IDLE);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
            100,&height,&effect,data,sine)==1);
        CHECK(height==INT32_C(0x70000) && recovery.y==86);
        requests=UINT16_C(0x400);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,0u,138,
            &height,&effect,data,sine)==-1 && requests==UINT16_C(0x400));
        requests=UINT16_C(0x1040);effect=UINT16_C(0x12e);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
            126,&height,&effect,data,sine)==1);
        CHECK(recovery.phase==3u && recovery.y==74 && requests==0u &&
            recovery.art_bank==20u && effect==UINT16_C(0x100) &&
            recovery.script==PORTABLE_GP_RECOVERY_SCRIPT_DROP_HOLD);
        /* Projection is from the preceding turn; current height gates coins. */
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
            -32768,&height,&effect,data,sine)==1 && recovery.y==-64);
        height=0;
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
            140,&height,&effect,data,sine)==1);
        CHECK(recovery.y==88 && recovery.phase==4u && recovery.coin_loss_requested==2u &&
            recovery.exit_velocity_q16==0 && recovery.art_bank==20u);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
            141,&height,&effect,data,sine)==1);
        CHECK(recovery.y==88 && recovery.wait==59u && recovery.art_bank==19u &&
            recovery.exit_velocity_q16==INT32_C(0x8000) && !recovery.coin_loss_requested);
        for(unsigned turn=0;turn<59u;++turn)
            CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
                141,&height,&effect,data,sine)==1);
        CHECK(recovery.wait==0u && recovery.y==-2 &&
            recovery.exit_velocity_q16==INT32_C(0xa0000));
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
            141,&height,&effect,data,sine)==1);
        CHECK(recovery.phase==4u && (recovery.visible&1u) &&
            recovery.script==PORTABLE_GP_RECOVERY_SCRIPT_HIDE);
        CHECK(portable_gp_recovery_lakitu_tick(&recovery,&requests,UINT32_C(0x8800),
            141,&height,&effect,data,sine)==1);
        CHECK(recovery.phase==0u && !(recovery.visible&1u) && recovery.x==-128 &&
            recovery.y==-128 && recovery.callback==PORTABLE_GP_RECOVERY_LAKITU_IDLE);
    }
    {
        /* Direct F924 lava retrieval has no water descent/bob prerequisite.
         * B64 increments even when lava already placed the kart at48 units. */
        PortableGpRecoveryLakituState lava={0};
        uint16_t requests=0x10u,effect=3u;
        int32_t height=INT32_C(0x300000);
        CHECK(portable_gp_recovery_lakitu_consume(&lava,&requests,0u)==1);
        CHECK(lava.phase==2u && lava.x==120 && lava.y==-16 && requests==0u);
        for(unsigned i=0;i<80u && height==INT32_C(0x300000);++i)
            CHECK(portable_gp_recovery_lakitu_tick(&lava,&requests,0u,92,
                &height,&effect,NULL,NULL)==1);
        CHECK(height==INT32_C(0x304000) && effect==3u &&
              lava.callback==PORTABLE_GP_RECOVERY_LAKITU_IDLE);
        requests=0x40u;
        CHECK(portable_gp_recovery_lakitu_consume(&lava,&requests,0u)==1 &&
              lava.phase==3u && requests==0u);
        requests=0x10u;
        CHECK(portable_gp_recovery_lakitu_consume(&lava,&requests,0u)==-1 &&
              lava.phase==3u && requests==0x10u);
        lava.phase=4u;
        CHECK(portable_gp_recovery_lakitu_consume(&lava,&requests,0u)==1 &&
              requests==0x10u);
        lava=(PortableGpRecoveryLakituState){0};
        requests=0x50u;
        CHECK(portable_gp_recovery_lakitu_consume(&lava,&requests,0u)==1 &&
              lava.phase==2u && requests==0x40u);
        CHECK(portable_gp_recovery_lakitu_consume(&lava,&requests,0u)==1 &&
              lava.phase==3u && requests==0u);
        PortableGpRecoveryLakituState fall={0};
        requests=0x40u;
        CHECK(portable_gp_recovery_lakitu_consume(&fall,&requests,0u)==1 &&
              fall.phase==3u && requests==0u);
    }
    {
        PortableGpRecoveryLakituState bounds={.phase=1u,.x=99,.y=44};
        uint16_t requests=0x20u,effect=0u;
        int32_t height=0;
        CHECK(portable_gp_recovery_lakitu_consume(&bounds,&requests,0u)==1);
        CHECK(bounds.phase==2u && bounds.x==120 && bounds.y==-16 && requests==0u);
        unsigned turns=0u;
        while(height<INT32_C(0x70000) && turns++<100u)
            CHECK(portable_gp_recovery_lakitu_tick(&bounds,&requests,0u,140,
                &height,&effect,NULL,NULL)==1);
        CHECK(height==INT32_C(0x70000) && turns>80u && turns<100u && effect==0u &&
              bounds.callback==PORTABLE_GP_RECOVERY_LAKITU_IDLE);
    }
    {
        PortableGpRecoveryLakituState cancel={.phase=1u,.y=40,.visible=1u,.art_bank=19u};
        uint16_t requests=0x200u,effect=13u;int32_t height=0;
        CHECK(portable_gp_recovery_lakitu_consume(&cancel,&requests,0u)==1 && requests==0u &&
              cancel.phase==5u && cancel.callback==PORTABLE_GP_RECOVERY_LAKITU_CANCEL_EXIT);
        unsigned turns=0u;
        while(cancel.phase!=0u && turns++<30u)
            CHECK(portable_gp_recovery_lakitu_tick(&cancel,&requests,0u,140,&height,&effect,NULL,NULL)==1);
        CHECK(cancel.phase==0u && !cancel.visible && height==0 && turns>10u && turns<30u);
    }
    /* 51014 handles these pending requests before testing driver48 bit20. */
    for(unsigned i=0u;i<4u;++i)
    {
        const uint16_t pending[4]={0x400u,0x10u,0x20u,0x40u};
        const uint8_t phases[4]={1u,2u,2u,3u};
        PortableGpRecoveryLakituState finished={0};
        uint16_t request=pending[i];
        CHECK(portable_gp_recovery_lakitu_consume(&finished,&request,UINT32_C(0xa034))==1);
        CHECK(request==0u && finished.phase==phases[i] && finished.visible);
    }
    {
        PortableGpRecoveryLakituState warning={0};
        uint16_t requests=0x1000u,effect=0u;int32_t height=0;
        CHECK(portable_gp_recovery_lakitu_consume(&warning,&requests,0x400u)==1);
        CHECK(warning.phase==6u&&requests==0x1000u);
        for(unsigned turn=0u;turn<120u;++turn){
            CHECK(portable_gp_recovery_lakitu_tick(&warning,&requests,0x400u,140,
                &height,&effect,data,sine)==1);
            CHECK(portable_gp_lakitu_source_state(&warning)==0xc000u&&height==0);
            if(turn==19u)CHECK(warning.x==120&&warning.y==48&&warning.art_bank==16u);
            if(turn==32u)CHECK(warning.art_bank==17u);
            if(turn==48u)CHECK(warning.art_bank==18u);
            if(turn==64u)CHECK(warning.art_bank==15u);
            if(turn==96u)CHECK(warning.art_bank==17u);
        }
        CHECK(portable_gp_recovery_lakitu_tick(&warning,&requests,0u,140,
            &height,&effect,data,sine)==1);
        CHECK(warning.phase==7u&&requests==0u&&warning.cleared_requests==0x1000u);
        for(unsigned turn=0u;turn<22u;++turn)
            CHECK(portable_gp_recovery_lakitu_tick(&warning,&requests,0u,140,
                &height,&effect,data,sine)==1);
        CHECK(!warning.phase&&!warning.visible&&height==0);
        requests=0x1000u;
        CHECK(portable_gp_recovery_lakitu_consume(&warning,&requests,32u)==1&&warning.phase==0u);
        CHECK(portable_gp_lakitu_image(15u)==0x27020u&&portable_gp_lakitu_image(18u)==0x27023u);
        CHECK(portable_gp_lakitu_image(19u)==0x27010u&&portable_gp_lakitu_image(20u)==0x27011u);
        CHECK(portable_gp_lakitu_image(14u)==0u&&portable_gp_lakitu_scale(NULL)==0u);
        warning=(PortableGpRecoveryLakituState){.phase=PORTABLE_GP_LAKITU_WARNING,.y=48,.visible=1u};
        requests=0x400u;
        CHECK(portable_gp_recovery_lakitu_consume(&warning,&requests,0x400u)==1);
        CHECK(warning.phase==PORTABLE_GP_LAKITU_RESET&&requests==0x400u);
        for(unsigned turn=0u;turn<30u&&warning.phase;++turn)
            CHECK(portable_gp_recovery_lakitu_tick(&warning,&requests,0x400u,140,
                &height,&effect,data,sine)==1);
        CHECK(!warning.phase&&requests==0x400u);
        CHECK(portable_gp_recovery_lakitu_consume(&warning,&requests,0x400u)==1);
        CHECK(warning.phase==1u&&requests==0u);

    }
    {
        /* Invented art schedule: callback boundaries remain independently testable. */
        PortableGpFinishLakituRules rules={.row_count=3u,.entrance_ticks=22u,
            .bob_ticks=16u,.exit_ticks=4u,.frames={{21u,7u},{25u,19u},{28u,16u}}};
        CHECK(portable_gp_finish_lakitu_rules_valid(&rules));
        PortableGpRecoveryLakituState finish={.phase=3u,.visible=1u,.retrieval_effect_started=1u};
        uint16_t markers=UINT16_C(0x4570),requests=0u,effect=14u;int32_t height=17;
        uint8_t bob[128]={0};int32_t curve[1024]={0};
        curve[19u*256u/20u]=16384;curve[256]=32768;
        bob[8u]=3u;bob[12u]=2u; /* First bob row moves right3/down2. */
        CHECK(portable_gp_finish_lakitu_consume(&finish,&markers,&rules)==1 && markers==0u &&
            finish.phase==PORTABLE_GP_LAKITU_FINISH && !finish.retrieval_effect_started);
        CHECK(portable_gp_lakitu_source_state(&finish)==0xd000u);
        CHECK(portable_gp_finish_lakitu_tick(&finish,&rules,bob,curve)==1 &&
            finish.finish_turn==1u && finish.x==90 && finish.y==4 && finish.inverse_q8==128u &&
            finish.art_bank==21u && portable_gp_lakitu_scale(&finish)==512u);
        /* The legacy recovery path must not run a second callback or change physics. */
        CHECK(portable_gp_recovery_lakitu_tick(&finish,&requests,32u,140,&height,&effect,bob,curve)==1 &&
            finish.finish_turn==1u && height==17 && effect==14u);
        for(unsigned i=1u;i<22u;++i)CHECK(portable_gp_finish_lakitu_tick(&finish,&rules,bob,curve)==1);
        CHECK(finish.x==120 && finish.y==64 && finish.inverse_q8==256u && finish.art_bank==25u);
        for(unsigned i=0u;i<8u;++i)CHECK(portable_gp_finish_lakitu_tick(&finish,&rules,bob,curve)==1);
        CHECK(finish.x==123 && finish.y==66 && finish.counter==8 && finish.art_bank==28u);
        for(unsigned i=0u;i<8u;++i)CHECK(portable_gp_finish_lakitu_tick(&finish,&rules,bob,curve)==1);
        const int16_t old_x=finish.x,old_y=finish.y;
        CHECK(portable_gp_finish_lakitu_tick(&finish,&rules,bob,curve)==1 &&
            finish.x==old_x+old_y/8 && finish.y==old_y-(old_x-128)/16 && finish.inverse_q8==248u);
        requests=0x400u; /* A later request survives D000; only constructor-time requests clear. */
        CHECK(portable_gp_recovery_lakitu_consume(&finish,&requests,32u)==1 && requests==0x400u &&
            finish.phase==PORTABLE_GP_LAKITU_FINISH);
        while(finish.finish_turn<42u)CHECK(portable_gp_finish_lakitu_tick(&finish,&rules,bob,curve)==1);
        CHECK(finish.phase==PORTABLE_GP_LAKITU_FINISH && finish.visible);
        CHECK(portable_gp_finish_lakitu_tick(&finish,&rules,bob,curve)==1 &&
            !finish.phase && !finish.visible && finish.x==-128 && finish.y==-128);
        CHECK(requests==0x400u && portable_gp_recovery_lakitu_consume(&finish,&requests,32u)==1 &&
            requests==0u && finish.phase==1u);
        finish=(PortableGpRecoveryLakituState){0};
        markers=0x4000u;CHECK(portable_gp_finish_lakitu_consume(&finish,&markers,NULL)==1 &&
            !markers && !finish.phase);
        rules.frames[0].duration=0u;CHECK(!portable_gp_finish_lakitu_rules_valid(&rules));
        CHECK(portable_gp_lakitu_image(21u)==0x27030u && portable_gp_lakitu_image(28u)==0x27037u);
    }
    puts("GP start lamp: PASS ROM bob/ascent, preceding OAM, pre-wrap culling");
    return 0;
}
