#ifndef MKSC_PORTABLE_SKY_RENDERER_H
#define MKSC_PORTABLE_SKY_RENDERER_H

#include "sky_asset.h"
#include "sky_motion.h"
#include "software_frame.h"

int portable_sky_render(const PortableSkyAssetView *sky,
                        PortableSoftwareFrame *frame,
                        int32_t scroll_x,
                        int32_t scroll_y);

/* Draw only the middle layer when Lightning hides the distant sky. */
int portable_sky_render_middle(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame, int32_t scroll_x, int32_t scroll_y);
/* Legacy race profiles retain raw camera yaw for physical-pixel placement. */
int portable_sky_render_middle_yaw(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame, int16_t camera_yaw);

/* Race BG3 is the distant sky; BG2 is reused for the affine road. */
int portable_sky_render_race_back(const PortableSkyAssetView *sky,
                                  PortableSoftwareFrame *frame,
                                  int16_t camera_yaw);
int portable_sky_render_race_back_scrolled(const PortableSkyAssetView *sky,
                                          PortableSoftwareFrame *frame,
                                          int32_t back_x, int32_t middle_x,
                                          int32_t back_y, int32_t middle_y);
/* BG1 is in front of both BG2 and BG3 at their shared hardware priority. */
int portable_sky_render_race_front(const PortableSkyAssetView *sky,
                                   PortableSoftwareFrame *frame,
                                   int16_t camera_yaw);
int portable_sky_render_race_front_scrolled(const PortableSkyAssetView *sky,
                                           PortableSoftwareFrame *frame,
                                           int32_t front_x,
                                           int32_t scroll_y);
int portable_sky_render_race_front_scrolled_offset(
    const PortableSkyAssetView *sky, PortableSoftwareFrame *frame,
    int32_t front_x, int32_t scroll_y, uint8_t scanline_offset);

/* Source BG1 is the foreground layer and sits above the affine BG2 road. */
int portable_sky_render_front(const PortableSkyAssetView *sky,
                              PortableSoftwareFrame *frame,
                              int32_t scroll_x,
                              int32_t scroll_y);

/* Resolved immutable race motion. Draw back/middle before the road and front
 * afterward. Layer sources, horizontal split and optional front alpha are data.
 * At scale>1, precise snapshots retain fractional motion at physical pixels;
 * native1x and older snapshots use the original integer row sampler. */
int portable_sky_render_motion_back(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame,const PortableSkyMotionSnapshot *motion);
int portable_sky_render_motion_front(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame,const PortableSkyMotionSnapshot *motion);

#endif
