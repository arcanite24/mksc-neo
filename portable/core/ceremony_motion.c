#include "ceremony_motion.h"
#include <limits.h>
#include <string.h>

static int32_t signed32(uint32_t x)
{return x<=INT32_MAX?(int32_t)x:-1-(int32_t)(UINT32_MAX-x);}
static int16_t narrow16(int32_t x)
{const uint16_t b=(uint16_t)x;return (int16_t)(b<0x8000u?(int32_t)b:(int32_t)b-65536);}
static int32_t add32(int32_t a,int32_t b)
{return signed32((uint32_t)a+(uint32_t)b);}
static int32_t asr(int32_t x,unsigned n)
{const int64_t divisor=INT64_C(1)<<n;
 return x>=0?(int32_t)(x/divisor):(int32_t)(-((-(int64_t)x+divisor-1)/divisor));}
static int32_t mul32(int32_t a,int32_t b)
{return signed32((uint32_t)a*(uint32_t)b);}
/* Source Thumb adds the sign bit, not0xffff, before ASR16. */
static int32_t product16(int32_t a,int32_t b)
{const int32_t p=mul32(a,b);return asr(add32(p,p<0?1:0),16u);}
static int sample(const int32_t *sine,size_t count)
{return sine!=NULL&&count>=1024u;}
void portable_ceremony_motion_reset(PortableCeremonyMotion *s)
{if(s)memset(s,0,sizeof *s);}
void portable_ceremony_zoom_start(PortableCeremonyMotion *s,int visible)
{
    s->zoom=(PortableCeremonyCellState){.x_q16=112*65536,.y_q16=52*65536,
        .inverse_x=(int16_t)(visible?256:0),.inverse_y=(int16_t)(visible?256:0),.visible=1u};
}
int portable_ceremony_zoom_tick(PortableCeremonyMotion *s,const int32_t *sine,size_t count)
{
    if(!s||!sample(sine,count))return -1;
    const int t=s->zoom.tick=narrow16(s->zoom.tick+1);
    if(t>64){s->zoom.tick=0;return 1;}
    s->zoom.inverse_x=s->zoom.inverse_y=narrow16(1536-(1280*t)/64);
    s->zoom.angle=narrow16(mul32(asr(mul32(sine[(unsigned)(t*4)&1023u],768),15u),256));
    return 0;
}
void portable_ceremony_flypast_start(PortableCeremonyMotion *s)
{
    s->flypast=(PortableCeremonyCellState){.y_q16=210*65536,.inverse_x=256,.inverse_y=256,.visible=1u};
    s->fly_frame=s->fly_counter=0u;
}
static int32_t fly_sine(const int32_t *sine,int t,int divisor)
{
    const int q=narrow16(mul32(t,32768)/divisor);
    return sine[(unsigned)(256-q/64)&1023u];
}
int portable_ceremony_flypast_tick(PortableCeremonyMotion *s,const int32_t *sine,size_t count)
{
    if(!s||!sample(sine,count))return -1;
    const int t=s->flypast.tick=narrow16(s->flypast.tick+1);
    if(t>200){s->flypast.tick=0;return 1;}
    const int32_t a=32768-fly_sine(sine,t,200),b=32768-fly_sine(sine,t,100);
    s->flypast.x_q16=(280+product16(a,-330))*65536;
    s->flypast.y_q16=(70+product16(b,-50))*65536;
    s->flypast.inverse_x=s->flypast.inverse_y=narrow16(176+product16(b,-16));
    s->flypast.angle=narrow16(4096+product16(a,-8192));
    return 0;
}
uint16_t portable_ceremony_flypast_animation_tick(PortableCeremonyMotion *s)
{
    if(++s->fly_counter<=6u)return 0u;
    s->fly_counter=0u;s->fly_frame=(uint8_t)((s->fly_frame+1u)&7u);
    return s->fly_frame==3u||s->fly_frame==7u?120u:0u;
}
int portable_ceremony_trophy_start(PortableCeremonyMotion *s,uint8_t rank,int small_trophy)
{
    if(!s||(!small_trophy&&rank>2u))return 0;
    static const int16_t xs[3]={120,88,152},ys[3]={32,40,48};
    const int16_t x=small_trophy?24:xs[rank],y=small_trophy?24:ys[rank];
    s->trophy=(PortableCeremonyCellState){.x_q16=x*65536,.y_q16=y*65536,.origin_x=x,.origin_y=y,
        .inverse_x=(int16_t)(small_trophy?256:0),.inverse_y=(int16_t)(small_trophy?256:0),.visible=1u};
    s->trophy_frame=s->trophy_counter=s->sparkle_palette=0u;s->confetti_rng=0u;
    for(unsigned i=0u;i<32u;++i)s->confetti[i].active=0u;
    return 1;
}
int portable_ceremony_trophy_appear_tick(PortableCeremonyMotion *s)
{
    if(!s)return -1;
    const int t=s->trophy.tick=narrow16(s->trophy.tick+1);
    if(t>32){s->trophy.tick=0;return 1;}
    s->trophy.inverse_x=s->trophy.inverse_y=narrow16(25600/((99*t)/32+1));return 0;
}
int portable_ceremony_trophy_move_tick(PortableCeremonyMotion *s,const int32_t *sine,size_t count)
{
    if(!s||!sample(sine,count))return -1;
    const int t=s->trophy.tick=narrow16(s->trophy.tick+1);
    if(t>128){s->trophy.tick=0;return 1;}
    const int32_t a=32768-sine[(unsigned)(256-4*t)&1023u];
    s->trophy.x_q16=(s->trophy.origin_x+product16(24-s->trophy.origin_x,a))*65536;
    s->trophy.y_q16=(s->trophy.origin_y+product16(24-s->trophy.origin_y,a))*65536;
    s->trophy.inverse_x=s->trophy.inverse_y=narrow16(256+product16(32768-sine[(unsigned)(256-8*t)&1023u],-48));
    return 0;
}
void portable_ceremony_trophy_animation_tick(PortableCeremonyMotion *s)
{
    if(s->trophy.inverse_x||s->trophy.inverse_y){portable_ceremony_confetti_tick(s);s->sparkle_angle=(uint16_t)(s->sparkle_angle+1024u);}
    if(++s->trophy_counter>8u){s->trophy_counter=0u;s->trophy_frame=(uint8_t)((s->trophy_frame+1u)&15u);}
    s->sparkle_palette=(uint8_t)((s->sparkle_palette+1u)%12u);
}
static uint32_t random15(PortableCeremonyMotion *s)
{s->confetti_rng=s->confetti_rng*UINT32_C(0x41c64e6d)+0x3039u;return (s->confetti_rng>>16u)&0x7fffu;}
void portable_ceremony_confetti_tick(PortableCeremonyMotion *s)
{
    for(unsigned i=0u;i<32u;++i){
        PortableCeremonyConfetti *p=&s->confetti[i];
        if(!p->active){
            p->x_q16=s->trophy.x_q16;p->y_q16=s->trophy.y_q16;
            p->target_x_q16=add32(p->x_q16,(150-(int32_t)(random15(s)%301u))*65536);
            p->target_y_q16=add32(p->y_q16,(100+(int32_t)(random15(s)%101u))*65536);
            p->remaining=(uint8_t)(64u+random15(s)%33u);
            const int life=p->remaining;
            p->vx_q16=signed32((uint32_t)p->target_x_q16-(uint32_t)p->x_q16)/life;
            const int32_t dy=signed32((uint32_t)p->target_y_q16-(uint32_t)p->y_q16);
            p->vy_q16=add32(mul32(-8192,life*life),mul32(dy,2))/(2*life);p->active=1u;
        }
        p->x_q16=add32(p->x_q16,p->vx_q16);p->y_q16=add32(p->y_q16,p->vy_q16);p->vy_q16=add32(p->vy_q16,8192);
        p->remaining=(uint8_t)(p->remaining-1u);
        if(p->remaining&0x80u){p->x_q16=p->target_x_q16;p->y_q16=p->target_y_q16;p->active=0u;}
    }
}
void portable_ceremony_loser_start(PortableCeremonyMotion *s)
{s->loser=(PortableCeremonyCellState){.x_q16=120*65536,.y_q16=-38*65536,.origin_x=120,.origin_y=-38,.inverse_x=256,.inverse_y=256,.visible=1u};s->shadow_expansion=0u;}
int portable_ceremony_loser_tick(PortableCeremonyMotion *s,int16_t y,uint16_t *song)
{
    if(!s||!song)return -1;
    *song=0u;const int t=s->loser.tick=narrow16(s->loser.tick+1);
    if(t>64){s->loser.tick=0;return 1;}
    s->loser.y_q16=y*65536;if(t==24||t==40||t==48)*song=175u;return 0;
}
uint16_t portable_ceremony_loser_shadow_tick(PortableCeremonyMotion *s)
{
    const int y=s->loser.y_q16/65536;
    if(y==122)return 0u;
    int width;
    if(s->loser.tick==0){s->shadow_expansion=(uint16_t)(s->shadow_expansion<198u?s->shadow_expansion+2u:200u);width=s->shadow_expansion;}
    else{width=200-(48*y)/122;if(width>200)width=200;if(width<120)width=120;}
    return (uint16_t)(25600/width);
}
static int tracks_valid(const PortableCeremonyTrack *tracks,const PortableCeremonySequenceFrame *frames,size_t count)
{
    if(!tracks||!frames||count>32u)return 0;
    for(size_t i=0u;i<count;++i)if(frames[i].duration>32767u)return 0;
    for(unsigned i=0u;i<8u;++i)if(!tracks[i].count||tracks[i].first>count||tracks[i].count>count-tracks[i].first||tracks[i].initial_frame>=tracks[i].count)return 0;
    return 1;
}
int portable_ceremony_tracks_start(PortableCeremonyMotion *s,const PortableCeremonyTrack tracks[8],const PortableCeremonySequenceFrame *frames,size_t count,int scrolling)
{
    if(!s||!tracks_valid(tracks,frames,count))return 0;
    for(unsigned i=0u;i<8u;++i)s->actors[i]=(PortableCeremonyTimedActor){.x_q16=tracks[i].x*65536,.y_q16=(tracks[i].y+(scrolling?128:0))*65536,
        .frame=tracks[i].initial_frame,.remaining=tracks[i].initial_remaining,.depth_bias=tracks[i].depth_bias};
    s->actor_scroll_remaining=(int16_t)(scrolling?256:0);return 1;
}
int portable_ceremony_tracks_tick(PortableCeremonyMotion *s,const PortableCeremonyTrack tracks[8],const PortableCeremonySequenceFrame *frames,size_t count)
{
    if(!s||!tracks_valid(tracks,frames,count))return 0;
    const int scrolling=s->actor_scroll_remaining>0;
    if(scrolling)--s->actor_scroll_remaining;
    for(unsigned i=0u;i<8u;++i){
        PortableCeremonyTimedActor *a=&s->actors[i];if(a->frame>=tracks[i].count)return 0;
        if(scrolling)a->y_q16=add32(a->y_q16,-32768);
        a->remaining=(uint16_t)(a->remaining-1u);
        if(a->remaining&0x8000u){a->frame=(uint16_t)((a->frame+1u)%tracks[i].count);a->remaining=frames[tracks[i].first+a->frame].duration;}
    }
    return 1;
}
PortableCeremonySprite portable_ceremony_cell_sprite(const PortableCeremonyCellState *cell,uint16_t image_id,uint8_t priority)
{return (PortableCeremonySprite){.image_id=image_id,.x_q16=cell->x_q16,.y_q16=cell->y_q16,.inverse_x=cell->inverse_x,
    .inverse_y=cell->inverse_y,.angle=cell->angle,.priority=priority,.visible=(uint8_t)(cell->visible&&(cell->inverse_x||cell->inverse_y))};}
int portable_ceremony_track_sprites(const PortableCeremonyMotion *s,const PortableCeremonyTrack tracks[8],const PortableCeremonySequenceFrame *frames,size_t count,int16_t depth,int16_t road_line,PortableCeremonySprite out[8])
{
    if(!s||!out||!tracks_valid(tracks,frames,count))return 0;
    for(unsigned i=0u;i<8u;++i){const PortableCeremonyTimedActor *a=&s->actors[i];if(a->frame>=tracks[i].count)return 0;
        const int32_t y=add32(a->y_q16,s->actor_scroll_remaining==0?(road_line-73)*65536:0);
        out[i]=(PortableCeremonySprite){.image_id=frames[tracks[i].first+a->frame].image_id,.x_q16=a->x_q16,.y_q16=y,
            .depth=narrow16(depth+a->depth_bias),.inverse_x=256,.inverse_y=256,.priority=1u,.visible=(uint8_t)(y/65536<=223)};}
    return 1;
}

static int32_t midpoint32(int32_t a,int32_t b)
{const int32_t sum=add32(a,b);return asr(add32(sum,sum<0?1:0),1u);}
int portable_ceremony_world_target(const PortableCeremonyWorldRules *r,unsigned i,PortableCeremonyWorldPoint *out)
{
    if(!r||!out||i>3u)return 0;
    out->x_q16=i==1u?r->grid[6].x_q16:i==2u?r->grid[5].x_q16:midpoint32(r->grid[5].x_q16,r->grid[6].x_q16);
    out->x_q16=add32(out->x_q16,-0x88000);
    out->y_q16=i==3u?add32(r->grid[6].y_q16,0xc0000):midpoint32(r->grid[5].y_q16,r->grid[6].y_q16);
    out->z_q16=0;return 1;
}
int portable_ceremony_world_init(PortableCeremonyMotion *s,const PortableCeremonyWorldRules *r,uint8_t rank,const uint8_t classes[4])
{
    if(!s||!r||!classes||rank>7u)return 0;
    for(unsigned i=0u;i<4u;++i)if(classes[i]>7u)return 0;
    memset(&s->world,0,sizeof s->world);
    s->world.count=(uint8_t)(rank<3u?3u:4u);s->world.selected_index=(uint8_t)(rank<3u?rank:3u);
    s->world.camera_x_q16=add32(midpoint32(r->grid[6].x_q16,r->grid[7].x_q16),-0x88000);
    s->world.camera_y_q16=midpoint32(r->grid[6].y_q16,r->grid[7].y_q16);
    for(unsigned i=0u;i<s->world.count;++i){
        PortableCeremonyWorldDriver *d=&s->world.drivers[i];d->position=r->grid[i];d->source_flags=0x20u;
        d->selected=(uint8_t)(i==s->world.selected_index);d->driver_class=classes[i];
        d->affine.inverse_x_q8=d->affine.inverse_y_q8=256u;
    }
    return 1;
}
int portable_ceremony_world_reveal(PortableCeremonyMotion *s,const PortableCeremonyWorldRules *r,unsigned i)
{
    if(!s||i>=s->world.count||!portable_ceremony_world_target(r,i,&s->world.drivers[i].target))return 0;
    PortableCeremonyWorldDriver *d=&s->world.drivers[i];d->position=d->target;
    if(i<3u){d->position.y_q16=add32(d->position.y_q16,-(0xff0000+(int32_t)i*0x80000));d->heading=0x8000u;}
    else{d->position.x_q16=add32(d->position.x_q16,0x1e0000);d->heading=0xc000u;}
    d->visible=1u;d->arrived=0u;return 1;
}
int portable_ceremony_world_arrival_tick(PortableCeremonyMotion *s,unsigned i)
{
    if(!s||i>=s->world.count)return -1;
    PortableCeremonyWorldDriver *d=&s->world.drivers[i];int done=1;
    const int step=i<3u?0x8000:0x4000;
    int32_t *positions[2]={&d->position.x_q16,&d->position.y_q16};
    const int32_t targets[2]={d->target.x_q16,d->target.y_q16};
    for(unsigned axis=0u;axis<2u;++axis)if(*positions[axis]!=targets[axis]){
        const int32_t delta=signed32((uint32_t)targets[axis]-(uint32_t)*positions[axis]);
        *positions[axis]=add32(*positions[axis],delta>0?step:-step);done=0;
    }
    if(done){d->arrived=1u;s->world.arrival_mask|=(uint8_t)(1u<<i);}return done;
}
static int podium_target(const PortableCeremonyWorldRules *r,unsigned i,PortableCeremonyWorldPoint *p)
{
    if(!r||!p||i>2u)return 0;
    p->x_q16=add32(add32(midpoint32(r->grid[6].x_q16,r->grid[7].x_q16),-0x88000),r->podium_offset[i].x_q16);
    p->y_q16=add32(midpoint32(r->grid[6].y_q16,r->grid[7].y_q16),r->podium_offset[i].y_q16);
    p->z_q16=r->podium_offset[i].z_q16;return 1;
}
static void podium_pose(PortableCeremonyWorldDriver *d,unsigned i)
{if(i==0u)d->response_88=-1;else d->heading=(uint16_t)(i==1u?0x8800u:0x7800u);}
int portable_ceremony_world_hop_start(PortableCeremonyMotion *s,const PortableCeremonyWorldRules *r,unsigned i)
{
    if(!s||i>=s->world.count||!podium_target(r,i,&s->world.drivers[i].target))return -1;
    PortableCeremonyWorldDriver *d=&s->world.drivers[i];
    d->vx_q16=signed32((uint32_t)d->target.x_q16-(uint32_t)d->position.x_q16)/32;
    d->vy_q16=signed32((uint32_t)d->target.y_q16-(uint32_t)d->position.y_q16)/32;
    const int32_t dz=signed32((uint32_t)d->target.z_q16-(uint32_t)d->position.z_q16);
    d->vz_q16=add32(0x800000,mul32(dz,2))/64;
    d->callback_tick=0u;d->hop_complete=0u;d->source_flags|=0x40u;return 156;
}
int portable_ceremony_world_hop_tick(PortableCeremonyMotion *s,unsigned i,const PortableDriverAffineRules *rules)
{
    if(!s||i>=s->world.count||i>2u)return -1;
    PortableCeremonyWorldDriver *d=&s->world.drivers[i];
    if(d->callback_tick>22u)s->world.shadow_started=1u;
    d->callback_tick=(uint16_t)(d->callback_tick+1u);
    int done=0;
    if(d->callback_tick<=32u){d->position.x_q16=add32(d->position.x_q16,d->vx_q16);
        d->position.y_q16=add32(d->position.y_q16,d->vy_q16);d->position.z_q16=add32(d->position.z_q16,d->vz_q16);
        d->vz_q16=add32(d->vz_q16,-8192);
    }else{d->position=d->target;d->hop_complete=1u;s->world.hop_mask|=(uint8_t)(1u<<i);done=1;}
    if(portable_driver_affine_request(&d->affine,rules,1u)<0)return -1;
    podium_pose(d,i);return done;
}
int portable_ceremony_world_place_podium(PortableCeremonyMotion *s,const PortableCeremonyWorldRules *r,unsigned i)
{
    if(!s||i>=s->world.count||!podium_target(r,i,&s->world.drivers[i].position))return 0;
    s->world.drivers[i].source_flags|=0x40u;podium_pose(&s->world.drivers[i],i);return 1;
}
int portable_ceremony_world_rotate_loser_tick(PortableCeremonyMotion *s)
{
    if(!s||s->world.count!=4u)return -1;
    PortableCeremonyWorldDriver *d=&s->world.drivers[3];d->callback_tick=(uint16_t)(d->callback_tick+1u);
    if(d->callback_tick>32u)return 1;
    d->heading=(uint16_t)(0xc000u+(unsigned)d->callback_tick*512u);return 0;
}
int portable_ceremony_world_bounce_start(PortableCeremonyMotion *s,unsigned i)
{if(!s||i>=s->world.count)return 0;PortableCeremonyWorldDriver *d=&s->world.drivers[i];d->bounce_base_q16=d->position.z_q16;d->callback_tick=0u;return 1;}
int portable_ceremony_world_bounce_tick_scaled(PortableCeremonyMotion *s,unsigned i,int16_t ease,int32_t factor)
{
    if(!s||i>=s->world.count)return -1;
    PortableCeremonyWorldDriver *d=&s->world.drivers[i];d->callback_tick=(uint16_t)(d->callback_tick+1u);
    if(d->callback_tick>32u){d->position.z_q16=d->bounce_base_q16;return 1;}
    d->position.z_q16=add32(d->bounce_base_q16,mul32(ease,factor));return 0;
}
int portable_ceremony_world_bounce_tick(PortableCeremonyMotion *s,unsigned i,int16_t ease)
{return portable_ceremony_world_bounce_tick_scaled(s,i,ease,768);}
int portable_ceremony_world_celebrate(PortableCeremonyMotion *s,unsigned i,const PortableFinishSchedule *schedule)
{
    if(!s||i>=s->world.count)return -1;
    PortableCeremonyWorldDriver *d=&s->world.drivers[i];
    return portable_finish_animation_request(&d->finish_mode,&d->finish_phase,&d->finish_flags,schedule);
}
int portable_ceremony_world_art_tick(PortableCeremonyMotion *s,unsigned i,const PortableCharacterViewRules *view_rules,const PortableFinishSchedule *schedule,const PortableDriverAffineRules *affine,uint16_t distance)
{
    if(!s||i>=s->world.count)return 0;
    PortableCeremonyWorldDriver *d=&s->world.drivers[i];
    if(portable_driver_affine_tick(&d->affine,affine)<0)return 0;
    PortableCharacterViewInput input={.camera_yaw=s->world.camera_yaw,.driver_angle=d->heading,
        .steering_response_q8=d->response_88,.source_flags=d->source_flags,.selected=d->selected};
    PortableCharacterViewPose pose=portable_character_view_pose(&input,view_rules);
    if(schedule&&portable_finish_animation_tick(&d->finish_mode,&d->finish_phase,&d->finish_frame,schedule,pose.angle,distance)<0)return 0;
    input.contact_mode_6e=d->finish_mode;input.contact_phase_6f=d->finish_phase;input.contact_frame_6c=d->finish_frame;input.contact_flags_6d=d->finish_flags;
    d->art_pose=portable_character_view_pose(&input,view_rules);return 1;
}
