#ifndef MKSC_NITRO_2D_H
#define MKSC_NITRO_2D_H
/* Nitro 2D graphics: an NSCR text screen over NCGR characters and an NCLR
 * palette, decoded to ARGB and cropped to its opaque pixels. */
#include "rom_import_internal.h"

typedef struct Nitro2DImage
{
    RiBuf *pixels;           /* width * height little-endian ARGB words */
    uint32_t width, height;  /* of the crop */
    uint32_t left, top;      /* crop origin within the screen */
} Nitro2DImage;

/* Fails naming `where` on malformed files, linear or 8-bit-per-cell
 * characters, or a screen with no opaque pixel. */
Nitro2DImage nitro_screen(RiCtx *ctx, RiData nclr, RiData ncgr, RiData nscr, const char *where);

#endif
