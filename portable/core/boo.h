#ifndef PORTABLE_BOO_H
#define PORTABLE_BOO_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#define PORTABLE_BOO_CAPACITY 64u
/* Class8D, constructor56EE8/update56F60. Contact5704C is separate. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,base_height_q16,velocity_z_q16;
    uint32_t phase;
    uint32_t state;
    uint16_t heading;
    int16_t affine_y_q8;
    uint8_t checkpoint,placement_order,flags;
} PortableBoo;
typedef struct {
    PortableBoo actors[PORTABLE_BOO_CAPACITY];
    const PortableBoo *initial;
    size_t count;
} PortableBooCourse;
static inline int32_t portable_boo_signed(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);}
static inline int portable_boo_init(PortableBoo *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant>1u)return 0;
    *a=(PortableBoo){.x_q16=x,.y_q16=y,.checkpoint=checkpoint,.flags=3u,
        .affine_y_q8=256,.heading=0x4000u,.state=16u,.base_height_q16=0x80000};return 1;
}
static inline int portable_boo_advance(PortableBoo *a,const int32_t *sine,size_t count)
{
    if(!a || !sine || count<1024u)return 0;
    switch(a->state){
    case 16:a->state=17u; /* Source falls through on initialization. */
        /* fall through */
    case 17:{
        int32_t phase=(int32_t)(a->phase&0xffffu);
        if(phase>=32768)phase-=65536;
        if(phase<0)phase+=63;
        const uint32_t index=(uint16_t)phase/64u;
        a->height_q16=portable_boo_signed((uint32_t)sine[index]*3u+(uint32_t)a->base_height_q16);
        a->phase+=1024u;return 1;
    }
    case 240:a->state=241u;a->flags&=UINT8_C(0xfd);
        a->affine_y_q8=-256;a->velocity_z_q16=0x30000;
        /* fall through */
    case 241:
        a->height_q16=portable_boo_signed((uint32_t)a->height_q16+(uint32_t)a->velocity_z_q16);
        if(a->height_q16<0){a->state=243u;a->flags&=UINT8_C(0xfe);return 1;}
        a->velocity_z_q16=portable_boo_signed((uint32_t)a->velocity_z_q16-0x4000u);return 1;
    case 242:a->state=243u;a->flags&=UINT8_C(0xfe);return 1;
    case 243:return 1;
    default:return 0;
    }
}
static inline unsigned portable_boo_view_art(const PortableBoo *a,uint16_t camera_heading)
{return (uint16_t)(a->heading-camera_heading)>32767u?1u:0u;}
static inline int portable_boo_course_init(PortableBooCourse *course,
    const PortableBoo *initial,size_t count)
{
    if(!course || count>PORTABLE_BOO_CAPACITY || (count && !initial))return 0;
    *course=(PortableBooCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
#endif
