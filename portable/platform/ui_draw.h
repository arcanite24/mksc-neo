#ifndef PORTABLE_UI_DRAW_H
#define PORTABLE_UI_DRAW_H

#include "ui_canvas.h"

/* SDL-free draw boundary. Pointers in a command are borrowed only during emit:
 * an asynchronous recorder must retain/copy them before returning. Commands
 * never advance a menu or race clock. */
typedef enum
{
    /* CLEAR and CHECKER overwrite the full canvas, ignoring its clip, just
     * like the CPU implementations. All other commands use the captured clip. */
    UI_DRAW_CLEAR,
    UI_DRAW_CONVEX,
    UI_DRAW_ROUND_RECT,
    UI_DRAW_ELLIPSE,
    UI_DRAW_STROKE_CIRCLE,
    UI_DRAW_STROKE_ROUND_RECT,
    UI_DRAW_RECT,
    UI_DRAW_IMAGE,
    UI_DRAW_TINT,
    UI_DRAW_GLYPH,
    UI_DRAW_CHECKER
} UiDrawKind;

typedef struct
{
    UiDrawKind kind;
    int clip_x0, clip_y0, clip_x1, clip_y1;
    uint8_t premultiplied_alpha;
    union
    {
        uint32_t clear;
        struct { const UiPoint *points; int count; UiPaint paint; float feather, alpha; } convex;
        struct
        {
            float x, y, w, h, radius, skew, feather, alpha, stroke;
            UiPaint paint;
        } box;
        struct { float cx, cy, rx, ry, feather, alpha, stroke; UiPaint paint; } ellipse;
        struct { float x, y, w, h, alpha; uint32_t color; } rect;
        struct
        {
            const UiImage *source;
            float sx, sy, sw, sh, x, y, w, h, alpha, radius;
            unsigned flags;
            uint32_t tint;
        } image;
        struct { int x0, y0, x1, y1; uint32_t color; float amount; } tint;
        struct
        {
            const unsigned char *field;
            int width, height, face, slot;
            float x, y, scale, outline, softness, alpha;
            uint32_t color;
        } glyph;
        struct { float scale, seconds; } checker;
    } data;
} UiDrawCommand;

struct UiCanvasSink
{
    void *owner;
    /* 0 makes failure sticky. A failed sink consumes subsequent commands
     * without executing them; the host then rerenders the entire CPU frame. */
    int (*emit)(void *owner, const UiDrawCommand *command);
    uint8_t failed;
};

/* Returns 1 when a bound sink consumed the operation (including failure).
 * The command's clip/mode fields are filled from the current canvas. */
int ui_canvas_emit(UiCanvas *canvas, UiDrawCommand *command);
void ui_canvas_bind_sink(UiCanvas *canvas, UiCanvasSink *sink);
/* An initialized canvas may switch modes without discarding its bound sink.
 * Ordinary initializers always clear the sink, including on reused storage. */
void ui_canvas_reset_mode(UiCanvas *canvas, int transparent);

#endif
