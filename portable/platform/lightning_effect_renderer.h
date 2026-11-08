#ifndef PORTABLE_LIGHTNING_EFFECT_RENDERER_H
#define PORTABLE_LIGHTNING_EFFECT_RENDERER_H

#include "race_particle_asset.h"
#include "lightning_effect_asset.h"
#include "software_frame.h"

#include <stdint.h>

int portable_lightning_effect_render(const PortableLightningEffectAssetView *asset,
                                     const PortableRaceParticleAssetView *race_effects,
                                     PortableSoftwareFrame *frame, int32_t origin_x,
                                     int32_t origin_y, uint8_t frame_index);

int portable_lightning_effect_render_positioned(const PortableLightningEffectAssetView *asset,
                                                const PortableRaceParticleAssetView *race_effects,
                                                PortableSoftwareFrame *frame, int32_t origin_x_q16,
                                                int32_t origin_y_q16, uint8_t frame_index);

#endif
