#ifndef PORTABLE_FALLING_STAR_H
#define PORTABLE_FALLING_STAR_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define PORTABLE_FALLING_STAR_CAPACITY 64u
/* 57B04/57BE0: class97 stars descend on a parabola, bounce and dissolve.
 * Source callbacks57E20/57E24 are empty; protection leaves stars intact. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,initial_x_q16,offset_q16;
    int32_t velocity_x_q16,velocity_z_q16;
    int16_t timer,delay;
    uint16_t affine_q8,angle;
    uint8_t state,variant,checkpoint,placement_order,flags,art;
} PortableFallingStar;
typedef struct {
    PortableFallingStar actors[PORTABLE_FALLING_STAR_CAPACITY];
    const PortableFallingStar *initial;
    size_t count;
} PortableFallingStarCourse;
static inline int32_t portable_falling_star_signed(uint32_t n)
{return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static inline int portable_falling_star_init(PortableFallingStar *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant>5u)return 0;
    *a=(PortableFallingStar){.x_q16=x,.y_q16=y,.initial_x_q16=x,
        .offset_q16=(variant&1u)?-40*65536:40*65536,
        .delay=(int16_t)(32u+(variant/2u)*16u),.affine_q8=224u,
        .state=1u,.variant=(uint8_t)variant,.checkpoint=checkpoint};
    return 1;
}
static inline int portable_falling_star_advance(PortableFallingStar *a)
{
    if(!a || a->variant>5u || a->state>5u || a->art>4u || a->timer<0 || a->timer>64 ||
       a->delay!=(int16_t)(32u+(a->variant/2u)*16u) || a->offset_q16!=((a->variant&1u)?-40*65536:40*65536))return 0;
    int timer=a->timer-1;
    unsigned state=a->state;
    if(timer<0 && ++state>5u){state=0;timer=0;}
    if(state==0u){
        state=1u;timer=a->delay;a->flags&=UINT8_C(0xfc);
    }else if(state==2u){
        state=3u;timer=64;a->art=0u;a->height_q16=40*65536;
        a->affine_q8=1024u;a->flags|=3u;
    }else if(state==4u){
        state=5u;timer=40;a->affine_q8=256u;a->angle=0u;
        a->velocity_x_q16=-(a->offset_q16/64);a->velocity_z_q16=0x28000;
    }
    if(state==3u){
        a->affine_q8=a->affine_q8>320u?(uint16_t)(a->affine_q8-64u):256u;
        a->angle=(uint16_t)(a->angle+((a->offset_q16<0)?1024u:UINT16_C(0xfc00)));
        const uint32_t square=(uint32_t)(64-timer)*(uint32_t)(64-timer);
        a->x_q16=portable_falling_star_signed((uint32_t)a->initial_x_q16+(uint32_t)a->offset_q16-
            square*(uint32_t)(a->offset_q16/4096));
        a->height_q16=(int32_t)(0x400000u-square*0x400u);
    }else if(state==5u){
        a->x_q16=portable_falling_star_signed((uint32_t)a->x_q16+(uint32_t)a->velocity_x_q16);
        a->height_q16=portable_falling_star_signed((uint32_t)a->height_q16+(uint32_t)a->velocity_z_q16);
        if(a->height_q16<0)a->height_q16=0;
        a->velocity_z_q16=portable_falling_star_signed((uint32_t)a->velocity_z_q16-0x2800u);
        a->angle=(uint16_t)(a->angle+((a->offset_q16<0)?768u:UINT16_C(0xfd00)));
        a->art=timer<=3?4u:timer<=7?1u:timer<=11?3u:timer<=15?2u:timer<=19?1u:0u;
    }
    a->state=(uint8_t)state;a->timer=(int16_t)timer;
    return 1;
}
static inline int portable_falling_star_course_init(PortableFallingStarCourse *course,
    const PortableFallingStar *initial,size_t count)
{
    if(!course || count>PORTABLE_FALLING_STAR_CAPACITY || (count && !initial))return 0;
    *course=(PortableFallingStarCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
