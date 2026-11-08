#ifndef PORTABLE_GP_COUNTDOWN_RENDERER_H
#define PORTABLE_GP_COUNTDOWN_RENDERER_H
#include "asset_pack.h"
#include "race_session.h"
#include "software_frame.h"

/* Snapshot-only presentation. The race remains the owner of its countdown. */
int portable_gp_countdown_render(const PortableAssetPackView *pack,
    PortableRacePhase phase,uint16_t duration,uint16_t remaining,
    uint32_t race_ticks,PortableSoftwareFrame *frame);
#endif
