#include "ui_canvas.h"
#include "ui_draw.h"

#include <math.h>

static int clampi(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}
static float clampf(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

uint32_t ui_mix(uint32_t from, uint32_t to, float amount)
{
    amount = clampf(amount, 0.0f, 1.0f);
    uint32_t result = 0u;
    for (unsigned shift = 0u; shift <= 24u; shift += 8u)
    {
        const float a = (float)((from >> shift) & 255u), b = (float)((to >> shift) & 255u);
        result |= (uint32_t)(a + (b - a) * amount + 0.5f) << shift;
    }
    return result;
}

void ui_canvas_init(UiCanvas *canvas, uint32_t *pixels, int width, int height, int stride)
{
    canvas->pixels = pixels;
    canvas->width = width;
    canvas->height = height;
    canvas->stride = stride > 0 ? stride : width;
    canvas->premultiplied_alpha = 0u;
    canvas->sink = NULL;
    ui_canvas_unclip(canvas);
}

void ui_canvas_init_transparent(UiCanvas *canvas, uint32_t *pixels, int width, int height,
                                int stride)
{
    ui_canvas_init(canvas, pixels, width, height, stride);
    canvas->premultiplied_alpha = 1u;
}

void ui_canvas_bind_sink(UiCanvas *canvas, UiCanvasSink *sink)
{
    canvas->sink = sink;
}

void ui_canvas_reset_mode(UiCanvas *canvas, int transparent)
{
    canvas->premultiplied_alpha = (uint8_t)(transparent != 0);
    ui_canvas_unclip(canvas);
}

int ui_canvas_emit(UiCanvas *canvas, UiDrawCommand *command)
{
    UiCanvasSink *sink = canvas->sink;
    if (sink == NULL) return 0;
    command->clip_x0 = canvas->clip_x0;
    command->clip_y0 = canvas->clip_y0;
    command->clip_x1 = canvas->clip_x1;
    command->clip_y1 = canvas->clip_y1;
    command->premultiplied_alpha = canvas->premultiplied_alpha;
    if (!sink->failed && (sink->emit == NULL || !sink->emit(sink->owner, command))) sink->failed = 1u;
    return 1;
}

void ui_canvas_clip(UiCanvas *canvas, int x0, int y0, int x1, int y1)
{
    canvas->clip_x0 = clampi(x0, 0, canvas->width);
    canvas->clip_y0 = clampi(y0, 0, canvas->height);
    canvas->clip_x1 = clampi(x1, canvas->clip_x0, canvas->width);
    canvas->clip_y1 = clampi(y1, canvas->clip_y0, canvas->height);
}

void ui_canvas_unclip(UiCanvas *canvas)
{
    canvas->clip_x0 = canvas->clip_y0 = 0;
    canvas->clip_x1 = canvas->width;
    canvas->clip_y1 = canvas->height;
}

/* Exact divide-by-255 for products of two bytes. */
static inline unsigned div255(unsigned value)
{
    value += 128u;
    return (value + (value >> 8)) >> 8;
}

static uint32_t premultiply(uint32_t color)
{
    const unsigned alpha = color >> 24;
    return (color & 0xff000000u) | (div255(((color >> 16) & 255u) * alpha) << 16) |
           (div255(((color >> 8) & 255u) * alpha) << 8) | div255((color & 255u) * alpha);
}

void ui_canvas_clear(UiCanvas *canvas, uint32_t color)
{
    UiDrawCommand command = {.kind = UI_DRAW_CLEAR, .data.clear = color};
    if (ui_canvas_emit(canvas, &command)) return;
    const uint32_t value = canvas->premultiplied_alpha ? premultiply(color) : color | 0xff000000u;
    for (int y = 0; y < canvas->height; ++y)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;
        for (int x = 0; x < canvas->width; ++x) row[x] = value;
    }
}

static inline void blend_opaque(uint32_t *destination, uint32_t color, unsigned alpha)
{
    if (alpha == 0u) return;
    if (alpha >= 255u)
    {
        *destination = color | 0xff000000u;
        return;
    }
    const uint32_t old = *destination;
    const unsigned inverse = 255u - alpha;
    const unsigned r = div255(((color >> 16) & 255u) * alpha + ((old >> 16) & 255u) * inverse);
    const unsigned g = div255(((color >> 8) & 255u) * alpha + ((old >> 8) & 255u) * inverse);
    const unsigned b = div255((color & 255u) * alpha + (old & 255u) * inverse);
    *destination = 0xff000000u | (r << 16) | (g << 8) | b;
}

/* Source colors are straight-alpha; destination channels are already weighted
 * by their alpha. Coverage changes only the source contribution. */
static inline void blend_premultiplied(uint32_t *destination, uint32_t color, unsigned alpha)
{
    if (alpha == 0u) return;
    if (alpha >= 255u)
    {
        *destination = color | 0xff000000u;
        return;
    }
    const uint32_t old = *destination;
    const unsigned inverse = 255u - alpha;
    const unsigned a = alpha + div255((old >> 24) * inverse);
    const unsigned r = div255(((color >> 16) & 255u) * alpha + ((old >> 16) & 255u) * inverse);
    const unsigned g = div255(((color >> 8) & 255u) * alpha + ((old >> 8) & 255u) * inverse);
    const unsigned b = div255((color & 255u) * alpha + (old & 255u) * inverse);
    *destination = (a << 24) | (r << 16) | (g << 8) | b;
}

static inline void blend(const UiCanvas *canvas, uint32_t *destination, uint32_t color,
                         unsigned alpha)
{
    if (canvas->premultiplied_alpha) blend_premultiplied(destination, color, alpha);
    else blend_opaque(destination, color, alpha);
}

/* Blends one color at a constant alpha over a run of pixels. */
static void blend_span(const UiCanvas *canvas, uint32_t *destination, int count,
                       uint32_t color, unsigned alpha)
{
    if (alpha == 0u || count <= 0) return;
    if (canvas->premultiplied_alpha)
    {
        for (int i = 0; i < count; ++i) blend_premultiplied(destination + i, color, alpha);
        return;
    }
    if (alpha >= 255u)
    {
        const uint32_t value = color | 0xff000000u;
        for (int i = 0; i < count; ++i) destination[i] = value;
        return;
    }
    const unsigned inverse = 255u - alpha;
    const unsigned r = ((color >> 16) & 255u) * alpha, g = ((color >> 8) & 255u) * alpha, b = (color & 255u) * alpha;
    for (int i = 0; i < count; ++i)
    {
        const uint32_t old = destination[i];
        destination[i] = 0xff000000u | (div255(r + ((old >> 16) & 255u) * inverse) << 16) |
                         (div255(g + ((old >> 8) & 255u) * inverse) << 8) | div255(b + (old & 255u) * inverse);
    }
}

void ui_blend_pixel(UiCanvas *canvas, int x, int y, uint32_t color, float coverage)
{
    if (x < canvas->clip_x0 || y < canvas->clip_y0 || x >= canvas->clip_x1 ||
        y >= canvas->clip_y1 || coverage <= 0.0f)
        return;
    UiDrawCommand command = {.kind = UI_DRAW_RECT,
        .data.rect = {(float)x, (float)y, 1.0f, 1.0f, coverage, color}};
    if (ui_canvas_emit(canvas, &command)) return;
    const float alpha = (float)(color >> 24) * (coverage > 1.0f ? 1.0f : coverage);
    blend(canvas, canvas->pixels + (size_t)y * (size_t)canvas->stride + (size_t)x, color,
          (unsigned)(alpha + 0.5f));
}

void ui_tint_rect(UiCanvas *canvas, int x0, int y0, int x1, int y1, uint32_t color, float amount)
{
    UiDrawCommand command = {.kind = UI_DRAW_TINT,
        .data.tint = {x0, y0, x1, y1, color, amount}};
    if (ui_canvas_emit(canvas, &command)) return;
    x0 = clampi(x0, canvas->clip_x0, canvas->clip_x1);
    x1 = clampi(x1, canvas->clip_x0, canvas->clip_x1);
    y0 = clampi(y0, canvas->clip_y0, canvas->clip_y1);
    y1 = clampi(y1, canvas->clip_y0, canvas->clip_y1);
    const unsigned alpha = (unsigned)(clampf(amount, 0.0f, 1.0f) * 255.0f + 0.5f);
    for (int y = y0; y < y1; ++y)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;
        for (int x = x0; x < x1; ++x)
        {
            if (!canvas->premultiplied_alpha)
            {
                blend_opaque(row + x, color, alpha);
                continue;
            }
            /* Tint changes existing ink, never its coverage. Weight the tint
             * by destination alpha so transparent areas remain transparent. */
            const uint32_t old = row[x];
            const uint32_t target = premultiply((color & 0x00ffffffu) | (old & 0xff000000u));
            const unsigned inverse = 255u - alpha;
            const unsigned r = div255(((target >> 16) & 255u) * alpha + ((old >> 16) & 255u) * inverse);
            const unsigned g = div255(((target >> 8) & 255u) * alpha + ((old >> 8) & 255u) * inverse);
            const unsigned b = div255((target & 255u) * alpha + (old & 255u) * inverse);
            row[x] = (old & 0xff000000u) | (r << 16) | (g << 8) | b;
        }
    }
}

UiPaint ui_paint_solid(uint32_t color)
{
    UiPaint paint = {{color, 0u, 0u, 0u}, {0.0f, 1.0f, 1.0f, 1.0f}, 1u, 0.0f, 0.0f, 0.0f, 1.0f};
    return paint;
}

UiPaint ui_paint_linear(float x0, float y0, uint32_t c0, float x1, float y1, uint32_t c1)
{
    UiPaint paint = {{c0, c1, 0u, 0u}, {0.0f, 1.0f, 1.0f, 1.0f}, 2u, x0, y0, x1, y1};
    return paint;
}

void ui_paint_stop(UiPaint *paint, float position, uint32_t color)
{
    if (paint->count >= UI_PAINT_STOPS) return;
    unsigned at = paint->count;
    while (at > 0u && paint->positions[at - 1u] > position)
    {
        paint->positions[at] = paint->positions[at - 1u];
        paint->colors[at] = paint->colors[at - 1u];
        --at;
    }
    paint->positions[at] = position;
    paint->colors[at] = color;
    ++paint->count;
}

typedef struct
{
    const UiPaint *paint;
    float tx, ty, origin; /* t = x*tx + y*ty - origin */
    unsigned samples;     /* Once a paint is sampled often, colors come from lut. */
    int has_lut;
    uint32_t lut[256];
} PaintEval;

static void paint_prepare(PaintEval *eval, const UiPaint *paint)
{
    eval->paint = paint;
    eval->samples = 0u;
    eval->has_lut = 0;
    const float dx = paint->x1 - paint->x0, dy = paint->y1 - paint->y0;
    const float length = dx * dx + dy * dy;
    if (paint->count < 2u || length <= 1e-6f)
    {
        eval->tx = eval->ty = eval->origin = 0.0f;
        return;
    }
    eval->tx = dx / length;
    eval->ty = dy / length;
    eval->origin = paint->x0 * eval->tx + paint->y0 * eval->ty;
}

static uint32_t paint_mix(const UiPaint *paint, float t)
{
    if (paint->count <= 1u || t <= paint->positions[0]) return paint->colors[0];
    for (unsigned i = 1u; i < paint->count; ++i)
        if (t <= paint->positions[i])
        {
            const float span = paint->positions[i] - paint->positions[i - 1u];
            return ui_mix(paint->colors[i - 1u], paint->colors[i],
                          span > 1e-6f ? (t - paint->positions[i - 1u]) / span : 1.0f);
        }
    return paint->colors[paint->count - 1u];
}

/* Gradient color at t; large fills switch to a 256-step table. */
static uint32_t paint_at(PaintEval *eval, float t)
{
    if (!eval->has_lut)
    {
        if (++eval->samples < 512u) return paint_mix(eval->paint, t);
        for (unsigned i = 0u; i < 256u; ++i) eval->lut[i] = paint_mix(eval->paint, (float)i / 255.0f);
        eval->has_lut = 1;
    }
    const int index = t <= 0.0f ? 0 : t >= 1.0f ? 255 : (int)(t * 255.0f + 0.5f);
    return eval->lut[index];
}

void ui_fill_convex(UiCanvas *canvas, const UiPoint *points, int count, const UiPaint *paint,
                    float feather, float alpha)
{
    enum { MAX_EDGES = 160 };
    if (count < 3 || count > MAX_EDGES || alpha <= 0.0f) return;
    UiDrawCommand command = {.kind = UI_DRAW_CONVEX,
        .data.convex = {points, count, *paint, feather, alpha}};
    if (ui_canvas_emit(canvas, &command)) return;
    if (feather < 1.0f) feather = 1.0f;
    float area = 0.0f, top = points[0].y, bottom = points[0].y;
    for (int i = 0; i < count; ++i)
    {
        const UiPoint a = points[i], b = points[(i + 1) % count];
        area += a.x * b.y - b.x * a.y;
        if (a.y < top) top = a.y;
        if (a.y > bottom) bottom = a.y;
    }
    if (fabsf(area) < 1e-4f) return;
    const float orientation = area > 0.0f ? 1.0f : -1.0f;
    float nx[MAX_EDGES], ny[MAX_EDGES], offset[MAX_EDGES];
    int edges = 0;
    for (int i = 0; i < count; ++i)
    {
        const UiPoint a = points[i], b = points[(i + 1) % count];
        const float ex = b.x - a.x, ey = b.y - a.y, length = sqrtf(ex * ex + ey * ey);
        if (length < 1e-5f) continue;
        nx[edges] = orientation * ey / length;
        ny[edges] = -orientation * ex / length;
        offset[edges] = nx[edges] * a.x + ny[edges] * a.y;
        ++edges;
    }
    if (edges < 3) return;
    const float half = feather * 0.5f;
    const int y0 = clampi((int)floorf(top - half), canvas->clip_y0, canvas->clip_y1);
    const int y1 = clampi((int)ceilf(bottom + half) + 1, canvas->clip_y0, canvas->clip_y1);
    PaintEval eval;
    paint_prepare(&eval, paint);
    const int solid = paint->count <= 1u;
    const float inverse_feather = 1.0f / feather;
    const float alpha_scale = clampf(alpha, 0.0f, 1.0f);
    for (int y = y0; y < y1; ++y)
    {
        const float py = (float)y + 0.5f;
        float low = (float)canvas->clip_x0, high = (float)canvas->clip_x1;
        float full_low = low, full_high = high;
        int empty = 0, no_full = 0;
        float c[MAX_EDGES];
        for (int i = 0; i < edges && !empty; ++i)
        {
            c[i] = ny[i] * py - offset[i];
            if (fabsf(nx[i]) < 1e-6f)
            {
                if (c[i] >= half) empty = 1;
                if (c[i] > -half) no_full = 1;
                continue;
            }
            const float edge = (half - c[i]) / nx[i], inner = (-half - c[i]) / nx[i];
            if (nx[i] > 0.0f)
            {
                if (edge < high) high = edge;
                if (inner < full_high) full_high = inner;
            }
            else
            {
                if (edge > low) low = edge;
                if (inner > full_low) full_low = inner;
            }
        }
        if (empty || low >= high) continue;
        const int x0 = clampi((int)floorf(low - 0.5f), canvas->clip_x0, canvas->clip_x1);
        const int x1 = clampi((int)ceilf(high - 0.5f) + 1, canvas->clip_x0, canvas->clip_x1);
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;
        const int flat = solid || eval.tx == 0.0f;
        const uint32_t base = solid ? paint->colors[0] : flat ? paint_at(&eval, py * eval.ty - eval.origin) : 0u;
        const unsigned base_alpha = (unsigned)((float)(base >> 24) * alpha_scale + 0.5f);
        /* Pixels whose centers lie inside every edge's inner band are fully
         * covered; fill that run without per-pixel distances. */
        int inner0 = x1, inner1 = x1;
        if (!no_full && full_low <= full_high)
        {
            inner0 = clampi((int)ceilf(full_low - 0.5f), x0, x1);
            inner1 = clampi((int)floorf(full_high - 0.5f) + 1, inner0, x1);
        }
        if (flat && inner0 < inner1)
        {
            blend_span(canvas, row + inner0, inner1 - inner0, base, base_alpha);
        }
        for (int x = x0; x < x1; ++x)
        {
            if (x == inner0 && flat && inner0 < inner1)
            {
                x = inner1 - 1;
                continue;
            }
            const float px = (float)x + 0.5f;
            float coverage;
            if (x >= inner0 && x < inner1)
                coverage = 1.0f;
            else
            {
                float distance = -1e30f;
                for (int i = 0; i < edges; ++i)
                {
                    const float d = nx[i] * px + c[i];
                    if (d > distance) distance = d;
                }
                coverage = 0.5f - distance * inverse_feather;
                if (coverage <= 0.0f) continue;
                if (coverage > 1.0f) coverage = 1.0f;
            }
            if (flat)
            {
                blend(canvas, row + x, base, coverage >= 1.0f ? base_alpha
                                                      : (unsigned)((float)base_alpha * coverage + 0.5f));
                continue;
            }
            const uint32_t color = paint_at(&eval, px * eval.tx + py * eval.ty - eval.origin);
            blend(canvas, row + x, color, (unsigned)((float)(color >> 24) * alpha_scale * coverage + 0.5f));
        }
    }
}

/* Half-width of the region of a centered rounded box whose signed distance
 * is at most d, on a row qy = |dy| - (h/2 - r) from the box; -1 when empty. */
static float round_box_extent(float half_w, float radius, float qy, float d)
{
    const float rr = radius + d;
    if (qy > 0.0f)
    {
        if (rr <= qy) return -1.0f;
        return half_w - radius + sqrtf(rr * rr - qy * qy);
    }
    if (qy > d) return -1.0f;
    return half_w + d;
}

/* Rounded boxes (most panels, tabs and shadows) use their exact signed
 * distance rather than a polygon: wide soft shadows stay cheap. */
void ui_fill_round_rect(UiCanvas *canvas, float x, float y, float w, float h, float radius,
                        float skew, const UiPaint *paint, float feather, float alpha)
{
    if (w <= 0.0f || h <= 0.0f || alpha <= 0.0f) return;
    UiDrawCommand command = {.kind = UI_DRAW_ROUND_RECT,
        .data.box = {x, y, w, h, radius, skew, feather, alpha, 0.0f, *paint}};
    if (ui_canvas_emit(canvas, &command)) return;
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    if (radius < 0.0f) radius = 0.0f;
    if (feather < 1.0f) feather = 1.0f;
    const float half = feather * 0.5f, half_w = w * 0.5f, half_h = h * 0.5f;
    const float cx = x + half_w, cy = y + half_h;
    const int y0 = clampi((int)floorf(y - half), canvas->clip_y0, canvas->clip_y1);
    const int y1 = clampi((int)ceilf(y + h + half) + 1, canvas->clip_y0, canvas->clip_y1);
    PaintEval eval;
    paint_prepare(&eval, paint);
    const int solid = paint->count <= 1u;
    const int vertical = !solid && eval.tx == 0.0f;
    const float inverse_feather = 1.0f / feather;
    const float alpha_scale = clampf(alpha, 0.0f, 1.0f);
    for (int row_y = y0; row_y < y1; ++row_y)
    {
        const float py = (float)row_y + 0.5f;
        const float dy = fabsf(py - cy), qy = dy - (half_h - radius);
        const float outer = round_box_extent(half_w, radius, qy, half);
        if (outer < 0.0f) continue;
        const float inner = round_box_extent(half_w, radius, qy, -half);
        const float row_cx = cx + skew * (cy - py);
        const int x0 = clampi((int)floorf(row_cx - outer - 0.5f), canvas->clip_x0, canvas->clip_x1);
        const int x1 = clampi((int)ceilf(row_cx + outer - 0.5f) + 1, canvas->clip_x0, canvas->clip_x1);
        uint32_t *row = canvas->pixels + (size_t)row_y * (size_t)canvas->stride;
        uint32_t color = paint->colors[0];
        if (vertical) color = paint_at(&eval, py * eval.ty - eval.origin);
        const unsigned color_alpha = (unsigned)((float)(color >> 24) * alpha_scale + 0.5f);
        /* Fully covered middle of a flat-colored row: one run. */
        int inner0 = x1, inner1 = x1;
        if ((solid || vertical) && inner >= 0.0f)
        {
            inner0 = clampi((int)ceilf(row_cx - inner - 0.5f), x0, x1);
            inner1 = clampi((int)floorf(row_cx + inner - 0.5f) + 1, inner0, x1);
            blend_span(canvas, row + inner0, inner1 - inner0, color, color_alpha);
        }
        for (int px_i = x0; px_i < x1; ++px_i)
        {
            if (px_i == inner0 && inner0 < inner1)
            {
                px_i = inner1 - 1;
                continue;
            }
            const float px = (float)px_i + 0.5f;
            const float dx = fabsf(px - row_cx);
            float coverage = 1.0f;
            if (inner < 0.0f || dx > inner)
            {
                const float qx = dx - (half_w - radius);
                const float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
                const float in = qx > qy ? qx : qy;
                const float distance = sqrtf(ox * ox + oy * oy) + (in < 0.0f ? in : 0.0f) - radius;
                coverage = 0.5f - distance * inverse_feather;
                if (coverage <= 0.0f) continue;
                if (coverage > 1.0f) coverage = 1.0f;
            }
            if (solid || vertical)
            {
                blend(canvas, row + px_i, color, coverage >= 1.0f ? color_alpha
                                                          : (unsigned)((float)color_alpha * coverage + 0.5f));
                continue;
            }
            const uint32_t shade = paint_at(&eval, px * eval.tx + py * eval.ty - eval.origin);
            blend(canvas, row + px_i, shade, (unsigned)((float)(shade >> 24) * alpha_scale * coverage + 0.5f));
        }
    }
}

void ui_fill_chevron_bar(UiCanvas *canvas, float x, float y, float w, float h, float point,
                         const UiPaint *paint, float feather, float alpha)
{
    const UiPoint points[6] = {{x + point, y},         {x + w - point, y}, {x + w, y + h * 0.5f},
                               {x + w - point, y + h}, {x + point, y + h}, {x, y + h * 0.5f}};
    ui_fill_convex(canvas, points, 6, paint, feather, alpha);
}

/* Ellipses use an approximate signed distance (|p/r| scaled by the gradient)
 * so soft shadows cost a few operations per edge pixel, and the fully
 * covered middle of each row is one run. */
void ui_fill_ellipse(UiCanvas *canvas, float cx, float cy, float rx, float ry,
                     const UiPaint *paint, float feather, float alpha)
{
    if (rx <= 0.0f || ry <= 0.0f || alpha <= 0.0f) return;
    UiDrawCommand command = {.kind = UI_DRAW_ELLIPSE,
        .data.ellipse = {cx, cy, rx, ry, feather, alpha, 0.0f, *paint}};
    if (ui_canvas_emit(canvas, &command)) return;
    if (feather < 1.0f) feather = 1.0f;
    const float half = feather * 0.5f;
    const float outer_x = rx + half + 1.0f, outer_y = ry + half + 1.0f;
    const float inner_x = rx - half - 1.0f, inner_y = ry - half - 1.0f;
    const int y0 = clampi((int)floorf(cy - outer_y), canvas->clip_y0, canvas->clip_y1);
    const int y1 = clampi((int)ceilf(cy + outer_y) + 1, canvas->clip_y0, canvas->clip_y1);
    PaintEval eval;
    paint_prepare(&eval, paint);
    const int solid = paint->count <= 1u;
    const float alpha_scale = clampf(alpha, 0.0f, 1.0f), inverse_feather = 1.0f / feather;
    const float irx = 1.0f / rx, iry = 1.0f / ry, irx2 = irx * irx, iry2 = iry * iry;
    for (int y = y0; y < y1; ++y)
    {
        const float py = (float)y + 0.5f, dy = py - cy;
        const float oy = dy / outer_y;
        if (oy * oy >= 1.0f) continue;
        const float span = outer_x * sqrtf(1.0f - oy * oy);
        const int x0 = clampi((int)floorf(cx - span), canvas->clip_x0, canvas->clip_x1);
        const int x1 = clampi((int)ceilf(cx + span) + 1, canvas->clip_x0, canvas->clip_x1);
        int inner0 = x1, inner1 = x1;
        if (inner_x > 0.0f && inner_y > 0.0f && fabsf(dy) < inner_y)
        {
            const float iy = dy / inner_y, inner = inner_x * sqrtf(1.0f - iy * iy);
            inner0 = clampi((int)ceilf(cx - inner - 0.5f), x0, x1);
            inner1 = clampi((int)floorf(cx + inner - 0.5f) + 1, inner0, x1);
        }
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;
        const int flat = solid || eval.tx == 0.0f;
        const uint32_t base = solid ? paint->colors[0] : flat ? paint_at(&eval, py * eval.ty - eval.origin) : 0u;
        const unsigned base_alpha = (unsigned)((float)(base >> 24) * alpha_scale + 0.5f);
        if (flat && inner0 < inner1) blend_span(canvas, row + inner0, inner1 - inner0, base, base_alpha);
        const float qy = dy * iry, qy2 = dy * iry2;
        for (int x = x0; x < x1; ++x)
        {
            if (x == inner0 && inner0 < inner1)
            {
                if (flat)
                {
                    x = inner1 - 1;
                    continue;
                }
            }
            const float px = (float)x + 0.5f, dx = px - cx;
            float coverage = 1.0f;
            if (x < inner0 || x >= inner1)
            {
                const float qx = dx * irx, qx2 = dx * irx2;
                const float k0 = sqrtf(qx * qx + qy * qy), k1 = sqrtf(qx2 * qx2 + qy2 * qy2);
                const float distance = k1 > 1e-6f ? k0 * (k0 - 1.0f) / k1 : -fminf(rx, ry);
                coverage = 0.5f - distance * inverse_feather;
                if (coverage <= 0.0f) continue;
                if (coverage > 1.0f) coverage = 1.0f;
            }
            if (flat)
            {
                blend(canvas, row + x, base, coverage >= 1.0f ? base_alpha
                                                      : (unsigned)((float)base_alpha * coverage + 0.5f));
                continue;
            }
            const uint32_t color = paint_at(&eval, px * eval.tx + py * eval.ty - eval.origin);
            blend(canvas, row + x, color, (unsigned)((float)(color >> 24) * alpha_scale * coverage + 0.5f));
        }
    }
}

void ui_fill_circle(UiCanvas *canvas, float cx, float cy, float radius, const UiPaint *paint,
                    float feather, float alpha)
{
    ui_fill_round_rect(canvas, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, radius, 0.0f, paint,
                       feather, alpha);
}

void ui_stroke_circle(UiCanvas *canvas, float cx, float cy, float radius, float width,
                      uint32_t color, float alpha)
{
    UiDrawCommand command = {.kind = UI_DRAW_STROKE_CIRCLE,
        .data.ellipse = {cx, cy, radius, radius, 1.0f, alpha, width, ui_paint_solid(color)}};
    if (ui_canvas_emit(canvas, &command)) return;
    const float outer = radius + 1.0f;
    const int x0 = clampi((int)floorf(cx - outer), canvas->clip_x0, canvas->clip_x1);
    const int x1 = clampi((int)ceilf(cx + outer) + 1, canvas->clip_x0, canvas->clip_x1);
    const int y0 = clampi((int)floorf(cy - outer), canvas->clip_y0, canvas->clip_y1);
    const int y1 = clampi((int)ceilf(cy + outer) + 1, canvas->clip_y0, canvas->clip_y1);
    const float middle = radius - width * 0.5f, half = width * 0.5f;
    const float a = (float)(color >> 24) * clampf(alpha, 0.0f, 1.0f);
    for (int y = y0; y < y1; ++y)
    {
        uint32_t *row = canvas->pixels + (size_t)y * (size_t)canvas->stride;
        const float dy = (float)y + 0.5f - cy;
        for (int x = x0; x < x1; ++x)
        {
            const float dx = (float)x + 0.5f - cx;
            const float distance = fabsf(sqrtf(dx * dx + dy * dy) - middle) - half;
            const float coverage = clampf(0.5f - distance, 0.0f, 1.0f);
            if (coverage > 0.0f) blend(canvas, row + x, color, (unsigned)(a * coverage + 0.5f));
        }
    }
}

/* Signed distance to an axis-aligned rounded rectangle centered at origin. */
static float round_rect_distance(float px, float py, float hw, float hh, float radius)
{
    const float qx = fabsf(px) - hw + radius, qy = fabsf(py) - hh + radius;
    const float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
    const float inside = (qx > qy ? qx : qy) < 0.0f ? (qx > qy ? qx : qy) : 0.0f;
    return sqrtf(ox * ox + oy * oy) + inside - radius;
}

void ui_stroke_round_rect(UiCanvas *canvas, float x, float y, float w, float h, float radius,
                          float skew, float width, uint32_t color, float alpha)
{
    if (w <= 0.0f || h <= 0.0f || width <= 0.0f) return;
    UiDrawCommand command = {.kind = UI_DRAW_STROKE_ROUND_RECT,
        .data.box = {x, y, w, h, radius, skew, 1.0f, alpha, width, ui_paint_solid(color)}};
    if (ui_canvas_emit(canvas, &command)) return;
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    const float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
    const float lean = fabsf(skew) * hh;
    const int x0 = clampi((int)floorf(x - lean - 1.0f), canvas->clip_x0, canvas->clip_x1);
    const int x1 = clampi((int)ceilf(x + w + lean + 1.0f) + 1, canvas->clip_x0, canvas->clip_x1);
    const int y0 = clampi((int)floorf(y - 1.0f), canvas->clip_y0, canvas->clip_y1);
    const int y1 = clampi((int)ceilf(y + h + 1.0f) + 1, canvas->clip_y0, canvas->clip_y1);
    const float a = (float)(color >> 24) * clampf(alpha, 0.0f, 1.0f);
    const float band = width + radius + 2.0f;
    for (int yy = y0; yy < y1; ++yy)
    {
        uint32_t *row = canvas->pixels + (size_t)yy * (size_t)canvas->stride;
        const float py = (float)yy + 0.5f - cy;
        const float shift = skew * (-py);
        const int interior = fabsf(py) < hh - band;
        for (int xx = x0; xx < x1; ++xx)
        {
            const float px = (float)xx + 0.5f - cx - shift;
            if (interior && fabsf(px) < hw - band)
            {
                const int skip = (int)(cx + shift + hw - band) - 1;
                if (skip > xx) xx = skip;
                continue;
            }
            const float d = round_rect_distance(px, py, hw, hh, radius);
            const float coverage = clampf(0.5f - d, 0.0f, 1.0f) * clampf(0.5f + d + width, 0.0f, 1.0f);
            if (coverage > 0.0f) blend(canvas, row + xx, color, (unsigned)(a * coverage + 0.5f));
        }
    }
}

void ui_fill_rect(UiCanvas *canvas, float x, float y, float w, float h, uint32_t color, float alpha)
{
    UiDrawCommand command = {.kind = UI_DRAW_RECT, .data.rect = {x, y, w, h, alpha, color}};
    if (ui_canvas_emit(canvas, &command)) return;
    const int x0 = clampi((int)floorf(x + 0.5f), canvas->clip_x0, canvas->clip_x1);
    const int x1 = clampi((int)floorf(x + w + 0.5f), canvas->clip_x0, canvas->clip_x1);
    const int y0 = clampi((int)floorf(y + 0.5f), canvas->clip_y0, canvas->clip_y1);
    const int y1 = clampi((int)floorf(y + h + 0.5f), canvas->clip_y0, canvas->clip_y1);
    const unsigned a = (unsigned)((float)(color >> 24) * clampf(alpha, 0.0f, 1.0f) + 0.5f);
    for (int yy = y0; yy < y1; ++yy)
    {
        uint32_t *row = canvas->pixels + (size_t)yy * (size_t)canvas->stride;
        for (int xx = x0; xx < x1; ++xx) blend(canvas, row + xx, color, a);
    }
}

/* Premultiplied bilinear sample; out channels are 0..255 floats. */
static void sample_bilinear(const UiImage *image, float u, float v, float out[4])
{
    const float fx = u - 0.5f, fy = v - 0.5f;
    int ix = (int)floorf(fx), iy = (int)floorf(fy);
    const float ax = fx - (float)ix, ay = fy - (float)iy;
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    for (int k = 0; k < 4; ++k)
    {
        const int sx = clampi(ix + (k & 1), 0, image->width - 1);
        const int sy = clampi(iy + (k >> 1), 0, image->height - 1);
        const float weight = ((k & 1) ? ax : 1.0f - ax) * ((k >> 1) ? ay : 1.0f - ay);
        if (weight <= 0.0f) continue;
        const uint32_t p = image->pixels[(size_t)sy * (size_t)image->width + (size_t)sx];
        const float alpha = (float)(p >> 24) * weight;
        out[0] += alpha;
        out[1] += (float)((p >> 16) & 255u) * alpha;
        out[2] += (float)((p >> 8) & 255u) * alpha;
        out[3] += (float)(p & 255u) * alpha;
    }
}

void ui_draw_image_region(UiCanvas *canvas, const UiImage *image, float sx, float sy, float sw,
                          float sh, float x, float y, float w, float h, float alpha,
                          unsigned flags, uint32_t tint, float radius)
{
    if (image == NULL || image->pixels == NULL || image->width <= 0 || image->height <= 0 ||
        w <= 0.0f || h <= 0.0f || sw <= 0.0f || sh <= 0.0f || alpha <= 0.0f)
        return;
    UiDrawCommand command = {.kind = UI_DRAW_IMAGE,
        .data.image = {image, sx, sy, sw, sh, x, y, w, h, alpha, radius, flags, tint}};
    if (ui_canvas_emit(canvas, &command)) return;
    const int x0 = clampi((int)floorf(x), canvas->clip_x0, canvas->clip_x1);
    const int x1 = clampi((int)ceilf(x + w), canvas->clip_x0, canvas->clip_x1);
    const int y0 = clampi((int)floorf(y), canvas->clip_y0, canvas->clip_y1);
    const int y1 = clampi((int)ceilf(y + h), canvas->clip_y0, canvas->clip_y1);
    const float step_x = sw / w, step_y = sh / h;
    const float tr = (float)((tint >> 16) & 255u) / 255.0f, tg = (float)((tint >> 8) & 255u) / 255.0f,
                tb = (float)(tint & 255u) / 255.0f, ta = (float)(tint >> 24) / 255.0f;
    const float hw = w * 0.5f, hh = h * 0.5f;
    if (radius > hw) radius = hw;
    if (radius > hh) radius = hh;
    for (int yy = y0; yy < y1; ++yy)
    {
        uint32_t *row = canvas->pixels + (size_t)yy * (size_t)canvas->stride;
        const float py = (float)yy + 0.5f;
        const float row_cover = clampf(py - y + 0.5f, 0.0f, 1.0f) * clampf(y + h - py + 0.5f, 0.0f, 1.0f);
        if (row_cover <= 0.0f) continue;
        const float v = sy + (py - y) * step_y;
        for (int xx = x0; xx < x1; ++xx)
        {
            const float px = (float)xx + 0.5f;
            float coverage = row_cover * clampf(px - x + 0.5f, 0.0f, 1.0f) * clampf(x + w - px + 0.5f, 0.0f, 1.0f);
            if (radius > 0.0f)
                coverage *= clampf(0.5f - round_rect_distance(px - x - hw, py - y - hh, hw, hh, radius), 0.0f, 1.0f);
            if (coverage <= 0.0f) continue;
            const float u = sx + (px - x) * step_x;
            float s[4];
            if (flags & UI_IMAGE_NEAREST)
            {
                const int ix = clampi((int)floorf(u), 0, image->width - 1);
                const int iy = clampi((int)floorf(v), 0, image->height - 1);
                const uint32_t p = image->pixels[(size_t)iy * (size_t)image->width + (size_t)ix];
                s[0] = (float)(p >> 24);
                s[1] = (float)((p >> 16) & 255u) * s[0];
                s[2] = (float)((p >> 8) & 255u) * s[0];
                s[3] = (float)(p & 255u) * s[0];
            }
            else
                sample_bilinear(image, u, v, s);
            if (s[0] <= 0.5f) continue;
            float r = s[1] / s[0], g = s[2] / s[0], b = s[3] / s[0];
            if (flags & UI_IMAGE_SILHOUETTE)
            {
                r = tr * 255.0f;
                g = tg * 255.0f;
                b = tb * 255.0f;
            }
            else
            {
                if (flags & UI_IMAGE_GRAY)
                {
                    const float luma = (0.30f * r + 0.59f * g + 0.11f * b) * 0.55f + 30.0f;
                    r = g = b = luma;
                }
                r *= tr;
                g *= tg;
                b *= tb;
            }
            const float a = s[0] * alpha * ta * coverage;
            const uint32_t color = 0xff000000u | ((uint32_t)clampf(r, 0.0f, 255.0f) << 16) |
                                   ((uint32_t)clampf(g, 0.0f, 255.0f) << 8) |
                                   (uint32_t)clampf(b, 0.0f, 255.0f);
            blend(canvas, row + xx, color, (unsigned)clampf(a + 0.5f, 0.0f, 255.0f));
        }
    }
}

void ui_draw_image(UiCanvas *canvas, const UiImage *image, float x, float y, float w, float h,
                   float alpha, unsigned flags, uint32_t tint, float radius)
{
    if (image == NULL) return;
    ui_draw_image_region(canvas, image, 0.0f, 0.0f, (float)image->width, (float)image->height, x, y,
                         w, h, alpha, flags, tint, radius);
}
