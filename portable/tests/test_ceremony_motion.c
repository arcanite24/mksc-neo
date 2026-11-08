#include "ceremony_motion.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"ceremony motion line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static int controllers(void)
{
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;sine[64]=16384;
    PortableCeremonyMotion s;portable_ceremony_motion_reset(&s);
    portable_ceremony_zoom_start(&s,0);CHECK(s.zoom.inverse_x==0&&s.zoom.x_q16==112*65536);
    for(unsigned i=1u;i<=64u;++i){
        CHECK(portable_ceremony_zoom_tick(&s,sine,1024u)==0);
        CHECK(s.zoom.inverse_x==1536-(int)i*20);
        if(i==16u)CHECK(s.zoom.angle==-32768);
    }
    CHECK(s.zoom.inverse_y==256&&s.zoom.angle==0);
    CHECK(portable_ceremony_zoom_tick(&s,sine,1024u)==1&&s.zoom.tick==0);
    CHECK(portable_ceremony_zoom_tick(&s,NULL,1024u)==-1);
    portable_ceremony_flypast_start(&s);CHECK(s.flypast.y_q16==210*65536);
    unsigned sounds=0;
    for(unsigned i=1u;i<=56u;++i){
        const uint16_t song=portable_ceremony_flypast_animation_tick(&s);
        CHECK((song==120u)==(i==21u||i==49u));sounds+=song!=0u;
    }
    CHECK(sounds==2u&&s.fly_frame==0u);
    for(unsigned i=1u;i<=200u;++i)CHECK(portable_ceremony_flypast_tick(&s,sine,1024u)==0);
    CHECK(s.flypast.x_q16==-50*65536&&s.flypast.y_q16==70*65536);
    CHECK(s.flypast.inverse_x==176&&s.flypast.angle==-4096);
    CHECK(portable_ceremony_flypast_tick(&s,sine,1024u)==1);
    CHECK(!portable_ceremony_trophy_start(&s,3u,0));
    CHECK(portable_ceremony_trophy_start(&s,1u,0));
    CHECK(s.trophy.origin_x==88&&s.trophy.origin_y==40&&s.trophy.inverse_x==0);
    for(unsigned i=1u;i<=32u;++i){CHECK(portable_ceremony_trophy_appear_tick(&s)==0);
        CHECK(s.trophy.inverse_x==25600/((99*(int)i)/32+1));}
    CHECK(s.trophy.inverse_x==256&&portable_ceremony_trophy_appear_tick(&s)==1);
    for(unsigned i=1u;i<=128u;++i)CHECK(portable_ceremony_trophy_move_tick(&s,sine,1024u)==0);
    CHECK(s.trophy.x_q16==24*65536&&s.trophy.y_q16==24*65536&&s.trophy.inverse_y==256);
    CHECK(portable_ceremony_trophy_move_tick(&s,sine,1024u)==1);
    CHECK(portable_ceremony_trophy_start(&s,0u,0));
    for(unsigned i=1u;i<=144u;++i)portable_ceremony_trophy_animation_tick(&s);
    CHECK(s.trophy_frame==0u&&s.trophy_counter==0u&&s.sparkle_palette==0u&&s.confetti_rng==0u);
    CHECK(portable_ceremony_trophy_start(&s,0u,1));
    portable_ceremony_trophy_animation_tick(&s);CHECK(s.confetti_rng!=0u&&s.sparkle_angle==1024u);
    portable_ceremony_loser_start(&s);
    CHECK(s.loser.y_q16==-38*65536&&portable_ceremony_loser_shadow_tick(&s)==12800u);
    for(unsigned i=1u;i<=64u;++i){uint16_t song=999u;
        CHECK(portable_ceremony_loser_tick(&s,(int16_t)i,&song)==0);
        CHECK((song==175u)==(i==24u||i==40u||i==48u));}
    uint16_t song=999u;CHECK(portable_ceremony_loser_tick(&s,122,&song)==1&&song==0u&&s.loser.tick==0);
    s.loser.y_q16=122*65536;CHECK(portable_ceremony_loser_shadow_tick(&s)==0u);
    return 0;
}
static int confetti(void)
{
    PortableCeremonyMotion a,b;portable_ceremony_motion_reset(&a);portable_ceremony_motion_reset(&b);
    CHECK(portable_ceremony_trophy_start(&a,0u,1));CHECK(portable_ceremony_trophy_start(&b,0u,1));
    portable_ceremony_confetti_tick(&a);portable_ceremony_confetti_tick(&b);
    CHECK(!memcmp(&a,&b,sizeof a));
    uint32_t seed=0u;for(unsigned i=0u;i<96u;++i)seed=seed*UINT32_C(0x41c64e6d)+0x3039u;
    CHECK(a.confetti_rng==seed);
    CHECK(a.confetti[0].target_x_q16==174*65536); /* First random15 is0. */
    CHECK(a.confetti[0].remaining>=63u&&a.confetti[0].remaining<=95u);
    CHECK(a.confetti[0].x_q16==24*65536+a.confetti[0].vx_q16);
    a.confetti[0].remaining=0u;a.confetti[0].active=1u;
    const uint32_t before=a.confetti_rng;
    portable_ceremony_confetti_tick(&a);
    CHECK(!a.confetti[0].active&&a.confetti[0].remaining==255u);
    CHECK(a.confetti[0].x_q16==a.confetti[0].target_x_q16&&a.confetti_rng==before);
    portable_ceremony_confetti_tick(&a);CHECK(a.confetti[0].active&&a.confetti_rng!=before);
    return 0;
}
static int tracks_and_snapshot(void)
{
    PortableCeremonyTrack tracks[8];
    const PortableCeremonySequenceFrame frames[2]={{4u,0u},{7u,2u}};
    for(unsigned i=0u;i<8u;++i)tracks[i]=(PortableCeremonyTrack){.x=(int16_t)(10u+i),.y=144,
        .depth_bias=-5,.count=2u,.initial_remaining=0u};
    PortableCeremonyMotion s;portable_ceremony_motion_reset(&s);
    CHECK(portable_ceremony_tracks_start(&s,tracks,frames,2u,1));
    CHECK(s.actor_scroll_remaining==256&&portable_ceremony_tracks_tick(&s,tracks,frames,2u));
    CHECK(s.actors[0].y_q16==272*65536-32768&&s.actors[0].frame==1u&&s.actors[0].remaining==2u);
    CHECK(portable_ceremony_tracks_tick(&s,tracks,frames,2u)&&s.actors[0].remaining==1u);
    CHECK(portable_ceremony_tracks_tick(&s,tracks,frames,2u)&&s.actors[0].remaining==0u);
    CHECK(portable_ceremony_tracks_tick(&s,tracks,frames,2u)&&s.actors[0].frame==0u);
    for(unsigned i=4u;i<256u;++i)CHECK(portable_ceremony_tracks_tick(&s,tracks,frames,2u));
    CHECK(s.actor_scroll_remaining==0&&s.actors[0].y_q16==144*65536);
    const PortableCeremonyMotion before=s;PortableCeremonySprite sprites[8];
    CHECK(portable_ceremony_track_sprites(&s,tracks,frames,2u,256,77,sprites));
    CHECK(!memcmp(&before,&s,sizeof s)&&sprites[0].depth==251&&sprites[0].y_q16==148*65536);
    const PortableCeremonySprite cell=portable_ceremony_cell_sprite(&s.trophy,9u,1u);
    CHECK(cell.image_id==9u&&!cell.visible&&!memcmp(&before,&s,sizeof s));
    tracks[0].initial_frame=2u;CHECK(!portable_ceremony_tracks_start(&s,tracks,frames,2u,0));
    return 0;
}
static int world_choreography(void)
{
    PortableCeremonyWorldRules rules={0};
    rules.grid[5]=(PortableCeremonyWorldPoint){100*65536,100*65536,0};
    rules.grid[6]=(PortableCeremonyWorldPoint){118*65536,104*65536,0};
    rules.grid[7]=(PortableCeremonyWorldPoint){118*65536,104*65536,0};
    rules.podium_offset[0].z_q16=0xa8000;
    rules.podium_offset[1]=(PortableCeremonyWorldPoint){-9*65536,0,0x58000};
    rules.podium_offset[2]=(PortableCeremonyWorldPoint){9*65536,0,0x38000};
    const uint8_t classes[4]={0u,2u,6u,7u};
    PortableCeremonyMotion s;portable_ceremony_motion_reset(&s);
    CHECK(portable_ceremony_world_init(&s,&rules,1u,classes));
    CHECK(s.world.count==3u&&s.world.selected_index==1u&&s.world.drivers[1].selected);
    CHECK(s.world.camera_x_q16==109*65536+32768&&s.world.camera_y_q16==104*65536&&s.world.camera_yaw==0u);
    CHECK(!s.world.drivers[0].visible&&s.world.drivers[0].source_flags==0x20u);
    for(unsigned index=0u;index<3u;++index){
        CHECK(portable_ceremony_world_reveal(&s,&rules,index));
        CHECK(s.world.drivers[index].heading==0x8000u&&s.world.drivers[index].visible);
        const unsigned frames=510u+16u*index;
        for(unsigned i=0u;i<frames;++i)CHECK(portable_ceremony_world_arrival_tick(&s,index)==0);
        CHECK(!s.world.drivers[index].arrived);
        CHECK(portable_ceremony_world_arrival_tick(&s,index)==1&&s.world.drivers[index].arrived);
    }
    CHECK(s.world.arrival_mask==7u);
    const PortableDriverAffineRow row={193u,211u,2u};
    PortableDriverAffineRules affine={0};affine.tables[0]=(PortableDriverAffineTable){1u,1u,&row};
    CHECK(portable_ceremony_world_hop_start(&s,&rules,0u)==156);
    CHECK(s.world.drivers[0].vx_q16==18432&&s.world.drivers[0].vy_q16==4096);
    CHECK(s.world.drivers[0].vz_q16==152576); /* (128+2*10.5)/64 units. */
    for(unsigned tick=1u;tick<=32u;++tick){
        CHECK(portable_ceremony_world_hop_tick(&s,0u,&affine)==0);
        CHECK(s.world.shadow_started==(tick>=24u));
    }
    CHECK(s.world.drivers[0].position.z_q16==0xc8000); /* Source snaps nextcall. */
    CHECK(portable_ceremony_world_hop_tick(&s,0u,&affine)==1);
    CHECK(s.world.drivers[0].position.z_q16==0xa8000&&s.world.hop_mask==1u);
    CHECK(s.world.drivers[0].response_88==-1&&(s.world.drivers[0].source_flags&0x40u));
    CHECK(portable_ceremony_world_place_podium(&s,&rules,1u)&&s.world.drivers[1].heading==0x8800u);
    CHECK(portable_ceremony_world_place_podium(&s,&rules,2u)&&s.world.drivers[2].heading==0x7800u);
    const PortableFinishRow animation[2]={{0u,1u,1u},{28u,2u,1u}};
    const PortableFinishSchedule schedule={2u,animation};
    CHECK(portable_ceremony_world_celebrate(&s,0u,&schedule)==1);
    CHECK(portable_ceremony_world_art_tick(&s,0u,NULL,&schedule,&affine,256u));
    CHECK(s.world.drivers[0].art_pose.frame_override==28u&&s.world.drivers[0].affine.inverse_x_q8==193u);
    CHECK(portable_ceremony_world_bounce_start(&s,0u));
    for(unsigned i=0u;i<32u;++i)CHECK(portable_ceremony_world_bounce_tick(&s,0u,128)==0);
    CHECK(s.world.drivers[0].position.z_q16==0xa8000+128*768);
    CHECK(portable_ceremony_world_bounce_tick(&s,0u,0)==1&&s.world.drivers[0].position.z_q16==0xa8000);
    CHECK(portable_ceremony_world_init(&s,&rules,7u,classes)&&s.world.count==4u&&s.world.selected_index==3u);
    CHECK(portable_ceremony_world_reveal(&s,&rules,3u));
    for(unsigned i=0u;i<120u;++i)CHECK(portable_ceremony_world_arrival_tick(&s,3u)==0);
    CHECK(portable_ceremony_world_arrival_tick(&s,3u)==1);
    for(unsigned i=0u;i<32u;++i)CHECK(portable_ceremony_world_rotate_loser_tick(&s)==0);
    CHECK(s.world.drivers[3].heading==0u&&portable_ceremony_world_rotate_loser_tick(&s)==1);
    CHECK(portable_ceremony_world_arrival_tick(&s,4u)==-1);
    return 0;
}
int main(void)
{
    CHECK(controllers()==0);CHECK(confetti()==0);CHECK(tracks_and_snapshot()==0);CHECK(world_choreography()==0);
    puts("portable ceremony motion: PASS");return 0;
}
