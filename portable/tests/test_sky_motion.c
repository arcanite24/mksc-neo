#include "test_support.h"
#include "sky_motion.h"

#define SKY_CHECK(x) do { if(!(x)){fprintf(stderr,"sky motion line %d: %s\n",__LINE__,#x);return 1;} } while(0)

static int sky_road_handoff(void)
{
    const PortableSkyMotionRules rules={.layer_mask=7u,.split_y=-1};
    PortableSkyMotion state;
    PortableSkyMotionInputs in={0};
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,NULL,0u)==1);
    /* Observed312E8: old threshold74/currentcamera75 installs74 and publishes
     * actoroffset2; next old75/current77 installs75 and publishes4. The
     * threshold itself precedes the first displayed road row by one. */
    in.vertical_offset=1;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0u)==1);
    const int8_t offsets[]={2,4};
    const int16_t first_rows[]={75,76};
    for(unsigned i=0u;i<2u;++i){
        in.vertical_offset=offsets[i];
        SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0u)==1);
        SKY_CHECK(state.snapshot.road_line==74+offsets[i] &&
            state.snapshot.draw_road_line==first_rows[i] && state.snapshot.draw_road_line_valid &&
            state.snapshot.scroll_y[0]==74-first_rows[i]);
    }
    state.prior_vertical_offset=0;state.prior_vertical_offset_q16=49152;
    in.vertical_offset=1;in.vertical_offset_q16=65536;in.precise_vertical_offset=1u;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0u)==1);
    SKY_CHECK(state.snapshot.road_line==75 && state.snapshot.draw_road_line==74 &&
        state.snapshot.draw_road_line_q16==74*65536+49152 &&
        state.snapshot.scroll_y_q16[0]==-49152 &&
        portable_sky_motion_road_first_row(&state.snapshot,1u)==74 &&
        portable_sky_motion_road_first_row(&state.snapshot,4u)==299);
    PortableSkyMotionSnapshot legacy={.road_line=75};
    SKY_CHECK(portable_sky_motion_road_first_row(&legacy,4u)==300);
    legacy.draw_road_line_valid=2u;
    SKY_CHECK(portable_sky_motion_road_first_row(&legacy,4u)==-1);
    legacy.draw_road_line_valid=1u;legacy.draw_road_line=161;
    SKY_CHECK(portable_sky_motion_road_first_row(&legacy,1u)==-1);
    legacy.draw_road_line=74;legacy.draw_road_line_q16=-1;
    SKY_CHECK(portable_sky_motion_road_first_row(&legacy,4u)==-1);
    legacy.draw_road_line_q16=160*65536+1;
    SKY_CHECK(portable_sky_motion_road_first_row(&legacy,4u)==-1);
    legacy.draw_road_line=160;legacy.draw_road_line_q16=160*65536;legacy.precise=1u;
    SKY_CHECK(portable_sky_motion_road_first_row(&legacy,4u)==640 &&
        portable_sky_motion_road_first_row(&legacy,0u)==-1);
    return 0;
}

static int sky_fractional_motion(void)
{
    PortableSkyMotionRules rules={.yaw_shift={2u,3u,4u},.layer_mask=7u,
        .y_bias={12,13,14},.split_y=-1,.layer_source={0u,1u,2u}};
    PortableSkyMotionInputs input={.yaw=1u,.precise_vertical_offset=1u,
        .vertical_offset_q16=16384};
    PortableSkyMotion state;
    int32_t sine[1024]={0};
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&input,NULL,0u)==1);
    SKY_CHECK(state.snapshot.precise && state.snapshot.scroll_x[0]==0 &&
        state.snapshot.scroll_x_q16[0]==16384 && state.snapshot.scroll_y_q16[0]==12*65536);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,NULL,0u)==1);
    SKY_CHECK(state.prior_vertical_offset==0 && state.prior_vertical_offset_q16==16384);
    input.yaw=2u;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,NULL,0u)==1);
    SKY_CHECK(state.snapshot.scroll_x[0]==0 && state.snapshot.scroll_x_q16[0]==32768 &&
        state.snapshot.scroll_y[0]==12 && state.snapshot.scroll_y_q16[0]==12*65536-16384);
    const PortableSkyMotion paused=state;
    input.paused=1u;input.yaw=40u;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,NULL,0u)==1 &&
        memcmp(&state,&paused,sizeof state)==0);
    input.paused=0u;input.source_gate=1u;input.vertical_offset_q16=32768;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,NULL,0u)==1 &&
        state.snapshot.scroll_x_q16[0]==32768 && state.snapshot.scroll_y_q16[0]==12*65536-16384);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,NULL,0u)==1 &&
        state.snapshot.scroll_y_q16[0]==12*65536-32768);
    input=(PortableSkyMotionInputs){.yaw=2u,.clock=1u};
    rules.controller=PORTABLE_SKY_MOTION_CLOCK_MID;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&input,NULL,0u)==1 &&
        state.snapshot.scroll_x[1]==0 && state.snapshot.scroll_x_q16[1]==20480);
    rules.controller=PORTABLE_SKY_MOTION_WATER_SWAY;sine[8]=1024;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&input,sine,1024u)==1 &&
        state.snapshot.scroll_x[1]==0 && state.snapshot.scroll_x_q16[1]==32768 &&
        state.snapshot.scroll_y[1]==13 && state.snapshot.scroll_y_q16[1]==13*65536+8192);
    rules.controller=PORTABLE_SKY_MOTION_SPEED_PAN;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&input,sine,1024u)==1);
    state.phase=1u;state.elapsed=1;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,sine,1024u)==1 &&
        state.snapshot.scroll_x[1]==0 && state.snapshot.scroll_x_q16[1]==40960 &&
        state.snapshot.scroll_y_q16[1]==13*65536+2048 && state.elapsed==2);
    rules.controller=PORTABLE_SKY_MOTION_SUNSET;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&input,sine,1024u)==1);
    state.phase=1u;state.elapsed=1;state.transition_counter=3;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,sine,1024u)==1 &&
        state.horizon_y==182 && state.horizon_y_q16==184*65536-81920);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&input,sine,1024u)==1 &&
        state.snapshot.scroll_y[2]==180 && state.snapshot.scroll_y_q16[2]==182*65536-81920);
    SKY_CHECK(portable_sky_motion_camera_offset_q16(8192,0u,0u)==16384 &&
        portable_sky_motion_camera_offset_q16(INT32_MAX,0u,0u)==24*65536 &&
        portable_sky_motion_camera_offset_q16(0,14u,0u)==65536 &&
        portable_sky_motion_wrap_q16(INT64_C(32768)*65536)==INT32_MIN);
    return 0;
}

int run_sky_motion(void)
{
    SKY_CHECK(sky_road_handoff()==0);
    SKY_CHECK(sky_fractional_motion()==0);
    /* Synthetic rule coefficients and sine samples, independent of ROM art. */
    PortableSkyMotionRules rules={.yaw_shift={3,4,5},.layer_mask=7,
        .x_bias={12,13,14},.y_bias={90,91,92},.split_y=-1,
        .layer_source={2,1,0},.lightning_hide_layer=2,.road_mode=2};
    PortableSkyMotionInputs in={.yaw=0xffffu,.vertical_offset=4};
    PortableSkyMotion state={0};
    int32_t sine[1024]={0};
    sine[0]=8193;sine[256]=-4097;sine[512]=-8193;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,NULL,0)==1);
    SKY_CHECK(state.snapshot.initialized && state.snapshot.scroll_x[0]==12 &&
        state.snapshot.scroll_y[0]==90 && state.prior_vertical_offset==0 &&
        state.snapshot.layer_source[0]==2u && state.snapshot.road_line==78 &&
        state.snapshot.road_mode==2u);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.scroll_y[0]==90 && state.prior_vertical_offset==4);
    in.yaw=0xffe7u;in.vertical_offset=-3;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.scroll_x[0]==9 && state.snapshot.scroll_x[1]==12 &&
        state.snapshot.scroll_x[2]==14 && state.snapshot.scroll_y[0]==86);
    PortableSkyMotion saved=state;
    in.paused=1;in.yaw=0;in.vertical_offset=20;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        memcmp(&state,&saved,sizeof state)==0);
    in.paused=0;in.source_gate=1;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.scroll_x[0]==9 && state.snapshot.scroll_y[0]==93 &&
        state.prior_vertical_offset==20);
    saved=state;
    SKY_CHECK(portable_sky_motion_tick(&state,NULL,&in,NULL,0)==0 &&
        memcmp(&state,&saved,sizeof state)==0);
    in.source_gate=0;in.vertical_offset=0;in.lightning_phase=2;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.layer_mask==3u);
    rules.lightning_hide_layer=0;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.layer_mask==6u);
    in.lightning_phase=0;
    rules.controller=PORTABLE_SKY_MOTION_CLOCK_SWAY;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.scroll_x[0]==10 && state.snapshot.scroll_x[1]==13 &&
        state.snapshot.scroll_y[1]==92);
    in.clock=64;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.scroll_x[0]==16 && state.snapshot.scroll_x[1]==29 &&
        state.snapshot.scroll_y[1]==90);
    saved=state;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==-1 &&
        memcmp(&state,&saved,sizeof state)==0);
    rules.controller=PORTABLE_SKY_MOTION_WATER_SWAY;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.scroll_x[0]==12 && state.snapshot.scroll_x[1]==29 &&
        state.snapshot.scroll_y[1]==90);
    rules.controller=PORTABLE_SKY_MOTION_CLOCK_MID;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.scroll_x[1]==17);
    rules.controller=PORTABLE_SKY_MOTION_SPEED_PAN;
    rules.split_y=24;rules.split_layer=2;rules.split_yaw_shift=5;
    in.selected_progress=130u*256u;in.yaw=64;in.clock=0;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,sine,1024)==1 &&
        state.phase==0u && state.snapshot.scroll_x[0]==12);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.phase==1u && state.elapsed==0 && state.snapshot.scroll_x[1]==21);
    state.elapsed=1024;in.clock=9;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.phase==2u && state.transition_clock==9u &&
        state.snapshot.scroll_x[1]==149 && state.snapshot.scroll_y[1]==123 &&
        state.snapshot.split_scroll_x==2 && state.snapshot.split_y==24);
    in.clock=17;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.scroll_x[1]==150);
    saved=state;in.paused=1;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        memcmp(&saved,&state,sizeof state)==0);
    in.paused=0;rules.split_y=-1;
    rules.controller=PORTABLE_SKY_MOTION_RISE;
    rules.y_bias[0]=352;in.scene_flags_7fe=0x100;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,NULL,0)==1);
    for(unsigned tick=0;tick<300u;++tick)
        SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 && state.phase==0u);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.phase==1u && state.elapsed==0 && state.snapshot.scroll_y[0]==352);
    state.elapsed=7200;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.scroll_y[0]==32 && state.elapsed==7201);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.scroll_y[0]==32 && state.elapsed==7201);
    rules.controller=PORTABLE_SKY_MOTION_LEADER_SHAKE;
    in.leader_progress=128u*256u;in.clock=0;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,NULL,0)==1);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 && !state.phase);
    in.leader_progress=129u*256u;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 && state.phase==1u &&
        state.snapshot.scroll_x[1]==16);
    in.leader_progress=0;in.clock=1;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,NULL,0)==1 &&
        state.snapshot.scroll_x[1]==18);
    rules.controller=PORTABLE_SKY_MOTION_SUNSET;
    in.yaw=0;in.clock=0;in.leader_route_wrap=0;in.leader_route_index=86;
    SKY_CHECK(portable_sky_motion_init(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.scroll_y[2]==182 && state.phase==0u);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 && state.phase==1u);
    for(unsigned tick=0;tick<132u;++tick)
        SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1);
    SKY_CHECK(state.phase==2u && state.horizon_y==144 && state.elapsed==0);
    in.leader_route_wrap=1;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.phase==3u && state.snapshot.scroll_y[2]==142);
    for(unsigned tick=0;tick<129u;++tick)
        SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1);
    SKY_CHECK(state.phase==4u && state.horizon_y==48 && state.elapsed==32);
    for(unsigned tick=0;tick<132u;++tick)
        SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1);
    SKY_CHECK(state.phase==5u && state.elapsed==0 &&
        state.snapshot.split_vertical==1u && state.snapshot.split_layer==2u &&
        state.snapshot.split_y==24 && state.snapshot.split_scroll_y==46);
    for(unsigned tick=0;tick<65u;++tick)
        SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1);
    SKY_CHECK(state.phase==6u && state.horizon_y==32);
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.scroll_y[2]==30);
    in.vertical_offset=7;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.split_y==24 && state.snapshot.split_scroll_y==46);
    in.vertical_offset=11;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.split_y==31 && state.snapshot.split_scroll_y==39 &&
        state.snapshot.scroll_y[2]==23);
    saved=state;in.source_gate=1;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.split_y==saved.snapshot.split_y &&
        state.snapshot.split_scroll_y==saved.snapshot.split_scroll_y);
    in.source_gate=0;
    SKY_CHECK(portable_sky_motion_camera_offset(8*65536,0,0)==16 &&
        portable_sky_motion_camera_offset(40*65536,0,0)==24 &&
        portable_sky_motion_camera_offset(0,0x10,0)==1 &&
        portable_sky_motion_camera_offset(0,0x10,1)==0 &&
        portable_sky_motion_camera_offset(-1,0,0)==0);
    rules.yaw_shift[0]=16;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==-1);
    rules.yaw_shift[0]=3;rules.road_mode=3;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==1 &&
        state.snapshot.road_mode==3u);
    rules.road_mode=4;
    SKY_CHECK(portable_sky_motion_tick(&state,&rules,&in,sine,1024)==-1);
    puts("portable sky motion: PASS");
    return 0;
}
