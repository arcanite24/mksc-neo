#ifndef PORTABLE_GREEN_SHELL_RULES_H
#define PORTABLE_GREEN_SHELL_RULES_H
#include <stdint.h>
/* 4676C release;6FF4 CPU hold;4BA0 flight;7884/7848 destruction. */
typedef struct {
    uint16_t launch_speed_bonus,max_speed,collision_grace_ticks,wall_contact_budget,release_lock_ticks,cpu_hold_ticks;
    uint32_t destruction_velocity_q16,destruction_gravity_q16;
} PortableGreenShellRules;
static inline int portable_green_shell_rules_valid(const PortableGreenShellRules *r)
{
    return r!=0&&r->launch_speed_bonus<=32767u&&r->max_speed!=0u&&r->max_speed<=255u&&
        r->wall_contact_budget!=0u&&r->wall_contact_budget<=7u&&
        r->release_lock_ticks!=0u&&r->release_lock_ticks<=32767u&&r->cpu_hold_ticks!=0u&&r->cpu_hold_ticks<=32767u&&
        r->destruction_velocity_q16!=0u&&r->destruction_velocity_q16<=0xffffffu&&
        r->destruction_gravity_q16!=0u&&r->destruction_gravity_q16<=0xffffffu;
}
#endif
