#include "gp_shell_orbit.h"
#include <limits.h>
#include <assert.h>
static int32_t signed_bits(uint32_t value)
{return value<=INT32_MAX?(int32_t)value:-1-(int32_t)(UINT32_MAX-value);}

void portable_gp_shell_orbit_position(PortableGpShellOrbitView shell,
    const PortableGpShellOrbitOwner *owner,const int32_t sine[1024])
{
    const PortableShellOrbitRules *rules=shell.rules;
    assert(portable_shell_orbit_rules_valid(rules));
    if(!portable_shell_orbit_rules_valid(rules))return;
    const int32_t angle=*shell.angle<=INT16_MAX?(int32_t)*shell.angle:(int32_t)*shell.angle-65536;
    /* Thumb adds63 before the signed shift: divide toward zero. */
    const int32_t index=angle/64;
    *shell.x_q16=signed_bits((uint32_t)owner->x_q16+
        (uint32_t)sine[(uint32_t)index&0x3ffu]*rules->radius_multiplier);
    *shell.y_q16=signed_bits((uint32_t)owner->y_q16-
        (uint32_t)sine[(0x100u-(uint32_t)index)&0x3ffu]*rules->radius_multiplier);
    *shell.z_q16=owner->z_q16;*shell.floor=owner->floor;
}

void portable_gp_shell_orbit_tick(PortableGpShellOrbitView shell,
    const PortableGpShellOrbitOwner *owner,const int32_t sine[1024])
{
    const PortableShellOrbitRules *rules=shell.rules;
    assert(portable_shell_orbit_rules_valid(rules));
    if(!portable_shell_orbit_rules_valid(rules))return;
    *shell.angle=(uint16_t)((uint32_t)*shell.tick*rules->angle_step);
    portable_gp_shell_orbit_position(shell,owner,sine);
    if(*shell.tick%rules->animation_period==0u){
        *shell.display=*shell.frame;
        *shell.frame=(uint8_t)((*shell.frame+1u)%6u);
    }
    ++*shell.tick;
    if(owner->mode==2u || ((owner->mode==3u || owner->mode==4u) && owner->z_q16==0))
        *shell.flags&=UINT16_C(0xfffc);
    else *shell.flags|=3u;
}
