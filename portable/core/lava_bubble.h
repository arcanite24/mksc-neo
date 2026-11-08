#ifndef PORTABLE_LAVA_BUBBLE_H
#define PORTABLE_LAVA_BUBBLE_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#define PORTABLE_LAVA_BUBBLE_CAPACITY 64u

/* Class82: native fields consumed by 55D70/55E5C/55F74. */
typedef struct {
    int32_t x_q16,y_q16,height_q16;
    int32_t velocity_x_q16,velocity_y_q16,velocity_z_q16;
    int32_t timer;
    uint16_t period;
    uint8_t phase,flags,art,checkpoint;
    uint8_t placement_order;
} PortableLavaBubble;

typedef struct {
    PortableLavaBubble actors[PORTABLE_LAVA_BUBBLE_CAPACITY];
    const PortableLavaBubble *initial;
    size_t count;
} PortableLavaBubbleCourse;

static inline int32_t portable_lava_bubble_signed(uint32_t bits)
{
    return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);
}

static inline int portable_lava_bubble_init(PortableLavaBubble *actor,
    int32_t x,int32_t y,uint8_t checkpoint,unsigned variant)
{
    /* Constructor switches on code-0x820001; variant0 takes the default. */
    static const uint16_t delays[13]={0,100,180,140,160,200,120,30,146,122,98,74,50};
    static const uint16_t periods[13]={90,100,90,100,140,100,90,80,180,180,180,180,180};
    if(actor==NULL||variant>12u||checkpoint>=128u||x<0||y<0||
       x>2048*65536||y>2048*65536)return 0;
    *actor=(PortableLavaBubble){.x_q16=x,.y_q16=y,.timer=delays[variant],
        .period=periods[variant],.phase=1u,.checkpoint=checkpoint};
    return 1;
}

static inline int portable_lava_bubble_advance(PortableLavaBubble *actor,
    const int32_t *sine)
{
    if(actor==NULL||sine==NULL||actor->phase>6u||actor->art>1u||
       actor->period==0u)return 0;
    PortableLavaBubble next=*actor;
    next.timer=portable_lava_bubble_signed((uint32_t)next.timer-1u);
    switch(next.phase){
    case 0u:
        next.flags&=UINT8_C(0xfc);next.phase=1u;
        /* fall through */
    case 1u:
        if(next.timer<=0){next.phase=2u;next.timer=64;}
        break;
    case 2u:
        next.art=0u;next.flags|=3u;next.phase=3u;
        /* fall through */
    case 3u:
        if(next.timer<0||next.timer>63)return 0;
        next.height_q16=portable_lava_bubble_signed((uint32_t)sine[next.timer*8]*48u);
        if(next.timer<=31)next.art=1u;
        if(next.timer<=0){next.phase=0u;next.timer=next.period;}
        break;
    case 4u:
        next.flags&=UINT8_C(0xfd);next.phase=5u;
        /* fall through */
    case 5u:
        next.x_q16=portable_lava_bubble_signed((uint32_t)next.x_q16+(uint32_t)next.velocity_x_q16);
        next.y_q16=portable_lava_bubble_signed((uint32_t)next.y_q16+(uint32_t)next.velocity_y_q16);
        next.height_q16=portable_lava_bubble_signed((uint32_t)next.height_q16+(uint32_t)next.velocity_z_q16);
        next.art=next.velocity_z_q16>0?0u:1u;
        next.velocity_z_q16=portable_lava_bubble_signed((uint32_t)next.velocity_z_q16-0x4000u);
        if(next.height_q16<0){next.phase=6u;next.flags&=UINT8_C(0xfe);}
        break;
    case 6u:break;
    default:return 0;
    }
    *actor=next;return 1;
}

static inline void portable_lava_bubble_contact(PortableLavaBubble *actor,
    uint16_t type,int16_t guard,int16_t protection,int16_t vx,int16_t vy)
{
    /* 55F74 leaves ordinary contacts to the driver's callback. */
    if(actor!=NULL&&(type&0xf000u)==0x2000u&&guard==0&&protection!=0){
        actor->phase=4u;actor->velocity_x_q16=vx;actor->velocity_y_q16=vy;
        actor->velocity_z_q16=0x30000;
    }
}

static inline int portable_lava_bubble_course_init(PortableLavaBubbleCourse *course,
    const PortableLavaBubble *initial,size_t count)
{
    if(course==NULL||count>PORTABLE_LAVA_BUBBLE_CAPACITY||
       (count!=0u&&initial==NULL))return 0;
    *course=(PortableLavaBubbleCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}

static inline int portable_lava_bubble_course_advance(PortableLavaBubbleCourse *course,
    const int32_t *sine)
{
    if(course==NULL||course->count>PORTABLE_LAVA_BUBBLE_CAPACITY)return 0;
    for(size_t i=0;i<course->count;++i)
        if(!portable_lava_bubble_advance(&course->actors[i],sine))return 0;
    return 1;
}
#endif
