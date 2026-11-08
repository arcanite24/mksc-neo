#include "ui_font.h"
#include "ui_draw.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "stb_truetype.h"

extern const uint8_t portable_embed_NunitoBlack_ttf[];
extern const uint8_t portable_embed_NunitoExtraBold_ttf[];
extern const uint8_t portable_embed_NunitoBlackItalic_ttf[];

/* Glyph distance fields are rasterized once at BASE pixels. One field unit
 * of distance is DISTANCE_SCALE/PADDING base pixels; the padding bounds how
 * far outlines and shadows can reach (PADDING scaled by size/BASE). */
enum
{
    BASE = 64,
    PADDING = 12,
    ON_EDGE = 128,
    EXTRA_GLYPHS = 12,
    GLYPH_SLOTS = 95 + EXTRA_GLYPHS
};
static const float DISTANCE_SCALE = (float)ON_EDGE / (float)PADDING;
static const int extra_codepoints[EXTRA_GLYPHS] = {0x2022, 0x2190, 0x2191, 0x2192, 0x2193, 0xd7,
                                                   0xb7,   0x2026, 0x2013, 0x2014, 0x2019, 0xe9};

typedef struct
{
    unsigned char *field;
    int width, height, x_offset, y_offset;
    float advance; /* Base pixels. */
    uint8_t loaded;
} Glyph;

typedef struct
{
    stbtt_fontinfo info;
    float base_scale;
    float ascent; /* Base pixels above the baseline. */
    float cap_height;
    Glyph glyphs[GLYPH_SLOTS];
} Face;

static Face faces[UI_FONT_COUNT];
static int font_state; /* 0 untried, 1 ready, -1 failed */

static int ensure_fonts(void)
{
    if (font_state != 0) return font_state > 0;
    const uint8_t *data[UI_FONT_COUNT] = {portable_embed_NunitoBlack_ttf, portable_embed_NunitoExtraBold_ttf,
                                          portable_embed_NunitoBlackItalic_ttf};
    font_state = 1;
    for (int face = 0; face < UI_FONT_COUNT; ++face)
    {
        Face *f = &faces[face];
        if (!stbtt_InitFont(&f->info, data[face], stbtt_GetFontOffsetForIndex(data[face], 0)))
        {
            font_state = -1;
            return 0;
        }
        f->base_scale = stbtt_ScaleForPixelHeight(&f->info, (float)BASE);
        int ascent, descent, gap;
        stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &gap);
        f->ascent = (float)ascent * f->base_scale;
        int x0, y0, x1, y1;
        f->cap_height = stbtt_GetCodepointBox(&f->info, 'H', &x0, &y0, &x1, &y1)
                            ? (float)y1 * f->base_scale
                            : f->ascent * 0.7f;
    }
    return 1;
}

int ui_font_ready(void)
{
    return ensure_fonts();
}

void ui_font_release(void)
{
    for (int face = 0; face < UI_FONT_COUNT; ++face)
        for (int slot = 0; slot < GLYPH_SLOTS; ++slot)
        {
            Glyph *glyph = &faces[face].glyphs[slot];
            if (glyph->field != NULL) stbtt_FreeSDF(glyph->field, NULL);
            memset(glyph, 0, sizeof *glyph);
        }
}

static int glyph_slot(int codepoint)
{
    if (codepoint >= 32 && codepoint < 127) return codepoint - 32;
    for (int i = 0; i < EXTRA_GLYPHS; ++i)
        if (extra_codepoints[i] == codepoint) return 95 + i;
    return '?' - 32;
}
static int slot_codepoint(int slot)
{
    return slot < 95 ? slot + 32 : extra_codepoints[slot - 95];
}

static const Glyph *glyph_get(UiFontFace face, int slot)
{
    Face *f = &faces[face];
    Glyph *glyph = &f->glyphs[slot];
    if (glyph->loaded) return glyph;
    const int codepoint = slot_codepoint(slot);
    int advance, bearing;
    stbtt_GetCodepointHMetrics(&f->info, codepoint, &advance, &bearing);
    glyph->advance = (float)advance * f->base_scale;
    glyph->field = stbtt_GetCodepointSDF(&f->info, f->base_scale, codepoint, PADDING, ON_EDGE,
                                         DISTANCE_SCALE, &glyph->width, &glyph->height,
                                         &glyph->x_offset, &glyph->y_offset);
    glyph->loaded = 1u;
    return glyph;
}

/* UTF-8 decoder; invalid bytes decode as '?'. */
static int next_codepoint(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    int codepoint = '?', length = 1;
    if (s[0] < 0x80u)
        codepoint = s[0];
    else if ((s[0] & 0xe0u) == 0xc0u && (s[1] & 0xc0u) == 0x80u)
    {
        codepoint = ((s[0] & 0x1f) << 6) | (s[1] & 0x3f);
        length = 2;
    }
    else if ((s[0] & 0xf0u) == 0xe0u && (s[1] & 0xc0u) == 0x80u && (s[2] & 0xc0u) == 0x80u)
    {
        codepoint = ((s[0] & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        length = 3;
    }
    *text += length;
    return codepoint;
}

UiTextStyle ui_text_style(UiFontFace face, float size, uint32_t color)
{
    UiTextStyle style;
    memset(&style, 0, sizeof style);
    style.face = face;
    style.size = size;
    style.color = color;
    return style;
}

float ui_text_width(const UiTextStyle *style, const char *text)
{
    if (!ensure_fonts() || text == NULL) return 0.0f;
    Face *f = &faces[style->face];
    const float k = style->size / (float)BASE;
    float width = 0.0f;
    int previous = 0;
    while (*text != '\0')
    {
        const int codepoint = next_codepoint(&text);
        const int slot = glyph_slot(codepoint);
        if (previous)
            width += (float)stbtt_GetCodepointKernAdvance(&f->info, previous, slot_codepoint(slot)) *
                     f->base_scale * k;
        width += glyph_get(style->face, slot)->advance * k + style->tracking;
        previous = slot_codepoint(slot);
    }
    return width > style->tracking ? width - style->tracking : width;
}

float ui_text_cap_height(const UiTextStyle *style)
{
    if (!ensure_fonts()) return style->size * 0.7f;
    return faces[style->face].cap_height * style->size / (float)BASE;
}

static float sample_field(const Glyph *glyph, float u, float v)
{
    const float fx = u - 0.5f, fy = v - 0.5f;
    const int ix = (int)floorf(fx), iy = (int)floorf(fy);
    const float ax = fx - (float)ix, ay = fy - (float)iy;
    float value = 0.0f;
    for (int k = 0; k < 4; ++k)
    {
        const int x = ix + (k & 1), y = iy + (k >> 1);
        const float weight = ((k & 1) ? ax : 1.0f - ax) * ((k >> 1) ? ay : 1.0f - ay);
        if (x >= 0 && y >= 0 && x < glyph->width && y < glyph->height)
            value += weight * (float)glyph->field[y * glyph->width + x];
    }
    return value;
}

enum { PASS_SHADOW, PASS_OUTLINE, PASS_FILL };

static void draw_pass(UiCanvas *canvas, const UiTextStyle *style, float x, float y,
                      const char *text, float alpha, int pass)
{
    Face *f = &faces[style->face];
    const float k = style->size / (float)BASE;
    const float reach = (float)PADDING * k - 0.5f;
    float outline = style->outline > reach ? reach : style->outline;
    uint32_t color = style->color;
    float softness = 1.0f;
    if (pass == PASS_SHADOW)
    {
        color = style->shadow_color;
        x += style->shadow_x;
        y += style->shadow_y;
        softness = style->shadow_blur > 1.0f ? style->shadow_blur : 1.0f;
        if (outline + softness * 0.5f > reach) softness = 2.0f * (reach - outline);
        if (softness < 1.0f) softness = 1.0f;
    }
    else if (pass == PASS_OUTLINE)
        color = style->outline_color;
    else
        outline = 0.0f;
    const float color_alpha = (float)(color >> 24) * alpha;
    if (color_alpha <= 0.0f) return;
    const uint32_t opaque = color | 0xff000000u;
    float pen = x;
    int previous = 0;
    while (*text != '\0')
    {
        const int codepoint = next_codepoint(&text);
        const int slot = glyph_slot(codepoint);
        const Glyph *glyph = glyph_get(style->face, slot);
        if (previous)
            pen += (float)stbtt_GetCodepointKernAdvance(&f->info, previous, slot_codepoint(slot)) *
                   f->base_scale * k;
        previous = slot_codepoint(slot);
        if (glyph->field != NULL)
        {
            const float gx = pen + (float)glyph->x_offset * k, gy = y + (float)glyph->y_offset * k;
            UiDrawCommand command = {.kind = UI_DRAW_GLYPH,
                                     .data.glyph = {.field = glyph->field,
                                                    .width = glyph->width,
                                                    .height = glyph->height,
                                                    .face = (int)style->face,
                                                    .slot = slot,
                                                    .x = gx,
                                                    .y = gy,
                                                    .scale = k,
                                                    .outline = outline,
                                                    .softness = softness,
                                                    .alpha = alpha,
                                                    .color = color}};
            if (!ui_canvas_emit(canvas, &command))
            {
                int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
                int x1 = (int)ceilf(gx + (float)glyph->width * k),
                    y1 = (int)ceilf(gy + (float)glyph->height * k);
                if (x0 < canvas->clip_x0)
                    x0 = canvas->clip_x0;
                if (y0 < canvas->clip_y0)
                    y0 = canvas->clip_y0;
                if (x1 > canvas->clip_x1)
                    x1 = canvas->clip_x1;
                if (y1 > canvas->clip_y1)
                    y1 = canvas->clip_y1;
                const float inverse = 1.0f / k;
                for (int py = y0; py < y1; ++py)
                {
                    uint32_t *row = canvas->pixels + (size_t)py * (size_t)canvas->stride;
                    const float v = ((float)py + 0.5f - gy) * inverse;
                    for (int px = x0; px < x1; ++px)
                    {
                        const float u = ((float)px + 0.5f - gx) * inverse;
                        const float value = sample_field(glyph, u, v);
                        if (value <= 0.0f)
                            continue;
                        const float distance = (value - (float)ON_EDGE) / DISTANCE_SCALE * k;
                        float coverage = (distance + outline) / softness + 0.5f;
                        if (coverage <= 0.0f)
                            continue;
                        if (coverage > 1.0f)
                            coverage = 1.0f;
                        const unsigned a = (unsigned)(color_alpha * coverage + 0.5f);
                        if (a == 0u)
                            continue;
                        uint32_t *target = row + px;
                        if (a >= 255u)
                        {
                            *target = opaque;
                            continue;
                        }
                        const uint32_t old = *target;
                        const unsigned inv = 255u - a;
                        const unsigned r =
                            (((opaque >> 16) & 255u) * a + ((old >> 16) & 255u) * inv + 127u) /
                            255u;
                        const unsigned g =
                            (((opaque >> 8) & 255u) * a + ((old >> 8) & 255u) * inv + 127u) / 255u;
                        const unsigned b = ((opaque & 255u) * a + (old & 255u) * inv + 127u) / 255u;
                        const unsigned out_alpha = canvas->premultiplied_alpha
                                                       ? a + ((old >> 24) * inv + 127u) / 255u
                                                       : 255u;
                        *target = (out_alpha << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
        pen += glyph->advance * k + style->tracking;
    }
}

void ui_text_draw(UiCanvas *canvas, const UiTextStyle *style, float x, float y, UiAlign align,
                  const char *text, float alpha)
{
    if (!ensure_fonts() || text == NULL || text[0] == '\0' || alpha <= 0.0f || style->size < 2.0f)
        return;
    if (align != UI_ALIGN_LEFT)
    {
        const float width = ui_text_width(style, text);
        x -= align == UI_ALIGN_CENTER ? width * 0.5f : width;
    }
    if (alpha > 1.0f) alpha = 1.0f;
    if ((style->shadow_color >> 24) != 0u) draw_pass(canvas, style, x, y, text, alpha, PASS_SHADOW);
    if (style->outline > 0.0f && (style->outline_color >> 24) != 0u)
        draw_pass(canvas, style, x, y, text, alpha, PASS_OUTLINE);
    draw_pass(canvas, style, x, y, text, alpha, PASS_FILL);
}

void ui_text_draw_centered(UiCanvas *canvas, const UiTextStyle *style, float x, float y,
                           UiAlign align, const char *text, float alpha)
{
    ui_text_draw(canvas, style, x, y + ui_text_cap_height(style) * 0.5f, align, text, alpha);
}

UiTextStyle ui_text_fit(const UiTextStyle *style, const char *text, float max_width)
{
    UiTextStyle fitted = *style;
    const float width = ui_text_width(style, text);
    if (width > max_width && width > 0.0f)
    {
        const float ratio = max_width / width;
        fitted.size *= ratio;
        fitted.outline *= ratio;
        fitted.tracking *= ratio;
    }
    return fitted;
}

int ui_text_draw_wrapped(UiCanvas *canvas, const UiTextStyle *style, float x, float y,
                         float max_width, float line_height, int max_lines, UiAlign align,
                         const char *text, float alpha)
{
    if (text == NULL) return 0;
    char line[256];
    int lines = 0;
    while (*text != '\0' && lines < max_lines)
    {
        while (*text == ' ') ++text;
        size_t fit = 0u, length = 0u;
        for (;;)
        {
            size_t next = length;
            while (text[next] == ' ') ++next;
            while (text[next] != '\0' && text[next] != ' ' && text[next] != '\n') ++next;
            if (next >= sizeof line) break;
            memcpy(line, text, next);
            line[next] = '\0';
            if (fit != 0u && ui_text_width(style, line) > max_width) break;
            fit = next;
            length = next;
            if (text[next] == '\0' || text[next] == '\n') break;
        }
        if (fit == 0u) break;
        memcpy(line, text, fit);
        line[fit] = '\0';
        ui_text_draw(canvas, style, x, y + (float)lines * line_height, align, line, alpha);
        ++lines;
        text += fit;
        if (*text == '\n') ++text;
    }
    return lines;
}
