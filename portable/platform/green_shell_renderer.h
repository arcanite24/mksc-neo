#ifndef PORTABLE_GREEN_SHELL_RENDERER_H
#define PORTABLE_GREEN_SHELL_RENDERER_H

#include <stdint.h>

#include "race_particle_asset.h"
#include "software_frame.h"

/* Render one source green-shell cell from the common race-effect OBJ sheet.
 * The six source cells are _0807ABB4.._0807ABDC. An inverse scale of 0 or 0x100 is
 * unscaled, matching oam_renderCellData. */
int portable_green_shell_render(
    const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t animation_index,
    uint16_t scale);

/* Source 437D4/49B0C inverse coefficient: depth multiplied by 192/256. */
int portable_green_shell_render_projected(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint8_t animation, uint16_t depth);

#endif
