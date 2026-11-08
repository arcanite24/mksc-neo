#include "lightning_effect_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "Lightning renderer failed line %d: %s\n", __LINE__, #c);              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    uint8_t tiles[PORTABLE_LIGHTNING_EFFECT_TILE_BYTES];
    uint16_t palette[16] = {0};
    uint32_t pixels[64u * 64u];
    PortableSoftwareFrame frame = {pixels, 64u, 64u, NULL, 1u, 0u,0u,NULL};
    PortableLightningEffectAssetView asset = {tiles, sizeof tiles, 4u, 0x1a0u};
    PortableRaceParticleAssetView race = {0};
    memset(tiles, 0x11, 0x200u);
    memset(tiles + 0x200u, 0x22, 0x200u);
    memset(tiles + 0x400u, 0x33, 0x200u);
    memset(tiles + 0x600u, 0x44, 0x200u);
    palette[3] = UINT16_C(0x001f);
    race.palette = palette;
    race.palette_count = 16u;
    race.palette_base = 3u;
    portable_software_frame_clear(&frame, 0u);
    CHECK(portable_lightning_effect_render(&asset, &race, &frame, 24, 24, 2u));
    for (size_t y = 0u; y < 64u; ++y)
        for (size_t x = 0u; x < 64u; ++x)
        {
            const uint32_t expected =
                x >= 8u && x < 40u && y >= 8u && y < 40u ? UINT32_C(0xff0000) : 0u;
            if (pixels[y * 64u + x] != expected)
            {
                fprintf(stderr, "Lightning pixel mismatch x=%zu y=%zu got=%08x expected=%08x\n", x,
                        y, pixels[y * 64u + x], expected);
                return 1;
            }
        }
    CHECK(!portable_lightning_effect_render(&asset, &race, &frame, 24, 24, 4u));
    {
        enum
        {
            SCALE = 4,
            STRIDE = 64 * SCALE + 3,
            ROWS = 64 * SCALE
        };
        uint32_t *physical = malloc((size_t)STRIDE * ROWS * sizeof *physical);
        CHECK(physical != NULL);
        PortableSoftwareFrame large = {physical, 64u, 64u, NULL, SCALE, STRIDE,0u,NULL};
        /* The first source pixel is transparent; its shifted four-pixel
         * footprint must retain the destination at every fractional origin. */
        tiles[0x400u] = 0x30;
        uint8_t unchanged[sizeof tiles];
        memcpy(unchanged, tiles, sizeof tiles);
        const uint16_t before_color = palette[3];
        for (unsigned step = 0u; step < 5u; ++step)
        {
            for (size_t i = 0u; i < (size_t)STRIDE * ROWS; ++i)
                physical[i] = UINT32_C(0x11223344);
            const int32_t x = step == 4u ? 16384 : 24 * 65536 + (int32_t)step * 16384;
            const int32_t y = step == 4u ? -16384 : 24 * 65536;
            const int left = step == 4u ? -63 : 8 * SCALE + (int)step;
            const int top = step == 4u ? -65 : 8 * SCALE;
            CHECK(portable_lightning_effect_render_positioned(&asset, &race, &large, x, y, 2u));
            for (unsigned py = 0u; py < ROWS; ++py)
                for (unsigned px = 0u; px < STRIDE; ++px)
                {
                    const int covered = (int)px >= left && (int)px < left + 32 * SCALE &&
                                        (int)py >= top && (int)py < top + 32 * SCALE &&
                                        px < 64 * SCALE &&
                                        !((int)px < left + SCALE && (int)py < top + SCALE);
                    CHECK(physical[(size_t)py * STRIDE + px] ==
                          (covered ? UINT32_C(0xff0000) : UINT32_C(0x11223344)));
                }
            CHECK(!memcmp(tiles, unchanged, sizeof tiles) && palette[3] == before_color);
        }
        free(physical);
        /* The new entry point retains the native pixel oracle at1x. */
        uint32_t expected[64u * 64u];
        portable_software_frame_clear(&frame, 0u);
        CHECK(portable_lightning_effect_render(&asset, &race, &frame, 24, 24, 2u));
        memcpy(expected, pixels, sizeof pixels);
        portable_software_frame_clear(&frame, 0u);
        CHECK(portable_lightning_effect_render_positioned(&asset, &race, &frame, 24 * 65536 + 49152,
                                                          24 * 65536 + 49152, 2u));
        CHECK(!memcmp(expected, pixels, sizeof pixels));
    }
    puts("Lightning effect renderer: PASS frames=4 size=32x32 palette=3");
    return 0;
}
