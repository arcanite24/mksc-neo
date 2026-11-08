#ifndef PORTABLE_FINISH_CAMERA_H
#define PORTABLE_FINISH_CAMERA_H
#include <stddef.h>
#include <stdint.h>
#include "sky_motion.h"

typedef struct {
    uint16_t orbit_step,orbit_end,front_hold_ticks,final_hold_ticks;
    uint8_t eligible_rank_max;
} PortableFinishCameraRules;
typedef struct { uint8_t frame,duration,next; } PortableFinishRow;
typedef struct { uint8_t count;const PortableFinishRow *rows; } PortableFinishSchedule;
typedef struct {
    PortableFinishCameraRules camera;
    PortableFinishSchedule schedules[8];
} PortableFinishRules;
typedef struct {
    int32_t x_q16,y_q16;
    uint16_t yaw;
    int8_t vertical_offset;
    uint8_t active,request_celebration,cancel_effects;
} PortableFinishCameraSnapshot;
typedef struct {
    PortableFinishCameraSnapshot snapshot;
    int32_t orbit_yaw,hold_ticks;
    uint8_t phase;
} PortableFinishCamera;
typedef struct {
    int32_t driver_x_q16,driver_y_q16,height_q16;
    int32_t normal_x_q16,normal_y_q16;
    /* Source camera3b0/3b4 are integer basis coefficients, not world poses. */
    int32_t offset_x,offset_y,yaw_bias;
    uint16_t driver_angle_56,normal_yaw,mode_flags_7da;
    uint8_t driver_mode_78,rank,material_85,bob_phase,paused;
} PortableFinishCameraInput;

static inline int portable_finish_camera_rules_valid(const PortableFinishCameraRules *r)
{
    return r && r->orbit_step && r->orbit_step<=32768u && r->orbit_end &&
        r->orbit_end<=32768u && r->front_hold_ticks && r->front_hold_ticks<=32767u &&
        r->final_hold_ticks && r->final_hold_ticks<=32767u && r->eligible_rank_max<=7u;
}
static inline int portable_finish_schedule_valid(const PortableFinishSchedule *table)
{
    if(!table || !table->rows || !table->count || table->count>32u)return 0;
    for(unsigned i=0;i<table->count;++i){
        const PortableFinishRow *row=&table->rows[i];
        if(row->frame>=34u || row->duration>127u ||
           (row->next!=255u && row->next>=table->count) ||
           (!row->duration && row->next!=255u))return 0;
    }
    return table->rows[0].duration!=0u;
}
static inline int portable_finish_rules_valid(const PortableFinishRules *r)
{
    if(!r || !portable_finish_camera_rules_valid(&r->camera))return 0;
    for(unsigned i=0;i<8u;++i)if(!portable_finish_schedule_valid(&r->schedules[i]))return 0;
    return 1;
}
static inline void portable_finish_camera_start(PortableFinishCamera *state)
{
    if(state)*state=(PortableFinishCamera){.phase=8u};
}
static inline int32_t portable_finish_signed32(uint32_t value)
{
    return value<=INT32_MAX?(int32_t)value:
        (int32_t)((int64_t)value-INT64_C(0x100000000));
}
static inline int32_t portable_finish_add(int32_t a,int32_t b)
{ return portable_finish_signed32((uint32_t)a+(uint32_t)b); }
static inline int32_t portable_finish_mul(int32_t a,int32_t b)
{ return portable_finish_signed32((uint32_t)a*(uint32_t)b); }
static inline void portable_finish_camera_position(PortableFinishCamera *state,
    const PortableFinishCameraInput *in,const int32_t *sine)
{
    const unsigned slot=((int16_t)in->driver_angle_56/64)&1023u;
    const int32_t sn=sine[slot],cs=sine[(256u-slot)&1023u];
    const int32_t dx=portable_finish_add(portable_finish_mul(in->offset_x,cs),
        portable_finish_mul(in->offset_y,sn))/2;
    const int32_t dy=portable_finish_signed32((uint32_t)portable_finish_mul(in->offset_x,sn)-
        (uint32_t)portable_finish_mul(in->offset_y,cs))/2;
    state->snapshot.x_q16=portable_finish_add(in->driver_x_q16,dx);
    state->snapshot.y_q16=portable_finish_add(in->driver_y_q16,dy);
}
/* B4BC after drivers. Before start/missing data, the ordinary camera owns output. */
static inline int portable_finish_camera_tick(PortableFinishCamera *state,
    const PortableFinishCameraRules *rules,const PortableFinishCameraInput *in,
    const int32_t *sine,size_t sine_count)
{
    if(!rules)return 0;
    if(!state || !in || !portable_finish_camera_rules_valid(rules))return -1;
    if(in->paused)return 1;
    state->snapshot.request_celebration=state->snapshot.cancel_effects=0u;
    if(!state->phase)return 0;
    if(state->phase==8u){
        state->snapshot=(PortableFinishCameraSnapshot){.x_q16=in->normal_x_q16,
            .y_q16=in->normal_y_q16,.yaw=in->normal_yaw,
            .vertical_offset=portable_sky_motion_camera_offset(in->height_q16,in->material_85,in->bob_phase)};
        if(in->driver_mode_78)return 1;
        state->orbit_yaw=0;state->hold_ticks=0;state->snapshot.active=1;
        const unsigned mode=in->mode_flags_7da&0x0f00u;
        if(mode==0x100u || mode==0x800u)state->phase=9;
        else if(mode==0x200u)state->phase=10;
        else if(mode==0x400u)state->phase=15;
        else return -1;
        return 1;
    }
    if(state->phase>=9u && state->phase<=11u){
        /* BBC0 preserves a zero vertical offset during finish continuation. */
        if(state->snapshot.vertical_offset)
            state->snapshot.vertical_offset=portable_sky_motion_camera_offset(in->height_q16,in->material_85,in->bob_phase);
        if(state->phase!=9u || in->rank>rules->eligible_rank_max)return 1;
        if(!sine || sine_count<1024u)return -1;
        if(state->orbit_yaw<rules->orbit_end){
            portable_finish_camera_position(state,in,sine);
            state->orbit_yaw=portable_finish_add(state->orbit_yaw,rules->orbit_step);
            state->snapshot.yaw=(uint16_t)portable_finish_add(state->orbit_yaw,in->yaw_bias);
        }else{
            state->orbit_yaw=rules->orbit_end;
            state->snapshot.yaw=(uint16_t)portable_finish_add(state->orbit_yaw,in->yaw_bias);
            state->phase=12;
        }
        return 1;
    }
    if(state->phase==12u){
        state->hold_ticks=rules->front_hold_ticks;
        state->snapshot.request_celebration=1;state->phase=13;return 1;
    }
    if(state->phase==13u){
        if(--state->hold_ticks<=0){state->hold_ticks=rules->final_hold_ticks;state->phase=14;return 1;}
        if(!sine || sine_count<1024u)return -1;
        portable_finish_camera_position(state,in,sine);
        state->snapshot.yaw=(uint16_t)((uint32_t)in->driver_angle_56+(uint32_t)state->orbit_yaw);
        return 1;
    }
    if(state->phase==14u){
        if(--state->hold_ticks<=0){state->snapshot.cancel_effects=1;state->phase=18;}
        return 1;
    }
    if(state->phase==15u){
        if(state->snapshot.vertical_offset)
            state->snapshot.vertical_offset=portable_sky_motion_camera_offset(in->height_q16,in->material_85,in->bob_phase);
        if((in->mode_flags_7da&0x0f00u)==0x100u){
            /* B4BC BF84: standings follow the autonomous driver, with a
             * retained yaw accumulator advancing by128 per simulation tick. */
            if(!sine || sine_count<1024u)return -1;
            portable_finish_camera_position(state,in,sine);
            state->orbit_yaw=portable_finish_add(state->orbit_yaw,128);
            state->snapshot.yaw=(uint16_t)portable_finish_add(state->orbit_yaw,in->yaw_bias);
        }
        return 1;
    }
    return state->phase==18u?1:-1;
}

/* AA10 constructor: mode2 takes the art channel; cached frame and flip bit0 survive. */
static inline int portable_finish_animation_request(uint8_t *mode,uint8_t *phase,
    uint8_t *flags,const PortableFinishSchedule *table)
{
    if(!table)return 0;
    if(!mode || !phase || !flags || !portable_finish_schedule_valid(table))return -1;
    if((*mode&15u)==2u)return 0;
    *mode=2u;*phase=(uint8_t)(table->rows[0].duration<<1u);*flags&=UINT8_C(0xfd);
    return 1;
}
/* A90C runs before the source body's new projection, consuming prior distance. */
static inline int portable_finish_animation_tick(uint8_t *mode,uint8_t *phase,
    uint8_t *frame,const PortableFinishSchedule *table,uint8_t view,uint16_t distance)
{
    if(!table)return 0;
    if(!mode || !phase || !frame || !portable_finish_schedule_valid(table))return -1;
    if((*mode&15u)!=2u)return 0;
    const uint16_t packed=(uint16_t)*mode | (uint16_t)((uint16_t)*phase<<8u);
    const unsigned current=(packed>>4u)&31u;
    if(current>=table->count)return -1;
    if((view!=63u && view!=64u) || distance>511u){
        *mode=2u;*phase=(uint8_t)(table->rows[0].duration<<1u);return 1;
    }
    const uint8_t remaining=(uint8_t)((packed>>9u)-1u);
    if(remaining){*phase=(uint8_t)((*phase&1u)|(remaining<<1u));return 1;}
    const unsigned next=table->rows[current].next;
    if(next==255u){*mode=0;*phase=0;return 1;}
    const PortableFinishRow *row=&table->rows[next];*frame=row->frame;
    if(!row->duration){*mode=0;*phase=0;return 1;}
    const uint16_t updated=(uint16_t)(2u|(next<<4u));
    *mode=(uint8_t)updated;*phase=(uint8_t)((updated>>8u)|(row->duration<<1u));
    return 1;
}
#endif
