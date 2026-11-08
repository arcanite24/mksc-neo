#ifndef PORTABLE_RACE_ITEM_CELL_RENDERER_H
#define PORTABLE_RACE_ITEM_CELL_RENDERER_H

#include "race_particle_asset.h"
#include "software_frame.h"

/* Native sampler for the shared sixteen-pixel item cells. Inverse axes and
 * centre rounding follow the source cell renderer; index zero stays clear. */
static inline int portable_race_item_cell_render(
    const PortableRaceParticleAssetView *asset, PortableSoftwareFrame *frame,
    int32_t origin_x, int32_t origin_y, uint16_t tile_base, uint8_t palette_bank,
    int8_t offset_y, uint8_t mirror, uint16_t inverse)
{
    if (asset == NULL || frame == NULL || frame->pixels == NULL ||
        asset->tiles == NULL || asset->palette == NULL ||
        tile_base < asset->tile_base || palette_bank < asset->palette_base)
        return 0;
    if (inverse == 0u) inverse = 256u;
    const int32_t output_scale=(int32_t)portable_software_frame_scale(frame);
    const int32_t divisor=256*output_scale;
    const uint32_t canvas = inverse < 256u ? 32u : 16u;
    const int32_t left = inverse == 256u ? (origin_x - 8)*output_scale :
        origin_x*output_scale + 256*output_scale / inverse - output_scale - (int32_t)canvas*output_scale / 2;
    const int32_t top = inverse == 256u ? (origin_y + offset_y)*output_scale :
        origin_y*output_scale + ((int32_t)offset_y + 8) * 256*output_scale / inverse - (int32_t)canvas*output_scale / 2;
    const size_t colors = (size_t)(palette_bank - asset->palette_base) * 16u;
    if (colors + 15u >= asset->palette_count) return 0;
    for (uint32_t y = 0u; y < canvas*(uint32_t)output_scale; ++y)
        for (uint32_t x = 0u; x < canvas*(uint32_t)output_scale; ++x) {
            const int32_t mx = ((int32_t)x - (int32_t)canvas * output_scale / 2) * inverse * (mirror ? -1 : 1);
            const int32_t my = ((int32_t)y - (int32_t)canvas * output_scale / 2) * inverse;
            const int32_t sx = inverse == 256u ? (mirror ? 15 - (int32_t)x/output_scale : (int32_t)x/output_scale) :
                8 + (mx >= 0 ? mx / divisor : -((-mx + divisor-1) / divisor));
            const int32_t sy = inverse == 256u ? (int32_t)y/output_scale :
                8 + (my >= 0 ? my / divisor : -((-my + divisor-1) / divisor));
            if (sx < 0 || sx >= 16 || sy < 0 || sy >= 16) continue;
            const size_t tile = (size_t)(tile_base - asset->tile_base) +
                (size_t)(sy / 8) * 2u + (size_t)(sx / 8);
            const size_t offset = tile * 32u + (size_t)(sy & 7) * 4u + (size_t)(sx & 7) / 2u;
            if (offset >= asset->tile_bytes) return 0;
            const uint8_t packed = asset->tiles[offset];
            const uint8_t index = (sx & 1) ? packed >> 4 : packed & 15u;
            const int32_t dx = left + (int32_t)x, dy = top + (int32_t)y;
            if (index == 0u || dx < 0 || dy < 0 ||
                dx >= (int32_t)frame->width*output_scale || dy >= (int32_t)frame->height*output_scale) continue;
            const uint16_t color = asset->palette[colors + index];
            const uint32_t rgb = ((uint32_t)(color & 31u) * 255u / 31u << 16) |
                ((uint32_t)((color >> 5) & 31u) * 255u / 31u << 8) |
                (uint32_t)((color >> 10) & 31u) * 255u / 31u;
            portable_software_frame_store_rgb_physical(frame,(size_t)dx,(size_t)dy,rgb);
        }
    return 1;
}

/* F7AC calls 437D4 for every allocation. Original Thumb 4381E initializes the
 * intrinsic inverse coefficient to C0 (the decompiled C incorrectly says C000).
 * 49B0C multiplies that halfword by projected depth before drawing. */
static inline uint16_t portable_race_item_inverse(uint16_t depth)
{
    return (uint16_t)((uint32_t)depth * 192u / 256u);
}

#endif
