#include "modern_menu_internal.h"

#include <math.h>
#include <string.h>

/* Native settings pages drawn over the current menu: category tabs, a list of
 * rows with value pills, and the highlighted row's help line. Rows arrive
 * already formatted by the shell, which also owns editing and saving. */

static void value_pill(MmCtx *x, float right, float cy, const ModernMenuOptionRow *row, int selected, int capturing,
                       float alpha)
{
    UiCanvas *c = x->c;
    const char *value = capturing ? "Press a key..." : row->value;
    UiTextStyle text = ui_text_style(UI_FONT_BLACK, mm_s(x, 32.0f), selected ? 0xff3a2a00u : MM_INK);
    text = ui_text_fit(&text, value, mm_s(x, 380.0f));
    const float h = mm_s(x, 56.0f), arrow = row->adjustable && selected ? mm_s(x, 40.0f) : 0.0f;
    const float w = fmaxf(ui_text_width(&text, value) + mm_s(x, 52.0f) + arrow * 2.0f, mm_s(x, 200.0f));
    const float px = right - w, py = cy - h * 0.5f;
    UiPaint pill = selected ? ui_paint_solid(capturing ? 0xffff8a3du : 0xfffffbe0u) : ui_paint_solid(0xd8ffffffu);
    if (capturing)
    {
        const float pulse = 0.65f + 0.35f * sinf(x->t * 8.0f);
        alpha *= pulse;
    }
    ui_fill_round_rect(c, px, py, w, h, h * 0.5f, 0.0f, &pill, 1.2f, alpha);
    ui_text_draw_centered(c, &text, px + w * 0.5f, cy, UI_ALIGN_CENTER, value, alpha);
    if (arrow > 0.0f && !capturing)
    {
        UiPaint ink = ui_paint_solid(0xffe7a300u);
        const float a = mm_s(x, 12.0f), nudge = sinf(x->t * 6.0f) * mm_s(x, 2.5f);
        const float lx = px + mm_s(x, 28.0f) - nudge, rx = px + w - mm_s(x, 28.0f) + nudge;
        const UiPoint left[3] = {{lx - a, cy}, {lx + a * 0.6f, cy - a}, {lx + a * 0.6f, cy + a}};
        const UiPoint right_arrow[3] = {{rx + a, cy}, {rx - a * 0.6f, cy + a}, {rx - a * 0.6f, cy - a}};
        ui_fill_convex(c, left, 3, &ink, 1.2f, alpha);
        ui_fill_convex(c, right_arrow, 3, &ink, 1.2f, alpha);
    }
}

void mm_options_overlay(MmCtx *x, const ModernMenuOptions *options)
{
    UiCanvas *c = x->c;
    if (options == NULL || options->page == MODERN_MENU_OPTIONS_CLOSED) return;
    mm_header(x, options->title != NULL ? options->title : "Options", NULL);
    const int tabbed = options->page != MODERN_MENU_OPTIONS_PAUSE_GRAPHICS;
    /* Category tabs. */
    static const char *const tabs[5] = {"Game", "Controls", "Enhancements", "Mods", "Games"};
    const float tab_y = 170.0f;
    float tab_x = 160.0f;
    for (unsigned tab = 0u; tabbed && tab < 5u; ++tab)
    {
        const int on = (unsigned)options->page == tab + 1u;
        UiTextStyle text = ui_text_style(UI_FONT_BLACK, mm_s(x, 32.0f), on ? 0xff3a2a00u : MM_WHITE);
        const float w = ui_text_width(&text, tabs[tab]) / x->u + 70.0f;
        UiPaint pill = on ? ui_paint_linear(0.0f, mm_y(x, tab_y), 0xfffff38cu, 0.0f, mm_y(x, tab_y + 60.0f), 0xffffc21au)
                          : ui_paint_solid(0x50002a5au);
        ui_fill_round_rect(c, mm_x(x, tab_x), mm_y(x, tab_y), mm_s(x, w), mm_s(x, 60.0f), mm_s(x, 30.0f), 0.25f, &pill, 1.2f,
                           1.0f);
        ui_text_draw_centered(c, &text, mm_x(x, tab_x + w * 0.5f), mm_y(x, tab_y + 30.0f), UI_ALIGN_CENTER, tabs[tab], 1.0f);
        tab_x += w + 16.0f;
    }
    if (options->page == MODERN_MENU_OPTIONS_MODS)
    {
        mm_mods_overlay(x, options->mods);
        return;
    }
    if (options->page == MODERN_MENU_OPTIONS_GAMES)
    {
        mm_games_overlay(x, options->games);
        return;
    }
    /* Row list in a glass panel, scrolled to keep the cursor visible. */
    const float px = 140.0f, py = tabbed ? 252.0f : 190.0f, pw = 1640.0f, ph = tabbed ? 680.0f : 740.0f, row_h = 84.0f;
    mm_panel(x, px, py, pw, ph, 30.0f, 1.0f);
    UiPaint deep = ui_paint_linear(0.0f, mm_y(x, py), 0xe0083a78u, 0.0f, mm_y(x, py + ph), 0xe8042650u);
    ui_fill_round_rect(c, mm_x(x, px + 6.0f), mm_y(x, py + 6.0f), mm_s(x, pw - 12.0f), mm_s(x, ph - 12.0f), mm_s(x, 26.0f),
                       0.0f, &deep, 1.2f, 1.0f);
    const unsigned visible = (unsigned)((ph - 120.0f) / row_h);
    float target = 0.0f;
    if (options->row_count > visible && options->cursor + 2u > visible)
        target = (float)(options->cursor + 2u - visible);
    if (target > (float)(options->row_count - visible) && options->row_count > visible)
        target = (float)(options->row_count - visible);
    const float scroll = mm_follow(x, 6u, 100u + (unsigned)options->page, target, 0.35f);
    const int saved[4] = {c->clip_x0, c->clip_y0, c->clip_x1, c->clip_y1};
    const float list_top = py + 24.0f, list_bottom = list_top + row_h * (float)visible;
    ui_canvas_clip(c, saved[0], (int)mm_y(x, list_top), saved[2], (int)mm_y(x, list_bottom));
    const float cursor_y = mm_follow(x, 7u, 100u + (unsigned)options->page, (float)options->cursor, 0.45f);
    {
        /* Gliding highlight bar. */
        const float hy = list_top + (cursor_y - scroll) * row_h;
        UiPaint bar = ui_paint_linear(0.0f, mm_y(x, hy), 0xfffff38cu, 0.0f, mm_y(x, hy + row_h - 8.0f), 0xffffc61au);
        ui_fill_round_rect(c, mm_x(x, px + 24.0f), mm_y(x, hy + 4.0f), mm_s(x, pw - 48.0f), mm_s(x, row_h - 8.0f),
                           mm_s(x, 18.0f), 0.0f, &bar, 1.2f, 1.0f);
    }
    for (unsigned i = 0u; i < options->row_count && i < MODERN_MENU_OPTION_ROWS; ++i)
    {
        const float ry = list_top + ((float)i - scroll) * row_h;
        if (ry + row_h < list_top - row_h || ry > list_bottom) continue;
        const ModernMenuOptionRow *row = &options->rows[i];
        const int selected = i == options->cursor;
        const float cy = mm_y(x, ry + row_h * 0.5f);
        UiTextStyle label = ui_text_style(UI_FONT_BLACK, mm_s(x, 38.0f), selected ? 0xff3a2a00u : MM_WHITE);
        if (!selected)
        {
            label.shadow_color = 0x60002a5au;
            label.shadow_y = mm_s(x, 2.0f);
        }
        if (row->label != NULL)
            ui_text_draw_centered(c, &label, mm_x(x, px + 64.0f), cy, UI_ALIGN_LEFT, row->label, 1.0f);
        if (row->value[0] != '\0' || (selected && options->capturing))
            value_pill(x, mm_x(x, px + pw - 50.0f), cy, row, selected, selected && options->capturing, 1.0f);
    }
    ui_canvas_clip(c, saved[0], saved[1], saved[2], saved[3]);
    /* Scroll indicator. */
    if (options->row_count > visible)
    {
        const float track_h = row_h * (float)visible, knob_h = track_h * (float)visible / (float)options->row_count;
        const float knob_y = list_top + (track_h - knob_h) * scroll / (float)(options->row_count - visible);
        UiPaint track = ui_paint_solid(0x30ffffffu), knob = ui_paint_solid(0xc0ffffffu);
        ui_fill_round_rect(c, mm_x(x, px + pw - 22.0f), mm_y(x, list_top), mm_s(x, 8.0f), mm_s(x, track_h), mm_s(x, 4.0f),
                           0.0f, &track, 1.2f, 1.0f);
        ui_fill_round_rect(c, mm_x(x, px + pw - 22.0f), mm_y(x, knob_y), mm_s(x, 8.0f), mm_s(x, knob_h), mm_s(x, 4.0f), 0.0f,
                           &knob, 1.2f, 1.0f);
    }
    /* Help and notice under the list. */
    const ModernMenuOptionRow *current = options->cursor < options->row_count ? &options->rows[options->cursor] : NULL;
    UiTextStyle help = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), MM_WHITE);
    help.shadow_color = 0x80001a40u;
    help.shadow_y = mm_s(x, 2.0f);
    if (current != NULL && current->help != NULL)
        ui_text_draw_wrapped(c, &help, mm_x(x, px + 64.0f), mm_y(x, py + ph - 70.0f), mm_s(x, pw - 128.0f), mm_s(x, 36.0f), 2,
                             UI_ALIGN_LEFT, current->help, 1.0f);
    if (options->notice != NULL && options->notice[0] != '\0')
    {
        UiTextStyle notice = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), MM_YELLOW);
        notice.shadow_color = 0x80001a40u;
        notice.shadow_y = mm_s(x, 2.0f);
        ui_text_draw_centered(c, &notice, mm_x(x, px + pw - 50.0f), mm_y(x, py + ph + 36.0f), UI_ALIGN_RIGHT, options->notice,
                              1.0f);
    }
    if (options->capturing)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Cancel"}};
        mm_footer(x, "Press the new key or button", hints, 1u);
    }
    else if (!tabbed)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_DPAD, "Change"}};
        mm_footer(x, options->hint, hints, 2u);
    }
    else if (options->page == MODERN_MENU_OPTIONS_BINDINGS)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_LR, "Category"}, {MM_BUTTON_A, "Rebind"}};
        mm_footer(x, options->hint, hints, 3u);
    }
    else
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_LR, "Category"}, {MM_BUTTON_DPAD, "Change"},
                                {MM_BUTTON_A, "Select"}};
        mm_footer(x, options->hint, hints, 4u);
    }
}
