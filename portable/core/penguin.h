#ifndef PORTABLE_PENGUIN_H
#define PORTABLE_PENGUIN_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define PORTABLE_PENGUIN_CAPACITY 64u
/* 588AC/58788/588E0: class9C stationary penguins and protected sine flight. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,timer;
    int16_t affine_y_q8;
    uint8_t checkpoint,placement_order,flags,art;
} PortablePenguin;
typedef struct {
    PortablePenguin actors[PORTABLE_PENGUIN_CAPACITY];
    const PortablePenguin *initial;
    size_t count;
} PortablePenguinCourse;
static inline int32_t portable_penguin_signed(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);}
static inline int portable_penguin_init(PortablePenguin *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    /* Factory retains low variant bits at +50; these callbacks do not read them. */
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant>1u)return 0;
    *a=(PortablePenguin){.x_q16=x,.y_q16=y,.checkpoint=checkpoint,.flags=3u,.affine_y_q8=256};return 1;
}
static inline int portable_penguin_advance(PortablePenguin *a,const int32_t *sine,size_t count)
{
    if(!a || !sine || count<1024u || a->art>5u)return 0;
    if(a->flags&2u){
        if(a->timer<0 || a->timer>41)return 0;
        switch(a->timer){
        case 0:a->art=0u;break;
        case 6:a->art=1u;break;
        case 14:a->art=2u;break;
        case 20:a->art=3u;break;
        case 28:a->art=4u;break;
        case 34:a->art=5u;break;
        default:break;
        }
        a->timer=portable_penguin_signed((uint32_t)a->timer+1u);
        if(a->timer>41)a->timer=0;
    }else if(a->flags&1u){
        if(a->timer<0 || a->timer>32)return 0;
        a->height_q16=portable_penguin_signed((uint32_t)sine[(uint32_t)a->timer*16u]*48u);
        if(--a->timer<0)a->flags&=UINT8_C(0xfe);
    }
    return 1;
}
static inline int portable_penguin_contact(PortablePenguin *a,uint16_t type,
    int16_t guard,int16_t protection)
{
    if(!a || (type&0xf000u)!=0x2000u || guard || !protection)return 0;
    a->timer=32;a->flags&=UINT8_C(0xfd);a->affine_y_q8=-256;return 1;
}
static inline int portable_penguin_course_init(PortablePenguinCourse *course,
    const PortablePenguin *initial,size_t count)
{
    if(!course || count>PORTABLE_PENGUIN_CAPACITY || (count && !initial))return 0;
    *course=(PortablePenguinCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
