#ifndef MKSC_PORTABLE_GP_ITEM_HUD_H
#define MKSC_PORTABLE_GP_ITEM_HUD_H
#include <stdint.h>

typedef struct {
    uint8_t logical_page,physical_page;
} PortableGpItemHudSelection;

/* D340/D3E0/F128/F15C/D4B4 item-window state. Zero initialization is blank. */
typedef struct {
    uint16_t current, previous, remaining, displayed_item;
} PortableGpItemHud;

int portable_gp_item_hud_selection(uint16_t item,PortableGpItemHudSelection *out);
void portable_gp_item_hud_transition(PortableGpItemHud *state,
    uint16_t previous,uint16_t current);
void portable_gp_item_hud_consume(PortableGpItemHud *state);
/* One HUD turn; returns the source song request, or zero. Finished freezes it. */
uint16_t portable_gp_item_hud_tick(PortableGpItemHud *state,uint8_t finished);
#endif
