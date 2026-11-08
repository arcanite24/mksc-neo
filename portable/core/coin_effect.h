#ifndef MKSC_PORTABLE_COIN_EFFECT_H
#define MKSC_PORTABLE_COIN_EFFECT_H
#include <stdint.h>

enum { PORTABLE_COIN_EFFECT_SLOTS=32, PORTABLE_COIN_EFFECT_AUDIO_CAPACITY=64 };
typedef enum { PORTABLE_COIN_EFFECT_CAUSE_NONE=0, PORTABLE_COIN_EFFECT_CAUSE_HIT=1,
    PORTABLE_COIN_EFFECT_CAUSE_RETRIEVAL=2, PORTABLE_COIN_EFFECT_CAUSE_CONTACT=3,
    PORTABLE_COIN_EFFECT_CAUSE_CRUSH=4, PORTABLE_COIN_EFFECT_CAUSE_HAUNT=5 } PortableCoinEffectCause;
typedef enum { PORTABLE_COIN_EFFECT_NONE=0, PORTABLE_COIN_EFFECT_BOUNCE,
    PORTABLE_COIN_EFFECT_RISE, PORTABLE_COIN_EFFECT_DROP_EMITTER,
    PORTABLE_COIN_EFFECT_RISE_EMITTER } PortableCoinEffectKind;
typedef struct {
    int32_t horizontal_step[9],upward_speed_q16[9];
    uint16_t bounce_volumes[3],script_ticks[4]; /* pickup/drop/rise/rise-emitter */
} PortableCoinEffectRules;
typedef struct {
    int32_t y_q16,speed_q16;
    int16_t x,y,depth;
    uint16_t inverse,age;
    uint8_t kind,variant,cause,counter,limit,bounces,visible;
} PortableCoinEffectSlot;
typedef struct { PortableCoinEffectSlot slots[PORTABLE_COIN_EFFECT_SLOTS]; } PortableCoinEffects;
typedef struct {
    PortableCoinEffectCause cause;
    uint32_t clock;
    int16_t x,y,inverse;
    uint8_t loss,remaining;
} PortableCoinEffectRequest;
typedef struct { uint16_t song_id,volume; } PortableCoinEffectAudio;
typedef struct { PortableCoinEffectAudio events[PORTABLE_COIN_EFFECT_AUDIO_CAPACITY];uint8_t count; } PortableCoinEffectAudioEvents;
static inline int32_t portable_coin_effect_signed32(uint32_t bits)
{return bits>=UINT32_C(0x80000000)?(int32_t)((int64_t)bits-INT64_C(4294967296)):(int32_t)bits;}
static inline int16_t portable_coin_effect_signed16(uint16_t bits)
{return (int16_t)(bits>=0x8000u?(int32_t)bits-65536:(int32_t)bits);}
static inline int32_t portable_coin_effect_asr16(int32_t value)
{return value>=0?value/65536:(int32_t)(-((-(int64_t)value+65535)/65536));}
static inline int portable_coin_effect_rules_valid(const PortableCoinEffectRules *rules)
{
    if(!rules)return 0;
    for(unsigned i=0u;i<9u;++i)
        if(rules->horizontal_step[i]<-64||rules->horizontal_step[i]>64||
           rules->upward_speed_q16[i]<0||rules->upward_speed_q16[i]>64*65536)return 0;
    for(unsigned i=0u;i<3u;++i)if(rules->bounce_volumes[i]>256u)return 0;
    for(unsigned i=0u;i<4u;++i)if(!rules->script_ticks[i]||rules->script_ticks[i]>8192u)return 0;
    return 1;
}
/* Source allocator chooses the lowest idle slot, replacing slot31 when full.
 * This bounded owner models coin-to-coin order; other presentation FX retain
 * their native pools rather than emulating the original global manager. */
static inline PortableCoinEffectSlot *portable_coin_effect_allocate(PortableCoinEffects *effects)
{
    unsigned i=0u;while(i<PORTABLE_COIN_EFFECT_SLOTS-1u&&effects->slots[i].kind)++i;
    effects->slots[i]=(PortableCoinEffectSlot){0};return &effects->slots[i];
}
static inline void portable_coin_effect_bounce(PortableCoinEffects *effects,
    const PortableCoinEffectRules *rules,int16_t x,int16_t y,int16_t depth,uint8_t variant,uint8_t cause)
{
    PortableCoinEffectSlot *slot=portable_coin_effect_allocate(effects);
    *slot=(PortableCoinEffectSlot){.x=x,.y=y,.y_q16=portable_coin_effect_signed32((uint32_t)(uint16_t)y<<16u),
        .depth=depth,.inverse=256u,.speed_q16=rules->upward_speed_q16[variant],
        .kind=PORTABLE_COIN_EFFECT_BOUNCE,.variant=variant,.cause=cause,.visible=1u};
}
/* D4E4 has already subtracted loss. Cause identity remains separate from the
 * amount: cause2 losing one coin still starts B8F4 rather than a bounce. */
static inline int portable_coin_effect_request(PortableCoinEffects *effects,
    const PortableCoinEffectRules *rules,const PortableCoinEffectRequest *request)
{
    if(!effects||!request||!portable_coin_effect_rules_valid(rules)||(unsigned)request->cause>PORTABLE_COIN_EFFECT_CAUSE_HAUNT||request->loss>8u)return 0;
    if(request->cause==PORTABLE_COIN_EFFECT_CAUSE_NONE||!request->loss)return 1;
    const uint8_t cause=(uint8_t)request->cause;
    int16_t x=request->x;
    const int16_t depth=portable_coin_effect_signed16((uint16_t)((uint16_t)request->inverse+1u));
    if(cause==3u||cause==5u){
        const uint8_t parity=(uint8_t)((cause==3u?request->clock:request->remaining)&1u);
        portable_coin_effect_bounce(effects,rules,x,request->y,depth,(uint8_t)(parity?4u:3u),cause);
    }else{
        if(cause==1u)x=portable_coin_effect_signed16((uint16_t)((uint16_t)x+4u-(request->clock&7u)));
        PortableCoinEffectSlot *slot=portable_coin_effect_allocate(effects);
        *slot=(PortableCoinEffectSlot){.kind=(uint8_t)(cause==2u?PORTABLE_COIN_EFFECT_RISE_EMITTER:PORTABLE_COIN_EFFECT_DROP_EMITTER),
            .x=x,.y=request->y,.depth=256,.inverse=256u,.cause=cause,.limit=request->loss};
    }
    return 1;
}
static inline int portable_coin_effect_audio(PortableCoinEffectAudioEvents *events,uint16_t song,uint16_t volume)
{
    if(events->count>=PORTABLE_COIN_EFFECT_AUDIO_CAPACITY)return 0;
    events->events[events->count++]=(PortableCoinEffectAudio){song,volume};return 1;
}
static inline int portable_coin_effect_tick(PortableCoinEffects *effects,
    const PortableCoinEffectRules *rules,int16_t current_inverse,PortableCoinEffectAudioEvents *audio)
{
    if(!effects||!audio||!portable_coin_effect_rules_valid(rules))return 0;
    audio->count=0u;
    for(unsigned i=0u;i<PORTABLE_COIN_EFFECT_SLOTS;++i){
        PortableCoinEffectSlot *slot=&effects->slots[i];
        if(slot->kind==PORTABLE_COIN_EFFECT_NONE)continue;
        if(slot->kind==PORTABLE_COIN_EFFECT_BOUNCE){
            if(slot->variant>8u)return 0;
            slot->inverse=(uint16_t)current_inverse;
            slot->x=portable_coin_effect_signed16((uint16_t)((uint32_t)(uint16_t)slot->x+
                (uint32_t)rules->horizontal_step[slot->variant]));
            slot->y_q16=portable_coin_effect_signed32((uint32_t)slot->y_q16-(uint32_t)slot->speed_q16);
            if(slot->y_q16>136*65536){
                slot->speed_q16=portable_coin_effect_signed32(0u-(uint32_t)slot->speed_q16);
                ++slot->bounces;
                if(slot->bounces>=1u&&slot->bounces<=3u &&
                   !portable_coin_effect_audio(audio,102u,rules->bounce_volumes[slot->bounces-1u]))return 0;
            }
            slot->y=portable_coin_effect_signed16((uint16_t)portable_coin_effect_asr16(slot->y_q16));
            slot->speed_q16=portable_coin_effect_signed32((uint32_t)slot->speed_q16-32768u);
            if(++slot->age>rules->script_ticks[slot->variant?1u:0u])slot->kind=PORTABLE_COIN_EFFECT_NONE;
        }else if(slot->kind==PORTABLE_COIN_EFFECT_RISE){
            if(slot->counter==3u)slot->visible=1u;
            slot->y=portable_coin_effect_signed16((uint16_t)((uint16_t)slot->y-
                (uint32_t)portable_coin_effect_asr16(slot->speed_q16)));
            slot->speed_q16=portable_coin_effect_signed32((uint32_t)slot->speed_q16+32768u);
            ++slot->counter;
            if(++slot->age>rules->script_ticks[2])slot->kind=PORTABLE_COIN_EFFECT_NONE;
        }else if(slot->kind==PORTABLE_COIN_EFFECT_RISE_EMITTER){
            if(slot->counter<(unsigned)slot->limit*2u){
                if(!(slot->counter&1u)){
                    const int16_t x=slot->x,y=slot->y,depth=slot->depth;const uint8_t cause=slot->cause;
                    PortableCoinEffectSlot *child=portable_coin_effect_allocate(effects);
                    *child=(PortableCoinEffectSlot){.kind=PORTABLE_COIN_EFFECT_RISE,.x=x,.y=y,.depth=depth,
                        .inverse=256u,.cause=cause};
                    if(!portable_coin_effect_audio(audio,101u,256u))return 0;
                }
                ++slot->counter;
            }
            if(++slot->age>rules->script_ticks[3])slot->kind=PORTABLE_COIN_EFFECT_NONE;
        }else if(slot->kind==PORTABLE_COIN_EFFECT_DROP_EMITTER){
            const uint8_t count=slot->counter,limit=(uint8_t)(slot->limit*4u+8u);
            if(count<limit){
                if(count>7u&&!(count&3u)){
                    const uint8_t variant=(uint8_t)((((count-4u)>>2u)-1u)&3u)+1u;
                    portable_coin_effect_bounce(effects,rules,slot->x,slot->y,slot->depth,variant,slot->cause);
                }
                ++slot->counter;
            }else slot->kind=PORTABLE_COIN_EFFECT_NONE;
        }else return 0;
    }
    return 1;
}
static inline uint64_t portable_coin_effect_digest(const PortableCoinEffects *effects)
{
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned i=0u;i<PORTABLE_COIN_EFFECT_SLOTS;++i){
        const PortableCoinEffectSlot *s=&effects->slots[i];
        const uint32_t values[]={(uint32_t)s->y_q16,(uint32_t)s->speed_q16,(uint16_t)s->x,(uint16_t)s->y,
            (uint16_t)s->depth,s->inverse,s->age,s->kind,s->variant,s->cause,s->counter,s->limit,s->bounces,s->visible};
        for(unsigned j=0u;j<sizeof values/sizeof values[0];++j)
            for(unsigned k=0u;k<4u;++k){hash^=(values[j]>>(k*8u))&255u;hash*=UINT64_C(1099511628211);}
    }
    return hash;
}
#endif
