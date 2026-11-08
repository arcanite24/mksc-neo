#ifndef PORTABLE_NITRO_SPA_H
#define PORTABLE_NITRO_SPA_H

/* Nitro SPL particle resources (.spa, magic " APS" version "12_1"): emitter
 * descriptions and their texture bank. The layout follows the SPL library
 * as decompiled in pret/pokeplatinum (lib/spl). Only what the importer turns
 * into effect-set art and emitter rows is kept; the reader bounds every
 * table and fails naming the file and emitter. */

#include "nitro_g3d.h"

#define NITRO_SPA_EMITTER_LIMIT 256u
#define NITRO_SPA_TEXTURE_LIMIT 64u

typedef struct NitroSpaEmitter
{
    uint32_t flags;           /* SPLResourceHeader flags: draw type, optional blocks */
    uint32_t texture;         /* texture bank index */
    uint32_t tile_s, tile_t;  /* the quad repeats the texture 1 << tile times */
    uint32_t interval, life;  /* frames between emissions, particle life in frames */
    uint32_t alpha;           /* 0..31 */
    uint16_t color;           /* BGR555 */
    int32_t count;            /* particles per emission, fx (Q12) */
    int32_t radius;           /* spawn radius, fx world units */
    int32_t scale;            /* particle half size, fx world units */
    int32_t axis_speed;       /* initial speed along the axis, fx per frame */
    int32_t outward_speed;    /* initial speed away from the emitter, fx per frame */
    int16_t axis[3];          /* emission axis, Q12 */
    int16_t gravity[3];       /* fx per frame squared; zero without the block */
    int16_t scale_start, scale_end; /* Q12 scale animation ends; 4096 without the block */
} NitroSpaEmitter;

typedef struct NitroSpa
{
    const char *file;
    NitroSpaEmitter *emitters;
    uint32_t emitter_count;
    NitroTexture textures[NITRO_SPA_TEXTURE_LIMIT];
    NitroPalette palettes[NITRO_SPA_TEXTURE_LIMIT];
    uint32_t flips[NITRO_SPA_TEXTURE_LIMIT]; /* bit 0 mirror S, bit 1 mirror T on repeat */
    uint32_t texture_count;
} NitroSpa;

NitroSpa nitro_spa(RiCtx *ctx, RiData file, const char *name);
/* The emitter's texture as its quad shows it: tiled 1 << tile_s by
 * 1 << tile_t, each repeat mirrored where the texture flips. ARGB words. */
RiBuf *nitro_spa_emitter_argb(RiCtx *ctx, const NitroSpa *spa, uint32_t emitter, uint32_t *width, uint32_t *height);

#endif
