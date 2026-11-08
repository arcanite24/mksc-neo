#ifndef PORTABLE_PIRANHA_H
#define PORTABLE_PIRANHA_H
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "course_prop.h"
#include "driver_hop.h"
#define PORTABLE_PIRANHA_CAPACITY 64u
/* 57074/570B8/57228: hidden class-8A Piranha Plant and contact bite. */
typedef struct {
    int32_t x_q16,y_q16,height_q16,velocity_z_q16;
    int32_t state,timer,phase;
    uint16_t affine_x_q8,affine_y_q8;
    uint8_t checkpoint,placement_order,flags,art,flip_y,struck_player;
} PortablePiranha;
typedef struct {
    PortablePiranha actors[PORTABLE_PIRANHA_CAPACITY];
    const PortablePiranha *initial;
    size_t count;
} PortablePiranhaCourse;
/* Unpositioned course-contact songs: piranha 373/378 and the E490 spin
 * squeal 162. Presentation requests are excluded from replay checksums. */
typedef struct {
    uint32_t serial;
    uint16_t songs[PORTABLE_PIRANHA_CAPACITY];
    uint8_t count;
} PortablePiranhaSounds;
static inline int32_t portable_piranha_signed(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);}
static inline int portable_piranha_init(PortablePiranha *a,int32_t x,int32_t y,
    uint8_t checkpoint,uint16_t variant)
{
    if(!a || x<0 || y<0 || x>2048*65536 || y>2048*65536 || checkpoint>=128u || variant)return 0;
    *a=(PortablePiranha){.x_q16=x,.y_q16=y,.checkpoint=checkpoint,.flags=2u,
        .affine_x_q8=224u,.affine_y_q8=224u};return 1;
}
static inline int portable_piranha_advance(PortablePiranha *a,uint32_t driver_flags,
    uint16_t *song)
{
    if(!a || !song || a->art>4u)return 0;
    *song=0u;
    switch(a->state){
    case 0:a->state=1;a->flags=(a->flags&~1u)|2u;break;
    case 1:break;
    case 16:
        a->state=17;a->timer=4;a->phase=0;a->flags=(a->flags|1u)&~2u;a->art=0u;
        /* fall through */
    case 17:
        if(a->timer<0 || a->timer>6 || a->phase<0 || a->phase>4)return 0;
        if(a->timer>0)--a->timer;
        else{
            ++a->phase;
            switch(a->phase){
            case 1:a->timer=3;a->art=1u;break;
            case 2:a->timer=6;a->art=2u;
                if(a->struck_player && !(driver_flags&0x20u))*song=378u;
                break;
            case 3:a->timer=4;a->art=4u;break;
            case 4:a->timer=2;a->art=3u;break;
            default:a->art=3u;a->state=0;break;
            }
        }
        break;
    case 240:
        a->state=241;a->flags&=UINT8_C(0xfd);a->flip_y=1u;
        a->affine_y_q8=256u;a->velocity_z_q16=0x30000;
        /* fall through */
    case 241:
        a->height_q16=portable_piranha_signed((uint32_t)a->height_q16+(uint32_t)a->velocity_z_q16);
        if(a->height_q16>=0){
            a->velocity_z_q16=portable_piranha_signed((uint32_t)a->velocity_z_q16-0x4000u);
            break;
        }
        /* fall through */
    case 242:a->state=243;a->flags&=UINT8_C(0xfe);break;
    case 243:break;
    default:return 0;
    }
    return 1;
}
static inline int portable_piranha_contact(PortablePiranha *a,uint16_t type,
    int16_t guard,int16_t protection,int is_player)
{
    if(!a || (type&0xf000u)!=0x2000u || guard || a->state==243)return 0;
    a->struck_player=(uint8_t)(is_player!=0);
    a->state=protection?240:16;return 1;
}
static inline int portable_piranha_course_init(PortablePiranhaCourse *course,
    const PortablePiranha *initial,size_t count)
{
    if(!course || count>PORTABLE_PIRANHA_CAPACITY || (count && !initial))return 0;
    *course=(PortablePiranhaCourse){.initial=initial,.count=count};
    for(size_t i=0;i<count;++i)course->actors[i]=initial[i];
    return 1;
}
/* Course actors that 8040A44 may find beside material 0x2A, with their
 * +0x34 flags. Placements never overlap a four-tile search, so the native
 * list order (plants, then common props) does not select between them. */
#define PORTABLE_COURSE_CAPTURE_ACTOR_CAPACITY \
    (PORTABLE_PIRANHA_CAPACITY + PORTABLE_COURSE_PROP_CAPACITY)
static inline size_t portable_course_capture_actors(const PortablePiranhaCourse *piranhas,
    const PortableCourseProps *props,PortableDriverCaptureActor *out,size_t capacity)
{
    size_t count=0u;
    if(!out)return 0u;
    for(size_t i=0u;piranhas && i<piranhas->count && count<capacity;++i)
        out[count++]=(PortableDriverCaptureActor){piranhas->actors[i].x_q16,
            piranhas->actors[i].y_q16,piranhas->actors[i].flags};
    for(size_t i=0u;props && i<props->count && count<capacity;++i)
        out[count++]=(PortableDriverCaptureActor){props->actors[i].x_q16,
            props->actors[i].y_q16,props->actors[i].flags};
    return count;
}
#endif
