#include "pause_menu.h"

#include "frontend_flow.h"

PortablePauseOpening portable_pause_menu_opening(const PortablePauseMenu *menu)
{
    PortablePauseOpening opening={0};
    if(menu==0)return opening;
    const unsigned half_height=menu->item_count==5u?6u:menu->item_count==3u?4u:3u;
    /* The displayed pause keeps four entry/setup turns unchanged. State1's
     * eight expansion steps then appear on turns5..12; labels appear at12.
     * Its counter is already one step ahead of the displayed rectangle. */
    unsigned expansion=menu->open_ticks>4u?menu->open_ticks-4u:0u;
    if(expansion>8u)expansion=8u;
    opening.half_width_tiles=(uint8_t)(5u*expansion/8u);
    opening.half_height_tiles=(uint8_t)(half_height*expansion/8u);
    opening.labels_visible=(uint8_t)(menu->open_ticks>=PORTABLE_PAUSE_MENU_OPEN_TICKS);
    opening.dim_steps=(uint8_t)expansion;
    return opening;
}

void portable_pause_menu_init(PortablePauseMenu *menu,
                              uint16_t opening_buttons)
{
    if (menu != 0)
        *menu = (PortablePauseMenu){opening_buttons, 0u, 0u, 0u, 5u,
            {PORTABLE_PAUSE_ACTION_CONTINUE, PORTABLE_PAUSE_ACTION_RETRY,
             PORTABLE_PAUSE_ACTION_CHANGE_TRACK,
             PORTABLE_PAUSE_ACTION_CHANGE_CHARACTER, PORTABLE_PAUSE_ACTION_QUIT},0u,{0}};
}

void portable_pause_menu_init_gp(PortablePauseMenu *menu,
                                 uint16_t opening_buttons, int lives_remain)
{
    if (menu == 0)
        return;
    *menu = (PortablePauseMenu){opening_buttons, 0u, 0u, 0u, 2u,
        {PORTABLE_PAUSE_ACTION_CONTINUE, PORTABLE_PAUSE_ACTION_QUIT},0u,{0}};
    if (lives_remain) {
        menu->item_count = 3u;
        menu->items[1] = PORTABLE_PAUSE_ACTION_GP_RETRY;
        menu->items[2] = PORTABLE_PAUSE_ACTION_QUIT;
    }
}

int portable_pause_graphics_tick(PortablePauseGraphics *graphics, uint16_t triggered, int can_open,
                                 uint16_t *cue)
{
    if (!graphics->open)
    {
        if (!can_open || (triggered & PORTABLE_FRONTEND_BUTTON_SELECT) == 0u)
            return 0;
        graphics->open = 1u;
        graphics->notice = 0;
        *cue = 143u;
        return 1;
    }
    if ((triggered & (PORTABLE_FRONTEND_BUTTON_B | PORTABLE_FRONTEND_BUTTON_SELECT |
                      PORTABLE_FRONTEND_BUTTON_START)) != 0u)
        graphics->open = 0u;
    else if ((triggered & PORTABLE_FRONTEND_BUTTON_UP) != 0u)
    {
        graphics->cursor = graphics->cursor == 0u ? PORTABLE_PAUSE_GRAPHICS_ROWS - 1u : graphics->cursor - 1u;
        graphics->notice = 0;
        *cue = 370u;
    }
    else if ((triggered & PORTABLE_FRONTEND_BUTTON_DOWN) != 0u)
    {
        graphics->cursor = graphics->cursor + 1u == PORTABLE_PAUSE_GRAPHICS_ROWS ? 0u : graphics->cursor + 1u;
        graphics->notice = 0;
        *cue = 370u;
    }
    else if ((triggered & PORTABLE_FRONTEND_BUTTON_LEFT) != 0u)
        graphics->edit = -1;
    else if ((triggered & (PORTABLE_FRONTEND_BUTTON_RIGHT | PORTABLE_FRONTEND_BUTTON_A)) != 0u)
        graphics->edit = 1;
    return 1;
}

PortablePauseAction portable_pause_menu_tick(PortablePauseMenu *menu,
                                              uint16_t buttons)
{
    uint16_t triggered;
    if (menu == 0 || menu->item_count == 0u ||
        menu->item_count > PORTABLE_PAUSE_MENU_ITEMS ||
        menu->cursor >= menu->item_count)
        return PORTABLE_PAUSE_ACTION_NONE;
    menu->cue = 0u;
    buttons &= UINT16_C(0x00ff); /* Shoulders do nothing here. */
    triggered = buttons & (uint16_t)~menu->buttons;
    menu->buttons = buttons;
    if (menu->open_ticks < PORTABLE_PAUSE_MENU_OPEN_TICKS)
    {
        ++menu->open_ticks;
        return PORTABLE_PAUSE_ACTION_NONE;
    }
    menu->palette_clock=(uint16_t)(menu->palette_clock+1u);
    if (portable_pause_graphics_tick(&menu->graphics, triggered, 1, &menu->cue))
        return PORTABLE_PAUSE_ACTION_NONE;
    if ((triggered & PORTABLE_FRONTEND_BUTTON_UP) != 0u)
    {
        menu->cursor = menu->cursor == 0u
            ? menu->item_count - 1u : menu->cursor - 1u;
        menu->cue = 370u;
    }
    if ((triggered & PORTABLE_FRONTEND_BUTTON_DOWN) != 0u)
    {
        menu->cursor = menu->cursor + 1u == menu->item_count
            ? 0u : menu->cursor + 1u;
        menu->cue = 370u;
    }
    if ((triggered & (PORTABLE_FRONTEND_BUTTON_A |
                      PORTABLE_FRONTEND_BUTTON_START)) != 0u)
    {
        if (menu->items[menu->cursor] != PORTABLE_PAUSE_ACTION_CONTINUE)
            menu->cue = 143u;
        return (PortablePauseAction)menu->items[menu->cursor];
    }
    return PORTABLE_PAUSE_ACTION_NONE;
}
