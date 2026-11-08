#include "sky_renderer.h"

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static size_t positive_mod(int32_t value, size_t modulus)
{
    int64_t wide = value;
    int64_t remainder = wide % (int64_t)modulus;

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

/* One GBA text background prepared for row-wise sampling: wrap sizes in
 * pixels and the 16 banks x 16 colours already converted (index 0 and
 * colours outside the imported palette stay transparent). */
typedef struct
{
    const PortableSkyAssetView *sky;
    const uint8_t *map;
    size_t map_width;
    size_t width_pixels;
    size_t height_pixels;
    uint32_t rgb[256];
    uint8_t opaque[256];
} SkyLayer;

static int sky_layer_init(SkyLayer *layer, const PortableSkyAssetView *sky,
                          const uint8_t *map, size_t map_width)
{
    const size_t map_bytes = map == sky->front_map ? sky->front_map_bytes :
        map == sky->mid_map ? sky->mid_map_bytes : sky->back_map_bytes;
    const size_t map_height = map_bytes / (map_width * 2u);

    if (map_height == 0u)
        return 0;
    layer->sky = sky;
    layer->map = map;
    layer->map_width = map_width;
    layer->width_pixels = map_width * 8u;
    layer->height_pixels = map_height * 8u;
    for (size_t index = 0u; index < 256u; ++index)
    {
        layer->opaque[index] = (uint8_t)((index & 15u) != 0u &&
            index >= sky->palette_hardware_base &&
            index - sky->palette_hardware_base < sky->palette_count);
        layer->rgb[index] = layer->opaque[index]
            ? color_to_rgb(sky->palette[index - sky->palette_hardware_base]) : 0u;
    }
    return 1;
}

static void blend_pixel(PortableSoftwareFrame *frame,size_t index,uint32_t color,
    unsigned alpha_a,unsigned alpha_b)
{
    if(alpha_a==8u&&alpha_b==16u){portable_software_frame_add_half(frame,index,color);return;}
    const size_t scale=portable_software_frame_scale(frame),stride=portable_software_frame_stride(frame);
    const size_t first=(index/frame->width)*stride*scale+(index%frame->width)*scale;
    for(size_t y=0u;y<scale;++y)for(size_t x=0u;x<scale;++x){
        uint32_t *destination=&frame->pixels[first+y*stride+x];
        uint32_t mixed=0u;
        for(unsigned shift=0u;shift<24u;shift+=8u){
            unsigned channel=(((color>>shift)&255u)*alpha_a+((*destination>>shift)&255u)*alpha_b)/16u;
            if(channel>255u)channel=255u;
            mixed|=(uint32_t)channel<<shift;
        }
        *destination=mixed;
    }
}

/* Samples one output row starting at world (x, y) and stores (or, for
 * half, blends) every opaque pixel. Same addressing as the hardware text
 * map, walked one map entry (up to eight pixels) at a time. */
static void sky_layer_row_blended(const SkyLayer *layer, PortableSoftwareFrame *frame,
                          size_t y, int32_t world_x, int32_t world_y, int alpha_a,int alpha_b)
{
    const PortableSkyAssetView *sky = layer->sky;
    const size_t map_y = positive_mod(world_y, layer->height_pixels);
    const size_t tile_row = map_y / 8u;
    const size_t row_pixel_y = map_y & 7u;
    const int direct = alpha_a<0 && frame->scale <= 1u && frame->stride == 0u;
    uint32_t *const row = frame->pixels + y * frame->width;
    size_t map_x = positive_mod(world_x, layer->width_pixels);
    size_t x = 0u;

    while (x < frame->width)
    {
        /* A 64x32 GBA text map stores two 32x32 screenblocks side by side. */
        const size_t map_index = layer->map_width == 64u
            ? (map_x / 256u) * 1024u + tile_row * 32u + ((map_x / 8u) & 31u)
            : tile_row * layer->map_width + map_x / 8u;
        const uint16_t entry = read_le16(layer->map + 2u * map_index);
        const size_t tile_base = (size_t)(entry & 0x03ffu) * 32u +
            ((entry & 0x0800u) != 0u ? 7u - row_pixel_y : row_pixel_y) * 4u;
        const size_t bank = (size_t)(entry >> 12) * 16u;
        const size_t first = map_x & 7u;
        size_t span = 8u - first;

        if (span > frame->width - x)
            span = frame->width - x;
        /* The eight 4bpp pixels of this tile row, pixel p in nibble p after
         * a horizontal flip; bytes past the tile data read as transparent. */
        uint32_t bits = 0u;
        for (size_t byte = 0u; byte < 4u; ++byte)
            if (tile_base + byte < sky->tile_bytes)
                bits |= (uint32_t)sky->tiles[tile_base + byte] << (8u * byte);
        if ((entry & 0x0400u) != 0u)
        {
            uint32_t flipped = 0u;
            for (size_t pixel = 0u; pixel < 8u; ++pixel)
                flipped |= ((bits >> (4u * pixel)) & 15u) << (4u * (7u - pixel));
            bits = flipped;
        }
        if (bits != 0u)
            for (size_t step = 0u; step < span; ++step)
            {
                const size_t palette_index = bank + ((bits >> (4u * (first + step))) & 15u);
                if (!layer->opaque[palette_index])
                    continue;
                if (direct)
                    row[x + step] = layer->rgb[palette_index];
                else if (alpha_a>=0)
                    blend_pixel(frame,y*frame->width+x+step,layer->rgb[palette_index],
                        (unsigned)alpha_a,(unsigned)alpha_b);
                else
                    portable_software_frame_store(frame, y * frame->width + x + step, layer->rgb[palette_index]);
            }
        x += span;
        map_x += span;
        if (map_x >= layer->width_pixels)
            map_x -= layer->width_pixels;
    }
}

/* Fractional movement changes the physical placement of whole source texels.
 * Group physical rows sharing one source Y, decode each texel once per group,
 * and write its run into every row without sharing destination blend values. */
static void sky_layer_row_precise_range(const SkyLayer *layer, PortableSoftwareFrame *frame,
    size_t y, int64_t world_x_q16, int64_t world_y_q16, int alpha_a, int alpha_b,
    size_t first_row, size_t last_row)
{
    const size_t scale = portable_software_frame_scale(frame);
    const size_t stride = portable_software_frame_stride(frame);
    const size_t width = frame->width * scale;
    const int64_t scaled_x = portable_software_frame_floor_div(world_x_q16 * (int64_t)scale, 65536);
    const int64_t scaled_y = portable_software_frame_floor_div(world_y_q16 * (int64_t)scale, 65536);
    for (size_t row = first_row; row < last_row;)
    {
        const int32_t source_y = (int32_t)portable_software_frame_floor_div(scaled_y + (int64_t)row, (int64_t)scale);
        size_t end_row = row + scale - positive_mod((int32_t)(scaled_y + (int64_t)row), scale);
        if (end_row > last_row)
            end_row = last_row;
        const size_t map_y = positive_mod(source_y, layer->height_pixels);
        size_t map_x = positive_mod((int32_t)scaled_x, layer->width_pixels * scale);
        size_t x = 0u;
        while (x < width)
        {
            const size_t source_x = map_x / scale;
            const size_t map_index = layer->map_width == 64u
                ? (source_x / 256u) * 1024u + (map_y / 8u) * 32u + ((source_x / 8u) & 31u)
                : (map_y / 8u) * layer->map_width + source_x / 8u;
            const uint16_t entry = read_le16(layer->map + map_index * 2u);
            const size_t tile_y = (entry & 0x0800u) ? 7u - (map_y & 7u) : map_y & 7u;
            const size_t offset = (size_t)(entry & 0x03ffu) * 32u + tile_y * 4u;
            uint32_t bits = 0u;
            for (size_t byte = 0u; byte < 4u; ++byte)
                if (offset + byte < layer->sky->tile_bytes)
                    bits |= (uint32_t)layer->sky->tiles[offset + byte] << (byte * 8u);
            if (entry & 0x0400u)
            {
                uint32_t flipped = 0u;
                for (size_t pixel = 0u; pixel < 8u; ++pixel)
                    flipped |= ((bits >> (pixel * 4u)) & 15u) << ((7u - pixel) * 4u);
                bits = flipped;
            }
            const size_t bank = (size_t)(entry >> 12u) * 16u;
            size_t texel = source_x & 7u, phase = map_x % scale;
            size_t tile_span = (8u - texel) * scale - phase;
            if (tile_span > width - x)
                tile_span = width - x;
            size_t consumed = 0u;
            while (bits != 0u && consumed < tile_span)
            {
                const size_t palette_index = bank + ((bits >> (texel * 4u)) & 15u);
                size_t span = scale - phase;
                if (span > tile_span - consumed)
                    span = tile_span - consumed;
                if (layer->opaque[palette_index])
                {
                    const uint32_t rgb = layer->rgb[palette_index];
                    uint32_t *target_row = frame->pixels + (y * scale + row) * stride + x + consumed;
                    if (alpha_a < 0)
                    {
                        for (size_t physical_y = row; physical_y < end_row; ++physical_y)
                        {
                            for (size_t pixel = 0u; pixel < span; ++pixel)
                                target_row[pixel] = rgb;
                            if (physical_y + 1u < end_row)
                                target_row += stride;
                        }
                    }
                    else if (alpha_a == 8 && alpha_b == 16)
                    {
                        const uint32_t half = (rgb & UINT32_C(0x00fefefe)) >> 1u;
                        for (size_t physical_y = row; physical_y < end_row; ++physical_y)
                        {
                            for (size_t pixel = 0u; pixel < span; ++pixel)
                            {
                                uint32_t mixed = 0u;
                                for (unsigned shift = 0u; shift < 24u; shift += 8u)
                                {
                                    const unsigned channel = ((target_row[pixel] >> shift) & 255u) +
                                        ((half >> shift) & 255u);
                                    mixed |= (uint32_t)(channel > 255u ? 255u : channel) << shift;
                                }
                                target_row[pixel] = mixed;
                            }
                            if (physical_y + 1u < end_row)
                                target_row += stride;
                        }
                    }
                    else
                    {
                        for (size_t physical_y = row; physical_y < end_row; ++physical_y)
                        {
                            for (size_t pixel = 0u; pixel < span; ++pixel)
                            {
                                uint32_t mixed = 0u;
                                for (unsigned shift = 0u; shift < 24u; shift += 8u)
                                {
                                    unsigned channel = (((rgb >> shift) & 255u) * (unsigned)alpha_a +
                                        ((target_row[pixel] >> shift) & 255u) * (unsigned)alpha_b) / 16u;
                                    if (channel > 255u) channel = 255u;
                                    mixed |= (uint32_t)channel << shift;
                                }
                                target_row[pixel] = mixed;
                            }
                            if (physical_y + 1u < end_row)
                                target_row += stride;
                        }
                    }
                }
                consumed += span;
                ++texel;
                phase = 0u;
            }
            x += tile_span;
            map_x += tile_span;
            if (map_x >= layer->width_pixels * scale)
                map_x -= layer->width_pixels * scale;
        }
        row = end_row;
    }
}

static void sky_layer_row_precise(const SkyLayer *layer, PortableSoftwareFrame *frame,
    size_t y, int64_t world_x_q16, int64_t world_y_q16, int alpha_a, int alpha_b)
{
    sky_layer_row_precise_range(layer,frame,y,world_x_q16,world_y_q16,alpha_a,alpha_b,
        0u,portable_software_frame_scale(frame));
}

static void sky_layer_row(const SkyLayer *layer,PortableSoftwareFrame *frame,
    size_t y,int32_t world_x,int32_t world_y,int half)
{
    sky_layer_row_blended(layer,frame,y,world_x,world_y,half?8:-1,16);
}

static int32_t sky_center_offset(const PortableSoftwareFrame *frame)
{
    return (int32_t)((frame->width > 240u ? frame->width - 240u : 0u) / 2u);
}

static void render_layer(const PortableSkyAssetView *sky,
                         PortableSoftwareFrame *frame,
                         const uint8_t *map,
                         size_t map_width,
                         int32_t scroll_x,
                         int32_t scroll_y)
{
    SkyLayer layer;

    if (!sky_layer_init(&layer, sky, map, map_width))
        return;
    for (size_t y = 0; y < frame->height; ++y)
        sky_layer_row(&layer, frame, y, scroll_x - sky_center_offset(frame),
                      (int32_t)y + scroll_y, 0);
}

static int render_layer_precise(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame, const uint8_t *map, size_t map_width,
    int64_t scroll_x_q16, int32_t scroll_y, int foreground)
{
    SkyLayer layer;
    if (!sky_layer_init(&layer, sky, map, map_width))
        return 1;
    for (size_t y = 0u; y < frame->height; ++y)
        sky_layer_row_precise(&layer, frame, y,
            scroll_x_q16 - (int64_t)sky_center_offset(frame) * 65536,
            ((int64_t)y + scroll_y) * 65536, foreground && y >= 74u ? 8 : -1, 16);
    return 1;
}

int portable_sky_render(const PortableSkyAssetView *sky,
                        PortableSoftwareFrame *frame,
                        int32_t scroll_x,
                        int32_t scroll_y)
{
    if (sky == NULL || frame == NULL || frame->pixels == NULL ||
        sky->tiles == NULL || sky->palette == NULL || sky->front_map == NULL ||
        sky->mid_map == NULL || sky->back_map == NULL ||
        sky->front_width_tiles == 0 || sky->mid_width_tiles == 0 ||
        sky->back_width_tiles == 0 || sky->map_height_tiles == 0)
        return 0;
    render_layer(sky, frame, sky->back_map, sky->back_width_tiles, scroll_x,
                 scroll_y);
    render_layer(sky, frame, sky->mid_map, sky->mid_width_tiles, scroll_x,
                 scroll_y);
    render_layer(sky, frame, sky->front_map, sky->front_width_tiles, scroll_x,
                 scroll_y);
    return 1;
}

int portable_sky_render_middle(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame, int32_t scroll_x, int32_t scroll_y)
{
    if (sky == NULL || frame == NULL || frame->pixels == NULL ||
        sky->tiles == NULL || sky->palette == NULL || sky->mid_map == NULL ||
        sky->mid_width_tiles == 0u || sky->map_height_tiles == 0u)
        return 0;
    render_layer(sky, frame, sky->mid_map, sky->mid_width_tiles,
                 scroll_x, scroll_y);
    return 1;
}

int portable_sky_render_middle_yaw(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame, int16_t camera_yaw)
{
    if (frame != NULL && portable_software_frame_scale(frame) > 1u)
    {
        if (sky == NULL || frame->pixels == NULL || sky->tiles == NULL ||
            sky->palette == NULL || sky->mid_map == NULL ||
            sky->mid_width_tiles == 0u || sky->map_height_tiles == 0u)
            return 0;
        return render_layer_precise(sky, frame, sky->mid_map, sky->mid_width_tiles,
            (int64_t)camera_yaw * 512, 70, 0);
    }
    return portable_sky_render_middle(sky, frame, camera_yaw / 128, 70);
}

int portable_sky_render_race_back(const PortableSkyAssetView *sky,
                                  PortableSoftwareFrame *frame,
                                  int16_t camera_yaw)
{
    if (frame != NULL && portable_software_frame_scale(frame) > 1u)
    {
        if (sky == NULL || frame->pixels == NULL || sky->tiles == NULL ||
            sky->palette == NULL || sky->back_map == NULL || sky->mid_map == NULL ||
            sky->back_width_tiles == 0u || sky->mid_width_tiles == 0u || sky->map_height_tiles == 0u)
            return 0;
        return render_layer_precise(sky, frame, sky->back_map, sky->back_width_tiles,
                   (int64_t)camera_yaw * 256, 70, 0) &&
               render_layer_precise(sky, frame, sky->mid_map, sky->mid_width_tiles,
                   (int64_t)camera_yaw * 512, 70, 0);
    }
    return portable_sky_render_race_back_scrolled(
        sky, frame, camera_yaw / 256, camera_yaw / 128, 70, 70);
}

int portable_sky_render_race_back_scrolled(const PortableSkyAssetView *sky,
                                          PortableSoftwareFrame *frame,
                                          int32_t back_x, int32_t middle_x,
                                          int32_t back_y, int32_t middle_y)
{
    if (sky == NULL || frame == NULL || frame->pixels == NULL ||
        sky->tiles == NULL || sky->palette == NULL || sky->back_map == NULL ||
        sky->mid_map == NULL || sky->back_width_tiles == 0 ||
        sky->mid_width_tiles == 0 || sky->map_height_tiles == 0)
        return 0;
    render_layer(sky, frame, sky->back_map, sky->back_width_tiles,
                 back_x, back_y);
    /* Above the horizon BG2 is the middle sky. The affine road replaces it
     * for scanlines 0x40..0x9f in the race scene renderer. */
    render_layer(sky, frame, sky->mid_map, sky->mid_width_tiles,
                 middle_x, middle_y);
    return 1;
}

int portable_sky_render_race_front(const PortableSkyAssetView *sky,
                                   PortableSoftwareFrame *frame,
                                   int16_t camera_yaw)
{
    if (frame != NULL && portable_software_frame_scale(frame) > 1u)
    {
        if (sky == NULL || frame->pixels == NULL || sky->tiles == NULL ||
            sky->palette == NULL || sky->front_map == NULL ||
            sky->front_width_tiles == 0u || sky->map_height_tiles == 0u)
            return 0;
        return render_layer_precise(sky, frame, sky->front_map, sky->front_width_tiles,
            (int64_t)camera_yaw * 1024 - INT64_C(40) * 65536, 70, 1);
    }
    return portable_sky_render_race_front_scrolled(
        sky, frame, camera_yaw / 64 - 40, 70);
}

int portable_sky_render_race_front_scrolled(const PortableSkyAssetView *sky,
                                           PortableSoftwareFrame *frame,
                                           int32_t front_x,
                                           int32_t scroll_y)
{
    return portable_sky_render_race_front_scrolled_offset(
        sky, frame, front_x, scroll_y, 0u);
}

int portable_sky_render_race_front_scrolled_offset(
    const PortableSkyAssetView *sky, PortableSoftwareFrame *frame,
    int32_t front_x, int32_t scroll_y, uint8_t scanline_offset)
{
    if (sky == NULL || frame == NULL || frame->pixels == NULL ||
        sky->tiles == NULL || sky->palette == NULL || sky->front_map == NULL ||
        sky->front_width_tiles == 0 || sky->map_height_tiles == 0)
        return 0;
    if (scanline_offset > 24u)
        return 0;
    /* The result scene keeps BLDCNT=0x2c42 and BLDALPHA=0x1008: BG1
     * contributes 8/16 over the full-strength lower BG/backdrop. In the
     * paired source frame the upper sky remains opaque through line 73;
     * the first blended horizon row is line 74. The upper BG2/HBlank
     * handoff still needs separate recovery. */
    SkyLayer layer;
    if (!sky_layer_init(&layer, sky, sky->front_map, sky->front_width_tiles))
        return 1;
    for (size_t y = 0; y < frame->height; ++y)
        sky_layer_row(&layer, frame, y, front_x - sky_center_offset(frame),
                      (int32_t)y + scroll_y, y >= 74u + scanline_offset);
    return 1;
}

int portable_sky_render_front(const PortableSkyAssetView *sky,
                              PortableSoftwareFrame *frame,
                              int32_t scroll_x,
                              int32_t scroll_y)
{
    if (sky == NULL || frame == NULL || frame->pixels == NULL ||
        sky->tiles == NULL || sky->palette == NULL || sky->front_map == NULL ||
        sky->front_width_tiles == 0 || sky->map_height_tiles == 0)
        return 0;
    render_layer(sky, frame, sky->front_map, sky->front_width_tiles, scroll_x,
                 scroll_y);
    return 1;
}

static int motion_valid(const PortableSkyAssetView *sky,const PortableSoftwareFrame *frame,
    const PortableSkyMotionSnapshot *motion)
{
    if(sky==NULL||frame==NULL||frame->pixels==NULL||frame->width==0u||frame->height==0u||
       sky->tiles==NULL||sky->palette==NULL||motion==NULL||!motion->initialized||
       motion->precise>1u||motion->layer_mask>7u||motion->blend_mode>PORTABLE_SKY_BLEND_ALPHA||
       motion->blend_a>16u||motion->blend_b>16u||motion->split_y< -1||motion->split_y>159||
       motion->split_layer>2u||motion->split_vertical>1u||motion->road_mode>PORTABLE_SKY_ROAD_ALPHA_OCCLUDES_FRONT||
       motion->road_line<0||motion->road_line>159||
       portable_sky_motion_road_first_row(motion,(unsigned)portable_software_frame_scale(frame))<0)return 0;
    for(unsigned layer=0u;layer<3u;++layer)if(motion->layer_source[layer]>2u)return 0;
    return 1;
}

/* A fractional road boundary can bisect a logical row. Apply the source
 * foreground priority/blend separately on each physical side of that edge. */
static void render_motion_row(const SkyLayer *prepared, PortableSoftwareFrame *frame,
    const PortableSkyMotionSnapshot *motion, unsigned layer, int road_underlay,
    size_t y, size_t first_row, size_t last_row, int road)
{
    if(first_row==last_row)return;
    int alpha_a=layer==0u&&motion->blend_mode==PORTABLE_SKY_BLEND_ALPHA?motion->blend_a:-1;
    int alpha_b=motion->blend_b;
    if(layer==0u){
        const int occluded=motion->road_mode==PORTABLE_SKY_ROAD_OCCLUDES_FRONT||
            motion->road_mode==PORTABLE_SKY_ROAD_ALPHA_OCCLUDES_FRONT;
        if(road_underlay){if(!road||!occluded)return;alpha_a=-1;}
        else if(road){
            if(occluded)return;
            alpha_a=motion->road_mode==PORTABLE_SKY_ROAD_FRONT_ALPHA?8:-1;
            alpha_b=16;
        }
    }
    const int split=motion->split_y>=0&&layer==motion->split_layer&&y>=(size_t)motion->split_y;
    if(motion->precise&&portable_software_frame_scale(frame)>1u){
        const int32_t x_q16=split&&!motion->split_vertical?
            motion->split_scroll_x_q16:motion->scroll_x_q16[layer];
        const int32_t y_q16=split&&motion->split_vertical?
            motion->split_scroll_y_q16:motion->scroll_y_q16[layer];
        sky_layer_row_precise_range(prepared,frame,y,
            (int64_t)x_q16-(int64_t)sky_center_offset(frame)*65536,
            (int64_t)y*65536+y_q16,alpha_a,alpha_b,first_row,last_row);
    }else{
        const int32_t scroll_x=split&&!motion->split_vertical?motion->split_scroll_x:motion->scroll_x[layer];
        const int32_t scroll_y=split&&motion->split_vertical?motion->split_scroll_y:motion->scroll_y[layer];
        sky_layer_row_blended(prepared,frame,y,scroll_x-sky_center_offset(frame),
            (int32_t)y+scroll_y,alpha_a,alpha_b);
    }
}

static int render_motion_layer(const PortableSkyAssetView *sky,PortableSoftwareFrame *frame,
    const PortableSkyMotionSnapshot *motion,unsigned layer,int road_underlay)
{
    if(!(motion->layer_mask&(1u<<layer)))return 1;
    const uint8_t *maps[3]={sky->front_map,sky->mid_map,sky->back_map};
    const size_t widths[3]={sky->front_width_tiles,sky->mid_width_tiles,sky->back_width_tiles};
    const unsigned source=motion->layer_source[layer];
    SkyLayer prepared;
    if(maps[source]==NULL||widths[source]==0u||
       !sky_layer_init(&prepared,sky,maps[source],widths[source]))return 0;
    const size_t scale=portable_software_frame_scale(frame);
    const size_t road_first=(size_t)portable_sky_motion_road_first_row(motion,(unsigned)scale);
    for(size_t y=0u;y<frame->height;++y){
        if(layer!=0u){
            render_motion_row(&prepared,frame,motion,layer,road_underlay,y,0u,scale,0);
            continue;
        }
        const size_t row_start=y*scale;
        const size_t before=road_first<=row_start?0u:
            road_first-row_start<scale?road_first-row_start:scale;
        render_motion_row(&prepared,frame,motion,layer,road_underlay,y,0u,before,0);
        render_motion_row(&prepared,frame,motion,layer,road_underlay,y,before,scale,1);
    }
    return 1;
}

int portable_sky_render_motion_back(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame,const PortableSkyMotionSnapshot *motion)
{
    return motion_valid(sky,frame,motion)&&render_motion_layer(sky,frame,motion,2u,0)&&
        render_motion_layer(sky,frame,motion,1u,0)&&
        render_motion_layer(sky,frame,motion,0u,1);
}

int portable_sky_render_motion_front(const PortableSkyAssetView *sky,
    PortableSoftwareFrame *frame,const PortableSkyMotionSnapshot *motion)
{
    return motion_valid(sky,frame,motion)&&render_motion_layer(sky,frame,motion,0u,0);
}
