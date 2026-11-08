#ifndef PORTABLE_GP_PROGRESS_RENDERER_H
#define PORTABLE_GP_PROGRESS_RENDERER_H

#include "frontend_flow.h"
#include "gp_progress_store.h"
#include "software_frame.h"

/* Presentation reads saved medals and best ratings; it never changes access. */
int portable_gp_progress_render(const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableGpProgress *progress,PortableSoftwareFrame *frame);

#endif
