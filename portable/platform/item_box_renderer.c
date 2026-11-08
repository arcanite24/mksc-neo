#include "item_box_renderer.h"

#include <stddef.h>

static uint32_t rgb(uint16_t color)
{
    return ((uint32_t)(color & 31u) * 255u / 31u << 16) |
           ((uint32_t)((color >> 5) & 31u) * 255u / 31u << 8) |
           (uint32_t)((color >> 10) & 31u) * 255u / 31u;
}

static void animated_palette(const PortableItemBoxAssetView *asset,
                             uint32_t tick, uint16_t palette[16])
{
    const uint32_t phase = tick & 31u;
    const uint32_t fade = phase <= 15u ? phase * 2u : (31u - phase) * 2u;
    const uint16_t c0 = (uint16_t)(((fade + 15u) & 31u) << 5u | 0x7c00u);
    const uint16_t c1 = (uint16_t)(((55u - fade) & 31u) << 5u | 31u);
    const uint16_t c2 = (uint16_t)(((55u - fade) & 31u) << 5u |
                                   ((fade & 31u) << 10u));
    const uint16_t c3 = (uint16_t)(((31u - fade) & 31u) | 0x7c00u);
    const uint16_t c4 = (uint16_t)(((31u - fade) & 31u) | 0x02e0u);
    const uint32_t cycle = (tick >> 2u) & 31u;

    for (size_t index = 0u; index < 16u; ++index)
        palette[index] = asset->palette[index];
    /* sub_804389C always writes OBJ palette-bank 6 color 5, then rotates
       colors 4 and 6 through four eight-tick spans. */
    palette[5] = c0;
    if (cycle <= 7u) { palette[4] = c4; palette[6] = c1; }
    else if (cycle <= 15u) { palette[4] = c1; palette[6] = c2; }
    else if (cycle <= 23u) { palette[4] = c2; palette[6] = c3; }
    else { palette[4] = c3; palette[6] = c4; }
}

int portable_item_box_animated_palette(const PortableItemBoxAssetView *asset,
    uint32_t phase,uint16_t palette[16])
{
    if(asset==NULL||asset->palette==NULL||palette==NULL||
       asset->palette_count!=16u)return 0;
    animated_palette(asset,phase,palette);
    return 1;
}

static int render_scaled(const PortableItemBoxAssetView *asset,
                             PortableSoftwareFrame *frame,
                             int32_t origin_x, int32_t origin_y,
                             uint32_t animation_tick, uint16_t inverse_scale)
{
    uint16_t palette[16];
    const uint32_t frame_index = (animation_tick >> 2u) & 7u;
    size_t frame_offset;

    if (asset == NULL || frame == NULL || frame->pixels == NULL ||
        asset->tiles == NULL || asset->palette == NULL ||
        asset->frame_count != PORTABLE_ITEM_BOX_FRAME_COUNT ||
        asset->tile_bytes != PORTABLE_ITEM_BOX_TILE_BYTES ||
        asset->palette_count != PORTABLE_ITEM_BOX_PALETTE_COUNT ||
        asset->width != 32u || asset->height != 32u)
        return 0;
    if (inverse_scale == 0u) return 0;
    /* Affine cells sample around their centre. Expansion uses a double-sized
     * canvas; the source's signed nine-bit X decoder gives -15 for raw -16,
     * hence the one-pixel centre correction when the cell is transformed. */
    const int32_t output_scale=(int32_t)portable_software_frame_scale(frame);
    const int32_t divisor=256*output_scale;
    const uint32_t extent = inverse_scale < 256u ? 64u : 32u;
    const int32_t left = inverse_scale == 256u ? (origin_x - 16)*output_scale :
        origin_x*output_scale + 256*output_scale / inverse_scale - output_scale - (int32_t)extent*output_scale / 2;
    const int32_t top = inverse_scale == 256u ? (origin_y - 30)*output_scale :
        origin_y*output_scale - 14 * 256*output_scale / inverse_scale - (int32_t)extent*output_scale / 2;
    frame_offset = (size_t)frame_index * PORTABLE_ITEM_BOX_FRAME_BYTES;
    animated_palette(asset, animation_tick, palette);
    for (uint32_t y = 0u; y < extent*(uint32_t)output_scale; ++y)
        for (uint32_t x = 0u; x < extent*(uint32_t)output_scale; ++x)
        {
            const int32_t mx = ((int32_t)x - (int32_t)extent * output_scale / 2) * inverse_scale;
            const int32_t my = ((int32_t)y - (int32_t)extent * output_scale / 2) * inverse_scale;
            const int32_t sx = 16 + (mx >= 0 ? mx / divisor : -((-mx + divisor-1) / divisor));
            const int32_t sy = 16 + (my >= 0 ? my / divisor : -((-my + divisor-1) / divisor));
            if (sx < 0 || sx >= 32 || sy < 0 || sy >= 32) continue;
            const size_t tile = (size_t)(sy / 8) * 4u + (size_t)(sx / 8);
            const size_t offset = frame_offset + tile * 32u +
                                  (size_t)(sy & 7) * 4u + (size_t)(sx & 7) / 2u;
            const uint8_t packed = asset->tiles[offset];
            const uint8_t color = (sx & 1) != 0u ? packed >> 4u : packed & 15u;
            const int32_t destination_x = left + (int32_t)x;
            const int32_t destination_y = top + (int32_t)y;
            if (color != 0u && destination_x >= 0 && destination_y >= 0 &&
                destination_x < (int32_t)frame->width*output_scale &&
                destination_y < (int32_t)frame->height*output_scale)
                portable_software_frame_store_rgb_physical(frame,(size_t)destination_x,(size_t)destination_y, rgb(palette[color]));
        }
    return 1;
}

int portable_item_box_render(const PortableItemBoxAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint32_t tick)
{
    return render_scaled(asset, frame, x, y, tick, 256u);
}

int portable_item_box_render_projected(const PortableItemBoxAssetView *asset,
    PortableSoftwareFrame *frame, int32_t x, int32_t y, uint32_t tick,
    uint16_t depth)
{
    /* 43860 stores B6; 49B0C multiplies it by projected depth before drawing. */
    const uint32_t inverse = (uint32_t)depth * 182u / 256u;
    if (inverse == 0u || inverse > UINT16_MAX) return 0;
    return render_scaled(asset, frame, x, y, tick, (uint16_t)inverse);
}
