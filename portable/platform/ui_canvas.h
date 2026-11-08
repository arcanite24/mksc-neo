#ifndef PORTABLE_UI_CANVAS_H
#define PORTABLE_UI_CANVAS_H

#include <stddef.h>
#include <stdint.h>

/* High-resolution presentation canvas for the modern menus. Pixels are opaque
 * ARGB8888 by default, or premultiplied ARGB8888 when explicitly initialized
 * transparent. Coordinates are float pixels at physical window resolution with
 * pixel centers at +0.5. Every primitive is anti-aliased and clipped to the
 * canvas clip rectangle. Nothing here reads or writes game state. */
typedef struct UiCanvasSink UiCanvasSink;

typedef struct
{
    uint32_t *pixels;
    int width, height, stride;
    int clip_x0, clip_y0, clip_x1, clip_y1; /* Half-open clip rectangle. */
    uint8_t premultiplied_alpha;
    UiCanvasSink *sink; /* Explicitly bound draw recorder, otherwise CPU pixels. */
} UiCanvas;

typedef struct
{
    float x, y;
} UiPoint;

enum { UI_PAINT_STOPS = 4 };

/* Straight-alpha ARGB colors along a linear axis (one stop is a solid fill). */
typedef struct
{
    uint32_t colors[UI_PAINT_STOPS];
    float positions[UI_PAINT_STOPS];
    uint8_t count;
    float x0, y0, x1, y1;
} UiPaint;

/* Straight-alpha ARGB image, borrowed for the draw call. */
typedef struct
{
    const uint32_t *pixels;
    int width, height;
} UiImage;

enum
{
    UI_IMAGE_SMOOTH = 0,
    UI_IMAGE_NEAREST = 1u << 0,
    UI_IMAGE_GRAY = 1u << 1, /* Locked or unavailable: desaturated and dimmed. */
    UI_IMAGE_SILHOUETTE = 1u << 2 /* Alpha only, painted with the tint color. */
};

static inline uint32_t ui_rgba(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static inline uint32_t ui_with_alpha(uint32_t color, float alpha)
{
    float a = (float)(color >> 24) * alpha;
    if (a < 0.0f) a = 0.0f;
    if (a > 255.0f) a = 255.0f;
    return (color & 0x00ffffffu) | ((uint32_t)(a + 0.5f) << 24);
}
uint32_t ui_mix(uint32_t from, uint32_t to, float amount);

void ui_canvas_init(UiCanvas *canvas, uint32_t *pixels, int width, int height, int stride);
/* Inputs remain straight-alpha colors/images; stored pixels retain alpha for
 * composition with a premultiplied-alpha GPU blend mode. Clear before drawing. */
void ui_canvas_init_transparent(UiCanvas *canvas, uint32_t *pixels, int width, int height,
                                int stride);
void ui_canvas_clip(UiCanvas *canvas, int x0, int y0, int x1, int y1);
void ui_canvas_unclip(UiCanvas *canvas);
void ui_canvas_clear(UiCanvas *canvas, uint32_t color);

UiPaint ui_paint_solid(uint32_t color);
UiPaint ui_paint_linear(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1);
/* Adds a stop at position 0..1 along an existing linear paint. */
void ui_paint_stop(UiPaint *paint, float position, uint32_t color);

/* Convex polygon in either winding. feather is the transition width in pixels:
 * 1 gives a crisp anti-aliased edge, larger values a soft shadow or glow. */
void ui_fill_convex(UiCanvas *canvas, const UiPoint *points, int count, const UiPaint *paint,
                    float feather, float alpha);
/* Rounded rectangle whose x positions shift by skew*(row offset from center),
 * the slanted MK8 tab shape. radius is clamped to half the shorter side. */
void ui_fill_round_rect(UiCanvas *canvas, float x, float y, float w, float h, float radius,
                        float skew, const UiPaint *paint, float feather, float alpha);
/* A pointed tab: rectangle with chevron ends of the given depth. */
void ui_fill_chevron_bar(UiCanvas *canvas, float x, float y, float w, float h, float point,
                         const UiPaint *paint, float feather, float alpha);
void ui_fill_circle(UiCanvas *canvas, float cx, float cy, float radius, const UiPaint *paint,
                    float feather, float alpha);
void ui_fill_ellipse(UiCanvas *canvas, float cx, float cy, float rx, float ry,
                     const UiPaint *paint, float feather, float alpha);
void ui_stroke_circle(UiCanvas *canvas, float cx, float cy, float radius, float width,
                      uint32_t color, float alpha);
/* Rounded-rectangle outline of the given stroke width (inside the edge). */
void ui_stroke_round_rect(UiCanvas *canvas, float x, float y, float w, float h, float radius,
                          float skew, float width, uint32_t color, float alpha);
/* Axis-aligned fill without anti-aliasing; integer-snapped. */
void ui_fill_rect(UiCanvas *canvas, float x, float y, float w, float h, uint32_t color,
                  float alpha);

/* Draws the image into the destination rectangle. radius > 0 rounds the
 * destination corners. tint multiplies the image (white is unchanged), or is
 * the paint color with UI_IMAGE_SILHOUETTE. */
void ui_draw_image(UiCanvas *canvas, const UiImage *image, float x, float y, float w, float h,
                   float alpha, unsigned flags, uint32_t tint, float radius);
/* Draws a source sub-rectangle (in source pixels) into the destination. */
void ui_draw_image_region(UiCanvas *canvas, const UiImage *image, float sx, float sy, float sw,
                          float sh, float x, float y, float w, float h, float alpha,
                          unsigned flags, uint32_t tint, float radius);

/* Blends color over one pixel with coverage 0..1; used by the font renderer. */
void ui_blend_pixel(UiCanvas *canvas, int x, int y, uint32_t color, float coverage);
/* Multiplies every pixel inside the rectangle toward color by amount 0..1.
 * Transparent canvases retain destination alpha; color alpha is ignored. */
void ui_tint_rect(UiCanvas *canvas, int x0, int y0, int x1, int y1, uint32_t color,
                  float amount);

#endif
