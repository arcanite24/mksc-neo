#ifndef MKSC_PORTABLE_SPATIAL_AUDIO_PITCH_H
#define MKSC_PORTABLE_SPATIAL_AUDIO_PITCH_H

#include <stddef.h>
#include <stdint.h>
#include "driver_vector_effect.h"

enum { PORTABLE_SPATIAL_PITCH_SIDE=33, PORTABLE_SPATIAL_PITCH_COUNT=33*33 };
typedef struct
{
    int32_t x_q16,y_q16,speed;
    uint16_t heading,type;
} PortableSpatialAudioActor;

typedef struct
{
    int32_t x_q16,y_q16,speed_q16;
    uint16_t heading;
    uint8_t racing,finished;
    uint8_t fix_heading_wrap; /* Optional original-bug fix; zero keeps C234. */
} PortableSpatialAudioCamera;

static inline int32_t portable_spatial_audio_signed32(uint32_t bits)
{return bits<=INT32_MAX?(int32_t)bits:-1-(int32_t)(UINT32_MAX-bits);}

static inline int portable_spatial_audio_heading_within(uint16_t angle,uint16_t heading,uint8_t fix)
{
    const uint16_t bits=(uint16_t)(angle-heading);
    const int32_t delta=bits<=INT16_MAX?(int32_t)bits:(int32_t)bits-65536;
    const uint32_t distance=(uint32_t)(delta<0?-delta:delta);
    /* C234 narrows abs(-32768) back to a signed halfword before comparing, so
     * a source dead astern counts as ahead and its Doppler flips sign. The
     * optional fix treats exactly opposite as behind. */
    return (!fix&&distance==32768u) || distance<=0x4000u;
}

/* C234: source ArcTan2 direction, signed speed bins and EDD60 table. Item
 * callers supply byte71<<4 verbatim: it intentionally quantizes to zero. */
static inline int portable_spatial_audio_pitch(const PortableSpatialAudioCamera *camera,
    const PortableSpatialAudioActor *actor,const int16_t *table,size_t count,int16_t *pitch)
{
    if(!camera||!actor||!pitch)return 0;
    *pitch=0;
    if(!table && count==0u)return 1; /* Packs before material v12. */
    if(!table || count!=PORTABLE_SPATIAL_PITCH_COUNT)return 0;
    if((actor->type&0xf000u)!=0x2000u && (actor->type&0xff00u)!=0x8400u)return 1;
    const int32_t dx=portable_spatial_audio_signed32((uint32_t)camera->x_q16-(uint32_t)actor->x_q16)/0x100000;
    const int32_t dy=portable_spatial_audio_signed32((uint32_t)camera->y_q16-(uint32_t)actor->y_q16)/0x100000;
    uint16_t angle;
    if(portable_driver_vector_effect_source_angle((int16_t)dx,(int16_t)dy,&angle)!=0)return 0;
    int32_t source=actor->speed/0x800000;
    int32_t observer=camera->racing && !camera->finished?camera->speed_q16/0x800000:0;
    if(source>16)source=16;
    if(observer>16)observer=16;
    if(!portable_spatial_audio_heading_within(angle,actor->heading,camera->fix_heading_wrap))source=-source;
    if(!portable_spatial_audio_heading_within(angle,camera->heading,camera->fix_heading_wrap))observer=-observer;
    if(source < -16 || source>16 || observer < -16 || observer>16)return 0;
    *pitch=table[(observer+16)*PORTABLE_SPATIAL_PITCH_SIDE+source+16];
    return 1;
}

#endif
