#ifndef PORTABLE_SEAGULL_H
#define PORTABLE_SEAGULL_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define PORTABLE_SEAGULL_CAPACITY 64u
/* 57364/573B8/57554: class87 island seagull. Facing is presentation-only. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,velocity_z_q16;
    int32_t state,timer,phase;
    uint16_t affine_x_q8,affine_y_q8;
    uint8_t checkpoint,placement_order,flags,art,flip_y;
} PortableSeagull;
typedef struct {
    PortableSeagull actors[PORTABLE_SEAGULL_CAPACITY];
    const PortableSeagull *initial;
    size_t count;
} PortableSeagullCourse;
static inline int32_t portable_seagull_signed(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);}
static inline int portable_seagull_init(PortableSeagull *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant) return 0;
    *a=(PortableSeagull){.x_q16=x,.y_q16=y,.checkpoint=checkpoint,.flags=3u,
        .affine_x_q8=224u,.affine_y_q8=224u};return 1;
}
static inline int portable_seagull_advance(PortableSeagull *a,const int32_t *sine)
{
    if(!a || !sine || a->art>2u)return 0;
    switch(a->state){
    case 0:
        a->state=1;a->flags|=2u;a->art=0u;a->height_q16=0;
        /* fall through */
    case 1:a->affine_x_q8=256u;break;
    case 16:a->state=17;a->timer=0;a->phase=0;
        /* fall through */
    case 17:{
        if(a->timer<0 || a->timer>4 || a->phase<0 || a->phase>7)return 0;
        a->affine_x_q8=256u;
        if(a->timer<=3)++a->timer;else{a->timer=0;++a->phase;}
        if(a->phase<=7)a->art=(uint8_t)(1+(a->phase&1));else a->state=0;
        /* 574D4 converts the six-bit phase to a signed angle, divides
         * toward zero by64, then uses its low ten sine-index bits. */
        const uint16_t bits=(uint16_t)((uint32_t)(a->phase*4+a->timer)<<10u);
        const int32_t angle=bits<=INT16_MAX?bits:(int32_t)bits-65536;
        const unsigned index=(unsigned)(angle/64)&1023u;
        a->height_q16=portable_seagull_signed((uint32_t)sine[index]*20u);
        break;}
    case 240:
        a->state=241;a->flags&=UINT8_C(0xfd);a->flip_y=1u;
        a->affine_y_q8=256u;a->velocity_z_q16=0x30000;
        /* fall through */
    case 241:
        a->height_q16=portable_seagull_signed((uint32_t)a->height_q16+(uint32_t)a->velocity_z_q16);
        if(a->height_q16<0){a->state=243;a->flags&=UINT8_C(0xfe);}
        else a->velocity_z_q16=portable_seagull_signed((uint32_t)a->velocity_z_q16-0x4000u);
        break;
    case 242:a->state=243;a->flags&=UINT8_C(0xfe);break;
    case 243:break;
    default:return 0;
    }
    return 1;
}
static inline void portable_seagull_contact(PortableSeagull *a,uint16_t type,
    int16_t guard,int16_t protection)
{
    if(a && (type&0xf000u)==0x2000u && !guard && a->state!=243){
        if(protection)a->state=240;
        else if(!(a->state&0xf0))a->state=16;
    }
}
static inline int portable_seagull_course_init(PortableSeagullCourse *course,
    const PortableSeagull *initial,size_t count)
{
    if(!course || count>PORTABLE_SEAGULL_CAPACITY || (count && !initial))return 0;
    *course=(PortableSeagullCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
