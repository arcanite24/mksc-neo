#include "modern_menu_internal.h"

#include "course_menu_renderer.h"
#include "frontend_config.h"
#include "menu_names.h"

#include <stdio.h>
#include <math.h>

/* Menu artwork is keyed by catalog position, not the runtime course ID. This
 * also resolves Extra and custom courses without a second stock mapping. */
static int course_art(const PortableAssetPackView *pack, uint32_t course_id,
                       uint32_t *preview, uint32_t *name)
{
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    if (pack == NULL || course_id == 0u || !build_frontend_config(pack, drivers, courses, &config)) return 0;
    for (size_t i = 0u; i < config.course_count; ++i)
        if (courses[i].content_id == course_id && courses[i].available)
        {
            const uint32_t slot = (uint32_t)courses[i].cup * PORTABLE_FRONTEND_TRACKS_PER_CUP + courses[i].track;
            *preview = PORTABLE_COURSE_PREVIEW + slot;
            *name = PORTABLE_COURSE_NAME + slot;
            return 1;
        }
    return 0;
}

static const char *stage_title(ModernMenuTransitionKind kind)
{
    switch (kind)
    {
    case MODERN_TRANSITION_LOADING: return "Loading";
    case MODERN_TRANSITION_ENTRY: return "Ready to race";
    case MODERN_TRANSITION_RESULTS: return "Race complete";
    case MODERN_TRANSITION_CUP: return "Cup complete";
    default: return "";
    }
}

static void transition_text(MmCtx *x, const char *text, float dx, float dy,
                             float width, float size, uint32_t color, float alpha)
{
    if (text == NULL || text[0] == '\0') return;
    UiTextStyle style = ui_text_style(UI_FONT_BLACK, mm_s(x, size), color);
    style = ui_text_fit(&style, text, mm_s(x, width));
    ui_text_draw_centered(x->c, &style, mm_x(x, dx), mm_y(x, dy), UI_ALIGN_LEFT, text, alpha);
}

int modern_menu_render_transition(ModernMenu *menu, const ModernMenuTransition *transition, UiCanvas *canvas)
{
    if (menu == NULL || transition == NULL || canvas == NULL || (canvas->pixels == NULL && canvas->sink == NULL) ||
        canvas->width <= 0 || canvas->height <= 0 || canvas->stride < canvas->width || transition->duration == 0u ||
        (unsigned)transition->kind > (unsigned)MODERN_TRANSITION_CUP ||
        (transition->frame != NULL && (transition->frame_width <= 0 || transition->frame_height <= 0 ||
                                       transition->frame_w <= 0.0f || transition->frame_h <= 0.0f ||
                                       !isfinite(transition->frame_x) || !isfinite(transition->frame_y) ||
                                       !isfinite(transition->frame_w) || !isfinite(transition->frame_h))))
        return 0;
    const uint32_t elapsed = transition->elapsed < transition->duration
        ? transition->elapsed : transition->duration;
    const float progress = (float)elapsed / (float)transition->duration;
    const ModernMenuView view = {.pack = transition->pack, .gamepad = transition->gamepad};
    MmCtx x;
    /* A card may cover another modern screen. Preserve that screen's entrance
     * clock so revealing it cannot restart its rows or action animations. */
    const uint32_t screen_key = menu->screen_key, screen_tick = menu->screen_tick, last_tick = menu->last_tick;
    const uint8_t screen_valid = menu->screen_valid;
    mm_context_init(&x, menu, &view, NULL, canvas, elapsed, 0x7a000000u + (uint32_t)transition->kind);
    menu->screen_key = screen_key; menu->screen_tick = screen_tick;
    menu->last_tick = last_tick; menu->screen_valid = screen_valid;
    ui_canvas_unclip(canvas);
    if (transition->external_background)
    {
        ui_canvas_reset_mode(canvas, 1);
        ui_canvas_clear(canvas, 0u);
    }
    else if (transition->frame != NULL)
    {
        ui_canvas_clear(canvas, 0xff000000u);
        const UiImage race = {transition->frame, transition->frame_width, transition->frame_height};
        ui_draw_image(canvas, &race, transition->frame_x, transition->frame_y, transition->frame_w,
                      transition->frame_h, 1.0f, UI_IMAGE_NEAREST, MM_WHITE, 0.0f);
    }
    else
        mm_background(&x, 1);
    /* Keep a loading card present while work is in flight. Other cards reveal
     * their underlying picture at the end without adding an input lock. */
    const float reveal = transition->kind == MODERN_TRANSITION_LOADING
                             ? 0.0f
                             : mm_ease_out(mm_clamp01((progress - 0.55f) / 0.45f));
    const float alpha = 1.0f - reveal;
    if (alpha <= 0.0f) return 1;
    const float slide = reveal * 420.0f;
    UiPaint blue = ui_paint_linear(0.0f, 0.0f, 0xe0062450u,
                                   (float)canvas->width, (float)canvas->height, 0xc01174c5u);
    ui_fill_round_rect(canvas, -4.0f, -4.0f, (float)canvas->width + 8.0f,
                       (float)canvas->height + 8.0f, 0.0f, 0.0f, &blue, 1.0f, alpha);
    const float left = 170.0f + slide, top = 350.0f;
    const UiPaint shadow = ui_paint_solid(0x60001230u);
    ui_fill_round_rect(canvas, mm_x(&x, left + 14.0f), mm_y(&x, top + 20.0f),
                       mm_s(&x, 1580.0f), mm_s(&x, 380.0f), mm_s(&x, 24.0f),
                       0.0f, &shadow, mm_s(&x, 22.0f), alpha);
    const UiPaint white = ui_paint_solid(0xf4ffffffu);
    ui_fill_round_rect(canvas, mm_x(&x, left), mm_y(&x, top), mm_s(&x, 1580.0f),
                       mm_s(&x, 380.0f), mm_s(&x, 24.0f), 0.0f, &white, 1.0f, alpha);
    /* The yellow rail travels with supplied progress, including loading frames. */
    ui_fill_rect(canvas, mm_x(&x, left), mm_y(&x, top + 350.0f), mm_s(&x, 1580.0f),
                 mm_s(&x, 30.0f), MM_YELLOW_DEEP, alpha);
    ui_fill_rect(canvas, mm_x(&x, left + progress * 1240.0f), mm_y(&x, top + 350.0f),
                 mm_s(&x, 340.0f), mm_s(&x, 30.0f), MM_YELLOW, alpha);
    uint32_t preview = 0u, name_art = 0u;
    const int catalog_art = course_art(transition->pack, transition->course_id, &preview, &name_art);
    const int has_preview = catalog_art &&
        mm_image_fit(&x, preview, mm_x(&x, left + 255.0f), mm_y(&x, top + 175.0f),
                      mm_s(&x, 410.0f), mm_s(&x, 280.0f), alpha, UI_IMAGE_NEAREST);
    const float text_x = left + (has_preview ? 510.0f : 80.0f);
    const float text_width = has_preview ? 990.0f : 1420.0f;
    transition_text(&x, transition->title != NULL ? transition->title : stage_title(transition->kind),
                    text_x, top + 76.0f, text_width, 52.0f, MM_BLUE_DEEP, alpha);
    const char *course_name = transition->course_id != 0u
        ? portable_menu_course_name(transition->course_id) : NULL;
    if (course_name != NULL)
        transition_text(&x, course_name, text_x, top + 170.0f, text_width, 76.0f, MM_INK, alpha);
    else if (!(catalog_art && mm_image_fit(&x, name_art,
                 mm_x(&x, text_x + text_width * 0.5f), mm_y(&x, top + 170.0f),
                 mm_s(&x, text_width), mm_s(&x, 84.0f), alpha, UI_IMAGE_NEAREST)))
    {
        char fallback[40];
        if (transition->course_id != 0u)
            snprintf(fallback, sizeof fallback, "Course %u", (unsigned)transition->course_id);
        else snprintf(fallback, sizeof fallback, "%s", transition->kind == MODERN_TRANSITION_CUP
                      ? "Celebration" : "Mario Kart");
        transition_text(&x, fallback, text_x, top + 170.0f, text_width, 76.0f, MM_INK, alpha);
    }
    transition_text(&x, transition->subtitle, text_x, top + 270.0f, text_width,
                    42.0f, MM_INK_SOFT, alpha);
    return 1;
}
