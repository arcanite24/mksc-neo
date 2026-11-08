#ifndef PORTABLE_PAUSE_MENU_H
#define PORTABLE_PAUSE_MENU_H

#include <stdint.h>

#define PORTABLE_PAUSE_MENU_ITEMS 5u
#define PORTABLE_PAUSE_MENU_OPEN_TICKS 12u

typedef enum
{
    PORTABLE_PAUSE_ACTION_NONE = 0,
    PORTABLE_PAUSE_ACTION_CONTINUE,
    PORTABLE_PAUSE_ACTION_RETRY,
    PORTABLE_PAUSE_ACTION_CHANGE_TRACK,
    PORTABLE_PAUSE_ACTION_CHANGE_CHARACTER,
    PORTABLE_PAUSE_ACTION_QUIT,
    PORTABLE_PAUSE_ACTION_GP_RETRY /* 2C7F4 item 5: spends a life first */
} PortablePauseAction;

/* Native addition to the recovered list: Select opens the graphics rows (see
 * platform/graphics_options.c). It never changes the five original items. */
#define PORTABLE_PAUSE_GRAPHICS_ROWS 11u
#define PORTABLE_PAUSE_GRAPHICS_VALUE 20u

typedef struct
{
    uint8_t open, cursor;
    int8_t edit; /* -1/+1 requested by the last tick; the host applies and clears it. */
    const char *notice; /* Result of the last edit (static text), or NULL. */
    char values[PORTABLE_PAUSE_GRAPHICS_ROWS][PORTABLE_PAUSE_GRAPHICS_VALUE]; /* Filled by the host. */
} PortablePauseGraphics;

/* Handles one tick of newly pressed buttons for the page: Select opens it
 * (when `can_open`); Up/Down move, Left/Right/A request an edit, B/Select/
 * Start close it. Returns 1 when the page owns the buttons this tick, and a
 * menu cue (370 cursor) in *cue when one should sound. */
int portable_pause_graphics_tick(PortablePauseGraphics *graphics, uint16_t triggered, int can_open,
                                 uint16_t *cue);

typedef struct
{
    uint16_t buttons;
    uint8_t cursor;
    uint8_t open_ticks;
    uint16_t cue; /* 802C06C: 370 cursor, 143 non-continue confirm; one tick. */
    uint8_t item_count;
    uint8_t items[PORTABLE_PAUSE_MENU_ITEMS]; /* PortablePauseAction per row */
    uint16_t palette_clock; /* 2C484: private counter, reset after label setup. */
    PortablePauseGraphics graphics;
} PortablePauseMenu;

typedef struct
{
    uint8_t half_width_tiles;
    uint8_t half_height_tiles;
    uint8_t labels_visible;
    uint8_t dim_steps;
} PortablePauseOpening;

/* 2C06C: setup turns, eight tile-quantized expansion turns, then labels. */
PortablePauseOpening portable_pause_menu_opening(const PortablePauseMenu *menu);

/* Time Trial / Quick Run / Battle list DDF18: Continue, Retry, Change
 * Track, Change Character, Quit. */
void portable_pause_menu_init(PortablePauseMenu *menu,
                              uint16_t opening_buttons);
/* GP lists: DDEF4 Continue, GP Retry, Quit while lives remain; else DDF04. */
void portable_pause_menu_init_gp(PortablePauseMenu *menu,
                                 uint16_t opening_buttons, int lives_remain);
PortablePauseAction portable_pause_menu_tick(PortablePauseMenu *menu,
                                              uint16_t buttons);

#endif
