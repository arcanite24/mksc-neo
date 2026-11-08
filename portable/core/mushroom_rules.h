#ifndef PORTABLE_MUSHROOM_RULES_H
#define PORTABLE_MUSHROOM_RULES_H
#include <stdint.h>
/* A9AC duration;48138 single actor;482B8 golden owner;8298 mode8.
 * Mode8 acceleration and speed also govern A2B4's surface turbo. */
typedef struct {
    uint16_t boost_ticks, single_actor_ticks, golden_actor_ticks, acceleration_units;
    uint32_t max_speed_q16;
} PortableMushroomRules;
static inline int portable_mushroom_rules_valid(const PortableMushroomRules *rules)
{
    return rules!=0&&rules->boost_ticks!=0u&&rules->golden_actor_ticks!=0u&&
        rules->acceleration_units!=0u&&rules->acceleration_units<=32767u&&
        rules->max_speed_q16!=0u&&rules->max_speed_q16<=UINT32_C(0x07e00000);
}
#endif
