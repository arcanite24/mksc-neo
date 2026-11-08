#ifndef PORTABLE_BOO_RULES_H
#define PORTABLE_BOO_RULES_H
#include <stdint.h>
/* 48694 theft protection;4886C approach/lock/departure and Star repulsion. */
typedef struct {
    uint16_t invisibility_ticks,lock_ticks,orbit_ticks,orbit_step,bob_step;
    uint32_t repel_velocity_q16,repel_gravity_q16;
} PortableBooRules;
static inline int portable_boo_rules_valid(const PortableBooRules *r)
{
    return r!=0&&r->invisibility_ticks!=0u&&r->invisibility_ticks<=32767u&&
        r->lock_ticks!=0u&&r->lock_ticks<=32767u&&r->orbit_ticks!=0u&&r->orbit_ticks<=255u&&
        r->orbit_step!=0u&&r->orbit_step<=32767u&&r->bob_step!=0u&&r->bob_step<=32767u&&
        r->repel_velocity_q16!=0u&&r->repel_velocity_q16<=0xffffffu&&
        r->repel_gravity_q16!=0u&&r->repel_gravity_q16<=0xffffffu;
}
#endif
