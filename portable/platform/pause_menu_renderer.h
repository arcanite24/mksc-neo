#ifndef PORTABLE_PAUSE_MENU_RENDERER_H
#define PORTABLE_PAUSE_MENU_RENDERER_H
#include "frontend_image_renderer.h"
#include "pause_menu.h"
#include "menu_palette.h"

enum {
    PORTABLE_PAUSE_PANEL = 0x24000,
    PORTABLE_PAUSE_LABEL = 0x24100,
    PORTABLE_PAUSE_SELECTED_LABEL = 0x24200,
    PORTABLE_PAUSE_CURSOR = 0x24300
};
PortableFrontendPresentationStatus portable_pause_menu_render(
    const PortableAssetPackView *pack, uint8_t cursor, uint8_t open_ticks,
    PortableSoftwareFrame *frame);
/* Any item list: labels are PORTABLE_PAUSE_LABEL + action - 1; the 3- and
 * 2-item GP panels are PORTABLE_PAUSE_PANEL + 1 and + 2. */
PortableFrontendPresentationStatus portable_pause_menu_render_menu(
    const PortableAssetPackView *pack, const PortablePauseMenu *menu,
    PortableSoftwareFrame *frame);
PortableFrontendPresentationStatus portable_pause_menu_render_menu_palette(
    const PortableAssetPackView *pack,const PortablePauseMenu *menu,
    const PortableMenuPaletteProfile *profile,PortableSoftwareFrame *frame);
#endif
