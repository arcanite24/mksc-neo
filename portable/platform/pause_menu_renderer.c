#include "pause_menu_renderer.h"
#include "pause_menu.h"
#include "graphics_options.h"

/* 2CEA4 subtracts one from each nonzero RGB555 channel on every expansion
 * turn. Apply the accumulated palette result to the freshly rendered world. */
static void dim_pause_backdrop(PortableSoftwareFrame *frame,unsigned steps)
{
    if(steps==0u)return;
    const size_t scale=portable_software_frame_scale(frame);
    const size_t stride=portable_software_frame_stride(frame);
    for(size_t y=0u;y<frame->height*scale;++y)for(size_t x=0u;x<frame->width*scale;++x){
        uint32_t pixel=frame->pixels[y*stride+x],result=pixel&UINT32_C(0xff000000);
        for(unsigned shift=0u;shift<=16u;shift+=8u){
            unsigned channel=(pixel>>(shift+3u))&31u;
            channel=channel>steps?channel-steps:0u;
            result|=((channel<<3u)|(channel>>2u))<<shift;
        }
        portable_software_frame_store_physical(frame,x,y,result);
    }
}

PortableFrontendPresentationStatus portable_pause_menu_render_menu_palette(
    const PortableAssetPackView *pack, const PortablePauseMenu *menu,
    const PortableMenuPaletteProfile *profile,PortableSoftwareFrame *frame)
{
    if (pack == NULL || menu == NULL || frame == NULL || frame->pixels == NULL ||
        (menu->item_count != 5u && menu->item_count != 3u && menu->item_count != 2u) ||
        menu->cursor >= menu->item_count)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    for (unsigned row = 0; row < menu->item_count; ++row)
        if (menu->items[row] < PORTABLE_PAUSE_ACTION_CONTINUE ||
            menu->items[row] > PORTABLE_PAUSE_ACTION_GP_RETRY)
            return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    const PortablePauseOpening opening=portable_pause_menu_opening(menu);
    dim_pause_backdrop(frame,opening.dim_steps);
    PortableSoftwareFrame centered=portable_software_frame_native_view(frame,PORTABLE_FRAME_CENTER);
    frame=&centered;
    if(menu->graphics.open){
        portable_graphics_render_classic(pack,frame,&menu->graphics);
        return PORTABLE_FRONTEND_PRESENTATION_OK;
    }
    /* DDF18/DDEF4/DDF04: 10 tiles wide at x 10; rows 12/8/6 from y 4/4/5. */
    const unsigned panel = menu->item_count == 5u ? 0u : menu->item_count == 3u ? 1u : 2u;
    const int top = panel == 2u ? 40 : 32;
    const int half = (menu->item_count == 5u ? 96 : panel == 1u ? 64 : 48) / 2;
    const int center = top + half;
    /* Preserve the source's center-out opening using native image quadrants.
       Simulation stays frozen; only this presentation counter advances. */
    const int half_width = opening.half_width_tiles*8;
    const int half_height = opening.half_height_tiles*8;
    if(half_width==0||half_height==0)return PORTABLE_FRONTEND_PRESENTATION_OK;
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const int dx = column ? half_width - 40 : 40 - half_width;
            const int dy = row ? half_height - half : half - half_height;
            PortableFrontendPresentationStatus status = portable_frontend_image_draw(
                pack, PORTABLE_PAUSE_PANEL + panel, frame, 80 + dx, top + dy,
                column ? 120 : 120-half_width, row ? center : center-half_height,
                column ? 120+half_width : 120, row ? center+half_height : center);
            if (status != PORTABLE_FRONTEND_PRESENTATION_OK) return status;
        }
    }
    if (!opening.labels_visible)
        return PORTABLE_FRONTEND_PRESENTATION_OK;
    uint16_t colors[16];uint8_t mask[16];
    for(unsigned i=0u;i<16u;++i)mask[i]=1u;
    const int pulse=portable_menu_palette_colors(profile,menu->palette_clock,colors);
    for (unsigned row = 0; row < menu->item_count; ++row) {
        PortableFrontendPresentationStatus status = portable_frontend_image_draw_palette(
            pack, (row == menu->cursor ? PORTABLE_PAUSE_SELECTED_LABEL :
                   PORTABLE_PAUSE_LABEL) + menu->items[row] - 1u, frame, 96,
            top + 8 + (int)row*16, 0, 0, 240, 160,
            pulse?colors:NULL,pulse?16u:0u,pulse?mask:NULL);
        if (status != PORTABLE_FRONTEND_PRESENTATION_OK) return status;
    }
    return portable_frontend_image_draw(pack, PORTABLE_PAUSE_CURSOR, frame,
        88, top + 8 + (int)menu->cursor*16, 0, 0, 240, 160);
}

PortableFrontendPresentationStatus portable_pause_menu_render_menu(
    const PortableAssetPackView *pack,const PortablePauseMenu *menu,PortableSoftwareFrame *frame)
{ return portable_pause_menu_render_menu_palette(pack,menu,NULL,frame); }

PortableFrontendPresentationStatus portable_pause_menu_render(
    const PortableAssetPackView *pack, uint8_t cursor, uint8_t open_ticks,
    PortableSoftwareFrame *frame)
{
    PortablePauseMenu menu;
    portable_pause_menu_init(&menu, 0u);
    if (cursor >= PORTABLE_PAUSE_MENU_ITEMS)
        return PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT;
    menu.cursor = cursor;
    menu.open_ticks = open_ticks;
    return portable_pause_menu_render_menu(pack, &menu, frame);
}
