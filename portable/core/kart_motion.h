#ifndef MKSC_PORTABLE_KART_MOTION_H
#define MKSC_PORTABLE_KART_MOTION_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_KART_MOTION_OK = 0,
    PORTABLE_KART_MOTION_INVALID_ARGUMENT = 1,
    PORTABLE_KART_MOTION_TABLE_TOO_SMALL = 2,
};

typedef struct
{
    int32_t speed_q16;
    int32_t acceleration_q16;
    int16_t angle;
    int16_t component_x;
    int16_t component_y;
} PortableKartMotionState;

/*
 * The table is caller-owned because the original ROM table is a separate
 * asset dependency.  sub_8038200 masks indexes to ten bits.
 */
int portable_kart_motion_tick(PortableKartMotionState *state,
                              const int32_t *sine_table,
                              size_t sine_count);

#endif
