#include "boo_attack.h"
#include <limits.h>
static int32_t signed32(uint32_t v)
{return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v);}
static int32_t signed16(uint16_t v)
{return v<=INT16_MAX?(int32_t)v:-1-(int32_t)(UINT16_MAX-v);}
static unsigned angle_index(uint16_t angle)
{return (unsigned)(signed16(angle)/64)&1023u;}
static void orbit(PortableBooAttack *a,const PortableBooAttackTarget *t,const int32_t *sine)
{
    const unsigned index=angle_index(a->angle);
    /* 487E4 truncates both signed products toward zero, independently.
     * The transcribed C misplaced the sine-index assignment inside a branch. */
    const int32_t x=signed32((uint32_t)sine[(256u-index)&1023u]*
        (uint32_t)signed16(a->radius)*3u)/16;
    const int32_t y=signed32((uint32_t)sine[index]*
        (uint32_t)signed16(a->radius)*3u)/16;
    a->x_q16=signed32((uint32_t)t->x_q16+(uint32_t)x);
    a->y_q16=signed32((uint32_t)t->y_q16+(uint32_t)y);
    a->frame=(uint32_t)(uint16_t)(a->angle-t->heading)-UINT32_C(0x4001)<=UINT32_C(0x7fff);
}
int portable_boo_attack_tick(PortableBooAttack *a,PortableBooAttackTarget *t,
    const int32_t *sine,size_t count,const PortableBooRules *rules)
{
    if(!portable_boo_rules_valid(rules))return 0;
    if(!a||!t||!sine||count<1024u||!a->occupied||a->phase>7u)return 0;
    switch(a->phase){
    case 0:
        a->phase=1u;a->affine_y_q8=256;a->radius=0u;
        a->angle=0u;a->bob_angle=0u;a->timer=rules->orbit_ticks;a->visible=1u;
        /* fall through */
    case 1:
        --a->timer;++a->radius;a->angle=(uint16_t)(a->angle+rules->orbit_step);
        orbit(a,t,sine);
        a->z_q16=signed32((uint32_t)t->z_q16+(uint32_t)signed16(a->timer)*8192u+0x60000u);
        if(signed16(a->timer)<=0){
            if(t->protection>0)a->phase=6u;
            else{t->lock_ticks=rules->lock_ticks;a->phase=2u;}
        }break;
    case 2:a->phase=3u; /* fall through */
    case 3:
        a->angle=(uint16_t)(a->angle+rules->orbit_step);orbit(a,t,sine);
        a->z_q16=signed32((uint32_t)t->z_q16+(uint32_t)sine[angle_index(a->bob_angle)]*4u+0x60000u);
        a->bob_angle=(uint16_t)(a->bob_angle+rules->bob_step);
        if(t->protection>0)a->phase=6u;
        else if(signed16(t->lock_ticks)<=0)a->phase=4u;
        break;
    case 4:a->timer=0u;a->phase=5u; /* fall through */
    case 5:
        ++a->timer;--a->radius;a->angle=(uint16_t)(a->angle+rules->orbit_step);orbit(a,t,sine);
        a->z_q16=signed32((uint32_t)t->z_q16+(uint32_t)signed16(a->timer)*8192u+
            (uint32_t)sine[angle_index(a->bob_angle)]*4u+0x60000u);
        if(signed16(a->timer)>=(int32_t)rules->orbit_ticks)a->occupied=a->visible=0u;
        break;
    case 6:{
        const unsigned index=angle_index(a->angle);
        a->phase=7u;a->affine_y_q8=-256;
        a->vx_q16=signed32((uint32_t)t->component_x*256u+(uint32_t)sine[(256u-index)&1023u]);
        a->vy_q16=signed32((uint32_t)t->component_y*256u+(uint32_t)sine[index]);
        a->vz_q16=(int32_t)rules->repel_velocity_q16;
    }
    /* fall through */
    case 7:
        a->x_q16=signed32((uint32_t)a->x_q16+(uint32_t)a->vx_q16);
        a->y_q16=signed32((uint32_t)a->y_q16+(uint32_t)a->vy_q16);
        a->z_q16=signed32((uint32_t)a->z_q16+(uint32_t)a->vz_q16);
        if(a->z_q16<0)a->occupied=a->visible=0u;
        else a->vz_q16=signed32((uint32_t)a->vz_q16-rules->repel_gravity_q16);
        break;
    }
    return 1;
}

int portable_boo_attack_music_transition(uint8_t before,uint8_t after)
{
    if(before<=1u&&after==2u)return 2;
    if((before==2u||before==3u)&&(after==4u||after==6u))return -2;
    return 0;
}
