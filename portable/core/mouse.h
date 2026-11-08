#ifndef PORTABLE_MOUSE_H
#define PORTABLE_MOUSE_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define PORTABLE_MOUSE_CAPACITY 64u
/* 58408/584F4/5868C/58734: class8E walking and hopping mice. Camera-facing
 * inputs are snapshots of pre-turn velocity; presentation owns the transform. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,velocity_x_q16,velocity_y_q16,velocity_z_q16;
    int32_t facing_x_q16,facing_y_q16;
    int16_t timer,affine_y_q8;
    uint16_t variant;
    uint8_t checkpoint,placement_order,flags,art,walk_phase,walk_ticks,hop_phase,turn_flip;
} PortableMouse;
typedef struct {
    PortableMouse actors[PORTABLE_MOUSE_CAPACITY];
    const PortableMouse *initial;
    size_t count;
} PortableMouseCourse;
static inline int32_t portable_mouse_signed(uint32_t n)
{return n<=INT32_MAX?(int32_t)n:-1-(int32_t)(UINT32_MAX-n);}
static inline int portable_mouse_init(PortableMouse *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u ||
       (variant!=0u && variant!=1u && variant!=0x8001u))return 0;
    *a=(PortableMouse){.x_q16=x,.y_q16=y,.checkpoint=checkpoint,.variant=variant,
        .flags=3u,.affine_y_q8=256,
        .velocity_x_q16=variant==0u?-32768:0,
        .velocity_y_q16=variant==1u?-32768:0};
    a->facing_x_q16=a->velocity_x_q16;a->facing_y_q16=a->velocity_y_q16;
    return 1;
}
static inline int portable_mouse_advance(PortableMouse *a,const int32_t *sine,size_t sine_count)
{
    if(!a || a->art>1u || a->walk_phase>1u || a->walk_ticks>9u || a->hop_phase>1u ||
       (a->variant!=0u && a->variant!=1u && a->variant!=0x8001u))return 0;
    if(a->variant==0x8001u){
        if(a->flags&2u){
            if(a->timer<0 || a->timer>=INT16_MAX)return 0;
            ++a->timer;
            if(!a->hop_phase){
                a->art=1u;
                /* ROM 586C0: ble skips the launch, unlike transcribed C. */
                if(a->timer>59){a->hop_phase=1u;a->velocity_z_q16=0x20000;}
                return 1;
            }
            a->art=0u;
        }else if(a->flags&1u)a->art=0u;
        else return 1;
        a->height_q16=portable_mouse_signed((uint32_t)a->height_q16+(uint32_t)a->velocity_z_q16);
        a->velocity_z_q16=portable_mouse_signed((uint32_t)a->velocity_z_q16-0x4000u);
        if(a->height_q16<=0){
            a->height_q16=0;
            if(a->flags&2u){a->hop_phase=0u;a->timer=0;}
            else a->flags&=UINT8_C(0xfe);
        }
        return 1;
    }
    a->facing_x_q16=a->velocity_x_q16;a->facing_y_q16=a->velocity_y_q16;
    a->turn_flip=0u;
    if(!(a->flags&2u)){
        if(!(a->flags&1u))return 1;
        if(a->timer<0 || a->timer>32 || !sine || sine_count<1024u)return 0;
        a->height_q16=portable_mouse_signed((uint32_t)sine[(unsigned)a->timer*16u]*48u);
        if(--a->timer<0)a->flags&=UINT8_C(0xfe);
        return 1;
    }
    const int t=a->timer;
    if(t<0 || t>620)return 0;
    if(t<=150 || (t>=311 && t<=460)){
        if(t==0 || t==311){
            a->velocity_x_q16=portable_mouse_signed(0u-(uint32_t)a->velocity_x_q16);
            a->velocity_y_q16=portable_mouse_signed(0u-(uint32_t)a->velocity_y_q16);
        }
        a->x_q16=portable_mouse_signed((uint32_t)a->x_q16+(uint32_t)a->velocity_x_q16);
        a->y_q16=portable_mouse_signed((uint32_t)a->y_q16+(uint32_t)a->velocity_y_q16);
        if(++a->walk_ticks>9u){a->walk_ticks=0u;a->walk_phase^=1u;a->art=a->walk_phase;}
    }else if((t>=191 && t<=210) || (t>=231 && t<=250) ||
             (t>=501 && t<=520) || (t>=541 && t<=560))a->turn_flip=1u;
    else if((t>=151 && t<=190) || (t>=461 && t<=500))a->art=1u;
    a->timer=(int16_t)(t==620?0:t+1);
    return 1;
}
static inline void portable_mouse_contact(PortableMouse *a,uint16_t type,int16_t guard,int16_t protection)
{
    if(a && (type&0xf000u)==0x2000u && !guard && protection){
        a->timer=32;a->flags&=UINT8_C(0xfd);a->affine_y_q8=-256;a->velocity_z_q16=0x30000;
    }
}
static inline int portable_mouse_course_init(PortableMouseCourse *course,
    const PortableMouse *initial,size_t count)
{
    if(!course || count>PORTABLE_MOUSE_CAPACITY || (count && !initial))return 0;
    *course=(PortableMouseCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
