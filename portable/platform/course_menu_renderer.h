#ifndef PORTABLE_COURSE_MENU_RENDERER_H
#define PORTABLE_COURSE_MENU_RENDERER_H
#include "frontend_presentation.h"
enum {
    PORTABLE_COURSE_BACKGROUND=0x23000,
    PORTABLE_COURSE_HEADING=0x23010,
    PORTABLE_COURSE_AFFINE=0x23020, /* +cup: source BG2 with cup palette5. */
    PORTABLE_COURSE_PANEL=0x23030, /* +cup: C8DAC frame colors7..15. */
    PORTABLE_COURSE_OK=0x23038, /* C8A58 confirmation cell. */
    PORTABLE_COURSE_CUP=0x23100,
    PORTABLE_COURSE_LOCKED_CUP=0x23120, /* +stock cup: source unavailable palette. */
    PORTABLE_COURSE_CUP_NAME=0x23200,
    PORTABLE_COURSE_PREVIEW=0x23300,
    PORTABLE_COURSE_NAME=0x23400,
    PORTABLE_COURSE_SELECTED_NAME=0x23500,
    PORTABLE_RECORDS_NAME=0x23600,  /* +stripe*64+course: 0800C78C list rows */
    PORTABLE_RECORDS_PANEL=0x23800, /* list panel over the settled pattern */
    PORTABLE_RECORDS_BEST=0x23801,
    PORTABLE_RECORDS_LAP=0x23802,
    PORTABLE_RECORDS_GLYPHS=0x23803, /* 8x16 cells: 0-9, ', ", dash (two cells) */
    PORTABLE_COURSE_RANK=0x23900,   /* +rating: three/two/one stars, A..E */
    PORTABLE_COURSE_TROPHY=0x23a00  /* +medal*16+frame: gold/silver/bronze */
};
PortableFrontendPresentationStatus portable_course_menu_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    PortableSoftwareFrame *frame);
/* The Time Trial records list: eight course rows on the stripe panel. */
PortableFrontendPresentationStatus portable_records_menu_render(
    const PortableAssetPackView *pack,const PortableFrontendFlow *flow,
    PortableSoftwareFrame *frame);
#endif
