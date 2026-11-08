#ifndef PORTABLE_GP_FINISH_BANNER_RENDERER_H
#define PORTABLE_GP_FINISH_BANNER_RENDERER_H

#include "gp_finish_banner_asset.h"
#include "software_frame.h"

int portable_gp_finish_banner_render(
    const PortableGpFinishBannerAssetView *asset,
    PortableSoftwareFrame *frame,uint8_t variant,uint16_t affine_scale,
    int32_t origin_x,int32_t origin_y);

#endif
