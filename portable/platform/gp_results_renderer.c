#include "gp_results_renderer.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "../core/race_session.h"
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}



static uint32_t rgb(uint16_t color)
{
    const uint32_t r5 = color & 31u;
    const uint32_t g5 = (color >> 5u) & 31u;
    const uint32_t b5 = (color >> 10u) & 31u;
    return (((r5 << 3u) | (r5 >> 2u)) << 16u) |
           (((g5 << 3u) | (g5 >> 2u)) << 8u) |
           ((b5 << 3u) | (b5 >> 2u));
}

/* Preserve fractional map coordinates until physical placement. At native
 * resolution this is the source's signed arithmetic shift. */
static int marker_physical_coordinate(int32_t world_q16, unsigned shift,
                                      int origin, int scale)
{
    return (int)portable_software_frame_floor_div((int64_t)world_q16 * scale,
                                                 INT64_C(1) << shift) + origin * scale;
}

static void draw_race_marker(const uint8_t *tile, const uint16_t *colors,
                             int x, int y, PortableSoftwareFrame *frame)
{
    const int scale = (int)portable_software_frame_scale(frame);
    const int width = (int)frame->width * scale;
    const int height = (int)frame->height * scale;
    for (unsigned row = 0u; row < 8u; ++row)
        for (unsigned column = 0u; column < 8u; ++column)
        {
            const unsigned packed = tile[row * 4u + column / 2u];
            const unsigned color = (column & 1u) ? packed >> 4u : packed & 15u;
            if (!color)
                continue;
            const uint32_t pixel = rgb(colors[color]);
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx)
                {
                    const int dx = x + (int)column * scale + sx;
                    const int dy = y + (int)row * scale + sy;
                    if (dx >= 0 && dy >= 0 && dx < width && dy < height)
                        portable_software_frame_store_physical(frame, (size_t)dx, (size_t)dy, pixel);
                }
        }
}

int portable_gp_results_render_race_markers(const PortableGpMinimapMarkerAssetView *asset,
    const PortableRaceMinimapMarker *markers, uint8_t count, uint8_t coordinate_shift,
    uint32_t marker_clock, PortableSoftwareFrame *frame)
{
    if (!asset || !asset->tiles || !asset->palette || !markers || !count || count > 8u ||
        (coordinate_shift != 20u && coordinate_shift != 21u) || !frame || !frame->pixels)
        return 0;
    unsigned selected = count;
    for (unsigned i = 0u; i < count; ++i)
        if (markers[i].selected)
        {
            if (selected != count)
                return 0;
            selected = i;
        }
    if (selected != 0u)
        return 0;
    PortableSoftwareFrame right = portable_software_frame_native_view(frame, PORTABLE_FRAME_RIGHT);
    const int scale = (int)portable_software_frame_scale(&right);
    /* 4F340 submits selected first. Reverse rasterization preserves its
     * highest priority while retaining source4E7EC's visibility predicate. */
    for (unsigned reverse = count; reverse > 0u; --reverse)
    {
        const PortableRaceMinimapMarker *marker = &markers[reverse - 1u];
        if (!(markers[selected].class_flags & 8u) && (marker->class_flags & 8u))
            continue;
        unsigned tile = (!marker->selected && (marker->source_flags & 0x8000u)) ? 1u : 0u;
        unsigned palette7 = 1u;
        if ((marker->source_flags & 0x20u) || (marker->class_flags & 8u))
        {
            tile = 1u;
            palette7 = 0u;
        }
        if (marker->selected && (marker_clock & 16u))
        {
            tile = 0u;
            palette7 = 0u;
        }
        const uint16_t *colors = palette7 && asset->rival_palette
            ? asset->rival_palette : asset->palette;
        const int x = marker_physical_coordinate(marker->x_q16, coordinate_shift, 172, scale);
        const int y = marker_physical_coordinate(marker->y_q16, coordinate_shift, 92, scale);
        draw_race_marker(asset->tiles + tile * 32u, colors, x, y, &right);
    }
    return 1;
}

int portable_gp_results_render_minimap_markers_count(
    const PortableGpMinimapMarkerAssetView *asset,const int32_t x_q16[8],const int32_t y_q16[8],
    uint8_t count,uint32_t result_tick,uint8_t rivals_use_bank7,const uint32_t source_flags[8],
    PortableSoftwareFrame *frame)
{
    if(!x_q16||!y_q16||!count||count>8u)return 0;
    PortableRaceMinimapMarker markers[8]={0};
    for(unsigned i=0u;i<count;++i)markers[i]=(PortableRaceMinimapMarker){
        .x_q16=x_q16[i],.y_q16=y_q16[i],.selected=(uint8_t)(i==0u),
        .source_flags=source_flags?source_flags[i]:(i&& !rivals_use_bank7?0x8000u:0u)};
    return portable_gp_results_render_race_markers(asset,markers,count,21u,result_tick,frame);
}

int portable_gp_results_render_minimap_markers(
    const PortableGpMinimapMarkerAssetView *asset,const int32_t x_q16[8],const int32_t y_q16[8],
    uint32_t result_tick,uint8_t rivals_use_bank7,const uint32_t source_flags[8],PortableSoftwareFrame *frame)
{
    return portable_gp_results_render_minimap_markers_count(asset,x_q16,y_q16,8u,
        result_tick,rivals_use_bank7,source_flags,frame);
}

int portable_gp_results_render_lakitu_record(
    const PortableGpResultsAssetView *asset,
    const uint8_t record[PORTABLE_GP_RESULTS_LAKITU_FRAME_BYTES],
    PortableSoftwareFrame *frame)
{
    static const uint8_t dimensions[3][4][2]={
        {{8,8},{16,16},{32,32},{64,64}},
        {{16,8},{32,8},{32,16},{64,32}},
        {{8,16},{8,32},{16,32},{32,64}}};
    const uint8_t *art;
    uint8_t count;
    if(asset==NULL||record==NULL||frame==NULL||frame->pixels==NULL||
       frame->width<240u||frame->height<160u)return 0;
    if(asset->lakitu_art==NULL||
       asset->lakitu_palette==NULL)return 1;
    count=record[1];
    if(count==0u)return 1;
    if(count>3u||record[0]>=asset->lakitu_art_count)return 0;
    art=asset->lakitu_art+
        (size_t)record[0]*PORTABLE_GP_RESULTS_LAKITU_ART_BYTES;
    const int output_scale=(int)portable_software_frame_scale(frame);
    /* Lower source OAM indices win equal-priority overlap. */
    for(size_t reverse=count;reverse!=0u;--reverse){
        const size_t cell=reverse-1u;
        const uint16_t a0=le16(record+2u+cell*6u);
        const uint16_t a1=le16(record+4u+cell*6u);
        const uint16_t a2=le16(record+6u+cell*6u);
        const unsigned shape=a0>>14u,size_code=a1>>14u;
        const int affine=(a0&UINT16_C(0x0100))!=0u;
        const int doubled=(a0&UINT16_C(0x0200))!=0u;
        int x=a1&0x1ffu,y=a0&0xffu;
        int width,height,box_w,box_h;
        if(shape>=3u||((a2>>12u)!=7u)||
           ((a2>>10u)&3u)!=1u||
           (a0&UINT16_C(0x2000))!=0u)return 0;
        width=dimensions[shape][size_code][0];
        height=dimensions[shape][size_code][1];
        if(x>=256)x-=512;
        if(y>=160)y-=256;
        box_w=width<<(affine&&doubled);
        box_h=height<<(affine&&doubled);
        /* GBA OAM x/y names the enlarged affine box's top-left corner. */
        for(int dy=0;dy<box_h*output_scale;++dy){
            const int screen_y=y*output_scale+dy;
            if(screen_y<0||screen_y>=160*output_scale)continue;
            for(int dx=0;dx<box_w*output_scale;++dx){
                const int screen_x=x*output_scale+dx;
                int sx=dx/output_scale,sy=dy/output_scale;
                unsigned tile;
                size_t byte_offset;
                uint8_t pair,index;
                if(screen_x<0||screen_x>=240*output_scale)continue;
                if(affine){
                    const int pa=(int16_t)le16(record+20u);
                    const int pb=(int16_t)le16(record+22u);
                    const int pc=(int16_t)le16(record+24u);
                    const int pd=(int16_t)le16(record+26u);
                    const int cx=dx-box_w/2*output_scale,cy=dy-box_h/2*output_scale;
                    sx=(int)portable_software_frame_floor_div((int64_t)pa*cx+(int64_t)pb*cy,256*output_scale)+width/2;
                    sy=(int)portable_software_frame_floor_div((int64_t)pc*cx+(int64_t)pd*cy,256*output_scale)+height/2;
                }else{
                    if(a1&UINT16_C(0x1000))sx=width-1-sx;
                    if(a1&UINT16_C(0x2000))sy=height-1-sy;
                }
                if(sx<0||sx>=width||sy<0||sy>=height)continue;
                tile=(a2&1023u)+(unsigned)(sy/8)*(unsigned)(width/8)+
                    (unsigned)(sx/8);
                if(tile<0x140u||tile>=0x158u)return 0;
                byte_offset=(size_t)(tile-0x140u)*32u+
                    (size_t)(sy&7)*4u+(size_t)(sx&7)/2u;
                if(byte_offset>=PORTABLE_GP_RESULTS_LAKITU_ART_BYTES)
                    return 0;
                pair=art[byte_offset];
                index=(uint8_t)((sx&1)?pair>>4u:pair&15u);
                if(index!=0u)
                    portable_software_frame_store_physical(frame,(size_t)screen_x,(size_t)screen_y, rgb(asset->lakitu_palette[index]));
            }
        }
    }
    return 1;
}

static int copy_pattern(uint16_t map[1024], const uint8_t *pattern,
                        size_t pattern_bytes, uint8_t x, uint8_t y)
{
    uint16_t width;
    uint16_t height;
    size_t cursor = 4u;
    if (pattern == NULL || pattern_bytes < 4u)
        return 0;
    width = le16(pattern);
    height = le16(pattern + 2u);
    if (pattern_bytes != 4u + (size_t)width * height * 2u)
        return 0;
    for (uint16_t row = 0u; row < height; ++row)
        for (uint16_t column = 0u; column < width; ++column, cursor += 2u)
        {
            const uint16_t value = le16(pattern + cursor);
            const size_t dx = (size_t)x + column;
            const size_t dy = (size_t)y + row;
            if (value != 0u && dx < 32u && dy < 32u)
                map[dy * 32u + dx] = value;
        }
    return 1;
}

static void draw_packed(uint16_t map[1024], const uint16_t *digits,
                        uint8_t packed, uint8_t x, uint8_t y, uint16_t palette)
{
    const uint8_t upper = (uint8_t)(packed >> 4u);
    const uint8_t lower = (uint8_t)(packed & 15u);
    map[(size_t)y * 32u + x] = (uint16_t)(palette | digits[upper]);
    map[(size_t)y * 32u + x + 1u] = (uint16_t)(palette | digits[lower]);
    map[((size_t)y + 1u) * 32u + x] =
        (uint16_t)(palette | digits[upper + 10u]);
    map[((size_t)y + 1u) * 32u + x + 1u] =
        (uint16_t)(palette | digits[lower + 10u]);
}

static void draw_time(uint16_t map[1024], const uint16_t *digits,
                      uint16_t value, uint8_t x, uint8_t y, uint16_t palette)
{
    uint32_t minutes;
    uint32_t seconds;
    uint32_t hundredths;
    if (value > UINT16_C(0xea5f))
    {
        minutes = 9u; seconds = 59u; hundredths = 99u;
    }
    else
    {
        uint32_t total_seconds = value / 100u;
        hundredths = value - total_seconds * 100u;
        minutes = total_seconds / 60u;
        seconds = total_seconds - minutes * 60u;
    }
    draw_packed(map, digits,
                (uint8_t)(((minutes / 10u) << 4u) | (minutes % 10u)),
                x, y, palette);
    draw_packed(map, digits,
                (uint8_t)(((seconds / 10u) << 4u) | (seconds % 10u)),
                (uint8_t)(x + 3u), y, palette);
    draw_packed(map, digits,
                (uint8_t)(((hundredths / 10u) << 4u) | (hundredths % 10u)),
                (uint8_t)(x + 6u), y, palette);
    map[(size_t)y * 32u + x + 2u] = (uint16_t)(palette | UINT16_C(0x005a));
    map[(size_t)y * 32u + x + 5u] = (uint16_t)(palette | UINT16_C(0x0057));
    map[(size_t)y * 32u + x] = 0u;
    map[((size_t)y + 1u) * 32u + x] = 0u;
}

static void copy_background(const PortableGpResultsAssetView *asset,
                            uint16_t map[1024], uint8_t progress)
{
    for (int row = 0; row < 5; ++row)
        for (int column = 0; column < 18; ++column)
        {
            const int dy = row - progress;
            if (dy >= 0)
                map[(size_t)dy * 32u + (size_t)column] =
                    asset->background_map[(size_t)row * 32u + (size_t)column];
        }
    for (int row = 0; row < 9; ++row)
        for (int column = 0; column < 9; ++column)
        {
            const int dx = 22 + progress + column;
            const int dy = 12 + progress + row;
            if (dx <= 29 && dy <= 19)
                map[(size_t)dy * 32u + (size_t)dx] =
                    asset->background_map[(size_t)(12 + row) * 32u +
                                          (size_t)(22 + column)];
        }
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 5; ++column)
            map[(size_t)(16 + row) * 32u + (size_t)(1 + column)] =
                asset->background_map[(size_t)(16 + row) * 32u +
                                      (size_t)(1 + column)];
}

typedef struct {
    const PortableNativeImage *image;
    int x,y,width;
} RecordPart;
typedef struct { RecordPart parts[12];unsigned count; } RecordParts;

static int record_pixel_hidden(const RecordParts *parts,int x,int y)
{
    if(parts)for(unsigned i=0u;i<parts->count;++i){
        const RecordPart *part=&parts->parts[i];
        if(x>=part->x&&x<part->x+part->width&&y>=part->y&&y<part->y+16)return 1;
    }
    return 0;
}

static int render_map_data_masked(const uint8_t *tiles, size_t tile_bytes,
                           const uint16_t *palette, size_t palette_count,
                           const uint16_t map[1024],
                           PortableSoftwareFrame *frame,const RecordParts *parts)
{
    if (frame->width < 240u || frame->height < 160u)
        return 0;
    for (size_t y = 0u; y < 160u; ++y)
        for (size_t x = 0u; x < 240u; ++x)
        {
            if(record_pixel_hidden(parts,(int)x,(int)y))continue;
            const uint16_t entry = map[(y / 8u) * 32u + x / 8u];
            const size_t tile = entry & UINT16_C(0x03ff);
            const size_t palette_index = (size_t)(entry >> 12u) * 16u;
            size_t tx = x & 7u;
            size_t ty = y & 7u;
            uint8_t packed;
            uint8_t color;
            if ((entry & UINT16_C(0x0400)) != 0u) tx = 7u - tx;
            if ((entry & UINT16_C(0x0800)) != 0u) ty = 7u - ty;
            if (tile * 32u + ty * 4u + tx / 2u >= tile_bytes ||
                palette_index + 15u >= palette_count)
                return 0;
            packed = tiles[tile * 32u + ty * 4u + tx / 2u];
            color = (tx & 1u) != 0u ? (uint8_t)(packed >> 4u) :
                                      (uint8_t)(packed & 15u);
            /* GBA text-background palette index zero reveals the lower
             * priority race layer.  Preserve the caller's composed scene for
             * those pixels instead of painting palette entry zero. */
            if (color != 0u)
                portable_software_frame_store(frame,y * frame->width + x, rgb(palette[palette_index + color]));
        }
    return 1;
}

static int render_map_data(const uint8_t *tiles,size_t tile_bytes,const uint16_t *palette,
    size_t palette_count,const uint16_t map[1024],PortableSoftwareFrame *frame)
{ return render_map_data_masked(tiles,tile_bytes,palette,palette_count,map,frame,NULL); }

static int render_record_map(const PortableGpResultsAssetView *asset,const PortableGpResultsFlow *flow,
    const uint16_t *palette,const uint16_t map[1024],PortableSoftwareFrame *frame,int sliding)
{
    RecordParts parts={0};
    for(unsigned row=0u;row<flow->record_row_count;++row){
        const PortableGpResultsRecordRowCommand *command=&flow->record_rows[row];
        const PortableGpResultsRecordArt *art=&asset->record_art[row];
        if(!command->roster_driver_id||command->roster_driver_id!=art->roster_driver_id||command->identity==9u)continue;
        const unsigned tile_y=4u+2u*command->display_row+(command->display_row==6u);
        unsigned tile_x=0u;
        if(sliding){
            int found=0;
            for(unsigned band=0u;band<flow->slide_band_count;++band)
                if(flow->slide_bands[band].source_y==tile_y){tile_x=flow->slide_bands[band].destination_x;found=1;break;}
            if(!found)continue;
        }
        for(unsigned part=0u;part<2u;++part){
            const PortableNativeImage *image=part?art->name:art->portrait;
            if(!image)continue;
            const unsigned width=part?40u:16u;
            if(!image->pixels||image->width==0u||image->width>width||image->height==0u||image->height>16u)return 0;
            parts.parts[parts.count++]=(RecordPart){image,(int)(tile_x*8u+(part?48u:16u)),(int)(tile_y*8u),(int)width};
        }
    }
    if(!render_map_data_masked(asset->followup_tiles,asset->followup_tile_bytes,palette,
        asset->followup_palette_count,map,frame,&parts))return 0;
    const unsigned scale=(unsigned)portable_software_frame_scale(frame);
    for(unsigned i=0u;i<parts.count;++i){
        const RecordPart *part=&parts.parts[i];
        for(unsigned py=0u;py<part->image->height*scale;++py)
            for(unsigned px=0u;px<part->image->width*scale;++px){
                const int x=part->x*(int)scale+(int)px,y=part->y*(int)scale+(int)py;
                if(x<0||y<0||x>=240*(int)scale||y>=160*(int)scale)continue;
                const uint32_t pixel=portable_native_image_pixel(part->image,px/scale,py/scale);
                if(pixel>>24u)portable_software_frame_store_physical(frame,(size_t)x,(size_t)y,pixel);
            }
    }
    return 1;
}

static int render_map(const PortableGpResultsAssetView *asset,
                      const uint16_t map[1024], PortableSoftwareFrame *frame)
{
    uint16_t palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    if (asset->rank_palette != NULL &&
        asset->rank_palette_count == PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT &&
        asset->palette_count == PORTABLE_GP_RESULTS_PALETTE_COUNT)
    {
        memcpy(palette, asset->palette, sizeof palette);
        memcpy(palette + 12u * 16u, asset->rank_palette,
               PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT * sizeof *palette);
        return render_map_data(asset->tiles, asset->tile_bytes,palette,
                               PORTABLE_GP_RESULTS_PALETTE_COUNT,map,frame);
    }
    return render_map_data(asset->tiles, asset->tile_bytes, asset->palette,
                           asset->palette_count, map, frame);
}

static int valid_arguments(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    const uint16_t times_centiseconds[PORTABLE_GP_RESULTS_MAX_ROWS],
    const PortableSoftwareFrame *frame)
{
    return asset != NULL && flow != NULL && times_centiseconds != NULL &&
        frame != NULL && frame->pixels != NULL && asset->tiles != NULL &&
        asset->palette != NULL && asset->digit_tiles != NULL &&
        asset->header_patterns != NULL && asset->row_patterns != NULL &&
        asset->row_pattern_bytes != NULL && asset->background_map != NULL &&
        asset->tile_bytes == PORTABLE_GP_RESULTS_TILE_BYTES &&
        asset->palette_count == PORTABLE_GP_RESULTS_PALETTE_COUNT &&
        asset->digit_count == PORTABLE_GP_RESULTS_DIGIT_COUNT &&
        asset->header_count == PORTABLE_GP_RESULTS_HEADER_COUNT &&
        asset->row_count == PORTABLE_GP_RESULTS_ROW_COUNT &&
        asset->background_map_count == PORTABLE_GP_RESULTS_MAP_COUNT;
}

static int compose_scoreboard(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    const uint16_t times_centiseconds[PORTABLE_GP_RESULTS_MAX_ROWS],
    uint8_t highlighted_row, uint16_t map[1024],int include_background)
{
    if (!copy_pattern(map,
                      asset->header_patterns +
                          (size_t)flow->header_variant *
                              PORTABLE_GP_RESULTS_HEADER_BYTES,
                      PORTABLE_GP_RESULTS_HEADER_BYTES,
                      flow->header_x, flow->header_y))
        return 0;
    for (uint8_t index = 0u; index < flow->row_count; ++index)
    {
        const PortableGpResultsRowCommand *command = &flow->rows[index];
        const uint8_t source = command->racer_index;
        const uint16_t palette = source == highlighted_row ?
            UINT16_C(0xa000) : UINT16_C(0xc000);
        if (source >= asset->row_count ||
            !copy_pattern(map,
                          asset->row_patterns +
                              (size_t)source * PORTABLE_GP_RESULTS_ROW_BYTES,
                          asset->row_pattern_bytes[source],
                          command->x, command->y))
            return 0;
        draw_time(map, asset->digit_tiles, times_centiseconds[source],
                  (uint8_t)(command->x + 4u + (source == 3u)),
                  (uint8_t)(command->y + (source == 3u)), palette);
    }
    if(include_background)copy_background(asset, map, flow->reveal_progress);
    return 1;
}

int portable_gp_results_render(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    const uint16_t times_centiseconds[PORTABLE_GP_RESULTS_MAX_ROWS],
    uint8_t highlighted_row,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    if (!valid_arguments(asset, flow, times_centiseconds, frame) ||
        flow->reveal_progress == 0u || flow->reveal_progress > 8u ||
        flow->row_count > PORTABLE_GP_RESULTS_MAX_ROWS ||
        flow->header_variant >= asset->header_count)
        return 0;
    if (!compose_scoreboard(asset, flow, times_centiseconds, highlighted_row,
                            map,1))
        return 0;
    return render_map(asset, map, frame);
}

int portable_gp_results_render_rank(
    const PortableGpResultsAssetView *asset,
    uint8_t player_rank,
    PortableSoftwareFrame *frame)
{
    return portable_gp_results_render_rank_with_colors(
        asset,player_rank,NULL,frame);
}

int portable_gp_results_render_rank_with_colors(
    const PortableGpResultsAssetView *asset,
    uint8_t player_rank,
    const uint8_t *rank_colors,
    PortableSoftwareFrame *frame)
{
    static const uint8_t row_lengths[3] = {3u, 3u, 5u};
    unsigned tile_index = 0u;
    if (asset == NULL || frame == NULL || frame->pixels == NULL ||
        asset->rank_tiles == NULL || asset->rank_palette == NULL ||
        asset->rank_tile_bytes != PORTABLE_GP_RESULTS_RANK_COUNT *
                                  PORTABLE_GP_RESULTS_RANK_TILE_BYTES ||
        asset->rank_palette_count != PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT ||
        player_rank >= PORTABLE_GP_RESULTS_RANK_COUNT ||
        frame->width < 240u || frame->height < 160u)
        return 0;
    const uint8_t *rank = asset->rank_tiles +
        (size_t)player_rank * PORTABLE_GP_RESULTS_RANK_TILE_BYTES;
    for (unsigned row = 0u; row < 3u; ++row)
        for (unsigned col = 0u; col < row_lengths[row]; ++col, ++tile_index)
            for (unsigned py = 0u; py < 8u; ++py)
                for (unsigned px = 0u; px < 8u; ++px)
                {
                    const uint8_t packed = rank[tile_index * 32u +
                                                py * 4u + px / 2u];
                    const uint8_t color = (px & 1u) != 0u ?
                        (uint8_t)(packed >> 4u) : (uint8_t)(packed & 15u);
                    if (color != 0u)
                    {
                        const size_t x = (1u + col) * 8u + px;
                        const size_t y = (16u + row) * 8u + py;
                        const uint16_t palette_color =
                            rank_colors != NULL && color >= 9u && color <= 12u
                            ? le16(rank_colors +
                                (size_t)player_rank * 8u +
                                ((size_t)color - 9u) * 2u)
                            : asset->rank_palette[color];
                        portable_software_frame_store(frame,y * frame->width + x, rgb(palette_color));
                    }
                }
    return 1;
}

int portable_gp_results_render_coins(
    const PortableGpResultsAssetView *asset,
    uint8_t coins,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    if (asset == NULL || frame == NULL || frame->pixels == NULL ||
        asset->tiles == NULL || asset->palette == NULL ||
        asset->tile_bytes != PORTABLE_GP_RESULTS_TILE_BYTES ||
        asset->palette_count != PORTABLE_GP_RESULTS_PALETTE_COUNT ||
        coins > 99u)
        return 0;
    /* Keep HUD bank C for the coin; the rank-label bank has different colors. */
    map[32u] = UINT16_C(0xc020);
    map[33u] = (uint16_t)(UINT16_C(0xb010) + coins / 10u);
    map[34u] = (uint16_t)(UINT16_C(0xb010) + coins % 10u);
    return render_map_data(asset->tiles, asset->tile_bytes,
                           asset->palette, PORTABLE_GP_RESULTS_PALETTE_COUNT,
                           map, frame);
}

int portable_gp_results_render_exit_wipe(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    const uint16_t times_centiseconds[PORTABLE_GP_RESULTS_MAX_ROWS],
    uint8_t course_in_cup,
    uint8_t highlighted_row,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    PortableGpResultsFlow final;
    if (!valid_arguments(asset, flow, times_centiseconds, frame) ||
        course_in_cup > 3u || !flow->exit_wipe_pending ||
        flow->exit_wipe_progress == 0u ||
        flow->exit_wipe_progress > 43u ||
        flow->exit_band_count > PORTABLE_GP_RESULTS_EXIT_BAND_COUNT)
        return 0;
    memset(&final, 0, sizeof final);
    final.reveal_progress = 8u;
    final.header_x = 9u;
    final.header_y = 8u;
    final.header_variant = 1u;
    final.row_count = (uint8_t)(course_in_cup + 1u);
    for (uint8_t index = 0u; index < final.row_count; ++index)
    {
        final.rows[index].racer_index =
            index < course_in_cup ? index : course_in_cup;
        final.rows[index].x = (uint8_t)(9u - (course_in_cup <= index));
        final.rows[index].y = (uint8_t)(1u + 2u * index);
    }
    if (!compose_scoreboard(asset, &final, times_centiseconds,
                            highlighted_row, map,1))
        return 0;
    for (uint8_t band = 0u; band < flow->exit_band_count; ++band)
    {
        const PortableGpResultsExitBandCommand *command =
            &flow->exit_bands[band];
        if (command->source_y >= 20u || command->row_count == 0u ||
            (uint16_t)command->source_y + command->row_count > 20u)
            return 0;
        for (uint8_t row = 0u; row < command->row_count; ++row)
            for (int source_x = 0; source_x < 32; ++source_x)
            {
                const int destination_x =
                    source_x + command->destination_x;
                if (destination_x >= 0 && destination_x <= 29)
                    map[((size_t)command->source_y + row) * 32u +
                        (size_t)destination_x] =
                        asset->background_map[
                            ((size_t)command->source_y + row) * 32u +
                            (size_t)source_x];
            }
    }
    return render_map(asset, map, frame);
}

int portable_gp_results_render_followup_setup(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    if (asset == NULL || flow == NULL || frame == NULL ||
        frame->pixels == NULL || !flow->followup_graphics_pending ||
        !flow->followup_map_pending || !flow->scroll_map_pending ||
        asset->followup_tiles == NULL || asset->followup_palette == NULL ||
        asset->followup_map == NULL || asset->scroll_map == NULL ||
        asset->followup_tile_bytes != PORTABLE_GP_RESULTS_TILE_BYTES ||
        asset->followup_palette_count != PORTABLE_GP_RESULTS_PALETTE_COUNT ||
        asset->followup_map_count != PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT ||
        asset->scroll_map_count != PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT)
        return 0;
    memcpy(map, asset->followup_map,
           PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT * sizeof *map);
    memcpy(map + 20u * 32u, asset->scroll_map,
           PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT * sizeof *map);
    return render_map_data(asset->followup_tiles, asset->followup_tile_bytes,
                           asset->followup_palette,
                           asset->followup_palette_count, map, frame);
}

static void draw_followup_time(uint16_t map[1024], uint16_t value,
                               uint8_t y, uint16_t palette)
{
    uint32_t total_seconds;
    uint32_t minutes;
    uint32_t seconds;
    uint32_t hundredths;
    static const uint16_t separators[7] = {
        UINT16_C(0x0100), UINT16_C(0x010a), UINT16_C(0x0100),
        UINT16_C(0x0100), UINT16_C(0x010b), UINT16_C(0x0100),
        UINT16_C(0x0100)};
    uint8_t digits[7];
    if (value > UINT16_C(0xea5f)) value = UINT16_C(0xea5f);
    total_seconds = value / 100u;
    hundredths = value - total_seconds * 100u;
    minutes = total_seconds / 60u;
    seconds = total_seconds - minutes * 60u;
    digits[0] = (uint8_t)(minutes % 10u);
    digits[1] = 0u;
    digits[2] = (uint8_t)(seconds / 10u);
    digits[3] = (uint8_t)(seconds % 10u);
    digits[4] = 0u;
    digits[5] = (uint8_t)(hundredths / 10u);
    digits[6] = (uint8_t)(hundredths % 10u);
    for (uint8_t column = 0u; column < 7u; ++column)
    {
        uint16_t tile = separators[column];
        if (column != 1u && column != 4u)
            tile = (uint16_t)(tile + digits[column]);
        map[(size_t)y * 32u + 13u + column] = (uint16_t)(palette | tile);
        map[((size_t)y + 1u) * 32u + 13u + column] =
            (uint16_t)(palette | (uint16_t)(tile + UINT16_C(0x20)));
    }
}

static int compose_record_map(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    uint16_t map[1024])
{
    if (asset == NULL || flow == NULL || !flow->record_rows_pending ||
        flow->record_row_count != PORTABLE_GP_RESULTS_RECORD_ROW_COUNT ||
        asset->record_patterns == NULL || asset->object_tiles == NULL ||
        asset->object_palette == NULL ||
        asset->record_pattern_count != PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT ||
        asset->object_tile_bytes != PORTABLE_GP_RESULTS_OBJECT_TILE_BYTES ||
        asset->object_palette_count !=
            PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT ||
        asset->followup_map == NULL || asset->scroll_map == NULL)
        return 0;
    memcpy(map, asset->followup_map,
           PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT * sizeof *map);
    memcpy(map + 20u * 32u, asset->scroll_map,
           PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT * sizeof *map);
    for (uint8_t index = 0u; index < flow->record_row_count; ++index)
    {
        const PortableGpResultsRecordRowCommand *row =
            &flow->record_rows[index];
        const uint16_t palette = row->emphasized ?
            UINT16_C(0xe000) : UINT16_C(0xf000);
        const uint8_t y = (uint8_t)(4u + 2u * row->display_row +
                                    (row->display_row == 6u));
        const uint16_t icon = (uint16_t)(UINT16_C(0x0101) +
            (row->display_row <= 4u ? row->display_row : 0u));
        if (row->identity >= 10u || y + 1u >= 20u)
            return 0;
        map[(size_t)y * 32u + 1u] = (uint16_t)(palette | icon);
        map[((size_t)y + 1u) * 32u + 1u] =
            (uint16_t)(palette | (uint16_t)(icon + UINT16_C(0x20)));
        for (uint8_t column = 0u;
             column < PORTABLE_GP_RESULTS_RECORD_PATTERN_CELLS; ++column)
        {
            uint16_t top = asset->record_patterns[
                (size_t)row->identity * 2u *
                    PORTABLE_GP_RESULTS_RECORD_PATTERN_CELLS + column];
            uint16_t bottom = asset->record_patterns[
                ((size_t)row->identity * 2u + 1u) *
                    PORTABLE_GP_RESULTS_RECORD_PATTERN_CELLS + column];
            if (column >= 4u)
            {
                top = (uint16_t)((top & UINT16_C(0x0fff)) | palette);
                bottom = (uint16_t)((bottom & UINT16_C(0x0fff)) | palette);
            }
            map[(size_t)y * 32u + 2u + column] = top;
            map[((size_t)y + 1u) * 32u + 2u + column] = bottom;
        }
        draw_followup_time(map, row->time_centiseconds, y, palette);
    }
    return 1;
}

int portable_gp_results_render_record_setup(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    if (frame == NULL || frame->pixels == NULL ||
        !compose_record_map(asset, flow, map))
        return 0;
    return render_record_map(asset,flow,asset->followup_palette,map,frame,0);
}

int portable_gp_results_render_record_slide(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame)
{
    uint16_t source[1024] = {0};
    uint16_t map[1024] = {0};
    if (frame == NULL || frame->pixels == NULL || flow == NULL ||
        !flow->record_slide_pending || !flow->map_dirty_pending ||
        flow->record_slide_progress == 0u ||
        flow->record_slide_progress > 59u ||
        flow->slide_band_count != PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT ||
        !compose_record_map(asset, flow, source))
        return 0;
    for (uint8_t band = 0u; band < flow->slide_band_count; ++band)
    {
        const PortableGpResultsSlideBandCommand *command =
            &flow->slide_bands[band];
        if (command->row_count != 2u || command->source_y + 1u >= 20u)
            return 0;
        for (uint8_t row = 0u; row < command->row_count; ++row)
            for (uint8_t column = 0u; column < 20u; ++column)
            {
                const uint8_t destination_x =
                    (uint8_t)(command->destination_x + column);
                if (destination_x <= 29u)
                    map[((size_t)command->source_y + row) * 32u +
                        destination_x] =
                        source[((size_t)command->source_y + row) * 32u +
                               column];
            }
    }
    return render_record_map(asset,flow,asset->followup_palette,map,frame,1);
}

int portable_gp_results_render_menu_reveal(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame)
{
    static const uint8_t band_y[PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT] = {
        0u, 2u, 4u, 6u, 8u, 10u, 12u, 15u, 17u};
    uint16_t source[1024] = {0};
    uint16_t map[1024] = {0};
    uint8_t progress;
    if (frame == NULL || frame->pixels == NULL || flow == NULL ||
        !flow->menu_reveal_pending || !flow->map_dirty_pending ||
        flow->menu_reveal_progress == 0u ||
        flow->menu_reveal_progress > 5u ||
        !compose_record_map(asset, flow, source))
        return 0;
    for (uint8_t band = 0u;
         band < PORTABLE_GP_RESULTS_SLIDE_BAND_COUNT; ++band)
        for (uint8_t row = 0u; row < 2u; ++row)
            memcpy(map + ((size_t)band_y[band] + row) * 32u,
                   source + ((size_t)band_y[band] + row) * 32u,
                   20u * sizeof *map);
    progress = flow->menu_reveal_progress;
    for (uint8_t row = 0u; row < (uint8_t)(progress * 2u); ++row)
        for (uint8_t column = 0u; column < (uint8_t)(progress * 2u);
             ++column)
        {
            const uint8_t destination_x = column < progress ?
                (uint8_t)(20u + column) :
                (uint8_t)(30u - progress + column - progress);
            const uint8_t destination_y = row < progress ?
                (uint8_t)(8u + row) :
                (uint8_t)(20u - progress + row - progress);
            const uint8_t source_x = (uint8_t)(25u - progress + column);
            const uint8_t source_y = (uint8_t)(14u - progress + row);
            map[(size_t)destination_y * 32u + destination_x] =
                source[(size_t)source_y * 32u + source_x];
        }
    return render_record_map(asset,flow,asset->followup_palette,map,frame,0);
}

int portable_gp_results_render_menu_setup(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    uint16_t palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    const uint16_t *render_palette;
    if (asset == NULL || flow == NULL || frame == NULL ||
        frame->pixels == NULL || !flow->menu_setup_graphics_pending ||
        !flow->menu_setup_map_pending ||
        !flow->menu_setup_scroll_map_pending ||
        asset->followup_tiles == NULL || asset->followup_palette == NULL ||
        asset->followup_map == NULL || asset->scroll_map == NULL ||
        asset->followup_tile_bytes != PORTABLE_GP_RESULTS_TILE_BYTES ||
        asset->followup_palette_count != PORTABLE_GP_RESULTS_PALETTE_COUNT ||
        asset->followup_map_count != PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT ||
        asset->scroll_map_count != PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT)
        return 0;
    memcpy(map, asset->followup_map,
           PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT * sizeof *map);
    memcpy(map + 20u * 32u, asset->scroll_map,
           PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT * sizeof *map);
    render_palette = asset->followup_palette;
    if (flow->menu_highlight_pending)
    {
        const uint16_t palette_bits = (uint16_t)(
            (uint16_t)flow->menu_highlight_palette_bank << 12u);
        if (!flow->menu_palette_pending || asset->menu_palette == NULL ||
            asset->menu_palette_count != PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT ||
            flow->menu_highlight_x + flow->menu_highlight_width > 32u ||
            flow->menu_highlight_y + flow->menu_highlight_height > 32u ||
            flow->menu_highlight_palette_bank >= 16u)
            return 0;
        memcpy(palette, asset->followup_palette, sizeof palette);
        memcpy(palette +
                   (size_t)flow->menu_highlight_palette_bank * 16u,
               asset->menu_palette,
               PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT * sizeof *palette);
        render_palette = palette;
        for (uint8_t y = 0u; y < flow->menu_highlight_height; ++y)
            for (uint8_t x = 0u; x < flow->menu_highlight_width; ++x)
            {
                uint16_t *entry = &map[
                    ((size_t)flow->menu_highlight_y + y) * 32u +
                    flow->menu_highlight_x + x];
                *entry = (uint16_t)((*entry & UINT16_C(0x0fff)) |
                                    palette_bits);
            }
    }
    return render_map_data(asset->followup_tiles, asset->followup_tile_bytes,
                           render_palette, asset->followup_palette_count,
                           map, frame);
}

static void recolor_menu_row(uint16_t map[1024], uint8_t selection,
                             uint16_t palette_bits)
{
    const uint8_t y = (uint8_t)(9u + 2u * selection);
    for (uint8_t row = 0u; row < 2u; ++row)
        for (uint8_t column = 0u; column < 7u; ++column)
        {
            uint16_t *entry = &map[((size_t)y + row) * 32u + 22u + column];
            *entry = (uint16_t)((*entry & UINT16_C(0x0fff)) | palette_bits);
        }
}

static int32_t arithmetic_shift_15(int32_t value)
{
    if (value >= 0)
        return value >> 15u;
    return -(int32_t)(((uint32_t)(-value) + UINT32_C(0x7fff)) >> 15u);
}

static uint16_t blend_color(uint16_t first, uint16_t second, uint16_t factor)
{
    uint16_t result = 0u;
    for (unsigned shift = 0u; shift <= 10u; shift += 5u)
    {
        const int32_t left = (first >> shift) & 31u;
        const int32_t right = (second >> shift) & 31u;
        const int32_t channel = left + arithmetic_shift_15(
            (right - left) * (int32_t)factor);
        result = (uint16_t)(result | (uint16_t)(channel & 31) << shift);
    }
    return result;
}

int portable_gp_results_menu_colors(const PortableGpResultsAssetView *asset,
    uint32_t frame_counter,uint16_t colors[16])
{
    if(!asset||!colors||!asset->menu_palette||!asset->menu_pulse_base_palette||
       !asset->menu_pulse_factors||asset->menu_palette_count!=16u||
       asset->menu_pulse_base_palette_count!=16u||asset->menu_pulse_factor_count!=64u)return 0;
    const PortableMenuPaletteProfile profile=portable_gp_results_menu_profile(asset);
    return portable_menu_palette_colors(&profile,frame_counter,colors);
}

int portable_gp_results_render_menu_selection(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    uint16_t race_flags,
    uint32_t frame_counter,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    uint16_t palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    const uint16_t *render_palette;
    const uint8_t disabled_selection = 1u;
    if (asset == NULL || flow == NULL || frame == NULL ||
        frame->pixels == NULL || !flow->map_dirty_pending ||
        flow->menu_selection >= PORTABLE_GP_RESULTS_MENU_OPTION_COUNT ||
        flow->menu_previous_selection >= PORTABLE_GP_RESULTS_MENU_OPTION_COUNT ||
        asset->menu_palette == NULL ||
        asset->menu_palette_count != PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT ||
        asset->menu_pulse_base_palette == NULL ||
        asset->menu_pulse_factors == NULL ||
        asset->menu_pulse_base_palette_count !=
            PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT ||
        asset->menu_pulse_factor_count !=
            PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT ||
        !compose_record_map(asset, flow, map))
        return 0;
    memcpy(palette, asset->followup_palette, sizeof palette);
    for (size_t index = 0u;
         index < PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT; ++index)
        palette[14u * 16u + index] = blend_color(
            asset->menu_pulse_base_palette[index],
            asset->menu_palette[index],
            asset->menu_pulse_factors[
                frame_counter &
                (PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT - 1u)]);
    render_palette = palette;
    if ((race_flags & UINT16_C(2)) != 0u)
    {
        if (asset->menu_palette == NULL ||
            asset->menu_palette_count != PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT)
            return 0;
        memcpy(palette + 4u * 16u, asset->menu_palette,
               PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT * sizeof *palette);
        render_palette = palette;
        recolor_menu_row(map, disabled_selection, UINT16_C(0x4000));
    }
    if (!flow->menu_selection_changed)
    {
        const uint8_t y = (uint8_t)(9u + 2u * flow->menu_selection);
        if (flow->menu_selection_highlight_pending)
            recolor_menu_row(map, flow->menu_selection, UINT16_C(0xe000));
        if (flow->menu_cursor_pending)
        {
            map[(size_t)y * 32u + 21u] = map[30u * 32u + 27u];
            map[((size_t)y + 1u) * 32u + 21u] = map[31u * 32u + 27u];
        }
    }
    return render_record_map(asset,flow,render_palette,map,frame,0);
}

int portable_gp_results_render_action_window(
    const PortableGpResultsAssetView *asset,uint8_t selection,uint8_t replay_disabled,
    uint32_t frame_counter,PortableSoftwareFrame *frame)
{
    uint16_t map[1024]={0};
    uint16_t palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    if(asset==NULL||frame==NULL||frame->pixels==NULL||
       selection>=PORTABLE_GP_RESULTS_MENU_OPTION_COUNT||
       asset->followup_tiles==NULL||asset->followup_palette==NULL||
       asset->followup_map==NULL||asset->scroll_map==NULL||
       asset->followup_tile_bytes!=PORTABLE_GP_RESULTS_TILE_BYTES||
       asset->followup_palette_count!=PORTABLE_GP_RESULTS_PALETTE_COUNT||
       asset->followup_map_count!=PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT||
       asset->scroll_map_count!=PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT||
       asset->menu_palette==NULL||
       asset->menu_palette_count!=PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT||
       asset->menu_pulse_base_palette==NULL||
       asset->menu_pulse_base_palette_count!=PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT||
       asset->menu_pulse_factors==NULL||
       asset->menu_pulse_factor_count!=PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT)
        return 0;
    /* 2123C case 8 copies the DB6FC window (columns 20..29, rows 8..19). */
    for(size_t row=8u;row<20u;++row)
        memcpy(map+row*32u+20u,asset->followup_map+row*32u+20u,10u*sizeof *map);
    memcpy(palette,asset->followup_palette,sizeof palette);
    if(!portable_gp_results_menu_colors(asset,frame_counter,palette+14u*16u))return 0;
    if(replay_disabled){
        memcpy(palette+4u*16u,asset->menu_palette,
            PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT*sizeof *palette);
        recolor_menu_row(map,1u,UINT16_C(0x4000));
    }
    /* Case 10: the cursor from 9BD7C (27,30)/(27,31) and the pulsing row. */
    if(!(replay_disabled&&selection==1u))
        recolor_menu_row(map,selection,UINT16_C(0xe000));
    const size_t y=9u+2u*selection;
    map[y*32u+21u]=asset->scroll_map[10u*32u+27u];
    map[(y+1u)*32u+21u]=asset->scroll_map[11u*32u+27u];
    return render_map_data(asset->followup_tiles,asset->followup_tile_bytes,
        palette,asset->followup_palette_count,map,frame);
}

int portable_gp_results_render_terminal(
    const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,
    uint16_t race_flags,
    uint32_t frame_counter,
    PortableSoftwareFrame *frame)
{
    uint16_t map[1024] = {0};
    uint16_t palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    const uint8_t disabled = (uint8_t)(
        flow != NULL && flow->menu_selection == 1u &&
        (race_flags & UINT16_C(2)) != 0u);
    if (asset == NULL || flow == NULL || frame == NULL ||
        frame->pixels == NULL || flow->state != 13u ||
        !flow->scene_request_pending ||
        flow->menu_selection >= PORTABLE_GP_RESULTS_MENU_OPTION_COUNT ||
        asset->menu_palette == NULL ||
        asset->menu_palette_count != PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT ||
        asset->menu_pulse_base_palette == NULL ||
        asset->menu_pulse_base_palette_count !=
            PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT ||
        asset->menu_pulse_factors == NULL ||
        asset->menu_pulse_factor_count !=
            PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT ||
        !compose_record_map(asset, flow, map))
        return 0;
    memcpy(palette, asset->followup_palette, sizeof palette);
    for (size_t index = 0u;
         index < PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT; ++index)
        palette[14u * 16u + index] = blend_color(
            asset->menu_pulse_base_palette[index],
            asset->menu_palette[index],
            asset->menu_pulse_factors[
                frame_counter &
                (PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT - 1u)]);
    if ((race_flags & UINT16_C(2)) != 0u)
    {
        memcpy(palette + 4u * 16u, asset->menu_palette,
               PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT * sizeof *palette);
        recolor_menu_row(map, 1u, UINT16_C(0x4000));
    }
    if (!disabled)
        recolor_menu_row(map, flow->menu_selection, UINT16_C(0xe000));
    return render_record_map(asset,flow,palette,map,frame,0);
}

/* 801E4CC..E666 copies these five live-HUD regions after the board. Whole
 * cells (including zero cells) replace the board; mask first so transparent
 * frozen art exposes the caller's current world rather than stale board art. */
static void lap_hud_regions(uint8_t *board_mask,const PortableLapBoardHud *hud,
    unsigned progress,PortableSoftwareFrame *frame)
{
    const int regions[5][6]={{0,2,4,13,-(int)progress,2},
        {4,0,13,4,4,-(int)progress},{22,12,9,9,22+(int)progress,12+(int)progress},
        {1,16,5,3,1,16},{0,1,3,1,0,1}};
    const int margin=frame->width>240u?(int)(frame->width-240u)/2:0;
    for(unsigned i=0;i<5u;++i){
        const int *r=regions[i];
        const PortableFrameAnchor anchor=i==2u?PORTABLE_FRAME_RIGHT:PORTABLE_FRAME_LEFT;
        PortableSoftwareFrame target=portable_software_frame_native_view(frame,anchor);
        const int origin=i==2u && frame->width>240u?(int)(frame->width-240u):0;
        for(int row=0;row<r[3];++row)for(int col=0;col<r[2];++col){
            const int dx=r[4]+col,dy=r[5]+row;
            if(dx<0 || dx>=30 || dy<0 || dy>=20)continue;
            for(unsigned y=0;y<8u;++y)for(unsigned x=0;x<8u;++x){
                const unsigned sx=(unsigned)(r[0]+col)*8u+x;
                const unsigned sy=(unsigned)(r[1]+row)*8u+y;
                const unsigned ty=(unsigned)dy*8u+y,tx=(unsigned)dx*8u+x;
                if(board_mask){
                    const int bx=(int)tx+origin-margin;
                    if(bx>=0 && bx<240)board_mask[ty*240u+(unsigned)bx]=1u;
                }else if(sx<240u && sy<160u){
                    const uint32_t pixel=hud->pixels[sy*240u+sx];
                    if(pixel!=PORTABLE_LAP_BOARD_HUD_TRANSPARENT)
                        portable_software_frame_store(&target,ty*target.width+tx,pixel);
                }
            }
        }
    }
}

static int render_lap_map(const PortableGpResultsAssetView *asset,
    const uint16_t map[1024],const PortableLapBoardHud *hud,unsigned progress,
    PortableSoftwareFrame *frame)
{
    struct LapMapScratch
    {
        uint32_t pixels[240u * 160u];
        uint8_t mask[240u * 160u];
    };
    struct LapMapScratch *scratch = calloc(1u, sizeof *scratch);
    if (scratch == NULL)
        return 0;
    uint32_t *pixels = scratch->pixels;
    uint8_t *mask = scratch->mask;
    PortableSoftwareFrame board={.pixels=pixels,.width=240u,.height=160u};
    portable_software_frame_clear(&board,PORTABLE_LAP_BOARD_HUD_TRANSPARENT);
    if(!render_map(asset,map,&board))
    {
        free(scratch);
        return 0;
    }
    lap_hud_regions(mask,hud,progress,frame);
    PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    for(unsigned y=0;y<160u;++y)for(unsigned x=0;x<240u;++x){
        const unsigned i=y*240u+x;
        if(!mask[i] && pixels[i]!=PORTABLE_LAP_BOARD_HUD_TRANSPARENT)
            portable_software_frame_store(&centered,y*centered.width+x,pixels[i]);
    }
    lap_hud_regions(NULL,hud,progress,frame);
    free(scratch);
    return 1;
}

static int render_laps(const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,const uint16_t *times_centiseconds,
    uint8_t lap_count,const PortableLapBoardHud *hud,PortableSoftwareFrame *frame,int legacy_background)
{
    if(!valid_arguments(asset,flow,times_centiseconds,frame)||frame->width<240u||frame->height<160u||
       lap_count==0u||lap_count>PORTABLE_RACE_MAX_LAPS)return 0;
    if(lap_count==3u){
        if(!hud && legacy_background)return portable_gp_results_render(asset,flow,times_centiseconds,UINT8_MAX,frame);
        uint16_t map[1024]={0};
        if(flow->reveal_progress==0u || flow->reveal_progress>8u ||
           flow->row_count>PORTABLE_GP_RESULTS_MAX_ROWS || flow->header_variant>=asset->header_count ||
           !compose_scoreboard(asset,flow,times_centiseconds,UINT8_MAX,map,0))return 0;
        if(hud)return render_lap_map(asset,map,hud,flow->reveal_progress,frame);
        PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
        return render_map(asset,map,&centered);
    }
    /* sub_8022004/8022264 past three laps: DB7C8's one-row lap labels with
     * single-row digits (0x10+d, ' 0x1D, " 0x1C), the divider at x-1 and
     * the total beside the header. */
    if(asset->compact_row_patterns==NULL||asset->compact_row_pattern_bytes==NULL||
       asset->compact_row_count!=PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT||
       flow->reveal_progress==0u||flow->reveal_progress>8u||flow->row_count>lap_count+1u||
       flow->header_variant>=asset->header_count)return 0;
    uint16_t map[1024]={0};
    if(!copy_pattern(map,asset->header_patterns+(size_t)flow->header_variant*PORTABLE_GP_RESULTS_HEADER_BYTES,
           PORTABLE_GP_RESULTS_HEADER_BYTES,flow->header_x,flow->header_y))return 0;
    draw_time(map,asset->digit_tiles,times_centiseconds[lap_count],(uint8_t)(flow->header_x+4u),
        flow->header_y,UINT16_C(0xc000));
    for(uint8_t index=0u;index<flow->row_count;++index){
        const PortableGpResultsRowCommand *command=&flow->rows[index];
        const uint8_t source=command->racer_index;
        /* The ROM indexes the divider by lap count (five); mod lap counts
         * keep the divider too. */
        const uint8_t pattern=source==lap_count?PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT-1u:source;
        if(source>lap_count||
           !copy_pattern(map,asset->compact_row_patterns+(size_t)pattern*32u,
               asset->compact_row_pattern_bytes[pattern],command->x,command->y))return 0;
        if(source==lap_count||command->x+11u>=32u||command->y>=32u)continue;
        const uint16_t value=times_centiseconds[source]>UINT16_C(0xea5f)?UINT16_C(0xea5f):times_centiseconds[source];
        const unsigned seconds=value/100u,minutes=seconds/60u,cs=value%100u;
        const uint16_t cells[7]={(uint16_t)(0x10u+minutes%10u),0x1du,(uint16_t)(0x10u+seconds%60u/10u),
            (uint16_t)(0x10u+seconds%10u),0x1cu,(uint16_t)(0x10u+cs/10u),(uint16_t)(0x10u+cs%10u)};
        for(unsigned cell=0u;cell<7u;++cell)
            map[(size_t)command->y*32u+command->x+5u+cell]=(uint16_t)(UINT16_C(0xc000)|cells[cell]);
    }
    if(hud)return render_lap_map(asset,map,hud,flow->reveal_progress,frame);
    if(legacy_background){
        copy_background(asset,map,flow->reveal_progress);
        return render_map(asset,map,frame);
    }
    PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    return render_map(asset,map,&centered);
}

int portable_gp_results_render_laps_with_hud(const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,const uint16_t *times_centiseconds,
    uint8_t lap_count,const PortableLapBoardHud *hud,PortableSoftwareFrame *frame)
{ return render_laps(asset,flow,times_centiseconds,lap_count,hud,frame,0); }

int portable_gp_results_render_laps(const PortableGpResultsAssetView *asset,
    const PortableGpResultsFlow *flow,const uint16_t *times_centiseconds,
    uint8_t lap_count,PortableSoftwareFrame *frame)
{ return render_laps(asset,flow,times_centiseconds,lap_count,NULL,frame,1); }

int portable_gp_results_render_action_window_open(const PortableGpResultsAssetView *asset,
    uint8_t progress,uint8_t replay_disabled,PortableSoftwareFrame *frame)
{
    if(!asset||!frame||!frame->pixels||progress>6u||!asset->followup_tiles||
       !asset->followup_palette||!asset->followup_map||!asset->menu_palette||
       asset->followup_tile_bytes!=PORTABLE_GP_RESULTS_TILE_BYTES||
       asset->followup_palette_count!=PORTABLE_GP_RESULTS_PALETTE_COUNT||
       asset->followup_map_count!=PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT||
       asset->menu_palette_count!=PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT)return 0;
    uint16_t source[1024]={0},map[1024]={0},palette[256];
    memcpy(source,asset->followup_map,640u*sizeof *source);
    memcpy(palette,asset->followup_palette,sizeof palette);
    if(replay_disabled){
        memcpy(palette+64u,asset->menu_palette,16u*sizeof *palette);
        recolor_menu_row(source,1u,UINT16_C(0x4000));
    }
    /* 2123C case9 keeps the authored outer corners while the destination
     * grows around tile(25,14). Case8 then redraws the full sixth row-pair. */
    const unsigned half_x=progress<5u?progress:5u;
    for(unsigned y=0u;y<2u*progress;++y)for(unsigned x=0u;x<2u*half_x;++x){
        const unsigned sx=x<half_x?20u+x:30u-2u*half_x+x;
        const unsigned sy=y<progress?8u+y:20u-2u*progress+y;
        map[(14u-progress+y)*32u+25u-half_x+x]=source[sy*32u+sx];
    }
    return render_map_data(asset->followup_tiles,asset->followup_tile_bytes,
        palette,asset->followup_palette_count,map,frame);
}
