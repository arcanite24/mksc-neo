#ifndef PORTABLE_BANANA_RULES_H
#define PORTABLE_BANANA_RULES_H
#include <stdint.h>
/*8090/7F1C hold;7C2C release;7AAC flight;7790/7848 destruction. */
typedef struct {
    uint32_t throw_speed_q16;
    uint32_t cpu_flight_ticks;
    uint32_t throw_velocity_q16;
    uint32_t throw_gravity_q16;
    uint32_t collision_grace_ticks;
    uint32_t release_lock_ticks;
    uint32_t cpu_hold_ticks;
    uint32_t destruction_velocity_q16;
    uint32_t destruction_gravity_q16;
} PortableBananaRules;
static inline int portable_banana_rules_valid(const PortableBananaRules *r)
{
    if(r==0) return 0;
    if(!(r->throw_speed_q16!=0u&&r->throw_speed_q16<=16777215u&&
        r->cpu_flight_ticks!=0u&&r->cpu_flight_ticks<=32767u&&
        r->throw_velocity_q16!=0u&&r->throw_velocity_q16<=16777215u&&
        r->throw_gravity_q16!=0u&&r->throw_gravity_q16<=16777215u&&
        r->collision_grace_ticks<=65535u&&
        r->release_lock_ticks!=0u&&r->release_lock_ticks<=32767u&&
        r->cpu_hold_ticks!=0u&&r->cpu_hold_ticks<=32767u&&
        r->destruction_velocity_q16!=0u&&r->destruction_velocity_q16<=16777215u&&
        r->destruction_gravity_q16!=0u&&r->destruction_gravity_q16<=16777215u))return 0;
    /* Lock must clear in flight, before landing can switch callbacks. */
    const int64_t ticks=r->release_lock_ticks;
    const int64_t peak=r->throw_velocity_q16/r->throw_gravity_q16+1u;
    const int64_t sample=peak<ticks?peak:ticks;
    if(INT64_C(65536)+sample*r->throw_velocity_q16-
        sample*(sample-1)*r->throw_gravity_q16/2>INT32_MAX)return 0;
    return INT64_C(65536)+ticks*r->throw_velocity_q16-
        ticks*(ticks-1)*r->throw_gravity_q16/2>0;
}
#endif
