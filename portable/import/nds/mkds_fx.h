#ifndef PORTABLE_MKDS_FX_H
#define PORTABLE_MKDS_FX_H

/* The kind-116 effect set DS courses race with (META mksc:fx-set): every
 * kart's shadow (mkds_kart_shadow rows) and particle art from the race's SPL particle file (MainEffect.carc/RaceEffect.spa). */

#include "nds_fs.h"

#define MKDS_FX_SET_KIND 116u
#define MKDS_FX_SET_ID 0x4D4B4658u
#define MKDS_FX_MATERIAL_ID(slot) (0x4D4B4600u + (slot))

/* Adds the art materials and the FXS1 section. shadows holds shadow_count
 * 32-byte rows. */
void mkds_effects(RiCtx *ctx, const NdsFs *rom, const RiBuf *shadows, uint32_t shadow_count);

#endif
