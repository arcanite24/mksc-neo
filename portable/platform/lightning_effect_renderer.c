#include "lightning_effect_renderer.h"

enum
{
    LIGHTNING_PALETTE_BANK = 3,
    LIGHTNING_SIZE = 32,
};

static uint32_t rgb(uint16_t color)
{
    return ((uint32_t)(color & 31u) * 255u / 31u << 16) |
           ((uint32_t)((color >> 5) & 31u) * 255u / 31u << 8) |
           (uint32_t)((color >> 10) & 31u) * 255u / 31u;
}

static int render_positioned(const PortableLightningEffectAssetView *asset,
                             const PortableRaceParticleAssetView *race_effects,
                             PortableSoftwareFrame *frame, int64_t origin_x_q16,
                             int64_t origin_y_q16, uint8_t frame_index)
{
    const uint8_t *tiles;
    const size_t color_base = (size_t)(LIGHTNING_PALETTE_BANK -
                                       (race_effects != NULL ? race_effects->palette_base : 0u)) *
                              16u;
    if (asset == NULL || race_effects == NULL || frame == NULL || frame->pixels == NULL ||
        asset->tiles == NULL || asset->tile_base != UINT16_C(0x01a0) ||
        asset->frame_count != PORTABLE_LIGHTNING_EFFECT_FRAME_COUNT ||
        asset->tile_bytes != PORTABLE_LIGHTNING_EFFECT_TILE_BYTES ||
        race_effects->palette == NULL || race_effects->palette_base > LIGHTNING_PALETTE_BANK ||
        color_base + 15u >= race_effects->palette_count ||
        frame_index >= PORTABLE_LIGHTNING_EFFECT_FRAME_COUNT)
        return 0;
    tiles = asset->tiles + (size_t)frame_index * PORTABLE_LIGHTNING_EFFECT_FRAME_BYTES;
    const int64_t scale = (int64_t)portable_software_frame_scale(frame);
    const int64_t left =
        portable_software_frame_floor_div(origin_x_q16 * scale, 65536) - 16 * scale;
    const int64_t top = portable_software_frame_floor_div(origin_y_q16 * scale, 65536) - 16 * scale;
    const int64_t right = left + LIGHTNING_SIZE * scale, bottom = top + LIGHTNING_SIZE * scale;
    const int64_t width = (int64_t)frame->width * scale, height = (int64_t)frame->height * scale;
    const int64_t first_x = left < 0 ? 0 : left, first_y = top < 0 ? 0 : top;
    const int64_t last_x = right < width ? right : width,
                  last_y = bottom < height ? bottom : height;
    for (int64_t dy = first_y; dy < last_y; ++dy)
        for (int64_t dx = first_x; dx < last_x; ++dx)
        {
            const uint32_t x = (uint32_t)((dx - left) / scale), y = (uint32_t)((dy - top) / scale);
            const size_t tile = (size_t)(y / 8u) * 4u + x / 8u;
            const size_t offset = tile * 32u + (y & 7u) * 4u + (x & 7u) / 2u;
            const uint8_t packed = tiles[offset];
            const uint8_t color = (x & 1u) != 0u ? packed >> 4 : packed & 15u;
            if (color != 0u)
                portable_software_frame_store_rgb_physical(
                    frame, (size_t)dx, (size_t)dy, rgb(race_effects->palette[color_base + color]));
        }
    return 1;
}

int portable_lightning_effect_render(const PortableLightningEffectAssetView *asset,
                                     const PortableRaceParticleAssetView *race_effects,
                                     PortableSoftwareFrame *frame, int32_t origin_x,
                                     int32_t origin_y, uint8_t frame_index)
{
    return render_positioned(asset, race_effects, frame, (int64_t)origin_x * 65536,
                             (int64_t)origin_y * 65536, frame_index);
}

int portable_lightning_effect_render_positioned(const PortableLightningEffectAssetView *asset,
                                                const PortableRaceParticleAssetView *race_effects,
                                                PortableSoftwareFrame *frame, int32_t origin_x_q16,
                                                int32_t origin_y_q16, uint8_t frame_index)
{
    return render_positioned(asset, race_effects, frame, origin_x_q16, origin_y_q16, frame_index);
}
