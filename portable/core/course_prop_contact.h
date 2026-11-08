#ifndef PORTABLE_COURSE_PROP_CONTACT_H
#define PORTABLE_COURSE_PROP_CONTACT_H

#include "course_prop.h"
#include "driver_vector_effect.h"

/* B550/40810 runs after XY integration and before the hold timer and9B38.
 * Keep the cached EE/F0 anchor separate from the sampled world position. */
typedef struct {
    int32_t world_x,world_y,height_q16;
    int16_t anchor_x,anchor_y,component_x,component_y;
    uint16_t heading,timer;
    int16_t guard,protection;
    uint8_t material,hop_mode;
} PortableCoursePropContactInput;
typedef enum {
    PORTABLE_COURSE_PROP_CONTACT_NONE,
    PORTABLE_COURSE_PROP_CONTACT_BOUNCE,
    PORTABLE_COURSE_PROP_CONTACT_DAMAGE,
    PORTABLE_COURSE_PROP_CONTACT_ERROR
} PortableCoursePropContactKind;
typedef struct {
    PortableDriverObjectContactResult vector;
    uint32_t source_code;
    uint16_t callback_song;
    uint8_t owner_found;
} PortableCoursePropContactResult;

static inline PortableCoursePropContactKind portable_course_prop_tile_contact(
    PortableCourseProps *props,const PortableCoursePropContactInput *in,
    const int32_t *sine,size_t sine_count,PortableCoursePropContactResult *out)
{
    *out=(PortableCoursePropContactResult){0};
    if((in->material&0x90u)!=0x90u || in->height_q16>INT32_C(0x40000) ||
       in->timer>1u || in->hop_mode==6u || in->guard!=0)
        return PORTABLE_COURSE_PROP_CONTACT_NONE;
    PortableCourseProp *prop=portable_course_prop_at(props,in->world_x,in->world_y);
    if(prop){
        out->owner_found=1u;out->source_code=prop->source_code;
        /* Flight and removal leave the occupied terrain byte intact. */
        if(prop->state)return PORTABLE_COURSE_PROP_CONTACT_NONE;
    }
    if(in->protection){
        if(!prop)return PORTABLE_COURSE_PROP_CONTACT_NONE;
        portable_course_prop_damage(prop);
        return PORTABLE_COURSE_PROP_CONTACT_DAMAGE;
    }
    const int16_t x=(int16_t)((in->world_x/8)*8+4);
    const int16_t y=(int16_t)((in->world_y/8)*8+4);
    const int result=in->timer==1u
        ? portable_driver_object_contact_escape((int16_t)in->world_x,
            (int16_t)in->world_y,x,y,sine,sine_count,&out->vector)
        : portable_driver_object_contact_resultant(in->anchor_x,in->anchor_y,
            x,y,in->component_x,in->component_y,in->heading,
            sine,sine_count,&out->vector);
    if(result!=PORTABLE_DRIVER_VECTOR_EFFECT_OK)return PORTABLE_COURSE_PROP_CONTACT_ERROR;
    /* 40810 invokes the owner callback only on first contact, after40570.
     * 55FC0 toggles the beach prop independently of the driver's response. */
    if(prop && prop->source_code==UINT32_C(0x860000) && in->timer==0u){
        prop->contact_phase^=1u;
        out->callback_song=(uint16_t)(prop->contact_phase?376u:375u);
    }
    return PORTABLE_COURSE_PROP_CONTACT_BOUNCE;
}
#endif
