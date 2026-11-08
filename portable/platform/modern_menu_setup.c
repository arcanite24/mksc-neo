#include "modern_menu_internal.h"

#include <math.h>
#include <string.h>

/* First-start screens. They run before any asset pack exists, so they use
 * only vector shapes and the embedded fonts. */

static void cartridge(MmCtx *x, float cx, float cy, float scale, float alpha)
{
    UiCanvas *c = x->c;
    const float w = mm_s(x, 250.0f) * scale, h = mm_s(x, 200.0f) * scale;
    const float left = cx - w * 0.5f, top = cy - h * 0.5f;
    UiPaint shadow = ui_paint_solid(0x50001a40u);
    ui_fill_round_rect(c, left, top + h * 0.08f, w, h, w * 0.08f, 0.0f, &shadow, w * 0.08f, alpha);
    /* Grey shell with the GBA cartridge's notched top. */
    UiPaint shell = ui_paint_linear(0.0f, top, 0xff5d6470u, 0.0f, top + h, 0xff3a3f48u);
    ui_fill_round_rect(c, left, top, w, h, w * 0.07f, 0.0f, &shell, 1.2f, alpha);
    UiPaint notch = ui_paint_solid(0xff2b2f36u);
    ui_fill_round_rect(c, left + w * 0.08f, top + h * 0.05f, w * 0.84f, h * 0.06f, h * 0.02f, 0.0f, &notch, 1.2f, alpha);
    /* Label: the race-flag stripe and a red title band. */
    const float lx = left + w * 0.1f, ly = top + h * 0.18f, lw = w * 0.8f, lh = h * 0.66f;
    UiPaint label = ui_paint_linear(0.0f, ly, 0xff2f8cffu, 0.0f, ly + lh, 0xff0b4fb8u);
    ui_fill_round_rect(c, lx, ly, lw, lh, w * 0.04f, 0.0f, &label, 1.2f, alpha);
    UiPaint band = ui_paint_linear(0.0f, ly, 0xffff4b4bu, 0.0f, ly + lh * 0.3f, 0xffc8141au);
    ui_fill_round_rect(c, lx, ly + lh * 0.62f, lw, lh * 0.3f, w * 0.02f, 0.0f, &band, 1.2f, alpha);
    const float cell = lw / 10.0f;
    for (int row = 0; row < 2; ++row)
        for (int col = 0; col < 10; ++col)
            if (((row + col) & 1) == 0)
                ui_fill_rect(c, lx + (float)col * cell, ly + lh * 0.1f + (float)row * cell, cell, cell, 0xd0ffffffu, alpha);
    UiTextStyle text = ui_text_style(UI_FONT_ITALIC, lh * 0.2f, MM_WHITE);
    text.outline = lh * 0.02f;
    text.outline_color = 0xff7a0a0eu;
    ui_text_draw_centered(c, &text, cx, ly + lh * 0.77f, UI_ALIGN_CENTER, "MKSC", alpha);
}

static void spinner(MmCtx *x, float cx, float cy, float radius)
{
    for (int i = 0; i < 12; ++i)
    {
        const float angle = (float)i / 12.0f * 6.2831853f;
        const float phase = fmodf(x->t * 1.6f - (float)i / 12.0f + 1.0f, 1.0f);
        UiPaint dot = ui_paint_solid(ui_mix(MM_WHITE, MM_YELLOW, phase));
        ui_fill_circle(x->c, cx + cosf(angle) * radius, cy + sinf(angle) * radius, radius * (0.08f + 0.08f * phase), &dot,
                       1.2f, 0.4f + 0.6f * phase);
    }
}

static void status_line(MmCtx *x, const ModernMenuSetup *setup, float cy)
{
    if (setup->status == NULL || setup->status[0] == '\0') return;
    UiTextStyle text = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), setup->status_error ? MM_WHITE : 0xff3a2a00u);
    const float max_w = mm_s(x, 1500.0f);
    float w = ui_text_width(&text, setup->status) + mm_s(x, 64.0f);
    int lines = 1;
    if (w > max_w)
    {
        w = max_w;
        lines = 2;
    }
    const float h = mm_s(x, lines == 1 ? 60.0f : 100.0f);
    UiPaint pill = setup->status_error ? ui_paint_linear(0.0f, cy, 0xfff0424au, 0.0f, cy + h, 0xffc0182au)
                                       : ui_paint_linear(0.0f, cy, 0xfffff38cu, 0.0f, cy + h, 0xffffc61au);
    const float px = mm_x(x, 960.0f) - w * 0.5f;
    ui_fill_round_rect(x->c, px, cy - h * 0.5f, w, h, mm_s(x, 24.0f), 0.0f, &pill, 1.2f, 1.0f);
    ui_text_draw_wrapped(x->c, &text, mm_x(x, 960.0f), cy - h * 0.5f + mm_s(x, lines == 1 ? 30.0f : 32.0f),
                         w - mm_s(x, 64.0f), mm_s(x, 38.0f), 2, UI_ALIGN_CENTER, setup->status, 1.0f);
}

static void import_screen(MmCtx *x, const ModernMenuSetup *setup)
{
    UiCanvas *c = x->c;
    mm_background(x, 0);
    const float appear = mm_ease_back(mm_stagger(x, 0.0f, 22.0f));
    mm_title_logo(x, mm_x(x, 960.0f), mm_y(x, 190.0f), 0.62f * (0.8f + 0.2f * appear), mm_clamp01(appear * 1.5f));
    /* Drop zone: a dashed card the ROM can be dropped onto. */
    const float zx = 460.0f, zy = 330.0f, zw = 1000.0f, zh = 470.0f;
    const float zone_in = mm_ease_out(mm_stagger(x, 6.0f, 18.0f));
    mm_panel(x, zx, zy + (1.0f - zone_in) * 80.0f, zw, zh, 40.0f, zone_in);
    const float dash = 34.0f, gap = 20.0f, inset = 26.0f, march = fmodf(x->t * 30.0f, dash + gap);
    const float l = zx + inset, t = zy + inset + (1.0f - zone_in) * 80.0f, r = zx + zw - inset, b = t + zh - inset * 2.0f;
    for (float d = -dash + march; d < (r - l); d += dash + gap)
    {
        const float a = fmaxf(d, 0.0f), e = fminf(d + dash, r - l);
        if (e <= a) continue;
        ui_fill_rect(c, mm_x(x, l + a), mm_y(x, t), mm_s(x, e - a), mm_s(x, 5.0f), 0xd0ffffffu, zone_in);
        ui_fill_rect(c, mm_x(x, r - e), mm_y(x, b) - mm_s(x, 5.0f), mm_s(x, e - a), mm_s(x, 5.0f), 0xd0ffffffu, zone_in);
    }
    for (float d = -dash + march; d < (b - t); d += dash + gap)
    {
        const float a = fmaxf(d, 0.0f), e = fminf(d + dash, b - t);
        if (e <= a) continue;
        ui_fill_rect(c, mm_x(x, r) - mm_s(x, 5.0f), mm_y(x, t + a), mm_s(x, 5.0f), mm_s(x, e - a), 0xd0ffffffu, zone_in);
        ui_fill_rect(c, mm_x(x, l), mm_y(x, b - e), mm_s(x, 5.0f), mm_s(x, e - a), 0xd0ffffffu, zone_in);
    }
    const float bob = sinf(x->t * 2.4f) * 10.0f;
    cartridge(x, mm_x(x, 960.0f), mm_y(x, zy + 170.0f + bob), 0.9f, zone_in);
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 54.0f), MM_WHITE);
    head.shadow_color = 0x80002a5au;
    head.shadow_y = mm_s(x, 4.0f);
    head.shadow_blur = mm_s(x, 6.0f);
    ui_text_draw_centered(c, &head, mm_x(x, 960.0f), mm_y(x, zy + 330.0f), UI_ALIGN_CENTER, "Drop your game here",
                          zone_in);
    UiTextStyle body = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), MM_WHITE);
    body.shadow_color = 0x70002a5au;
    body.shadow_y = mm_s(x, 2.0f);
    ui_text_draw_centered(c, &body, mm_x(x, 960.0f), mm_y(x, zy + 396.0f), UI_ALIGN_CENTER,
                          setup->gamepad ? "Your own Mario Kart: Super Circuit (USA) .gba file \xe2\x80\x94 or press (A) to browse."
                                         : "Your own Mario Kart: Super Circuit (USA) .gba file \xe2\x80\x94 or press Enter to browse.",
                          zone_in);
    UiTextStyle note = ui_text_style(UI_FONT_BOLD, mm_s(x, 26.0f), 0xd8ffffffu);
    note.shadow_color = 0x60002a5au;
    note.shadow_y = mm_s(x, 2.0f);
    ui_text_draw_centered(c, &note, mm_x(x, 960.0f), mm_y(x, 860.0f), UI_ALIGN_CENTER,
                          "It is converted once on this computer. The ROM itself is never changed or shared.",
                          mm_stagger(x, 14.0f, 14.0f));
    status_line(x, setup, mm_y(x, 930.0f));
    const MmHint hints[] = {{MM_BUTTON_B, "Quit"}, {MM_BUTTON_A, "Choose File"}};
    mm_footer(x, NULL, hints, 2u);
}

static void importing_screen(MmCtx *x)
{
    UiCanvas *c = x->c;
    mm_background(x, 0);
    mm_title_logo(x, mm_x(x, 960.0f), mm_y(x, 260.0f), 0.7f, 1.0f);
    spinner(x, mm_x(x, 960.0f), mm_y(x, 560.0f), mm_s(x, 70.0f));
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 56.0f), MM_WHITE);
    head.shadow_color = 0x80002a5au;
    head.shadow_y = mm_s(x, 4.0f);
    head.shadow_blur = mm_s(x, 6.0f);
    ui_text_draw_centered(c, &head, mm_x(x, 960.0f), mm_y(x, 720.0f), UI_ALIGN_CENTER, "Building your game data", 1.0f);
    /* Indeterminate bar: a highlight sweeping along a track. */
    const float bx = 660.0f, by = 790.0f, bw = 600.0f, bh = 18.0f;
    UiPaint track = ui_paint_solid(0x50001a40u);
    ui_fill_round_rect(c, mm_x(x, bx), mm_y(x, by), mm_s(x, bw), mm_s(x, bh), mm_s(x, bh * 0.5f), 0.0f, &track, 1.2f, 1.0f);
    const float sweep = fmodf(x->t * 0.7f, 1.0f), seg = 0.3f;
    const float a = fmaxf(0.0f, sweep * (1.0f + seg) - seg), e = fminf(1.0f, sweep * (1.0f + seg));
    UiPaint bar = ui_paint_linear(mm_x(x, bx), 0.0f, 0xffffe55au, mm_x(x, bx + bw), 0.0f, 0xffff9c00u);
    if (e > a)
        ui_fill_round_rect(c, mm_x(x, bx + bw * a), mm_y(x, by), mm_s(x, bw * (e - a)), mm_s(x, bh), mm_s(x, bh * 0.5f), 0.0f,
                           &bar, 1.2f, 1.0f);
    UiTextStyle body = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), 0xe0ffffffu);
    ui_text_draw_centered(c, &body, mm_x(x, 960.0f), mm_y(x, 870.0f), UI_ALIGN_CENTER,
                          "Converting courses, karts, music and menus. This takes a moment.", 1.0f);
    mm_footer(x, NULL, NULL, 0u);
}

static void reimport_screen(MmCtx *x, const ModernMenuSetup *setup)
{
    UiCanvas *c = x->c;
    mm_background(x, 0);
    mm_header(x, "Update Game Data", NULL);
    const float appear = mm_ease_out(mm_stagger(x, 0.0f, 16.0f));
    mm_panel(x, 360.0f, 240.0f + (1.0f - appear) * 60.0f, 1200.0f, 560.0f, 36.0f, appear);
    cartridge(x, mm_x(x, 960.0f), mm_y(x, 380.0f), 0.7f, appear);
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 52.0f), MM_WHITE);
    head.shadow_color = 0x80002a5au;
    head.shadow_y = mm_s(x, 4.0f);
    head.shadow_blur = mm_s(x, 6.0f);
    ui_text_draw_centered(c, &head, mm_x(x, 960.0f), mm_y(x, 540.0f), UI_ALIGN_CENTER, "This version converts more of the game",
                          appear);
    UiTextStyle body = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), MM_WHITE);
    body.shadow_color = 0x70002a5au;
    body.shadow_y = mm_s(x, 2.0f);
    if (setup->detail != NULL)
        ui_text_draw_wrapped(c, &body, mm_x(x, 960.0f), mm_y(x, 610.0f), mm_s(x, 1080.0f), mm_s(x, 40.0f), 2,
                             UI_ALIGN_CENTER, setup->detail, appear);
    if (setup->rom_path != NULL && setup->rom_path[0] != '\0')
    {
        UiTextStyle path = ui_text_style(UI_FONT_BLACK, mm_s(x, 26.0f), 0xff17324du);
        path = ui_text_fit(&path, setup->rom_path, mm_s(x, 1040.0f));
        UiPaint chip = ui_paint_solid(0xc0ffffffu);
        const float w = ui_text_width(&path, setup->rom_path) + mm_s(x, 50.0f);
        ui_fill_round_rect(c, mm_x(x, 960.0f) - w * 0.5f, mm_y(x, 700.0f), w, mm_s(x, 52.0f), mm_s(x, 26.0f), 0.0f, &chip,
                           1.2f, appear);
        ui_text_draw_centered(c, &path, mm_x(x, 960.0f), mm_y(x, 726.0f), UI_ALIGN_CENTER, setup->rom_path, appear);
    }
    status_line(x, setup, mm_y(x, 880.0f));
    const MmHint hints[] = {{MM_BUTTON_B, "Keep Old Data"}, {MM_BUTTON_A, "Convert Again"}};
    mm_footer(x, NULL, hints, 2u);
}

static void preferences_screen(MmCtx *x, const ModernMenuSetup *setup)
{
    UiCanvas *c = x->c;
    mm_background(x, 0);
    mm_header(x, "Before You Race", NULL);
    UiTextStyle intro = ui_text_style(UI_FONT_BOLD, mm_s(x, 32.0f), MM_WHITE);
    intro.shadow_color = 0x80002a5au;
    intro.shadow_y = mm_s(x, 2.0f);
    ui_text_draw(c, &intro, mm_x(x, 160.0f), mm_y(x, 236.0f), UI_ALIGN_LEFT,
                 "Game data is ready. Pick a few basics \xe2\x80\x94 everything else lives in Options.",
                 mm_stagger(x, 2.0f, 12.0f));
    for (unsigned i = 0u; i <= setup->row_count; ++i)
    {
        const int start = i == setup->row_count, selected = i == setup->cursor;
        const float appear = mm_ease_out(mm_stagger(x, 2.0f + (float)i * 3.0f, 14.0f));
        const float dy = 300.0f + (float)i * 130.0f + (start ? 30.0f : 0.0f);
        const float dx = 150.0f + (1.0f - appear) * -500.0f;
        if (start)
        {
            mm_tab(x, dx, dy, 840.0f, 116.0f, "Start Racing!", NULL, selected, appear, MM_ICON_PLAY, 0u, 0);
            continue;
        }
        const ModernMenuOptionRow *row = &setup->rows[i];
        mm_tab(x, dx, dy, 840.0f, 116.0f, row->label, NULL, selected, appear,
               i == 0u ? MM_ICON_LIST : i == 1u ? MM_ICON_FLAG : MM_ICON_GEAR, 0u, 0);
        /* Value pill with arrows at the tab's right end. */
        UiTextStyle value = ui_text_style(UI_FONT_BLACK, mm_s(x, 34.0f), selected ? 0xff3a2a00u : 0xff0b4f9cu);
        const float right = mm_x(x, dx + 780.0f), cy = mm_y(x, dy + 58.0f);
        ui_text_draw_centered(c, &value, right - mm_s(x, 40.0f), cy, UI_ALIGN_RIGHT, row->value, appear);
        if (selected)
        {
            UiPaint ink = ui_paint_solid(0xffb06a00u);
            const float a = mm_s(x, 13.0f), w = ui_text_width(&value, row->value);
            const float nudge = sinf(x->t * 6.0f) * mm_s(x, 3.0f);
            const float lx = right - mm_s(x, 64.0f) - w - nudge, rx = right - mm_s(x, 16.0f) + nudge;
            const UiPoint left_arrow[3] = {{lx - a, cy}, {lx + a * 0.6f, cy - a}, {lx + a * 0.6f, cy + a}};
            const UiPoint right_arrow[3] = {{rx + a, cy}, {rx - a * 0.6f, cy + a}, {rx - a * 0.6f, cy - a}};
            ui_fill_convex(c, left_arrow, 3, &ink, 1.2f, appear);
            ui_fill_convex(c, right_arrow, 3, &ink, 1.2f, appear);
        }
    }
    /* Help card for the highlighted row. */
    const float card_in = mm_ease_out(mm_stagger(x, 8.0f, 16.0f));
    const float cx = 1110.0f + (1.0f - card_in) * 240.0f;
    mm_panel(x, cx, 300.0f, 660.0f, 520.0f, 30.0f, card_in);
    const int start = setup->cursor >= setup->row_count;
    const char *title = start ? "Let's go!" : setup->rows[setup->cursor].label;
    const char *help = start ? "Your settings are saved. You can change them any time from Options on the title screen."
                             : setup->rows[setup->cursor].help;
    mm_icon(x, start ? MM_ICON_PLAY : setup->cursor == 0u ? MM_ICON_LIST : setup->cursor == 1u ? MM_ICON_FLAG : MM_ICON_GEAR,
            mm_x(x, cx + 330.0f), mm_y(x, 420.0f), mm_s(x, 76.0f), MM_RED, card_in);
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 48.0f), MM_WHITE);
    head.shadow_color = 0x80002a5au;
    head.shadow_y = mm_s(x, 3.0f);
    head.shadow_blur = mm_s(x, 5.0f);
    ui_text_draw_centered(c, &head, mm_x(x, cx + 330.0f), mm_y(x, 560.0f), UI_ALIGN_CENTER, title, card_in);
    UiTextStyle body = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), 0xff17324du);
    if (help != NULL)
        ui_text_draw_wrapped(c, &body, mm_x(x, cx + 330.0f), mm_y(x, 630.0f), mm_s(x, 560.0f), mm_s(x, 40.0f), 4,
                             UI_ALIGN_CENTER, help, card_in);
    status_line(x, setup, mm_y(x, 930.0f));
    const MmHint hints[] = {{MM_BUTTON_DPAD, "Change"}, {MM_BUTTON_A, start ? "Start" : "Next"}};
    mm_footer(x, NULL, hints, 2u);
}

int modern_menu_render_setup(ModernMenu *menu, const ModernMenuSetup *setup, UiCanvas *canvas)
{
    if (menu == NULL || setup == NULL || canvas == NULL || canvas->width <= 0 || canvas->height <= 0) return 0;
    const ModernMenuView view = {.gamepad = setup->gamepad};
    MmCtx x;
    mm_context_init(&x, menu, &view, NULL, canvas, setup->tick, 0x5e7000u + (uint32_t)setup->screen);
    ui_canvas_unclip(canvas);
    switch (setup->screen)
    {
    case MODERN_SETUP_IMPORT: import_screen(&x, setup); break;
    case MODERN_SETUP_IMPORTING: importing_screen(&x); break;
    case MODERN_SETUP_REIMPORT: reimport_screen(&x, setup); break;
    case MODERN_SETUP_PREFERENCES: preferences_screen(&x, setup); break;
    default: return 0;
    }
    return 1;
}
