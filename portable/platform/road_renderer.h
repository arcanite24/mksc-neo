#ifndef MKSC_PORTABLE_ROAD_RENDERER_H
#define MKSC_PORTABLE_ROAD_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#include "camera_affine.h"
#include "road_map_asset.h"
#include "software_frame.h"
#include "sky_motion.h"
#include "track_tileset_asset.h"

enum
{
    PORTABLE_ROAD_RENDER_OK = 0,
    PORTABLE_ROAD_RENDER_INVALID_ARGUMENT,
    PORTABLE_ROAD_RENDER_CAPACITY,
};

typedef struct
{
    PortableRoadMapView map;
    PortableTrackTilesetView tileset;
} PortableRoadView;

/* Sample the source affine BG into the visible scanline range 0x40..0x9f. */
int portable_road_render(const PortableRoadView *road,
                         const PortableCameraAffineLine *lines,
                         size_t line_count,
                         PortableSoftwareFrame *frame);

/* AF74 displays affine record y-2 from line74 in ordinary races and results. */
int portable_road_render_result(const PortableRoadView *road,
                                const PortableCameraAffineLine *lines,
                                size_t line_count,
                                PortableSoftwareFrame *frame);

/* 12E8/AF74: retained VCount shifts the DMA handoff and affine row index. */
int portable_road_render_result_offset(const PortableRoadView *road,
    const PortableCameraAffineLine *lines, size_t line_count,
    PortableSoftwareFrame *frame, uint8_t scanline_offset);

/* Source AF74 handoff and per-course BG composition, resolved once per tick. */
int portable_road_render_motion(const PortableRoadView *road,
    const PortableCameraAffineLine *lines,size_t line_count,
    PortableSoftwareFrame *frame,const PortableSkyMotionSnapshot *motion);

#endif
