#ifndef PORTABLE_GP_SHELL_ORBIT_H
#define PORTABLE_GP_SHELL_ORBIT_H

#include <stdint.h>
#include "shell_orbit_rules.h"

/* Native field views share46DE4/46F5C across green/red actor layouts.
 * These pointers address owned C fields; none are serialized. */
typedef struct {
    int32_t *x_q16,*y_q16,*z_q16;
    uint16_t *angle,*flags,*tick;
    uint8_t *floor,*frame,*display;
    const PortableShellOrbitRules *rules;
} PortableGpShellOrbitView;
typedef struct {
    int32_t x_q16,y_q16,z_q16;
    uint8_t floor,mode;
} PortableGpShellOrbitOwner;

void portable_gp_shell_orbit_position(PortableGpShellOrbitView shell,
    const PortableGpShellOrbitOwner *owner,const int32_t sine[1024]);
void portable_gp_shell_orbit_tick(PortableGpShellOrbitView shell,
    const PortableGpShellOrbitOwner *owner,const int32_t sine[1024]);
#endif
