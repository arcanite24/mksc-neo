#ifndef PORTABLE_CANNONBALL_H
#define PORTABLE_CANNONBALL_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define PORTABLE_CANNONBALL_CAPACITY 64u
/* 563D4/56440: class84 falling cannonball and six explosion frames. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,velocity_z_q16,state,timer;
    uint16_t affine_x_q8,affine_y_q8;
    uint8_t checkpoint,placement_order,flags,art,size;
} PortableCannonball;
typedef struct {
    PortableCannonball actors[PORTABLE_CANNONBALL_CAPACITY];
    const PortableCannonball *initial;
    size_t count;
} PortableCannonballCourse;
static inline int32_t portable_cannonball_signed(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);}
static inline int portable_cannonball_init(PortableCannonball *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant>3u)return 0;
    *a=(PortableCannonball){.x_q16=x,.y_q16=y,.checkpoint=checkpoint,
        .state=1,.timer=variant*32,.size=4,.affine_x_q8=256,.affine_y_q8=256};return 1;
}
static inline int portable_cannonball_advance(PortableCannonball *a)
{
    if(!a || a->state<0 || a->state>5 || a->timer<0 || a->timer>999 || a->art>6)return 0;
    int32_t timer=a->timer-1;
    if(timer<0 && ++a->state>5){a->state=0;timer=0;}
    a->timer=timer;
    switch(a->state){
    case 0:a->state=1;a->timer=64;a->size=4;a->flags&=UINT8_C(0xfc);break;
    case 1:break;
    case 2:
        a->state=3;a->timer=999;a->art=0;a->height_q16=0x280000;
        a->velocity_z_q16=0;a->affine_x_q8=1024;a->affine_y_q8=1024;a->flags|=3u;
        /* fall through */
    case 3:{
        const int32_t scale=(int32_t)a->affine_x_q8-24;
        a->affine_x_q8=(uint16_t)(scale>256?scale:256);a->affine_y_q8=a->affine_x_q8;
        a->velocity_z_q16=portable_cannonball_signed((uint32_t)a->velocity_z_q16-0x800u);
        a->height_q16=portable_cannonball_signed((uint32_t)a->height_q16+(uint32_t)a->velocity_z_q16);
        if(a->height_q16<=0){a->height_q16=0;a->state=4;a->timer=1;}break;}
    case 4:
        a->state=5;a->timer=31;a->affine_x_q8=256;a->affine_y_q8=256;a->flags&=UINT8_C(0xfd);
        /* 5664E uses the entry countdown, not the newly stored31. */
        /* fall through */
    case 5:
        if((uint32_t)timer<=31u && timer>=28){a->art=1;a->size=20;}
        else if((uint32_t)timer<=27u && timer>=24)a->art=2;
        else if((uint32_t)timer<=23u && timer>=20)a->art=3;
        else if((uint32_t)timer<=19u && timer>=14)a->art=4;
        else if((uint32_t)timer<=13u && timer>=8)a->art=5;
        else a->art=6;
        break;
    default:return 0;
    }
    return 1;
}
static inline int portable_cannonball_course_init(PortableCannonballCourse *course,
    const PortableCannonball *initial,size_t count)
{
    if(!course || count>PORTABLE_CANNONBALL_CAPACITY || (count && !initial))return 0;
    *course=(PortableCannonballCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
