#ifndef MKSC_PORTABLE_RACE_PARTICLE_RENDERER_H
#define MKSC_PORTABLE_RACE_PARTICLE_RENDERER_H

#include <stdint.h>

#include "race_particle_asset.h"
#include "software_frame.h"

/* Age 1 is the source creation boundary and draws nothing. Later ages
 * traverse sub_805B44C duration+1 spans without mutating the view. */
int portable_race_particle_render(
    const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint8_t age);

/* Source BDC4 halves the local inverse affine scale (128 doubles the art). */
int portable_race_particle_render_scaled(
    const PortableRaceParticleAssetView *asset, PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y, uint8_t age, uint16_t scale_q8);

/* Rendering-only native-pixel Q16 anchors; art remains nearest-neighbor.
 * Integer entrypoints above retain source placement at native resolution. */
int portable_race_particle_render_positioned(
    const PortableRaceParticleAssetView *asset,PortableSoftwareFrame *frame,
    int32_t x_q16,int32_t y_q16,uint8_t age,uint16_t inverse_q8);

#endif
