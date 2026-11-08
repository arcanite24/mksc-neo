#ifndef PORTABLE_BANANA_RENDERER_H
#define PORTABLE_BANANA_RENDERER_H

#include <stdint.h>

#include "race_particle_asset.h"
#include "software_frame.h"

/* Render the source banana cell at _0807AC44 from the common race-effect
 * OBJ sheet. An inverse scale of 0 or 0x100 is unscaled, matching the source OAM path. */
int portable_banana_render(
    const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint16_t scale);

/* Source 437D4/49B0C inverse coefficient: depth multiplied by 192/256. */
int portable_banana_render_projected(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint16_t depth);

#endif
