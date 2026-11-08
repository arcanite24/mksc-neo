#include "modern_menu_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame_scheduler.h"
#include "model_asset.h"

#define MM_TICK_RATE 59.7275f

ModernMenu *modern_menu_create(void)
{
    ModernMenu *menu = calloc(1u, sizeof *menu);
    if (menu == NULL) return NULL;
    menu->art = ui_art_create();
    if (menu->art == NULL)
    {
        free(menu);
        return NULL;
    }
    return menu;
}

void modern_menu_destroy(ModernMenu *menu)
{
    if (menu == NULL) return;
    ui_art_destroy(menu->art);
    portable_menu_driver_model_destroy(menu->driver_model);
    free(menu->field);
    free(menu);
}

void modern_menu_content_changed(ModernMenu *menu)
{
    if (menu == NULL) return;
    ui_art_clear(menu->art);
    portable_menu_driver_model_forget(menu->driver_model);
}

PortableMenuDriverModel *modern_menu_driver_model(ModernMenu *menu)
{
    if (menu != NULL && menu->driver_model == NULL) menu->driver_model = portable_menu_driver_model_create();
    return menu != NULL ? menu->driver_model : NULL;
}

float mm_clamp01(float value)
{
    return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

float mm_ease_out(float value)
{
    value = 1.0f - mm_clamp01(value);
    return 1.0f - value * value * value;
}

float mm_ease_back(float value)
{
    value = mm_clamp01(value);
    const float c1 = 1.70158f, c3 = c1 + 1.0f, v = value - 1.0f;
    return 1.0f + c3 * v * v * v + c1 * v * v;
}

float mm_stagger(const MmCtx *x, float delay_ticks, float duration_ticks)
{
    return mm_clamp01(((float)x->screen_ticks - delay_ticks) / duration_ticks);
}

float mm_follow(MmCtx *x, unsigned slot, uint32_t key, float target, float rate)
{
    ModernMenu *m = x->menu;
    if (slot >= MM_SMOOTH_SLOTS) return target;
    if (m->smooth_key[slot] != key || x->elapsed > 8u)
    {
        m->smooth_key[slot] = key;
        m->smooth_value[slot] = target;
        return target;
    }
    float value = m->smooth_value[slot];
    for (uint32_t i = 0u; i < x->elapsed; ++i) value += (target - value) * rate;
    if (fabsf(target - value) < 0.05f) value = target;
    m->smooth_value[slot] = value;
    return value;
}

void mm_format_ticks(uint32_t ticks, char out[16])
{
    uint64_t cs = (uint64_t)ticks * PORTABLE_FRAME_TICK_NUMERATOR * 100u / PORTABLE_FRAME_TICK_DENOMINATOR;
    if (cs > 59999u) cs = 59999u;
    snprintf(out, 16, "%u'%02u\"%02u", (unsigned)(cs / 6000u), (unsigned)(cs / 100u % 60u),
             (unsigned)(cs % 100u));
}

const UiImage *mm_art(MmCtx *x, uint32_t id)
{
    return ui_art_image(x->menu->art, x->view->pack, id, 1);
}

int mm_image(MmCtx *x, uint32_t id, float px, float py, float pw, float ph, float alpha,
             unsigned flags, float radius)
{
    const UiImage *image = mm_art(x, id);
    if (image == NULL) return 0;
    ui_draw_image(x->c, image, px, py, pw, ph, alpha, flags, MM_WHITE, radius);
    return 1;
}

int mm_image_fit(MmCtx *x, uint32_t id, float cx, float cy, float max_w, float max_h, float alpha,
                 unsigned flags)
{
    const UiImage *image = mm_art(x, id);
    if (image == NULL) return 0;
    float scale = max_w / (float)image->width;
    if ((float)image->height * scale > max_h) scale = max_h / (float)image->height;
    const float w = (float)image->width * scale, h = (float)image->height * scale;
    ui_draw_image(x->c, image, cx - w * 0.5f, cy - h * 0.5f, w, h, alpha, flags, MM_WHITE, 0.0f);
    return 1;
}

int mm_portrait_3d(MmCtx *x, uint32_t driver_id, UiImage *out)
{
    PortableDriver3D row;
    PortableModelMaterial material;
    if (x->view->pack_3d == NULL || !portable_driver_3d_find(x->view->pack_3d, driver_id, &row) ||
        !portable_model_material_open(x->view->pack_3d, row.portrait, &material) ||
        material.texture.pixels == NULL || material.texture.palette != NULL)
        return 0;
    *out = (UiImage){(const uint32_t *)(const void *)material.texture.pixels, material.texture.width,
                     material.texture.height};
    return 1;
}

void mm_title_text(MmCtx *x, const UiTextStyle *style, float px, float py, UiAlign align,
                   const char *text, float alpha)
{
    if (text != NULL) ui_text_draw_centered(x->c, style, px, py, align, text, alpha);
}

/* ---- Background ---------------------------------------------------------- */

static uint32_t hash32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

/* Draws the field into a w x h buffer at scale u (pixels per design unit). */
static void checker_pixels(uint32_t *pixels, int width, int height, float u, float t)
{
    const UiCanvas field = {.pixels = pixels, .width = width, .height = height, .stride = width,
        .clip_x1 = width, .clip_y1 = height};
    const UiCanvas *c = &field;
    const float cell = 168.0f * u;
    const float angle = -0.33f, ca = cosf(angle), sa = sinf(angle);
    const float scroll = t * 26.0f * u;
    const float inverse = 1.0f / cell;
    const float light_x = (float)c->width * 0.18f, light_y = (float)c->height * -0.1f;
    const float light_r = (float)(c->width > c->height ? c->width : c->height) * 1.1f;
    for (int y = 0; y < c->height; ++y)
    {
        const float fy = (float)y / (float)(c->height > 1 ? c->height - 1 : 1);
        /* Row gradient: bright cyan at the top to deep blue at the bottom. */
        const float base_r = 52.0f + (10.0f - 52.0f) * fy, base_g = 190.0f + (104.0f - 190.0f) * fy,
                    base_b = 246.0f + (206.0f - 246.0f) * fy;
        uint32_t *row = c->pixels + (size_t)y * (size_t)c->stride;
        const float py = (float)y + 0.5f;
        float uu = (0.5f * ca + py * sa + scroll) * inverse + 1000.0f;
        float vv = (-0.5f * sa + py * ca + scroll * 0.35f) * inverse + 1000.0f;
        const float du = ca * inverse, dv = -sa * inverse;
        const float ly = py - light_y;
        for (int px = 0; px < c->width; ++px, uu += du, vv += dv)
        {
            const int iu = (int)uu, iv = (int)vv;
            const float fu = uu - (float)iu, fv = vv - (float)iv;
            const float eu = (fu < 0.5f ? fu : 1.0f - fu) * cell, ev = (fv < 0.5f ? fv : 1.0f - fv) * cell;
            float tone = ((iu + iv) & 1) ? 1.0f : 0.0f;
            const float edge = eu < ev ? eu : ev;
            if (edge < 1.0f) tone = 0.5f + (tone - 0.5f) * edge;
            const float lx = (float)px - light_x;
            float light = 1.0f - (lx * lx + ly * ly) / (light_r * light_r);
            if (light < 0.0f) light = 0.0f;
            const float lift = tone * 14.0f + light * 34.0f;
            const float r = base_r + lift, g = base_g + lift * 0.8f, b = base_b + lift * 0.4f;
            const unsigned ri = r > 255.0f ? 255u : (unsigned)r, gi = g > 255.0f ? 255u : (unsigned)g,
                           bi = b > 255.0f ? 255u : (unsigned)b;
            row[px] = 0xff000000u | (ri << 16) | (gi << 8) | bi;
        }
    }
}

/* Packed 0x00RR00BB / 0x0000GG00 averaging: (3a + b) / 4 per channel. */
static uint32_t blend31(uint32_t a, uint32_t b)
{
    const uint32_t rb = (((a & 0x00ff00ffu) * 3u + (b & 0x00ff00ffu)) >> 2) & 0x00ff00ffu;
    const uint32_t g = (((a & 0x0000ff00u) * 3u + (b & 0x0000ff00u)) >> 2) & 0x0000ff00u;
    return 0xff000000u | rb | g;
}

/* MK8's sky-blue checkered flag field: a rotated low-contrast checker that
 * drifts diagonally, a soft top-left light and sparse twinkling glitter. The
 * smooth field renders at half resolution and is upsampled bilinearly. */
static void checker_field(MmCtx *x)
{
    UiCanvas *c = x->c;
    ModernMenu *menu = x->menu;
    UiDrawCommand command = {.kind = UI_DRAW_CHECKER, .data.checker = {x->u, x->t}};
    if (!ui_canvas_emit(c, &command))
    {
        const int fw = (c->width + 1) / 2 + 1, fh = (c->height + 1) / 2 + 1;
        const size_t needed = (size_t)fw * (size_t)(fh + 1); /* Field plus one blended row. */
        if (needed > menu->field_capacity)
        {
            uint32_t *grown = realloc(menu->field, needed * sizeof *grown);
            if (grown == NULL) return;
            menu->field = grown;
            menu->field_capacity = needed;
        }
        checker_pixels(menu->field, fw, fh, x->u * 0.5f, x->t);
        /* Bilinear 2x: output centers sit a quarter texel from the two nearest
         * source rows and columns. Blend the rows once, then neighbor columns. */
        uint32_t *blended = menu->field + (size_t)fw * (size_t)fh;
        for (int y = 0; y < c->height; ++y)
        {
            const int sy = y / 2, ny = (y & 1) ? sy + 1 : (sy > 0 ? sy - 1 : 0);
            const uint32_t *near_row = menu->field + (size_t)sy * (size_t)fw;
            const uint32_t *far_row = menu->field + (size_t)ny * (size_t)fw;
            for (int sx = 0; sx < fw; ++sx) blended[sx] = blend31(near_row[sx], far_row[sx]);
            uint32_t *row = c->pixels + (size_t)y * (size_t)c->stride;
            row[0] = blended[0];
            for (int px = 1; px < c->width; ++px)
            {
                const int sx = px / 2;
                row[px] = blend31(blended[sx], blended[(px & 1) ? sx + 1 : sx - 1]);
            }
        }
    }
    const uint32_t glitter_seed = x->tick / 6u;
    /* Sparse glitter: fixed 2x2 sparks that twinkle. */
    const unsigned sparks = (unsigned)(c->width * c->height) / 2048u;
    for (unsigned i = 0u; i < sparks; ++i)
    {
        const uint32_t h = hash32(i * 2654435761u + 17u);
        const uint32_t twinkle = hash32(h ^ glitter_seed) & 255u;
        if (twinkle <= 180u) continue;
        const int sx = (int)(h % (uint32_t)c->width) & ~1, sy = (int)((h >> 12) % (uint32_t)c->height) & ~1;
        ui_fill_rect(c, (float)sx, (float)sy, 2.0f, 2.0f, 0xffffffffu, (float)(twinkle - 180u) / 160.0f);
    }
}

/* The title's white speed swoosh with drifting streaks. */
static void swoosh(MmCtx *x)
{
    UiCanvas *c = x->c;
    const float w = (float)c->width, h = (float)c->height;
    const float reveal = mm_ease_out(mm_stagger(x, 0.0f, 22.0f));
    const float shift = (1.0f - reveal) * w * 0.5f;
    const UiPoint band[4] = {{w * 0.42f + shift, -10.0f}, {w + 10.0f, -10.0f},
                             {w + 10.0f, h + 10.0f}, {w * 0.24f + shift, h + 10.0f}};
    UiPaint shadow = ui_paint_solid(0x40003060u);
    const UiPoint shade[4] = {{band[0].x - mm_s(x, 26.0f), band[0].y}, band[1], band[2],
                              {band[3].x - mm_s(x, 26.0f), band[3].y}};
    ui_fill_convex(c, shade, 4, &shadow, mm_s(x, 40.0f), 1.0f);
    UiPaint white = ui_paint_linear(w * 0.3f, 0.0f, 0xfffbfdff, w, h, 0xffd9e3ea);
    ui_fill_convex(c, band, 4, &white, 1.5f, 1.0f);
    /* Thin blue racing stripe along the leading edge. */
    const float stripe = mm_s(x, 14.0f);
    const UiPoint edge[4] = {{band[0].x + stripe * 1.2f, band[0].y}, {band[0].x + stripe * 2.2f, band[0].y},
                             {band[3].x + stripe * 2.2f, band[3].y}, {band[3].x + stripe * 1.2f, band[3].y}};
    UiPaint blue = ui_paint_solid(0xff39b6f2u);
    ui_fill_convex(c, edge, 4, &blue, 1.2f, 0.9f);
    /* Streaks race diagonally across the band. */
    for (unsigned i = 0u; i < 9u; ++i)
    {
        const uint32_t seed = hash32(i * 977u + 13u);
        const float speed = 0.35f + (float)(seed & 255u) / 255.0f * 0.5f;
        const float lane = (float)((seed >> 8) & 1023u) / 1023.0f;
        float travel = fmodf(x->t * speed + (float)((seed >> 18) & 255u) / 255.0f, 1.0f);
        const float length = mm_s(x, 220.0f + (float)((seed >> 4) & 127u) * 2.0f);
        const float y0 = h * (1.1f - travel * 1.3f), x0 = w * (0.3f + lane * 0.75f) + (h - y0) * 0.45f - h * 0.2f;
        const float thick = mm_s(x, 3.0f + (float)(seed & 3u));
        const UiPoint streak[4] = {{x0, y0}, {x0 + thick, y0}, {x0 + thick + length * 0.45f, y0 - length},
                                   {x0 + length * 0.45f, y0 - length}};
        UiPaint paint = ui_paint_linear(x0, y0, 0x00a8d8f0u, x0 + length * 0.45f, y0 - length, 0x90a8d8f0u);
        ui_fill_convex(c, streak, 4, &paint, 1.2f, reveal);
    }
}

void mm_background(MmCtx *x, int with_swoosh)
{
    checker_field(x);
    if (with_swoosh) swoosh(x);
}

/* ---- Prompts ------------------------------------------------------------- */

static const char *key_name(MmButton button)
{
    switch (button)
    {
    case MM_BUTTON_A: return "Enter";
    case MM_BUTTON_B: return "Esc";
    case MM_BUTTON_START: return "Space";
    case MM_BUTTON_SELECT: return "Tab";
    case MM_BUTTON_L: return "A";
    case MM_BUTTON_R: return "S";
    case MM_BUTTON_LR: return "A/S";
    case MM_BUTTON_DPAD: return "";
    }
    return "?";
}

static const char *pad_name(MmButton button)
{
    switch (button)
    {
    case MM_BUTTON_A: return "A";
    case MM_BUTTON_B: return "B";
    case MM_BUTTON_START: return "+";
    case MM_BUTTON_SELECT: return "\xe2\x80\x93";
    case MM_BUTTON_L: return "L";
    case MM_BUTTON_R: return "R";
    case MM_BUTTON_LR: return "L R";
    case MM_BUTTON_DPAD: return "";
    }
    return "?";
}

static int round_glyph(MmButton button)
{
    return button == MM_BUTTON_A || button == MM_BUTTON_B;
}

float mm_button_glyph_width(MmCtx *x, float height, MmButton button)
{
    if ((x->view->gamepad && round_glyph(button)) || button == MM_BUTTON_DPAD) return height;
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, height * 0.62f, MM_INK);
    const char *text = x->view->gamepad ? pad_name(button) : key_name(button);
    const float text_width = ui_text_width(&style, text);
    return text_width + height * 0.7f > height ? text_width + height * 0.7f : height;
}

float mm_button_glyph(MmCtx *x, float px, float py, float height, MmButton button, float alpha)
{
    const float width = mm_button_glyph_width(x, height, button);
    const float top = py - height * 0.5f;
    if (button == MM_BUTTON_DPAD)
    {
        /* Direction pad cross (the menu font has no arrow glyphs). */
        const uint32_t ink = x->view->gamepad ? MM_YELLOW : MM_WHITE;
        UiPaint paint = ui_paint_solid(ink);
        const float arm = height * 0.3f, cx = px + height * 0.5f;
        ui_fill_round_rect(x->c, cx - arm * 0.5f, top + height * 0.08f, arm, height * 0.84f, arm * 0.25f, 0.0f, &paint,
                           1.0f, alpha);
        ui_fill_round_rect(x->c, px + height * 0.08f, py - arm * 0.5f, height * 0.84f, arm, arm * 0.25f, 0.0f, &paint,
                           1.0f, alpha);
        UiPaint hub = ui_paint_solid(0x60000000u);
        ui_fill_circle(x->c, cx, py, arm * 0.28f, &hub, 1.0f, alpha);
        return width;
    }
    if (x->view->gamepad)
    {
        const char *text = pad_name(button);
        UiTextStyle style = ui_text_style(UI_FONT_BLACK, height * 0.66f, MM_YELLOW);
        if (round_glyph(button))
        {
            ui_stroke_circle(x->c, px + height * 0.5f, py, height * 0.5f, height * 0.1f, MM_YELLOW, alpha);
            ui_text_draw_centered(x->c, &style, px + height * 0.5f, py, UI_ALIGN_CENTER, text, alpha);
        }
        else
        {
            ui_stroke_round_rect(x->c, px, top, width, height, height * 0.32f, 0.0f, height * 0.09f,
                                 MM_YELLOW, alpha);
            style.size = height * 0.56f;
            ui_text_draw_centered(x->c, &style, px + width * 0.5f, py, UI_ALIGN_CENTER, text, alpha);
        }
        return width;
    }
    /* Keyboard keycap: light key with a darker lip. */
    UiPaint lip = ui_paint_solid(0xff9aa6b4u);
    ui_fill_round_rect(x->c, px, top + height * 0.06f, width, height, height * 0.2f, 0.0f, &lip, 1.0f, alpha);
    UiPaint face = ui_paint_linear(0.0f, top, 0xffffffffu, 0.0f, top + height, 0xffdfe6eeu);
    ui_fill_round_rect(x->c, px, top - height * 0.04f, width, height, height * 0.2f, 0.0f, &face, 1.0f, alpha);
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, height * 0.56f, MM_INK);
    ui_text_draw_centered(x->c, &style, px + width * 0.5f, py - height * 0.04f, UI_ALIGN_CENTER,
                          key_name(button), alpha);
    return width;
}

/* ---- Header and footer ---------------------------------------------------- */

void mm_header(MmCtx *x, const char *title, const char *subtitle)
{
    if (title == NULL) return;
    const float appear = mm_ease_out(mm_stagger(x, 0.0f, 14.0f));
    const float slide = (1.0f - appear) * -mm_s(x, 120.0f);
    UiTextStyle style = ui_text_style(UI_FONT_ITALIC, mm_s(x, 64.0f), MM_WHITE);
    style.shadow_color = 0x80002a5au;
    style.shadow_y = mm_s(x, 4.0f);
    style.shadow_blur = mm_s(x, 6.0f);
    style.outline = mm_s(x, 2.0f);
    style.outline_color = 0x60003060u;
    const float text_x = mm_x(x, x->left + 96.0f) + slide;
    const float baseline = mm_y(x, x->top + 104.0f);
    const float width = ui_text_width(&style, title);
    /* Slanted dark ribbon behind the title, red tip at the left. */
    const float ribbon_y = baseline - mm_s(x, 62.0f), ribbon_h = mm_s(x, 82.0f);
    UiPaint ribbon = ui_paint_linear(0.0f, ribbon_y, 0xa0062a55u, 0.0f, ribbon_y + ribbon_h, 0xc0031a38u);
    ui_fill_round_rect(x->c, mm_x(x, x->left) - mm_s(x, 40.0f) + slide, ribbon_y,
                       text_x - mm_x(x, x->left) + width + mm_s(x, 120.0f), ribbon_h, mm_s(x, 6.0f),
                       0.35f, &ribbon, 1.0f, appear);
    UiPaint tip = ui_paint_linear(0.0f, ribbon_y, 0xffff5a52u, 0.0f, ribbon_y + ribbon_h, 0xffd0141au);
    ui_fill_round_rect(x->c, text_x - mm_s(x, 52.0f), ribbon_y, mm_s(x, 18.0f), ribbon_h, mm_s(x, 3.0f), 0.35f,
                       &tip, 1.0f, appear);
    ui_text_draw(x->c, &style, text_x, baseline, UI_ALIGN_LEFT, title, appear);
    if (subtitle != NULL && subtitle[0] != '\0')
    {
        UiTextStyle sub = ui_text_style(UI_FONT_BLACK, mm_s(x, 34.0f), MM_INK);
        const float sub_x = text_x + width + mm_s(x, 30.0f);
        const float sub_w = ui_text_width(&sub, subtitle) + mm_s(x, 40.0f);
        UiPaint chip = ui_paint_linear(0.0f, baseline - mm_s(x, 44.0f), 0xfffff38cu, 0.0f, baseline,
                                       0xffffc61au);
        ui_fill_round_rect(x->c, sub_x, baseline - mm_s(x, 48.0f), sub_w, mm_s(x, 50.0f), mm_s(x, 10.0f),
                           0.3f, &chip, 1.0f, appear);
        ui_text_draw_centered(x->c, &sub, sub_x + sub_w * 0.5f, baseline - mm_s(x, 23.0f), UI_ALIGN_CENTER,
                              subtitle, appear);
    }
}

void mm_footer(MmCtx *x, const char *center, const MmHint *hints, unsigned hint_count)
{
    UiCanvas *c = x->c;
    const float height = mm_s(x, 96.0f);
    const float top = (float)c->height - height;
    UiPaint band = ui_paint_linear(0.0f, top, 0xe6202429u, 0.0f, (float)c->height, 0xf2101216u);
    UiPaint shade = ui_paint_solid(0x50000000u);
    const UiPoint shadow[4] = {{-10.0f, top - mm_s(x, 6.0f)}, {(float)c->width + 10.0f, top - mm_s(x, 6.0f)},
                               {(float)c->width + 10.0f, (float)c->height}, {-10.0f, (float)c->height}};
    ui_fill_convex(c, shadow, 4, &shade, mm_s(x, 18.0f), 1.0f);
    const UiPoint body[4] = {{-10.0f, top}, {(float)c->width + 10.0f, top},
                             {(float)c->width + 10.0f, (float)c->height + 2.0f}, {-10.0f, (float)c->height + 2.0f}};
    ui_fill_convex(c, body, 4, &band, 1.0f, 1.0f);
    ui_fill_rect(c, 0.0f, top, (float)c->width, mm_s(x, 2.0f), 0x40ffffffu, 1.0f);
    const float glyph_h = mm_s(x, 46.0f), mid = top + height * 0.5f;
    UiTextStyle label = ui_text_style(UI_FONT_BLACK, mm_s(x, 34.0f), MM_WHITE);
    float cursor = mm_x(x, x->left + 48.0f);
    /* Back sits at the far left, confirm at the far right, the rest follow Back. */
    for (int pass = 0; pass < 2; ++pass)
        for (unsigned i = 0u; i < hint_count; ++i)
        {
            const MmHint *hint = &hints[i];
            if ((pass == 0) != (hint->button == MM_BUTTON_B) || hint->button == MM_BUTTON_A) continue;
            if (hint->button == MM_BUTTON_B)
            {
                /* MK8's yellow back arrow. */
                const float ax = cursor, ay = mid;
                const UiPoint arrow[3] = {{ax, ay}, {ax + glyph_h * 0.55f, ay - glyph_h * 0.42f},
                                          {ax + glyph_h * 0.55f, ay + glyph_h * 0.42f}};
                UiPaint yellow = ui_paint_solid(MM_YELLOW);
                ui_fill_convex(c, arrow, 3, &yellow, 1.0f, 1.0f);
                ui_fill_rect(c, ax + glyph_h * 0.5f, ay - glyph_h * 0.16f, glyph_h * 0.42f, glyph_h * 0.32f,
                             MM_YELLOW, 1.0f);
                cursor += glyph_h * 1.12f;
            }
            cursor += mm_button_glyph(x, cursor, mid, glyph_h, hint->button, 1.0f) + mm_s(x, 14.0f);
            ui_text_draw_centered(c, &label, cursor, mid, UI_ALIGN_LEFT, hint->label, 1.0f);
            cursor += ui_text_width(&label, hint->label) + mm_s(x, 46.0f);
        }
    for (unsigned i = 0u; i < hint_count; ++i)
        if (hints[i].button == MM_BUTTON_A)
        {
            UiTextStyle ok = ui_text_style(UI_FONT_BLACK, mm_s(x, 42.0f), MM_YELLOW);
            const float right = mm_x(x, x->right - 56.0f);
            const float text_w = ui_text_width(&ok, hints[i].label);
            const float gw = mm_button_glyph_width(x, glyph_h, MM_BUTTON_A);
            mm_button_glyph(x, right - text_w - gw - mm_s(x, 16.0f), mid, glyph_h, MM_BUTTON_A, 1.0f);
            ui_text_draw_centered(c, &ok, right, mid, UI_ALIGN_RIGHT, hints[i].label, 1.0f);
        }
    if (center != NULL && center[0] != '\0')
    {
        UiTextStyle title = ui_text_style(UI_FONT_BLACK, mm_s(x, 50.0f), MM_WHITE);
        title = ui_text_fit(&title, center, mm_s(x, 640.0f));
        ui_text_draw_centered(c, &title, (float)c->width * 0.5f, mid, UI_ALIGN_CENTER, center, 1.0f);
    }
}

/* ---- Panels, tabs, icons --------------------------------------------------- */

void mm_panel(MmCtx *x, float dx, float dy, float dw, float dh, float radius, float alpha)
{
    const float px = mm_x(x, dx), py = mm_y(x, dy), pw = mm_s(x, dw), ph = mm_s(x, dh), r = mm_s(x, radius);
    UiPaint shadow = ui_paint_solid(0x40002050u);
    ui_fill_round_rect(x->c, px, py + mm_s(x, 10.0f), pw, ph, r, 0.0f, &shadow, mm_s(x, 30.0f), alpha);
    UiPaint glass = ui_paint_linear(0.0f, py, 0x70ffffffu, 0.0f, py + ph, 0x40e8f6ffu);
    ui_fill_round_rect(x->c, px, py, pw, ph, r, 0.0f, &glass, 1.0f, alpha);
    ui_stroke_round_rect(x->c, px, py, pw, ph, r, 0.0f, mm_s(x, 3.0f), 0xb0ffffffu, alpha);
}

void mm_corner_brackets(MmCtx *x, float px, float py, float pw, float ph, uint32_t color, float alpha)
{
    const float pulse = 0.5f + 0.5f * sinf(x->t * 7.0f);
    const float out = mm_s(x, 6.0f + 6.0f * pulse);
    const float arm = (pw < ph ? pw : ph) * 0.3f, thick = mm_s(x, 10.0f);
    const float l = px - out, t = py - out, r = px + pw + out, b = py + ph + out;
    UiPaint paint = ui_paint_solid(color);
    UiPaint dark = ui_paint_solid(0x80705000u);
    for (int pass = 0; pass < 2; ++pass)
    {
        const float o = pass == 0 ? mm_s(x, 3.0f) : 0.0f;
        UiPaint *p = pass == 0 ? &dark : &paint;
        const float f = pass == 0 ? mm_s(x, 6.0f) : 1.2f;
        /* Each corner is two bars meeting at the corner. */
        ui_fill_round_rect(x->c, l + o, t + o, arm, thick, thick * 0.4f, 0.0f, p, f, alpha);
        ui_fill_round_rect(x->c, l + o, t + o, thick, arm, thick * 0.4f, 0.0f, p, f, alpha);
        ui_fill_round_rect(x->c, r - arm + o, t + o, arm, thick, thick * 0.4f, 0.0f, p, f, alpha);
        ui_fill_round_rect(x->c, r - thick + o, t + o, thick, arm, thick * 0.4f, 0.0f, p, f, alpha);
        ui_fill_round_rect(x->c, l + o, b - thick + o, arm, thick, thick * 0.4f, 0.0f, p, f, alpha);
        ui_fill_round_rect(x->c, l + o, b - arm + o, thick, arm, thick * 0.4f, 0.0f, p, f, alpha);
        ui_fill_round_rect(x->c, r - arm + o, b - thick + o, arm, thick, thick * 0.4f, 0.0f, p, f, alpha);
        ui_fill_round_rect(x->c, r - thick + o, b - arm + o, thick, arm, thick * 0.4f, 0.0f, p, f, alpha);
    }
}

static void quad(UiCanvas *c, float x0, float y0, float x1, float y1, float x2, float y2, float x3,
                 float y3, const UiPaint *paint, float alpha)
{
    const UiPoint points[4] = {{x0, y0}, {x1, y1}, {x2, y2}, {x3, y3}};
    ui_fill_convex(c, points, 4, paint, 1.0f, alpha);
}

/* A thick line segment as a quad. */
static void segment(UiCanvas *c, float x0, float y0, float x1, float y1, float width,
                    const UiPaint *paint, float alpha)
{
    float dx = x1 - x0, dy = y1 - y0;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < 1e-3f) return;
    dx = dx / length * width * 0.5f;
    dy = dy / length * width * 0.5f;
    quad(c, x0 - dy, y0 + dx, x1 - dy, y1 + dx, x1 + dy, y1 - dx, x0 + dy, y0 - dx, paint, alpha);
}

void mm_icon(MmCtx *x, MmIcon icon, float cx, float cy, float r, uint32_t color, float alpha)
{
    UiCanvas *c = x->c;
    UiPaint p = ui_paint_solid(color);
    switch (icon)
    {
    case MM_ICON_TROPHY:
    {
        const UiPoint bowl[6] = {{cx - r * 0.62f, cy - r * 0.7f}, {cx + r * 0.62f, cy - r * 0.7f},
                                 {cx + r * 0.55f, cy - r * 0.15f}, {cx + r * 0.2f, cy + r * 0.2f},
                                 {cx - r * 0.2f, cy + r * 0.2f}, {cx - r * 0.55f, cy - r * 0.15f}};
        ui_fill_convex(c, bowl, 6, &p, 1.0f, alpha);
        ui_stroke_circle(c, cx - r * 0.62f, cy - r * 0.42f, r * 0.3f, r * 0.12f, color, alpha);
        ui_stroke_circle(c, cx + r * 0.62f, cy - r * 0.42f, r * 0.3f, r * 0.12f, color, alpha);
        ui_fill_rect(c, cx - r * 0.1f, cy + r * 0.15f, r * 0.2f, r * 0.35f, color, alpha);
        ui_fill_round_rect(c, cx - r * 0.45f, cy + r * 0.48f, r * 0.9f, r * 0.24f, r * 0.06f, 0.0f, &p, 1.0f, alpha);
        break;
    }
    case MM_ICON_CLOCK:
        ui_stroke_circle(c, cx, cy + r * 0.08f, r * 0.72f, r * 0.16f, color, alpha);
        ui_fill_round_rect(c, cx - r * 0.18f, cy - r * 0.95f, r * 0.36f, r * 0.2f, r * 0.05f, 0.0f, &p, 1.0f, alpha);
        segment(c, cx, cy + r * 0.08f, cx, cy - r * 0.38f, r * 0.14f, &p, alpha);
        segment(c, cx, cy + r * 0.08f, cx + r * 0.32f, cy + r * 0.22f, r * 0.14f, &p, alpha);
        break;
    case MM_ICON_FLAG:
    {
        ui_fill_rect(c, cx - r * 0.62f, cy - r * 0.8f, r * 0.12f, r * 1.6f, color, alpha);
        const float s = r * 0.32f, fx = cx - r * 0.5f, fy = cy - r * 0.78f;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                if (((row + col) & 1) == 0)
                    ui_fill_rect(c, fx + (float)col * s, fy + (float)row * s, s, s, color, alpha);
        ui_stroke_round_rect(c, fx, fy, s * 3.0f, s * 3.0f, 0.0f, 0.0f, r * 0.06f, color, alpha);
        break;
    }
    case MM_ICON_BALLOON:
        ui_fill_ellipse(c, cx, cy - r * 0.2f, r * 0.55f, r * 0.68f, &p, 1.0f, alpha);
        quad(c, cx - r * 0.12f, cy + r * 0.55f, cx + r * 0.12f, cy + r * 0.55f, cx + r * 0.04f, cy + r * 0.44f,
             cx - r * 0.04f, cy + r * 0.44f, &p, alpha);
        segment(c, cx, cy + r * 0.55f, cx - r * 0.12f, cy + r * 0.95f, r * 0.07f, &p, alpha);
        {
            UiPaint shine = ui_paint_solid(0x80ffffffu);
            ui_fill_ellipse(c, cx - r * 0.22f, cy - r * 0.45f, r * 0.12f, r * 0.2f, &shine, 1.0f, alpha);
        }
        break;
    case MM_ICON_GEAR:
        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const float angle = (float)tooth * 0.785398f;
            const float ax = cosf(angle), ay = sinf(angle);
            segment(c, cx + ax * r * 0.45f, cy + ay * r * 0.45f, cx + ax * r * 0.88f, cy + ay * r * 0.88f, r * 0.3f,
                    &p, alpha);
        }
        ui_stroke_circle(c, cx, cy, r * 0.62f, r * 0.34f, color, alpha);
        break;
    case MM_ICON_GHOST:
        ui_fill_circle(c, cx, cy - r * 0.18f, r * 0.62f, &p, 1.0f, alpha);
        ui_fill_rect(c, cx - r * 0.62f, cy - r * 0.18f, r * 1.24f, r * 0.62f, color, alpha);
        for (int bump = 0; bump < 3; ++bump)
            ui_fill_circle(c, cx - r * 0.42f + (float)bump * r * 0.42f, cy + r * 0.44f, r * 0.21f, &p, 1.0f, alpha);
        {
            UiPaint eye = ui_paint_solid(0xff1b3a5cu);
            ui_fill_ellipse(c, cx - r * 0.22f, cy - r * 0.22f, r * 0.1f, r * 0.16f, &eye, 1.0f, alpha);
            ui_fill_ellipse(c, cx + r * 0.22f, cy - r * 0.22f, r * 0.1f, r * 0.16f, &eye, 1.0f, alpha);
        }
        break;
    case MM_ICON_PEOPLE:
        ui_fill_circle(c, cx + r * 0.32f, cy - r * 0.42f, r * 0.26f, &p, 1.0f, alpha * 0.7f);
        ui_fill_round_rect(c, cx - r * 0.02f, cy - r * 0.1f, r * 0.7f, r * 0.75f, r * 0.3f, 0.0f, &p, 1.0f, alpha * 0.7f);
        ui_fill_circle(c, cx - r * 0.25f, cy - r * 0.32f, r * 0.3f, &p, 1.0f, alpha);
        ui_fill_round_rect(c, cx - r * 0.65f, cy + r * 0.02f, r * 0.8f, r * 0.8f, r * 0.34f, 0.0f, &p, 1.0f, alpha);
        break;
    case MM_ICON_PERSON:
        ui_fill_circle(c, cx, cy - r * 0.35f, r * 0.34f, &p, 1.0f, alpha);
        ui_fill_round_rect(c, cx - r * 0.5f, cy + r * 0.05f, r, r * 0.8f, r * 0.4f, 0.0f, &p, 1.0f, alpha);
        break;
    case MM_ICON_PLAY:
    {
        const UiPoint tri[3] = {{cx - r * 0.38f, cy - r * 0.55f}, {cx + r * 0.6f, cy}, {cx - r * 0.38f, cy + r * 0.55f}};
        ui_fill_convex(c, tri, 3, &p, 1.0f, alpha);
        break;
    }
    case MM_ICON_LIST:
        for (int line = 0; line < 3; ++line)
        {
            const float ly = cy - r * 0.42f + (float)line * r * 0.42f;
            ui_fill_circle(c, cx - r * 0.55f, ly, r * 0.11f, &p, 1.0f, alpha);
            ui_fill_round_rect(c, cx - r * 0.32f, ly - r * 0.09f, r * 0.9f, r * 0.18f, r * 0.09f, 0.0f, &p, 1.0f, alpha);
        }
        break;
    case MM_ICON_NONE:
        break;
    }
}

/* Selection chevrons hugging a tab's pointed ends, pulsing outward. */
static void tab_brackets(MmCtx *x, float px, float py, float pw, float ph, float point, float alpha)
{
    const float pulse = 0.5f + 0.5f * sinf(x->t * 6.5f);
    const float gap = mm_s(x, 10.0f + 8.0f * pulse), thick = mm_s(x, 15.0f), reach = ph * 0.18f;
    UiPaint gold = ui_paint_linear(0.0f, py, 0xfffff07au, 0.0f, py + ph, 0xffffb800u);
    UiPaint edge = ui_paint_solid(0xa0a86b00u);
    for (int side = 0; side < 2; ++side)
    {
        const float dir = side == 0 ? -1.0f : 1.0f;
        const float tip_x = side == 0 ? px - gap : px + pw + gap, tip_y = py + ph * 0.5f;
        const float back_x = tip_x - dir * point, top = py - reach, bottom = py + ph + reach;
        for (int pass = 0; pass < 2; ++pass)
        {
            const float o = pass == 0 ? mm_s(x, 3.0f) : 0.0f;
            const UiPaint *paint = pass == 0 ? &edge : &gold;
            const UiPoint upper[4] = {{back_x + o, top + o}, {back_x - dir * thick + o, top + o},
                                      {tip_x - dir * thick + o, tip_y + o}, {tip_x + o, tip_y + o}};
            const UiPoint lower[4] = {{tip_x + o, tip_y + o}, {tip_x - dir * thick + o, tip_y + o},
                                      {back_x - dir * thick + o, bottom + o}, {back_x + o, bottom + o}};
            ui_fill_convex(x->c, upper, 4, paint, pass == 0 ? mm_s(x, 4.0f) : 1.2f, alpha);
            ui_fill_convex(x->c, lower, 4, paint, pass == 0 ? mm_s(x, 4.0f) : 1.2f, alpha);
        }
    }
}

void mm_tab(MmCtx *x, float dx, float dy, float dw, float dh, const char *label, const char *badge,
            int selected, float alpha, MmIcon icon, uint32_t portrait_id, int disabled)
{
    if (alpha <= 0.0f) return;
    UiCanvas *c = x->c;
    float px = mm_x(x, dx), py = mm_y(x, dy), pw = mm_s(x, dw), ph = mm_s(x, dh);
    if (selected)
    {
        const float grow = mm_s(x, 10.0f);
        px -= grow;
        py -= grow * 0.5f;
        pw += grow * 2.0f;
        ph += grow;
    }
    const float point = ph * 0.24f, border = mm_s(x, 4.0f);
    UiPaint shadow = ui_paint_solid(selected ? 0x70604000u : 0x50003060u);
    ui_fill_chevron_bar(c, px + mm_s(x, 4.0f), py + mm_s(x, 10.0f), pw, ph, point, &shadow, mm_s(x, 18.0f), alpha);
    if (selected)
    {
        const float glow = 0.55f + 0.45f * sinf(x->t * 6.5f);
        UiPaint halo = ui_paint_solid(0xc0fff6b0u);
        ui_fill_chevron_bar(c, px - mm_s(x, 8.0f), py - mm_s(x, 8.0f), pw + mm_s(x, 16.0f), ph + mm_s(x, 16.0f),
                            point * 1.2f, &halo, mm_s(x, 16.0f), alpha * glow);
    }
    UiPaint rim = ui_paint_solid(selected ? 0xffd88f00u : disabled ? 0xff8ea4b4u : 0xffa9d4ecu);
    ui_fill_chevron_bar(c, px, py, pw, ph, point, &rim, 1.2f, alpha);
    UiPaint fill;
    if (selected)
    {
        fill = ui_paint_linear(0.0f, py, 0xfffffbb8u, 0.0f, py + ph, 0xffffbf00u);
        ui_paint_stop(&fill, 0.48f, 0xffffe55au);
    }
    else if (disabled)
        fill = ui_paint_linear(0.0f, py, 0xffd5dde3u, 0.0f, py + ph, 0xffb7c3cdu);
    else
    {
        fill = ui_paint_linear(0.0f, py, 0xffffffffu, 0.0f, py + ph, 0xffd6ecf8u);
        ui_paint_stop(&fill, 0.5f, 0xfff4fafeu);
    }
    ui_fill_chevron_bar(c, px + border, py + border, pw - border * 2.0f, ph - border * 2.0f,
                        point - border * 0.4f, &fill, 1.2f, alpha);
    /* Gloss: the upper half of the face, brightened. */
    const int saved_y1 = c->clip_y1;
    ui_canvas_clip(c, c->clip_x0, c->clip_y0, c->clip_x1, (int)(py + ph * 0.46f));
    UiPaint gloss = ui_paint_linear(0.0f, py, 0x90ffffffu, 0.0f, py + ph * 0.46f, 0x10ffffffu);
    ui_fill_chevron_bar(c, px + border * 2.0f, py + border * 2.0f, pw - border * 4.0f, ph - border * 2.0f,
                        point - border, &gloss, 1.2f, alpha);
    ui_canvas_clip(c, c->clip_x0, c->clip_y0, c->clip_x1, saved_y1);
    /* Icon disc at the left. */
    const float icon_r = ph * 0.36f, icon_x = px + point + icon_r + mm_s(x, 10.0f), icon_y = py + ph * 0.5f;
    float text_x = px + point + mm_s(x, 26.0f);
    if (portrait_id != 0u || icon != MM_ICON_NONE)
    {
        const UiImage *portrait = portrait_id != 0u ? mm_art(x, portrait_id) : NULL;
        if (portrait != NULL)
        {
            ui_fill_circle(c, icon_x, icon_y, icon_r + mm_s(x, 4.0f), &rim, 1.0f, alpha);
            ui_draw_image(c, portrait, icon_x - icon_r, icon_y - icon_r, icon_r * 2.0f, icon_r * 2.0f, alpha,
                          disabled ? UI_IMAGE_GRAY : 0u, MM_WHITE, icon_r);
        }
        else
        {
            UiPaint disc = selected ? ui_paint_linear(0.0f, icon_y - icon_r, 0xffff6a3cu, 0.0f, icon_y + icon_r, 0xffd5181eu)
                                    : ui_paint_linear(0.0f, icon_y - icon_r, 0xff47c4ffu, 0.0f, icon_y + icon_r, 0xff1478d6u);
            if (disabled) disc = ui_paint_solid(0xff8c9aa6u);
            ui_fill_circle(c, icon_x, icon_y, icon_r, &disc, 1.0f, alpha);
            ui_stroke_circle(c, icon_x, icon_y, icon_r, mm_s(x, 3.0f), 0x80ffffffu, alpha);
            mm_icon(x, icon, icon_x, icon_y, icon_r * 0.78f, MM_WHITE, alpha);
        }
        text_x = icon_x + icon_r + mm_s(x, 24.0f);
    }
    float text_right = px + pw - point - mm_s(x, 16.0f);
    if (badge != NULL && badge[0] != '\0')
    {
        UiTextStyle badge_style = ui_text_style(UI_FONT_ITALIC, ph * 0.46f, selected ? 0xff5a3a00u : 0xff3b6f96u);
        const float bw = ui_text_width(&badge_style, badge) + ph * 0.5f, bh = ph * 0.64f;
        const float bx = px + pw - point * 0.55f - bw - mm_s(x, 8.0f), by = py + (ph - bh) * 0.5f;
        UiPaint box = selected ? ui_paint_linear(0.0f, by, 0xffffd84au, 0.0f, by + bh, 0xffffa800u)
                               : ui_paint_linear(0.0f, by, 0xffe9f6ffu, 0.0f, by + bh, 0xffc5e3f6u);
        ui_fill_round_rect(c, bx, by, bw, bh, mm_s(x, 8.0f), 0.28f, &box, 1.0f, alpha);
        ui_text_draw_centered(c, &badge_style, bx + bw * 0.5f, by + bh * 0.5f, UI_ALIGN_CENTER, badge, alpha);
        text_right = bx - mm_s(x, 12.0f);
    }
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, ph * 0.42f, disabled ? 0xff6f7c88u : selected ? 0xff3a2a00u : MM_INK);
    if (selected)
    {
        style.shadow_color = 0x60ffffffu;
        style.shadow_y = mm_s(x, 2.0f);
    }
    style = ui_text_fit(&style, label, text_right - text_x);
    ui_text_draw_centered(c, &style, text_x, py + ph * 0.5f, UI_ALIGN_LEFT, label, alpha);
    if (selected) tab_brackets(x, px, py, pw, ph, point, alpha);
}

/* ---- Frame -------------------------------------------------------------- */

void mm_mode_names(const PortableFrontendFlow *flow, const char **title, const char **subtitle)
{
    static const char *const modes[4] = {"Grand Prix", "Time Trial", "Quick Run", "Battle"};
    static const char *const classes[3] = {"50cc", "100cc", "150cc"};
    *title = modes[flow->mode_cursor & 3u];
    *subtitle = flow->mode_cursor == 1u || flow->mode_cursor == 3u ? NULL : classes[flow->race_speed % 3u];
    if (flow->players > 1u && flow->mode_cursor == 2u) *title = "VS Race";
}

static uint32_t screen_key(const PortableFrontendFlow *flow)
{
    uint32_t key = (uint32_t)flow->page * 4096u;
    if (flow->page == PORTABLE_FRONTEND_PAGE_MODE) key += 1u + flow->mode_phase;
    if (flow->page == PORTABLE_FRONTEND_PAGE_COURSE || flow->page == PORTABLE_FRONTEND_PAGE_RECORDS)
        key += 16u + (flow->course_phase > 1u ? 2u : flow->course_phase);
    if (flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU) key += flow->player_count_open ? 200u : 0u;
    return key;
}

void mm_context_init(MmCtx *x, ModernMenu *menu, const ModernMenuView *view, const PortableFrontendFlow *flow,
                     UiCanvas *canvas, uint32_t tick, uint32_t key)
{
    memset(x, 0, sizeof *x);
    x->c = canvas;
    x->view = view;
    x->flow = flow;
    x->menu = menu;
    const float design_w = 1920.0f, design_h = 1080.0f;
    const float sx = (float)canvas->width / design_w, sy = (float)canvas->height / design_h;
    x->u = sx < sy ? sx : sy;
    x->ox = ((float)canvas->width - design_w * x->u) * 0.5f;
    x->oy = ((float)canvas->height - design_h * x->u) * 0.5f;
    x->left = -x->ox / x->u;
    x->right = design_w + x->ox / x->u;
    x->top = -x->oy / x->u;
    x->bottom = design_h + x->oy / x->u;
    x->tick = tick;
    x->t = (float)tick / MM_TICK_RATE;
    if (!menu->screen_valid || menu->screen_key != key)
    {
        menu->screen_key = key;
        menu->screen_tick = tick;
        menu->screen_valid = 1u;
    }
    x->elapsed = menu->last_tick <= tick ? tick - menu->last_tick : 9u;
    if (x->elapsed > 9u) x->elapsed = 9u;
    menu->last_tick = tick;
    x->screen_ticks = tick - menu->screen_tick;
    x->enter = mm_ease_out(mm_stagger(x, 0.0f, 18.0f));
}

int modern_menu_render(ModernMenu *menu, const ModernMenuView *view, UiCanvas *canvas)
{
    if (menu == NULL || view == NULL || view->pack == NULL || view->flow == NULL || canvas == NULL ||
        canvas->width <= 0 || canvas->height <= 0)
        return 0;
    const PortableFrontendFlow *source = view->flow;
    /* During an original-length transition, show the outgoing page and then
     * its successor, as the original presentation does. */
    PortableFrontendFlow shown = *source;
    if (source->transition_ticks != 0u && source->transition_total_ticks != 0u)
    {
        const unsigned age = (unsigned)source->transition_total_ticks - source->transition_ticks;
        if (age >= source->transition_out_ticks && source->pending_page != PORTABLE_FRONTEND_PAGE_RACE &&
            source->pending_page != 0)
        {
            shown.page = source->pending_page;
            shown.page_ticks = age - source->transition_out_ticks;
            if (shown.page == PORTABLE_FRONTEND_PAGE_CHARACTER)
            {
                shown.character_locked = 0u;
                shown.character_player = 0u;
            }
            if (shown.page == PORTABLE_FRONTEND_PAGE_COURSE)
                shown.course_phase = shown.mode_cursor == 3u ? 1u : 0u;
        }
    }
    MmCtx x;
    mm_context_init(&x, menu, view, &shown, canvas, source->tick, screen_key(&shown));
    ui_canvas_unclip(canvas);
    int ok = 1;
    const int options_open = view->options != NULL && view->options->page != MODERN_MENU_OPTIONS_CLOSED;
    /* Options replace the page: a clean backdrop keeps the rows legible. */
    if (options_open) mm_background(&x, 0);
    else switch (shown.page)
    {
    case PORTABLE_FRONTEND_PAGE_TITLE_INTRO: ok = mm_page_title(&x, 1); break;
    case PORTABLE_FRONTEND_PAGE_TITLE_MENU: ok = mm_page_title(&x, 0); break;
    case PORTABLE_FRONTEND_PAGE_MODE: ok = mm_page_mode(&x); break;
    case PORTABLE_FRONTEND_PAGE_TIME_TRIAL: ok = mm_page_time_trial(&x); break;
    case PORTABLE_FRONTEND_PAGE_CHARACTER: ok = mm_page_character(&x); break;
    case PORTABLE_FRONTEND_PAGE_COURSE: ok = mm_page_course(&x); break;
    case PORTABLE_FRONTEND_PAGE_RECORDS: ok = mm_page_records(&x); break;
    default: ok = 0; break;
    }
    if (ok && options_open)
        mm_options_overlay(&x, view->options);
    /* Race launch: the course page fades to black like the original. */
    if (ok && source->transition_ticks != 0u && source->pending_page == PORTABLE_FRONTEND_PAGE_RACE &&
        source->transition_total_ticks != 0u)
    {
        const float age = (float)(source->transition_total_ticks - source->transition_ticks);
        const float hold = (float)(PORTABLE_FRONTEND_COURSE_RACE_TICKS - PORTABLE_FRONTEND_COURSE_RACE_FADE_TICKS);
        const float fade = source->transition_total_ticks <= 1u ? 1.0f
                                                               : mm_clamp01((age - hold) / (float)PORTABLE_FRONTEND_COURSE_RACE_FADE_TICKS);
        ui_tint_rect(canvas, 0, 0, canvas->width, canvas->height, 0xff000000u, fade);
    }
    return ok;
}
