#ifndef PORTABLE_BOO_ATTACK_H
#define PORTABLE_BOO_ATTACK_H
#include <stddef.h>
#include <stdint.h>
#include "boo_rules.h"
#define PORTABLE_BOO_ATTACK_CAPACITY 16u
/* 48694 owns theft; 4886C owns this independent leader-targeted actor. */
typedef struct {
    int32_t x_q16,y_q16,z_q16,vx_q16,vy_q16,vz_q16;
    uint16_t angle,bob_angle,timer,radius;
    int16_t affine_y_q8;
    uint8_t phase,target,occupied,visible,frame;
} PortableBooAttack;
typedef struct {
    int32_t x_q16,y_q16,z_q16;
    int16_t component_x,component_y,protection;
    uint16_t heading,lock_ticks;
} PortableBooAttackTarget;
/* 4886C requests effect2 on successful approach; both normal departure
 * and Star repulsion from the orbit release it. Protected approach is silent. */
int portable_boo_attack_music_transition(uint8_t before,uint8_t after);
/* Writes the target's AC when the approach completes; never advances it. */
int portable_boo_attack_tick(PortableBooAttack *actor,
    PortableBooAttackTarget *target,const int32_t *sine,size_t sine_count,const PortableBooRules *rules);
#endif
