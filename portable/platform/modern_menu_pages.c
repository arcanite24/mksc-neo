#include "modern_menu_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "character_menu_renderer.h"
#include "mod_archive.h"
#include "title_logo.h"

/* Stock preview animations by grid slot (Random has none). */
static uint32_t stock_preview(unsigned slot)
{
    return PORTABLE_CHARACTER_PREVIEW + slot;
}

void mm_kart_showcase(MmCtx *x, uint32_t preview_id, float cx, float cy, float size, float alpha,
                      uint32_t tick, float hop)
{
    UiCanvas *c = x->c;
    /* Pedestal: dark disc with a lit rim, MK8's character stand. */
    const float rx = size * 0.62f, ry = size * 0.17f, base_y = cy + size * 0.36f;
    UiPaint shadow = ui_paint_solid(0x60001830u);
    ui_fill_ellipse(c, cx, base_y + size * 0.07f, rx * 1.04f, ry * 1.2f, &shadow, mm_s(x, 30.0f), alpha);
    UiPaint side = ui_paint_linear(0.0f, base_y, 0xff2a3440u, 0.0f, base_y + size * 0.08f, 0xff11161cu);
    ui_fill_ellipse(c, cx, base_y + size * 0.05f, rx, ry, &side, 1.2f, alpha);
    UiPaint top = ui_paint_linear(cx - rx, base_y - ry, 0xff58626eu, cx + rx, base_y + ry, 0xff2c333cu);
    ui_fill_ellipse(c, cx, base_y, rx, ry, &top, 1.2f, alpha);
    UiPaint inner = ui_paint_linear(0.0f, base_y - ry, 0xff3b444fu, 0.0f, base_y + ry, 0xff4b5561u);
    ui_fill_ellipse(c, cx, base_y, rx * 0.84f, ry * 0.8f, &inner, 1.2f, alpha);
    UiPaint glow = ui_paint_solid(0x5038b8ffu);
    ui_fill_ellipse(c, cx, base_y, rx * 0.7f, ry * 0.55f, &glow, mm_s(x, 26.0f), alpha);
    const uint32_t frame = ui_art_animation_frame(x->view->pack, preview_id, tick);
    /* Sprite art reads best crisp: whole-pixel nearest scaling, not smoothing. */
    const UiImage *kart = frame != 0u ? ui_art_image(x->menu->art, x->view->pack, frame, 0) : NULL;
    if (kart == NULL) return;
    /* The 64px preview's kart sits low in its frame; anchor its base. */
    float draw = size * 1.06f;
    const float whole = floorf(draw / (float)kart->width);
    if (whole >= 1.0f) draw = whole * (float)kart->width;
    const float lift = hop * size * 0.18f;
    UiPaint drop = ui_paint_solid(0x70000000u);
    ui_fill_ellipse(c, cx, base_y - ry * 0.1f, size * (0.3f - hop * 0.08f), ry * 0.42f, &drop, mm_s(x, 16.0f), alpha);
    ui_draw_image(c, kart, floorf(cx - draw * 0.5f), floorf(base_y - draw * 0.86f - lift), draw, draw, alpha,
                  UI_IMAGE_NEAREST, MM_WHITE, 0.0f);
}

/* ---- Title -------------------------------------------------------------- */

/* The project logo, inflated once from the build-time header. */
const UiImage *modern_menu_logo_image(void)
{
    static uint32_t pixels[TITLE_LOGO_WIDTH * TITLE_LOGO_HEIGHT];
    static UiImage image;
    static int state; /* 0 untried, 1 ready, -1 failed. */
    if (state == 0)
    {
        uint8_t *bytes = (uint8_t *)pixels;
        state = portable_inflate(title_logo_deflate, sizeof title_logo_deflate, bytes, sizeof pixels) ? 1 : -1;
        if (state < 0)
            fprintf(stderr, "Title logo: embedded image failed to inflate\n");
        /* Little-endian ARGB words, decoded in place on any host. */
        for (size_t i = 0u; state > 0 && i < sizeof pixels / 4u; ++i)
            pixels[i] = (uint32_t)bytes[i * 4u] | (uint32_t)bytes[i * 4u + 1u] << 8 |
                        (uint32_t)bytes[i * 4u + 2u] << 16 | (uint32_t)bytes[i * 4u + 3u] << 24;
        image = (UiImage){pixels, TITLE_LOGO_WIDTH, TITLE_LOGO_HEIGHT};
    }
    return state > 0 ? &image : NULL;
}

void mm_title_logo(MmCtx *x, float cx, float cy, float scale, float alpha)
{
    const UiImage *logo = modern_menu_logo_image();
    if (logo == NULL) return;
    const float bob = sinf(x->t * 1.6f) * mm_s(x, 6.0f);
    const float w = mm_s(x, 1040.0f * scale), h = w * (float)logo->height / (float)logo->width;
    /* cy is the top line of the old two-line wordmark; centre slightly below it. */
    const float left = cx - w * 0.5f, top = cy + bob + mm_s(x, 50.0f * scale) - h * 0.5f;
    ui_draw_image(x->c, logo, left + mm_s(x, 6.0f * scale), top + mm_s(x, 12.0f * scale), w, h, alpha * 0.45f,
                  UI_IMAGE_SILHOUETTE, 0xff001a40u, 0.0f);
    ui_draw_image(x->c, logo, left, top, w, h, alpha, UI_IMAGE_SMOOTH, MM_WHITE, 0.0f);
}

/* Cycles the stock drivers through the title's showcase. */
static void title_showcase(MmCtx *x, float cx, float cy, float size, float alpha)
{
    static const uint8_t slots[8] = {0u, 1u, 2u, 3u, 5u, 6u, 7u, 8u};
    const uint32_t period = 300u;
    const uint32_t index = (x->tick / period) % 8u, phase = x->tick % period;
    const float in = mm_ease_back(mm_clamp01((float)phase / 24.0f));
    const float out = mm_clamp01(((float)phase - (float)(period - 16u)) / 16.0f);
    const float slide = (1.0f - in) * mm_s(x, 260.0f) - out * mm_s(x, 200.0f);
    mm_kart_showcase(x, stock_preview(slots[index]), cx + slide, cy, size, alpha * (1.0f - out), x->tick, 0.0f);
}

int mm_page_title(MmCtx *x, int intro)
{
    const PortableFrontendFlow *flow = x->flow;
    mm_background(x, 1);
    UiCanvas *c = x->c;
    const float hero_x = mm_x(x, x->right - 470.0f), hero_y = mm_y(x, 500.0f);
    title_showcase(x, hero_x, hero_y, mm_s(x, 640.0f), mm_ease_out(mm_stagger(x, 6.0f, 20.0f)));
    if (intro)
    {
        const float appear = mm_ease_back(mm_stagger(x, 4.0f, 26.0f));
        mm_title_logo(x, mm_x(x, 720.0f), mm_y(x, 430.0f), 0.6f + 0.4f * appear, mm_clamp01(appear * 1.4f));
        const float blink = 0.55f + 0.45f * sinf(x->t * 4.2f);
        UiTextStyle press = ui_text_style(UI_FONT_BLACK, mm_s(x, 54.0f), MM_WHITE);
        press.outline = mm_s(x, 5.0f);
        press.outline_color = 0xb0003a78u;
        press.shadow_color = 0x60001a40u;
        press.shadow_y = mm_s(x, 5.0f);
        press.shadow_blur = mm_s(x, 8.0f);
        const float py = mm_y(x, 820.0f), glyph_h = mm_s(x, 64.0f);
        const float word = ui_text_width(&press, "Press");
        const float glyph_w = mm_button_glyph_width(x, glyph_h, MM_BUTTON_A);
        const float total = word + mm_s(x, 22.0f) + glyph_w;
        const float start = mm_x(x, 720.0f) - total * 0.5f;
        const float alpha = mm_stagger(x, 24.0f, 12.0f) * blink;
        ui_text_draw_centered(c, &press, start, py, UI_ALIGN_LEFT, "Press", alpha);
        mm_button_glyph(x, start + word + mm_s(x, 22.0f), py, glyph_h, MM_BUTTON_A, alpha);
        UiTextStyle note = ui_text_style(UI_FONT_BOLD, mm_s(x, 26.0f), 0xd0ffffffu);
        note.shadow_color = 0x60001a40u;
        note.shadow_y = mm_s(x, 2.0f);
        ui_text_draw(c, &note, mm_x(x, x->left + 40.0f), (float)c->height - mm_s(x, 32.0f), UI_ALIGN_LEFT,
                     "MKSC Neo \xc2\xb7 native port of Mario Kart: Super Circuit (USA)", mm_stagger(x, 30.0f, 20.0f));
        return 1;
    }
    /* Title menu: logo shrinks to the top-left, tabs fill the left column. */
    mm_title_logo(x, mm_x(x, 520.0f), mm_y(x, 176.0f), 0.52f, 1.0f);
    static const char *const labels[4] = {"Single Player", "Multiplayer", "Ghost Exchange", "Options"};
    static const MmIcon icons[4] = {MM_ICON_PERSON, MM_ICON_PEOPLE, MM_ICON_GHOST, MM_ICON_GEAR};
    char badge[16];
    for (unsigned row = 0u; row < 4u; ++row)
    {
        const float appear = mm_ease_out(mm_stagger(x, 2.0f + (float)row * 3.0f, 14.0f));
        const float slide = (1.0f - appear) * -520.0f;
        const char *row_badge = row == 0u ? "1P" : row == 1u ? "2-4P" : NULL;
        if (row == 1u && flow->player_count_open)
        {
            snprintf(badge, sizeof badge, "< %uP >", (unsigned)flow->players);
            row_badge = badge;
        }
        mm_tab(x, 170.0f + slide, 330.0f + (float)row * 150.0f, 760.0f, 118.0f, labels[row], row_badge,
               flow->title_cursor == row, appear, icons[row], 0u, 0);
    }
    static const char *const blurbs[4] = {
        "Grand Prix, Time Trial, VS and Battle on your own.",
        "Split-screen racing and battles for 2 to 4 players.",
        "Export your Time Trial ghosts or load ones from friends.",
        "Video, audio, controls, enhancements and mods."};
    UiTextStyle blurb = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), MM_WHITE);
    blurb.shadow_color = 0x70001a40u;
    blurb.shadow_y = mm_s(x, 3.0f);
    blurb.shadow_blur = mm_s(x, 4.0f);
    const char *blurb_text = flow->player_count_open ? "Choose how many players will race." : blurbs[flow->title_cursor & 3u];
    const float blurb_alpha = mm_stagger(x, 10.0f, 12.0f);
    UiPaint pill = ui_paint_solid(0x70032048u);
    ui_fill_round_rect(c, mm_x(x, 176.0f), mm_y(x, 914.0f), ui_text_width(&blurb, blurb_text) + mm_s(x, 48.0f),
                       mm_s(x, 52.0f), mm_s(x, 26.0f), 0.25f, &pill, 1.2f, blurb_alpha);
    ui_text_draw_centered(c, &blurb, mm_x(x, 200.0f), mm_y(x, 940.0f), UI_ALIGN_LEFT, blurb_text, blurb_alpha);
    if (flow->player_count_open)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_DPAD, "Players"}, {MM_BUTTON_A, "OK"}};
        mm_footer(x, NULL, hints, 3u);
    }
    else
    {
        const MmHint hints[] = {{MM_BUTTON_A, "OK"}};
        mm_footer(x, NULL, hints, 1u);
    }
    return 1;
}

/* ---- Mode and class ---------------------------------------------------- */

static void info_card(MmCtx *x, MmIcon icon, const char *title, const char *body, const char *detail)
{
    const float appear = mm_ease_out(mm_stagger(x, 6.0f, 16.0f));
    const float dx = 1110.0f + (1.0f - appear) * 200.0f, dy = 300.0f, dw = 660.0f, dh = 560.0f;
    mm_panel(x, dx, dy, dw, dh, 28.0f, appear);
    UiCanvas *c = x->c;
    const float cx = mm_x(x, dx + dw * 0.5f), cy = mm_y(x, dy + 170.0f), r = mm_s(x, 110.0f);
    const float spin = sinf(x->t * 2.0f) * 0.04f;
    UiPaint disc = ui_paint_linear(0.0f, cy - r, 0xffff7048u, 0.0f, cy + r, 0xffd3161cu);
    ui_fill_circle(c, cx, cy, r * (1.0f + spin), &disc, 1.2f, appear);
    ui_stroke_circle(c, cx, cy, r * (1.0f + spin), mm_s(x, 8.0f), 0xc0ffffffu, appear);
    mm_icon(x, icon, cx, cy, r * 0.72f, MM_WHITE, appear);
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 58.0f), MM_WHITE);
    head.shadow_color = 0x70002a5au;
    head.shadow_y = mm_s(x, 4.0f);
    head.shadow_blur = mm_s(x, 6.0f);
    head = ui_text_fit(&head, title, mm_s(x, dw - 60.0f));
    ui_text_draw_centered(c, &head, cx, mm_y(x, dy + 345.0f), UI_ALIGN_CENTER, title, appear);
    UiTextStyle text = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), 0xff17324du);
    ui_text_draw_wrapped(c, &text, cx, mm_y(x, dy + 410.0f), mm_s(x, dw - 90.0f), mm_s(x, 40.0f), 3,
                         UI_ALIGN_CENTER, body, appear);
    if (detail != NULL)
    {
        UiTextStyle small = ui_text_style(UI_FONT_BLACK, mm_s(x, 26.0f), 0xff0b5fb8u);
        ui_text_draw(c, &small, cx, mm_y(x, dy + dh - 34.0f), UI_ALIGN_CENTER, detail, appear);
    }
}

/* Engine-class speedometer: arc segments light up to the selected class. */
static void class_gauge(MmCtx *x, unsigned speed)
{
    const float appear = mm_ease_out(mm_stagger(x, 6.0f, 16.0f));
    const float dx = 1110.0f + (1.0f - appear) * 200.0f, dy = 300.0f, dw = 660.0f, dh = 560.0f;
    mm_panel(x, dx, dy, dw, dh, 28.0f, appear);
    UiCanvas *c = x->c;
    const float cx = mm_x(x, dx + dw * 0.5f), cy = mm_y(x, dy + 330.0f), r = mm_s(x, 230.0f);
    const float target = (float)(speed + 1u) / 3.0f;
    const float level = mm_follow(x, 0u, 77u, target, 0.18f);
    for (unsigned i = 0u; i < 15u; ++i)
    {
        const float f = ((float)i + 0.5f) / 15.0f;
        const float a0 = 3.4f + (float)i / 15.0f * 2.62f, a1 = a0 + 2.62f / 15.0f * 0.72f;
        const float inner = r * 0.74f, outer = r;
        const UiPoint seg[4] = {{cx + cosf(a0) * inner, cy + sinf(a0) * inner}, {cx + cosf(a0) * outer, cy + sinf(a0) * outer},
                                {cx + cosf(a1) * outer, cy + sinf(a1) * outer}, {cx + cosf(a1) * inner, cy + sinf(a1) * inner}};
        const uint32_t lit = f < 0.34f ? 0xff3ddc6au : f < 0.67f ? 0xffffc61au : 0xffff4a3au;
        UiPaint paint = ui_paint_solid(f <= level ? lit : 0x50ffffffu);
        ui_fill_convex(c, seg, 4, &paint, 1.2f, appear);
    }
    const float needle = 3.4f + level * 2.62f;
    UiPaint ink = ui_paint_solid(0xff22303cu);
    const float nx = cosf(needle), ny = sinf(needle), w = mm_s(x, 9.0f);
    const UiPoint hand[4] = {{cx - ny * w, cy + nx * w}, {cx + nx * r * 0.86f, cy + ny * r * 0.86f},
                             {cx + ny * w, cy - nx * w}, {cx - nx * r * 0.12f, cy - ny * r * 0.12f}};
    ui_fill_convex(c, hand, 4, &ink, 1.2f, appear);
    ui_fill_circle(c, cx, cy, mm_s(x, 22.0f), &ink, 1.2f, appear);
    static const char *const names[3] = {"50cc", "100cc", "150cc"};
    static const char *const notes[3] = {"Relaxed speed for learning the courses.",
                                         "Faster karts and sharper rivals.",
                                         "Full speed. The real challenge."};
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 76.0f), MM_WHITE);
    head.shadow_color = 0x70002a5au;
    head.shadow_y = mm_s(x, 4.0f);
    head.shadow_blur = mm_s(x, 6.0f);
    ui_text_draw_centered(c, &head, cx, mm_y(x, dy + 420.0f), UI_ALIGN_CENTER, names[speed % 3u], appear);
    UiTextStyle text = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), 0xff17324du);
    ui_text_draw(c, &text, cx, mm_y(x, dy + 500.0f), UI_ALIGN_CENTER, notes[speed % 3u], appear);
}

int mm_page_mode(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    mm_background(x, 0);
    const int multi = flow->players > 1u;
    char players[24];
    snprintf(players, sizeof players, "%u Players", (unsigned)flow->players);
    if (flow->mode_phase == 0u)
    {
        mm_header(x, multi ? "Multiplayer" : "Single Player", multi ? players : NULL);
        const char *const labels[4] = {"Grand Prix", "Time Trial", multi ? "VS Race" : "Quick Run", "Battle"};
        static const MmIcon icons[4] = {MM_ICON_TROPHY, MM_ICON_CLOCK, MM_ICON_FLAG, MM_ICON_BALLOON};
        const unsigned first = multi ? 2u : 0u;
        for (unsigned mode = first; mode < 4u; ++mode)
        {
            const unsigned row = mode - first;
            const float appear = mm_ease_out(mm_stagger(x, (float)row * 3.0f, 14.0f));
            mm_tab(x, 150.0f + (1.0f - appear) * -560.0f, 290.0f + (float)row * 150.0f, 820.0f, 118.0f,
                   labels[mode], NULL, flow->mode_cursor == mode, appear, icons[mode], 0u, 0);
        }
        static const char *const bodies[4] = {
            "Race four-course cups against seven rivals. Win gold trophies to unlock new cups.",
            "Race alone against the clock. Set records and chase your ghost.",
            "Pick any course and race by your own rules.",
            "Pop your rivals' balloons in the battle arenas."};
        const char *body = multi && flow->mode_cursor == 2u ? "Race your friends on any course, split-screen."
                                                             : bodies[flow->mode_cursor & 3u];
        info_card(x, icons[flow->mode_cursor & 3u], labels[flow->mode_cursor & 3u], body,
                  flow->mode_cursor == 2u || (flow->mode_cursor == 1u && !multi) ? "Rules can be changed" : NULL);
        const int rules = (!multi && flow->mode_cursor == 1u) || flow->mode_cursor == 2u;
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, "OK"},
                                {MM_BUTTON_SELECT, flow->mode_cursor == 1u ? "Ghosts" : "Rules"}};
        mm_footer(x, NULL, hints, rules ? 3u : 2u);
        if (flow->quick_options_open) mm_quick_options(x);
        return 1;
    }
    const char *title, *subtitle;
    mm_mode_names(flow, &title, &subtitle);
    mm_header(x, title, multi ? players : NULL);
    static const char *const classes[3] = {"50cc", "100cc", "150cc"};
    for (unsigned speed = 0u; speed < 3u; ++speed)
    {
        const float appear = mm_ease_out(mm_stagger(x, (float)speed * 3.0f, 14.0f));
        char badge[8];
        snprintf(badge, sizeof badge, "%s", speed == 0u ? "Easy" : speed == 1u ? "Normal" : "Hard");
        mm_tab(x, 150.0f + (1.0f - appear) * -560.0f, 330.0f + (float)speed * 160.0f, 820.0f, 124.0f,
               classes[speed], badge, flow->race_speed == speed, appear, MM_ICON_NONE, 0u, 0);
    }
    class_gauge(x, flow->race_speed);
    const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, "OK"}};
    mm_footer(x, NULL, hints, 2u);
    return 1;
}

int mm_page_time_trial(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    mm_background(x, 0);
    mm_header(x, "Time Trial", NULL);
    static const char *const labels[2] = {"Start Race", "Records & Ghosts"};
    static const MmIcon icons[2] = {MM_ICON_PLAY, MM_ICON_LIST};
    for (unsigned row = 0u; row < 2u; ++row)
    {
        const float appear = mm_ease_out(mm_stagger(x, (float)row * 3.0f, 14.0f));
        mm_tab(x, 150.0f + (1.0f - appear) * -560.0f, 380.0f + (float)row * 160.0f, 820.0f, 124.0f, labels[row],
               NULL, flow->time_trial_cursor == row, appear, icons[row], 0u, 0);
    }
    info_card(x, flow->time_trial_cursor ? MM_ICON_LIST : MM_ICON_CLOCK, labels[flow->time_trial_cursor & 1u],
              flow->time_trial_cursor ? "Browse your best times on every course, export ghosts or erase records."
                                      : "Pick a driver and a course, then race three laps against the clock.",
              NULL);
    const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, "OK"}};
    mm_footer(x, NULL, hints, 2u);
    return 1;
}

/* ---- Rules popup --------------------------------------------------------- */

static void toggle(MmCtx *x, float px, float py, float h, int on, float alpha)
{
    const float w = h * 1.9f;
    const float knob = on ? 1.0f : 0.0f;
    UiPaint track = ui_paint_solid(ui_mix(0xffb8c4cfu, 0xff3ddc6au, knob));
    ui_fill_round_rect(x->c, px, py - h * 0.5f, w, h, h * 0.5f, 0.0f, &track, 1.2f, alpha);
    UiPaint white = ui_paint_solid(MM_WHITE);
    ui_fill_circle(x->c, px + h * 0.5f + knob * (w - h), py, h * 0.4f, &white, 1.2f, alpha);
}

void mm_quick_options(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    ui_tint_rect(c, 0, 0, c->width, c->height, 0xff001830u, 0.45f);
    const int vs = flow->players > 1u, ghosts = flow->mode_cursor == 1u;
    const unsigned rows = vs ? 4u : ghosts ? 2u : 3u;
    const PortableQuickRunOptions *options = vs ? &flow->vs_options : &flow->quick_run_options;
    const float dw = 900.0f, row_h = 104.0f, dh = 200.0f + row_h * (float)rows;
    const float dx = 960.0f - dw * 0.5f, dy = 540.0f - dh * 0.5f - 30.0f;
    mm_panel(x, dx, dy, dw, dh, 30.0f, 1.0f);
    UiPaint face = ui_paint_linear(0.0f, mm_y(x, dy), 0xf0ffffffu, 0.0f, mm_y(x, dy + dh), 0xf0e2f1fau);
    ui_fill_round_rect(c, mm_x(x, dx), mm_y(x, dy), mm_s(x, dw), mm_s(x, dh), mm_s(x, 30.0f), 0.0f, &face, 1.2f, 1.0f);
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 54.0f), 0xff0b5fb8u);
    ui_text_draw_centered(c, &head, mm_x(x, 960.0f), mm_y(x, dy + 80.0f), UI_ALIGN_CENTER,
                          ghosts ? "Ghost Display" : "Race Rules", 1.0f);
    for (unsigned row = 0u; row < rows; ++row)
    {
        const char *label;
        int on;
        if (ghosts)
        {
            label = row == 0u ? "Show my ghost" : "Show received ghost";
            on = ((flow->ghost_display_off >> row) & 1u) == 0u;
        }
        else
        {
            const unsigned field = row == 2u && !vs ? 3u : row;
            label = field == 0u ? "5 laps" : field == 1u ? "Items" : field == 2u ? "Mushroom start" : "Coins";
            on = field == 0u ? options->five_laps != 0u : field == 1u ? options->items_off == 0u
                 : field == 2u ? options->mushroom_start != 0u : options->coins_off == 0u;
        }
        const float ry = dy + 150.0f + (float)row * row_h;
        const int selected = flow->quick_options_cursor == row;
        if (selected)
        {
            UiPaint bar = ui_paint_linear(0.0f, mm_y(x, ry), 0xfffff38cu, 0.0f, mm_y(x, ry + row_h - 14.0f), 0xffffc61au);
            ui_fill_round_rect(c, mm_x(x, dx + 40.0f), mm_y(x, ry), mm_s(x, dw - 80.0f), mm_s(x, row_h - 14.0f),
                               mm_s(x, 16.0f), 0.0f, &bar, 1.2f, 1.0f);
        }
        UiTextStyle text = ui_text_style(UI_FONT_BLACK, mm_s(x, 40.0f), selected ? 0xff3a2a00u : MM_INK);
        const float mid = mm_y(x, ry + (row_h - 14.0f) * 0.5f);
        ui_text_draw_centered(c, &text, mm_x(x, dx + 80.0f), mid, UI_ALIGN_LEFT, label, 1.0f);
        UiTextStyle state = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), selected ? 0xff5a3a00u : MM_INK_SOFT);
        ui_text_draw_centered(c, &state, mm_x(x, dx + dw - 270.0f), mid, UI_ALIGN_RIGHT, on ? "On" : "Off", 1.0f);
        toggle(x, mm_x(x, dx + dw - 240.0f), mid, mm_s(x, 56.0f), on, 1.0f);
    }
    const MmHint hints[] = {{MM_BUTTON_DPAD, "Change"}, {MM_BUTTON_A, "Done"}};
    mm_footer(x, NULL, hints, 2u);
}
