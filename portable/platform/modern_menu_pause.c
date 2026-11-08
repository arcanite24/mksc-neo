#include "modern_menu_internal.h"

/* The race stays visible, dimmed toward the left where the choices slide in;
 * the shell owns the pause rules and passes the rows already resolved. */

static MmIcon pause_icon(ModernMenuPauseKind kind)
{
    switch (kind)
    {
    case MODERN_PAUSE_CONTINUE: return MM_ICON_PLAY;
    case MODERN_PAUSE_RETRY: return MM_ICON_CLOCK;
    case MODERN_PAUSE_CHANGE_COURSE: return MM_ICON_FLAG;
    case MODERN_PAUSE_CHANGE_DRIVER: return MM_ICON_PERSON;
    default: return MM_ICON_LIST;
    }
}

int modern_menu_render_pause(ModernMenu *menu, const ModernMenuPause *pause, UiCanvas *canvas)
{
    if (menu == NULL || pause == NULL || canvas == NULL || canvas->width <= 0 ||
        canvas->height <= 0 || (canvas->pixels == NULL && canvas->sink == NULL) ||
        (!pause->external_background && pause->frame == NULL) || pause->item_count == 0u ||
        pause->item_count > MODERN_MENU_PAUSE_ITEMS)
        return 0;
    const ModernMenuView view = {.gamepad = pause->gamepad};
    MmCtx x;
    mm_context_init(&x, menu, &view, NULL, canvas, pause->tick, 0x9a05e000u + pause->item_count);
    ui_canvas_unclip(canvas);
    if (pause->external_background)
    {
        ui_canvas_reset_mode(canvas, 1);
        ui_canvas_clear(canvas, 0u);
    }
    else
    {
        canvas->premultiplied_alpha = 0u;
        ui_canvas_clear(canvas, 0xff000000u);
        const UiImage race = {pause->frame, pause->frame_width, pause->frame_height};
        ui_draw_image(canvas, &race, pause->frame_x, pause->frame_y, pause->frame_w, pause->frame_h,
                      1.0f, UI_IMAGE_NEAREST, 0xffffffffu, 0.0f);
    }
    /* Dim: deep blue on the left for the list, lighter over the race. */
    const float dim = mm_ease_out(mm_stagger(&x, 0.0f, 8.0f));
    UiPaint shade =
        ui_paint_linear(0.0f, 0.0f, 0xe0021230u, (float)canvas->width, 0.0f, 0x70021230u);
    ui_fill_round_rect(canvas, -4.0f, -4.0f, (float)canvas->width + 8.0f,
                       (float)canvas->height + 8.0f, 0.0f, 0.0f, &shade, 1.0f, dim);
    if (pause->graphics != NULL)
    {
        mm_options_overlay(&x, pause->graphics);
        return 1;
    }
    mm_header(&x, "Paused", pause->subtitle);
    const float row_h = 112.0f, gap = 24.0f;
    const float list_h = (float)pause->item_count * row_h + (float)(pause->item_count - 1u) * gap;
    const float top = 540.0f - list_h * 0.5f + 20.0f;
    for (unsigned i = 0u; i < pause->item_count; ++i)
    {
        const float appear = mm_ease_back(mm_stagger(&x, 2.0f + 2.0f * (float)i, 12.0f));
        const float dx = x.left + 110.0f - (1.0f - appear) * 260.0f;
        mm_tab(&x, dx, top + (float)i * (row_h + gap), 780.0f, row_h, pause->labels[i], NULL, i == pause->cursor,
               mm_clamp01(appear), pause_icon(pause->kinds[i]), 0u, 0);
    }
    const MmHint hints[] = {{MM_BUTTON_B, "Resume"}, {MM_BUTTON_A, "OK"}, {MM_BUTTON_SELECT, "Graphics"}};
    mm_footer(&x, NULL, hints, 3u);
    return 1;
}
