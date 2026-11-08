#ifndef PORTABLE_SNOWMAN_H
#define PORTABLE_SNOWMAN_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define PORTABLE_SNOWMAN_CAPACITY 64u
/* 57E28/57E78/58098: class92 snowmen, two-stage destruction. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,state,timer;
    uint8_t checkpoint,placement_order,flags,art;
} PortableSnowman;
typedef struct {
    PortableSnowman actors[PORTABLE_SNOWMAN_CAPACITY];
    const PortableSnowman *initial;
    size_t count;
} PortableSnowmanCourse;
static inline int32_t portable_snowman_signed(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);}
static inline int portable_snowman_init(PortableSnowman *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant)return 0;
    *a=(PortableSnowman){.x_q16=x,.y_q16=y,.checkpoint=checkpoint,.state=17,.flags=3u};return 1;
}
static inline int portable_snowman_advance(PortableSnowman *a)
{
    if(!a || a->art>1u)return 0;
    switch(a->state){
    case 0:a->state=1;break;
    case 1:case 16:case 17:case 34:break;
    case 32:a->state=33;a->timer=0;break;
    case 33:{
        const uint32_t timer=(uint32_t)a->timer;
        a->timer=portable_snowman_signed(timer+1u);
        if(timer>34u)break;
        if((timer&3u)==0u)a->flags&=UINT8_C(0xfe);
        else if((timer&3u)==2u){
            a->art=(uint8_t)((timer&7u)==2u);a->flags|=1u;
            if(timer==34u){a->state=34;a->art=1u;}
        }
        break;}
    case 48:a->state=49;a->timer=0;a->flags&=UINT8_C(0xfd);a->art=1u;break;
    case 49:
        a->timer=portable_snowman_signed((uint32_t)a->timer+1u);
        if((uint32_t)a->timer&4u)a->flags|=1u;else a->flags&=UINT8_C(0xfe);
        if(a->timer>39){a->flags&=UINT8_C(0xfe);a->state=34;}break;
    default:return 0;
    }
    return 1;
}
static inline int portable_snowman_contact(PortableSnowman *a,uint16_t type,int16_t guard)
{
    if(!a || (type&0xf000u)!=0x2000u || guard)return 0;
    if((a->state&0xf0)==0x10)a->state=32;
    else if((a->state&0xf0)==0x20)a->state=48;
    return 1;
}
static inline int portable_snowman_course_init(PortableSnowmanCourse *course,
    const PortableSnowman *initial,size_t count)
{
    if(!course || count>PORTABLE_SNOWMAN_CAPACITY || (count && !initial))return 0;
    *course=(PortableSnowmanCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
