#ifndef MKSC_PORTABLE_TRACK_Y_H
#define MKSC_PORTABLE_TRACK_Y_H

#include <stdint.h>

/*
 * The original race-state pointer is replaced by a native view whose only
 * pointer is the host address of an imported, read-only track blob.  The
 * serialized track offsets inside that blob remain 32-bit little-endian data.
 */
typedef struct
{
    const uint8_t *track_data;
    uint16_t nr_checkpoints;
} PortableRaceView;

uint32_t portable_track_read_y(const PortableRaceView *race,
                               uint32_t index,
                               uint8_t row);

#endif
