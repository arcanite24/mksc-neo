#ifndef MKSC_PORTABLE_DRIVER_TRACK_RESPONSE_H
#define MKSC_PORTABLE_DRIVER_TRACK_RESPONSE_H

#include <stdint.h>

#include "driver_material.h"

enum
{
    PORTABLE_DRIVER_TRACK_RESPONSE_OK = 0,
    PORTABLE_DRIVER_TRACK_RESPONSE_INVALID_ARGUMENT = 1,
};

enum
{
    PORTABLE_DRIVER_TRACK_RESPONSE_BOUNDARY = 1u << 0,
    PORTABLE_DRIVER_TRACK_RESPONSE_OFF_TRACK = 1u << 1,
    PORTABLE_DRIVER_TRACK_RESPONSE_TRANSITION = 1u << 2,
};

typedef struct
{
    uint32_t flags;
    uint32_t source_flags;
    uint8_t surface_cell;
    uint8_t surface_cell_valid;
    uint8_t surface_cell_changed;
} PortableDriverTrackResponseState;

/*
 * Carry the source sub_803B98C state boundary without applying guessed
 * friction, damage or checkpoint side effects. source_flags mirrors the
 * source state+0x48 boundary/off-track bits, while flags adds the native
 * surface-transition observation at bit 2.
 */
int portable_driver_track_response_tick(
    const PortableDriverMaterialState *material,
    PortableDriverTrackResponseState *state);

#endif
