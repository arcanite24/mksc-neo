#ifndef PORTABLE_THUNDERCLOUD_H
#define PORTABLE_THUNDERCLOUD_H
#include <stdint.h>
#include <stddef.h>
#define PORTABLE_THUNDERCLOUD_CAPACITY 64u
#define PORTABLE_THUNDERCLOUD_ART_COUNT 72u
/* 57808/5787C: stationary class8F clouds, warning flashes and lightning.
 * Palette phases select imported colors, shared by the two source banks. */
typedef struct {
    int32_t x_q16,y_q16,height_q16;
    int32_t state,timer,palette_timer;
    uint8_t variant,checkpoint,placement_order,flags,art;
} PortableThundercloud;
typedef struct {
    PortableThundercloud actors[PORTABLE_THUNDERCLOUD_CAPACITY];
    const PortableThundercloud *initial;
    size_t count;
    uint8_t palette_phase[2];
} PortableThundercloudCourse;
static inline int portable_thundercloud_init(PortableThundercloud *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant>3u)return 0;
    /* The constructor's second art assignment is unconditional in the ROM. */
    *a=(PortableThundercloud){.x_q16=x,.y_q16=y,.height_q16=4*65536,
        .state=1,.timer=32,.variant=(uint8_t)variant,.checkpoint=checkpoint,.flags=1u,.art=4u};
    return 1;
}
static inline int portable_thundercloud_advance(PortableThundercloud *a,uint8_t palette_phase[2])
{
    if(!a || !palette_phase || a->variant>3u || a->art>7u || a->palette_timer<0 ||
       a->palette_timer>241 || palette_phase[0]>8u || palette_phase[1]>8u)return 0;
    const uint8_t base=a->variant<2u?0u:4u;
    if(a->state==0){a->state=1;a->art=base;a->timer=32;}
    if(a->state==1){
        if(a->timer<0 || a->timer>32)return 0;
        if(--a->timer<0)a->state=16;
    }else{
        if(a->state==16){a->state=17;a->timer=0;}
        if(a->state!=17 || a->timer<0 || a->timer>241)return 0;
        switch(a->timer){
        case 0:a->flags&=UINT8_C(0xfd);a->art=base;break;
        case 60:case 94:case 158:case 166:a->art=base+1u;break;
        case 64:case 98:case 162:case 170:a->art=base;break;
        case 226:a->art=base+2u;break;
        case 234:a->flags|=2u;a->art=base+3u;break;
        default:break;
        }
        if(++a->timer>241)a->timer=0;
    }
    if(!(a->variant&1u) && (a->state&0xf0)==0x10){
        uint8_t phase=0;
        switch(a->palette_timer){
        case 0:case 170:phase=1u;break;
        case 178:phase=2u;break;
        case 186:phase=3u;break;
        case 194:phase=4u;break;
        case 202:phase=5u;break;
        case 210:phase=6u;break;
        case 218:phase=7u;break;
        case 222:phase=8u;break;
        default:break;
        }
        if(phase)palette_phase[a->variant/2u]=phase;
        if(++a->palette_timer>241)a->palette_timer=0;
    }
    return 1;
}
static inline int portable_thundercloud_course_init(PortableThundercloudCourse *course,
    const PortableThundercloud *initial,size_t count)
{
    if(!course || count>PORTABLE_THUNDERCLOUD_CAPACITY || (count && !initial))return 0;
    *course=(PortableThundercloudCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
