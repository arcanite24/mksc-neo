#include "mkds_fx.h"
#include "mkds_model.h"
#include "nitro_spa.h"

/* RaceEffect.spa emitters by what the race draws with them. The SPL file
 * carries no names: each is matched by its texture, colour and motion (the
 * drift sparks are the three tex-31 dots that differ only in colour; the
 * mini-turbo flash the short yellow starburst; smoke and dust the white
 * and sand clouds blown back from the wheels; boost the orange flame). */
enum
{
    SLOT_GLOW,
    SLOT_SPARK,
    SLOT_SMOKE,
    SLOT_FLAME,
    SLOT_DUST,
    SLOT_SPLASH,
    SLOT_MIST,
    SLOT_EMBER,
    STYLE_ADDITIVE = 1,
    STYLE_COLOURED = 2,
};
static const struct
{
    uint32_t slot, emitter, flags;
} styles[] = {
    {SLOT_GLOW, 114, STYLE_ADDITIVE},   {SLOT_SPARK, 66, STYLE_ADDITIVE}, {SLOT_SMOKE, 96, 0},
    {SLOT_FLAME, 52, STYLE_COLOURED},   {SLOT_DUST, 15, 0},               {SLOT_SPLASH, 55, 0},
    {SLOT_MIST, 89, 0},                 {SLOT_EMBER, 57, STYLE_ADDITIVE},
};
#define SPARK_CHARGING 67u /* blue sparks */
#define SPARK_READY 66u    /* red-orange sparks: the mini-turbo is charged */
#define SMOKE 96u
#define DUST 15u
#define FLAME 52u
static uint32_t argb(const NitroSpaEmitter *e)
{
    return (e->alpha * 255u / 31u) << 24 | mkds_rgb_word(e->color) >> 8;
}

void mkds_effects(RiCtx *ctx, const NdsFs *rom, const RiBuf *shadows, uint32_t shadow_count)
{
    const char *path = "data/MainEffect.carc";
    const NdsFs effects = nds_narc(ctx, nds_file(ctx, rom, path), path);
    const NitroSpa spa = nitro_spa(ctx, nds_file(ctx, &effects, "RaceEffect.spa"), "data/MainEffect.carc/RaceEffect.spa");
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); ++i)
    {
        uint32_t width, height;
        RiBuf *pixels = nitro_spa_emitter_argb(ctx, &spa, styles[i].emitter, &width, &height);
        RiBuf *image = ri_image_asset(ctx, width, height, pixels, 0, 0);
        RiBuf *out = ri_buf(ctx);
        ri_put(ctx, out, "MTL1", 4);
        ri_words(ctx, out, 3, 0xFFFFFFFFu, 0u, (uint32_t)image->size);
        ri_put_buf(ctx, out, image);
        ri_section(ctx, MKDS_KIND_MATERIAL, MKDS_FX_MATERIAL_ID(styles[i].slot), 1, out);
        ri_buf_free(ctx, image);
        ri_buf_free(ctx, pixels);
    }

    /* No DS MapObj is a particle emitter: FallsWaterDst, once taken for
     * Yoshi Falls' spray, is where a falls-water KCL variant pulls karts
     * (MKDS-decomp-headers fwdst_t: index, per-class pull strength). */
    const uint32_t style_count = sizeof(styles) / sizeof(styles[0]), emitter_count = 0;
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "FXS1", 4);
    ri_words(ctx, out, 3, shadow_count, style_count, emitter_count);
    const NitroSpaEmitter *e = spa.emitters;
    ri_words(ctx, out, 4, argb(&e[SPARK_CHARGING]), argb(&e[SPARK_READY]), argb(&e[SPARK_READY]),
             argb(&e[SPARK_READY]));
    ri_words(ctx, out, 4, argb(&e[SMOKE]), argb(&e[DUST]), 0xFFFFFFFFu, argb(&e[FLAME]));
    ri_put_buf(ctx, out, shadows);
    for (size_t i = 0; i < style_count; ++i)
        ri_words(ctx, out, 4, styles[i].slot, MKDS_FX_MATERIAL_ID(styles[i].slot), 1u, styles[i].flags);
    ri_section(ctx, MKDS_FX_SET_KIND, MKDS_FX_SET_ID, 1, out);
}
