#ifndef MKSC_PORTABLE_TRACK_OBJECT_RENDERER_H
#define MKSC_PORTABLE_TRACK_OBJECT_RENDERER_H

#include <stdint.h>

#include "software_frame.h"
#include "track_object_art.h"

/* Render the source cell data at a projected screen origin. Scale 0x100 is
 * the generic source actor's unscaled object path. */
int portable_track_object_render(
    const PortableTrackObjectArtView *art,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint16_t scale_x,
    uint16_t scale_y);

#endif
