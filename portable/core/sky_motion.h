#ifndef PORTABLE_SKY_MOTION_H
#define PORTABLE_SKY_MOTION_H

#include <stddef.h>
#include <stdint.h>

enum { PORTABLE_SKY_MOTION_STATIC, PORTABLE_SKY_MOTION_CLOCK_SWAY,
    PORTABLE_SKY_MOTION_SPEED_PAN, PORTABLE_SKY_MOTION_RISE,
    PORTABLE_SKY_MOTION_SUNSET, PORTABLE_SKY_MOTION_LEADER_SHAKE,
    PORTABLE_SKY_MOTION_CLOCK_MID, PORTABLE_SKY_MOTION_WATER_SWAY };
enum { PORTABLE_SKY_BLEND_NONE, PORTABLE_SKY_BLEND_ALPHA };
enum { PORTABLE_SKY_ROAD_FRONT_ALPHA, PORTABLE_SKY_ROAD_OCCLUDES_FRONT,
    PORTABLE_SKY_ROAD_FRONT_OPAQUE, PORTABLE_SKY_ROAD_ALPHA_OCCLUDES_FRONT };

typedef struct
{
    uint8_t controller, yaw_shift[3], layer_mask, blend_mode, blend_a, blend_b;
    int16_t x_bias[3], y_bias[3];
    int16_t split_y;
    uint8_t split_layer, split_yaw_shift;
    int16_t split_x_bias;
    uint8_t lightning_hide_layer;
    uint8_t road_mode;
    uint8_t layer_source[3];
} PortableSkyMotionRules;

typedef struct
{
    int16_t scroll_x[3], scroll_y[3];
    uint8_t layer_mask, blend_mode, blend_a, blend_b;
    int16_t split_y;
    uint8_t split_layer;
    int16_t split_scroll_x;
    int16_t split_scroll_y;
    uint8_t split_vertical;
    int16_t road_line; /* 74 + current camera offset; actor/replay compatibility. */
    uint8_t road_mode;
    uint8_t layer_source[3], initialized;
    /* Derived presentation coordinates; original integer fields remain the
     * native-pixel and replay contract. Never used by simulation decisions. */
    int32_t scroll_x_q16[3], scroll_y_q16[3];
    int32_t split_scroll_x_q16, split_scroll_y_q16;
    uint8_t precise;
    /* 312E8 installs the preceding boundary while publishing the current
     * camera offset. Zero valid retains older hand-built snapshot behavior. */
    int16_t draw_road_line;
    int32_t draw_road_line_q16;
    uint8_t draw_road_line_valid;
} PortableSkyMotionSnapshot;

/* A translated source texel covers pixels before the ceiling of its edge.
 * Sky composition and road sampling must switch on that same physical row. */
static inline int portable_sky_motion_road_first_row(
    const PortableSkyMotionSnapshot *motion, unsigned scale)
{
    if (!motion || scale < 1u || scale > 8u || motion->precise > 1u ||
        motion->draw_road_line_valid > 1u)
        return -1;
    if (!motion->draw_road_line_valid)
        return motion->road_line < 0 || motion->road_line > 160 ? -1 : motion->road_line * (int)scale;
    if (motion->draw_road_line < 0 || motion->draw_road_line > 160 ||
        motion->draw_road_line_q16 < 0 || motion->draw_road_line_q16 > 160 * 65536)
        return -1;
    if (scale == 1u || !motion->precise)
        return motion->draw_road_line * (int)scale;
    return (int)(((int64_t)motion->draw_road_line_q16 * scale + 65535) / 65536);
}

typedef struct
{
    uint32_t clock;
    uint16_t yaw;
    int8_t vertical_offset;
    uint16_t selected_progress, leader_progress;
    uint8_t leader_route_index, leader_route_wrap;
    uint16_t scene_flags_7fe;
    uint8_t lightning_phase;
    uint8_t paused, source_gate;
    int32_t vertical_offset_q16;
    uint8_t precise_vertical_offset;
} PortableSkyMotionInputs;

typedef struct
{
    uint8_t initialized, phase;
    int8_t prior_vertical_offset;
    int32_t prior_vertical_offset_q16;
    int32_t elapsed, transition_counter, horizon_y;
    uint32_t transition_clock;
    int16_t raw_scroll_y[3];
    int32_t raw_scroll_y_q16[3], horizon_y_q16;
    PortableSkyMotionSnapshot snapshot;
} PortableSkyMotion;

/* Explicit signed halfword conversion; source divisions truncate to zero. */
static inline int32_t portable_sky_motion_signed16(uint32_t bits)
{ return bits&0x8000u?(int32_t)(bits&0xffffu)-65536:(int32_t)(bits&0xffffu); }

static inline int16_t portable_sky_motion_wrap16(int32_t value)
{ return (int16_t)portable_sky_motion_signed16((uint32_t)value); }

/* Q16 coordinates wrap with the same source halfword pixel period. */
static inline int32_t portable_sky_motion_wrap_q16(int64_t value)
{
    const uint32_t bits = (uint32_t)(uint64_t)value;
    return bits <= INT32_MAX ? (int32_t)bits : -1 - (int32_t)(UINT32_MAX - bits);
}

static inline int32_t portable_sky_motion_sine(const int32_t *sine,
    uint32_t angle)
{
    const int32_t signed_angle=portable_sky_motion_signed16(angle);
    return sine[(unsigned)(signed_angle/64)&1023u];
}

/* B4BC normal-camera branch. Positive height uses half-unit pixels. */
static inline int8_t portable_sky_motion_camera_offset(int32_t height_q16,
    uint8_t material_85, uint8_t bob_phase)
{
    if(height_q16>0){
        const int32_t offset=height_q16/32768;
        return (int8_t)(offset>24?24:offset);
    }
    return (material_85&0x1eu)>12u?(int8_t)(1u-(bob_phase&1u)):0;
}

static inline int32_t portable_sky_motion_camera_offset_q16(int32_t height_q16,
    uint8_t material_85, uint8_t bob_phase)
{
    if (height_q16 > 0)
        return height_q16 >= 12 * 65536 ? 24 * 65536 : height_q16 * 2;
    return portable_sky_motion_camera_offset(height_q16, material_85, bob_phase) * 65536;
}

static inline int32_t portable_sky_motion_floor(int64_t value,unsigned shift)
{
    const int64_t divisor=INT64_C(1)<<shift;
    return (int32_t)(value>=0?value/divisor:-((-value+divisor-1)/divisor));
}

static inline int portable_sky_motion_rules_valid(const PortableSkyMotionRules *r)
{
    if(!r || r->controller>PORTABLE_SKY_MOTION_WATER_SWAY || r->layer_mask>7u ||
       r->blend_mode>PORTABLE_SKY_BLEND_ALPHA || r->blend_a>16u || r->blend_b>16u ||
       r->split_layer>2u || r->split_yaw_shift>15u || r->lightning_hide_layer>2u ||
       r->split_y < -1 || r->split_y>159 ||
       r->road_mode>PORTABLE_SKY_ROAD_ALPHA_OCCLUDES_FRONT)return 0;
    for(unsigned i=0;i<3u;++i)
        if(r->yaw_shift[i]>15u || r->layer_source[i]>2u)return 0;
    return 1;
}

/* 616C shares its transition clock with palette lanes, but the motion
 * retains that timing independently of the optional ambient animation. */
static inline void portable_sky_motion_sunset_advance(PortableSkyMotion *state,
    const PortableSkyMotionInputs *in,const int32_t *sine)
{
    const unsigned lap=(in->leader_route_wrap&15u)+1u;
    switch(state->phase){
    case 0:
        if(lap==1u && in->leader_route_index>85u)++state->phase;
        break;
    case 1:
        if(++state->transition_counter>3){
            state->transition_counter=0;
            if(state->elapsed<=31){
                const uint32_t angle=(uint32_t)(state->elapsed*16384/31);
                state->horizon_y=184+portable_sky_motion_floor(
                    -(int64_t)portable_sky_motion_sine(sine,angle)*40,15u);
                state->horizon_y_q16=(int32_t)(INT64_C(184)*65536-
                    (int64_t)portable_sky_motion_sine(sine,angle)*80);
                ++state->elapsed;
            }else{state->horizon_y=144;state->horizon_y_q16=144*65536;state->elapsed=0;++state->phase;}
        }
        break;
    case 2:
        if(lap==2u && in->leader_route_index>85u)++state->phase;
        break;
    case 3:
        if(++state->elapsed<=128){
            state->horizon_y=144+portable_sky_motion_floor(
                -(int64_t)portable_sky_motion_sine(sine,(uint32_t)state->elapsed<<7u)*96,15u);
            state->horizon_y_q16=(int32_t)(INT64_C(144)*65536-
                (int64_t)portable_sky_motion_sine(sine,(uint32_t)state->elapsed<<7u)*192);
        }else{state->horizon_y=48;state->horizon_y_q16=48*65536;state->elapsed=32;++state->phase;}
        break;
    case 4:
        if(++state->transition_counter>3){
            state->transition_counter=0;
            if(state->elapsed<=63)++state->elapsed;
            else{state->elapsed=0;++state->phase;}
        }
        break;
    case 5:
        if(++state->elapsed<=64){
            state->horizon_y=48+portable_sky_motion_floor(
                -(int64_t)portable_sky_motion_sine(sine,(uint32_t)state->elapsed<<8u)*16,15u);
            state->horizon_y_q16=(int32_t)(INT64_C(48)*65536-
                (int64_t)portable_sky_motion_sine(sine,(uint32_t)state->elapsed<<8u)*32);
        }else{state->horizon_y=32;state->horizon_y_q16=32*65536;++state->phase;}
        break;
    default:break;
    }
}

/* 31E14 resolves before contacts/drivers. A source gate preserves the
 * callback's cached coordinates; its prior camera-offset handoff still runs. */
static inline int portable_sky_motion_resolve(PortableSkyMotion *state,
    const PortableSkyMotionRules *rules,const PortableSkyMotionInputs *in,
    const int32_t *sine,size_t sine_count,int advance)
{
    if(!rules)return 0;
    if(!state || !in || !portable_sky_motion_rules_valid(rules))return -1;
    if(in->paused)return 1;
    const int needs_sine=rules->controller==PORTABLE_SKY_MOTION_CLOCK_SWAY ||
        rules->controller==PORTABLE_SKY_MOTION_SPEED_PAN ||
        rules->controller==PORTABLE_SKY_MOTION_SUNSET ||
        rules->controller==PORTABLE_SKY_MOTION_WATER_SWAY;
    if(needs_sine && (!sine || sine_count!=1024u))return -1;
    if(!state->initialized){
        *state=(PortableSkyMotion){.initialized=1u,.horizon_y=184,.horizon_y_q16=184*65536};
        state->snapshot.split_y=-1;
    }
    PortableSkyMotionSnapshot *out=&state->snapshot;
    if(!in->source_gate){
        const int32_t yaw=portable_sky_motion_signed16(in->yaw);
        int64_t x[3],y[3],x_q16[3],y_q16[3];
        for(unsigned i=0;i<3u;++i){
            x[i]=yaw/(INT32_C(1)<<rules->yaw_shift[i])+rules->x_bias[i];
            y[i]=rules->y_bias[i];
            x_q16[i]=(int64_t)yaw*65536/(INT64_C(1)<<rules->yaw_shift[i])+
                (int64_t)rules->x_bias[i]*65536;
            y_q16[i]=(int64_t)rules->y_bias[i]*65536;
            out->layer_source[i]=rules->layer_source[i];
        }
        out->layer_mask=rules->layer_mask;
        if(in->lightning_phase==2u)
            out->layer_mask&=(uint8_t)~(1u<<rules->lightning_hide_layer);
        out->blend_mode=rules->blend_mode;
        out->blend_a=rules->blend_a;out->blend_b=rules->blend_b;
        out->road_mode=rules->road_mode;
        out->split_y=rules->split_y;out->split_layer=rules->split_layer;
        out->split_vertical=0u;out->split_scroll_y=0;out->split_scroll_y_q16=0;
        out->split_scroll_x=portable_sky_motion_wrap16(
            yaw/(INT32_C(1)<<rules->split_yaw_shift)+rules->split_x_bias);
        out->split_scroll_x_q16=portable_sky_motion_wrap_q16(
            (int64_t)yaw*65536/(INT64_C(1)<<rules->split_yaw_shift)+
            (int64_t)rules->split_x_bias*65536);
        switch(rules->controller){
        case PORTABLE_SKY_MOTION_CLOCK_SWAY:{
            const int32_t angle=portable_sky_motion_signed16(in->clock<<8u)/64;
            x[0]+=sine[(unsigned)(256-angle)&1023u]/2048;
            x[1]+=(in->clock&2047u)>>2u;
            y[1]+=portable_sky_motion_sine(sine,in->clock<<9u)/8192;
            x_q16[0]+=(int64_t)sine[(unsigned)(256-angle)&1023u]*32;
            x_q16[1]+=(int64_t)(in->clock&2047u)*16384;
            y_q16[1]+=(int64_t)portable_sky_motion_sine(sine,in->clock<<9u)*8;
            break;
        }
        case PORTABLE_SKY_MOTION_SPEED_PAN:{
            int32_t pan_x=0,pan_y=0;
            int64_t pan_x_q16=0,pan_y_q16=0;
            x[0]=rules->x_bias[0];
            x_q16[0]=(int64_t)rules->x_bias[0]*65536;
            if(state->phase==1u){
                pan_x=state->elapsed/8;pan_y=state->elapsed/32;
                pan_x_q16=(int64_t)state->elapsed*8192;
                pan_y_q16=(int64_t)state->elapsed*2048;
                if(advance && ++state->elapsed>1024){
                    state->phase=2u;state->transition_clock=in->clock;
                }
            }else if(state->phase==2u){
                const uint32_t difference=in->clock-state->transition_clock;
                pan_x=128+(int32_t)((difference&4095u)>>3u);
                pan_y=32+portable_sky_motion_sine(sine,difference<<9u)/16384;
                pan_x_q16=INT64_C(128)*65536+(int64_t)(difference&4095u)*8192;
                pan_y_q16=INT64_C(32)*65536+(int64_t)portable_sky_motion_sine(sine,difference<<9u)*4;
            }else if(advance && (int32_t)(in->selected_progress>>8u)-127>2){
                state->phase=1u;state->elapsed=0;
            }
            x[1]=2*(yaw/(INT32_C(1)<<rules->yaw_shift[1]))+rules->x_bias[1]+pan_x;
            y[1]+=pan_y;
            x_q16[1]=2*((int64_t)yaw*65536/(INT64_C(1)<<rules->yaw_shift[1]))+
                (int64_t)rules->x_bias[1]*65536+pan_x_q16;
            y_q16[1]+=pan_y_q16;
            break;
        }
        case PORTABLE_SKY_MOTION_RISE:
            if(!state->phase){
                if(advance){
                    if(!(in->scene_flags_7fe&0x100u) || ++state->elapsed>300){
                        state->elapsed=0;state->phase=1u;
                    }
                }
            }else{
                y[0]=state->elapsed>7200?32:rules->y_bias[0]-(int64_t)state->elapsed*320/7200;
                y_q16[0]=state->elapsed>7200?INT64_C(32)*65536:
                    (int64_t)rules->y_bias[0]*65536-(int64_t)state->elapsed*320*65536/7200;
                if(advance && state->elapsed<=7200)++state->elapsed;
            }
            break;
        case PORTABLE_SKY_MOTION_SUNSET:
            y[2]=state->horizon_y-2;
            y_q16[2]=(int64_t)state->horizon_y_q16-2*65536;
            if(advance)portable_sky_motion_sunset_advance(state,in,sine);
            /* 6570 sees the phase after 616C advances it. Its retained
             * line and BG3 Y both use 31E14's preceding camera offset. */
            if(state->phase>4u){
                out->split_y=(int16_t)(24+state->prior_vertical_offset);
                out->split_layer=2u;out->split_vertical=1u;
                out->split_scroll_y=(int16_t)(46-state->prior_vertical_offset);
                out->split_scroll_y_q16=portable_sky_motion_wrap_q16(
                    INT64_C(46)*65536-state->prior_vertical_offset_q16);
            }
            break;
        case PORTABLE_SKY_MOTION_LEADER_SHAKE:
            if(advance && !state->phase && (int32_t)(in->leader_progress>>8u)-127>1)
                state->phase=1u;
            if(state->phase){x[1]+=(in->clock&1u)?1:-1;
                x_q16[1]+=(in->clock&1u)?65536:-65536;}
            break;
        case PORTABLE_SKY_MOTION_CLOCK_MID:
            x[1]+=in->clock>>4u;
            x_q16[1]+=(int64_t)in->clock*4096;
            break;
        case PORTABLE_SKY_MOTION_WATER_SWAY:
            x[1]+=(in->clock&2047u)>>2u;
            y[1]+=portable_sky_motion_sine(sine,in->clock<<9u)/8192;
            x_q16[1]+=(int64_t)(in->clock&2047u)*16384;
            y_q16[1]+=(int64_t)portable_sky_motion_sine(sine,in->clock<<9u)*8;
            break;
        default:break;
        }
        for(unsigned i=0;i<3u;++i){
            /* Source stores halfwords, so narrowing wraps before rendering. */
            out->scroll_x[i]=portable_sky_motion_wrap16((int32_t)(x[i]%65536));
            state->raw_scroll_y[i]=portable_sky_motion_wrap16((int32_t)(y[i]%65536));
            out->scroll_x_q16[i]=portable_sky_motion_wrap_q16(x_q16[i]);
            state->raw_scroll_y_q16[i]=portable_sky_motion_wrap_q16(y_q16[i]);
        }
    }
    for(unsigned i=0;i<3u;++i){
        out->scroll_y[i]=portable_sky_motion_wrap16(
            (int32_t)state->raw_scroll_y[i]-state->prior_vertical_offset);
        out->scroll_y_q16[i]=portable_sky_motion_wrap_q16(
            (int64_t)state->raw_scroll_y_q16[i]-state->prior_vertical_offset_q16);
    }
    out->precise=1u;
    out->road_line=(int16_t)(74+in->vertical_offset);
    out->draw_road_line=(int16_t)(74+state->prior_vertical_offset);
    out->draw_road_line_q16=portable_sky_motion_wrap_q16(
        INT64_C(74)*65536+state->prior_vertical_offset_q16);
    out->draw_road_line_valid=1u;
    out->initialized=1u;
    if(advance){
        state->prior_vertical_offset=in->vertical_offset;
        state->prior_vertical_offset_q16=in->precise_vertical_offset?
            in->vertical_offset_q16:(int32_t)in->vertical_offset*65536;
    }
    return 1;
}

static inline int portable_sky_motion_tick(PortableSkyMotion *state,
    const PortableSkyMotionRules *rules,const PortableSkyMotionInputs *in,
    const int32_t *sine,size_t sine_count)
{ return portable_sky_motion_resolve(state,rules,in,sine,sine_count,1); }

static inline int portable_sky_motion_init(PortableSkyMotion *state,
    const PortableSkyMotionRules *rules,const PortableSkyMotionInputs *in,
    const int32_t *sine,size_t sine_count)
{
    if(!rules)return 0;
    if(!state)return -1;
    *state=(PortableSkyMotion){0};
    return portable_sky_motion_resolve(state,rules,in,sine,sine_count,0);
}

#endif
