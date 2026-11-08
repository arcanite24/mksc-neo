#ifndef PORTABLE_GP_OPENING_RENDERER_H
#define PORTABLE_GP_OPENING_RENDERER_H
#include "gp_opening_asset.h"
#include "gp_entry.h"
#include "software_frame.h"
int portable_gp_opening_render(const PortableGpOpeningAssetView *asset,
    const PortableGpEntrySnapshot *entry,uint8_t engine_class,PortableSoftwareFrame *frame);
#endif
