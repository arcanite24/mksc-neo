#ifndef PORTABLE_MENU_CHROME_RENDERER_H
#define PORTABLE_MENU_CHROME_RENDERER_H
#include "frontend_presentation.h"

/* Normalized ROM art shared by the mode, character and course menus. */
enum {
    PORTABLE_MENU_TAB_GAME=0x26000,      /* +players-1: 1-P..4-P */
    PORTABLE_MENU_TAB_MODE=0x26010,      /* +action: GP, TT, VS, Battle, -, Quick Run */
    PORTABLE_MENU_TAB_CLASS=0x26020,     /* +50/100/150cc */
    PORTABLE_MENU_TAB_TRIAL=0x26030,     /* +trial, records */
    PORTABLE_MENU_TAB_DRIVER=0x26040,    /* +stock driver 0..7 face */
    PORTABLE_MENU_PLAYER_BANNER=0x26050, /* +player 0..3 */
    PORTABLE_MENU_SETTINGS_HINT=0x26060, /* SETTINGS / PRESS SELECT */
    PORTABLE_MENU_OPTION_CURSOR=0x26061,
    PORTABLE_MENU_OPTION_BRACKET=0x26062,
    PORTABLE_MENU_OPTION_WINDOW=0x26070, /* +action: TT 1, VS 2, Quick Run 5 */
    PORTABLE_CHARACTER_PLAYER_CURSOR=0x26100, /* +player*16+slot */
    PORTABLE_CHARACTER_PLAYER_BADGE=0x26200   /* +player*16+slot */
};

/* Number of breadcrumb tabs the page shows; 0 outside the shell menus. */
unsigned portable_menu_chrome_tabs(const PortableFrontendFlow *flow,PortableFrontendPage page);
/* spm_loadOtherGfx tabs, the player banner, the SETTINGS hint and the
 * 58B0 settings window. The page's newest tab slides in over its first 16
 * ticks; drawing never changes flow state. */
PortableFrontendPresentationStatus portable_menu_chrome_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    PortableFrontendPage page,PortableSoftwareFrame *frame);
#endif
