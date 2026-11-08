#include "item_rules_fixture.h"
#include "driver_response_fixture.h"
#include "source_random.h"
#include "driver_palette.h"
#include "lava_smoke.h"
#include "recovered_time_trial_session.h"
#include "time_trial_ghost_playback.h"
#include "thwomp_motion.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent pilot turns assign a transient motion observation by value.
 * Its two padding gaps are not state; compare every observation field while
 * retaining the original byte checks for the remainder of the driver. */
static int driver_states_equal(const PortableDriverPilotState *left,
                               const PortableDriverPilotState *right)
{
    const size_t observation = offsetof(PortableDriverPilotState, source_progress);
    const size_t suffix = observation + sizeof left->source_progress;
    if (memcmp(left, right, observation) ||
        memcmp((const unsigned char *)left + suffix,
               (const unsigned char *)right + suffix, sizeof *left - suffix))
        return 0;
    const PortableRaceMotionObservation *a = &left->source_progress;
    const PortableRaceMotionObservation *b = &right->source_progress;
    return a->old_x_q16 == b->old_x_q16 && a->old_y_q16 == b->old_y_q16 &&
           a->new_x_q16 == b->new_x_q16 && a->new_y_q16 == b->new_y_q16 &&
           a->drivable == b->drivable && a->track_cell == b->track_cell &&
           a->track_cell_valid == b->track_cell_valid && a->height_q16 == b->height_q16 &&
           a->hop_mode == b->hop_mode && a->checkpoint_mode == b->checkpoint_mode &&
           a->checkpoint_resample == b->checkpoint_resample;
}

#define CHECK(condition)                                                       \
    do                                                                         \
    {                                                                          \
        if (!(condition))                                                      \
        {                                                                      \
            fprintf(stderr, "recovered Time Trial session failed line %d: %s\n", \
                    __LINE__, #condition);                                     \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static PortableSurfaceTrailRules surface_trail_fixture(void)
{
    PortableSurfaceTrailRules rules={0};memset(rules.material_kind,1,sizeof rules.material_kind);
    rules.material_kind[0x54]=2u;
    for(unsigned i=0u;i<5u;++i){rules.frame_cutoffs[i][0]=9u;rules.frame_cutoffs[i][1]=19u;}
    rules.offroad_material_min=0x50u;rules.suppressed_moving_material=0x50u;rules.spray_material=0x5cu;
    return rules;
}

static void check_thwomp_landing_audio(void)
{
    PortableThwomp actor;
    CHECK(portable_thwomp_init(&actor,0,0,2u,0u));
    CHECK(portable_thwomp_landing_volume(&actor,487*65536,0,0u)==25u);
    CHECK(portable_thwomp_landing_volume(&actor,488*65536,0,0u)==0u);
    CHECK(portable_thwomp_landing_volume(&actor,0,0,0x20u)==0u);
    for(unsigned variant=0;variant<12u;++variant){
        CHECK(portable_thwomp_init(&actor,700*65536,700*65536,2u,variant));
        PortableThwompCourse course;
        PortableThwompSounds sounds={0};
        CHECK(portable_thwomp_course_init(&course,&actor,1u));
        unsigned landings=0u;
        for(unsigned tick=0;tick<600u;++tick){
            CHECK(portable_thwomp_motion_tick(&course,27u,"Time Trial",
                course.actors[0].x_q16,course.actors[0].y_q16,0u,&sounds));
            CHECK(sounds.serial==tick+1u && sounds.count<=1u);
            if(sounds.count){
                CHECK(sounds.volumes[0]==256u && course.actors[0].velocity_z_q16==0);
                ++landings;
                CHECK(portable_thwomp_motion_tick(&course,27u,"Time Trial",
                    course.actors[0].x_q16,course.actors[0].y_q16,0u,&sounds));
                CHECK(sounds.count==0u);
                break;
            }
        }
        CHECK(landings==1u);
    }
}

static void check_falling_star_cycle(void)
{
    for(unsigned variant=0;variant<6u;++variant){
        PortableFallingStar a;
        CHECK(portable_falling_star_init(&a,100*65536,100*65536,2u,(uint16_t)variant));
        CHECK(a.state==1u && !a.flags && !a.timer && a.affine_q8==224u);
        const int direction=(variant&1u)?1:-1;
        for(unsigned cycle=0;cycle<3u;++cycle){
            for(int t=0;t<=64;++t){
                const uint16_t angle=a.angle;
                CHECK(portable_falling_star_advance(&a));
                CHECK(a.state==3u && a.timer==64-t && a.art==0u && a.flags==3u);
                CHECK(a.height_q16==0x400000-t*t*1024);
                CHECK(a.x_q16==100*65536+a.offset_q16-t*t*(a.offset_q16/4096));
                CHECK(a.affine_q8==(t<11?960-t*64:256));
                CHECK(a.angle==(uint16_t)(angle+direction*1024));
            }
            int32_t z=0,vz=0x28000;
            for(int t=0;t<=40;++t){
                CHECK(portable_falling_star_advance(&a));
                z+=vz;if(z<0)z=0;vz-=0x2800;
                CHECK(a.state==5u && a.timer==40-t && a.flags==3u && a.affine_q8==256u);
                CHECK(a.x_q16==100*65536+(t+1)*direction*40960);
                CHECK(a.height_q16==z && a.velocity_z_q16==vz);
                CHECK(a.angle==(uint16_t)((t+1)*direction*768));
                if(t==21 || t==24 || t==33 || t==36)CHECK(a.art==1u);
                if(t==25 || t==28)CHECK(a.art==2u);
                if(t==29 || t==32)CHECK(a.art==3u);
                if(t>=37)CHECK(a.art==4u);
            }
            CHECK(portable_falling_star_advance(&a));
            CHECK(a.state==1u && a.timer==32+(int)(variant/2u)*16 && !a.flags && a.height_q16==0);
            const int32_t x=a.x_q16;
            for(int t=a.delay-1;t>=0;--t){
                CHECK(portable_falling_star_advance(&a));
                CHECK(a.state==1u && a.timer==t && a.x_q16==x && !a.flags);
            }
        }
    }
    PortableFallingStar invalid;
    CHECK(!portable_falling_star_init(&invalid,0,0,0,6u));
    CHECK(portable_falling_star_init(&invalid,0,0,0,0u));
    invalid.state=6u;CHECK(!portable_falling_star_advance(&invalid));
}

static void check_thundercloud_cycle(void)
{
    for(unsigned variant=0;variant<4u;++variant){
        PortableThundercloud a;
        uint8_t phases[2]={0};
        CHECK(portable_thundercloud_init(&a,100*65536,100*65536,2u,(uint16_t)variant));
        CHECK(a.art==4u && a.flags==1u && a.height_q16==4*65536);
        for(unsigned t=0;t<33u;++t){
            CHECK(portable_thundercloud_advance(&a,phases));
            CHECK(a.flags==1u && a.art==4u && a.state==(t==32u?16:1));
        }
        CHECK(phases[variant/2u]==((variant&1u)?0u:1u));
        const unsigned base=variant<2u?0u:4u;
        for(unsigned cycle=0;cycle<2u;++cycle){
            for(unsigned t=0;t<242u;++t){
                CHECK(portable_thundercloud_advance(&a,phases));
                CHECK(a.state==17 && a.timer==(t==241u?0:(int)t+1));
                CHECK((a.flags&2u)==(t>=234u?2u:0u));
                if(t==0u || t==64u || t==98u || t==162u || t==170u)CHECK(a.art==base);
                if(t==60u || t==94u || t==158u || t==166u)CHECK(a.art==base+1u);
                if(t==226u)CHECK(a.art==base+2u);
                if(t==234u)CHECK(a.art==base+3u);
                /* Palette clocks start on the preceding state16 tick. */
                if(!(variant&1u)){
                    const unsigned pt=t+1u;
                    if(pt==178u)CHECK(phases[variant/2u]==2u);
                    if(pt==210u)CHECK(phases[variant/2u]==6u);
                    if(pt==218u)CHECK(phases[variant/2u]==7u);
                    if(pt==222u)CHECK(phases[variant/2u]==8u);
                    if(pt==242u)CHECK(phases[variant/2u]==1u);
                }
            }
        }
    }
    PortableThundercloud initial[3];
    for(unsigned i=0;i<3u;++i)CHECK(portable_thundercloud_init(&initial[i],0,0,0,0));
    initial[0].state=initial[1].state=initial[2].state=17;
    initial[0].palette_timer=186;initial[1].palette_timer=222;initial[2].variant=1u;
    PortableThundercloudCourse c;
    CHECK(portable_thundercloud_course_init(&c,initial,3u));
    /* Last source placement runs first; the first placement's write wins. */
    for(unsigned i=3u;i>0u;--i)CHECK(portable_thundercloud_advance(&c.actors[i-1u],c.palette_phase));
    CHECK(c.palette_phase[0]==3u && !c.palette_phase[1]);
    CHECK(!portable_thundercloud_init(&initial[0],0,0,0,4u));
    c.actors[0].state=2;CHECK(!portable_thundercloud_advance(&c.actors[0],c.palette_phase));
}

static void check_mouse_motion(void)
{
    int32_t sine[1024]={0};sine[256]=32768;
    for(unsigned variant=0u;variant<2u;++variant){
        PortableMouse mouse;
        CHECK(portable_mouse_init(&mouse,100*65536,100*65536,2u,(uint16_t)variant));
        for(int t=0;t<=620;++t){
            const int32_t x=mouse.x_q16,y=mouse.y_q16;
            const int moving=t<=150 || (t>=311 && t<=460);
            CHECK(portable_mouse_advance(&mouse,sine,1024u));
            const int delta=moving?(t<=150?32768:-32768):0;
            CHECK(mouse.x_q16==x+(variant==0u?delta:0));
            CHECK(mouse.y_q16==y+(variant==1u?delta:0));
            CHECK(mouse.timer==(t==620?0:t+1));
            CHECK(mouse.turn_flip==((t>=191 && t<=210) || (t>=231 && t<=250) ||
                (t>=501 && t<=520) || (t>=541 && t<=560)));
            if(t==9)CHECK(mouse.art==1u && mouse.walk_ticks==0u);
            if(t==19)CHECK(mouse.art==0u && mouse.walk_ticks==0u);
        }
        CHECK(mouse.x_q16==100*65536+(variant==0u?32768:0));
        CHECK(mouse.y_q16==100*65536+(variant==1u?32768:0));
        const PortableMouse before=mouse;
        portable_mouse_contact(&mouse,0x2001u,1,1);
        CHECK(!memcmp(&before,&mouse,sizeof mouse));
        portable_mouse_contact(&mouse,0x1000u,0,1);
        CHECK(!memcmp(&before,&mouse,sizeof mouse));
        portable_mouse_contact(&mouse,0x2002u,0,1);
        CHECK(mouse.timer==32 && mouse.flags==1u && mouse.affine_y_q8==-256);
        for(unsigned tick=0u;tick<33u;++tick){
            CHECK(portable_mouse_advance(&mouse,sine,1024u));
            CHECK(mouse.timer==31-(int)tick && mouse.height_q16==(tick==16u?32768*48:0));
        }
        CHECK(!mouse.flags);
    }
    PortableMouse hopper;
    CHECK(portable_mouse_init(&hopper,100*65536,100*65536,2u,0x8001u));
    for(unsigned t=1;t<=60u;++t){
        CHECK(portable_mouse_advance(&hopper,sine,1024u));
        CHECK(hopper.timer==(int)t && hopper.height_q16==0 && hopper.hop_phase==(t==60u));
    }
    CHECK(hopper.velocity_z_q16==0x20000 && hopper.art==1u);
    int peak=0;
    for(unsigned t=0;t<17u;++t){
        CHECK(portable_mouse_advance(&hopper,sine,1024u));
        if(hopper.height_q16>peak)peak=hopper.height_q16;
    }
    CHECK(peak==0x90000 && hopper.height_q16==0 && !hopper.hop_phase && !hopper.timer);
    portable_mouse_contact(&hopper,0x2001u,0,1);
    for(unsigned t=0;t<25u;++t)CHECK(portable_mouse_advance(&hopper,sine,1024u));
    CHECK(hopper.flags==0u && hopper.height_q16==0 && hopper.affine_y_q8==-256);
}

static void check_common_prop_flight(void)
{
    const uint32_t codes[]={0x40000,0x50000,0x50002,0x60000,0x60001,
        0x80000,0x90000,0xa0000,0xa0007,0xc0000,0xc000b,0xe0000,0x860000,0x980000,0x980001,0xa10000,0xa10001};
    int32_t sine[1024]={0};sine[256]=32767;
    for(size_t i=0;i<sizeof codes/sizeof codes[0];++i){
        PortableCourseProp initial;
        CHECK(portable_course_prop_init(&initial,12*65536,20*65536,codes[i],224u,0u));
        PortableCourseProps course;
        CHECK(portable_course_props_init(&course,&initial,1u));
        CHECK(portable_course_props_advance(&course,sine,1024u));
        CHECK(course.actors[0].timer==0 && !course.actors[0].state);
        if(codes[i]==0x860000u)course.actors[0].contact_phase=1u;
        portable_course_prop_damage(&course.actors[0]);
        for(unsigned tick=0;tick<33u;++tick){
            CHECK(portable_course_props_advance(&course,sine,1024u));
            CHECK(course.actors[0].timer==31-(int32_t)tick);
            CHECK(!(course.actors[0].flags&2u));
            CHECK((course.actors[0].flags&1u)==(unsigned)(tick<32u));
            CHECK(course.actors[0].height_q16==(tick==16u?32767*48:0));
            CHECK(course.actors[0].affine_x_q8==224 && course.actors[0].affine_y_q8==-256);
            CHECK(course.actors[0].contact_phase==(codes[i]==0x860000u));
            portable_course_prop_damage(&course.actors[0]); /* no restart */
        }
        CHECK(portable_course_props_advance(&course,sine,1024u));
        CHECK(course.actors[0].timer==-1 && initial.state==0u && initial.flags);
        CHECK(portable_course_props_init(&course,course.initial,course.count));
        CHECK(course.actors[0].state==0u && course.actors[0].affine_y_q8==224 &&
            course.actors[0].contact_phase==0u);
    }
    PortableCourseProp pair[2];
    CHECK(portable_course_prop_init(&pair[0],12*65536,20*65536,0xc0000u,224u,0u));
    pair[1]=pair[0];pair[1].art_index=1u;
    PortableCourseProps overlap;
    CHECK(portable_course_props_init(&overlap,pair,2u));
    CHECK(portable_course_prop_at(&overlap,12,20)==&overlap.actors[1]);
    portable_course_prop_damage(&overlap.actors[1]);overlap.actors[1].flags=0u;
    CHECK(portable_course_prop_at(&overlap,12,20)==&overlap.actors[1]);
    const uint32_t decorations[]={0x10000u,0x10001u,0x20000u,0x70000u,
        0xb0000u,0xb0001u,0x850000u,0x8c0000u,0x900000u,0x9a0000u,0x9b0000u};
    for(size_t i=0;i<sizeof decorations/sizeof decorations[0];++i){
        CHECK(portable_course_prop_init(&pair[1],12*65536,20*65536,decorations[i],256u,1u));
        CHECK(pair[1].height_q16==((decorations[i]>>16u)==1u?-0x80000:0));
        CHECK(pair[1].flags==1u);
        CHECK(portable_course_props_init(&overlap,pair,2u));
        /* Later decoration must never mask a solid tile owner. */
        CHECK(portable_course_prop_at(&overlap,12,20)==&overlap.actors[0]);
        portable_course_prop_damage(&overlap.actors[1]);
        for(unsigned tick=0;tick<40u;++tick)
            CHECK(portable_course_props_advance(&overlap,sine,1024u));
        CHECK(memcmp(&overlap.actors[1],&pair[1],sizeof pair[1])==0);
        CHECK(portable_course_props_init(&overlap,&pair[1],1u));
        CHECK(portable_course_prop_at(&overlap,12,20)==NULL);
    }
}

static void check_mechakoopa_patrol(void)
{
    int32_t sine[1024]={0};
    for(unsigned variant=0;variant<2u;++variant){
        PortableCourseProp initial;
        CHECK(portable_course_prop_init(&initial,100*65536,120*65536,0xa10000u+variant,224u,5u));
        PortableCourseProps course;
        CHECK(portable_course_props_init(&course,&initial,1u));
        CHECK(portable_course_prop_at(&course,100,120)==NULL);
        CHECK(portable_course_prop_view_art(&initial,0u)==5u);
        int position=100*65536;
        for(unsigned tick=0;tick<704u;++tick){
            const unsigned cycle=tick%352u;
            const int walking=cycle<96u || (cycle>=176u && cycle<272u);
            const int vx=(variant?-32768:32768)*(cycle<176u?1:-1);
            if(walking)position+=vx;
            CHECK(portable_course_props_advance(&course,sine,1024u));
            const PortableCourseProp *a=&course.actors[0];
            CHECK(a->x_q16==position && a->y_q16==120*65536 && !a->height_q16 && a->flags==3u);
            CHECK(a->mechakoopa.cycle==(cycle+1u)%352u);
            const unsigned stride=cycle<96u?cycle+1u:cycle<176u?cycle-95u:cycle<272u?cycle-175u:cycle-271u;
            const unsigned duration=walking?6u:10u;
            CHECK(a->mechakoopa.animation_ticks==stride%duration);
            CHECK(a->mechakoopa.animation_phase==(stride/duration)%2u);
            CHECK(a->mechakoopa.heading==(vx>0?0x4000u:0xc000u));
            CHECK(portable_course_prop_view_art(a,a->mechakoopa.heading)==5u+(a->mechakoopa.animation_phase?2u:3u));
            CHECK(portable_course_prop_view_art(a,(uint16_t)(a->mechakoopa.heading+0x8000u))==5u+(a->mechakoopa.animation_phase?0u:1u));
        }
        CHECK(position==initial.x_q16);
        course.actors[0].mechakoopa.cycle=352u;
        CHECK(!portable_course_props_advance(&course,sine,1024u));
    }
    PortableCourseProp invalid;
    CHECK(!portable_course_prop_init(&invalid,0,0,0xa10002u,224u,0u));
    CHECK(!portable_course_prop_init(&invalid,0,0,0xa10000u,224u,61u));
}

static void check_pier_boo(void)
{
    int32_t sine[1024]={0};
    for(unsigned i=0;i<1024u;++i)sine[i]=(int32_t)i*64;
    PortableCourseProp initial;
    CHECK(portable_course_prop_init(&initial,100*65536,120*65536,0xa30000u,256u,0u));
    CHECK(!portable_course_prop_init(&initial,100*65536,120*65536,0xa30001u,256u,0u));
    CHECK(portable_course_prop_init(&initial,100*65536,120*65536,0xa30000u,256u,0u));
    CHECK(initial.flags==3u && initial.height_q16==0x80000 && !(initial.pier_boo.phase&0x3ffu));
    PortableCourseProps course;
    CHECK(portable_course_props_init(&course,&initial,1u));
    CHECK(portable_course_prop_at(&course,100,120)==NULL);
    /* 59F04 samples the current phase, then advances it by0x400. */
    for(unsigned tick=0;tick<80u;++tick){
        const uint16_t phase=course.actors[0].pier_boo.phase;
        int angle=(int16_t)phase;if(angle<0)angle+=63;
        CHECK(portable_course_props_advance(&course,sine,1024u));
        CHECK(course.actors[0].height_q16==sine[(uint16_t)angle/64u]*3+0x80000);
        CHECK(course.actors[0].pier_boo.phase==(uint16_t)(phase+1024u));
    }
    /* F0/F1: protected hits drop collision and fly from vz=C0000 by -4000. */
    portable_course_prop_damage(&course.actors[0]);
    CHECK(course.actors[0].state==5u);
    int32_t height=course.actors[0].height_q16,velocity=0xc0000;
    unsigned ticks=0;
    do{
        height+=velocity;if(height>=0)velocity-=0x4000;
        CHECK(portable_course_props_advance(&course,sine,1024u));
        CHECK(course.actors[0].height_q16==height && course.actors[0].affine_y_q8==-256);
        CHECK(course.actors[0].state==(height<0?8u:6u) && course.actors[0].flags==(height<0?0u:1u));
        CHECK(++ticks<200u);
    }while(height>=0);
    CHECK(portable_course_props_init(&course,&initial,1u));
    portable_pier_boo_vanish(&course.actors[0]);
    CHECK(course.actors[0].state==8u && course.actors[0].flags==0u);
    portable_course_prop_damage(&course.actors[0]);
    CHECK(course.actors[0].state==8u);
}

static void check_teepee(void)
{
    int32_t sine[1024]={0};
    for(unsigned i=0;i<1024u;++i)sine[i]=(int32_t)i*64;
    PortableCourseProp initial;
    CHECK(!portable_course_prop_init(&initial,100*65536,120*65536,0x030001u,256u,2u));
    CHECK(!portable_course_prop_init(&initial,100*65536,120*65536,0x030000u,256u,54u));
    CHECK(portable_course_prop_init(&initial,100*65536,120*65536,0x030000u,256u,2u));
    CHECK(initial.flags==3u && initial.height_q16==0);
    PortableCourseProps course;
    CHECK(portable_course_props_init(&course,&initial,1u));
    CHECK(portable_course_prop_at(&course,100,120)==NULL);
    portable_course_prop_damage(&course.actors[0]);
    CHECK(portable_course_props_advance(&course,sine,1024u) && course.actors[0].state==0u);
    PortableTeepeeKart karts[2]={{0},{.x_q16=90*65536,.y_q16=120*65536,.z_q16=0x10000,
        .speed_q16=0x20000,.component_x=-40,.component_y=8,.heading=0x4000u,.is_player=1u}};
    CHECK(portable_course_props_tick_teepees(&course,karts,2u,sine,1024u,2u)==0);
    CHECK(course.actors[0].state==0u && !course.actors[0].teepee.rider_state);
    /* 58B4C: the latest kart in the pass owns the burst velocity. */
    portable_teepee_hit(&course.actors[0],0u,5,5);
    portable_teepee_hit(&course.actors[0],1u,-40,8);
    CHECK(course.actors[0].state==1u && course.actors[0].teepee.kart==1u);
    CHECK(portable_course_props_tick_teepees(&course,karts,1u,sine,1024u,2u)==-1);
    /* 58AB0: the hit tick spawns the rider and takes the first burst step. */
    CHECK(portable_course_props_tick_teepees(&course,karts,2u,sine,1024u,2u)==0);
    const PortableTeepeeMotion *m=&course.actors[0].teepee;
    CHECK(course.actors[0].state==2u && course.actors[0].flags==1u && m->counter==1u && m->frame==1u);
    CHECK(course.actors[0].x_q16==100*65536-40*256 && course.actors[0].y_q16==120*65536+8*256);
    CHECK(karts[1].lock==360u && karts[0].lock==0u && m->rider_state==1u);
    CHECK(m->rider_x_q16==90*65536-sine[256]*12 && m->rider_y_q16==120*65536 &&
        m->rider_z_q16==0x10000+32768 && m->rider_heading==0x4000u);
    portable_teepee_hit(&course.actors[0],0u,5,5);
    CHECK(m->kart==1u && course.actors[0].state==2u);
    for(unsigned tick=2u;tick<=31u;++tick){
        karts[1].x_q16+=65536;karts[1].speed_q16=tick==5u?0x00a00000:0x20000;
        CHECK(portable_course_props_tick_teepees(&course,karts,2u,sine,1024u,tick)==0);
        CHECK(m->counter==tick && m->frame==(tick<11u?1u:tick<21u?2u:3u));
        CHECK(course.actors[0].state==(tick<31u?2u:3u) && course.actors[0].flags==(tick<31u?1u:0u));
        CHECK(m->rider_state==1u && m->rider_x_q16==karts[1].x_q16-sine[256]*12);
        CHECK(m->rider_z_q16==0x10000+(tick!=5u && (tick&2u)?32768:0));
    }
    CHECK(course.actors[0].x_q16==100*65536-31*40*256);
    /* 58DE8: an expired lock flings the rider with kart velocity (58CA4). */
    karts[1].lock=0u;karts[1].heading=0u;
    CHECK(portable_course_props_tick_teepees(&course,karts,2u,sine,1024u,40u)==1);
    CHECK(m->rider_state==2u && m->rider_vx_q16==-40*256 && m->rider_vy_q16==8*256 &&
        m->rider_vz_q16==0x12000 && m->rider_gravity==0x1800u && m->rider_spin_rate==0u);
    int32_t z=m->rider_z_q16,vz=0x12000;
    unsigned ticks=0u;
    do{
        z+=vz;if(z>=0)vz-=0x1800;
        CHECK(portable_course_props_tick_teepees(&course,karts,2u,sine,1024u,41u)==0);
        CHECK(m->rider_z_q16==z && m->rider_state==(z>=0?2u:0u));
        CHECK(++ticks<100u);
    }while(z>=0);
    CHECK(course.actors[0].state==3u && m->counter==31u);
    /* 58BA8's protected branch flings at once, three halves faster, spinning. */
    CHECK(portable_course_props_init(&course,&initial,1u));
    karts[1].protection=4;karts[1].lock=0u;karts[1].muted=1u;
    portable_teepee_hit(&course.actors[0],1u,-41,8);
    CHECK(portable_course_props_tick_teepees(&course,karts,2u,sine,1024u,2u)==0);
    CHECK(m->rider_state==2u && karts[1].lock==0u && m->rider_x_q16==karts[1].x_q16);
    CHECK(m->rider_vx_q16==-40*256*3/2 && m->rider_vy_q16==8*256*3/2 &&
        m->rider_spin_rate==0x1000u && m->rider_gravity==0x2000u);
    CHECK(portable_course_props_tick_teepees(&course,karts,2u,sine,1024u,3u)==0);
    CHECK(m->rider_spin==0x1000u);
    /* 58E10: seven views from camera minus kart heading minus spin. */
    PortableCourseProp view=course.actors[0];
    view.teepee.rider_heading=0u;view.teepee.rider_spin=0u;
    static const uint16_t cameras[]={0x0000u,0x3000u,0x3001u,0x5001u,0x8001u,0xb001u,0xd001u,0xf000u};
    static const unsigned views[]={2u,2u,1u,0u,4u,5u,6u,3u};
    for(unsigned i=0;i<8u;++i)CHECK(portable_teepee_rider_art(&view,cameras[i])==2u+4u+views[i]);
    CHECK(portable_course_prop_view_art(&course.actors[0],0u)==2u+course.actors[0].teepee.frame);
}

static void check_lake_rock(void)
{
    int32_t sine[1024]={0};
    PortableCourseProp initial;
    CHECK(!portable_course_prop_init(&initial,100*65536,120*65536,0xa20004u,224u,0u));
    CHECK(!portable_course_prop_init(&initial,100*65536,120*65536,0xa20000u,224u,60u));
    CHECK(portable_course_prop_init(&initial,100*65536,120*65536,0xa20003u,224u,0u));
    CHECK(initial.lake_rock.delay==0xa0 && initial.flags==0u);
    CHECK(portable_course_prop_init(&initial,100*65536,120*65536,0xa20000u,224u,2u));
    CHECK(initial.lake_rock.delay==0x70 && initial.flags==0u);
    PortableCourseProps course;
    CHECK(portable_course_props_init(&course,&initial,1u));
    CHECK(portable_course_prop_at(&course,100,120)==NULL);
    portable_course_prop_damage(&course.actors[0]);
    CHECK(course.actors[0].state==0u);
    PortableCoursePropSounds sounds={0};
    const PortableCoursePropListener near={.x_q16=100*65536,.y_q16=120*65536,.sounds=&sounds};
    /* 599EC: delay112, fall45, impact10, bounces23+13, rest128 (period331). */
    static const struct {unsigned tick;uint16_t song;} cues[]={{113u,386u},{158u,387u},{168u,388u},{191u,389u}};
    unsigned visible=0u,heard=0u;
    for(unsigned tick=1u;tick<=331u;++tick){
        CHECK(portable_course_props_advance_heard(&course,sine,1024u,&near));
        const PortableCourseProp *a=&course.actors[0];
        CHECK(sounds.serial==tick && sounds.count<=1u);
        if(sounds.count){
            CHECK(heard<4u && cues[heard].tick==tick && sounds.songs[0]==cues[heard].song && sounds.volumes[0]==256u);
            ++heard;
        }
        visible+=(a->flags&1u)!=0u;
        CHECK((a->flags==0u)==(tick<113u));
        if(tick==112u)CHECK(a->state==2u);
        if(tick==113u)CHECK(a->state==3u && a->height_q16==0x300000-0xc00 && a->affine_x_q8==0x3e8);
        if(tick==157u)CHECK(a->state==4u && a->height_q16==0 && a->affine_y_q8==0xc0);
        if(tick==158u)CHECK(a->state==5u && portable_course_prop_view_art(a,0u)==3u);
        if(tick==165u)CHECK(portable_course_prop_view_art(a,0u)==5u);
        if(tick==167u)CHECK(a->state==6u && a->lake_rock.frame==4u);
        if(tick==190u)CHECK(a->state==8u && a->lake_rock.velocity_z_q16==-0x11000 && a->height_q16==0);
        if(tick==191u)CHECK(a->state==9u && a->lake_rock.velocity_z_q16==0x8800-0x1800);
        if(tick==203u)CHECK(a->state==10u && a->x_q16==109*65536 && a->y_q16==129*65536);
    }
    CHECK(heard==4u && visible==219u && course.actors[0].state==0u && course.actors[0].flags==3u);
    CHECK(portable_course_props_advance_heard(&course,sine,1024u,&near));
    CHECK(course.actors[0].x_q16==100*65536 && course.actors[0].flags==0u && course.actors[0].lake_rock.timer==0x6f);
    /* Muted or distant listeners hear nothing; motion is unchanged. */
    CHECK(portable_course_props_init(&course,&initial,1u));
    const PortableCoursePropListener muted={.x_q16=100*65536,.y_q16=120*65536,.flags=0x20u,.sounds=&sounds};
    const PortableCoursePropListener far={.x_q16=100*65536+512*65536,.y_q16=120*65536,.sounds=&sounds};
    for(unsigned tick=1u;tick<=331u;++tick){
        CHECK(portable_course_props_advance_heard(&course,sine,1024u,tick&1u?&muted:&far));
        CHECK(sounds.count==0u);
    }
}

static void check_kamek_patrol(void)
{
    int32_t sine[1024]={0};sine[16]=100;sine[512]=-32768;
    PortableCourseProp initial;
    CHECK(portable_course_prop_init(&initial,100*65536,120*65536,0xa00000u,224u,5u));
    PortableCourseProps course;
    CHECK(portable_course_props_init(&course,&initial,1u));
    CHECK(portable_course_prop_at(&course,100,120)==NULL);
    CHECK(portable_course_prop_view_art(&initial,0u)==5u);
    CHECK(portable_course_props_advance(&course,sine,1024u));
    CHECK(course.actors[0].state==2u && course.actors[0].timer==64);
    CHECK(course.actors[0].height_q16==12*65536+300 && course.actors[0].x_q16==100*65536);
    for(unsigned tick=1;tick<=576u;++tick){
        CHECK(portable_course_props_advance(&course,sine,1024u));
        const PortableCourseProp *a=&course.actors[0];
        CHECK(a->kamek.phase==(uint16_t)((tick+1u)*1024u));
        if(tick==16u)CHECK(a->kamek.velocity_x_q16==32768 && a->x_q16==100*65536+245760);
        if(tick==31u)CHECK(a->height_q16==12*65536-32768*3);
        if(tick==64u || tick==320u || tick==576u)
            CHECK(a->state==3u && a->timer==128 && a->x_q16==127*65536+49152);
        if(tick==192u || tick==448u)
            CHECK(a->state==1u && a->timer==128 && a->x_q16==80*65536+16384);
        CHECK(portable_course_prop_view_art(a,a->kamek.heading)==6u);
        CHECK(portable_course_prop_view_art(a,(uint16_t)(a->kamek.heading+0x8000u))==5u);
    }
    const int32_t x=course.actors[0].x_q16;
    portable_course_prop_damage(&course.actors[0]);
    CHECK(course.actors[0].state==5u && course.actors[0].flags==3u);
    int height=course.actors[0].height_q16,velocity=3*65536;
    unsigned ticks=0;
    do {
        height+=velocity;if(height>=0)velocity-=0x4000;
        CHECK(portable_course_props_advance(&course,sine,1024u));
        CHECK(course.actors[0].height_q16==height && course.actors[0].x_q16==x);
        CHECK(course.actors[0].affine_x_q8==224 && course.actors[0].affine_y_q8==-256);
        CHECK(course.actors[0].state==(height<0?8u:6u));
        CHECK(course.actors[0].flags==(height<0?0u:1u));
        CHECK(++ticks<64u);
    }while(height>=0);
    CHECK(portable_course_props_init(&course,course.initial,course.count));
    CHECK(!memcmp(&course.actors[0],&initial,sizeof initial));
    course.actors[0].state=9u;
    CHECK(!portable_course_props_advance(&course,sine,1024u));
}

/* A stand-in for the platform script host: counts hooks and cancels the
 * first mushroom it is offered. */
typedef struct { unsigned ticks, offers; uint16_t first_item; } ScriptProbe;
static void probe_after_tick(void *context) { ++((ScriptProbe *)context)->ticks; }
static uint16_t probe_item_use(void *context, size_t racer, uint16_t item)
{
    ScriptProbe *probe = context;
    if (racer != 0u) return item;
    if (probe->offers++ == 0u) { probe->first_item = item; return 0u; }
    return item;
}

/* Construction consumes the same visual stream that weather uses later;
 * retry repeats construction before resetting the weather's spawn stagger. */
static void check_placed_visual_rng(const PortableRecoveredRaceView *base)
{
    PortableCourseProp props[3];
    for(unsigned i=0u;i<3u;++i)
        CHECK(portable_course_prop_init(&props[i],(800+(int)i*8)*65536,800*65536,0xa30000u,256u,0u));
    const PortableWeatherAssetView weather={.rules={.enabled=1u,.count=12u}};
    PortableRecoveredRaceView race=*base;race.course_props=props;race.course_prop_count=3u;
    race.weather_profile=&weather;
    PortableRecoveredTimeTrialSession session;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    CHECK(session.main_visual_rng_state==UINT32_C(2802067423) &&
        session.course_props.actors[0].pier_boo.phase==0u &&
        session.course_props.actors[1].pier_boo.phase==0x5000u &&
        session.course_props.actors[2].pier_boo.phase==0x2400u);
    const PortableCourseProps constructed=session.course_props;
    PortableRaceTickDisposition disposition;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    uint32_t expected=UINT32_C(2802067423);
    for(unsigned draw=0u;draw<4u;++draw)(void)portable_source_random(&expected);
    CHECK(session.main_visual_rng_state==expected && session.snow_weather.particles[0].active);
    const PortableSnowWeatherState first_weather=session.snow_weather;
    CHECK(session.marker_clock==1u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    const PortableRecoveredTimeTrialSession paused=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
        !memcmp(&paused,&session,sizeof session));
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.marker_clock==1u); /* Source visual blink survives Retry. */
    CHECK(session.main_visual_rng_state==UINT32_C(2802067423) &&
        !memcmp(&constructed,&session.course_props,sizeof constructed));
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
        session.main_visual_rng_state==expected &&
        !memcmp(&first_weather,&session.snow_weather,sizeof first_weather));
    CHECK(!props[0].pier_boo.phase && !props[1].pier_boo.phase && !props[2].pier_boo.phase);
    session.marker_clock=UINT32_MAX;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0&&
        session.marker_clock==0u&&session.marker_display_clock==UINT32_MAX);
    PortableRecoveredTimeTrialSnapshot marker_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&marker_snapshot)==0&&
        marker_snapshot.marker_clock==UINT32_MAX);
    race.weather_profile=NULL;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    portable_lava_smoke_start(&session.lava_smoke);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
        session.lava_smoke.puffs[0].age==1u &&
        session.main_visual_rng_state==UINT32_C(2802067423));
}

/* Use the imported motion schema's complete autonomous tables on the real
 * TT session, independently of the minimal fixtures used by contact tests. */
static void check_finished_follow_through(const PortableRecoveredRaceView *base)
{
    uint8_t targets[0x1a0]={0},modes[9]={0};
    int16_t deceleration[8]={-3,-7,-13,-21,-29,-43,-59,-79};
    int32_t sine[1024]={0};sine[256]=32768;sine[768]=-32768;
    memset(targets,4u,128u);memset(targets+128u,2u,160u);
    const PortableRacePoint alternate[6]={{100,40},{100,20},{40,20},
                                        {100,30},{100,10},{30,10}};
    const PortableFinishRow rows[]={{24u,1u,255u}};
    PortableFinishRules finish={.camera={1024u,32768u,80u,220u,3u}};
    for(unsigned i=0u;i<8u;++i)finish.schedules[i]=(PortableFinishSchedule){1u,rows};
    for(unsigned laps=3u;laps<=5u;laps+=2u){
        PortableRecoveredRaceView race=*base;
        const PortableGpFinishLakituRules lakitu={.row_count=2u,.entrance_ticks=22u,
            .bob_ticks=8u,.exit_ticks=3u,.frames={{21u,13u},{28u,20u}}};
        uint8_t bob[128]={0};
        race.finish_lakitu_rules=&lakitu;race.recovery_bob_offsets=bob;
        race.finish_rules=&finish;race.rules.lap_count=(uint16_t)laps;
        race.rules.progress_kind=PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
        race.rules.finish_kind=PORTABLE_FINISH_SOURCE_Y_CROSSING;
        race.rules.start_checkpoint_raw=(uint8_t)race.rules.start_route_index;
        race.rules.gp_route_rows=alternate;race.rules.gp_route_row_count=2u;
        race.rules.ai_modes=modes;race.rules.ai_mode_count=sizeof modes;
        race.dependencies.source_angle_target_table=targets;
        race.dependencies.source_angle_target_table_count=sizeof targets;
        race.dependencies.source_auto_deceleration_table=deceleration;
        race.dependencies.source_auto_deceleration_count=8u;
        race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
        PortableRecoveredTimeTrialSession session;
        CHECK(portable_recovered_time_trial_init(&session,&race)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        /* Represent B7B8's completed race with retained prior velocity. */
        session.runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
        session.runtime.session.countdown_remaining=0u;
        session.runtime.session.laps_completed=(uint16_t)laps;
        session.runtime.session.lap_started=1u;
        session.runtime.session.elapsed_ticks=laps*(laps+1u)*50u;
        for(unsigned i=0u;i<laps;++i)session.runtime.session.lap_ticks[i]=(i+1u)*100u;
        session.driver.motion.speed_q16=16*65536;
        session.driver.pose.component_y=-64;
        session.driver.steering.control_word=0x8000u;
        portable_driver_pilot_retire_selected(&session.driver);
        /* The fixture bypasses the crossing; retain its4000 request for next51014. */
        session.driver.hop.recovery_requests|=UINT16_C(0x4000);
        PortableRaceTickDisposition disposition;
        CHECK(portable_recovered_time_trial_tick_source(&session,0xffffu,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(disposition==PORTABLE_RACE_TICK_FINISHED && session.finish_camera.phase==10u &&
            session.finish_camera.snapshot.active && !session.finish_camera.snapshot.request_celebration);
        CHECK(session.hazard_lakitu.phase==PORTABLE_GP_LAKITU_FINISH &&
            session.hazard_lakitu.finish_turn==1u && !(session.driver.hop.recovery_requests&0x4000u));
        const PortableFinishCameraSnapshot held=session.finish_camera.snapshot;
        const int32_t initial_y=session.driver.pose.position_y;
        const uint32_t total=portable_race_session_total_ticks(&session.runtime.session);
        const uint32_t elapsed=session.runtime.session.elapsed_ticks;
        uint8_t ghost_before[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],ghost_after[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
        CHECK(portable_recovered_time_trial_ghost_candidate(&session,ghost_before));
        const PortableTimeTrialGhostEncoder encoder=session.ghost_encoder;
        uint32_t lap_times[PORTABLE_RACE_MAX_LAPS];memcpy(lap_times,session.runtime.session.lap_ticks,sizeof lap_times);
        for(unsigned tick=0u;tick<4u;++tick){
            const int32_t previous_y=session.driver.pose.position_y;
            CHECK(portable_recovered_time_trial_tick_source(&session,0xffffu,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
            CHECK(disposition==PORTABLE_RACE_TICK_FINISHED && session.driver.pose.position_y<previous_y &&
                session.driver.finished_initialized && session.driver.finished_route_row_191==1u);
            CHECK(!memcmp(&held,&session.finish_camera.snapshot,sizeof held));
            CHECK(session.hazard_lakitu.finish_turn==tick+2u);
            CHECK(session.runtime.session.elapsed_ticks==elapsed &&
                portable_race_session_total_ticks(&session.runtime.session)==total &&
                session.runtime.session.laps_completed==laps &&
                !memcmp(lap_times,session.runtime.session.lap_ticks,sizeof lap_times) &&
                session.mushrooms_remaining==3u);
        }
        CHECK(session.driver.pose.position_y<initial_y);
        CHECK(portable_recovered_time_trial_ghost_candidate(&session,ghost_after) &&
            !memcmp(ghost_before,ghost_after,sizeof ghost_before) &&
            !memcmp(&encoder,&session.ghost_encoder,sizeof encoder));
        CHECK(portable_recovered_time_trial_set_paused(&session,1)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        const PortableRecoveredTimeTrialSession paused=session;
        CHECK(portable_recovered_time_trial_tick_source(&session,0xffffu,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(disposition==PORTABLE_RACE_TICK_FINISHED && !memcmp(&paused,&session,sizeof session));
        CHECK(portable_recovered_time_trial_set_paused(&session,0)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.runtime.session.phase==PORTABLE_RACE_PHASE_COUNTDOWN &&
            !session.runtime.session.elapsed_ticks && !session.runtime.session.laps_completed &&
            !session.driver.finished_initialized && !(session.driver.steering.state_flags&0x2020u) &&
            !session.finish_camera.phase && !session.finish_camera.snapshot.active &&
            !session.hazard_lakitu.phase && !session.hazard_lakitu.finish_turn &&
            session.finish_lakitu_rules==&lakitu &&
            session.driver.pose.position_y==race.rules.start_y_q16);
    }
}

static void check_impact_session(const PortableRecoveredRaceView *base)
{
    const PortableImpactEffectRules rules={{14u,14u,14u,14u,13u,13u}};
    PortableRecoveredRaceView race=*base;
    race.impact_effect_rules=&rules;race.dependencies.impact_effect_rules=&rules;
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    PortableRecoveredTimeTrialSession session;PortableRaceTickDisposition disposition;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0 && session.driver.impact_selected &&
        session.runtime.recovered_dependencies->impact_effect_rules==&rules);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    session.driver.vector_effect.contact_word_5a=0xc007u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
        session.driver.impact_event==2u && !session.driver.vector_effect.contact_word_5a &&
        session.driver.impact_effects.slots[0].active && session.driver.impact_effects.slots[0].age==1u);
    PortableRecoveredTimeTrialSnapshot snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&snapshot)==0);
    const PortableImpactEffects held=snapshot.impact_effects;
    CHECK(!memcmp(&held,&session.driver.impact_effects,sizeof held));
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    const PortableRecoveredTimeTrialSession paused=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
        !memcmp(&paused,&session,sizeof session));
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_FINISHED;
    session.driver.steering.state_flags|=0x20u;
    session.driver.vector_effect.contact_word_5a=0xc007u;
    const unsigned life=rules.script_ticks[held.slots[0].variant];
    for(unsigned age=2u;age<=life+1u;++age){
        const int impact_status=portable_recovered_time_trial_tick_source(&session,0u,&disposition);
        CHECK(impact_status==0 && !session.driver.impact_event);
        CHECK(session.driver.impact_effects.slots[0].age==age &&
            session.driver.impact_effects.slots[0].active==(age<=rules.script_ticks[held.slots[0].variant]));
    }
    CHECK(!memcmp(&held,&snapshot.impact_effects,sizeof held));
    CHECK(portable_recovered_time_trial_restart(&session)==0 && session.driver.impact_selected &&
        !session.driver.impact_event && !session.driver.vector_effect.contact_word_5a);
    for(unsigned i=0u;i<PORTABLE_IMPACT_EFFECT_SLOTS;++i)CHECK(!session.driver.impact_effects.slots[i].active);
}

static void check_surface_trail_session(const PortableRecoveredRaceView *base)
{
    const PortableSurfaceTrailRules rules=surface_trail_fixture();
    uint8_t ground[256];memset(ground,0x54,sizeof ground);
    uint8_t flags[256]={0};flags[0x54]=0x54u;
    const PortableTerrainView terrain={ground,16u,16u,16u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    PortableRecoveredRaceView race=*base;race.surface_trail_rules=&rules;
    int32_t trail_sine[1024];
    for(unsigned i=0u;i<1024u;++i)trail_sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=trail_sine;race.dependencies.sine_count=1024u;
    race.dependencies.terrain=&terrain;race.dependencies.track_material=&material;
    PortableRecoveredTimeTrialSession session;PortableRaceTickDisposition disposition;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    for(unsigned tick=0u;tick<4u;++tick){
        session.driver.motion.speed_q16=256*65536;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
            session.surface_trails.eligible_counter==((tick+1u)&3u));
    }
    CHECK(session.surface_trails.slots[0].age==2u && session.surface_trails.slots[0].kind==2u &&
        session.surface_trails.side==1u);
    PortableRecoveredTimeTrialSnapshot snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&snapshot)==0 &&
        !memcmp(&snapshot.surface_trails,&session.surface_trails,sizeof session.surface_trails));
    const PortableSurfaceTrail held=snapshot.surface_trails;
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    const PortableRecoveredTimeTrialSession paused=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
        !memcmp(&session,&paused,sizeof session));
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.driver.pose.position_z=2*65536;session.driver.hop.mode=1u;
    session.driver.hop.height_q16=2*65536;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
        session.surface_trails.slots[0].age==3u && !session.surface_trails.eligible_counter);
    CHECK(!memcmp(&snapshot.surface_trails,&held,sizeof held));
    session.driver.steering.state_flags|=0x20u;
    const PortableSurfaceTrail before=session.surface_trails;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 && !session.surface_trails.visible &&
        !memcmp(session.surface_trails.slots,before.slots,sizeof before.slots));
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(!session.surface_trails.eligible_counter && !session.surface_trails.side && !session.surface_trails.extra_spray);
    for(unsigned i=0u;i<6u;++i)CHECK(!session.surface_trails.slots[i].age);
    race.surface_trail_rules=NULL;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.surface_trails.slots[0].age=5u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 && session.surface_trails.slots[0].age==5u);
}

static void check_recovery_effect_sessions(const PortableRecoveredRaceView *base)
{
    const PortableRecoveryFxRules rules={{127u,127u,127u},{0,1,1,1,2,2,3,3,1}};
    uint8_t ground[256];memset(ground,7,sizeof ground);
    uint8_t flags[256]={0x40u};flags[7]=0x22u;
    const PortableTerrainView terrain={ground,16u,16u,16u};
    const PortableTrackMaterialView material={.flags=flags,.count=256u};
    for(unsigned icy=0u;icy<2u;++icy){
        memset(ground,7,sizeof ground);
        PortableRecoveredRaceView race=*base;race.dependencies.terrain=&terrain;
        race.dependencies.track_material=&material;race.recovery_fx_rules=&rules;race.icy_recovery=(uint8_t)icy;
        PortableRecoveredTimeTrialSession session;PortableRaceTickDisposition disposition;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0 && session.icy_recovery==icy);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.hop.mode==4u && (session.hazard_effect_72&15u)==(icy?11u:13u));
        unsigned freeze_calls=0u,thaw_ticks=0u,drip_starts=0u,promoted=0u,paused=0u,touchdown=0u;
        for(unsigned tick=0u;tick<1100u;++tick){
            const unsigned before=session.hazard_effect_72&15u;
            CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
            PortableRecoveredTimeTrialSnapshot snapshot;
            CHECK(portable_recovered_time_trial_snapshot(&session,&snapshot)==0 &&
                !memcmp(&snapshot.recovery_fx,&session.recovery_fx,sizeof session.recovery_fx) &&
                snapshot.recovery_fx_events==session.recovery_fx_events &&
                snapshot.engine_sound.ice_thaw_started==!!(session.recovery_fx_events&PORTABLE_RECOVERY_FX_THAW_START));
            const unsigned mode=session.hazard_effect_72&15u;
            if(mode==(icy?12u:14u))promoted=1u;
            if(before==12u || mode==12u){
                ++freeze_calls;
                if(mode==12u && !session.driver.invulnerability_timer_6a)CHECK(snapshot.palette_bank==9u);
                if(!paused){
                    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
                    const PortableRecoveredTimeTrialSession held=session;
                    CHECK(portable_recovered_time_trial_tick_source(&session,0xffffu,&disposition)==0 &&
                        !memcmp(&held,&session,sizeof session));
                    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);paused=1u;
                    /* A fresh burn displaces the same live channel without
                     * manufacturing a thaw or retaining the icy palette. */
                    PortableRecoveredTimeTrialSession burn=session;
                    burn.runtime.recovered_driver=&burn.driver;burn.driver.burn_ticks_72=250u;
                    CHECK(portable_recovered_time_trial_tick_source(&burn,0u,&disposition)==0 &&
                        (burn.hazard_effect_72&15u)==3u && !burn.recovery_fx.thaw_age &&
                        !(burn.recovery_fx_events&PORTABLE_RECOVERY_FX_THAW_START));
                }
            }
            if(snapshot.recovery_fx_events&PORTABLE_RECOVERY_FX_THAW_START)
                CHECK(snapshot.recovery_fx.pending_thaw && !snapshot.recovery_fx.thaw_age);
            if(snapshot.recovery_fx.thaw_age){++thaw_ticks;CHECK(snapshot.recovery_fx.thaw_age<=12u);}
            if(snapshot.recovery_fx_events&PORTABLE_RECOVERY_FX_DRIP_START)++drip_starts;
            unsigned children=0u;
            for(unsigned i=0u;i<PORTABLE_RECOVERY_DRIPS;++i){
                CHECK(snapshot.recovery_fx.drips[i].age<=9u);children|=snapshot.recovery_fx.drips[i].age;
            }
            if(session.driver.hop.mode==6u)memset(ground,0,sizeof ground);
            touchdown|=session.driver.hop.recovery_touchdown;
            if(promoted && touchdown && mode!=12u && !snapshot.recovery_fx.thaw_age &&
               !snapshot.recovery_fx.pending_thaw &&
               !snapshot.recovery_fx.drip_emitter_ticks && !children)break;
        }
        CHECK(promoted && touchdown);
        if(icy)CHECK(freeze_calls==381u && thaw_ticks==12u && !drip_starts && paused);
        else CHECK(!freeze_calls && !thaw_ticks && drip_starts>0u);
        CHECK(portable_recovered_time_trial_restart(&session)==0 && !session.hazard_effect_72 &&
            !session.recovery_fx.thaw_age && !session.recovery_fx.pending_thaw &&
            !session.recovery_fx.drip_emitter_ticks && !session.recovery_fx_events);
        for(unsigned i=0u;i<PORTABLE_RECOVERY_DRIPS;++i)CHECK(!session.recovery_fx.drips[i].age);
    }
}

static void check_lap_hud_session(const PortableRecoveredRaceView *race)
{
    PortableRecoveredTimeTrialSession session;
    PortableRecoveredTimeTrialSnapshot first,second;
    PortableRaceTickDisposition disposition;
    CHECK(portable_recovered_time_trial_init(&session,race)==0);
    CHECK(session.lap_hud.pending_lap==1u&&session.lap_hud.displayed_lap==1u);
    session.runtime.session.max_progress_raw=UINT16_C(0x8100);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(portable_recovered_time_trial_snapshot(&session,&first)==0);
    CHECK(first.lap_hud.pending_lap==2u&&first.lap_hud.displayed_lap==1u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    const PortableLapHud frozen=session.lap_hud;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0&&
        memcmp(&frozen,&session.lap_hud,sizeof frozen)==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(portable_recovered_time_trial_snapshot(&session,&second)==0);
    CHECK(second.lap_hud.displayed_lap==2u&&first.lap_hud.displayed_lap==1u);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.lap_hud.pending_lap==1u&&session.lap_hud.displayed_lap==1u);
}

static void check_air_source_ghost(const PortableRecoveredRaceView *base)
{
    enum { TICKS=160 };
    typedef struct {
        PortableModernAirState air;
        int32_t x,y,z,speed,acceleration;
        uint32_t releases;
        uint16_t control;
    } AirGhostFrame;
    AirGhostFrame *frames=calloc(TICKS,sizeof *frames);
    PortableRecoveredTimeTrialSession *recorded=malloc(sizeof *recorded);
    PortableTimeTrialGhostPlayback *playback=malloc(sizeof *playback);
    CHECK(frames!=NULL&&recorded!=NULL&&playback!=NULL);
    PortableRecoveredRaceView race=*base;
    const PortableModernDriftRules rules={.profile_id=91u,.revision=2u,
        .charge_ticks={3u,6u,9u},.boost_ticks={4u,5u,6u},
        .cap_ratio_256={256u,272u,288u},.minimum_speed_ratio_256=1u,
        .acceleration_q16={1,2,3},
        .air={.minimum_air_ticks=2u,.trick_window_ticks=12u,.boost_ticks=6u,
            .cap_ratio_256=300u,.minimum_speed_ratio_256=1u,.acceleration_q16=3,
            .launch_mask=2u}};
    int32_t sine[1024];
    for(unsigned i=0u;i<1024u;++i)
        sine[i]=(int32_t)lround(sin((double)i*6.283185307179586/1024.0)*32768.0);
    race.dependencies.sine_table=sine;race.dependencies.sine_count=1024u;
    race.dependencies.modern_drift_rules=&rules;
    uint8_t raster_cells[32u*32u]={0},terrain_cells[64u*64u]={0};
    const PortableTrackRaster raster={.cells=raster_cells,.capacity=sizeof raster_cells,
        .width=32u,.height=32u};
    const PortableTerrainView terrain={.cells=terrain_cells,.row_stride=64u,.width=64u,.height=64u};
    race.dependencies.raster=&raster;race.dependencies.terrain=&terrain;
    race.rules.start_y_q16=400*65536;race.initial_driver.pose.position_y=race.rules.start_y_q16;
    /* Source vectors use whole speed units. A larger authored empty course
     * keeps one-unit movement in bounds throughout the short codec replay. */
    race.initial_driver.motion.speed_q16=65536;
    race.initial_driver.auto_acceleration.speed_q16=65536;
    race.initial_driver.forward_acceleration.speed_q16=65536;
    race.initial_driver.brake.speed_q16=65536;
    race.initial_driver.steering.speed_q16=65536;
    race.initial_driver.input.base_speed_q16=2*65536;
    CHECK(portable_recovered_time_trial_init(recorded,&race)==0);
    PortableRaceTickDisposition disposition;
    uint16_t previous=0u;
    unsigned armed=0u,tricked=0u,boosted=0u;
    for(unsigned tick=0u;tick<TICKS;++tick){
        uint16_t buttons=tick<2u?0u:PORTABLE_INPUT_ACCELERATE;
        if(tick==18u||tick==21u)buttons|=PORTABLE_INPUT_HOP;
        CHECK(portable_recovered_time_trial_tick(recorded,buttons,&disposition)==0);
        CHECK(recorded->ghost_recording_valid&&recorded->runtime.session.phase!=PORTABLE_RACE_PHASE_FINISHED);
        const PortableDriverPilotState *driver=&recorded->driver;
        frames[tick]=(AirGhostFrame){.air=driver->modern_air,
            .x=driver->pose.position_x,.y=driver->pose.position_y,.z=driver->pose.position_z,
            .speed=driver->motion.speed_q16,.acceleration=driver->motion.acceleration_q16,
            .releases=driver->charged_audio_cue_count,
            .control=portable_recovered_time_trial_source_control(buttons)};
        if((buttons&PORTABLE_INPUT_HOP)&&!(previous&PORTABLE_INPUT_HOP))frames[tick].control|=4u;
        previous=buttons;
        armed+=driver->modern_air.armed!=0u;tricked+=driver->modern_air.tricked!=0u;
        boosted+=driver->modern_air.last_applied!=0u;
    }
    CHECK(armed>0u&&tricked>0u&&boosted==rules.air.boost_ticks&&frames[TICKS-1u].releases==1u);
    CHECK(frames[TICKS-1u].x!=race.rules.start_x_q16||frames[TICKS-1u].y!=race.rules.start_y_q16);
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    memcpy(record,recorded->ghost_record,sizeof record);
    portable_time_trial_ghost_record_set_time(record,TICKS);
    CHECK(recorded->ghost_encoder.position<PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE);
    CHECK(portable_time_trial_ghost_playback_init(playback,&race,record)==PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
    PortableTimeTrialGhostDecoder decoder;
    CHECK(portable_time_trial_ghost_decoder_init(&decoder,record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
        PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE));
    for(unsigned tick=0u;tick<TICKS;++tick){
        uint16_t control;CHECK(portable_time_trial_ghost_decoder_next(&decoder,&control));
        CHECK(control==frames[tick].control); /* Includes both distinct Hop rising edges. */
        CHECK(portable_time_trial_ghost_playback_tick(playback,&disposition)==PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
        const PortableDriverPilotState *driver=&playback->session.driver;
        const PortableModernAirState *a=&driver->modern_air,*b=&frames[tick].air;
        CHECK(a->air_ticks==b->air_ticks&&a->boost_ticks==b->boost_ticks&&a->armed==b->armed&&
            a->tricked==b->tricked&&a->previous_hop==b->previous_hop&&a->last_applied==b->last_applied);
        CHECK(driver->pose.position_x==frames[tick].x&&driver->pose.position_y==frames[tick].y&&
            driver->pose.position_z==frames[tick].z&&driver->motion.speed_q16==frames[tick].speed&&
            driver->motion.acceleration_q16==frames[tick].acceleration&&
            driver->charged_audio_cue_count==frames[tick].releases);
    }
    CHECK(playback->decoded_ticks==TICKS&&!playback->ended);
    free(playback);free(recorded);free(frames);
}

int main(void)
{
    check_thwomp_landing_audio();
    check_common_prop_flight();
    check_mechakoopa_patrol();
    check_kamek_patrol();
    check_pier_boo();
    check_teepee();
    check_lake_rock();
    check_mouse_motion();
    check_thundercloud_cycle();
    check_falling_star_cycle();
    const PortableRacePoint route[3] = {
        {100, 100}, {100, 50}, {50, 50},
    };
    uint8_t cells[64] = {0};
    uint8_t terrain_cells[256] = {0};
    uint8_t material_flags[256] = {0x40};
    int16_t auto_acceleration[64] = {0};
    const int16_t forward_curve[8] = {0};
    const int16_t forward_local[16] = {0};
    int16_t steering_delta[10] = {
        0, 0, 0, 0, 0, 0, 0, 0, INT16_C(0x0080), 0,
    };
    int16_t steering_response[16] = {
        INT16_C(0x09ae), INT16_C(0x009f),
        INT16_C(0x006c), INT16_C(0x0094),
        INT16_C(0x09ae), INT16_C(0x009f),
        INT16_C(0x006c), INT16_C(0x0094),
        INT16_C(0x09ae), INT16_C(0x009f),
        INT16_C(0x006c), INT16_C(0x0094),
        INT16_C(0x09ae), INT16_C(0x009f),
        INT16_C(0x006c), INT16_C(0x0094),
    };
    int32_t sine[1024] = {0};
    uint8_t recovery_bob[128] = {0};
    uint16_t angle_lut[16384] = {0};
    const PortableTrackRaster raster = {
        .cells = cells, .capacity = 64u, .width = 8u, .height = 8u,
    };
    const PortableTerrainView terrain = {
        .cells = terrain_cells,
        .row_stride = 16u,
        .width = 16u,
        .height = 16u,
    };
    const PortableTrackMaterialView material = {
        .flags = material_flags,
        .count = 256u,
    };
    PortableRecoveredRaceView race = {
        .recovery_bob_offsets = recovery_bob,
        .rules = {
            .route = route,
            .route_count = 3u,
            .start_x_q16 = 100 * 65536,
            .start_y_q16 = 100 * 65536,
            .start_heading = 0u,
            .start_route_index = 2u,
            .progress_kind = PORTABLE_RACE_PROGRESS_DEMO_V1,
            .finish_kind = PORTABLE_FINISH_NORTHBOUND_Y,
            .finish_y_q16 = 100 * 65536,
            .lap_count = 3u,
            .countdown_ticks = 2u,
            .rescue_penalty_ticks = 180u,
            .compatibility = {
                .course_id = 40u,
                .handling_profile_id = PORTABLE_HANDLING_RECOVERED_V1,
            },
        },
        .dependencies = {.mushroom_rules=test_mushroom_rules(),
            .boo_rules=test_boo_rules(),
            .item_status_rules=test_item_status_rules(),
            .green_shell_rules=test_green_shell_rules(),
            .red_shell_rules=test_red_shell_rules(),
            .banana_rules=test_banana_rules(),
            .shell_orbit_rules=test_shell_orbit_rules(),
            .response_rules=portable_test_response_profile(),
            .auto_acceleration_table = auto_acceleration,
            .auto_acceleration_count = 64u,
            .forward_response_curve = forward_curve,
            .forward_response_count = 8u,
            .forward_local_table = forward_local,
            .forward_local_count = 16u,
            .steering_delta_table = steering_delta,
            .steering_delta_count = 10u,
            .steering_response_data = steering_response,
            .steering_response_data_count = 16u,
            .sine_table = sine,
            .sine_count = 1024u,
            .raster = &raster,
            .track_material = &material,
            .terrain = &terrain,
            .source_track_route = route,
            .source_track_route_count = 3u,
            .source_track_angle_lut = angle_lut,
            .source_track_angle_lut_count = 16384u,
        },
    };
    PortableRecoveredTimeTrialSession session;
    PortableRecoveredTimeTrialSession unchanged;
    PortableRecoveredTimeTrialSnapshot first;
    PortableRecoveredTimeTrialSnapshot second;
    PortableRaceTickDisposition disposition;
    uint8_t ghost_candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint8_t playback_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    PortableTimeTrialGhostEncoder playback_encoder;
    PortableTimeTrialGhostPlayback playback;

    race.dependencies.mushroom_voice_songs[0] = 202u;
    race.dependencies.mushroom_voice_songs[1] = 282u;
    race.initial_driver.item_cue_flags_58 = UINT16_C(0x10);
    race.initial_driver.pose.position_x = race.rules.start_x_q16;
    race.initial_driver.pose.position_y = race.rules.start_y_q16;
    check_finished_follow_through(&race);
    check_recovery_effect_sessions(&race);
    check_surface_trail_session(&race);
    check_impact_session(&race);
    check_placed_visual_rng(&race);
    check_lap_hud_session(&race);
    check_air_source_ghost(&race);
    CHECK(portable_recovered_time_trial_init(&session, &race) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.runtime.recovered_driver == &session.driver);
    CHECK(session.ghost_recording_valid != 0u &&
          session.ghost_encoder.position == 0u &&
          portable_time_trial_ghost_record_course(session.ghost_record) == 40u &&
          portable_time_trial_ghost_record_driver(session.ghost_record) == 0u);
    CHECK(portable_recovered_time_trial_snapshot(&session, &first) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(first.phase == PORTABLE_RACE_PHASE_COUNTDOWN &&
          first.compatibility.course_id == 40u && first.route_count == 3u &&
          first.next_route_index == 2u && first.lap_count == 3u);
    CHECK(first.camera_yaw ==
              (uint16_t)race.initial_driver.steering.steering_offset &&
          first.camera_adjustment == 0u);

    session.character_camera.current_yaw = UINT16_C(0x4cc9);
    session.character_camera.current_adjustment = UINT16_C(0x01c8);
    CHECK(portable_recovered_time_trial_snapshot(&session, &second) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(second.camera_yaw == UINT16_C(0x4e91) &&
          second.camera_adjustment == UINT16_C(0x01c8));
    session.character_camera.current_yaw = first.camera_yaw;
    session.character_camera.current_adjustment = first.camera_adjustment;

    unchanged = session;
    CHECK(portable_recovered_time_trial_snapshot(&session, &second) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(memcmp(&first, &second, sizeof first) == 0);
    CHECK(memcmp(&session.driver, &unchanged.driver,
                 sizeof session.driver) == 0);
    CHECK(memcmp(&session.runtime.session, &unchanged.runtime.session,
                 sizeof session.runtime.session) == 0);

    portable_time_trial_ghost_record_init(playback_record, 0u, 40u);
    portable_time_trial_ghost_record_set_time(playback_record, 2u);
    CHECK(portable_time_trial_ghost_encoder_init(
              &playback_encoder,
              playback_record + PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
              PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE));
    CHECK(portable_time_trial_ghost_encoder_append(&playback_encoder, 0u));
    CHECK(portable_time_trial_ghost_encoder_append(&playback_encoder, 0u));
    unchanged = session;
    CHECK(portable_time_trial_ghost_playback_init(
              &playback, &race, playback_record) ==
          PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
    CHECK(playback.session.runtime.recovered_driver ==
              &playback.session.driver &&
          playback.session.ghost_recording_valid == 0u &&
          playback.session.mushrooms_remaining == 3u);
    CHECK(portable_time_trial_ghost_playback_pause(&playback, 1) ==
          PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
    CHECK(portable_time_trial_ghost_playback_tick(&playback, &disposition) ==
              PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK &&
          disposition == PORTABLE_RACE_TICK_INVALID &&
          playback.decoded_ticks == 0u);
    CHECK(portable_time_trial_ghost_playback_pause(&playback, 0) ==
          PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
    CHECK(portable_time_trial_ghost_playback_tick(&playback, &disposition) ==
              PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK &&
          playback.decoded_ticks == 1u);
    CHECK(portable_time_trial_ghost_playback_restart(&playback) ==
              PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK &&
          playback.decoded_ticks == 0u && playback.ended == 0u);
    CHECK(portable_time_trial_ghost_playback_tick(&playback, &disposition) ==
          PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
    CHECK(portable_time_trial_ghost_playback_tick(&playback, &disposition) ==
          PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
    CHECK(portable_time_trial_ghost_playback_tick(&playback, &disposition) ==
              PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK &&
          playback.ended == 1u && playback.decoded_ticks == 2u);
    CHECK(portable_time_trial_ghost_playback_tick(&playback, &disposition) ==
              PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK &&
          playback.ended == 1u && playback.decoded_ticks == 2u);
    CHECK(memcmp(&session.driver, &unchanged.driver,
                 sizeof session.driver) == 0);
    CHECK(memcmp(&session.runtime.session, &unchanged.runtime.session,
                 sizeof session.runtime.session) == 0);

    /* Recorded source item edges must retain the original private inventory,
     * including the one-tick queued activation and restart. */
    {
        const uint16_t controls[]={0u,0u,UINT16_C(0x0040),UINT16_C(0x0040)};
        PortableRecoveredTimeTrialSession original;
        CHECK(portable_recovered_time_trial_init(&original,&race)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_time_trial_ghost_encoder_init(&playback_encoder,
            playback_record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE));
        for(size_t i=0u;i<4u;++i)
            CHECK(portable_time_trial_ghost_encoder_append(&playback_encoder,controls[i]));
        CHECK(portable_time_trial_ghost_playback_init(&playback,&race,playback_record)==PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
        for(size_t i=0u;i<4u;++i){
            CHECK(portable_recovered_time_trial_tick_source(&original,controls[i],&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
            CHECK(portable_time_trial_ghost_playback_tick(&playback,&disposition)==PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
            CHECK(driver_states_equal(&original.driver, &playback.session.driver));
            CHECK(original.mushrooms_remaining==playback.session.mushrooms_remaining);
        }
        CHECK(playback.session.mushrooms_remaining==2u&&playback.session.driver.steering.driver_mode==8u);
        CHECK(portable_time_trial_ghost_playback_restart(&playback)==PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
        CHECK(playback.session.mushrooms_remaining==3u);
    }

    /* A script-cancelled press is swallowed until release: the mushroom
     * stays, the ghost records no press, and its playback (which runs no
     * scripts) drives the same race and uses the same later mushroom. */
    {
        const uint16_t controls[]={0u,0u,UINT16_C(0x0040),UINT16_C(0x0040),UINT16_C(0x8040),
            UINT16_C(0x8000),UINT16_C(0x8040),UINT16_C(0x8000),UINT16_C(0x8000)};
        const size_t count=sizeof controls/sizeof controls[0];
        ScriptProbe probe={0};
        const PortableRaceScriptHooks hooks={&probe,probe_after_tick,probe_item_use};
        PortableRecoveredTimeTrialSession scripted;
        CHECK(portable_recovered_time_trial_init(&scripted,&race)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(scripted.script_hooks==NULL&&scripted.script_item_swallowed==0u);
        scripted.script_hooks=&hooks;
        for(size_t i=0u;i<count;++i){
            CHECK(portable_recovered_time_trial_tick_source(&scripted,controls[i],&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
            if(i==2u||i==3u||i==4u)CHECK(scripted.mushrooms_remaining==3u&&scripted.script_item_swallowed==1u);
        }
        CHECK(probe.ticks==count&&probe.offers==2u&&probe.first_item==0x1008u);
        CHECK(scripted.mushrooms_remaining==2u&&scripted.script_item_swallowed==0u);
        memcpy(playback_record,scripted.ghost_record,sizeof playback_record);
        portable_time_trial_ghost_record_set_time(playback_record,(uint16_t)count);
        CHECK(portable_time_trial_ghost_playback_init(&playback,&race,playback_record)==PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
        PortableRecoveredTimeTrialSession stock;
        CHECK(portable_recovered_time_trial_init(&stock,&race)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        for(size_t i=0u;i<count;++i){
            /* The same input without the cancelled press, scripted live. */
            const uint16_t control=(i>=2u&&i<=4u)?(uint16_t)(controls[i]&0xffbfu):controls[i];
            CHECK(portable_time_trial_ghost_playback_tick(&playback,&disposition)==PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK);
            CHECK(portable_recovered_time_trial_tick_source(&stock,control,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
            CHECK(driver_states_equal(&stock.driver, &playback.session.driver));
            CHECK(stock.mushrooms_remaining==playback.session.mushrooms_remaining);
        }
        CHECK(playback.session.mushrooms_remaining==2u);
        CHECK(memcmp(stock.ghost_record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,
            scripted.ghost_record+PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET,PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_SIZE)==0);
        CHECK(portable_recovered_time_trial_restart(&scripted)==PORTABLE_RECOVERED_TIME_TRIAL_OK&&
              scripted.script_item_swallowed==0u&&scripted.script_hooks==&hooks);
    }

    CHECK(portable_recovered_time_trial_set_paused(&session, 1) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.driver.motion.speed_q16 = -1;
    unchanged = session;
    CHECK(portable_recovered_time_trial_tick(&session,
                                              PORTABLE_INPUT_ACCELERATE,
                                              &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(disposition == PORTABLE_RACE_TICK_INVALID);
    CHECK(memcmp(&session.driver, &unchanged.driver,
                 sizeof session.driver) == 0);
    CHECK(memcmp(&session.runtime.session, &unchanged.runtime.session,
                 sizeof session.runtime.session) == 0);
    CHECK(session.driver.motion.speed_q16 == -1);
    CHECK(session.ghost_encoder.position == 0u);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, NULL) ==
          PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT);
    CHECK(session.driver.motion.speed_q16 == -1);

    CHECK(portable_recovered_time_trial_set_paused(&session, 0) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(disposition == PORTABLE_RACE_TICK_COUNTDOWN &&
          session.runtime.session.countdown_remaining == 1u);
    CHECK(session.ghost_encoder.position == 1u &&
          session.ghost_record[PORTABLE_TIME_TRIAL_GHOST_PAYLOAD_OFFSET + 2u] ==
              1u);

    CHECK(portable_recovered_time_trial_source_control(
              PORTABLE_INPUT_ACCELERATE) == UINT16_C(0x8000));
    CHECK(portable_recovered_time_trial_source_control(
              PORTABLE_INPUT_BRAKE | PORTABLE_INPUT_LEFT) ==
          UINT16_C(0x4200));
    CHECK(portable_recovered_time_trial_source_control(PORTABLE_INPUT_HOP) ==
          UINT16_C(0x0010));
    CHECK(portable_recovered_time_trial_source_control(PORTABLE_INPUT_ITEM) ==
          UINT16_C(0x0040));

    CHECK(session.mushrooms_remaining == 3u);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_ITEM, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.mushrooms_remaining == 2u &&
          session.driver.item_requests_b2 == UINT16_C(0x8000) &&
          session.driver.steering.driver_mode == 0u);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_ITEM, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.mushrooms_remaining == 2u &&
          session.driver.item_requests_b2 == 0u &&
          session.driver.steering.driver_mode == 8u &&
          session.driver.steering.exit_timer == test_mushroom_rules()->boost_ticks-1u &&
          session.driver.contact_audio.song_id == 125u &&
          session.driver.boost_voice.song_id == 202u &&
          session.driver.boost_particle_age == 1u &&
          session.driver.boost_particle_birth_height_q16 == 0);
    CHECK(portable_recovered_time_trial_set_paused(&session, 1) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && session.driver.boost_particle_age == 1u);
    CHECK(portable_recovered_time_trial_set_paused(&session, 0) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    for (unsigned use = 1u; use < 4u; ++use)
    {
        CHECK(portable_recovered_time_trial_tick(
                  &session, PORTABLE_INPUT_NONE, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_time_trial_tick(
                  &session, PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_time_trial_tick(
                  &session, PORTABLE_INPUT_NONE, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.mushrooms_remaining ==
              (use < 3u ? (uint8_t)(2u - use) : 0u));
    }
    /* 7D6C rejects airborne presses. Holding through landing must not
     * synthesize another edge or spend inventory; a fresh ground press does. */
    for (unsigned source_order = 0u; source_order < 2u; ++source_order)
    {
        race.initial_driver.source_update_order = (uint8_t)source_order;
        CHECK(portable_recovered_time_trial_init(&unchanged, &race) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        unchanged.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
        unchanged.runtime.session.countdown_remaining = 0u;
        for (unsigned tick = 0u; tick < 2u; ++tick)
            CHECK(portable_recovered_time_trial_tick(&unchanged,
                      PORTABLE_INPUT_HOP, &disposition) ==
                  PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(unchanged.driver.hop.mode == 1u);
        for (unsigned tick = 0u; tick < 25u; ++tick)
        {
            CHECK(portable_recovered_time_trial_tick(&unchanged,
                      PORTABLE_INPUT_ITEM, &disposition) ==
                  PORTABLE_RECOVERED_TIME_TRIAL_OK);
            CHECK(unchanged.mushrooms_remaining == 3u &&
                  unchanged.driver.item_requests_b2 == 0u &&
                  unchanged.driver.steering.driver_mode != 8u &&
                  unchanged.driver.contact_audio.song_id != 125u);
        }
        CHECK(unchanged.driver.hop.mode == 0u);
        CHECK(portable_recovered_time_trial_tick(&unchanged, 0u, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_time_trial_tick(&unchanged,
                  PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(unchanged.mushrooms_remaining == 2u &&
              unchanged.driver.item_requests_b2 == UINT16_C(0x8000));
        CHECK(portable_recovered_time_trial_tick(&unchanged,
                  PORTABLE_INPUT_ITEM, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(unchanged.mushrooms_remaining == 2u &&
              unchanged.driver.item_requests_b2 == 0u &&
              unchanged.driver.steering.exit_timer == test_mushroom_rules()->boost_ticks-1u &&
              unchanged.driver.contact_audio.song_id == 125u &&
              unchanged.driver.boost_voice.song_id == 202u);
        CHECK(portable_recovered_time_trial_restart(&unchanged) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK &&
              unchanged.driver.item_requests_b2 == 0u &&
              unchanged.mushrooms_remaining == 3u);
    }
    race.initial_driver.source_update_order = 0u;
    unchanged.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    unchanged.runtime.session.countdown_remaining = 0u;
    unchanged.driver.item_cue_flags_58 &= UINT16_C(0xffef);
    CHECK(portable_recovered_time_trial_tick(&unchanged, PORTABLE_INPUT_ITEM,
              &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_time_trial_tick(&unchanged, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(unchanged.driver.boost_voice.song_id == 282u &&
          unchanged.driver.contact_audio.song_id == 125u);
    const uint8_t initial_terrain[256] = {0};
    race.dependencies.owned_terrain_cells = terrain_cells;
    race.dependencies.initial_terrain_cells = initial_terrain;
    race.dependencies.owned_terrain_capacity = sizeof terrain_cells;
    terrain_cells[10] = 8u;
    session.driver.wall_mutation_count = 1u;
    CHECK(session.mushrooms_remaining == 0u &&
          session.driver.steering.exit_timer != test_mushroom_rules()->boost_ticks-1u);
    CHECK(portable_recovered_time_trial_restart(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(terrain_cells[10] == 0u && session.driver.wall_mutation_count == 0u);
    race.dependencies.owned_terrain_cells = NULL;
    race.dependencies.initial_terrain_cells = NULL;
    race.dependencies.owned_terrain_capacity = 0u;
    CHECK(session.mushrooms_remaining == 3u &&
          session.driver.item_requests_b2 == 0u &&
          session.previous_item_control == 0u &&
          session.ghost_encoder.position == 0u);
    CHECK(portable_recovered_time_trial_init(&session, &race) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);

    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 0u &&
          session.driver.pose.position_z == 0 &&
          session.previous_semantic_buttons == PORTABLE_INPUT_HOP);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 1u &&
          (uint16_t)session.driver.hop.velocity_q8 == UINT16_C(0x00e0) &&
          session.driver.pose.position_z == 0 &&
          session.previous_semantic_buttons == PORTABLE_INPUT_HOP);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 1u &&
          (uint16_t)session.driver.hop.velocity_q8 == UINT16_C(0x00c6) &&
          session.driver.pose.position_z == INT32_C(0x0000c600));
    for (unsigned tick = 2u; tick < 18u; ++tick)
        CHECK(portable_recovered_time_trial_tick(
                  &session, PORTABLE_INPUT_HOP, &disposition) ==
              PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 0u &&
          session.driver.pose.position_z == 0);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 0u);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_NONE, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 0u);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 1u);
    CHECK(portable_recovered_time_trial_snapshot(&session, &second) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && second.height_q16 == 0);
    CHECK(portable_recovered_time_trial_restart(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.previous_semantic_buttons == 0u &&
          session.driver.hop.mode == 0u);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && session.driver.hop.mode == 0u);
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_HOP, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && session.driver.hop.mode == 1u);

    CHECK(portable_recovered_time_trial_init(&session, &race) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);

    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    session.driver.pose.position_y = -1;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.runtime.session.rescue_count == 1u &&
          session.driver.source_hazard_kind == PORTABLE_DRIVER_HAZARD_BOUNDS &&
          session.driver.hop.mode == 5u && session.driver.pose.position_y < 0 &&
          session.driver.pose.position_z == 0 && session.driver.hop.recovery_requests == 0x20u &&
          session.runtime.boundary_exit == 0u && session.ghost_recording_valid != 0u &&
          session.runtime.session.elapsed_ticks == 1u);
    CHECK(portable_recovered_time_trial_set_paused(&session, 1) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    unchanged = session;
    CHECK(portable_recovered_time_trial_tick_source(&session, UINT16_C(0x8000), &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(memcmp(&session, &unchanged, sizeof session) == 0);
    CHECK(portable_recovered_time_trial_set_paused(&session, 0) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    unsigned bounds_ticks=0u;
    while (session.driver.hop.mode != 0u && bounds_ticks++ < 400u)
    {
        CHECK(portable_recovered_time_trial_tick(&session, PORTABLE_INPUT_ITEM, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.runtime.session.laps_completed == 0u && session.mushrooms_remaining == 3u);
    }
    CHECK(session.driver.hop.mode == 0u && session.driver.pose.position_z == 0 &&
          session.driver.pose.position_x == route[1].x * 65536 &&
          session.driver.pose.position_y == route[1].y * 65536 && bounds_ticks > 90u &&
          session.runtime.session.elapsed_ticks == bounds_ticks + 1u);
    /* A2B4 emits on every accepted panel turn, including refreshes. Both
     * size voices share A9AC's table; disabled and airborne entries are silent. */
    for (unsigned variant=0u;variant<2u;++variant) {
        CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        material_flags[0]=6u;
        session.driver.item_cue_flags_58=variant?0u:0x10u;
        session.driver.steering.response_flags=0x21u;
        session.driver.forward_acceleration.state_flags=0x21u;
        CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.driver.steering.driver_mode==8u && session.driver.contact_audio.song_id==125u &&
              session.driver.contact_audio.stop_song==163u &&
              session.driver.boost_voice.song_id==(variant?282u:202u) &&
              session.driver.boost_particle_age==0u);
        const uint32_t serial=session.driver.contact_audio.serial;
        CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.driver.contact_audio.serial==serial+1u && session.driver.contact_audio.stop_song==0u);
        session.driver.steering.state_flags|=0x20u;
        CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.driver.contact_audio.serial==serial+1u);
        session.driver.steering.state_flags&=~UINT32_C(0x20);
        session.driver.hop.mode=1u;
        session.driver.hop.velocity_q8=224;
        session.driver.pose.position_z=65536;
        CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.driver.contact_audio.serial==serial+1u);
    }
    /* A ground ramp launches after motion, then lands without a rescue or
     * a second launch on A58C's landing turn. */
    CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    material_flags[0]=UINT8_C(0x10);
    sine[56]=16384;sine[200]=24576;
    CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode==1u && session.driver.steering.driver_mode==1u &&
          session.driver.pose.position_z==INT32_C(0x28000) &&
          session.driver.motion.speed_q16==INT32_C(0x2280000) &&
          session.runtime.session.rescue_count==0u);
    unsigned jump_ticks=0;
    while(session.driver.hop.mode==1u && jump_ticks++<100u)
        CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(jump_ticks<100u && session.driver.hop.landed &&
          session.driver.steering.driver_mode==0u && session.driver.pose.position_z==0 &&
          session.runtime.session.rescue_count==0u);
    sine[56]=0;sine[200]=0;
    CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    material_flags[0]=8u;
    session.driver.motion.speed_q16=INT32_C(0x04000000);
    CHECK(portable_recovered_time_trial_tick_source(&session,UINT16_C(0x8000),&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.steering.response_status==8u &&
          session.driver.steering.spin_delay==59u &&
          session.driver.motion.acceleration_q16==0 &&
          session.driver.steering.position_82==0x600u);
    material_flags[0]=0x40u;
    CHECK(portable_recovered_time_trial_tick_source(&session,UINT16_C(0x4000),&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.steering.response_status==14u &&
          session.driver.steering.spin_delay==0u &&
          session.driver.steering.position_82==0u);
    material_flags[0]=UINT8_C(0x26);
    CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode==5u && session.driver.pose.position_z==0 &&
          session.driver.pose.position_x==100*65536 && session.driver.pose.position_y==100*65536 &&
          session.driver.source_hazard_kind==PORTABLE_DRIVER_HAZARD_BOUNDS);
    material_flags[0]=UINT8_C(0x40);
    /* Every finite edge returns through the same native path, without a
     * penalty teleport. An airborne boundary crossing waits for landing. */
    for (unsigned edge=0u; edge<4u; ++edge)
    {
        CHECK(portable_recovered_time_trial_restart(&session) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        if(edge<2u)session.driver.pose.position_x=edge==0u ? -1 : 128*65536;
        else session.driver.pose.position_y=edge==2u ? -1 : 128*65536;
        CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.driver.hop.mode==5u && session.runtime.session.rescue_count==1u);
    }
    CHECK(portable_recovered_time_trial_restart(&session) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.driver.pose.position_y=-1;
    session.driver.hop.mode=1u;
    session.driver.pose.position_z=2*65536;
    session.driver.hop.velocity_q8=224;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.runtime.session.rescue_count==0u && session.runtime.boundary_exit==0u &&
          session.driver.source_hazard_kind==PORTABLE_DRIVER_HAZARD_NONE);

    CHECK(portable_recovered_time_trial_restart(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    session.driver.motion.speed_q16 = -1;
    session.driver.motion.acceleration_q16 = -INT32_C(0x00020000);
    session.driver.motion.component_x = -1;
    session.driver.motion.component_y = -1;
    session.driver.pose.component_x = -1;
    session.driver.pose.component_y = -1;
    CHECK(portable_recovered_time_trial_tick(
              &session, PORTABLE_INPUT_LEFT, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.motion.speed_q16 == 0 &&
          session.driver.motion.acceleration_q16 == 0 &&
          session.driver.motion.component_x == 0 &&
          session.driver.motion.component_y == 0 &&
          session.driver.pose.component_x == 0 &&
          session.driver.pose.component_y == 0);

    CHECK(portable_recovered_time_trial_restart(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    session.runtime.session.next_route_index = 1u;
    session.driver.motion.speed_q16 = 123;
    session.driver.pose.position_x = 77;
    session.driver.contact_audio=(PortableDriverContactAudio){.serial=7u,.song_id=372u,
        .volume=256u,.restart=1u};
    session.driver.boost_voice=(PortableDriverContactAudio){.serial=3u,.song_id=161u,
        .volume=128u,.pitch=-12};
    session.driver.charged_audio_cue_count=2u;
    CHECK(portable_recovered_time_trial_rescue(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.pose.position_x == route[0].x * 65536 &&
          session.driver.pose.position_y == route[0].y * 65536 &&
          session.driver.motion.speed_q16 == 0 &&
          session.runtime.session.rescue_count == 1u &&
          session.runtime.session.elapsed_ticks == 180u &&
          session.ghost_recording_valid == 0u);
    CHECK(session.driver.contact_audio.serial==7u && session.driver.contact_audio.song_id==372u &&
          session.driver.contact_audio.volume==256u && session.driver.contact_audio.restart==1u &&
          session.driver.boost_voice.serial==3u && session.driver.boost_voice.song_id==161u &&
          session.driver.boost_voice.volume==128u && session.driver.boost_voice.pitch==-12 &&
          session.driver.charged_audio_cue_count==2u);

    CHECK(portable_recovered_time_trial_restart(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN &&
          session.runtime.session.countdown_remaining == 2u &&
          session.runtime.session.rescue_count == 0u &&
          session.driver.pose.position_x == race.rules.start_x_q16);

    terrain_cells[12u * 16u + 12u] = 7u;
    material_flags[7] = UINT8_C(0x22);
    race.rules.compatibility.course_id = 33u;
    CHECK(portable_recovered_time_trial_init(&session, &race) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.source_terrain22_active && session.driver.hop.mode == 4u &&
          session.driver.drift_charge.charge_counter == 144u &&
          session.runtime.session.rescue_count == 1u && session.hazard_effect_72 == 13u);
    CHECK(session.entry_splash.age==1u && session.entry_splash.x==120 &&
          session.entry_splash.y==140 && session.entry_splash.scale_q8==128u);
    CHECK(portable_recovered_time_trial_snapshot(&session,&second)==PORTABLE_RECOVERED_TIME_TRIAL_OK &&
          second.entry_splash.age==1u && second.entry_splash.y==140);
    CHECK(portable_recovered_time_trial_rescue(&session) == PORTABLE_RECOVERED_TIME_TRIAL_ACTION_REJECTED);
    CHECK(portable_recovered_time_trial_set_paused(&session, 1) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    unchanged = session;
    CHECK(portable_recovered_time_trial_tick_source(&session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(memcmp(&session, &unchanged, sizeof session) == 0);
    CHECK(portable_recovered_time_trial_set_paused(&session, 0) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    for(unsigned tick=0;tick<84u;++tick){
        CHECK(portable_recovered_time_trial_tick(&session, PORTABLE_INPUT_ITEM, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.entry_splash.age==(tick<27u?tick+2u:0u) && session.entry_splash.y==140);
    }
    CHECK(session.driver.drift_charge.charge_counter==60u && session.driver.hop.recovery_requests==0x400u &&
          session.hazard_lakitu.phase==0u);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.hazard_lakitu.phase==1u && session.hazard_lakitu.callback==PORTABLE_GP_RECOVERY_LAKITU_DESCENT);
    unsigned water_ticks=86u;
    while(session.driver.hop.mode!=0u && water_ticks++<500u)
    {
        CHECK(portable_recovered_time_trial_tick(&session, PORTABLE_INPUT_ITEM, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.runtime.session.laps_completed==0u && session.mushrooms_remaining==3u);
    }
    CHECK(session.driver.hop.mode==0u && session.driver.pose.position_z==0 && water_ticks<500u &&
          session.driver.pose.position_x==route[1].x*65536 && session.driver.pose.position_y==route[1].y*65536 &&
          (uint16_t)session.driver.motion.angle==UINT16_C(0x8000) && session.runtime.session.rescue_count==1u);
    /* Restart discards an in-flight actor as well as the driver recovery. */
    CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.hazard_lakitu.phase==0u && !session.hazard_lakitu.visible && session.hazard_effect_72==0u);

    /* A later water entry preserves laps and retrieves from the raster's
     * checkpoint, rather than rewinding to the initial finish band. */
    cells[6u * 8u + 6u] = 1u;
    CHECK(portable_recovered_time_trial_init(&session, &race) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    session.runtime.session.progress_kind = PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
    session.runtime.session.checkpoint_raw = 1u;
    session.runtime.session.lap_raw = UINT8_C(0x81);
    session.runtime.session.laps_completed = 1u;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.runtime.session.checkpoint_raw == 1u &&
          session.runtime.session.lap_raw == UINT8_C(0x81) &&
          session.runtime.session.laps_completed == 1u &&
          session.driver.hop.mode == 4u);
    for(unsigned tick=0;tick<25u;++tick)
        CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.recovery_target_x_9e==route[1].x && session.driver.recovery_target_y_a0==route[1].y);

    cells[6u * 8u + 6u] = 0u;
    /* Reach a shore while sinking:9904 hops out and200 cancels the actor. */
    CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    for(unsigned tick=0u;tick<85u;++tick)
        CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.hazard_lakitu.phase==1u);
    material_flags[7]=UINT8_C(0x40);
    CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode==1u && session.driver.hop.recovery_requests==0x200u && session.hazard_effect_72==0u);
    CHECK(portable_recovered_time_trial_tick(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.recovery_requests==0u && session.hazard_lakitu.phase==5u &&
          session.driver.source_terrain22_active==0u);

    /* Native void hold, checkpoint carry and descent. Player input cannot
     * shorten the source timer, advance laps in the air or spend mushrooms. */
    material_flags[7] = UINT8_C(0x20);
    race.rules.compatibility.course_id = 37u;
    CHECK(portable_recovered_time_trial_init(&session, &race) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hazard_audio_kind==PORTABLE_DRIVER_HAZARD_VOID &&
          session.driver.hazard_audio_variant==0u);
    CHECK(session.driver.source_hazard_kind && session.driver.hop.mode == 2u &&
          session.driver.drift_charge.charge_counter == 60u &&
          session.driver.pose.position_z == 256 && session.driver.fall_frames_76==0u &&
          session.runtime.session.rescue_count == 1u);
    CHECK(portable_recovered_time_trial_snapshot(&session,&second)==0 &&
        second.fall_frames_76==0u && !second.driver_hidden);
    CHECK(portable_recovered_time_trial_rescue(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_ACTION_REJECTED);
    CHECK(portable_recovered_time_trial_set_paused(&session, 1) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    unchanged = session;
    CHECK(portable_recovered_time_trial_tick_source(&session, UINT16_C(0x8000),
              &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(memcmp(&session, &unchanged, sizeof session) == 0);
    CHECK(portable_recovered_time_trial_set_paused(&session, 0) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    for (unsigned tick = 0u; tick < 60u; ++tick)
    {
        CHECK(portable_recovered_time_trial_tick(&session, PORTABLE_INPUT_ACCELERATE | PORTABLE_INPUT_ITEM,
                  &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.driver.hop.mode == 2u && session.driver.motion.speed_q16 == 0);
        const uint16_t sink=(uint16_t)(tick<15u?tick+1u:15u);
        CHECK(session.driver.fall_frames_76==sink &&
            session.driver.pose.position_z==-(int32_t)sink*32768 &&
            session.driver.steering.position_82==0u);
        CHECK(portable_recovered_time_trial_snapshot(&session,&second)==0 &&
            second.fall_frames_76==sink && second.driver_hidden==(sink==15u));
        /* Original calls1754/1755:14 remains visible,15 disappears.
         * Pause must retain both the sink owner and its published boundary. */
        if(tick==13u||tick==14u){
            CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
            unchanged=session;
            CHECK(portable_recovered_time_trial_tick_source(&session,0xffffu,&disposition)==0 &&
                !memcmp(&session,&unchanged,sizeof session));
            CHECK(portable_recovered_time_trial_snapshot(&session,&second)==0 &&
                second.fall_frames_76==sink && second.driver_hidden==(sink==15u));
            CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
        }
    }
    CHECK(session.driver.drift_charge.charge_counter == 0u);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 6u &&
          session.driver.pose.position_z == INT32_C(0x300000) &&
          session.driver.recovery_target_x_9e == 100u &&
          session.driver.recovery_target_y_a0 == 50u &&
          session.driver.recovery_target_angle_a2 == UINT16_C(0x8000));
    /* Original call1801: mode6 retains terminal76 for one turn. */
    CHECK(portable_recovered_time_trial_snapshot(&session,&second)==0 &&
        second.fall_frames_76==15u && second.driver_hidden);
    unsigned fall_ticks = 0u;
    while (session.driver.hop.mode != 0u && fall_ticks++ < 400u)
    {
        const int result = portable_recovered_time_trial_tick(&session,
            PORTABLE_INPUT_NONE, &disposition);
        if (result != PORTABLE_RECOVERED_TIME_TRIAL_OK)
            fprintf(stderr, "fall tick %u: mode=%u runtime=%d driver=%d material=%d\n",
                fall_ticks, session.driver.hop.mode, session.last_runtime_status,
                session.runtime.last_driver_status, session.runtime.last_material_status);
        CHECK(result == PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.runtime.session.laps_completed == 0u);
        if(fall_ticks==1u){
            /* Original call1802: the non2 prefix resets76 before carry. */
            CHECK(portable_recovered_time_trial_snapshot(&session,&second)==0 &&
                session.driver.fall_frames_76==0u && second.fall_frames_76==0u &&
                !second.driver_hidden);
        }
    }
    CHECK(session.driver.hop.mode == 0u && session.driver.hop.recovery_touchdown &&
          session.driver.pose.position_x == 100 * 65536 &&
          session.driver.pose.position_y == 50 * 65536 &&
          session.driver.pose.position_z == 0 && session.mushrooms_remaining == 3u &&
          (session.driver.hop.recovery_requests & UINT16_C(0x670)) == 0u);
    CHECK(portable_recovered_time_trial_snapshot(&session, &second) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && second.fall_mode == 0u);
    CHECK(portable_recovered_time_trial_restart(&session) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && !session.driver.source_hazard_kind &&
          !session.driver.fall_frames_76);
    /* A hop over a void tile does not dispatch the grounded fall branch. */
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.driver.hop.mode = 1u;
    session.driver.pose.position_z = 2 * 65536;
    session.driver.hop.velocity_q8 = 224;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && !session.driver.source_hazard_kind);
    material_flags[7] = UINT8_C(0x28);
    CHECK(portable_recovered_time_trial_restart(&session) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.driver.steering.state_flags |= UINT32_C(0x4000);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && !session.driver.source_hazard_kind);
    session.driver.steering.state_flags &= ~UINT32_C(0x4000);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) ==
          PORTABLE_RECOVERED_TIME_TRIAL_OK && session.driver.source_hazard_kind);
    /* Lava selects a return checkpoint immediately, without the void timer.
     * Actor requests are consumed one turn later and carry cannot score laps. */
    material_flags[7] = UINT8_C(0x24);
    CHECK(portable_recovered_time_trial_init(&session, &race) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    session.runtime.session.phase = PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining = 0u;
    session.driver.drift_charge.charge_counter = 200u;
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hazard_audio_kind==PORTABLE_DRIVER_HAZARD_LAVA);
    CHECK(session.entry_splash.age==1u && session.entry_splash.x==120 && session.entry_splash.y==140 &&
          session.driver.burn_ticks_72==249u && session.driver.palette_bank==5u &&
          session.lava_smoke.emitter_ticks==PORTABLE_LAVA_SMOKE_TICKS-1u);
    /* 99A4 raises CPU karts to48; the selected8000 kart retains its height
     * while the same local Lakitu owner lifts it on subsequent turns. */
    CHECK(session.driver.source_hazard_kind == PORTABLE_DRIVER_HAZARD_LAVA &&
          session.driver.hop.mode == 3u && session.driver.pose.position_z == 0 &&
          session.driver.recovery_target_x_9e == 100u &&
          session.driver.hop.recovery_requests == 0x10u &&
          session.hazard_lakitu.phase == 0u && session.runtime.session.rescue_count == 1u);
    CHECK(portable_recovered_time_trial_set_paused(&session, 1) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    unchanged = session;
    CHECK(portable_recovered_time_trial_tick_source(&session, UINT16_C(0x8000), &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(memcmp(&session, &unchanged, sizeof session) == 0);
    CHECK(portable_recovered_time_trial_set_paused(&session, 0) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_time_trial_tick(&session, 0u, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hazard_audio_kind==0u);
    CHECK(session.driver.hop.mode == 3u && session.hazard_lakitu.phase == 2u &&
          session.driver.hop.recovery_requests == 0u && session.hazard_lakitu.y == -14);
    unsigned lava_ticks = 0u;
    while (session.driver.hop.mode != 0u && lava_ticks++ < 400u)
        CHECK(portable_recovered_time_trial_tick(&session, PORTABLE_INPUT_ITEM, &disposition) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.driver.hop.mode == 0u && session.driver.pose.position_z == 0 &&
          session.driver.pose.position_x == 100 * 65536 &&
          session.driver.pose.position_y == 50 * 65536 && session.mushrooms_remaining == 3u &&
          session.runtime.session.laps_completed == 0u);
    material_flags[7] = UINT8_C(0x22);
    race.rules.compatibility.course_id = 33u;
    CHECK(portable_recovered_time_trial_init(&session, &race) == PORTABLE_RECOVERED_TIME_TRIAL_OK);

    CHECK(portable_recovered_time_trial_init(&session, &race) == PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(!portable_recovered_time_trial_ghost_candidate(
        &session, ghost_candidate));
    session.runtime.session.phase = PORTABLE_RACE_PHASE_FINISHED;
    /* This fixture bypasses B98C's finish edge; apply its B7B8 retirement. */
    portable_driver_pilot_retire_selected(&session.driver);
    session.runtime.session.laps_completed = 3u;
    session.runtime.session.lap_started = 1u;
    session.runtime.session.elapsed_ticks = 600u;
    session.runtime.session.lap_origin_ticks = 0u;
    session.runtime.session.lap_ticks[0] = 100u;
    session.runtime.session.lap_ticks[1] = 200u;
    session.runtime.session.lap_ticks[2] = 300u;
    CHECK(portable_recovered_time_trial_ghost_candidate(
        &session, ghost_candidate));
    CHECK(portable_time_trial_ghost_record_time(ghost_candidate) == 600u &&
          portable_time_trial_ghost_record_course(ghost_candidate) == 33u &&
          portable_time_trial_ghost_record_driver(ghost_candidate) == 0u);

    /* Mode200 accepts a fixed finish camera, with no GP orbit/celebration. */
    {
        const PortableFinishRow rows[]={{24u,1u,1u},{28u,2u,1u}};
        PortableFinishRules finish={.camera={1024u,32768u,80u,220u,3u}};
        for(unsigned i=0u;i<8u;++i)finish.schedules[i]=(PortableFinishSchedule){2u,rows};
        session.finish_rules=&finish;session.driver.hop.mode=0u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(session.finish_camera.phase==10u && session.finish_camera.snapshot.active &&
            !session.finish_camera.snapshot.request_celebration && (session.driver.steering.state_flags&0x20u));
        const PortableFinishCamera frozen=session.finish_camera;
        CHECK(portable_recovered_time_trial_set_paused(&session,1)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(memcmp(&frozen,&session.finish_camera,sizeof frozen)==0);
        CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
        CHECK(!session.finish_camera.phase && !session.finish_camera.snapshot.active);
    }

    PortableLavaBubble initial_bubble;
    CHECK(portable_lava_bubble_init(&initial_bubble,100*65536,200*65536,1u,1u));
    race.lava_bubbles=&initial_bubble;race.lava_bubble_count=1u;
    PortableThwomp initial_thwomp;
    CHECK(portable_thwomp_init(&initial_thwomp,300*65536,400*65536,2u,6u));
    race.thwomps=&initial_thwomp;race.thwomp_count=1u;
    PortableCrab initial_crab;
    CHECK(portable_crab_init(&initial_crab,500*65536,600*65536,2u,0x8001u));
    race.crabs=&initial_crab;race.crab_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.lava_bubbles.actors[0].timer==99&&initial_bubble.timer==100);
    CHECK(session.thwomps.actors[0].timer==31 && initial_thwomp.timer==32);
    CHECK(session.crabs.actors[0].y_q16==600*65536+0x8000 && initial_crab.timer==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.lava_bubbles.actors[0].timer==99);
    CHECK(session.thwomps.actors[0].timer==31);
    CHECK(session.crabs.actors[0].timer==1);
    CHECK(portable_recovered_time_trial_restart(&session)==PORTABLE_RECOVERED_TIME_TRIAL_OK);
    CHECK(session.lava_bubbles.actors[0].timer==100);
    CHECK(session.thwomps.actors[0].timer==32);
    CHECK(session.crabs.actors[0].timer==0 && session.crabs.actors[0].y_q16==600*65536);
    /* Real session dispatch: physical height/filter/guard rejection, ordinary
     * queued hit, pause, grounded consumption and protected bubble removal. */
    memset(terrain_cells,0,sizeof terrain_cells);
    material_flags[0]=0x40u;
    initial_bubble.x_q16=100*65536;initial_bubble.y_q16=100*65536;
    initial_bubble.phase=3u;initial_bubble.flags=3u;initial_bubble.timer=10;
    initial_bubble.checkpoint=2u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.contact_guard_66=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!(session.driver.forward_acceleration.state_flags&0x1000u));
    session.driver.contact_guard_66=0u;
    session.lava_bubbles.actors[0].height_q16=20*65536;
    session.lava_bubbles.actors[0].phase=6u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!(session.driver.forward_acceleration.state_flags&0x1000u));
    session.lava_bubbles.actors[0].height_q16=0;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!(session.driver.forward_acceleration.state_flags&0x1000u) &&
          session.driver.steering.response_status==6u &&
          session.driver.normal_collision_timer_38==7u &&
          session.driver.contact_audio.song_id==0x179u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0x8000u,&disposition)==0);
    CHECK(memcmp(&unchanged,&session,sizeof session)==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!(session.driver.forward_acceleration.state_flags&0x1000u) &&
          session.driver.steering.response_status==6u &&
          session.driver.steering.drift_angle==60u);
    /* Continued contact at timer1 pushes away without requeueing FF80. */
    session.driver.normal_collision_timer_38=1u;
    const uint32_t contact_serial=session.driver.contact_audio.serial;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.normal_collision_timer_38==7u &&
          !(session.driver.forward_acceleration.state_flags&0x1000u) &&
          session.driver.contact_audio.serial==contact_serial);
    session.driver.normal_collision_timer_38=0u;
    session.driver.invulnerability_timer_6a=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.lava_bubbles.actors[0].phase==5u &&
          session.lava_bubbles.actors[0].velocity_z_q16==0x2c000 &&
          !(session.driver.forward_acceleration.state_flags&0x1000u));
    /* Thwomp crush goes through the real checkpoint/height dispatch and
     * holds the zero components for128 ticks; pause/restart preserve ownership. */
    race.lava_bubble_count=0u;
    CHECK(portable_thwomp_init(&initial_thwomp,100*65536,100*65536,2u,0u));
    initial_thwomp.state=5u;initial_thwomp.timer=2;initial_thwomp.height_q16=8*65536;
    initial_thwomp.velocity_z_q16=-0x40000;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!session.driver.flattened_64 && session.thwomps.actors[0].height_q16==4*65536);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.flattened_64 && session.driver.normal_collision_timer_38==0x6fffu);
    CHECK(session.driver.contact_audio.song_id==169u && session.driver.contact_audio.stop_song==164u);
    CHECK(session.driver.motion.component_x==0 && session.driver.pose.component_y==0);
    CHECK(session.thwomp_sounds.count==1u && session.thwomp_sounds.volumes[0]==256u);
    CHECK(session.impact_sounds.count==1u && session.impact_sounds.events[0].volume==256u &&
        !session.impact_sounds.events[0].rival && session.impact_sounds.events[0].variant==0u);
    CHECK(portable_recovered_time_trial_snapshot(&session,&first)==0 && first.flattened);
    CHECK(first.thwomp_sounds.count==1u && first.thwomp_sounds.volumes[0]==256u);
    CHECK(first.impact_sounds.count==1u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0x8000u,&disposition)==0);
    CHECK(memcmp(&unchanged,&session,sizeof session)==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.thwomps.actors[0].flags=0u;
    const int32_t crushed_x=session.driver.pose.position_x,crushed_y=session.driver.pose.position_y;
    for(unsigned tick=0;tick<126u;++tick){
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.flattened_64);
        CHECK(session.driver.pose.position_x==crushed_x && session.driver.pose.position_y==crushed_y);
    }
    CHECK(session.driver.normal_collision_timer_38==0x6f81u);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!session.driver.flattened_64 && session.driver.normal_collision_timer_38==0u &&
          (session.driver.steering.state_flags&0x4000u));
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!(session.driver.steering.state_flags&0x4000u));
    {
        /* 805B3DC: normal size returns (133) 112 ticks after the release. */
        const uint32_t serial=session.driver.contact_audio.serial;
        for(unsigned tick=2u;tick<112u;++tick)
            CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.contact_audio.serial==serial);
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.contact_audio.serial==serial+1u&&
              session.driver.contact_audio.song_id==133u);
    }
    CHECK(portable_recovered_time_trial_restart(&session)==0 && !session.driver.flattened_64);
    /* A protected driver launches the block; ground contact only rebounds. */
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    session.thwomps.actors[0].height_q16=4*65536;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!session.driver.flattened_64 && session.thwomps.actors[0].state==7u &&
          session.thwomps.actors[0].velocity_z_q16==0x20000 &&
          !(session.thwomps.actors[0].flags&2u));
    /* Rebound changes this tick's integrated pose, not the next one. */
    CHECK(portable_thwomp_init(&initial_thwomp,100*65536,100*65536,2u,0u));
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.motion.component_x=512;
    session.driver.pose.component_x=512;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.pose.position_x==99*65536 &&
          session.driver.normal_collision_timer_38==7u && !session.driver.flattened_64);
    /* Entering the box during integration cannot hit until the next pass. */
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.pose.position_x=108*65536;
    session.driver.motion.component_x=-512;
    session.driver.pose.component_x=-512;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.pose.position_x==106*65536 &&
          session.driver.normal_collision_timer_38==0u);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.normal_collision_timer_38==7u);
    /* Protected callbacks consume the entry velocity and their flight moves
     * in the later passive-actor pass of this same tick. */
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    session.driver.motion.component_x=512;
    session.driver.pose.component_x=512;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.thwomps.actors[0].x_q16==103*65536 &&
          session.thwomps.actors[0].velocity_x_q16==3*65536 &&
          session.thwomps.actors[0].height_q16==0x24000 &&
          session.thwomps.actors[0].velocity_z_q16==0x20000);
    initial_thwomp.state=5u;initial_thwomp.timer=2;
    initial_thwomp.velocity_z_q16=-0x40000;
    /* Passive prepend order crosses actor families. The first ordinary hit
     * raises the live kart filter and prevents a second hit in the same tick. */
    race.lava_bubble_count=1u;
    initial_thwomp.height_q16=4*65536;
    initial_bubble.phase=6u;initial_bubble.height_q16=0;
    initial_bubble.placement_order=8u;initial_thwomp.placement_order=9u;
    for(unsigned bubble_first=0u;bubble_first<2u;++bubble_first){
        initial_bubble.placement_order=bubble_first?10u:8u;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.flattened_64==!bubble_first);
        CHECK(session.driver.contact_audio.song_id==(bubble_first?377u:169u));
        CHECK(!(session.driver.forward_acceleration.state_flags&0x1000u));
        CHECK(!bubble_first || session.driver.steering.response_status==6u);
    }
    /* Crab hits enter spin on the contact tick; protection flips the crab
     * before its passive update, without moving it along the patrol. */
    race.lava_bubble_count=0u;race.thwomp_count=0u;
    CHECK(portable_crab_init(&initial_crab,100*65536,100*65536,2u,0u));
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.steering.response_status==6u &&
          session.driver.normal_collision_timer_38==7u &&
          session.driver.contact_audio.song_id==373u);
    CHECK(session.crabs.actors[0].x_q16==100*65536+0x8000 &&
          session.crabs.actors[0].flags==3u);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.crabs.actors[0].flags==1u && session.crabs.actors[0].flip_y &&
          session.crabs.actors[0].timer==31 &&
          session.crabs.actors[0].x_q16==100*65536 &&
          session.driver.contact_audio.song_id==160u);
    PortableRecoveredTimeTrialSnapshot crab_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&crab_snapshot)==0);
    CHECK(crab_snapshot.crabs.actors[0].flip_y && crab_snapshot.crabs.actors[0].timer==31);
    PortableSeagull initial_seagull;
    CHECK(portable_seagull_init(&initial_seagull,100*65536,100*65536,2u,0u));
    race.crab_count=0u;race.seagulls=&initial_seagull;race.seagull_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.seagulls.actors[0].state==17 && session.seagulls.actors[0].timer==1 &&
          session.seagulls.actors[0].art==1u && session.driver.steering.response_status!=6u &&
          session.driver.normal_collision_timer_38==0u && session.driver.contact_audio.serial==1u &&
          session.driver.contact_audio.song_id==385u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
          memcmp(&session,&unchanged,sizeof session)==0);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.seagulls.actors[0].state==0 && session.seagulls.actors[0].affine_x_q8==224u);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.seagulls.actors[0].state==241 && session.seagulls.actors[0].height_q16==0x30000 &&
          session.seagulls.actors[0].flip_y && session.driver.contact_audio.song_id==160u);
    PortablePiranha initial_piranha;
    CHECK(portable_piranha_init(&initial_piranha,100*65536,100*65536,2u,0u));
    race.seagull_count=0u;race.piranhas=&initial_piranha;race.piranha_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.piranhas.actors[0].state==17 && session.piranhas.actors[0].timer==3 &&
          session.piranhas.actors[0].flags==1u && session.driver.steering.response_status==0u &&
          session.driver.normal_collision_timer_38==7u && session.driver.contact_audio.serial==0u &&
          session.piranha_sounds.count==1u && session.piranha_sounds.songs[0]==373u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
          memcmp(&session,&unchanged,sizeof session)==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    for(unsigned tick=2u;tick<=9u;++tick){
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.piranha_sounds.count==(tick==9u?1u:0u));
    }
    CHECK(session.piranha_sounds.songs[0]==378u);
    PortableRecoveredTimeTrialSnapshot plant_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&plant_snapshot)==0 &&
          plant_snapshot.piranhas.actors[0].art==2u && plant_snapshot.piranha_sounds.count==1u);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.piranhas.actors[0].state==0 && session.piranhas.actors[0].flags==2u &&
          session.piranha_sounds.serial==0u && initial_piranha.state==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.piranhas.actors[0].state==241 && session.piranhas.actors[0].height_q16==0x30000 &&
          session.piranhas.actors[0].flags==0u && session.driver.contact_audio.song_id==160u &&
          session.piranha_sounds.count==1u && session.piranha_sounds.songs[0]==373u);
    race.piranha_count=0u;
    PortableMouse initial_mouse;
    CHECK(portable_mouse_init(&initial_mouse,100*65536,100*65536,2u,0u));
    race.mice=&initial_mouse;race.mouse_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.contact_audio.song_id==373u && session.driver.normal_collision_timer_38==7u);
    CHECK(session.driver.steering.response_status==6u && !(session.driver.forward_acceleration.state_flags&0x1000u));
    CHECK(session.mice.actors[0].x_q16==initial_mouse.x_q16+32768 && session.mice.actors[0].timer==1);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 && !memcmp(&session,&unchanged,sizeof session));
    CHECK(portable_recovered_time_trial_restart(&session)==0 && session.mice.actors[0].timer==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.mice.actors[0].flags==1u && session.mice.actors[0].timer==31 &&
        session.mice.actors[0].affine_y_q8==-256 && session.driver.contact_audio.song_id==160u);
    PortableRecoveredTimeTrialSnapshot mouse_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&mouse_snapshot)==0 && mouse_snapshot.mice.count==1u &&
        mouse_snapshot.mice.actors[0].timer==31 && initial_mouse.flags==3u);
    race.mouse_count=0u;race.mice=NULL;
    PortableFallingStar initial_star;
    CHECK(portable_falling_star_init(&initial_star,100*65536,100*65536,2u,0u));
    initial_star.state=5u;initial_star.timer=1;initial_star.flags=3u;
    race.falling_stars=&initial_star;race.falling_star_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    /* E490 gives the selected8000 player the short1000 spin rate. */
    CHECK(session.driver.contact_audio.song_id==373u && !session.driver.lightning_timer_68 &&
        session.impact_sounds.count==1u && session.driver.strong_spin_rate_b6==0x1000u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 && !memcmp(&session,&unchanged,sizeof session));
    CHECK(portable_recovered_time_trial_restart(&session)==0 && session.falling_stars.actors[0].timer==1);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.contact_audio.song_id==160u && !session.impact_sounds.count &&
        session.falling_stars.actors[0].flags==3u && session.falling_stars.actors[0].timer==0);
    PortableRecoveredTimeTrialSnapshot star_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&star_snapshot)==0 && star_snapshot.falling_stars.count==1u);
    race.falling_stars=NULL;race.falling_star_count=0u;
    PortableThundercloud initial_cloud;
    CHECK(portable_thundercloud_init(&initial_cloud,100*65536,100*65536,2u,0u));
    initial_cloud.flags=3u;initial_cloud.state=17;
    race.thunderclouds=&initial_cloud;race.thundercloud_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.contact_audio.song_id==374u && session.driver.lightning_timer_68==359 &&
        session.impact_sounds.count==1u && session.driver.strong_spin_rate_b6==0x1000u &&
        !(session.driver.item_cue_flags_58&0x10u));
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 && !memcmp(&session,&unchanged,sizeof session));
    CHECK(portable_recovered_time_trial_restart(&session)==0 && !session.driver.lightning_timer_68 &&
        session.thunderclouds.palette_phase[0]==0u);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!session.driver.lightning_timer_68 && session.driver.contact_audio.song_id==160u &&
        !session.impact_sounds.count && initial_cloud.flags==3u);
    session.driver.lightning_timer_68=1;session.thunderclouds.actors[0].flags=1u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(!session.driver.lightning_timer_68 && session.driver.contact_audio.song_id==133u &&
        (session.driver.item_cue_flags_58&0x10u));
    PortableRecoveredTimeTrialSnapshot cloud_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&cloud_snapshot)==0 && cloud_snapshot.thunderclouds.count==1u);
    race.thundercloud_count=0u;race.thunderclouds=NULL;
    PortableSnowman initial_snowman;
    CHECK(portable_snowman_init(&initial_snowman,100*65536,100*65536,2u,0u));
    race.snowmen=&initial_snowman;race.snowman_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.contact_anchor_x_ee=99;session.driver.contact_anchor_y_f0=100;
    sine[768]=-32768;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.snowmen.actors[0].state==33 && session.snowmen.actors[0].timer==0 &&
          session.snowmen.actors[0].flags==3u && session.driver.normal_collision_timer_38==7u &&
          session.driver.contact_audio.song_id==373u && session.driver.steering.response_status==0u);
    CHECK(!(session.driver.forward_acceleration.state_flags&0x1300u));
    CHECK(session.driver.pose.position_x==99*65536 &&
          session.driver.pose.position_y==100*65536);
    sine[768]=0;
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
          memcmp(&session,&unchanged,sizeof session)==0);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.snowmen.actors[0].state==17 && session.snowmen.actors[0].flags==3u && initial_snowman.state==17);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=10u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.snowmen.actors[0].state==33 && session.driver.contact_audio.song_id==160u &&
          session.driver.normal_collision_timer_38==0u);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.snowmen.actors[0].state==49 && !(session.snowmen.actors[0].flags&2u));
    PortableRecoveredTimeTrialSnapshot snowman_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&snowman_snapshot)==0 &&
          snowman_snapshot.snowmen.actors[0].art==1u && snowman_snapshot.snowmen.actors[0].state==49);
    race.snowman_count=0u;
    PortablePenguin initial_penguin;
    CHECK(portable_penguin_init(&initial_penguin,100*65536,100*65536,2u,1u));
    race.penguins=&initial_penguin;race.penguin_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.penguins.actors[0].timer==1 && session.penguins.actors[0].flags==3u &&
          session.driver.steering.response_status==6u && session.driver.normal_collision_timer_38==7u &&
          session.driver.contact_audio.song_id==373u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
          memcmp(&session,&unchanged,sizeof session)==0);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.penguins.actors[0].timer==0 && session.penguins.actors[0].flags==3u && initial_penguin.timer==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    session.driver.invulnerability_timer_6a=1u;
    sine[512]=123;sine[496]=456;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.penguins.actors[0].timer==31 && session.penguins.actors[0].height_q16==123*48 &&
          session.penguins.actors[0].affine_y_q8==-256 && session.penguins.actors[0].flags==1u &&
          session.driver.contact_audio.song_id==160u && session.driver.normal_collision_timer_38==0u);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.penguins.actors[0].timer==30 && session.penguins.actors[0].height_q16==456*48);
    PortableRecoveredTimeTrialSnapshot penguin_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&penguin_snapshot)==0 &&
          penguin_snapshot.penguins.actors[0].timer==30 && penguin_snapshot.penguins.actors[0].affine_y_q8==-256);
    sine[512]=0;sine[496]=0;
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    /* Explicit CPU identity exercises FF80's2000 branch and long spin. */
    session.driver.steering.state_flags=(session.driver.steering.state_flags&~0x8000u)|0x2000u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.driver.steering.response_status==13u && session.driver.strong_spin_rate_b6==0x2000u &&
          session.penguins.actors[0].flags==3u);
    race.penguin_count=0u;
    PortableBoo initial_boo;
    /* Keep the contact fixture below TT's A8-288 drift gate. A zero
     * dynamic target otherwise arms status1 independently of Boo contact. */
    race.initial_driver.preserve_source_base_speed=1u;
    race.initial_driver.input.base_speed_q16=1000*65536;
    CHECK(portable_boo_init(&initial_boo,800*65536,500*65536,2u,0u));
    race.boos=&initial_boo;race.boo_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.boos.actors[0].state==17u && session.boos.actors[0].phase==1024u &&
          session.boos.actors[0].height_q16==0x80000 && initial_boo.phase==0u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
          memcmp(&session,&unchanged,sizeof session)==0);
    PortableRecoveredTimeTrialSnapshot boo_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&boo_snapshot)==0 &&
          boo_snapshot.boos.actors[0].phase==1024u);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.boos.actors[0].state==16u && session.boos.actors[0].height_q16==0 &&
          session.boos.actors[0].phase==0u);
    /* Contact5704C is independent of the guarded 0128 audio branch. */
    initial_boo.x_q16=100*65536;initial_boo.y_q16=100*65536;
    for(unsigned kind=0;kind<5u;++kind){
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.driver.contact_guard_66=kind==1u?1u:0u;
        session.driver.invulnerability_timer_6a=kind==2u?1u:0u;
        session.driver.normal_collision_timer_38=kind==3u?2u:kind==4u?1u:0u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.palette_bank==(kind==3u?0u:kind==2u?1u:7u));
        CHECK(session.driver.normal_collision_timer_38==(kind==3u?1u:0u));
        CHECK(session.driver.steering.response_status==0u && session.driver.vector_effect.contact_word_5a==0u);
        CHECK(session.driver.palette_requests_194==(kind==2u?0x8000u:0u));
        CHECK(session.driver.contact_audio.song_id==((kind==0u || kind==4u)?380u:0u));
        if(kind==0u)CHECK(session.driver.contact_audio.volume==256u &&
            session.driver.contact_audio.pitch==0 && !session.driver.contact_audio.restart);
        CHECK(portable_recovered_time_trial_snapshot(&session,&boo_snapshot)==0 &&
              boo_snapshot.palette_bank==session.driver.palette_bank);
        session.driver.pose.position_x=800*65536;
        session.driver.invulnerability_timer_6a=0u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.palette_bank==(kind==2u?7u:0u));
        CHECK(portable_recovered_time_trial_restart(&session)==0 &&
              session.driver.palette_requests_194==0u && session.driver.palette_bank==0u);
    }
    /* 56EE8 uses independent planar36/vertical64 extents (kart extent10). */
    for(unsigned axis=0;axis<3u;++axis){
        initial_boo.x_q16=(axis==0u?112:100)*65536;
        initial_boo.y_q16=(axis==1u?112:100)*65536;
        initial_boo.height_q16=axis==2u?19*65536:0;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
              session.driver.palette_bank==0u && session.driver.contact_audio.song_id==0u);
    }
    race.boo_count=0u;
    PortableCannonball initial_cannonball;
    CHECK(portable_cannonball_init(&initial_cannonball,800*65536,500*65536,2u,0u));
    race.seagull_count=0u;race.cannonballs=&initial_cannonball;race.cannonball_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.cannonballs.actors[0].state==3 && session.cannonballs.actors[0].height_q16==0x27f800);
    PortableRecoveredTimeTrialSnapshot cannonball_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&cannonball_snapshot)==0);
    CHECK(cannonball_snapshot.cannonballs.actors[0].affine_x_q8==1000u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
          memcmp(&session,&unchanged,sizeof session)==0);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(session.cannonballs.actors[0].state==1 && session.cannonballs.actors[0].timer==0 &&
          session.cannonballs.actors[0].flags==0u && initial_cannonball.state==1);
    /* A falling ball contacts entry poses; its floor transition disables the
     * next collision. The held driver must finish the strong spin unaided. */
    for(unsigned protected_hit=0;protected_hit<2u;++protected_hit)
    for(unsigned direction=0;direction<2u;++direction)
    {
        CHECK(portable_cannonball_init(&initial_cannonball,100*65536,100*65536,2u,0u));
        initial_cannonball.state=3;initial_cannonball.timer=999;
        initial_cannonball.flags=3u;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.scene_clock=direction;
        session.driver.motion.speed_q16=0x00800000;
        session.driver.motion.acceleration_q16=0x1234;
        session.driver.invulnerability_timer_6a=(uint16_t)protected_hit;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.cannonballs.actors[0].state==4 &&
              session.driver.contact_audio.song_id==(protected_hit?160u:170u));
        if(protected_hit){
            CHECK(session.driver.steering.response_status!=13u &&
                  session.driver.strong_spin_rate_b6==0u);
            continue;
        }
        CHECK(session.driver.steering.response_status==13u &&
              session.driver.steering.driver_mode==10u &&
              session.driver.strong_spin_rate_b6==0x1000u &&
              session.driver.steering.position_82==0u &&
              session.driver.normal_collision_timer_38==7u &&
              !!(session.driver.steering.response_flags&0x100u)==!direction);
        unsigned ticks=0u;int decelerated=0;
        while(session.driver.steering.response_status==13u && ticks++<180u){
            CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
            if(session.driver.motion.acceleration_q16<0){
                CHECK(session.driver.motion.acceleration_q16==-0x00380000+0x1234);
                decelerated=1;
            }
        }
        CHECK(ticks<180u && session.driver.steering.response_status==0u &&
              session.driver.steering.driver_mode==0u &&
              session.driver.strong_spin_rate_b6==192u &&
              session.driver.steering.position_82==0u);
        CHECK(decelerated && session.driver.motion.speed_q16==0);
        CHECK(portable_recovered_time_trial_restart(&session)==0 &&
              session.scene_clock==0u && session.driver.strong_spin_rate_b6==0u);
    }
    /* Timer-one escape pushes away without damage; an explosion is absent
     * from the passive collision list even when it overlaps the kart. */
    for(unsigned explosion=0;explosion<2u;++explosion){
        initial_cannonball.state=explosion?5:3;
        initial_cannonball.timer=explosion?20:999;
        initial_cannonball.flags=explosion?1u:3u;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.driver.normal_collision_timer_38=explosion?0u:1u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.strong_spin_rate_b6==0u &&
              session.driver.steering.response_status!=13u &&
              session.driver.contact_audio.serial==0u);
    }
    /* Sound requests follow motion transitions and the native listener.
     * They remain presentation data, freeze while paused and reset on restart. */
    sine[256]=32767;
    CHECK(portable_cannonball_init(&initial_cannonball,100*65536,0,2u,0u));
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.cannonball_sounds.count==1u &&
          session.cannonball_sounds.events[0].song_id==382u &&
          session.cannonball_sounds.events[0].volume==247u);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    unchanged=session;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
          memcmp(&session,&unchanged,sizeof session)==0);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    for(unsigned tick=1;tick<52u;++tick)
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.cannonball_sounds.count==1u &&
          session.cannonball_sounds.events[0].song_id==383u);
    CHECK(portable_recovered_time_trial_snapshot(&session,&first)==0 &&
          first.cannonball_sounds.serial==session.cannonball_sounds.serial &&
          first.cannonball_sounds.events[0].song_id==383u);
    CHECK(portable_recovered_time_trial_restart(&session)==0 &&
          session.cannonball_sounds.serial==0u && session.cannonball_sounds.count==0u);
    /* Both passive common families reach 0128/59E44 from an ordinary TT tick. */
    race.cannonball_count=0u;
    const unsigned passive_families[]={8u,9u,0xa1u,0xa0u};
    for(unsigned fi=0;fi<4u;++fi){
        const unsigned family=passive_families[fi];
        PortableCourseProp passive;
        CHECK(portable_course_prop_init(&passive,race.initial_driver.pose.position_x,
            race.initial_driver.pose.position_y,family<<16u,256u,0u));
        passive.checkpoint=race.initial_driver.source_route_index_90;
        race.course_props=&passive;race.course_prop_count=1u;
        for(unsigned protected=0u;protected<2u;++protected){
            CHECK(portable_recovered_time_trial_init(&session,&race)==0);
            session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
            session.runtime.session.countdown_remaining=0u;
            session.driver.invulnerability_timer_6a=(uint16_t)protected;
            CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
            CHECK(session.driver.contact_audio.song_id==(protected?160u:family==0xa0u?0u:169u));
            CHECK(session.driver.normal_collision_timer_38==(protected?0u:7u));
            if(family==0xa0u){
                CHECK(session.course_props.actors[0].state==(protected?6u:2u));
                CHECK(session.course_props.actors[0].timer==(protected?0:64));
            }else CHECK(session.course_props.actors[0].state==protected &&
                session.course_props.actors[0].timer==(protected?31:0));
        }
    }
    /* 59FAC: an unprotected touch vanishes the Broken Pier Boo, spoils the
     * ghost and starts 4886C's haunt; a protected touch flings it intact. */
    for(unsigned protected=0u;protected<2u;++protected){
        PortableCourseProp pier;
        CHECK(portable_course_prop_init(&pier,race.initial_driver.pose.position_x,
            race.initial_driver.pose.position_y,0xa30000u,256u,0u));
        pier.checkpoint=race.initial_driver.source_route_index_90;
        race.course_props=&pier;race.course_prop_count=1u;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.driver.invulnerability_timer_6a=(uint16_t)protected;
        CHECK(session.ghost_recording_valid==1u);
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.contact_audio.song_id==(protected?160u:380u));
        CHECK(session.course_props.actors[0].state==(protected?6u:8u));
        CHECK(session.course_props.actors[0].flags==(protected?1u:0u));
        CHECK(session.ghost_recording_valid==protected);
        CHECK(session.haunts[0].occupied==!protected);
        session.driver.invulnerability_timer_6a=0u;
        unsigned locked=0u;
        for(unsigned tick=0u;tick<80u && !locked;++tick){
            CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
            locked=session.driver.item_lock_ac!=0u;
        }
        CHECK(locked==(unsigned)!protected);
        CHECK(session.haunt_music==!protected);
        PortableRecoveredTimeTrialSnapshot haunt_snapshot;
        CHECK(portable_recovered_time_trial_snapshot(&session,&haunt_snapshot)==0);
        CHECK(haunt_snapshot.boo_attacks[0].occupied==!protected && haunt_snapshot.haunt_music_active==!protected);
        if(!protected){
            /* 7B30: each fresh accelerator press removes ten lock ticks.
             * The locked speed cap reads the source angle-target row8F. */
            static const uint8_t angle_targets[0x90]={0};
            PortableDriverPilotDependencies locked_deps=*session.runtime.recovered_dependencies;
            locked_deps.source_angle_target_table=angle_targets;
            locked_deps.source_angle_target_table_count=sizeof angle_targets;
            session.runtime.recovered_dependencies=&locked_deps;
            const uint16_t before=session.driver.item_lock_ac;
            CHECK(portable_recovered_time_trial_tick_source(&session,0x8000u,&disposition)==0);
            CHECK(session.driver.item_lock_ac==before-10u);
            CHECK(portable_recovered_time_trial_tick_source(&session,0x8000u,&disposition)==0);
            CHECK(session.driver.item_lock_ac==before-10u);
            CHECK(portable_recovered_time_trial_restart(&session)==0);
            CHECK(!session.haunts[0].occupied && !session.haunt_music && session.ghost_recording_valid);
        }
    }
    /* 58B4C/58BA8: a touch sounds 371, spoils the ghost and seats the Shy
     * Guy, which holds the speed lock; a protected kart flings it (372). */
    for(unsigned protected=0u;protected<2u;++protected){
        PortableCourseProp teepee;
        CHECK(portable_course_prop_init(&teepee,race.initial_driver.pose.position_x,
            race.initial_driver.pose.position_y,0x030000u,256u,0u));
        teepee.checkpoint=race.initial_driver.source_route_index_90;
        race.course_props=&teepee;race.course_prop_count=1u;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.driver.invulnerability_timer_6a=(uint16_t)protected;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.ghost_recording_valid==0u);
        CHECK(session.driver.contact_audio.song_id==(protected?372u:371u));
        CHECK(session.course_props.actors[0].state==2u && session.course_props.actors[0].flags==1u);
        CHECK(session.course_props.actors[0].teepee.rider_state==(protected?2u:1u));
        CHECK(session.driver.item_lock_ac==(protected?0u:360u));
        PortableRecoveredTimeTrialSnapshot teepee_snapshot;
        CHECK(portable_recovered_time_trial_snapshot(&session,&teepee_snapshot)==0);
        CHECK(teepee_snapshot.course_props.actors[0].teepee.rider_state==(protected?2u:1u));
        CHECK(portable_recovered_time_trial_restart(&session)==0);
        CHECK(!session.course_props.actors[0].teepee.rider_state && session.course_props.actors[0].state==0u);
    }
    /* 0128 treats a landed 101D rock like a lava bubble: sound377, bump. */
    {
        PortableCourseProp rock;
        CHECK(portable_course_prop_init(&rock,race.initial_driver.pose.position_x,
            race.initial_driver.pose.position_y,0xa20000u,224u,0u));
        rock.checkpoint=race.initial_driver.source_route_index_90;
        race.course_props=&rock;race.course_prop_count=1u;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        PortableCourseProp *live=&session.course_props.actors[0];
        live->state=11u;live->flags=3u;live->lake_rock.timer=50;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.contact_audio.song_id==377u && session.ghost_recording_valid==1u);
        CHECK(session.driver.normal_collision_timer_38==7u);
        CHECK(live->state==11u && live->flags==3u && live->lake_rock.timer==49);
        /* D214(3)/F2C38: bank5 for 240 ticks, bank6 for ten, then none. */
        CHECK(session.driver.burn_ticks_72==(uint16_t)(PORTABLE_DRIVER_BURN_TICKS-1u) && session.driver.palette_bank==5u);
        uint16_t requests=0u,ticks=11u;
        portable_driver_palette_burn(&ticks,&requests);
        CHECK(requests==0x2000u && ticks==10u);
        requests=0u;portable_driver_palette_burn(&ticks,&requests);
        CHECK(requests==0x4000u && ticks==9u);
        ticks=0u;requests=0u;portable_driver_palette_burn(&ticks,&requests);
        CHECK(!requests);
        /* 805C354: the local player smokes; its first puff cues song183. */
        CHECK(session.lava_smoke.emitter_ticks==PORTABLE_LAVA_SMOKE_TICKS-1u &&
            session.lava_smoke.puffs[0].age==1u && session.piranha_sounds.count==1u &&
            session.piranha_sounds.songs[0]==183u);
        PortableRecoveredTimeTrialSnapshot smoke_snapshot;
        CHECK(portable_recovered_time_trial_snapshot(&session,&smoke_snapshot)==0);
        CHECK(smoke_snapshot.lava_smoke.puffs[0].age==1u);
        PortableLavaSmoke smoke={0};
        portable_lava_smoke_start(&smoke);
        unsigned puffs=0u,live_max=0u;
        const uint8_t emit_clocks[]={0u,5u,14u,27u,44u,49u,58u,71u,88u,
            93u,102u,115u,132u,137u,146u,159u,176u};
        for(uint32_t clock=0u;clock<400u;++clock){
            const int emitted=portable_lava_smoke_tick(&smoke,clock);
            CHECK(emitted==(puffs<sizeof emit_clocks && clock==emit_clocks[puffs]));
            puffs+=(unsigned)emitted;
            unsigned live_puffs=0u;
            for(unsigned i=0u;i<PORTABLE_LAVA_SMOKE_PUFFS;++i){
                const PortableLavaSmokePuff *puff=&smoke.puffs[i];
                CHECK(puff->age<=PORTABLE_LAVA_SMOKE_PUFF_AGE && (!puff->age ||
                    (puff->dx>=-16 && puff->dx<=15 && puff->dy>=-16 && puff->dy<=-2 && !(puff->dy&1))));
                if(emitted && puff->age==1u)
                    CHECK(puff->dx==(int)(clock&31u)-16 && puff->dy==-16+(int)(clock&7u)*2);
                live_puffs+=puff->age!=0u;
            }
            if(live_puffs>live_max)live_max=live_puffs;
        }
        /* B44C includes each row's load tick; wait decrements return early. */
        CHECK(puffs==sizeof emit_clocks && live_max==2u && !smoke.emitter_ticks);
        for(unsigned i=0u;i<PORTABLE_LAVA_SMOKE_PUFFS;++i)CHECK(!smoke.puffs[i].age);
        /* Ordinary original BC1 TT C2E4 calls at clocks2969 and2978:
         * waits8/12 and offsets(9,-14)/(-14,-12), with no RNG writes. */
        memset(&smoke,0,sizeof smoke);portable_lava_smoke_start(&smoke);
        CHECK(portable_lava_smoke_tick(&smoke,2969u) && smoke.wait==8u &&
            smoke.puffs[0].dx==9 && smoke.puffs[0].dy==-14);
        for(uint32_t clock=2970u;clock<2978u;++clock)CHECK(!portable_lava_smoke_tick(&smoke,clock));
        CHECK(portable_lava_smoke_tick(&smoke,2978u) && smoke.wait==12u &&
            smoke.puffs[1].dx==-14 && smoke.puffs[1].dy==-12);
        memset(&smoke,0,sizeof smoke);portable_lava_smoke_start(&smoke);
        CHECK(!portable_lava_smoke_tick_eligible(&smoke,UINT32_MAX,0) && smoke.wait==16u);
        for(uint32_t clock=0u;clock<16u;++clock)CHECK(!portable_lava_smoke_tick(&smoke,clock));
        CHECK(portable_lava_smoke_tick(&smoke,16u) && smoke.puffs[0].dx==0 && smoke.puffs[0].dy==-16);
        smoke.emitter_ticks=0u;
        for(unsigned age=2u;age<=14u;++age){
            CHECK(!portable_lava_smoke_tick(&smoke,0u) && smoke.puffs[0].age==age);
        }
        CHECK(!portable_lava_smoke_tick(&smoke,0u) && !smoke.puffs[0].age);
        const PortableLavaSmoke digest_before=smoke;
        const uint64_t smoke_hash=portable_lava_smoke_digest(&smoke);
        CHECK(smoke_hash==portable_lava_smoke_digest(&smoke) &&
            !memcmp(&smoke,&digest_before,sizeof smoke));
        CHECK(!portable_lava_smoke_tick_eligible(&smoke,0u,0) && !smoke.visible);
        CHECK(smoke_hash!=portable_lava_smoke_digest(&smoke));
        PortableLavaSmoke changed=digest_before;changed.wait^=1u;
        CHECK(smoke_hash!=portable_lava_smoke_digest(&changed));
        changed=digest_before;changed.emitter_ticks^=1u;
        CHECK(smoke_hash!=portable_lava_smoke_digest(&changed));
        for(unsigned field=0u;field<3u;++field){
            changed=digest_before;
            if(field==0u)changed.puffs[3].dx=1;
            else if(field==1u)changed.puffs[3].dy=-1;
            else changed.puffs[3].age=1u;
            CHECK(smoke_hash!=portable_lava_smoke_digest(&changed));
        }

        race.course_props=NULL;race.course_prop_count=0u;
    }
    /* 58098 spoils the ghost on every unguarded snowman touch. */
    PortableSnowman snow;
    CHECK(portable_snowman_init(&snow,race.initial_driver.pose.position_x,
        race.initial_driver.pose.position_y,race.initial_driver.source_route_index_90,0u));
    race.course_props=NULL;race.course_prop_count=0u;
    race.snowmen=&snow;race.snowman_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.ghost_recording_valid==0u);
    race.snowmen=NULL;race.snowman_count=0u;
    /* Pause and restart own common props just like the other course actors. */
    PortableCourseProp initial_prop;
    CHECK(portable_course_prop_init(&initial_prop,12*65536,12*65536,0xc0000u,224u,0u));
    race.course_props=&initial_prop;race.course_prop_count=1u;
    CHECK(portable_recovered_time_trial_init(&session,&race)==0);
    session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
    session.runtime.session.countdown_remaining=0u;
    portable_course_prop_damage(&session.course_props.actors[0]);
    CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.course_props.actors[0].timer==32);
    CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
    CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
    CHECK(session.course_props.actors[0].timer==31);
    PortableRecoveredTimeTrialSnapshot prop_snapshot;
    CHECK(portable_recovered_time_trial_snapshot(&session,&prop_snapshot)==0);
    CHECK(prop_snapshot.course_props.actors[0].timer==31);
    CHECK(portable_recovered_time_trial_restart(&session)==0);
    CHECK(!session.course_props.actors[0].state && session.course_props.actors[0].affine_y_q8==224);
    /* The real TT tick owns affine resolution, pause, shrink expiry and
     * restart; rendering snapshots cannot advance the effect. Synthetic
     * coefficients deliberately differ from ROM data. */
    {
        const PortableDriverAffineRow landing[]={{250u,270u,2u},{256u,256u,1u}};
        const PortableDriverAffineRow expiry[]={{360u,370u,2u},{256u,256u,1u}};
        const PortableDriverAffineRow crush[]={{220u,380u,110u}};
        const PortableDriverAffineRow lift[]={{220u,380u,18u}};
        const PortableDriverAffineRow rebound[]={{220u,380u,112u},{270u,240u,1u},{256u,256u,1u}};
        const PortableDriverAffineRules affine_rules={.tables={
            {1u,2u,landing},{4u,2u,expiry},{5u,1u,crush},{6u,1u,lift},{7u,3u,rebound}}};
        race.course_props=NULL;race.course_prop_count=0u;
        const PortableDriverArtRules art_rules={
            .speed_spans={0u,12u,11u,10u,9u,8u,7u,6u,5u,4u,3u,2u,1u,1u,1u,1u},
            .refresh_masks={7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u,7u}};
        race.dependencies.art_motion_rules=&art_rules;
        const PortableSkyMotionRules sky_rules={.controller=PORTABLE_SKY_MOTION_CLOCK_MID,
            .yaw_shift={6u,6u,7u},.layer_mask=7u,.y_bias={31,32,33},
            .split_y=-1,.split_layer=0u,.layer_source={0u,1u,2u}};
        race.sky_motion_rules=&sky_rules;
        const uint8_t ambient_data[]={31u,0u};
        const PortableAmbientPatch ambient_patch={.target=PORTABLE_AMBIENT_OBJECT,
            .offset=19u,.count=1u};
        const PortableAmbientFrame ambient_frame={.duration=2u,.patch_count=1u};
        const PortableAmbientLane ambient_lane={.stage=PORTABLE_AMBIENT_POST_DRIVER,
            .write_policy=PORTABLE_AMBIENT_ON_ENTRY,.controller=PORTABLE_AMBIENT_TIMER,
            .frame_count=1u,.loop_frame=UINT16_MAX};
        const PortableAmbientRules ambient_rules={.lanes=&ambient_lane,.frames=&ambient_frame,
            .patches=&ambient_patch,.data=ambient_data,.data_size=sizeof ambient_data,
            .lane_count=1u,.frame_count=1u,.patch_count=1u};
        PortableAmbientSeed ambient_seed={0};ambient_seed.object[19]=100u;
        race.ambient_rules=&ambient_rules;race.ambient_seed=&ambient_seed;
        const PortableWeatherAssetView weather={.rules={.enabled=1u,.count=12u}};
        race.weather_profile=&weather;
        race.thwomp_count=0u;race.dependencies.affine_rules=&affine_rules;
        CHECK(portable_recovered_time_trial_init(&session,&race)==0);
        CHECK(session.snow_weather.enabled && !session.main_visual_rng_state);
        session.runtime.session.phase=PORTABLE_RACE_PHASE_RACING;
        session.runtime.session.countdown_remaining=0u;
        session.driver.lightning_timer_68=1;
        session.driver.burn_ticks_72=20u;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        uint32_t weather_rng=0u;
        for(unsigned draw=0;draw<4u;++draw)(void)portable_source_random(&weather_rng);
        CHECK(session.main_visual_rng_state==weather_rng && session.snow_weather.particles[0].active==1u &&
            session.snow_weather.particles[0].x_q16==20*65536 &&
            session.snow_weather.particles[0].y_q16==13*65536);
        CHECK(session.driver.affine.mode==4u && session.driver.affine.remaining==1u &&
              session.driver.affine.inverse_x_q8==360u && session.driver.burn_ticks_72==19u);
        CHECK(portable_recovered_time_trial_snapshot(&session,&first)==0 &&
              first.affine.inverse_y_q8==370u);
        CHECK(first.snow_weather.enabled && first.main_visual_rng_state==weather_rng &&
            first.snow_weather_digest==portable_snow_weather_digest(&session.snow_weather));
        CHECK(first.ambient.initialized && first.ambient.object[19]==31u &&
            first.ambient.object_dirty[19] && session.ambient.post_serial==1u &&
            first.ambient_digest==portable_ambient_digest(&session.ambient));
        CHECK(first.sky_motion.initialized && first.sky_motion.scroll_y[0]==31 &&
              session.sky_motion_rules==&sky_rules);
        CHECK(session.driver.art_motion.initialized && session.driver.art_motion.wheel_palette_valid &&
              session.driver.art_motion.wheel_palette_bank==15u &&
              first.art_motion.wheel_phase==session.driver.art_motion.wheel_phase &&
              first.art_motion.cached_bob_offset==session.driver.art_motion.cached_bob_offset);
        unchanged=session;
        CHECK(portable_recovered_time_trial_snapshot(&session,&second)==0 &&
              memcmp(&unchanged,&session,sizeof session)==0);
        CHECK(portable_recovered_time_trial_set_paused(&session,1)==0);
        unchanged=session;
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0 &&
              memcmp(&unchanged,&session,sizeof session)==0);
        CHECK(portable_recovered_time_trial_set_paused(&session,0)==0);
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.affine.mode==4u && session.driver.affine.row==1u);
        CHECK(portable_recovered_time_trial_tick_source(&session,0u,&disposition)==0);
        CHECK(session.driver.affine.mode==0u && session.driver.affine.inverse_y_q8==256u);
        CHECK(portable_recovered_time_trial_restart(&session)==0 && !session.driver.affine.mode &&
              !session.driver.affine.pending_mode && !session.driver.affine.inverse_x_q8 &&
              !session.driver.art_motion.initialized && !session.driver.art_motion.wheel_palette_valid);
        CHECK(session.sky_motion.snapshot.initialized && session.sky_motion.elapsed==0);
        CHECK(session.main_visual_rng_state==0u && session.snow_weather.enabled &&
            !session.snow_weather.particles[0].active && !session.snow_weather.particles[0].phase &&
            session.snow_weather.particles[11].phase==11u);
        CHECK(session.ambient.snapshot.initialized && !session.ambient.post_serial &&
            session.ambient.snapshot.object[19]==100u && !session.ambient.snapshot.object_dirty[19]);
        race.ambient_rules=NULL;race.ambient_seed=NULL;race.weather_profile=NULL;
        race.sky_motion_rules=NULL;
        race.dependencies.affine_rules=NULL;race.dependencies.art_motion_rules=NULL;
    }
    puts("portable recovered Time Trial session: PASS");
    return 0;
}
