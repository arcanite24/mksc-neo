#ifndef PORTABLE_MENU_FONT_H
#define PORTABLE_MENU_FONT_H
#include "frontend_presentation.h"

/* The sub_8005590 window font and border, for screens with no original
 * layout (options, records boards, notices). Glyphs are 8x16 cells. */
enum {
    PORTABLE_MENU_FONT=0x26080,  /* ASCII 0x20..0x7F atlas */
    PORTABLE_MENU_FRAME=0x26081, /* 24x24 border nine-slice */
    PORTABLE_MENU_FONT_WIDTH=8,
    PORTABLE_MENU_FONT_HEIGHT=16
};

/* Draws text left to right; other bytes show as '?'. Returns the pen x. */
int portable_menu_text(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    int x,int y,const char *text);
/* Centres text on x. */
int portable_menu_text_centered(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    int x,int y,const char *text);
/* Wraps at spaces into lines of at most columns glyphs, line_height apart;
 * words longer than a line are cut. Returns the number of lines; a NULL
 * pack draws nothing, which measures a text first. */
unsigned portable_menu_text_wrapped(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    int x,int y,unsigned columns,int line_height,unsigned max_lines,const char *text);
/* sub_8005590's backdrop: every channel at 3/4 (5-bit) behind a window. */
void portable_menu_dim(PortableSoftwareFrame *frame);
/* A bordered window with an open interior; width and height are rounded
 * down to whole tiles (at least two each). */
PortableFrontendPresentationStatus portable_menu_frame(const PortableAssetPackView *pack,
    PortableSoftwareFrame *frame,int x,int y,unsigned width,unsigned height);
/* A window over its own dimmed menu backdrop: the 58B0 look for native
 * pages and dialogs, opaque so nothing beneath shows through. */
PortableFrontendPresentationStatus portable_menu_panel(const PortableAssetPackView *pack,
    PortableSoftwareFrame *frame,int x,int y,unsigned width,unsigned height);
/* A race notice (mod messages): up to three 26-glyph lines in a panel near
 * the top of a 240x160 view. Returns 0 when the panel art is unusable. */
int portable_menu_notice(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,const char *text);
#endif
