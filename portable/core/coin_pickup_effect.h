#ifndef MKSC_PORTABLE_COIN_PICKUP_EFFECT_H
#define MKSC_PORTABLE_COIN_PICKUP_EFFECT_H

#include <stdint.h>

enum { PORTABLE_COIN_PICKUP_EFFECT_CAPACITY=32 };
/* 5B680 variant0/F2F2C/F2F50: fixed screen X, Q16 Y and upward speed.
 * Only the local pickup creates this27-tick F2DDC sequence. */
typedef struct
{
    int32_t y_q16, upward_speed_q16;
    int16_t x;
    uint8_t age;
} PortableCoinPickupEffect;

static inline void portable_coin_pickup_effect_start(PortableCoinPickupEffect *effect,
    int16_t x,int16_t y,uint32_t scene_clock)
{
    *effect=(PortableCoinPickupEffect){.x=(int16_t)(x-(int16_t)(scene_clock&7u)+4),
        .y_q16=(int32_t)y*65536,.upward_speed_q16=0x80000,.age=1u};
}

static inline void portable_coin_pickup_effect_tick(PortableCoinPickupEffect *effect)
{
    if(!effect->age)return;
    if(++effect->age>28u){effect->age=0u;return;}
    effect->y_q16-=effect->upward_speed_q16;
    if(effect->y_q16>136*65536)effect->upward_speed_q16=-effect->upward_speed_q16;
    effect->upward_speed_q16-=0x8000;
}

#endif
