#ifndef PORTABLE_RED_SHELL_RENDERER_H
#define PORTABLE_RED_SHELL_RENDERER_H

#include <stdint.h>
#include "race_particle_asset.h"
#include "software_frame.h"

int portable_red_shell_render(
    const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,int32_t origin_x,int32_t origin_y,
    uint8_t animation_index,uint16_t scale);
int portable_spiny_shell_render(const PortableRaceParticleAssetView *assets,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,uint8_t animation_index,uint16_t scale);

/* Source 437D4/49B0C inverse coefficient: depth multiplied by 192/256. */
int portable_red_shell_render_projected(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint8_t animation, uint16_t depth);
int portable_spiny_shell_render_projected(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint8_t animation, uint16_t depth);

#endif
