#ifndef PORTABLE_GP_OPENING_VEIL_H
#define PORTABLE_GP_OPENING_VEIL_H

#include <stdint.h>

/* sub_8052864 writes BG0 tile rows every three frames. Frame zero is the
 * first partially visible Shy Guy Beach frame (result +1046). The tile
 * graphics are uniform black or transparent on each eight-pixel row. */
int portable_gp_opening_veil_apply(uint32_t frame,
                                   uint32_t pixels[240u * 160u]);

/* Source transition phase0..10; later phases expose the complete world. */
int portable_gp_opening_veil_row_hidden(uint8_t phase,unsigned row);

#endif
