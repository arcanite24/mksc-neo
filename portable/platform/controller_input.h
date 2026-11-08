#ifndef PORTABLE_CONTROLLER_INPUT_H
#define PORTABLE_CONTROLLER_INPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "settings.h"

typedef struct {
    int south, east;
    int dpad_left, dpad_right;
    int16_t left_x, left_trigger, right_trigger;
} PortableControllerSample;

/* Convert one standardized-controller sample to deterministic driving bits. */
uint16_t portable_controller_driving_buttons(const PortableControllerSample *);
uint16_t portable_controller_driving_buttons_deadzone(const PortableControllerSample *,uint16_t deadzone);

uint16_t portable_keyboard_driving_buttons(const bool *keys,size_t count,const PortableSettings *settings);
/* Player 2's layout (keys 8-15), without player 1's arrow/shift aliases. */
uint16_t portable_keyboard2_driving_buttons(const bool *keys,size_t count,const PortableSettings *settings);

#endif
