#ifndef PORTABLE_SHELL_ORBIT_RULES_H
#define PORTABLE_SHELL_ORBIT_RULES_H
#include <stdint.h>
/* Shared46DE4/46F5C orbit; radius multiplies the imported Q15 sine. */
typedef struct { uint32_t radius_multiplier,angle_step,animation_period; } PortableShellOrbitRules;
static inline int portable_shell_orbit_rules_valid(const PortableShellOrbitRules *r)
{return r!=0 && r->radius_multiplier>0u && r->radius_multiplier<=255u &&
    r->angle_step>0u && r->angle_step<=65535u && r->animation_period>0u && r->animation_period<=65535u;}
#endif
