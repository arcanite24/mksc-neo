#include "hud_renderer.h"

#include <stddef.h>
#include <string.h>

static const uint16_t digit_bits[10] = {
    0x1b6, 0x124, 0x16d, 0x16f, 0x13d,
    0x16b, 0x1fb, 0x127, 0x1ff, 0x16f,
};

static uint16_t glyph_bits(char glyph)
{
    switch (glyph)
    {
    case 'A': return 0x1cf;
    case 'E': return 0x1eb;
    case 'I': return 0x1c9;
    case 'L': return 0x124;
    case 'M': return 0x1ff;
    case 'P': return 0x16d;
    case 'T': return 0x049;
    case '/': return 0x092;
    case '\'': return 0x008;
    default: return 0;
    }
}

static void draw_glyph(PortableSoftwareFrame *frame,
                       int x,
                       int y,
                       char glyph,
                       uint32_t color)
{
    const uint16_t bits = glyph_bits(glyph);

    for (unsigned row = 0; row < 5; ++row)
    {
        for (unsigned column = 0; column < 3; ++column)
        {
            if ((bits & (uint16_t)(1u << (row * 3u + column))) != 0)
                portable_software_frame_rect(frame, (size_t)(x + column),
                                             (size_t)(y + row), 1, 1, color);
        }
    }
}

static void draw_text(PortableSoftwareFrame *frame,
                      int x,
                      int y,
                      const char *text,
                      uint32_t color)
{
    for (size_t index = 0; text[index] != '\0'; ++index)
        draw_glyph(frame, x + (int)(index * 4u), y, text[index], color);
}

static void draw_digit(PortableSoftwareFrame *frame,
                       int x,
                       int y,
                       unsigned digit,
                       uint32_t color)
{
    const uint16_t bits = digit_bits[digit % 10u];

    for (unsigned row = 0; row < 5; ++row)
    {
        for (unsigned column = 0; column < 3; ++column)
        {
            if ((bits & (uint16_t)(1u << (row * 3u + column))) != 0 &&
                x + (int)column >= 0 && y + (int)row >= 0)
                portable_software_frame_rect(frame, (size_t)(x + column),
                                             (size_t)(y + row), 1, 1, color);
        }
    }
}

static void draw_number(PortableSoftwareFrame *frame,
                        int x,
                        int y,
                        unsigned value,
                        unsigned digits,
                        uint32_t color)
{
    unsigned divisor = 1;

    for (unsigned index = 1; index < digits; ++index)
        divisor *= 10u;
    for (unsigned index = 0; index < digits; ++index)
    {
        const int cursor = x + (int)(index * 4u);

        draw_digit(frame, cursor, y, (value / divisor) % 10u, color);
        divisor /= 10u;
    }
}

static uint32_t source_palette_color(const PortableHudAssetView *asset,
                                     uint16_t cell,
                                     uint8_t color)
{
    const size_t palette_bank = (size_t)((cell >> 12) & 0xfu);
    const size_t palette_index =
        palette_bank >= 10u ? (palette_bank - 10u) * 16u + color : 0;
    const uint16_t value =
        palette_index < asset->palette_count ? asset->palette[palette_index]
                                              : 0;
    const uint32_t red5 = value & 0x1fu;
    const uint32_t green5 = (value >> 5) & 0x1fu;
    const uint32_t blue5 = (value >> 10) & 0x1fu;
    const uint32_t red = (red5 << 3) | (red5 >> 2);
    const uint32_t green = (green5 << 3) | (green5 >> 2);
    const uint32_t blue = (blue5 << 3) | (blue5 >> 2);

    return (red << 16) | (green << 8) | blue;
}

static void draw_source_tile(PortableSoftwareFrame *frame,
                             const PortableHudAssetView *asset,
                             uint16_t cell,
                             size_t origin_x,
                             size_t origin_y)
{
    PortableSoftwareFrame anchored=portable_software_frame_native_view(frame,
        origin_x>=144u?PORTABLE_FRAME_RIGHT:origin_x>=104u?PORTABLE_FRAME_CENTER:PORTABLE_FRAME_LEFT);
    frame=&anchored;
    const size_t tile_index = (size_t)(cell & UINT16_C(0x03ff));
    const size_t tile_offset = tile_index * 32u;

    if (asset->tiles == NULL || tile_offset > asset->tile_bytes ||
        asset->tile_bytes - tile_offset < 32u)
        return;
    for (size_t y = 0; y < 8; ++y)
    {
        for (size_t x = 0; x < 8; ++x)
        {
            const size_t sample_x = (cell & UINT16_C(0x0400)) != 0u
                ? 7u - x : x;
            const size_t sample_y = (cell & UINT16_C(0x0800)) != 0u
                ? 7u - y : y;
            const uint8_t packed = asset->tiles[
                tile_offset + sample_y * 4u + sample_x / 2u];
            const uint8_t color = (uint8_t)(
                (packed >> ((sample_x & 1u) * 4u)) & 0xfu);

            if (color != 0)
                portable_software_frame_rect(
                    frame, origin_x + x, origin_y + y, 1, 1,
                    source_palette_color(asset, cell, color));
        }
    }
}

static void draw_source_cells(PortableSoftwareFrame *frame,
                              const PortableHudAssetView *asset,
                              const uint16_t *cells,
                              size_t cell_count,
                              size_t origin_x,
                              size_t origin_y,
                              size_t width)
{
    if (cells == NULL || width == 0)
        return;
    for (size_t index = 0; index < cell_count; ++index)
        draw_source_tile(frame, asset, cells[index],
                         origin_x + (index % width) * 8u,
                         origin_y + (index / width) * 8u);
}

static void draw_source_item_tile(PortableSoftwareFrame *frame,
                                  const PortableHudAssetView *asset,
                                  uint16_t cell,
                                  size_t origin_x,
                                  size_t origin_y,
                                  size_t state_index)
{
    PortableSoftwareFrame anchored=portable_software_frame_native_view(frame,
        origin_x>=144u?PORTABLE_FRAME_RIGHT:origin_x>=104u?PORTABLE_FRAME_CENTER:PORTABLE_FRAME_LEFT);
    frame=&anchored;
    const size_t tile_index = (size_t)(cell & UINT16_C(0x03ff));
    const uint8_t *tile;

    if (tile_index < 0x70u || tile_index > 0x73u ||
        asset->item_tiles == NULL ||
        state_index >= PORTABLE_HUD_ITEM_PAGE_COUNT ||
        asset->item_tile_bytes < (state_index + 1u) *
                                 PORTABLE_HUD_ITEM_PAGE_BYTES)
    {
        draw_source_tile(frame, asset, cell, origin_x, origin_y);
        return;
    }
    tile = asset->item_tiles + state_index * PORTABLE_HUD_ITEM_PAGE_BYTES +
           (tile_index - 0x70u) * 32u;
    for (size_t y = 0; y < 8u; ++y)
    {
        for (size_t x = 0; x < 8u; ++x)
        {
            const size_t sample_x = (cell & UINT16_C(0x0400)) != 0u
                ? 7u - x : x;
            const size_t sample_y = (cell & UINT16_C(0x0800)) != 0u
                ? 7u - y : y;
            const uint8_t packed = tile[sample_y * 4u + sample_x / 2u];
            const uint8_t color =
                (uint8_t)((packed >> ((sample_x & 1u) * 4u)) & 0xfu);
            if (color != 0u)
                portable_software_frame_rect(
                    frame, origin_x + x, origin_y + y, 1u, 1u,
                    source_palette_color(asset, cell, color));
        }
    }
}

static void draw_source_time_trial_items(PortableSoftwareFrame *frame,
                                         const PortableHudAssetView *asset,
                                         uint8_t mushrooms_remaining)
{
    const uint8_t remaining = mushrooms_remaining > 3u
        ? 3u : mushrooms_remaining;
    const size_t state_index=remaining==0u?0u:7u-remaining;

    if (asset->item_cells == NULL ||
        asset->item_cell_count < PORTABLE_HUD_ITEM_CELL_COUNT ||
        asset->item_palette_selectors==NULL || asset->item_palette_count!=17u)
        return;
    for (size_t index = 0; index < PORTABLE_HUD_ITEM_CELL_COUNT; ++index)
    {
        uint16_t cell = asset->item_cells[index];
        const size_t row = index / 4u;
        const size_t column = index % 4u;

        /* sub_804CD98 changes the four icon cells to palette E while an
           inventory page is present and to palette F for the empty page. */
        if ((row == 1u || row == 2u) &&
            (column == 1u || column == 2u))
            cell = (uint16_t)((cell & UINT16_C(0x0fff)) |
                              asset->item_palette_selectors[state_index]);
        draw_source_item_tile(frame, asset, cell,
                              13u * 8u + column * 8u, row * 8u,
                              state_index);
    }
}

int portable_hud_gp_item_graphic(const PortableHudAssetView *asset,uint16_t item,PortableHudGpItemGraphic *out)
{
    PortableGpItemHudSelection selection;
    if(out==NULL||asset==NULL||asset->item_palette_selectors==NULL||asset->item_palette_count!=17u||
       item==0u||!portable_gp_item_hud_selection(item,&selection))return 0;
    out->physical_page=selection.physical_page;
    out->palette_selector=asset->item_palette_selectors[selection.logical_page];
    return 1;
}

static void draw_source_gp_item(PortableSoftwareFrame *frame,
                                const PortableHudAssetView *asset,
                                uint16_t item)
{
    PortableHudGpItemGraphic graphic;
    if (!portable_hud_gp_item_graphic(asset,item, &graphic) ||
        asset->item_cells == NULL ||
        asset->item_cell_count < PORTABLE_HUD_ITEM_CELL_COUNT)
        return;
    for (size_t index = 0u; index < PORTABLE_HUD_ITEM_CELL_COUNT; ++index)
    {
        uint16_t cell = asset->item_cells[index];
        const size_t row = index / 4u;
        const size_t column = index % 4u;
        if ((row == 1u || row == 2u) &&
            (column == 1u || column == 2u))
            cell = (uint16_t)((cell & UINT16_C(0x0fff)) |
                              graphic.palette_selector);
        draw_source_item_tile(frame, asset, cell,
                              13u * 8u + column * 8u, row * 8u,
                              graphic.physical_page);
    }
}

static void draw_source_digit(PortableSoftwareFrame *frame,
                              const PortableHudAssetView *asset,
                              size_t tile_x,
                              size_t tile_y,
                              unsigned digit)
{
    const size_t index = (size_t)(digit % 10u);

    if (asset->digit_tiles == NULL || asset->digit_count < 20u)
        return;
    draw_source_tile(frame, asset,
                     asset->digit_tiles[index] | UINT16_C(0xc000),
                     tile_x * 8u, tile_y * 8u);
    draw_source_tile(frame, asset,
                     asset->digit_tiles[index + 10u] | UINT16_C(0xc000),
                     tile_x * 8u, (tile_y + 1u) * 8u);
}

static void draw_source_clock_units(PortableSoftwareFrame *frame,
                                    const PortableHudAssetView *asset,
                                    uint32_t clock_units)
{
    uint32_t minutes;
    uint32_t seconds;
    uint32_t hundredths;
    if (clock_units > 59999u)
        clock_units = 59999u;
    minutes = clock_units / 6000u;
    seconds = (clock_units / 100u) % 60u;
    hundredths = clock_units % 100u;
    draw_source_digit(frame, asset, 23, 0, minutes % 10u);
    draw_source_tile(frame, asset, UINT16_C(0xc05a), 24u * 8u, 0);
    draw_source_digit(frame, asset, 25, 0, seconds / 10u);
    draw_source_digit(frame, asset, 26, 0, seconds % 10u);
    draw_source_tile(frame, asset, UINT16_C(0xc057), 27u * 8u, 0);
    draw_source_digit(frame, asset, 28, 0, hundredths / 10u);
    draw_source_digit(frame, asset, 29, 0, hundredths % 10u);
}

static void draw_source_timer(PortableSoftwareFrame *frame,
                              const PortableHudAssetView *asset,
                              uint32_t elapsed)
{
    uint32_t clock_units;
    if (asset->clock_steps != NULL &&
        asset->clock_step_count >= PORTABLE_HUD_CLOCK_STEP_COUNT)
    {
        const uint32_t cycles = elapsed / 60u;
        const size_t remainder = (size_t)(elapsed % 60u);
        clock_units = cycles * 100u;
        for (size_t index = 0; index < remainder; ++index)
            clock_units += asset->clock_steps[index];
    }
    else
        clock_units = elapsed;
    draw_source_clock_units(frame, asset, clock_units);
}

static void draw_source_hud(PortableSoftwareFrame *frame,
                            const PortableHudAssetView *asset,
                            const PortableHudState *state)
{
    const unsigned lap_digit = (unsigned)state->lap + 1u;
    const unsigned lap_count_digit = (unsigned)state->lap_count;

    if (!state->show_battle) {
        draw_source_cells(frame, asset, asset->left_cells,
                          asset->left_cell_count, 4u * 8u, 0, 3);
        draw_source_digit(frame, asset, 8, 0, lap_digit);
        draw_source_cells(frame, asset, asset->slash_cells,
                          asset->slash_cell_count, 9u * 8u, 0, 1);
        draw_source_digit(frame, asset, 10, 0, lap_count_digit);
    }
    draw_source_cells(frame, asset,
                      (state->result_clock_mode == 2u ||
                       state->result_clock_mode == 3u) ? asset->left_cells
                                                       : asset->right_cells,
                      asset->right_cell_count, 18u * 8u, 0, 3);
    if(state->result_clock_mode == 2u)
        draw_source_clock_units(frame, asset,
                                state->result_lap_centiseconds);
    else if(state->result_clock_mode != 1u &&
            state->result_clock_mode != 3u)
    {
        if (state->source_clock_override)
            draw_source_clock_units(frame, asset,
                                    state->source_clock_centiseconds);
        else
            draw_source_timer(frame, asset, state->elapsed_frames);
    }
    if (state->show_time_trial_items)
        draw_source_time_trial_items(frame, asset,
                                     state->mushrooms_remaining);
    else if (state->show_gp_item && state->gp_item != 0u)
        draw_source_gp_item(frame, asset, state->gp_item);
}

static void draw_fallback_hud(PortableSoftwareFrame *frame,
                              const PortableHudState *state)
{
    if (!state->show_battle) {
        /* sub_804EA94 writes the race labels into the top BG map rows. */
        draw_text(frame, 32, 4, "LAP", UINT32_C(0x162235));
        draw_text(frame, 31, 3, "LAP", UINT32_C(0xf1fa8c));
        draw_number(frame, 48, 3, (unsigned)state->lap + 1u, 1,
                    UINT32_C(0xf1fa8c));
        draw_glyph(frame, 52, 3, '/', UINT32_C(0xf1fa8c));
        draw_number(frame, 56, 3, (unsigned)state->lap_count, 1,
                    UINT32_C(0xf1fa8c));
    }
    PortableSoftwareFrame right=portable_software_frame_native_view(frame,PORTABLE_FRAME_RIGHT);
    frame=&right;
    draw_text(frame, 144, 4, "TIME", UINT32_C(0x162235));
    draw_text(frame, 143, 3, "TIME", UINT32_C(0xf1fa8c));
    draw_number(frame, 164, 3, (unsigned)(state->elapsed_frames / 60u), 2,
                UINT32_C(0xf1fa8c));
    draw_glyph(frame, 172, 3, '\'', UINT32_C(0xf1fa8c));
}

static int render_with_asset(const PortableMinimapAssetView *minimap,
                             const PortableHudAssetView *hud_asset,
                             const PortableHudState *state,
                             PortableSoftwareFrame *frame)
{
    if (minimap == NULL || state == NULL || frame == NULL ||
        frame->pixels == NULL || minimap->cells == NULL ||
        minimap->width_tiles != PORTABLE_MINIMAP_WIDTH_TILES ||
        minimap->height_tiles != PORTABLE_MINIMAP_HEIGHT_TILES ||
        minimap->cell_count < PORTABLE_MINIMAP_CELL_COUNT)
        return PORTABLE_HUD_RENDER_INVALID_ARGUMENT;

    if(hud_asset!=NULL&&(state->show_time_trial_items||(state->show_gp_item&&state->gp_item!=0u))&&
       (hud_asset->item_palette_selectors==NULL||hud_asset->item_palette_count!=17u))
        return PORTABLE_HUD_RENDER_INVALID_ARGUMENT;
    if (hud_asset != NULL)
        draw_source_hud(frame, hud_asset, state);
    else
        draw_fallback_hud(frame, state);

    /* Kind11 was misidentified as a minimap. Its legacy bytes remain readable,
     * but its exact art is drawn separately until the source 4F500 finish clear. */
    return PORTABLE_HUD_RENDER_OK;
}

int portable_hud_render_with_asset(const PortableMinimapAssetView *minimap,
                                   const PortableHudAssetView *hud_asset,
                                   const PortableHudState *state,
                                   PortableSoftwareFrame *frame)
{
    return render_with_asset(minimap,hud_asset,state,frame);
}

int portable_hud_render_without_minimap_with_asset(
    const PortableMinimapAssetView *minimap,
    const PortableHudAssetView *hud_asset,
    const PortableHudState *state,
    PortableSoftwareFrame *frame)
{
    return render_with_asset(minimap,hud_asset,state,frame);
}

int portable_hud_render(const PortableMinimapAssetView *minimap,
                        const PortableHudState *state,
                        PortableSoftwareFrame *frame)
{
    return portable_hud_render_with_asset(minimap, NULL, state, frame);
}

static void draw_gp_bg0_tile(PortableSoftwareFrame *frame,
    const PortableHudAssetView *hud, uint16_t cell,
    int origin_x, int origin_y, uint32_t result_tick, const uint8_t *tile)
{
    PortableSoftwareFrame anchored=portable_software_frame_native_view(frame,
        origin_x>=144?PORTABLE_FRAME_RIGHT:origin_x>=104?PORTABLE_FRAME_CENTER:PORTABLE_FRAME_LEFT);
    frame=&anchored;
    if(tile==NULL)return;
    for(int y=0;y<8;++y){
        const unsigned sy=(cell&UINT16_C(0x0800))?7u-(unsigned)y:(unsigned)y;
        for(int x=0;x<8;++x){
            const unsigned sx=(cell&UINT16_C(0x0400))?7u-(unsigned)x:(unsigned)x;
            const uint8_t packed=tile[sy*4u+sx/2u];
            const uint8_t color=(uint8_t)((packed>>((sx&1u)*4u))&15u);
            const int dx=origin_x+x,dy=origin_y+y;
            if(color!=0u&&dx>=0&&dy>=0&&
               dx<(int)frame->width&&dy<(int)frame->height){
                uint32_t rgb=source_palette_color(hud,cell,color);
                /* Source BG palette entries 185/186 pulse every four
                 * result ticks; retained +1..+32 palette captures pair
                 * the 0x82bf/0x01ff and 0xffff/0xe739 phases. */
                if(((result_tick/4u)&1u)!=0u&&
                   ((cell>>12u)&15u)==11u){
                    if(color==9u)rgb=UINT32_C(0x00ffffff);
                    if(color==10u)rgb=UINT32_C(0x00cecece);
                }
                portable_software_frame_store(frame,(size_t)dy*frame->width+(size_t)dx, rgb);
            }
        }
    }
}

static void draw_gp_bg0_cell(PortableSoftwareFrame *frame,
                             const PortableGpRaceHudAssetView *gp,
                             const PortableHudAssetView *hud,
                             uint16_t cell,int origin_x,int origin_y,
                             uint32_t result_tick,
                             const uint8_t *top_tiles,
                             const uint8_t *badge_tiles)
{
    const unsigned index=cell&UINT16_C(0x03ff);
    const uint8_t *tile=NULL;
    if(index>=0x70u&&index<0x74u&&top_tiles!=NULL)
        tile=top_tiles+(index-0x70u)*32u;
    else if(index>=0xe0u&&index<0xebu&&badge_tiles!=NULL)
        tile=badge_tiles+(index-0xe0u)*32u;
    else if(index<0x80u&&hud->tiles!=NULL&&
       hud->tile_bytes>=(size_t)(index+1u)*32u)
        tile=hud->tiles+(size_t)index*32u;
    else if(index>=0x80u&&index<0xa0u)
        tile=gp->portrait_tiles+(size_t)(index-0x80u)*32u;
    else if(index>=0x100u&&index<0x140u)
        tile=gp->course_tiles+(size_t)(index-0x100u)*32u;
    if(tile==NULL)return;
    draw_gp_bg0_tile(frame,hud,cell,origin_x,origin_y,result_tick,tile);
}

static uint16_t finish_cell(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8u)); }

int portable_hud_render_course_map(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,PortableSoftwareFrame *frame)
{
    if(!gp||!hud||!frame||!frame->pixels||!gp->course_tiles||!hud->palette||
       hud->palette_count<PORTABLE_HUD_PALETTE_COUNT)return 0;
    for(unsigned y=0u;y<8u;++y)for(unsigned x=0u;x<8u;++x)
        draw_gp_bg0_cell(frame,gp,hud,(uint16_t)(0xb100u+y*8u+x),
            176+(int)x*8,96+(int)y*8,0u,NULL,NULL);
    return 1;
}

static int64_t speedometer_floor(int64_t value,int64_t divisor)
{ return value>=0?value/divisor:-((-value+divisor-1)/divisor); }

int portable_hud_render_speedometer(const PortableNativeImage *dial,
    const PortableNativeImage *needle,int32_t speed_q16,const int32_t *sine,
    size_t sine_count,PortableSoftwareFrame *frame)
{
    if(!dial||!needle||!sine||sine_count<1024u||!frame||!frame->pixels)return 0;
    PortableSoftwareFrame left=portable_software_frame_native_view(frame,PORTABLE_FRAME_LEFT);
    const size_t scale=portable_software_frame_scale(&left);
    const uint32_t magnitude=speed_q16<0?(uint32_t)(-(int64_t)speed_q16):(uint32_t)speed_q16;
    /* F494's LSL32 narrows before its unsigned shift. */
    const uint32_t speed_angle=(magnitude<<5u)>>16u;
    const unsigned angle=speed_angle<=0xc000u?speed_angle+0x2000u:0xe000u;
    const unsigned index=(angle>>6u)&1023u;
    /* Source oam_renderCellData narrows the inverse matrix to signed Q8,
     * with division toward zero, before sampling the authored cell. */
    const int32_t sin=sine[index]/128,cos=sine[(index+256u)&1023u]/128;
    const size_t dial_right=(left.width<48u?left.width:48u)*scale;
    const size_t dial_bottom=(left.height<152u?left.height:152u)*scale;
    for(size_t y=112u*scale;y<dial_bottom;++y)for(size_t x=0u;x<dial_right;++x){
        const int dx=(int)(x/scale)-dial->anchor_x;
        const int dy=(int)(y/scale)-112-dial->anchor_y;
        if(dx>=0&&dy>=0&&dx<dial->width&&dy<dial->height){
            uint32_t pixel=portable_native_image_pixel(dial,(unsigned)dx,(unsigned)dy);
            if(pixel>>24u==255u)portable_software_frame_store_physical(&left,x,y,pixel);
        }
    }
    /* EE764 is one normal-size 64x64 cell. F494 requests scale256, so
     * oam_renderCell never enables its enlarged affine canvas. Rotation
     * samples only this fixed output rectangle, even for opaque corners. */
    const size_t needle_right=(left.width<56u?left.width:56u)*scale;
    const size_t needle_bottom=(left.height<168u?left.height:168u)*scale;
    for(size_t y=104u*scale;y<needle_bottom;++y)for(size_t x=0u;x<needle_right;++x){
        const int64_t rx=(int64_t)x-24*(int64_t)scale,ry=(int64_t)y-136*(int64_t)scale;
        const int64_t sx=speedometer_floor(rx*cos+ry*sin,256*(int64_t)scale)-needle->anchor_x;
        const int64_t sy=speedometer_floor(-rx*sin+ry*cos,256*(int64_t)scale)-needle->anchor_y;
        if(sx<0||sy<0||sx>=needle->width||sy>=needle->height)continue;
        const uint32_t pixel=portable_native_image_pixel(needle,(size_t)sx,(size_t)sy);
        if(pixel>>24u==255u)portable_software_frame_store_physical(&left,x,y,pixel);
    }
    return 1;
}

int portable_hud_render_entry_bands(const PortableSoftwareFrame *hud,uint8_t progress,
    uint32_t transparent,PortableSoftwareFrame *frame)
{
    if(!hud||!frame||!hud->pixels||!frame->pixels||progress>10u||
       hud->width!=frame->width||hud->height!=frame->height||hud->width<240u||hud->height<160u||
       portable_software_frame_scale(hud)!=portable_software_frame_scale(frame))return 0;
    const size_t scale=portable_software_frame_scale(hud),stride=portable_software_frame_stride(hud);
    const int displacement=(10-(int)progress)*(int)(8u*scale);
    const size_t width=hud->width*scale;
    for(unsigned band=0u;band<3u;++band){
        const size_t left=band==0u?32u*scale:band==1u?0u:width-64u*scale;
        const size_t right=band==0u?width:band==1u?48u*scale:width;
        const size_t top=band==0u?0u:band==1u?16u*scale:96u*scale;
        const size_t bottom=band==0u?32u*scale:band==1u?152u*scale:160u*scale;
        for(size_t y=top;y<bottom;++y)for(size_t x=left;x<right;++x){
            const uint32_t pixel=hud->pixels[y*stride+x];if(pixel==transparent)continue;
            const int dx=(int)x+(band==1u?-displacement:band==2u?displacement:0);
            const int dy=(int)y+(band==0u?-displacement:0);
            if(dx>=0&&dy>=0&&(size_t)dx<width&&(size_t)dy<hud->height*scale)
                portable_software_frame_store_physical(frame,(size_t)dx,(size_t)dy,pixel);
        }
    }
    return 1;
}

static int render_gp_finish_rivals(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,const PortableGpFinishPortraits *state,
    const PortableNativeImage *const own_portraits[8],PortableSoftwareFrame *frame)
{
    if(!gp||!hud||!state||!frame||!frame->pixels||!state->count||state->count>8u||
       state->selected>=state->count||!hud->palette||hud->palette_count<PORTABLE_HUD_PALETTE_COUNT)return 0;
    if(!gp->finish_frames||!gp->finish_portrait_patterns)return 1; /* older packs */
    uint16_t map[4u*32u]={0};
    for(unsigned rank=0u;rank<state->count;++rank){
        const PortableGpFinishPortrait *row=&state->rows[rank];
        if(row->driver_class<0)continue;
        if(row->driver_class>7||row->racer_index>=state->count)return 0;
        if(row->racer_index==state->selected)continue;
        const uint8_t *border=gp->finish_frames+(rank==0u||rank==4u?0u:3u)*36u+4u;
        const uint8_t *portrait=gp->finish_portrait_patterns+(unsigned)row->driver_class*12u+4u;
        for(unsigned y=0u;y<4u;++y)for(unsigned x=0u;x<4u;++x)
            if(row->x_tile+x<32u)map[y*32u+row->x_tile+x]=finish_cell(border+(y*4u+x)*2u);
        const int custom=own_portraits&&own_portraits[row->racer_index];
        for(unsigned y=0u;y<2u;++y)for(unsigned x=0u;x<2u;++x)
            if(row->x_tile+1u+x<32u)map[(y+1u)*32u+row->x_tile+1u+x]=
                custom?0u:finish_cell(portrait+(y*2u+x)*2u);
    }
    PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    for(unsigned y=0u;y<4u;++y)for(unsigned x=0u;x<30u;++x)
        if(map[y*32u+x])draw_gp_bg0_cell(&centered,gp,hud,map[y*32u+x],(int)x*8,80+(int)y*8,0u,NULL,NULL);
    if(own_portraits)for(unsigned rank=0u;rank<state->count;++rank){
        const PortableGpFinishPortrait *row=&state->rows[rank];
        if(row->driver_class<0||row->racer_index==state->selected)continue;
        const PortableNativeImage *image=own_portraits[row->racer_index];
        if(!image)continue;
        for(unsigned y=0u;y<16u&&y<image->height;++y)for(unsigned x=0u;x<16u&&x<image->width;++x){
            const unsigned tx=(row->x_tile+1u)*8u+x,ty=88u+y;
            const uint32_t pixel=portable_native_image_pixel(image,x,y);
            if(tx<240u&&(pixel>>24u)==255u)
                portable_software_frame_store(&centered,ty*centered.width+tx,pixel);
        }
    }
    return 1;
}

int portable_hud_render_quick_finish_portraits(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,const PortableGpFinishPortraits *state,
    const PortableNativeImage *const own_portraits[8],PortableSoftwareFrame *frame)
{return render_gp_finish_rivals(gp,hud,state,own_portraits,frame);}

int portable_hud_render_gp_finish_portraits(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,const PortableGpFinishPortraits *state,
    const PortableNativeImage *const own_portraits[8],uint32_t clock,PortableSoftwareFrame *frame)
{
    if(!render_gp_finish_rivals(gp,hud,state,own_portraits,frame))return 0;
    if(!gp->finish_frames||!gp->finish_portrait_patterns)return 1;
    PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    for(unsigned rank=0u;rank<state->count;++rank){
        const PortableGpFinishPortrait *row=&state->rows[rank];
        if(row->racer_index!=state->selected || row->driver_class<0)continue;
        /* D568 draws the selected border last. Settled shared borders have
         * left/middle/right variants; moving portraits use the whole frame. */
        const unsigned target=rank*3u+(rank>3u?5u:0u);
        const PortableGpFinishPortrait *next=rank+1u<state->count?&state->rows[rank+1u]:NULL;
        const unsigned next_target=(rank+1u)*3u+(rank+1u>3u?5u:0u);
        const int adjacent=next && next->driver_class>=0 && next->x_tile==next_target;
        const unsigned shape=row->x_tile!=target?0u:!adjacent?
            (rank==0u||rank==4u?0u:3u):
            (rank==0u||rank==4u?1u:rank==3u||rank==7u?3u:2u);
        const unsigned variant=shape+(clock%12u>5u?4u:0u);
        const uint8_t *border=gp->finish_frames+variant*36u+4u;
        const uint8_t *portrait=gp->finish_portrait_patterns+(unsigned)row->driver_class*12u+4u;
        const PortableNativeImage *custom=own_portraits?own_portraits[row->racer_index]:NULL;
        for(unsigned y=0u;y<4u;++y)for(unsigned x=0u;x<4u;++x){
            const unsigned tx=row->x_tile+x;
            if(tx<30u)draw_gp_bg0_cell(&centered,gp,hud,finish_cell(border+(y*4u+x)*2u),
                (int)tx*8,80+(int)y*8,0u,NULL,NULL);
        }
        for(unsigned y=0u;y<2u;++y)for(unsigned x=0u;x<2u;++x){
            const unsigned tx=row->x_tile+1u+x;
            if(tx<30u&&!custom)draw_gp_bg0_cell(&centered,gp,hud,finish_cell(portrait+(y*2u+x)*2u),
                (int)tx*8,88+(int)y*8,0u,NULL,NULL);
        }
        if(custom)for(unsigned y=0u;y<16u&&y<custom->height;++y)
            for(unsigned x=0u;x<16u&&x<custom->width;++x){
                const unsigned tx=(row->x_tile+1u)*8u+x;
                const uint32_t pixel=portable_native_image_pixel(custom,x,y);
                if(tx<240u&&(pixel>>24u)==255u)
                    portable_software_frame_store(&centered,(88u+y)*centered.width+tx,pixel);
            }
    }
    return 1;
}

int portable_hud_render_gp_turn_cue(const PortableHudAssetView *hud,
    const uint16_t cells[12],const uint8_t tiles[384],
    const uint16_t rank_colors[4],PortableSoftwareFrame *frame)
{
    if(hud==NULL||cells==NULL||tiles==NULL||rank_colors==NULL||
       frame==NULL||frame->pixels==NULL||hud->palette==NULL||
       hud->palette_count<PORTABLE_HUD_PALETTE_COUNT)return 0;
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT];
    memcpy(palette,hud->palette,sizeof palette);
    for(unsigned i=0u;i<4u;++i)palette[2u*16u+9u+i]=rank_colors[i];
    PortableHudAssetView adjusted=*hud;
    adjusted.palette=palette;
    for(unsigned i=0u;i<12u;++i){
        const uint16_t cell=cells[i];
        const unsigned tile=cell&1023u;
        /* Empty maps use C000, the transparent source tile zero. */
        if(cell==0u||tile==0u)continue;
        if(tile<0x64u||tile>=0x70u||(cell>>12u)!=12u)return 0;
        draw_gp_bg0_tile(frame,&adjusted,cell,
            (int)(13u+i%4u)*8,(int)(5u+i/4u)*8,0u,
            tiles+(tile-0x64u)*32u);
    }
    return 1;
}

static int render_gp_course_map_entrance(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    uint32_t intro_frame,
    PortableSoftwareFrame *frame,uint32_t last_frame)
{
    if(gp==NULL||hud==NULL||frame==NULL||frame->pixels==NULL||
       gp->course_tiles==NULL||hud->palette==NULL||
       hud->palette_count<PORTABLE_HUD_PALETTE_COUNT||
       intro_frame<1339u||intro_frame>last_frame)return 0;
    const unsigned columns=intro_frame>=1346u?8u:intro_frame-1338u;
    /* Source map_getBufferPtr2d writes row 12..19. The first column
     * appears at x=29 and each following turn shifts it one tile left. */
    for(unsigned row=0u;row<8u;++row)
        for(unsigned col=0u;col<columns;++col)
            draw_gp_bg0_cell(frame,gp,hud,
                (uint16_t)(UINT16_C(0xb100)+row*8u+col),
                (int)(30u-columns+col)*8,(int)(12u+row)*8,0u,NULL,NULL);
    return 1;
}

static uint16_t gp_entrance_label_cell(const PortableHudAssetView *hud,
    unsigned group,unsigned column,unsigned lower)
{
    if(group==0u)return hud->left_cells[lower*3u+column];
    if(group==2u)return hud->right_cells[lower*3u+column];
    if(column==1u)return hud->slash_cells[lower];
    const unsigned digit=column==0u?1u:3u;
    return (uint16_t)(UINT16_C(0xc000)|hud->digit_tiles[digit+lower*10u]);
}

static int render_gp_top_entrance(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    const uint8_t *top_tiles,
    uint32_t intro_frame,
    PortableSoftwareFrame *frame,uint32_t last_frame)
{
    static const int label_x[3]={4,8,18};
    if(top_tiles==NULL || hud==NULL || hud->item_cells==NULL || hud->item_cell_count<16u ||
       hud->left_cells==NULL || hud->left_cell_count<6u ||
       hud->right_cells==NULL || hud->right_cell_count<6u ||
       hud->slash_cells==NULL || hud->slash_cell_count<2u ||
       hud->digit_tiles==NULL || hud->digit_count<20u ||
       intro_frame<1343u||intro_frame>last_frame||
       !render_gp_course_map_entrance(
           gp,hud,intro_frame,frame,last_frame))return 0;
    const unsigned rows=intro_frame>=1346u?4u:intro_frame-1342u;
    /* The source item box advances one BG0 row on each handoff turn;
     * the prior edge and middle patterns remain on the next rows. */
    for(unsigned row=0u;row<rows;++row){
        const unsigned pattern=rows-1u-row;
        for(unsigned col=0u;col<4u;++col)
            draw_gp_bg0_cell(frame,gp,hud,hud->item_cells[(3u-pattern)*4u+col],
                             (int)(13u+col)*8,(int)row*8,0u,top_tiles,NULL);
    }
    if(intro_frame>=1345u)
        for(unsigned group=0u;group<3u;++group)
            for(unsigned col=0u;col<3u;++col)
                draw_gp_bg0_cell(frame,gp,hud,gp_entrance_label_cell(hud,group,col,1u),
                                 (label_x[group]+(int)col)*8,
                                 intro_frame>=1346u?8:0,0u,
                                 top_tiles,NULL);
    if(intro_frame>=1346u)
        for(unsigned group=0u;group<3u;++group)
            for(unsigned col=0u;col<3u;++col)
                draw_gp_bg0_cell(frame,gp,hud,gp_entrance_label_cell(hud,group,col,0u),
                                 (label_x[group]+(int)col)*8,0,0u,
                                 top_tiles,NULL);
    return 1;
}

int portable_hud_render_gp_course_map_entrance(
    const PortableGpRaceHudAssetView *gp,const PortableHudAssetView *hud,
    uint32_t intro_frame,PortableSoftwareFrame *frame)
{
    return render_gp_course_map_entrance(gp,hud,intro_frame,frame,2629u);
}

int portable_hud_render_gp_top_entrance(
    const PortableGpRaceHudAssetView *gp,const PortableHudAssetView *hud,
    const uint8_t *top_tiles,uint32_t intro_frame,PortableSoftwareFrame *frame)
{
    return render_gp_top_entrance(gp,hud,top_tiles,intro_frame,frame,2629u);
}

static int render_gp_race_bg0(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    uint8_t player_rank,
    const uint8_t top_four_classes[4],
    uint32_t result_tick,
    PortableSoftwareFrame *frame,
    const uint16_t *rank_cells, const uint8_t *separator_tile)
{
    if(gp==NULL||hud==NULL||frame==NULL||frame->pixels==NULL||
       top_four_classes==NULL||gp->kind_pages==NULL||
       gp->portrait_tiles==NULL||gp->course_tiles==NULL||
       gp->rank_maps==NULL||hud->palette==NULL||
       hud->palette_count<PORTABLE_HUD_PALETTE_COUNT)
        return 0;
    const size_t variant=player_rank<4u?player_rank:4u;
    const uint8_t *map=gp->rank_maps+variant*
        PORTABLE_GP_RACE_HUD_RANK_MAP_CELLS*2u;
    for(size_t y=0;y<13u;++y)
        for(size_t x=0;x<4u;++x){
            const size_t i=(y*4u+x)*2u;
            const uint16_t cell=rank_cells!=NULL?rank_cells[y*4u+x]:
                (uint16_t)map[i]|(uint16_t)((uint16_t)map[i+1u]<<8u);
            if((cell&UINT16_C(0x03ff))==0xffu&&separator_tile!=NULL)
                draw_gp_bg0_tile(frame,hud,cell,(int)x*8,(int)(y+2u)*8,
                                 result_tick,separator_tile);
            else if(cell!=0u)
                draw_gp_bg0_cell(frame,gp,hud,cell,
                                 (int)x*8,(int)(y+2u)*8,result_tick,NULL,NULL);
        }
    /* sub_804DBF4 fills the 2x2 interior icon cells for each of the
     * first four standings entries. The static rank map leaves them zero. */
    for(size_t rank=0u;rank_cells==NULL&&rank<4u;++rank){
        const uint8_t kind=top_four_classes[rank];
        if(kind==PORTABLE_HUD_PORTRAIT_OVERRIDDEN)continue;
        if(kind>=8u)return 0;
        const uint16_t first=(uint16_t)(UINT16_C(0xd080)+
            (uint16_t)gp->kind_pages[kind]*4u);
        for(size_t y=0u;y<2u;++y)
            for(size_t x=0u;x<2u;++x)
                draw_gp_bg0_cell(frame,gp,hud,
                    (uint16_t)(first+y*2u+x),
                    (int)(x+1u)*8,(int)(3u+rank*3u+y)*8,
                    result_tick,NULL,NULL);
    }
    for(size_t y=0;y<8u;++y)
        for(size_t x=0;x<8u;++x)
            draw_gp_bg0_cell(frame,gp,hud,
                (uint16_t)(UINT16_C(0xb100)+y*8u+x),
                (int)(x+22u)*8,(int)(y+12u)*8,result_tick,NULL,NULL);
    return 1;
}

int portable_hud_render_gp_race_bg0(
    const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud,
    uint8_t player_rank, const uint8_t top_four_classes[4],
    uint32_t result_tick, PortableSoftwareFrame *frame)
{
    return render_gp_race_bg0(gp,hud,player_rank,top_four_classes,
                             result_tick,frame,NULL,NULL);
}

int portable_hud_render_gp_race_owned(const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,uint8_t rank,const uint8_t classes[4],
    uint32_t ticks,const uint16_t rank_cells[52],const uint16_t rank_colors[4],
    const uint16_t coin_cells[3],const uint16_t coin_colors[3],
    const uint16_t separator_colors[2],PortableSoftwareFrame *frame)
{
    if(!hud||!hud->palette||hud->palette_count<PORTABLE_HUD_PALETTE_COUNT||
       !rank_cells||!rank_colors||!coin_cells||!coin_colors||!separator_colors)return 0;
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT];memcpy(palette,hud->palette,sizeof palette);
    for(unsigned i=0u;i<4u;++i)palette[2u*16u+9u+i]=rank_colors[i];
    for(unsigned i=0u;i<3u;++i)palette[17u+i]=coin_colors[i];
    for(unsigned i=0u;i<2u;++i)palette[25u+i]=separator_colors[i];
    PortableHudAssetView adjusted=*hud;adjusted.palette=palette;
    /* The legacy sampler's tick flash is a fallback. Owned palettes already
     * contain DF08/E144's accepted colors, so sampling must not flash again. */
    (void)ticks;
    if(!render_gp_race_bg0(gp,&adjusted,rank,classes,0u,frame,rank_cells,
            gp->rank_separator_tile))return 0;
    for(unsigned i=0u;i<3u;++i)draw_source_tile(frame,&adjusted,coin_cells[i],i*8u,8u);
    return 1;
}

int portable_hud_render_gp_coins(const PortableHudAssetView *hud,uint8_t coins,
    PortableSoftwareFrame *frame)
{
    if(hud==NULL || hud->palette==NULL || hud->palette_count<PORTABLE_HUD_PALETTE_COUNT ||
       frame==NULL || frame->pixels==NULL || coins>99u)return 0;
    draw_source_tile(frame,hud,UINT16_C(0xc020),0u,8u);
    draw_source_tile(frame,hud,(uint16_t)(0xb010u+coins/10u),8u,8u);
    draw_source_tile(frame,hud,(uint16_t)(0xb010u+coins%10u),16u,8u);
    return 1;
}

int portable_hud_render_gp_race(
    const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud,
    uint8_t player_rank, const uint8_t top_four_classes[4], uint8_t coins,
    uint32_t ticks, PortableSoftwareFrame *frame)
{
    if (gp == NULL || hud == NULL || gp->rank_colors == NULL ||
        hud->palette == NULL || hud->palette_count < PORTABLE_HUD_PALETTE_COUNT ||
        player_rank >= 8u || coins > 99u) return 0;
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT];
    memcpy(palette, hud->palette, sizeof palette);
    for (unsigned i = 0u; i < 4u; ++i) {
        const unsigned palette_rank = player_rank == 6u ? 5u : player_rank;
        const uint8_t *color = gp->rank_colors + palette_rank * 8u + i * 2u;
        palette[2u * 16u + 9u + i] = (uint16_t)color[0] | (uint16_t)color[1] << 8u;
    }
    PortableHudAssetView adjusted = *hud;
    adjusted.palette = palette;
    if (!render_gp_race_bg0(gp, &adjusted, player_rank, top_four_classes,
                            ticks, frame, NULL, NULL)) return 0;
    return portable_hud_render_gp_coins(&adjusted,coins,frame);
}

static int render_gp_live_entrance(
    const PortableGpRaceHudAssetView *gp,
    const PortableHudAssetView *hud,
    const uint8_t *top_tiles,
    const uint8_t *badge_tiles,
    uint8_t player_rank,
    const uint8_t top_four_classes[4],
    uint32_t intro_frame,
    uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame,
    const uint16_t *rank_cells, const uint16_t *rank_colors,
    const uint8_t *separator_tile,
    const uint16_t *coin_cells,const uint16_t *coin_colors,
    uint32_t last_frame)
{
    static const uint16_t rank_header[3]={
        UINT16_C(0xc020),UINT16_C(0xb010),UINT16_C(0xb012)};
    uint16_t time_top[7]={
        UINT16_C(0xc051),UINT16_C(0xc05a),UINT16_C(0xc051),
        UINT16_C(0xc051),UINT16_C(0xc057),UINT16_C(0xc051),
        UINT16_C(0xc051)};
    uint16_t time_lower[7]={
        UINT16_C(0xc05c),0u,UINT16_C(0xc05c),UINT16_C(0xc05c),
        0u,UINT16_C(0xc05c),UINT16_C(0xc05c)};
    uint16_t palette[PORTABLE_HUD_PALETTE_COUNT];
    PortableHudAssetView adjusted;
    if(gp==NULL||hud==NULL||frame==NULL||top_tiles==NULL||
       badge_tiles==NULL||
       top_four_classes==NULL||gp->rank_colors==NULL||
       hud->palette==NULL||
       hud->palette_count<PORTABLE_HUD_PALETTE_COUNT||
       hud->digit_tiles==NULL||hud->digit_count<20u||
       intro_frame<1349u||intro_frame>last_frame)return 0;
    const uint32_t count=displayed_centiseconds>59999u?59999u:
                                                        displayed_centiseconds;
    const unsigned positions[5]={0u,2u,3u,5u,6u};
    const unsigned digits[5]={count/6000u,(count/1000u)%6u,
        (count/100u)%10u,(count/10u)%10u,count%10u};
    for(unsigned index=0u;index<5u;++index){
        time_top[positions[index]]=UINT16_C(0xc000)|hud->digit_tiles[digits[index]];
        time_lower[positions[index]]=UINT16_C(0xc000)|hud->digit_tiles[digits[index]+10u];
    }
    for(size_t index=0u;index<PORTABLE_HUD_PALETTE_COUNT;++index)
        palette[index]=hud->palette[index];
    if(coin_colors!=NULL)
        for(unsigned index=0u;index<3u;++index)
            palette[17u+index]=coin_colors[index];
    /* The original selects rank-one colors for BG bank C at +1349.
     * The four 16-bit entries are the ROM-derived rank-color table. */
    for(size_t index=0u;index<4u;++index){
        const uint8_t *entry=gp->rank_colors+8u+index*2u;
        palette[2u*16u+9u+index]=rank_colors!=NULL?rank_colors[index]:
            intro_frame>=1380u&&index<2u?
            (index==0u?UINT16_C(0xffff):UINT16_C(0xe318)):
            (((intro_frame-1349u)/4u&1u)!=0u && gp->rank_flash_colors!=NULL?
             (uint16_t)gp->rank_flash_colors[8u+index*2u]|
             (uint16_t)((uint16_t)gp->rank_flash_colors[9u+index*2u]<<8u):
             (uint16_t)entry[0]|(uint16_t)((uint16_t)entry[1]<<8u));
    }
    adjusted=*hud;
    adjusted.palette=palette;
    if(!render_gp_top_entrance(
           gp,&adjusted,top_tiles,intro_frame,frame,last_frame)||
       !render_gp_race_bg0(
           gp,&adjusted,player_rank,top_four_classes,
           intro_frame-1345u,frame,rank_cells,separator_tile))return 0;
    for(unsigned col=0u;col<3u;++col)
        draw_gp_bg0_cell(frame,gp,&adjusted,
                         coin_cells!=NULL?coin_cells[col]:rank_header[col],
                         (int)col*8,8,(uint32_t)(intro_frame-1345u),
                         top_tiles,badge_tiles);
    for(unsigned col=0u;col<7u;++col){
        draw_gp_bg0_cell(frame,gp,&adjusted,time_top[col],
                         (int)(23u+col)*8,0,
                         (uint32_t)(intro_frame-1345u),top_tiles,
                         badge_tiles);
        if(time_lower[col]!=0u)
            draw_gp_bg0_cell(frame,gp,&adjusted,time_lower[col],
                             (int)(23u+col)*8,8,
                             (uint32_t)(intro_frame-1345u),top_tiles,
                             badge_tiles);
    }
    for(unsigned row=0u;row<3u;++row)
        for(unsigned col=0u;col<(row==2u?(rank_cells!=NULL?5u:4u):3u);++col)
            draw_gp_bg0_cell(frame,gp,&adjusted,
                (uint16_t)(UINT16_C(0xc0e0)+row*3u+col),
                (int)(1u+col)*8,(int)(16u+row)*8,
                intro_frame-1345u,top_tiles,badge_tiles);
    return 1;
}

int portable_hud_render_gp_live_entrance(
    const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud,
    const uint8_t *top_tiles, const uint8_t *badge_tiles,
    uint8_t player_rank, const uint8_t top_four_classes[4],
    uint32_t intro_frame, uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame)
{
    return render_gp_live_entrance(gp,hud,top_tiles,badge_tiles,player_rank,
        top_four_classes,intro_frame,displayed_centiseconds,frame,
        NULL,NULL,NULL,NULL,NULL,2629u);
}

int portable_hud_render_gp_live_latched(
    const PortableGpRaceHudAssetView *gp, const PortableHudAssetView *hud,
    const uint8_t *top_tiles, const uint8_t badge_tiles[352],
    const uint16_t rank_cells[52], const uint16_t rank_colors[4],
    const uint8_t separator_tile[32],
    uint32_t intro_frame, uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame)
{
    static const uint8_t unused_classes[4]={0};
    if(rank_cells==NULL||rank_colors==NULL||separator_tile==NULL)return 0;
    return render_gp_live_entrance(gp,hud,top_tiles,badge_tiles,0u,
        unused_classes,intro_frame,displayed_centiseconds,frame,
        rank_cells,rank_colors,separator_tile,NULL,NULL,2629u);
}

int portable_hud_render_gp_live_coin_latched(
    const PortableGpRaceHudAssetView *gp,const PortableHudAssetView *hud,
    const uint8_t *top_tiles,const uint8_t badge_tiles[352],
    const uint16_t rank_cells[52],const uint16_t rank_colors[4],
    const uint8_t separator_tile[32],
    const uint16_t coin_cells[3],const uint16_t coin_colors[3],
    uint32_t intro_frame,uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame)
{
    static const uint8_t unused_classes[4]={0};
    if(rank_cells==NULL||rank_colors==NULL||separator_tile==NULL||
       coin_cells==NULL||coin_colors==NULL)return 0;
    return render_gp_live_entrance(gp,hud,top_tiles,badge_tiles,0u,
        unused_classes,intro_frame,displayed_centiseconds,frame,
        rank_cells,rank_colors,separator_tile,coin_cells,coin_colors,2629u);
}

int portable_hud_render_gp_live_coin_hit_continuation(
    const PortableGpRaceHudAssetView *gp,const PortableHudAssetView *hud,
    const uint8_t *top_tiles,const uint8_t badge_tiles[352],
    const uint16_t rank_cells[52],const uint16_t rank_colors[4],
    const uint8_t separator_tile[32],
    const uint16_t coin_cells[3],const uint16_t coin_colors[3],
    uint32_t intro_frame,uint32_t displayed_centiseconds,
    PortableSoftwareFrame *frame)
{
    static const uint8_t unused_classes[4]={0};
    if(rank_cells==NULL||rank_colors==NULL||separator_tile==NULL||
       coin_cells==NULL||coin_colors==NULL)return 0;
    return render_gp_live_entrance(gp,hud,top_tiles,badge_tiles,0u,
        unused_classes,intro_frame,displayed_centiseconds,frame,
        rank_cells,rank_colors,separator_tile,coin_cells,coin_colors,2749u);
}
