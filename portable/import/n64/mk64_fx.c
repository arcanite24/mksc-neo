#include "mk64_internal.h"

/* The kind-116 effect set MK64 courses race with (META mksc:fx-set): the
 * kart shadow and the drift, smoke, flame and dust particle art, with the
 * colours the game draws them in (n64decomp/mk64 58cfcb0).
 *
 * render_player_shadow draws gTextureKartShadow (64x64 I8, black at the
 * texel's intensity, G_RM_ZB_CLD_SURF) on an 18-unit square (D_800E51D0 and
 * D_800E5210, rows from +z to -z) scaled by gCharacterSize 0.75, one unit
 * above the floor; airborne it shrinks by 1 - 0.02 x the floor distance.
 * set_drift_particles colours drift sparks white, then yellow and orange by
 * driftState at alpha 0x70 (render_player_drift_particles, D_800E4770 art);
 * drift smoke is common_texture_particle_smoke[0..3] in primColors[0];
 * boost flames are common_texture_particle_fire; ground dust gGroundDust. */
#define KART_SHADOW 0x69A830u      /* standalone MIO0, 64x64 I8 */
#define DRIFT_SPARK 0x69B140u      /* standalone MIO0, 32x32 I8 (D_8018D424) */
#define GROUND_DUST 0x69C4E4u      /* standalone MIO0, 32x32 I8 */
#define COMMON_FIRE 0x200u         /* common data, 32x64 RGBA16 */
#define COMMON_SMOKE 0x2BC58u      /* common data, four 32x32 I8 frames */
#define SHADOW_HALF_UNITS_X100 675 /* 9 units x gCharacterSize 0.75 */
#define SHADOW_VANISH_UNITS 50     /* 1 - 0.02 x distance reaches zero */

enum { SLOT_SPARK = 1, SLOT_SMOKE = 2, SLOT_FLAME = 3, SLOT_DUST = 4, STYLE_COLOURED = 2, STYLE_WHEEL = 4 };

static uint32_t argb16(uint32_t v)
{
    const uint32_t r = (v >> 11) & 31, g = (v >> 6) & 31, b = (v >> 1) & 31;
    return (v & 1 ? 0xFF000000u : 0) | ((r << 3) | (r >> 2)) << 16 | ((g << 3) | (g >> 2)) << 8 | ((b << 3) | (b >> 2));
}

/* Kart-scale-1 metres of a length in source units: META mksc:kart-scale maps
 * the source sphere onto the 0.5 m stock kart. */
static int32_t kart_metres_x100(int64_t units_x100)
{
    return (int32_t)(units_x100 * 65536 * 5 / (MK64_KART_SPHERE_TENTHS * 100));
}

static void material(RiCtx *ctx, uint32_t id, uint32_t argb, uint32_t width, uint32_t height, RiBuf *pixels)
{
    RiBuf *image = ri_image_asset(ctx, width, height, pixels, 0, 0);
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MTL1", 4);
    ri_words(ctx, out, 3, argb, 0u, (uint32_t)image->size);
    ri_put_buf(ctx, out, image);
    ri_section(ctx, 97, id, 1, out);
    ri_buf_free(ctx, image);
    ri_buf_free(ctx, pixels);
}

/* I8 texels as intensity (white light at full alpha) or as coverage of black. */
static void i8_material(RiCtx *ctx, uint32_t id, const uint8_t *texels, uint32_t width, uint32_t height, int shadow)
{
    RiBuf *pixels = ri_buf_zero(ctx, (size_t)width * height * 4u);
    for (size_t i = 0; i < (size_t)width * height; ++i)
        ri_set_pixel(pixels, i, shadow ? (uint32_t)texels[i] << 24 : 0xFF000000u | texels[i] * 0x010101u);
    material(ctx, id, 0xFFFFFFFFu, width, height, pixels);
}

static RiBuf *standalone(RiCtx *ctx, uint32_t at, size_t size)
{
    RiBuf *out = mk64_mio0(ctx, ri_span(ctx, ctx->rom, at, ctx->rom.size - at));
    if (out->size < size)
        ri_fail(ctx, "MK64 effects: texture at %#x is %zu bytes, not %zu", at, out->size, size);
    return out;
}

void mk64_effects(RiCtx *ctx)
{
    RiBuf *common = mk64_mio0(ctx, ri_span(ctx, ctx->rom, MK64_COMMON_DATA, MK64_COMMON_DATA_END - MK64_COMMON_DATA));
    RiBuf *shadow = standalone(ctx, KART_SHADOW, 64u * 64u);
    i8_material(ctx, MK64_FX_MATERIAL_ID(0), shadow->data, 64, 64, 1);
    RiBuf *spark = standalone(ctx, DRIFT_SPARK, 32u * 32u);
    i8_material(ctx, MK64_FX_MATERIAL_ID(1), spark->data, 32, 32, 0);
    RiBuf *dust = standalone(ctx, GROUND_DUST, 32u * 32u);
    i8_material(ctx, MK64_FX_MATERIAL_ID(4), dust->data, 32, 32, 0);
    ri_buf_free(ctx, shadow);
    ri_buf_free(ctx, spark);
    ri_buf_free(ctx, dust);
    /* Smoke frames side by side as one strip. */
    const uint8_t *smoke = ri_at(ctx, ri_view(common), COMMON_SMOKE, 4u * 32u * 32u);
    uint8_t strip[4u * 32u * 32u];
    for (uint32_t frame = 0; frame < 4u; ++frame)
        for (uint32_t y = 0; y < 32u; ++y)
            for (uint32_t x = 0; x < 32u; ++x)
                strip[y * 128u + frame * 32u + x] = smoke[frame * 1024u + y * 32u + x];
    i8_material(ctx, MK64_FX_MATERIAL_ID(2), strip, 128, 32, 0);
    const uint8_t *fire = ri_at(ctx, ri_view(common), COMMON_FIRE, 32u * 64u * 2u);
    RiBuf *flame = ri_buf_zero(ctx, 32u * 64u * 4u);
    for (size_t i = 0; i < 32u * 64u; ++i)
        ri_set_pixel(flame, i, argb16(mk64_be16(fire + i * 2u)));
    material(ctx, MK64_FX_MATERIAL_ID(3), 0xFFFFFFFFu, 32, 64, flame);
    ri_buf_free(ctx, common);

    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "FXS1", 4);
    ri_words(ctx, out, 3, 1u, 4u, 0u);
    /* Drift tiers: driftState 0, 1 and 2 (the last also stands for a third). */
    ri_words(ctx, out, 4, 0x70FFFFFFu, 0x70FFFF00u, 0x70FF9600u, 0x70FF9600u);
    /* Smoke in primColors[0] (FBFFFB), dust, flame core and rim (the fire art's own colours). */
    ri_words(ctx, out, 4, 0x90FBFFFBu, 0xB0A08C6Eu, 0xFFFFFFFFu, 0xFFFFE8C8u);
    const int32_t half = kart_metres_x100(SHADOW_HALF_UNITS_X100);
    ri_words(ctx, out, 8, 0u, MK64_FX_MATERIAL_ID(0), (uint32_t)half, (uint32_t)half, 0u,
             (uint32_t)kart_metres_x100(SHADOW_VANISH_UNITS * 100), 0u, 0u);
    /* set_drift_particles: one spark on the outer back tyre, renewed every
     * three frames (check_drift_particles_setup_valid). */
    ri_words(ctx, out, 4, (uint32_t)SLOT_SPARK, MK64_FX_MATERIAL_ID(1), 1u, (uint32_t)STYLE_WHEEL);
    ri_words(ctx, out, 4, (uint32_t)SLOT_SMOKE, MK64_FX_MATERIAL_ID(2), 4u, 0u);
    ri_words(ctx, out, 4, (uint32_t)SLOT_FLAME, MK64_FX_MATERIAL_ID(3), 1u, (uint32_t)STYLE_COLOURED);
    ri_words(ctx, out, 4, (uint32_t)SLOT_DUST, MK64_FX_MATERIAL_ID(4), 1u, 0u);
    ri_section(ctx, 116, MK64_FX_SET_ID, 1, out);
}
