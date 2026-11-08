#include "test_support.h"
#include "finish_camera.h"
#define FINISH_CHECK(x) do { if(!(x)){fprintf(stderr,"finish camera line %d: %s\n",__LINE__,#x);return 1;} } while(0)

int run_finish_camera(void)
{
    /* Invented timing/basis coefficients exercise source branches, not ROM data. */
    PortableFinishCameraRules rules={.orbit_step=2048,.orbit_end=8192,
        .front_hold_ticks=3,.final_hold_ticks=4,.eligible_rank_max=1};
    int32_t sine[1024]={0};sine[256]=1000;
    PortableFinishCameraInput in={.driver_x_q16=60000,.driver_y_q16=90000,
        .normal_x_q16=10000,.normal_y_q16=20000,.normal_yaw=1234,
        .offset_x=3,.offset_y=5,.yaw_bias=17,.mode_flags_7da=0x100,
        .driver_mode_78=6};
    PortableFinishCamera camera={0};
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==0);
    portable_finish_camera_start(&camera);
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        camera.phase==8 && !camera.snapshot.active && camera.snapshot.yaw==1234);
    in.driver_mode_78=0;
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        camera.phase==9 && camera.snapshot.active && camera.orbit_yaw==0);
    for(unsigned i=1;i<=4;++i){
        FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1);
        FINISH_CHECK(camera.phase==9 && camera.snapshot.yaw==i*2048u+17u &&
            camera.snapshot.x_q16==61500 && camera.snapshot.y_q16==87500);
    }
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        camera.phase==12 && !camera.snapshot.request_celebration);
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        camera.phase==13 && camera.hold_ticks==3 && camera.snapshot.request_celebration);
    PortableFinishCamera saved=camera;
    in.paused=1;
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        memcmp(&saved,&camera,sizeof camera)==0);
    FINISH_CHECK(portable_finish_camera_tick(&camera,NULL,&in,sine,1024)==0 &&
        memcmp(&saved,&camera,sizeof camera)==0);
    in.paused=0;in.driver_angle_56=16384;
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        camera.hold_ticks==2 && !camera.snapshot.request_celebration &&
        camera.snapshot.x_q16==62500 && camera.snapshot.y_q16==91500 &&
        camera.snapshot.yaw==24576);
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 && camera.hold_ticks==1);
    saved=camera;in.driver_x_q16=120000;
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        camera.phase==14 && camera.hold_ticks==4 && camera.snapshot.x_q16==saved.snapshot.x_q16);
    for(unsigned i=0;i<3;++i)FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 && !camera.snapshot.cancel_effects);
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        camera.phase==18 && camera.snapshot.cancel_effects);
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 && !camera.snapshot.cancel_effects);

    /* TT200 and noneligible GP ranks retain the accepted finish camera. */
    for(unsigned tt=0;tt<2;++tt){
        in.mode_flags_7da=(uint16_t)(tt?0x200:0x100);in.rank=2;
        in.height_q16=0;portable_finish_camera_start(&camera);
        FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1);
        saved=camera;in.normal_x_q16+=500;in.driver_x_q16+=500;in.height_q16=65536;
        for(unsigned i=0;i<20;++i)FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1);
        FINISH_CHECK(camera.phase==(tt?10:9) && !camera.orbit_yaw &&
            camera.snapshot.x_q16==saved.snapshot.x_q16 && camera.snapshot.yaw==saved.snapshot.yaw &&
            !camera.snapshot.vertical_offset && !camera.snapshot.request_celebration);
    }
    in.mode_flags_7da=0x400;in.height_q16=65536;
    portable_finish_camera_start(&camera);
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 && camera.phase==15);
    in.height_q16=0;
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 && !camera.snapshot.vertical_offset);

    /* GP standings retain autonomous motion and advance BF84's yaw by128
     * per tick, including wrapped signed accumulation; redraws do not tick. */
    in.mode_flags_7da=0x100;in.driver_angle_56=0u;in.yaw_bias=17;
    camera.orbit_yaw=65500;camera.snapshot.active=1u;
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1);
    FINISH_CHECK(camera.phase==15u && camera.orbit_yaw==65628 && camera.snapshot.yaw==109u);
    FINISH_CHECK(camera.snapshot.x_q16==in.driver_x_q16+1500 &&
        camera.snapshot.y_q16==in.driver_y_q16-2500);
    saved=camera;in.paused=1u;
    FINISH_CHECK(portable_finish_camera_tick(&camera,&rules,&in,sine,1024)==1 &&
        memcmp(&camera,&saved,sizeof camera)==0);
    in.paused=0u;

    const PortableFinishRow rows[3]={{24,1,1},{9,3,2},{12,2,1}};
    const PortableFinishSchedule schedule={.count=3,.rows=rows};
    uint8_t mode=9,phase=50,frame=18,flags=3;
    FINISH_CHECK(portable_finish_animation_request(&mode,&phase,&flags,&schedule)==1 &&
        mode==2 && phase==2 && frame==18 && flags==1);
    FINISH_CHECK(portable_finish_animation_request(&mode,&phase,&flags,&schedule)==0);
    FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&schedule,62,256)==1 &&
        mode==2 && phase==2 && frame==18);
    FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&schedule,64,512)==1 && frame==18);
    FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&schedule,63,511)==1 &&
        mode==0x12 && phase==6 && frame==9);
    for(unsigned i=0;i<2;++i)FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&schedule,64,256)==1 && frame==9);
    FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&schedule,64,256)==1 &&
        mode==0x22 && phase==4 && frame==12);
    for(unsigned i=0;i<2;++i)FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&schedule,63,256)==1);
    FINISH_CHECK(mode==0x12 && frame==9);
    FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&schedule,0,256)==1 &&
        mode==2 && phase==2 && frame==9);
    const PortableFinishRow stop_rows[2]={{24,1,1},{0,0,255}};
    const PortableFinishSchedule stop={.count=2,.rows=stop_rows};
    FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&stop,64,256)==1 &&
        mode==0 && phase==0 && frame==0);
    FINISH_CHECK(portable_finish_animation_tick(&mode,&phase,&frame,&stop,64,256)==0);
    /* Read-only B4BC entry/return pairs after an ordinary fresh Peach GP:
     * Mario finished third; +0/+32/+80/+220 are saved frame boundaries.
     * Source camera3b0/3b4/3b8 were all zero, so no sine table is embedded. */
    const PortableFinishCameraRules captured_rules={1024,32768,80,220,3};
    const struct {
        uint8_t phase,next_phase;
        int32_t orbit,hold,next_hold,x,y,dx,dy;
        uint16_t yaw,driver_angle,next_yaw;
        int32_t next_x,next_y;
    } captures[]={
        {9,9,0,0,0,57981440,38652672,57998080,38442496,566,518,1024,57998080,38442496},
        {9,12,32768,0,0,58011136,33898496,58011136,33778688,32768,0,32768,58011136,33898496},
        {13,13,32768,34,33,58011136,27391232,58011136,27247616,32768,0,32768,58011136,27247616},
        {14,14,32768,114,113,57235968,22982912,46260736,18655232,28672,40960,28672,57235968,22982912},
    };
    for(size_t i=0;i<sizeof captures/sizeof captures[0];++i){
        camera=(PortableFinishCamera){.phase=captures[i].phase,.orbit_yaw=captures[i].orbit,
            .hold_ticks=captures[i].hold,.snapshot={.active=1,.x_q16=captures[i].x,
                .y_q16=captures[i].y,.yaw=captures[i].yaw}};
        in=(PortableFinishCameraInput){.driver_x_q16=captures[i].dx,.driver_y_q16=captures[i].dy,
            .driver_angle_56=captures[i].driver_angle,.mode_flags_7da=0x100,.rank=2};
        FINISH_CHECK(portable_finish_camera_tick(&camera,&captured_rules,&in,sine,1024)==1);
        FINISH_CHECK(camera.phase==captures[i].next_phase && camera.hold_ticks==captures[i].next_hold &&
            camera.snapshot.x_q16==captures[i].next_x && camera.snapshot.y_q16==captures[i].next_y &&
            camera.snapshot.yaw==captures[i].next_yaw && !camera.snapshot.request_celebration);
    }
    /* Owner ss2 is TT200, not GP: its actual finished camera remains phase10
     * while the driver continues at +0/+32/+80/+220. */
    const int32_t tt_driver_xy[][2]={{59183104,38458112},{59072256,33094656},
        {58095872,26178560},{44918784,20295168}};
    camera=(PortableFinishCamera){.phase=10,.snapshot={.active=1,.x_q16=59206656,
        .y_q16=38699520,.yaw=240}};
    for(size_t i=0;i<4u;++i){
        in=(PortableFinishCameraInput){.driver_x_q16=tt_driver_xy[i][0],
            .driver_y_q16=tt_driver_xy[i][1],.mode_flags_7da=0x200};
        FINISH_CHECK(portable_finish_camera_tick(&camera,&captured_rules,&in,sine,1024)==1 &&
            camera.phase==10 && camera.snapshot.x_q16==59206656 && camera.snapshot.y_q16==38699520 &&
            camera.snapshot.yaw==240 && !camera.snapshot.request_celebration);
    }
    rules.orbit_step=0;
    FINISH_CHECK(!portable_finish_camera_rules_valid(&rules));
    puts("portable finish camera: PASS");return 0;
}
