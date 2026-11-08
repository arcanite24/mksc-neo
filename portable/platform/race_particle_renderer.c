#include "race_particle_renderer.h"

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static int32_t sign8(uint32_t value)
{
    return (value & 0x80u) != 0u ? (int32_t)value - 0x100 : (int32_t)value;
}

static int32_t sign9(uint32_t value)
{
    return (value & 0x100u) != 0u ? (int32_t)value - 0x200 : (int32_t)value;
}

static uint32_t rgb(uint16_t color)
{
    return ((uint32_t)(color & 31u) * 255u / 31u << 16) |
           ((uint32_t)((color >> 5) & 31u) * 255u / 31u << 8) |
           (uint32_t)((color >> 10) & 31u) * 255u / 31u;
}

static void dimensions(uint32_t shape, uint32_t size,
                       uint32_t *width, uint32_t *height)
{
    static const uint8_t widths[3][4] = {
        {8,16,32,64}, {16,32,32,64}, {8,8,16,32}};
    static const uint8_t heights[3][4] = {
        {8,16,32,64}, {8,8,16,32}, {16,32,32,64}};
    if (shape > 2u) shape = 0u;
    *width = widths[shape][size & 3u];
    *height = heights[shape][size & 3u];
}

static int render_positioned(
    const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,
    int64_t origin_x_q16,
    int64_t origin_y_q16,
    uint8_t age, uint16_t scale_q8)
{
    size_t frame_index = 0u;
    uint32_t span = 2u;
    const uint8_t *cell;
    uint16_t count;

    if (age == 0u || age == 1u)
        return 1;
    if (scale_q8 == 0u || scale_q8 > 1024u) return 0;
    if (asset == NULL || frame == NULL || frame->pixels == NULL ||
        asset->tiles == NULL || asset->palette == NULL ||
        asset->cells == NULL || asset->frames == NULL ||
        (asset->frame_count == 0u || asset->frame_count > PORTABLE_RACE_PARTICLE_FRAME_CAPACITY))
        return 0;
    while (frame_index < asset->frame_count &&
           age >= span + asset->frames[frame_index].duration + 1u)
    {
        span += asset->frames[frame_index].duration + 1u;
        ++frame_index;
    }
    if (frame_index == asset->frame_count)
        return 1;
    if (asset->frames[frame_index].offset > asset->cell_bytes ||
        asset->frames[frame_index].size >
            asset->cell_bytes - asset->frames[frame_index].offset)
        return 0;
    cell = asset->cells + asset->frames[frame_index].offset;
    count = u16(cell);
    if (asset->frames[frame_index].size != 2u + (size_t)count * 6u)
        return 0;
    cell += 2;
    for (uint16_t entry = 0; entry < count; ++entry, cell += 6)
    {
        const uint16_t a0 = u16(cell);
        const uint16_t a1 = u16(cell + 2);
        const uint16_t a2 = u16(cell + 4);
        uint32_t width, height;
        const uint32_t source_tile = a2 & 0x3ffu;
        const uint32_t palette_bank = (a2 >> 12) & 0xfu;
        if (source_tile < asset->tile_base ||
            palette_bank < asset->palette_base)
            return 0;
        dimensions((a0 >> 14) & 3u, (a1 >> 14) & 3u, &width, &height);
        const uint32_t output_scale=(uint32_t)portable_software_frame_scale(frame);
        const uint32_t drawn_width=(width*256u*output_scale+scale_q8-1u)/scale_q8;
        const uint32_t drawn_height=(height*256u*output_scale+scale_q8-1u)/scale_q8;
        const int64_t left=portable_software_frame_floor_div(origin_x_q16*output_scale,65536)+
            (int64_t)sign9(a1&0x1ffu)*256*output_scale/scale_q8;
        const int64_t top=portable_software_frame_floor_div(origin_y_q16*output_scale,65536)+
            (int64_t)sign8(a0&0xffu)*256*output_scale/scale_q8;
        const int64_t available_x=(int64_t)frame->width*output_scale-left;
        const int64_t available_y=(int64_t)frame->height*output_scale-top;
        if(available_x<=0 || available_y<=0)continue;
        const uint32_t first_x=left<0?(uint32_t)(-left):0u;
        const uint32_t first_y=top<0?(uint32_t)(-top):0u;
        const uint32_t last_x=(uint64_t)available_x<drawn_width?(uint32_t)available_x:drawn_width;
        const uint32_t last_y=(uint64_t)available_y<drawn_height?(uint32_t)available_y:drawn_height;
        for (uint32_t y = first_y; y < last_y; ++y)
            for (uint32_t x = first_x; x < last_x; ++x)
            {
                const uint32_t sample_x=x*scale_q8/(256u*output_scale);
                const uint32_t sx = (a1 & 0x1000u) != 0u ? width-1u-sample_x : sample_x;
                const uint32_t sample_y=y*scale_q8/(256u*output_scale);
                const uint32_t sy = (a1 & 0x2000u) != 0u ? height-1u-sample_y : sample_y;
                const size_t tile = (size_t)(source_tile-asset->tile_base) +
                    (size_t)(sy/8u)*(width/8u) + sx/8u;
                const size_t offset = tile*32u + (sy&7u)*4u + (sx&7u)/2u;
                const size_t color_base =
                    (size_t)(palette_bank-asset->palette_base)*16u;
                uint8_t packed, color;
                int32_t dx, dy;
                if (offset >= asset->tile_bytes || color_base+15u >= asset->palette_count)
                    return 0;
                packed = asset->tiles[offset];
                color = (sx & 1u) != 0u ? packed >> 4 : packed & 15u;
                if (color == 0u) continue;
                dx = (int32_t)(left+x);
                dy = (int32_t)(top+y);
                if (dx >= 0 && dy >= 0 && dx < (int32_t)(frame->width*output_scale) &&
                    dy < (int32_t)(frame->height*output_scale))
                    portable_software_frame_store_rgb_physical(frame,(size_t)dx,(size_t)dy, rgb(asset->palette[color_base+color]));
            }
    }
    return 1;
}

int portable_race_particle_render_positioned(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,int32_t x_q16,int32_t y_q16,uint8_t age,uint16_t inverse)
{
    return render_positioned(asset,frame,x_q16,y_q16,age,inverse);
}

int portable_race_particle_render_scaled(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,int32_t x,int32_t y,uint8_t age,uint16_t inverse)
{
    return render_positioned(asset,frame,(int64_t)x*65536,(int64_t)y*65536,age,inverse);
}

int portable_race_particle_render(const PortableRaceParticleAssetView *asset,
    PortableSoftwareFrame *frame,int32_t origin_x,int32_t origin_y,uint8_t age)
{
    return portable_race_particle_render_scaled(asset,frame,origin_x,origin_y,age,256u);
}
