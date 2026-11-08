#ifndef PORTABLE_GP_ITEM_ART_CLOCK_H
#define PORTABLE_GP_ITEM_ART_CLOCK_H

#include <stdint.h>
#include "../core/item_art_intro_rules.h"

/* DAT_03002348 belongs to4389C, independently of the scene's2E30.
 * pending:0 normal,1 next callback suspends,2 that callback awaits completion.
 * Copy this native state through reload; never reset it with the scene clock. */
typedef struct {
    uint32_t calls;
    uint8_t pending;
} PortableGpItemArtClock;

int portable_gp_item_art_clock_begin_direct(PortableGpItemArtClock *clock,
    uint16_t retained_countdown,const PortableItemArtIntroRules *rules);
int portable_gp_item_art_clock_suspend_next(PortableGpItemArtClock *clock);
int portable_gp_item_art_clock_tick(PortableGpItemArtClock *clock,
    uint8_t scene_mode);
int portable_gp_item_art_clock_complete(PortableGpItemArtClock *clock);

#endif
