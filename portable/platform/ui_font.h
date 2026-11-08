#ifndef PORTABLE_UI_FONT_H
#define PORTABLE_UI_FONT_H

#include "ui_canvas.h"

/* Embedded Nunito faces (SIL OFL 1.1, portable/third_party/fonts) rendered
 * from cached signed-distance glyphs, so one cache serves every size and
 * outlines/shadows cost no extra rasterization. */
typedef enum
{
    UI_FONT_BLACK,
    UI_FONT_BOLD,
    UI_FONT_ITALIC,
    UI_FONT_COUNT
} UiFontFace;

typedef enum
{
    UI_ALIGN_LEFT,
    UI_ALIGN_CENTER,
    UI_ALIGN_RIGHT
} UiAlign;

typedef struct
{
    UiFontFace face;
    float size;             /* Cap-to-descender em size in pixels. */
    uint32_t color;
    float outline;          /* Outline width in pixels; 0 for none. */
    uint32_t outline_color;
    float shadow_x, shadow_y, shadow_blur;
    uint32_t shadow_color;  /* Zero alpha disables the shadow. */
    float tracking;         /* Extra advance per glyph, in pixels. */
} UiTextStyle;

UiTextStyle ui_text_style(UiFontFace face, float size, uint32_t color);
/* 1 when the embedded faces decoded; text calls are no-ops otherwise. */
int ui_font_ready(void);
float ui_text_width(const UiTextStyle *style, const char *text);
/* Ascent above the baseline for the style's size (for vertical centering). */
float ui_text_cap_height(const UiTextStyle *style);
/* Draws text with its baseline at y. */
void ui_text_draw(UiCanvas *canvas, const UiTextStyle *style, float x, float y, UiAlign align,
                  const char *text, float alpha);
/* Draws text vertically centered on y (cap height), the usual label anchor. */
void ui_text_draw_centered(UiCanvas *canvas, const UiTextStyle *style, float x, float y,
                           UiAlign align, const char *text, float alpha);
/* Shrinks the style's size so text fits max_width; returns the fitted style. */
UiTextStyle ui_text_fit(const UiTextStyle *style, const char *text, float max_width);
/* Word-wraps into at most max_lines lines of max_width, drawn from the first
 * baseline y with the given line height. Returns the number of lines drawn. */
int ui_text_draw_wrapped(UiCanvas *canvas, const UiTextStyle *style, float x, float y,
                         float max_width, float line_height, int max_lines, UiAlign align,
                         const char *text, float alpha);
/* Releases cached glyphs (process exit; the cache refills on demand). */
void ui_font_release(void);

#endif
