#include "modern_menu_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The loading screen runs before any game pack exists and while workers own
 * the shell, so it uses only vector shapes, the embedded fonts and logo. The
 * still layer (backdrop, logo, card, footer) is drawn once per size; the live
 * layer redraws only the card's contents each frame. */

enum { CARD_X = 420, CARD_Y = 430, CARD_W = 1080, CARD_H = 470 };

static void card_bounds(const MmCtx *x, float *left, float *top, float *right, float *bottom)
{
    const float margin = 24.0f;
    *left = floorf(mm_x(x, (float)CARD_X - margin));
    *top = floorf(mm_y(x, (float)CARD_Y - margin));
    *right = ceilf(mm_x(x, (float)(CARD_X + CARD_W) + margin));
    *bottom = ceilf(mm_y(x, (float)(CARD_Y + CARD_H) + margin * 1.5f));
}

static void loading_context(MmCtx *x, ModernMenu *menu, const ModernMenuView *view, UiCanvas *canvas,
                            uint32_t tick)
{
    /* A fixed screen key keeps entrance staggers settled: the screen may be
     * shown for one frame or a minute, and must never look half-drawn. */
    mm_context_init(x, menu, view, NULL, canvas, tick, 0x7b000000u);
    x->screen_ticks = 600u;
    x->enter = 1.0f;
}

static void still_layer(MmCtx *x, const ModernMenuLoading *loading)
{
    mm_background(x, 0);
    /* A soft vignette pulls the eye to the centre at any aspect ratio. */
    UiCanvas *c = x->c;
    const float w = (float)c->width, h = (float)c->height;
    UiPaint top = ui_paint_linear(0.0f, 0.0f, 0x50001a40u, 0.0f, h * 0.35f, 0x00001a40u);
    ui_fill_round_rect(c, -2.0f, -2.0f, w + 4.0f, h * 0.35f, 0.0f, 0.0f, &top, 1.0f, 1.0f);
    UiPaint bottom = ui_paint_linear(0.0f, h * 0.6f, 0x00001a40u, 0.0f, h, 0x60001a40u);
    ui_fill_round_rect(c, -2.0f, h * 0.6f, w + 4.0f, h * 0.4f + 2.0f, 0.0f, 0.0f, &bottom, 1.0f, 1.0f);
    mm_title_logo(x, mm_x(x, 960.0f), mm_y(x, 120.0f), 0.62f, 1.0f);
    mm_panel(x, (float)CARD_X, (float)CARD_Y, (float)CARD_W, (float)CARD_H, 36.0f, 1.0f);
    /* A red racing tab on the card's left edge, as on the menu headers. */
    UiPaint tab = ui_paint_linear(0.0f, mm_y(x, CARD_Y), 0xffff5a52u, 0.0f, mm_y(x, CARD_Y + 120.0f), 0xffd0141au);
    ui_fill_round_rect(c, mm_x(x, CARD_X - 10.0f), mm_y(x, CARD_Y + 40.0f), mm_s(x, 20.0f), mm_s(x, 96.0f),
                       mm_s(x, 6.0f), 0.0f, &tab, 1.0f, 1.0f);
    const char *center = loading->cancelling == 2 ? "Closing after the current step..."
                         : loading->cancelling ? "Cancelling..." : NULL;
    const MmHint hints[] = {{MM_BUTTON_B, "Cancel"}};
    mm_footer(x, center, hints, loading->cancellable && !loading->cancelling ? 1u : 0u);
}

static void check_mark(UiCanvas *c, float cx, float cy, float r, uint32_t color)
{
    UiPaint ink = ui_paint_solid(color);
    const float t = r * 0.2f;
    const UiPoint short_arm[4] = {{cx - r * 0.5f, cy - t * 0.2f}, {cx - r * 0.5f + t, cy - t * 0.9f},
                                  {cx - r * 0.08f + t * 0.5f, cy + r * 0.32f}, {cx - r * 0.12f, cy + r * 0.5f}};
    const UiPoint long_arm[4] = {{cx - r * 0.12f, cy + r * 0.5f}, {cx - r * 0.08f - t * 0.6f, cy + r * 0.2f},
                                 {cx + r * 0.5f - t * 0.6f, cy - r * 0.48f}, {cx + r * 0.52f, cy - r * 0.32f}};
    ui_fill_convex(c, short_arm, 4, &ink, 1.0f, 1.0f);
    ui_fill_convex(c, long_arm, 4, &ink, 1.0f, 1.0f);
}

static void step_marker(MmCtx *x, float cx, float cy, float r, int state)
{
    UiCanvas *c = x->c;
    if (state < 0)
    {
        UiPaint gold = ui_paint_linear(0.0f, cy - r, 0xfffff07au, 0.0f, cy + r, 0xffffb800u);
        ui_fill_circle(c, cx, cy, r, &gold, 1.2f, 1.0f);
        check_mark(c, cx, cy, r, 0xff3a2a00u);
        return;
    }
    if (state > 0)
    {
        ui_stroke_circle(c, cx, cy, r * 0.9f, r * 0.18f, 0x90ffffffu, 1.0f);
        return;
    }
    /* Current step: a ring of dots chasing round, like the setup spinner. */
    for (int i = 0; i < 10; ++i)
    {
        const float angle = (float)i / 10.0f * 6.2831853f;
        const float phase = fmodf(x->t * 1.4f - (float)i / 10.0f + 1.0f, 1.0f);
        UiPaint dot = ui_paint_solid(ui_mix(MM_WHITE, MM_YELLOW, phase));
        ui_fill_circle(c, cx + cosf(angle) * r * 0.82f, cy + sinf(angle) * r * 0.82f, r * (0.12f + 0.12f * phase),
                       &dot, 1.2f, 0.35f + 0.65f * phase);
    }
}

/* Segments follow reported steps exactly; only the current segment shimmers,
 * so animation shows activity without inventing a percentage. */
static void progress_bar(MmCtx *x, float dx, float dy, float dw, float dh, unsigned done, unsigned total)
{
    UiCanvas *c = x->c;
    const float px = mm_x(x, dx), py = mm_y(x, dy), pw = mm_s(x, dw), ph = mm_s(x, dh);
    const float radius = ph * 0.5f, gap = mm_s(x, 8.0f);
    UiPaint well = ui_paint_linear(0.0f, py, 0x90001a40u, 0.0f, py + ph, 0x60003060u);
    ui_fill_round_rect(c, px - mm_s(x, 6.0f), py - mm_s(x, 6.0f), pw + mm_s(x, 12.0f), ph + mm_s(x, 12.0f),
                       radius + mm_s(x, 6.0f), 0.0f, &well, 1.2f, 1.0f);
    if (total == 0u) total = 1u;
    const float segment = (pw - gap * (float)(total - 1u)) / (float)total;
    for (unsigned i = 0u; i < total; ++i)
    {
        const float sx = px + (float)i * (segment + gap);
        if (i < done)
        {
            UiPaint gold = ui_paint_linear(0.0f, py, 0xfffff38cu, 0.0f, py + ph, 0xffffb800u);
            ui_paint_stop(&gold, 0.5f, 0xffffd21eu);
            ui_fill_round_rect(c, sx, py, segment, ph, radius, 0.0f, &gold, 1.2f, 1.0f);
            continue;
        }
        UiPaint empty = ui_paint_solid(0x38ffffffu);
        ui_fill_round_rect(c, sx, py, segment, ph, radius, 0.0f, &empty, 1.2f, 1.0f);
        if (i != done) continue;
        /* Diagonal racing stripes travel through the active segment. */
        const int outer[4] = {c->clip_x0, c->clip_y0, c->clip_x1, c->clip_y1};
        const int inner[4] = {(int)floorf(sx), (int)floorf(py), (int)ceilf(sx + segment), (int)ceilf(py + ph)};
        ui_canvas_clip(c, inner[0] > outer[0] ? inner[0] : outer[0], inner[1] > outer[1] ? inner[1] : outer[1],
                       inner[2] < outer[2] ? inner[2] : outer[2], inner[3] < outer[3] ? inner[3] : outer[3]);
        UiPaint base = ui_paint_linear(0.0f, py, 0xffffe55au, 0.0f, py + ph, 0xffffb000u);
        ui_fill_round_rect(c, sx, py, segment, ph, radius, 0.0f, &base, 1.2f, 1.0f);
        const float stripe = ph * 1.2f, travel = fmodf(x->t * stripe * 2.2f, stripe * 2.0f);
        for (float at = sx - stripe * 2.0f + travel; at < sx + segment + stripe; at += stripe * 2.0f)
        {
            const UiPoint band[4] = {{at, py + ph}, {at + stripe, py + ph}, {at + stripe + ph, py}, {at + ph, py}};
            UiPaint white = ui_paint_solid(0x80fffbd0u);
            ui_fill_convex(c, band, 4, &white, 1.0f, 1.0f);
        }
        ui_canvas_clip(c, outer[0], outer[1], outer[2], outer[3]);
    }
}

static void live_layer(MmCtx *x, const ModernMenuLoading *loading)
{
    UiCanvas *c = x->c;
    const float left = CARD_X + 70.0f, width = CARD_W - 140.0f;
    /* Without a detail line the title and steps share its space. */
    const int has_detail = loading->detail != NULL && loading->detail[0] != '\0';
    const float shift = has_detail ? 0.0f : 30.0f;
    UiTextStyle title = ui_text_style(UI_FONT_ITALIC, mm_s(x, 58.0f), MM_WHITE);
    title.shadow_color = 0x80002a5au;
    title.shadow_y = mm_s(x, 4.0f);
    title.shadow_blur = mm_s(x, 6.0f);
    title.outline = mm_s(x, 2.0f);
    title.outline_color = 0x60003060u;
    title = ui_text_fit(&title, loading->title != NULL ? loading->title : "Loading", mm_s(x, width));
    ui_text_draw_centered(c, &title, mm_x(x, left), mm_y(x, CARD_Y + 88.0f + shift), UI_ALIGN_LEFT,
                          loading->title != NULL ? loading->title : "Loading", 1.0f);
    if (has_detail)
    {
        UiTextStyle chip = ui_text_style(UI_FONT_BLACK, mm_s(x, 28.0f), MM_INK);
        chip = ui_text_fit(&chip, loading->detail, mm_s(x, width - 48.0f));
        const float chip_w = ui_text_width(&chip, loading->detail) + mm_s(x, 48.0f);
        const float chip_y = mm_y(x, CARD_Y + 140.0f), chip_h = mm_s(x, 50.0f);
        UiPaint fill = ui_paint_linear(0.0f, chip_y, 0xfffff38cu, 0.0f, chip_y + chip_h, 0xffffc61au);
        ui_fill_round_rect(c, mm_x(x, left), chip_y, chip_w, chip_h, mm_s(x, 12.0f), 0.3f, &fill, 1.0f, 1.0f);
        ui_text_draw_centered(c, &chip, mm_x(x, left) + chip_w * 0.5f, chip_y + chip_h * 0.5f, UI_ALIGN_CENTER,
                              loading->detail, 1.0f);
    }
    const unsigned count = loading->phase_count < 4u ? loading->phase_count : 4u;
    const unsigned current = loading->phase < count ? loading->phase : count ? count - 1u : 0u;
    const float rows_top = CARD_Y + 236.0f - shift, row_step = count > 2u ? 46.0f : 58.0f;
    for (unsigned i = 0u; i < count; ++i)
    {
        const float cy = rows_top + (float)i * row_step;
        const int state = i < current ? -1 : i == current ? 0 : 1;
        step_marker(x, mm_x(x, left + 20.0f), mm_y(x, cy), mm_s(x, 18.0f), state);
        const char *text = loading->phases[i] != NULL ? loading->phases[i] : "";
        UiTextStyle row = ui_text_style(state == 0 ? UI_FONT_BLACK : UI_FONT_BOLD, mm_s(x, 32.0f),
                                        state > 0 ? 0xb0ffffffu : MM_WHITE);
        row.shadow_color = 0x60002a5au;
        row.shadow_y = mm_s(x, 2.0f);
        row = ui_text_fit(&row, text, mm_s(x, width - 260.0f));
        ui_text_draw_centered(c, &row, mm_x(x, left + 62.0f), mm_y(x, cy), UI_ALIGN_LEFT, text, 1.0f);
    }
    char step[32];
    snprintf(step, sizeof step, "Step %u of %u", count ? current + 1u : 0u, count);
    UiTextStyle counter = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), MM_YELLOW);
    counter.shadow_color = 0x80002a5au;
    counter.shadow_y = mm_s(x, 2.0f);
    ui_text_draw_centered(c, &counter, mm_x(x, left + width), mm_y(x, rows_top), UI_ALIGN_RIGHT, step, 1.0f);
    progress_bar(x, left, CARD_Y + CARD_H - 74.0f, width, 26.0f, current, count);
}

void modern_menu_loading_live_rect(int width, int height, int rect[4])
{
    UiCanvas canvas = {.width = width, .height = height};
    ModernMenu menu = {0};
    const ModernMenuView view = {0};
    MmCtx x;
    mm_context_init(&x, &menu, &view, NULL, &canvas, 0u, 0u);
    float left, top, right, bottom;
    card_bounds(&x, &left, &top, &right, &bottom);
    rect[0] = left < 0.0f ? 0 : (int)left;
    rect[1] = top < 0.0f ? 0 : (int)top;
    rect[2] = right > (float)width ? width : (int)right;
    rect[3] = bottom > (float)height ? height : (int)bottom;
}

int modern_menu_render_loading(ModernMenu *menu, const ModernMenuLoading *loading, UiCanvas *canvas, int live)
{
    if (menu == NULL || loading == NULL || canvas == NULL || (canvas->pixels == NULL && canvas->sink == NULL) ||
        canvas->width <= 0 || canvas->height <= 0 || canvas->stride < canvas->width ||
        (loading->phase_count != 0u && loading->phases == NULL))
        return 0;
    const ModernMenuView view = {.gamepad = loading->gamepad};
    MmCtx x;
    loading_context(&x, menu, &view, canvas, loading->tick);
    ui_canvas_unclip(canvas);
    if (!live)
    {
        still_layer(&x, loading);
        return 1;
    }
    int rect[4];
    modern_menu_loading_live_rect(canvas->width, canvas->height, rect);
    ui_canvas_clip(canvas, rect[0], rect[1], rect[2], rect[3]);
    live_layer(&x, loading);
    ui_canvas_unclip(canvas);
    return 1;
}
