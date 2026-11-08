#ifndef PORTABLE_INPUT_DISPLAY_H
#define PORTABLE_INPUT_DISPLAY_H

#include <stdint.h>
#include "software_frame.h"

/* Optional race HUD overlay: the semantic driving buttons the simulation
 * received this tick plus the analog stick (or the digital direction when no
 * stick is held). Presentation only; draws into a native-unit view. */
void portable_input_display_render(PortableSoftwareFrame *view,int x,int y,
    uint16_t buttons,int16_t stick_x,int16_t stick_y);
/* Bottom-left of a full race view, right of the place numeral; anchored left. */
void portable_input_display_render_corner(PortableSoftwareFrame *frame,
    uint16_t buttons,int16_t stick_x,int16_t stick_y);

#endif
