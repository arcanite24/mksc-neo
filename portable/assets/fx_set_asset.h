#ifndef MKSC_PORTABLE_FX_SET_ASSET_H
#define MKSC_PORTABLE_FX_SET_ASSET_H

#include <stddef.h>
#include <stdint.h>

#include "asset_pack.h"

/* Kind 116 FXS1 v1: the shadows and particle art a content platform's 3D
 * courses race with (META asset `mksc:fx-set` names the section). All words
 * are little-endian; lengths are metres at kart scale 1 (the host multiplies
 * by META mksc:kart-scale), speeds m/s, Q16.
 *
 *   0  "FXS1", u32 shadow count (<= 64), u32 style count (<= 8),
 *      u32 emitter count (<= 32)
 *   16 u32 ARGB drift tier colours [4] (charging, first..third mini-turbo)
 *   32 u32 ARGB smoke, dust, flame core, flame rim
 *   48 shadow rows, 32 bytes: u32 key (0: any racer; else a KRT3 kart ID or,
 *      for drivers without a kart, a driver ID), u32 kind-97 material whose
 *      texel alpha times the material alpha is the darkness, i32 half width,
 *      i32 half length, i32 centre ahead of the kart foot, i32 vanish height
 *      (floor gap at which it is gone), u32 flags (PORTABLE_FX_SHADOW_*), u32 zero
 *   style rows, 16 bytes: u32 slot (PortableFxStyleSlot, each at most once),
 *      u32 kind-97 material, u32 frames (1..16, a horizontal strip played over
 *      the particle's life), u32 flags (PORTABLE_FX_STYLE_*)
 *   emitter rows, 56 bytes: u32 ID (course objects' integer property
 *      `mksc:emitter`), u32 style slot, u32 interval (ticks, >= 1), u32 burst
 *      (particles, 1..16), u32 life (ticks, 1..600), i32 size, i32 growth
 *      (size multiplier gained over the life), i32 velocity [3] (object
 *      local: x right, y forward, z up), i32 random speed, i32 gravity
 *      (down), i32 spawn radius, u32 ARGB tint
 */
#define PORTABLE_FX_SET_SHADOW_LIMIT 64u
#define PORTABLE_FX_SET_STYLE_LIMIT 8u
#define PORTABLE_FX_SET_EMITTER_LIMIT 32u

typedef enum
{
    PORTABLE_FX_STYLE_GLOW = 0,   /* drift flares, mini-turbo flashes */
    PORTABLE_FX_STYLE_SPARK = 1,  /* drift sparks */
    PORTABLE_FX_STYLE_SMOKE = 2,  /* drift smoke, landing puffs */
    PORTABLE_FX_STYLE_FLAME = 3,  /* boost exhaust */
    PORTABLE_FX_STYLE_DUST = 4,   /* rough-ground dust */
    PORTABLE_FX_STYLE_SPLASH = 5, /* water: emitters */
    PORTABLE_FX_STYLE_MIST = 6,   /* waterfall mist, steam: emitters */
    PORTABLE_FX_STYLE_EMBER = 7,  /* lava and fire: emitters */
    PORTABLE_FX_STYLE_SLOTS
} PortableFxStyleSlot;

enum
{
    /* A shadow volume's cut: full size until the vanish height (otherwise
     * the decal shrinks away linearly). */
    PORTABLE_FX_SHADOW_VOLUME = 1u,
};

enum
{
    /* Adds light; otherwise straight alpha over the scene. */
    PORTABLE_FX_STYLE_ADDITIVE = 1u,
    /* The texture is the art's own colour; otherwise its intensity is tinted. */
    PORTABLE_FX_STYLE_COLOURED = 2u,
    /* Spark style only: one spark held at the drift's outer rear tyre,
     * renewed every six ticks, instead of flying sparks and tier glows. */
    PORTABLE_FX_STYLE_WHEEL = 4u,
};

typedef struct
{
    uint32_t key, material;
    int32_t half_width_q16, half_length_q16, forward_q16, vanish_q16;
    uint32_t flags; /* PORTABLE_FX_SHADOW_* */
} PortableFxShadow;

typedef struct
{
    uint32_t slot, material, frames, flags;
} PortableFxStyle;

typedef struct
{
    uint32_t id, slot, interval, burst, life;
    int32_t size_q16, grow_q16, velocity_q16[3], spread_q16, gravity_q16, radius_q16;
    uint32_t argb;
} PortableFxEmitter;

typedef struct
{
    uint32_t tier_argb[4], smoke_argb, dust_argb, flame_argb[2];
    uint32_t shadow_count, style_count, emitter_count;
    PortableFxShadow shadows[PORTABLE_FX_SET_SHADOW_LIMIT];
    PortableFxStyle styles[PORTABLE_FX_SET_STYLE_LIMIT];
    PortableFxEmitter emitters[PORTABLE_FX_SET_EMITTER_LIMIT];
} PortableFxSet;

/* 1 with a valid section, decoded into *out (no borrowed bytes). */
int portable_fx_set_open(const PortableAssetSectionView *section, PortableFxSet *out);
int portable_fx_set_find(const PortableAssetPackView *pack, uint32_t id, PortableFxSet *out);
/* The shadow for a racer: its kart's row, else its driver's, else the default
 * row (key 0); NULL when the set has none of them. */
const PortableFxShadow *portable_fx_set_shadow(const PortableFxSet *set, uint32_t kart_id, uint32_t driver_id);
const PortableFxStyle *portable_fx_set_style(const PortableFxSet *set, PortableFxStyleSlot slot);
const PortableFxEmitter *portable_fx_set_emitter(const PortableFxSet *set, uint32_t id);

#endif
