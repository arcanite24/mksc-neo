#include "road_renderer.h"

static int64_t floor_div256(int64_t value)
{
    if (value >= 0)
        return value / 256;
    return -((-value + 255) / 256);
}

static size_t positive_mod(int64_t value, size_t modulus)
{
    int64_t remainder = value % (int64_t)modulus;

    if (remainder < 0)
        remainder += (int64_t)modulus;
    return (size_t)remainder;
}

static uint32_t color_to_rgb(uint16_t color)
{
    const uint32_t red5 = color & 0x1fu;
    const uint32_t green5 = (color >> 5) & 0x1fu;
    const uint32_t blue5 = (color >> 10) & 0x1fu;
    const uint32_t red = (red5 << 3u) | (red5 >> 2u);
    const uint32_t green = (green5 << 3u) | (green5 >> 2u);
    const uint32_t blue = (blue5 << 3u) | (blue5 >> 2u);

    return (red << 16) | (green << 8) | blue;
}

static int render_road(const PortableRoadView *road,
                       const PortableCameraAffineLine *lines,
                       size_t line_count,
                       PortableSoftwareFrame *frame,
                       int result_scanline_timing, int16_t scanline_offset,int alpha_road,
                       int physical_first_row)
{
    const size_t scale=frame!=NULL?portable_software_frame_scale(frame):1u;
    size_t last_line = 0xa0u*scale;

    if (road == NULL || lines == NULL || frame == NULL || frame->pixels == NULL)
        return PORTABLE_ROAD_RENDER_INVALID_ARGUMENT;
    if (scanline_offset < -74 || scanline_offset > 86 || scale>8u)
        return PORTABLE_ROAD_RENDER_INVALID_ARGUMENT;
    const size_t first_line = physical_first_row >= 0 ? (size_t)physical_first_row :
        (result_scanline_timing ? (size_t)(74 + scanline_offset) : 0x40u)*scale;
    const size_t first_record = result_scanline_timing ? 8u * scale : 0u;
    if (road->map.map == NULL || road->map.width_tiles == 0 ||
        road->map.height_tiles == 0 || road->map.map_bytes <
            road->map.width_tiles * road->map.height_tiles ||
        road->tileset.tiles == NULL || road->tileset.palette == NULL ||
        road->tileset.tile_bytes < 64 || road->tileset.palette_count == 0 ||
        line_count < first_record + last_line - first_line)
        return PORTABLE_ROAD_RENDER_CAPACITY;

    if (last_line > frame->height*scale)
        last_line = frame->height*scale;
    /* Colour 0 and colours past the imported palette stay transparent. */
    uint32_t rgb[256];
    uint8_t opaque[256];
    for (size_t index = 0u; index < 256u; ++index)
    {
        opaque[index] = (uint8_t)(index != 0u && index < road->tileset.palette_count);
        rgb[index] = opaque[index] ? color_to_rgb(road->tileset.palette[index]) : 0u;
    }
    const size_t map_width_pixels = road->map.width_tiles * 8u;
    const size_t map_height_pixels = road->map.height_tiles * 8u;
    const uint64_t width_mask = (map_width_pixels & (map_width_pixels - 1u)) == 0u
        ? (uint64_t)map_width_pixels - 1u : 0u;
    const uint64_t height_mask = (map_height_pixels & (map_height_pixels - 1u)) == 0u
        ? (uint64_t)map_height_pixels - 1u : 0u;
    const size_t tile_count = road->tileset.tile_bytes / 64u;
    const size_t stride = portable_software_frame_stride(frame);
    for (size_t y = first_line; y < last_line; ++y)
    {
        const PortableCameraAffineLine *line =
            &lines[first_record + y - first_line];
        uint32_t *const row = frame->pixels + y * stride;

        for (size_t x = 0; x < frame->width*scale; ++x)
        {
            const int64_t source_x = (int64_t)line->dx +
                               (int64_t)x * line->pa/(int64_t)scale;
            const int64_t source_y = (int64_t)line->dy +
                               (int64_t)x * line->pc/(int64_t)scale;
            /* For power-of-two maps floor(v / 256) mod size is just bits
             * 8.. of v's two's complement value. */
            const size_t pixel_x = width_mask != 0u
                ? (size_t)(((uint64_t)source_x >> 8) & width_mask)
                : positive_mod(floor_div256(source_x), map_width_pixels);
            const size_t pixel_y = height_mask != 0u
                ? (size_t)(((uint64_t)source_y >> 8) & height_mask)
                : positive_mod(floor_div256(source_y), map_height_pixels);
            /* Affine maps are flat: BG2's 1024x1024 mode is 128 entries wide. */
            const size_t map_index = (pixel_y / 8u) * road->map.width_tiles + pixel_x / 8u;

            if (map_index >= road->map.map_bytes || road->map.map[map_index] >= tile_count)
                continue;
            const size_t byte_offset = (size_t)road->map.map[map_index] * 64u +
                (pixel_y & 7u) * 8u + (pixel_x & 7u);
            if (byte_offset >= road->tileset.tile_bytes)
                continue;
            const uint8_t color_index = road->tileset.tiles[byte_offset];
            if (opaque[color_index]) {
                if(!alpha_road)row[x]=rgb[color_index];
                else {
                    uint32_t mixed=0u;
                    for(unsigned shift=0u;shift<24u;shift+=8u){
                        unsigned channel=(((rgb[color_index]>>shift)&255u)*13u+
                            ((row[x]>>shift)&255u)*6u)/16u;
                        if(channel>255u)channel=255u;
                        mixed|=(uint32_t)channel<<shift;
                    }
                    row[x]=mixed;
                }
            }
        }
    }
    return PORTABLE_ROAD_RENDER_OK;
}

int portable_road_render(const PortableRoadView *road,
                         const PortableCameraAffineLine *lines,
                         size_t line_count,
                         PortableSoftwareFrame *frame)
{
    return render_road(road, lines, line_count, frame, 0, 0u,0,-1);
}

int portable_road_render_result(const PortableRoadView *road,
                                const PortableCameraAffineLine *lines,
                                size_t line_count,
                                PortableSoftwareFrame *frame)
{
    return render_road(road, lines, line_count, frame, 1, 0u,0,-1);
}

int portable_road_render_result_offset(const PortableRoadView *road,
    const PortableCameraAffineLine *lines, size_t line_count,
    PortableSoftwareFrame *frame, uint8_t scanline_offset)
{
    if(scanline_offset>24u)return PORTABLE_ROAD_RENDER_INVALID_ARGUMENT;
    return render_road(road, lines, line_count, frame, 1, scanline_offset,0,-1);
}

int portable_road_render_motion(const PortableRoadView *road,
    const PortableCameraAffineLine *lines,size_t line_count,
    PortableSoftwareFrame *frame,const PortableSkyMotionSnapshot *motion)
{
    if(motion==NULL||!motion->initialized||motion->road_line<0||motion->road_line>160||
       motion->road_mode>PORTABLE_SKY_ROAD_ALPHA_OCCLUDES_FRONT)
        return PORTABLE_ROAD_RENDER_INVALID_ARGUMENT;
    const int first_row=portable_sky_motion_road_first_row(motion,
        (unsigned)(frame?portable_software_frame_scale(frame):1u));
    if(first_row<0)return PORTABLE_ROAD_RENDER_INVALID_ARGUMENT;
    return render_road(road,lines,line_count,frame,1,0,
        motion->road_mode==PORTABLE_SKY_ROAD_ALPHA_OCCLUDES_FRONT,first_row);
}
