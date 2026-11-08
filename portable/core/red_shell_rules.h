#ifndef PORTABLE_RED_SHELL_RULES_H
#define PORTABLE_RED_SHELL_RULES_H
#include <stdint.h>
/* Shared red/Spiny lifecycle:4676C,4D4C,5378,5910,5A3C,5C18,7884/7848. */
typedef struct {
    uint32_t launch_speed_bonus;
    uint32_t max_launch_speed;
    uint32_t collision_grace_ticks;
    uint32_t route_budget;
    uint32_t release_lock_ticks;
    uint32_t cpu_hold_ticks;
    uint32_t flight_speed_50;
    uint32_t flight_speed_100;
    uint32_t flight_speed_150;
    uint32_t acceleration;
    uint32_t deceleration;
    uint32_t trap_arm_ticks;
    uint32_t trap_lifetime_ticks;
    uint32_t trap_lock_ticks;
    uint32_t destruction_velocity_q16;
    uint32_t destruction_gravity_q16;
} PortableRedShellRules;
static inline int portable_red_shell_rules_valid(const PortableRedShellRules *r)
{
    return r!=0&&r->release_lock_ticks<=r->trap_arm_ticks&&
        r->launch_speed_bonus<=32767u&&
        r->max_launch_speed!=0u&&r->max_launch_speed<=255u&&
        r->collision_grace_ticks<=65535u&&
        r->route_budget!=0u&&r->route_budget<=255u&&
        r->release_lock_ticks!=0u&&r->release_lock_ticks<=32767u&&
        r->cpu_hold_ticks!=0u&&r->cpu_hold_ticks<=32767u&&
        r->flight_speed_50!=0u&&r->flight_speed_50<=255u&&
        r->flight_speed_100!=0u&&r->flight_speed_100<=255u&&
        r->flight_speed_150!=0u&&r->flight_speed_150<=255u&&
        r->acceleration!=0u&&r->acceleration<=255u&&
        r->deceleration!=0u&&r->deceleration<=255u&&
        r->trap_arm_ticks!=0u&&r->trap_arm_ticks<=32767u&&
        r->trap_lifetime_ticks!=0u&&r->trap_lifetime_ticks<=32767u&&
        r->trap_lock_ticks!=0u&&r->trap_lock_ticks<=32767u&&
        r->destruction_velocity_q16!=0u&&r->destruction_velocity_q16<=16777215u&&
        r->destruction_gravity_q16!=0u&&r->destruction_gravity_q16<=16777215u;
}
#endif
