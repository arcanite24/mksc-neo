#ifndef PORTABLE_BOO_ATTACK_RENDERER_H
#define PORTABLE_BOO_ATTACK_RENDERER_H
#include "race_particle_asset.h"
#include "software_frame.h"
int portable_boo_attack_render(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,uint8_t facing,uint8_t inverted);
#endif
