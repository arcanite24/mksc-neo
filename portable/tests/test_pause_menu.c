#include "pause_menu.h"

#include "frontend_flow.h"

#include <stdio.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr,"pause menu failed line %d: %s\n",__LINE__,#x); \
    return 1; } } while (0)

static PortablePauseAction tap(PortablePauseMenu *menu, uint16_t button)
{
    PortablePauseAction action = portable_pause_menu_tick(menu, button);
    if (portable_pause_menu_tick(menu, 0u) != PORTABLE_PAUSE_ACTION_NONE)
        return (PortablePauseAction)-1;
    return action;
}

static int open_menu(PortablePauseMenu *menu)
{
    for (unsigned tick = 0u; tick < PORTABLE_PAUSE_MENU_OPEN_TICKS; ++tick)
        if (portable_pause_menu_tick(menu, 0u) != PORTABLE_PAUSE_ACTION_NONE)
            return 0;
    return menu->open_ticks == PORTABLE_PAUSE_MENU_OPEN_TICKS;
}

int main(void)
{
    PortablePauseMenu menu;
    portable_pause_menu_init(&menu,PORTABLE_FRONTEND_BUTTON_START);
    for(unsigned turn=1u;turn<=12u;++turn){
        /* A pressed during setup is consumed, never queued for state2. */
        CHECK(portable_pause_menu_tick(&menu,turn==4u?PORTABLE_FRONTEND_BUTTON_A:0u)==PORTABLE_PAUSE_ACTION_NONE);
        PortablePauseOpening opening=portable_pause_menu_opening(&menu);
        unsigned expansion=turn>4u?turn-4u:0u;if(expansion>8u)expansion=8u;
        CHECK(opening.half_width_tiles==5u*expansion/8u);
        CHECK(opening.half_height_tiles==6u*expansion/8u);
        CHECK(opening.labels_visible==(turn==12u));
        if(turn==1u||turn==4u)CHECK(opening.half_width_tiles==0u&&opening.half_height_tiles==0u);
        if(turn==8u)CHECK(opening.half_width_tiles==2u&&opening.half_height_tiles==3u);
    }
    CHECK(portable_pause_menu_tick(&menu,PORTABLE_FRONTEND_BUTTON_A)==PORTABLE_PAUSE_ACTION_CONTINUE);
    CHECK(menu.open_ticks==12u); /* first active turn13 does not restart opening */
    CHECK(menu.palette_clock==1u);
    for(unsigned tick=0u;tick<64u;++tick)portable_pause_menu_tick(&menu,0u);
    CHECK(menu.palette_clock==65u&&menu.open_ticks==12u);
    menu.palette_clock=UINT16_MAX;portable_pause_menu_tick(&menu,0u);CHECK(menu.palette_clock==0u);
    for(unsigned lives=0u;lives<2u;++lives){
        portable_pause_menu_init_gp(&menu,0u,(int)lives);
        CHECK(open_menu(&menu));
        PortablePauseOpening opening=portable_pause_menu_opening(&menu);
        CHECK(opening.half_width_tiles==5u&&opening.half_height_tiles==(lives?4u:3u));
    }
    static const PortablePauseAction actions[PORTABLE_PAUSE_MENU_ITEMS] = {
        PORTABLE_PAUSE_ACTION_CONTINUE, PORTABLE_PAUSE_ACTION_RETRY,
        PORTABLE_PAUSE_ACTION_CHANGE_TRACK,
        PORTABLE_PAUSE_ACTION_CHANGE_CHARACTER, PORTABLE_PAUSE_ACTION_QUIT,
    };
    portable_pause_menu_init(&menu, PORTABLE_FRONTEND_BUTTON_START);
    CHECK(menu.cursor == 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_START) ==
          PORTABLE_PAUSE_ACTION_NONE);
    CHECK(menu.open_ticks == 1u);
    CHECK(open_menu(&menu));
    CHECK(tap(&menu, PORTABLE_FRONTEND_BUTTON_B) ==
          PORTABLE_PAUSE_ACTION_NONE && menu.cursor == 0u);
    CHECK(tap(&menu, PORTABLE_FRONTEND_BUTTON_UP) ==
          PORTABLE_PAUSE_ACTION_NONE && menu.cursor == 4u);
    CHECK(tap(&menu, PORTABLE_FRONTEND_BUTTON_DOWN) ==
          PORTABLE_PAUSE_ACTION_NONE && menu.cursor == 0u);
    for (uint8_t row = 0u; row < PORTABLE_PAUSE_MENU_ITEMS; ++row)
    {
        portable_pause_menu_init(&menu, 0u);
        CHECK(open_menu(&menu));
        for (uint8_t step = 0u; step < row; ++step)
            CHECK(tap(&menu, PORTABLE_FRONTEND_BUTTON_DOWN) ==
                  PORTABLE_PAUSE_ACTION_NONE);
        CHECK(tap(&menu, row == 2u ? PORTABLE_FRONTEND_BUTTON_START
                                   : PORTABLE_FRONTEND_BUTTON_A) == actions[row]);
    }
    portable_pause_menu_init(&menu, 0u);
    CHECK(open_menu(&menu));
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_DOWN) ==
          PORTABLE_PAUSE_ACTION_NONE && menu.cursor == 1u);
    for (unsigned frame = 0u; frame < 80u; ++frame)
        CHECK(portable_pause_menu_tick(&menu,
              PORTABLE_FRONTEND_BUTTON_DOWN) == PORTABLE_PAUSE_ACTION_NONE);
    CHECK(menu.cursor == 1u && menu.cue == 0u);
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_UP) ==
          PORTABLE_PAUSE_ACTION_NONE && menu.cue == 370u);
    CHECK(portable_pause_menu_tick(&menu, 0u) ==
          PORTABLE_PAUSE_ACTION_NONE && menu.cue == 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_A) ==
          PORTABLE_PAUSE_ACTION_CONTINUE && menu.cue == 0u);
    portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_DOWN);
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_A) ==
          PORTABLE_PAUSE_ACTION_RETRY && menu.cue == 143u);
    CHECK(portable_pause_menu_tick(&menu, UINT16_C(0x100)) ==
          PORTABLE_PAUSE_ACTION_NONE);
    portable_pause_menu_init_gp(&menu, 0u, 1);
    CHECK(menu.item_count == 3u);
    for (unsigned tick = 0u; tick < PORTABLE_PAUSE_MENU_OPEN_TICKS; ++tick)
        portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_UP) ==
          PORTABLE_PAUSE_ACTION_NONE && menu.cursor == 2u && menu.cue == 370u);
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_START) ==
          PORTABLE_PAUSE_ACTION_QUIT && menu.cue == 143u);
    portable_pause_menu_init_gp(&menu, 0u, 0);
    for (unsigned tick = 0u; tick < PORTABLE_PAUSE_MENU_OPEN_TICKS; ++tick)
        portable_pause_menu_tick(&menu, 0u);
    portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_DOWN);
    CHECK(menu.item_count == 2u && menu.cursor == 1u);
    portable_pause_menu_tick(&menu, 0u);
    portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_DOWN|PORTABLE_FRONTEND_BUTTON_L);
    CHECK(menu.cursor == 0u); /* wraps; a held shoulder is ignored */
    /* Select opens the native graphics rows; the original items stay untouched meanwhile. */
    portable_pause_menu_init(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_SELECT) == PORTABLE_PAUSE_ACTION_NONE &&
          !menu.graphics.open); /* Still opening. */
    CHECK(open_menu(&menu));
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_SELECT) == PORTABLE_PAUSE_ACTION_NONE &&
          menu.graphics.open && menu.cue == 143u);
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_A) == PORTABLE_PAUSE_ACTION_NONE &&
          menu.graphics.edit == 1 && menu.cursor == 0u);
    menu.graphics.edit = 0;
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_UP) == PORTABLE_PAUSE_ACTION_NONE &&
          menu.graphics.cursor == PORTABLE_PAUSE_GRAPHICS_ROWS - 1u && menu.cursor == 0u);
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_LEFT) == PORTABLE_PAUSE_ACTION_NONE &&
          menu.graphics.edit == -1);
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_B) == PORTABLE_PAUSE_ACTION_NONE &&
          !menu.graphics.open);
    portable_pause_menu_tick(&menu, 0u);
    CHECK(portable_pause_menu_tick(&menu, PORTABLE_FRONTEND_BUTTON_A) == PORTABLE_PAUSE_ACTION_CONTINUE);
    puts("portable pause menu: PASS");
    return 0;
}
