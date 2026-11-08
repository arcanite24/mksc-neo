#ifndef PORTABLE_CHARACTER_MENU_RENDERER_H
#define PORTABLE_CHARACTER_MENU_RENDERER_H
#include "frontend_presentation.h"
#include "native_image.h"
enum {
    PORTABLE_CHARACTER_HEADING=0x22000,
    PORTABLE_CHARACTER_CURSOR=0x22010, /* Player 1's cursor; anchors give slot offsets. */
    PORTABLE_CHARACTER_PORTRAIT=0x22100,
    PORTABLE_CHARACTER_NAME=0x22200,
    PORTABLE_CHARACTER_PREVIEW=0x22300,
    PORTABLE_CHARACTER_STATS=0x22020, /* +slot: speed/weight stars, none for Random */
    /* Roster driver art: 0x22400 + character ID * 8 + part, placed for the
     * roster row's art slot. Missing parts use the seat driver's stock art. */
    PORTABLE_CHARACTER_ROSTER_ART=0x22400,
    PORTABLE_CHARACTER_ROSTER_PORTRAIT=0, /* +0..2 idle/selected/locked animations */
    PORTABLE_CHARACTER_ROSTER_NAME=3,
    PORTABLE_CHARACTER_ROSTER_PREVIEW=4,
    PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT=5, /* 16x16, race HUD and standings/results boards */
    PORTABLE_CHARACTER_ROSTER_BOARD_NAME=6 /* at most 40x16, standings/results boards */
};
uint32_t portable_character_roster_art(uint32_t character_id);
typedef struct
{
    uint32_t portrait,name,preview; /* portrait is the idle-state ID (+1 selected, +2 locked). */
    uint8_t art_slot;
} PortableCharacterSlotArt;
/* Art shown for a grid slot: a roster driver's own art where the pack has
 * it, else its seat driver's stock art. 0 for an empty roster slot. */
int portable_character_menu_slot_art(const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
                                     uint8_t page,uint8_t slot,PortableCharacterSlotArt *out);
/* Resolve a content driver through the same grid/seat mapping without a live
 * frontend flow (for results portraits and other read-only presentations). */
int portable_character_menu_driver_art(const PortableAssetPackView *pack,uint32_t character_id,
                                       PortableCharacterSlotArt *out);
/* A roster driver's own race portrait or board name, sized to replace the
 * stock cells; 0 when the pack has none (the seat's stock art stays). */
int portable_character_roster_image(const PortableAssetPackView *pack,
    uint32_t character_id,unsigned part,PortableNativeImage *out);
/* Name image shown for a selectable driver, or 0 when it is not on the grid. */
uint32_t portable_character_menu_name_id(const PortableAssetPackView *pack,
    const PortableFrontendFlow *flow,uint32_t character_id);
/* preview may be NULL; its hook replaces 3D-only drivers' stock preview. */
PortableFrontendPresentationStatus portable_character_menu_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    const PortableCharacterPreviewHook *preview,PortableSoftwareFrame *frame);
#endif
