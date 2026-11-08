#include "modern_menu_internal.h"

#include "character_menu_renderer.h"
#include "content_registry.h"
#include "menu_names.h"

#include <stdio.h>

/* Fit names and numbers independently: a long mod-supplied name must not
 * push its result outside the table or cover another racer's rank. */
static void result_text(MmCtx *x, const char *text, float dx, float dy, float width,
                        float size, uint32_t color, UiAlign align, float alpha)
{
    if (text == NULL || text[0] == '\0') return;
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, mm_s(x, size), color);
    style = ui_text_fit(&style, text, mm_s(x, width));
    ui_text_draw_centered(x->c, &style, mm_x(x, dx), mm_y(x, dy), align, text, alpha);
}

static void result_row(MmCtx *x, const ModernMenuResultRow *row, unsigned index)
{
    const float top = 220.0f + (float)index * 84.0f;
    const float appear = mm_ease_out(mm_stagger(x, (float)index * 2.0f, 12.0f));
    const float left = 100.0f - (1.0f - appear) * 100.0f;
    const uint32_t ink = row->highlighted ? MM_INK : MM_WHITE;
    const UiPaint paint = ui_paint_solid(row->highlighted ? 0xf0ffd21eu : 0xce142b46u);
    ui_fill_round_rect(x->c, mm_x(x, left), mm_y(x, top), mm_s(x, 1110.0f),
                       mm_s(x, 76.0f), mm_s(x, 10.0f), 0.0f, &paint, 1.0f, appear);
    char rank[16], fallback[32];
    if (row->rank != 0u)
    {
        snprintf(rank, sizeof rank, "%u", row->rank);
        result_text(x, rank, left + 52.0f, top + 38.0f, 78.0f, 50.0f, ink,
                    UI_ALIGN_CENTER, appear);
    }
    const float icon_x = left + 142.0f, icon_y = top + 38.0f;
    const uint32_t roster_art = row->driver_id != 0u && row->driver_id <= PORTABLE_DRIVER_ROSTER_MAX_ID
        ? portable_character_roster_art(row->driver_id) : 0u;
    const uint32_t portrait_id = row->portrait_id != 0u ? row->portrait_id
        : roster_art != 0u ? roster_art + PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT : 0u;
    int portrait_drawn = portrait_id != 0u && x->view->pack != NULL &&
        mm_image_fit(x, portrait_id, mm_x(x, icon_x), mm_y(x, icon_y),
                     mm_s(x, 62.0f), mm_s(x, 62.0f), appear, UI_IMAGE_NEAREST);
    if (!portrait_drawn && row->portrait_id == 0u && x->view->pack != NULL)
    {
        PortableCharacterSlotArt art;
        if (portable_character_menu_driver_art(x->view->pack, row->driver_id, &art))
        {
            const uint32_t idle = ui_art_animation_frame(x->view->pack, art.portrait, x->tick);
            if (idle != 0u)
                portrait_drawn = mm_image_fit(x, idle, mm_x(x, icon_x), mm_y(x, icon_y),
                                              mm_s(x, 62.0f), mm_s(x, 62.0f), appear,
                                              UI_IMAGE_NEAREST);
        }
    }
    UiImage emblem;
    if (!portrait_drawn && row->portrait_id == 0u && mm_portrait_3d(x, row->driver_id, &emblem))
    {
        const float side = mm_s(x, 54.0f);
        ui_draw_image(x->c, &emblem, mm_x(x, icon_x) - side * 0.5f, mm_y(x, icon_y) - side * 0.5f,
                      side, side, appear, UI_IMAGE_NEAREST, MM_WHITE, 0.0f);
        portrait_drawn = 1;
    }
    if (!portrait_drawn)
        if (row->driver_id != 0u || row->portrait_id != 0u)
            mm_icon(x, MM_ICON_PERSON, mm_x(x, icon_x), mm_y(x, icon_y), mm_s(x, 25.0f), ink, appear);
    const char *name = row->name;
    if (name == NULL || name[0] == '\0') name = portable_menu_driver_name(row->driver_id);
    const UiImage *name_art = NULL;
    if ((name == NULL || name[0] == '\0') && roster_art != 0u && x->view->pack != NULL)
    {
        name_art = mm_art(x, roster_art + PORTABLE_CHARACTER_ROSTER_BOARD_NAME);
        if (name_art == NULL) name_art = mm_art(x, roster_art + PORTABLE_CHARACTER_ROSTER_NAME);
    }
    if ((name == NULL || name[0] == '\0') && name_art == NULL)
    {
        snprintf(fallback, sizeof fallback, "Driver %u", (unsigned)row->driver_id);
        name = fallback;
    }
    const int detail = row->detail[0] != '\0';
    const float name_y = top + (detail ? 27.0f : 38.0f);
    if (name_art != NULL)
    {
        /* Fit the entire authored image without cropping or guessing a stock
         * seat. Mod names and portraits follow the resolved content ID. */
        float scale = mm_s(x, 535.0f) / (float)name_art->width;
        if ((float)name_art->height * scale > mm_s(x, 46.0f))
            scale = mm_s(x, 46.0f) / (float)name_art->height;
        const float width = (float)name_art->width * scale;
        const float height = (float)name_art->height * scale;
        ui_draw_image(x->c, name_art, mm_x(x, left + 194.0f), mm_y(x, name_y) - height * 0.5f,
                      width, height, appear, UI_IMAGE_NEAREST, MM_WHITE, 0.0f);
    }
    else
        result_text(x, name, left + 194.0f, name_y, 535.0f, 46.0f,
                    ink, UI_ALIGN_LEFT, appear);
    if (detail)
        result_text(x, row->detail, left + 196.0f, top + 59.0f, 535.0f, 28.0f,
                    row->highlighted ? MM_INK_SOFT : 0xffb8d8f3u, UI_ALIGN_LEFT, appear);
    result_text(x, row->value, left + 1076.0f, top + 38.0f, 310.0f, 45.0f,
                ink, UI_ALIGN_RIGHT, appear);
}

int modern_menu_render_results(ModernMenu *menu, const ModernMenuResults *results, UiCanvas *canvas)
{
    if (menu == NULL || results == NULL || canvas == NULL || (canvas->pixels == NULL && canvas->sink == NULL) ||
        canvas->width <= 0 || canvas->height <= 0 ||
        (!results->external_background &&
         (results->frame == NULL || results->frame_width <= 0 || results->frame_height <= 0 ||
          results->frame_w <= 0.0f || results->frame_h <= 0.0f)) ||
        results->row_count > MODERN_MENU_RESULT_ROWS ||
        results->item_count > MODERN_MENU_RESULT_ACTIONS ||
        (results->item_count != 0u && results->cursor >= results->item_count))
        return 0;
    for (unsigned i = 0u; i < results->item_count; ++i)
        if (results->labels[i] == NULL)
            return 0;
    const ModernMenuView view = {.pack = results->pack, .pack_3d = results->pack, .gamepad = results->gamepad};
    MmCtx x;
    mm_context_init(&x, menu, &view, NULL, canvas, results->tick,
                    0x9e500000u + results->row_count * 8u + results->item_count);
    ui_canvas_unclip(canvas);
    if (results->external_background)
    {
        ui_canvas_reset_mode(canvas, 1);
        ui_canvas_clear(canvas, 0u);
    }
    else
    {
        canvas->premultiplied_alpha = 0u;
        ui_canvas_clear(canvas, 0xff000000u);
        const UiImage race = {results->frame, results->frame_width, results->frame_height};
        ui_draw_image(canvas, &race, results->frame_x, results->frame_y, results->frame_w,
                      results->frame_h, 1.0f, UI_IMAGE_NEAREST, MM_WHITE, 0.0f);
    }
    if (!results->ceremony)
    {
        UiPaint shade =
            ui_paint_linear(0.0f, 0.0f, 0xe0021230u, (float)canvas->width, 0.0f, 0xa0021230u);
        ui_fill_rect(canvas, 0.0f, 0.0f, (float)canvas->width, (float)canvas->height, 0x70021230u,
                     1.0f);
        ui_fill_round_rect(canvas, -4.0f, -4.0f, (float)canvas->width + 8.0f,
                           (float)canvas->height + 8.0f, 0.0f, 0.0f, &shade, 1.0f,
                           mm_ease_out(mm_stagger(&x, 0.0f, 8.0f)));
    }
    mm_header(&x, results->title != NULL ? results->title : "Results", results->subtitle);
    for (unsigned i = 0u; i < results->row_count; ++i) result_row(&x, &results->rows[i], i);
    const float actions_top = results->item_count <= 3u ? 300.0f : 230.0f;
    for (unsigned i = 0u; i < results->item_count; ++i)
    {
        const float appear = mm_ease_out(mm_stagger(&x, 4.0f + (float)i * 2.0f, 12.0f));
        mm_tab(&x, 1280.0f + (1.0f - appear) * 120.0f, actions_top + (float)i * 116.0f,
               540.0f, 96.0f, results->labels[i], NULL, i == results->cursor,
               appear, MM_ICON_NONE, 0u, (results->disabled_mask & (1u << i)) != 0u);
    }
    if (results->notice != NULL && results->notice[0] != '\0')
    {
        UiTextStyle notice = ui_text_style(UI_FONT_BOLD, mm_s(&x, 33.0f), MM_WHITE);
        ui_text_draw_wrapped(canvas, &notice, mm_x(&x, 1290.0f), mm_y(&x, 870.0f),
                             mm_s(&x, 520.0f), mm_s(&x, 42.0f), 2, UI_ALIGN_LEFT,
                             results->notice, 1.0f);
    }
    const MmHint hints[] = {{MM_BUTTON_DPAD, "Choose"}, {MM_BUTTON_A, "OK"}};
    const int generic_hints = results->item_count != 0u &&
                              (results->hint == NULL || results->hint[0] == '\0');
    mm_footer(&x, results->hint, generic_hints ? hints : NULL, generic_hints ? 2u : 0u);
    return 1;
}
