#ifndef PORTABLE_FRONTEND_IMAGE_RENDERER_H
#define PORTABLE_FRONTEND_IMAGE_RENDERER_H
#include "frontend_presentation.h"
#include "frontend_motion_asset.h"
#include "native_image.h"

/* BG-only sampling for semi-transparent OBJ; other OBJ are not blend targets. */
typedef struct {
    const PortableNativeImage *pattern, *affine;
    int scroll_x, scroll_y, affine_a, affine_b;
    unsigned pattern_row;
} PortableMenuBackgroundLayers;
void portable_menu_background_layers_init(PortableMenuBackgroundLayers *layers,
    const PortableNativeImage *pattern,const PortableNativeImage *affine,
    const PortableFrontendFlow *flow,const PortableFrontendMotion *motion);
uint32_t portable_menu_background_layers_sample(const PortableMenuBackgroundLayers *layers,
    unsigned physical_x,unsigned physical_y,unsigned scale);
/* Signed centered source coordinates extend periodic layers into the margins. */
uint32_t portable_menu_background_layers_sample_at(const PortableMenuBackgroundLayers *layers,
    int64_t physical_x,int64_t physical_y,unsigned scale);

enum {
    PORTABLE_MENU_BACKGROUND = 0x21000,
    PORTABLE_MENU_HEADING = 0x21001,
    PORTABLE_MENU_PATTERN = 0x21002,
    PORTABLE_MENU_AFFINE = 0x21003,
    PORTABLE_MENU_WHEEL = 0x21004,
    PORTABLE_MENU_ORBIT = 0x21005,
    PORTABLE_MENU_BACK_PATTERN = 0x21006,
    PORTABLE_MENU_MODE = 0x21100,
    PORTABLE_MENU_CLASS = 0x21200,
    PORTABLE_MENU_TRIAL = 0x21300,
    PORTABLE_MENU_PLAYER_MEDALLION = 0x21400
};
PortableFrontendPresentationStatus portable_frontend_heading_draw(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,PortableSoftwareFrame *frame,
    unsigned group,uint32_t fallback_id,int fallback_x,int fallback_y);
PortableFrontendPresentationStatus portable_frontend_image_draw_affine(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    const PortableFrontendMotion *motion,int pivot_x,int pivot_y,int x,int y,
    unsigned inverse,unsigned angle,int right);
PortableFrontendPresentationStatus portable_frontend_image_draw_scaled(
    const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame,
    int x, int y, int left, int top, int right, int bottom, unsigned scale);
PortableFrontendPresentationStatus portable_frontend_image_draw(
    const PortableAssetPackView *pack, uint32_t id, PortableSoftwareFrame *frame,
    int x, int y, int left, int top, int right, int bottom);
/* Only indexed images consume this temporary palette; ARGB overrides retain
 * their supplied pixels. The mask names authored palette indices. */
PortableFrontendPresentationStatus portable_frontend_image_draw_palette(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    int x,int y,int left,int top,int right,int bottom,
    const uint16_t *palette,size_t palette_count,const uint8_t *palette_mask);
PortableFrontendPresentationStatus portable_frontend_image_draw_atlas(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    unsigned rows,unsigned row,int x,int y,int left,int top,int right,int bottom);
PortableFrontendPresentationStatus portable_menu_background_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    uint32_t pattern_id,uint32_t legacy_id,PortableSoftwareFrame *frame);
PortableFrontendPresentationStatus portable_frontend_pattern_draw(
    const PortableAssetPackView *pack,uint32_t id,PortableSoftwareFrame *frame,
    int offset_x,int offset_y);
PortableFrontendPresentationStatus portable_menu_render(
    const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
    PortableFrontendPage page, PortableSoftwareFrame *frame);
#endif
