#include "fx_set_asset.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static uint8_t *put(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(v >> (i * 8u));
    return p + 4;
}

/* Two shadows, one style and one emitter. */
static size_t build(uint8_t *out)
{
    uint8_t *p = out;
    memcpy(p, "FXS1", 4);
    p = put(p + 4, 2);
    p = put(p, 1);
    p = put(p, 1);
    for (unsigned i = 0; i < 8u; ++i)
        p = put(p, 0xff000000u + i);
    const uint32_t shadows[2][8] = {{0, 7, 40000, 40000, 0, 300000, 0, 0},
                                    {0x4d4b4b01u, 8, 30000, 50000, 2000, 65536, PORTABLE_FX_SHADOW_VOLUME, 0}};
    for (unsigned r = 0; r < 2u; ++r)
        for (unsigned i = 0; i < 8u; ++i)
            p = put(p, shadows[r][i]);
    const uint32_t style[4] = {PORTABLE_FX_STYLE_SPLASH, 9, 4, PORTABLE_FX_STYLE_ADDITIVE};
    for (unsigned i = 0; i < 4u; ++i)
        p = put(p, style[i]);
    const uint32_t emitter[14] = {5, PORTABLE_FX_STYLE_SPLASH, 3, 2, 40, 32768, 65536, 0, 0, 131072,
                                  16384, 9 * 65536, 65536, 0xc0ffffffu};
    for (unsigned i = 0; i < 14u; ++i)
        p = put(p, emitter[i]);
    return (size_t)(p - out);
}

static int open_bytes(const uint8_t *bytes, size_t size, PortableFxSet *out)
{
    const PortableAssetSectionView section = {.kind = PORTABLE_ASSET_SECTION_FX_SET, .version = 1,
                                              .bytes = bytes, .size = size};
    return portable_fx_set_open(&section, out);
}

int main(void)
{
    static PortableFxSet set;
    uint8_t bytes[512];
    const size_t size = build(bytes);
    CHECK(open_bytes(bytes, size, &set));
    CHECK(set.shadow_count == 2u && set.style_count == 1u && set.emitter_count == 1u);
    CHECK(set.tier_argb[3] == 0xff000003u && set.flame_argb[1] == 0xff000007u);
    /* Kart row first, then the driver's, then the default. */
    CHECK(portable_fx_set_shadow(&set, 0x4d4b4b01u, 9)->material == 8u);
    CHECK(portable_fx_set_shadow(&set, 0x4d4b4b02u, 9)->material == 7u);
    CHECK(portable_fx_set_shadow(&set, 0, 0x4d4b4b01u)->material == 8u);
    CHECK(portable_fx_set_shadow(&set, 0, 0x4d4b4b01u)->flags == PORTABLE_FX_SHADOW_VOLUME);
    CHECK(portable_fx_set_style(&set, PORTABLE_FX_STYLE_SPLASH)->frames == 4u);
    CHECK(!portable_fx_set_style(&set, PORTABLE_FX_STYLE_SPARK));
    const PortableFxEmitter *e = portable_fx_set_emitter(&set, 5);
    CHECK(e && e->burst == 2u && e->velocity_q16[2] == 131072 && e->gravity_q16 == 9 * 65536);
    CHECK(!portable_fx_set_emitter(&set, 6));

    uint8_t bad[512];
    memcpy(bad, bytes, size);
    CHECK(!open_bytes(bad, size - 4u, &set)); /* size must match the counts */
    put(bad + 48 + 32 + 28, 1);               /* reserved shadow word */
    CHECK(!open_bytes(bad, size, &set));
    memcpy(bad, bytes, size);
    put(bad + 48 + 24, 2);                    /* unknown shadow flag */
    CHECK(!open_bytes(bad, size, &set));
    memcpy(bad, bytes, size);
    put(bad + 48 + 32, 0);                    /* duplicate default key */
    CHECK(!open_bytes(bad, size, &set));
    memcpy(bad, bytes, size);
    put(bad + 48 + 64 + 8, 17);               /* too many frames */
    CHECK(!open_bytes(bad, size, &set));
    memcpy(bad, bytes, size);
    put(bad + 48 + 64 + 16 + 8, 0);           /* emitter interval 0 */
    CHECK(!open_bytes(bad, size, &set));
    const PortableAssetSectionView wrong = {.kind = PORTABLE_ASSET_SECTION_SOUND_SET, .version = 1,
                                            .bytes = bytes, .size = size};
    CHECK(!portable_fx_set_open(&wrong, &set));
    if (failures)
        return 1;
    puts("fx set asset: PASS");
    return 0;
}
