#ifndef MKSC_PORTABLE_SOFTWARE_FRAME_H
#define MKSC_PORTABLE_SOFTWARE_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include "ui_text.h"
#include "../core/modern_drift.h"

/* Borrowed only for a world draw. Zero value disables depth writes. */
typedef struct
{
    uint32_t *pixels;
    size_t stride;
    uint32_t value;
    uint32_t *resolved; /* Optional resolved world Z, with the same stride. */
} PortableSoftwareDepth;

typedef struct
{
    uint32_t *pixels;
    size_t width; /* Logical art/UI dimensions; physical stride is width*scale. */
    size_t height;
    const PortableUiTextBank *ui_text;
    uint8_t scale; /* Zero/one: native pixels; two..eight: physical pixels per UI unit. */
    size_t stride; /* Physical pixels per row; zero means tightly packed. */
    uint8_t artwork_lod; /* Presentation only: 0 Original, 1 High, 2 Max authored detail. */
    PortableSoftwareDepth *depth;
} PortableSoftwareFrame;

static inline size_t portable_software_frame_scale(const PortableSoftwareFrame *frame)
{ return frame->scale>1u?frame->scale:1u; }
static inline size_t portable_software_frame_pixel_count(const PortableSoftwareFrame *frame)
{ const size_t scale=portable_software_frame_scale(frame);return frame->width*frame->height*scale*scale; }
static inline size_t portable_software_frame_stride(const PortableSoftwareFrame *frame)
{ return frame->stride!=0u?frame->stride:frame->width*portable_software_frame_scale(frame); }
static inline size_t portable_software_frame_physical_index(const PortableSoftwareFrame *frame,size_t index)
{ const size_t width=frame->width*portable_software_frame_scale(frame);
  return (index/width)*portable_software_frame_stride(frame)+index%width; }
static inline uint32_t portable_software_frame_read(const PortableSoftwareFrame *frame,size_t index)
{
    const size_t scale=portable_software_frame_scale(frame);
    return frame->pixels[(index/frame->width)*portable_software_frame_stride(frame)*scale+(index%frame->width)*scale];
}
/* Perspective samplers address physical pixels directly; UI/native art uses
 * store() below and deliberately retains integer source-pixel blocks. */
static inline void portable_software_frame_store_physical(
    PortableSoftwareFrame *frame,size_t x,size_t y,uint32_t color)
{
    if(frame->depth!=NULL && frame->depth->value!=0u){
        PortableSoftwareDepth *depth=frame->depth;
        if((color>>24u)==0u)return;
        uint32_t *stored=&depth->pixels[y*depth->stride+x];
        if(depth->value>*stored)return;
        *stored=depth->value;
        if(depth->resolved!=NULL)depth->resolved[y*depth->stride+x]=depth->value;
    }
    frame->pixels[y*portable_software_frame_stride(frame)+x]=color;
}
/* Indexed samplers already rejected their transparent index. Palette black
 * is still opaque; retain legacy RGB bytes when no world depth is active. */
static inline void portable_software_frame_store_rgb_physical(
    PortableSoftwareFrame *frame,size_t x,size_t y,uint32_t color)
{
    if(frame->depth!=NULL && frame->depth->value!=0u)color|=UINT32_C(0xff000000);
    portable_software_frame_store_physical(frame,x,y,color);
}
static inline void portable_software_frame_store(PortableSoftwareFrame *frame,size_t index,uint32_t color)
{
    if(frame->depth!=NULL && frame->depth->value!=0u){
        const size_t scale=portable_software_frame_scale(frame);
        const size_t first_x=index%frame->width*scale,first_y=index/frame->width*scale;
        for(size_t y=0u;y<scale;++y)for(size_t x=0u;x<scale;++x)
            portable_software_frame_store_physical(frame,first_x+x,first_y+y,color);
        return;
    }
    if(frame->scale<=1u&&frame->stride==0u){frame->pixels[index]=color;return;}
    const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
    const size_t first=(index/frame->width)*stride*scale+(index%frame->width)*scale;
    for(size_t y=0u;y<scale;++y)for(size_t x=0u;x<scale;++x)
        frame->pixels[first+y*stride+x]=color;
}
static inline int64_t portable_software_frame_floor_div(int64_t value,int64_t divisor)
{ return value>=0?value/divisor:-((-value+divisor-1)/divisor); }
typedef enum { PORTABLE_FRAME_LEFT,PORTABLE_FRAME_CENTER,PORTABLE_FRAME_RIGHT } PortableFrameAnchor;
PortableSoftwareFrame portable_software_frame_native_view(const PortableSoftwareFrame *frame,PortableFrameAnchor anchor);
/* Copy native art into/out of a scaled surface for the shared menu handoff. */
void portable_software_frame_copy_native(const PortableSoftwareFrame *frame,uint32_t *pixels);
void portable_software_frame_add_half(PortableSoftwareFrame *frame,size_t index,uint32_t color);

int portable_software_frame_init(PortableSoftwareFrame *frame,
                                 uint32_t *pixels,
                                 size_t width,
                                 size_t height);
void portable_software_frame_clear(PortableSoftwareFrame *frame,
                                   uint32_t color);
void portable_software_frame_rect(PortableSoftwareFrame *frame,
                                  size_t x,
                                  size_t y,
                                  size_t width,
                                  size_t height,
                                  uint32_t color);
void portable_software_frame_text(PortableSoftwareFrame *frame,int x,int y,
                                  const char *text,uint32_t color);
uint32_t portable_software_frame_checksum(const PortableSoftwareFrame *frame);
int portable_software_frame_write_ppm(const PortableSoftwareFrame *frame,
                                      const char *path);
int portable_screen_capture_air(const PortableSoftwareFrame *frame,const char *mode,
    const PortableModernAirState *air);
int portable_screen_capture_drift(const PortableSoftwareFrame *frame,const char *mode,
    const PortableModernDriftState *drift);
/* Development smokes: with MKSC_SCREEN_DIR set, the first frame of each named
 * screen is written there as NAME.ppm and announced on stdout. Returns 0 only
 * when a due capture fails to write. */
int portable_screen_capture(const PortableSoftwareFrame *frame,const char *name);
/* A new application/capture run may explicitly reset process-wide first-frame
 * ownership. Existing files are then replaced on the next due capture. Never
 * reset between scenes or courses of one run. Storage also retires at exit. */
void portable_screen_capture_reset(void);
/* MKSC_MOTION_DIR: sixteen consecutive fixed-tick race frames, bounded per mode. */
int portable_race_motion_capture(const PortableSoftwareFrame *frame,const char *mode,uint32_t tick);
/* MKSC_SCREEN_DIR: first sample in each of eight bounded hop-height bands. */
int portable_race_hop_capture(const PortableSoftwareFrame *frame, const char *mode,
                              int32_t height_q16);

#ifdef PORTABLE_ENABLE_SDL3
/* Owned physical framebuffer/texture; logical art dimensions stay independent
 * of sampling resolution. Scene hosts retain their immutable presentation API. */
typedef struct {
    PortableSoftwareFrame frame;
    void *texture;
} PortableSoftwareRenderTarget;
int portable_software_render_target_resize(void *renderer,
    PortableSoftwareRenderTarget *target,size_t width,size_t height,uint8_t scale,int linear);
void portable_software_render_target_close(PortableSoftwareRenderTarget *target);
size_t portable_software_frame_window_width(void *renderer,uint16_t aspect_width);
int portable_software_frame_present(void *renderer,void *texture,
                                    PortableSoftwareFrame *frame,int fit);
#endif
#endif
