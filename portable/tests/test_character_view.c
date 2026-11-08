#include "test_support.h"
#include "character_view.h"

#define VIEW_CHECK(x) do { if(!(x)){fprintf(stderr,"character view line %d: %s\n",__LINE__,#x);return 1;} } while(0)

int run_character_view(void)
{
    PortableCharacterViewRules rules={0};
    /* Invented limits demonstrate the imported rule path without ROM tables. */
    for(unsigned i=0;i<15u;++i){rules.response_limit[i]=8;rules.speed_limit[i]=12;}
    PortableCharacterViewInput in={.selected=1,.speed_q16=INT32_C(12)<<21,
        .steering_response_q8=-1024};
    PortableCharacterViewPose pose=portable_character_view_pose(&in,&rules);
    VIEW_CHECK(pose.angle==4 && !pose.mirror && !pose.frame_override);
    VIEW_CHECK(portable_character_visual_yaw(&in,&rules)==UINT16_C(0xf800));
    /* The model facing keeps sub-slot source precision; the billboard keeps
     * its original byte-angle quantization and signed half division. */
    in.driver_angle=in.camera_yaw=UINT16_C(0x0123);
    VIEW_CHECK(portable_character_visual_yaw(&in,&rules)==UINT16_C(0xf923));
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==4);
    in.selected=0;
    VIEW_CHECK(portable_character_visual_yaw(&in,&rules)==UINT16_C(0x0123));
    in.selected=1;in.source_flags=0x20u;
    VIEW_CHECK(portable_character_visual_yaw(&in,&rules)==UINT16_C(0x0123));
    in.source_flags=0;in.driver_angle=in.camera_yaw=0;
    VIEW_CHECK(portable_character_visual_yaw(&in,NULL)==0u);
    VIEW_CHECK(portable_character_visual_yaw(NULL,&rules)==0u);
    in.camera_adjustment=UINT16_C(0xfe00);
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==5);
    in.camera_adjustment=UINT16_C(0x0200);
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==5);
    in.position_82=UINT16_C(0xf000);
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==1);
    in.position_82=0;in.camera_adjustment=0;
    in.steering_response_q8=1024;
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==123);
    VIEW_CHECK(portable_character_visual_yaw(&in,&rules)==UINT16_C(0x0a00));
    in.speed_q16=-in.speed_q16;
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==4);
    in.camera_yaw=UINT16_C(0x1000);
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==8);
    VIEW_CHECK(portable_character_visual_yaw(&in,&rules)==0u);

    /* Unselected and finished karts bypass all local steering arithmetic. */
    in.selected=0;in.driver_angle=UINT16_C(0x0400);
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==6);
    in.camera_yaw=0;
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==126);
    in.selected=1;in.source_flags=UINT32_C(0x20);
    VIEW_CHECK(portable_character_view_pose(&in,&rules).angle==126);
    in.source_flags=0;
    VIEW_CHECK(portable_character_view_pose(&in,NULL).angle==0);

    /* Every source direction survives; reflection is independent of slot folding. */
    in=(PortableCharacterViewInput){0};
    for(unsigned angle=0;angle<128u;++angle){
        in.camera_yaw=(uint16_t)(angle<<9u);
        pose=portable_character_view_pose(&in,&rules);
        VIEW_CHECK(pose.angle==angle && !pose.frame_override);
        if((angle&63u)!=0u && (angle&63u)!=63u)
            VIEW_CHECK(pose.mirror==(angle>>6u));
        else VIEW_CHECK(pose.mirror==1);
    }
    in.steering_response_q8=-1;
    for(unsigned i=0;i<4u;++i){
        static const uint8_t boundaries[4]={0,63,64,127};
        in.camera_yaw=(uint16_t)((unsigned)boundaries[i]<<9u);
        VIEW_CHECK(!portable_character_view_pose(&in,&rules).mirror);
    }

    /* D908 activates only art modes2/8/9 with nonzero packed phase bits. */
    in=(PortableCharacterViewInput){.selected=1,.speed_q16=INT32_C(12)<<21,
        .steering_response_q8=-1024,.contact_frame_6c=26,.contact_flags_6d=1};
    for(unsigned mode=0;mode<16u;++mode){
        in.contact_mode_6e=(uint8_t)(mode|0x10u);in.contact_phase_6f=0;
        const unsigned active=mode==2u || mode==8u || mode==9u;
        VIEW_CHECK(portable_character_contact_art_active(&in)==active);
        pose=portable_character_view_pose(&in,&rules);
        if(active)VIEW_CHECK(pose.angle==0 && pose.frame_override==26 && pose.mirror==1);
        else VIEW_CHECK(pose.angle==4 && !pose.frame_override);
    }
    in.contact_mode_6e=8;in.contact_phase_6f=2;
    VIEW_CHECK(!portable_character_contact_art_active(&in));
    in.contact_phase_6f=1;
    VIEW_CHECK(portable_character_contact_art_active(&in));
    in.contact_flags_6d=0;
    VIEW_CHECK(!portable_character_view_pose(&in,&rules).mirror);
    in.camera_yaw=UINT16_C(0x0200);
    pose=portable_character_view_pose(&in,&rules);
    VIEW_CHECK(pose.angle==1 && !pose.frame_override && !pose.mirror);
    in.camera_yaw=0;in.contact_frame_6c=0;
    VIEW_CHECK(!portable_character_view_pose(&in,&rules).frame_override);
    VIEW_CHECK(portable_character_view_pose(NULL,&rules).angle==0);
    /* Original CD50 calls from the owner's ss2: grid, then120 ordinary
     * A+Left/A+Right ticks. Inputs are read-only entry bytes; expected mirror
     * is the resulting coordinate flag. No extracted graphics/tables here. */
    const struct {
        PortableCharacterViewInput input;
        uint8_t angle,mirror;
    } captured[]={
        {{.selected=1,.source_flags=UINT32_C(0x8000),
          .contact_mode_6e=10,.contact_phase_6f=78},127,1},
        {{.selected=1,.source_flags=UINT32_C(0x8400),
          .camera_yaw=40297,.driver_angle=40297,.steering_response_q8=-2415,
          .speed_q16=19103744},7,0},
        {{.selected=1,.source_flags=UINT32_C(0x8400),
          .camera_yaw=26482,.driver_angle=28914,.steering_response_q8=1150,
          .speed_q16=11698176,.position_82=63440},119,1},
    };
    /* Minimal authored rule fixture for the three captured operating points;
     * the complete source tables remain imported content. */
    PortableCharacterViewRules capture_rules={0};
    capture_rules.speed_limit[0]=2;
    capture_rules.response_limit[9]=14;capture_rules.speed_limit[9]=14;
    capture_rules.response_limit[4]=6;capture_rules.speed_limit[5]=6;
    for(size_t i=0;i<sizeof captured/sizeof captured[0];++i){
        pose=portable_character_view_pose(&captured[i].input,&capture_rules);
        VIEW_CHECK(pose.angle==captured[i].angle && pose.mirror==captured[i].mirror &&
            !pose.frame_override);
    }
    puts("portable character view: PASS");
    return 0;
}
