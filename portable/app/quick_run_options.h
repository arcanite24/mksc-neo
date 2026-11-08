#ifndef PORTABLE_QUICK_RUN_OPTIONS_H
#define PORTABLE_QUICK_RUN_OPTIONS_H

#include <stdint.h>

/* Source 58B0 save halfwords: Quick Run +0E/+10/+12, VS +06/+08/+0C plus
 * +0A mushroom start. Zero selects 3 laps, items on, coins on, no mushroom
 * start (the VS default). Values survive menu/race returns for the
 * application lifetime. */
typedef struct {
    uint8_t five_laps;
    uint8_t items_off;
    uint8_t coins_off;
    uint8_t mushroom_start; /* VS: sub_8048D44 gives every racer item 0x1008. */
} PortableQuickRunOptions;

#endif
