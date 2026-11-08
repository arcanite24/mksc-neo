#include "modern_menu_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "character_menu_renderer.h"
#include "content_registry.h"
#include "course_menu_renderer.h"
#include "menu_names.h"

static const uint32_t player_colors[4] = {0xffffd21eu, 0xff3fb6ffu, 0xff4cdc6au, 0xffff6fb4u};

/* Draws pack name art scaled to a text height, as the fallback for content
 * without a listed text name. */
static void name_art(MmCtx *x, uint32_t id, float px, float py, float height, UiAlign align, float alpha)
{
    const UiImage *image = ui_art_image(x->menu->art, x->view->pack, id, 0);
    if (image == NULL) return;
    const float scale = height / (float)image->height, w = (float)image->width * scale;
    const float left = align == UI_ALIGN_CENTER ? px - w * 0.5f : align == UI_ALIGN_RIGHT ? px - w : px;
    ui_draw_image(x->c, image, left, py - height * 0.5f, w, height, alpha, UI_IMAGE_NEAREST, MM_WHITE, 0.0f);
}

static void stat_bar(MmCtx *x, float px, float py, float w, const char *label, unsigned value, float alpha,
                     unsigned slot)
{
    UiTextStyle text = ui_text_style(UI_FONT_BLACK, mm_s(x, 28.0f), MM_WHITE);
    text.shadow_color = 0x70002a5au;
    text.shadow_y = mm_s(x, 2.0f);
    ui_text_draw_centered(x->c, &text, px, py, UI_ALIGN_LEFT, label, alpha);
    const float bx = px + mm_s(x, 130.0f), bw = w - mm_s(x, 130.0f), bh = mm_s(x, 22.0f);
    UiPaint track = ui_paint_solid(0x50001a40u);
    ui_fill_round_rect(x->c, bx, py - bh * 0.5f, bw, bh, bh * 0.5f, 0.0f, &track, 1.2f, alpha);
    const float fill = mm_follow(x, slot, 1u, (float)value / 5.0f, 0.25f);
    if (fill > 0.0f)
    {
        UiPaint bar = ui_paint_linear(bx, 0.0f, 0xffffe55au, bx + bw, 0.0f, 0xffff9c00u);
        ui_fill_round_rect(x->c, bx, py - bh * 0.5f, bw * fill, bh, bh * 0.5f, 0.0f, &bar, 1.2f, alpha);
    }
    for (unsigned notch = 1u; notch < 5u; ++notch)
        ui_fill_rect(x->c, bx + bw * (float)notch / 5.0f - mm_s(x, 1.5f), py - bh * 0.5f, mm_s(x, 3.0f), bh,
                     0x60001a40u, alpha);
}

static uint32_t stock_driver_for_class(MmCtx *x, uint32_t character_class)
{
    for (uint8_t slot = 0u; slot < PORTABLE_FRONTEND_DRIVER_SLOTS; ++slot)
    {
        const PortableFrontendDriverSlot *driver = portable_frontend_flow_driver_at(x->flow, 0u, slot);
        PortableCharacterContent content;
        if (driver != NULL && driver->content_id != 0u &&
            portable_content_registry_character(x->view->pack, driver->content_id, &content) == PORTABLE_CONTENT_OK &&
            content.character_class == character_class)
            return driver->content_id;
    }
    return 0u;
}

/* ---- Character select ---------------------------------------------------- */

/* A 3D-only driver's showcase: its driver model in its kart (the chosen one
 * once locked) turning on the pedestal, as the stock drivers' karts do. The
 * drawn model is fitted between the pedestal's left edge and the stats cards
 * (and below the header), scaled about its base and slid left only as far as
 * needed; both settle smoothly as its silhouette turns or animates.
 * 0 leaves the caller's emblem fallback. */
static int driver_model(MmCtx *x, uint32_t driver_id, float cx, float cy, float size, float alpha, float hop)
{
    const PortableFrontendFlow *flow = x->flow;
    const float edge = size * 1.2f;
    const unsigned pixels = edge > 640.0f ? 640u : edge < 8.0f ? 8u : (unsigned)edge;
    const PortableMenuDriverModelPose pose = {
        .driver_id = driver_id,
        .kart_id = flow->character_locked ? flow->selected_kart : 0u,
        .size = pixels,
        .yaw = 0.6 + (double)x->tick * 0.02,
        .tick = flow->character_locked ? flow->tick - flow->character_lock_tick : x->tick,
        .celebrate = flow->character_locked,
    };
    UiImage image;
    if (x->view->pack_3d == NULL ||
        !portable_menu_driver_model_render(modern_menu_driver_model(x->menu), x->view->pack_3d, &pose, &image))
        return 0;
    mm_kart_showcase(x, 0u, cx, cy, size, alpha, x->tick, 0.0f);
    const float base_y = cy + size * 0.36f, lift = hop * size * 0.18f;
    const float left = cx - edge * 0.5f, top = base_y - edge * 0.78f;
    /* The silhouette's bounds in window pixels. */
    int low_x = image.width, high_x = -1, low_y = image.height, high_y = -1;
    for (int py = 0; py < image.height; ++py)
        for (int px = 0; px < image.width; ++px)
            if (image.pixels[(size_t)py * (size_t)image.width + (size_t)px] >> 24)
            {
                if (px < low_x) low_x = px;
                if (px > high_x) high_x = px;
                if (py < low_y) low_y = py;
                if (py > high_y) high_y = py;
            }
    float scale = 1.0f, shift = 0.0f, pivot_x = cx, pivot_y = base_y;
    if (high_x >= low_x)
    {
        const float k = edge / (float)image.width;
        const float l = left + (float)low_x * k, r = left + (float)(high_x + 1) * k;
        const float t = top + (float)low_y * k, b = top + (float)(high_y + 1) * k;
        const float box_l = cx - size * 0.42f, box_r = mm_x(x, 1480.0f - 16.0f), box_t = mm_y(x, 140.0f);
        pivot_x = (l + r) * 0.5f;
        pivot_y = b;
        if (box_r - box_l > 1.0f && b - box_t > 1.0f)
        {
            scale = fminf(1.0f, fminf((box_r - box_l) / (r - l), (b - box_t) / (b - t)));
            const float half = (r - l) * scale * 0.5f;
            shift = fminf(0.0f, box_r - (pivot_x + half));
            shift = fmaxf(shift, box_l - (pivot_x - half));
        }
    }
    const uint32_t key = driver_id * 2u + (flow->character_locked ? 1u : 0u);
    scale = mm_follow(x, 8u, key, scale * 1000.0f, 0.2f) / 1000.0f;
    shift = mm_follow(x, 9u, key, shift / x->u, 0.2f) * x->u;
    UiPaint drop = ui_paint_solid(0x70000000u);
    ui_fill_ellipse(x->c, pivot_x + shift, base_y - size * 0.017f, size * (0.3f - hop * 0.08f) * scale,
                    size * 0.07f * scale, &drop, mm_s(x, 16.0f), alpha);
    const float drawn = edge * scale;
    ui_draw_image(x->c, &image, pivot_x + shift + (left - pivot_x) * scale, pivot_y + (top - pivot_y) * scale - lift,
                  drawn, drawn, alpha, UI_IMAGE_SMOOTH, MM_WHITE, 0.0f);
    return 1;
}

static void character_tile(MmCtx *x, float px, float py, float size, uint8_t page, uint8_t slot, int selected,
                           float alpha)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    const PortableFrontendDriverSlot *driver = portable_frontend_flow_driver_at(flow, page, slot);
    PortableCharacterSlotArt art = {0};
    UiImage emblem;
    const int mesh = (driver != NULL && driver->mesh_only && mm_portrait_3d(x, driver->content_id, &emblem));
    const int has = mesh || portable_character_menu_slot_art(x->view->pack, flow, page, slot, &art);
    const int random = slot == PORTABLE_FRONTEND_RANDOM_SLOT;
    const int available = random || (has && driver != NULL && driver->content_id != 0u && driver->available);
    const float radius = size * 0.13f;
    UiPaint shadow = ui_paint_solid(0x40002050u);
    ui_fill_round_rect(c, px, py + size * 0.04f, size, size, radius, 0.0f, &shadow, size * 0.08f, alpha);
    UiPaint card = selected ? ui_paint_linear(0.0f, py, 0xfffff6a0u, 0.0f, py + size, 0xffffc21au)
                            : ui_paint_linear(0.0f, py, 0xf0ffffffu, 0.0f, py + size, 0xe0d9eef9u);
    if (!has && !random) card = ui_paint_solid(0x40ffffffu);
    ui_fill_round_rect(c, px, py, size, size, radius, 0.0f, &card, 1.2f, alpha);
    const float inset = size * 0.06f;
    if (random)
    {
        UiPaint mystery = ui_paint_linear(0.0f, py, 0xff8f6bffu, 0.0f, py + size, 0xff3b2ad1u);
        ui_fill_round_rect(c, px + inset, py + inset, size - inset * 2.0f, size - inset * 2.0f, radius * 0.8f, 0.0f,
                           &mystery, 1.2f, alpha);
        UiTextStyle mark = ui_text_style(UI_FONT_BLACK, size * 0.66f, MM_WHITE);
        mark.outline = size * 0.03f;
        mark.outline_color = 0xff2a1a90u;
        const float wobble = selected ? sinf(x->t * 5.0f) * size * 0.03f : 0.0f;
        ui_text_draw_centered(c, &mark, px + size * 0.5f, py + size * 0.5f + wobble, UI_ALIGN_CENTER, "?", alpha);
        return;
    }
    if (!has) return;
    if (mesh)
    {
        const float bob = selected ? sinf(x->t * 4.0f) * size * 0.02f : 0.0f;
        ui_draw_image(c, &emblem, px + inset * 2.0f, py + inset * 2.0f + bob, size - inset * 4.0f,
                      size - inset * 4.0f, alpha, 0u, MM_WHITE, 0.0f);
        return;
    }
    uint32_t anim = art.portrait, anim_tick = 0u;
    if (selected)
    {
        anim = art.portrait + (flow->character_locked ? 2u : 1u);
        anim_tick = flow->character_locked && (flow->character_animation_timing & 2u)
                        ? flow->tick - flow->character_lock_tick
                    : (flow->character_animation_timing & 1u) ? flow->tick - flow->character_selection_tick
                                                               : flow->page_ticks;
    }
    uint32_t frame = ui_art_animation_frame(x->view->pack, anim, anim_tick);
    if (frame == 0u) frame = ui_art_animation_frame(x->view->pack, art.portrait, 0u);
    const UiImage *portrait = frame != 0u ? mm_art(x, frame) : NULL;
    if (portrait != NULL)
        ui_draw_image(c, portrait, px + inset, py + inset, size - inset * 2.0f, size - inset * 2.0f, alpha,
                      available ? 0u : UI_IMAGE_GRAY, MM_WHITE, radius * 0.8f);
    /* Locked-in picks from earlier players carry their badge. */
    unsigned stacked = 0u;
    for (unsigned player = 0u; driver != NULL && player < flow->players && flow->players > 1u; ++player)
    {
        const int done = player < flow->character_player || (flow->character_locked && player == flow->character_player);
        if (!done || flow->player_characters[player] != driver->content_id) continue;
        char label[12];
        snprintf(label, sizeof label, "P%u", player + 1u);
        UiPaint chip = ui_paint_solid(player_colors[player]);
        const float bw = size * 0.36f, bh = size * 0.22f;
        const float by = py + inset * 0.5f + (float)stacked++ * (bh + inset * 0.3f);
        ui_fill_round_rect(c, px + size - bw - inset * 0.5f, by, bw, bh, bh * 0.4f, 0.0f, &chip, 1.2f, alpha);
        UiTextStyle text = ui_text_style(UI_FONT_BLACK, bh * 0.75f, MM_INK);
        ui_text_draw_centered(c, &text, px + size - bw * 0.5f - inset * 0.5f, by + bh * 0.5f, UI_ALIGN_CENTER, label,
                              alpha);
    }
}

/* With imported game platforms, pills above the grid name each platform
 * whose pages the player may browse; the current page's is lit. */
static void platform_tabs(MmCtx *x, float dx, float dy)
{
    const PortableFrontendFlow *flow = x->flow;
    const uint8_t pages = portable_frontend_flow_driver_pages(flow);
    uint8_t seen[PORTABLE_FRONTEND_DRIVER_PAGES] = {0};
    unsigned count = 0u;
    for (uint8_t page = 0u; page < pages && count < PORTABLE_FRONTEND_DRIVER_PAGES; ++page)
    {
        const uint8_t platform = portable_frontend_flow_page_platform(flow, page);
        unsigned known = 0u;
        while (known < count && seen[known] != platform)
            ++known;
        if (known == count && (flow->character_player == 0u || platform == flow->platform))
            seen[count++] = platform;
    }
    if (count < 2u && seen[0] == 0u)
        return;
    const uint8_t current = portable_frontend_flow_page_platform(flow, flow->character_page);
    const float h = mm_s(x, 50.0f), py = mm_y(x, dy);
    float px = mm_x(x, dx);
    for (unsigned i = 0u; i < count; ++i)
    {
        const uint8_t group = portable_frontend_flow_platform_group(flow, seen[i]);
        const char *name = seen[i] == 0u ? "Super Circuit"
                           : group != 0u ? flow->mesh_group_names[group - 1u] : "3D drivers";
        const int on = seen[i] == current;
        UiTextStyle text = ui_text_style(UI_FONT_BLACK, mm_s(x, 26.0f), on ? 0xff3a2a00u : MM_WHITE);
        text = ui_text_fit(&text, name, mm_s(x, 300.0f));
        const float w = ui_text_width(&text, name) + mm_s(x, 48.0f);
        UiPaint pill = on ? ui_paint_linear(0.0f, py, 0xfffff38cu, 0.0f, py + h, 0xffffc21au) : ui_paint_solid(0x50002a5au);
        ui_fill_round_rect(x->c, px, py, w, h, h * 0.5f, 0.25f, &pill, 1.2f, 1.0f);
        ui_text_draw_centered(x->c, &text, px + w * 0.5f, py + h * 0.5f, UI_ALIGN_CENTER, name, 1.0f);
        px += w + mm_s(x, 12.0f);
    }
}

int mm_page_character(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    mm_background(x, 0);
    const char *title, *subtitle;
    mm_mode_names(flow, &title, &subtitle);
    char player_title[24];
    if (flow->players > 1u)
    {
        snprintf(player_title, sizeof player_title, "Player %u", (unsigned)flow->character_player + 1u);
        mm_header(x, player_title, title);
    }
    else
        mm_header(x, title, subtitle);
    const uint8_t pages = portable_frontend_flow_driver_pages(flow) ? portable_frontend_flow_driver_pages(flow) : 1u;
    const uint8_t page = flow->character_page;
    const float tile = 204.0f, gap = 22.0f, pad = 36.0f;
    const float grid_w = tile * 3.0f + gap * 2.0f;
    const float panel_x = 120.0f, panel_y = 214.0f;
    const float appear = mm_ease_out(mm_stagger(x, 0.0f, 16.0f));
    mm_panel(x, panel_x + (1.0f - appear) * -300.0f, panel_y, grid_w + pad * 2.0f, grid_w + pad * 2.0f, 30.0f, appear);
    platform_tabs(x, panel_x, panel_y - 62.0f);
    float cursor_x = 0.0f, cursor_y = 0.0f;
    for (uint8_t slot = 0u; slot < PORTABLE_FRONTEND_DRIVER_SLOTS; ++slot)
    {
        const float pop = mm_ease_back(mm_stagger(x, 2.0f + (float)slot * 1.5f, 12.0f));
        const float dx = panel_x + pad + (float)(slot % 3u) * (tile + gap) + (1.0f - appear) * -300.0f;
        const float dy = panel_y + pad + (float)(slot / 3u) * (tile + gap);
        const int selected = slot == flow->character_cursor;
        const float grow = selected ? 1.06f : 1.0f;
        const float size = tile * pop * grow;
        const float px = mm_x(x, dx + (tile - size) * 0.5f), py = mm_y(x, dy + (tile - size) * 0.5f);
        character_tile(x, px, py, mm_s(x, size), page, slot, selected, mm_clamp01(pop * 1.5f));
        if (selected)
        {
            cursor_x = dx;
            cursor_y = dy;
        }
    }
    /* The cursor glides between tiles. */
    const float glide_x = mm_follow(x, 0u, 1u + page, cursor_x, 0.4f);
    const float glide_y = mm_follow(x, 3u, 1u + page, cursor_y, 0.4f);
    const uint32_t cursor_color = flow->players > 1u ? player_colors[flow->character_player & 3u] : MM_YELLOW;
    mm_corner_brackets(x, mm_x(x, glide_x), mm_y(x, glide_y), mm_s(x, tile), mm_s(x, tile), cursor_color,
                       mm_stagger(x, 8.0f, 8.0f));
    if (pages > 1u)
    {
        /* Roster pages: arrows at the grid's sides and page dots below it. */
        const float mid_y = mm_y(x, panel_y + pad + grid_w * 0.5f);
        UiPaint arrow = ui_paint_solid(MM_WHITE);
        const float nudge = sinf(x->t * 5.0f) * mm_s(x, 5.0f);
        const float lx = mm_x(x, panel_x - 26.0f) - nudge, rx = mm_x(x, panel_x + grid_w + pad * 2.0f + 26.0f) + nudge;
        const float ah = mm_s(x, 34.0f);
        const UiPoint left[3] = {{lx - ah * 0.6f, mid_y}, {lx + ah * 0.4f, mid_y - ah}, {lx + ah * 0.4f, mid_y + ah}};
        const UiPoint right[3] = {{rx + ah * 0.6f, mid_y}, {rx - ah * 0.4f, mid_y + ah}, {rx - ah * 0.4f, mid_y - ah}};
        ui_fill_convex(c, left, 3, &arrow, 1.2f, 0.9f);
        ui_fill_convex(c, right, 3, &arrow, 1.2f, 0.9f);
        const float dot_y = mm_y(x, panel_y + grid_w + pad * 2.0f + 2.0f) - mm_s(x, 20.0f);
        const float span = mm_s(x, 30.0f) * (float)(pages - 1u);
        for (uint8_t p = 0u; p < pages; ++p)
        {
            UiPaint dot = ui_paint_solid(p == page ? MM_YELLOW : 0x90ffffffu);
            ui_fill_circle(c, mm_x(x, panel_x + pad + grid_w * 0.5f) - span * 0.5f + mm_s(x, 30.0f) * (float)p, dot_y,
                           mm_s(x, p == page ? 9.0f : 6.0f), &dot, 1.2f, 1.0f);
        }
    }
    /* Showcase: the highlighted driver's kart turning on its pedestal. */
    const PortableFrontendDriverSlot *driver = portable_frontend_flow_driver_at(flow, page, flow->character_cursor);
    PortableCharacterSlotArt art = {0};
    UiImage emblem;
    const int random = flow->character_cursor == PORTABLE_FRONTEND_RANDOM_SLOT;
    const int mesh = (driver != NULL && driver->mesh_only && mm_portrait_3d(x, driver->content_id, &emblem));
    const int has = !mesh && portable_character_menu_slot_art(x->view->pack, flow, page, flow->character_cursor, &art);
    const float show_x = mm_x(x, 1390.0f), show_y = mm_y(x, 540.0f), show = mm_s(x, 640.0f);
    const float show_in = mm_ease_out(mm_stagger(x, 4.0f, 16.0f));
    const uint32_t selection_ticks = (flow->character_animation_timing & 1u) ? flow->tick - flow->character_selection_tick
                                                                              : flow->page_ticks;
    const float swap = mm_ease_back(mm_clamp01((float)selection_ticks / 10.0f));
    float hop = 0.0f;
    if (flow->character_locked && (flow->character_animation_timing & 2u))
    {
        const float since = (float)(flow->tick - flow->character_lock_tick);
        hop = since < 20.0f ? sinf(since / 20.0f * 3.14159f) : 0.0f;
    }
    if (flow->tick < flow->character_trick_until)
    {
        const float left = (float)(flow->character_trick_until - flow->tick);
        hop = fmaxf(hop, sinf(left / 19.0f * 3.14159f) * 0.7f);
    }
    if (random)
    {
        mm_kart_showcase(x, 0u, show_x, show_y, show, show_in, x->tick, 0.0f);
        UiTextStyle mark = ui_text_style(UI_FONT_BLACK, mm_s(x, 300.0f), MM_WHITE);
        mark.outline = mm_s(x, 10.0f);
        mark.outline_color = 0xff3b2ad1u;
        mark.shadow_color = 0x60001a40u;
        mark.shadow_y = mm_s(x, 12.0f);
        mark.shadow_blur = mm_s(x, 14.0f);
        ui_text_draw_centered(c, &mark, show_x, show_y - mm_s(x, 40.0f) + sinf(x->t * 3.0f) * mm_s(x, 14.0f),
                              UI_ALIGN_CENTER, "?", show_in * swap);
    }
    else if (mesh && !driver_model(x, driver->content_id, show_x + (1.0f - swap) * mm_s(x, 60.0f), show_y, show,
                                   show_in * mm_clamp01(swap * 1.4f), hop))
    {
        /* Without a model, 3D-only drivers show their emblem. */
        const float size = show * 0.55f * show_in * mm_clamp01(swap * 1.4f);
        const float bob = sinf(x->t * 3.0f) * mm_s(x, 10.0f) - hop * mm_s(x, 60.0f);
        ui_draw_image(c, &emblem, show_x - size * 0.5f, show_y - size * 0.5f + bob, size, size, show_in, 0u,
                      MM_WHITE, 0.0f);
    }
    else if (has)
        mm_kart_showcase(x, art.preview, show_x + (1.0f - swap) * mm_s(x, 60.0f), show_y, show, show_in * mm_clamp01(swap * 1.4f),
                         x->tick * 2u, hop);
    /* Stats card. */
    unsigned speed, weight;
    const uint32_t driver_id = driver != NULL ? driver->content_id : 0u;
    if (!random && driver_id != 0u && portable_menu_driver_stats(driver_id, &speed, &weight))
    {
        const float card_in = mm_ease_out(mm_stagger(x, 8.0f, 14.0f));
        const float cx = 1480.0f + (1.0f - card_in) * 300.0f;
        mm_panel(x, cx, 210.0f, 380.0f, 150.0f, 22.0f, card_in);
        stat_bar(x, mm_x(x, cx + 30.0f), mm_y(x, 258.0f), mm_s(x, 320.0f), "Speed", speed, card_in, 1u);
        stat_bar(x, mm_x(x, cx + 30.0f), mm_y(x, 312.0f), mm_s(x, 320.0f), "Weight", weight, card_in, 2u);
    }
    unsigned kart_ratings[3];
    const char *kart = flow->character_locked && flow->selected_kart != 0u
        ? portable_menu_kart(flow->selected_kart, kart_ratings) : NULL;
    if (kart != NULL)
    {
        /* Kart card: Left/Right turn through the karts before confirming. */
        static const char *labels[3] = {"Speed", "Accel", "Handling"};
        const float cx = 1480.0f, cy = 380.0f;
        mm_panel(x, cx, cy, 380.0f, 250.0f, 22.0f, 1.0f);
        UiTextStyle title_style = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), MM_YELLOW);
        title_style.shadow_color = 0x70002a5au;
        title_style.shadow_y = mm_s(x, 2.0f);
        char label[48];
        snprintf(label, sizeof label, "< %s >", kart);
        ui_text_draw_centered(c, &title_style, mm_x(x, cx + 190.0f), mm_y(x, cy + 40.0f), UI_ALIGN_CENTER, label, 1.0f);
        for (unsigned field = 0u; field < 3u; ++field)
            stat_bar(x, mm_x(x, cx + 30.0f), mm_y(x, cy + 100.0f + 54.0f * (float)field), mm_s(x, 320.0f),
                     labels[field], kart_ratings[field], 1.0f, 5u + field);
    }
    if (flow->character_locked)
    {
        /* "Ready!" banner over the pedestal once the driver is locked in. */
        const float since = (flow->character_animation_timing & 2u) ? (float)(flow->tick - flow->character_lock_tick) : 20.0f;
        const float pop = mm_ease_back(mm_clamp01(since / 12.0f));
        UiTextStyle ready = ui_text_style(UI_FONT_ITALIC, mm_s(x, 96.0f) * pop, MM_YELLOW);
        ready.outline = mm_s(x, 8.0f) * pop;
        ready.outline_color = 0xff7a3d00u;
        ready.shadow_color = 0x80001a40u;
        ready.shadow_y = mm_s(x, 8.0f);
        ready.shadow_blur = mm_s(x, 10.0f);
        ui_text_draw_centered(c, &ready, show_x, mm_y(x, 900.0f), UI_ALIGN_CENTER, "Ready!", mm_clamp01(since / 6.0f));
    }
    /* Footer: the highlighted driver's name in the middle, MK8-style. */
    const char *name = random ? "Random" : driver_id != 0u ? portable_menu_driver_name(driver_id) : NULL;
    const MmHint locked_hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, "Continue"}};
    const MmHint kart_hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_DPAD, "Kart"}, {MM_BUTTON_A, "Continue"}};
    const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_SELECT, "Horn"}, {MM_BUTTON_A, "OK"}};
    if (kart != NULL)
        mm_footer(x, name, kart_hints, 3u);
    else if (flow->character_locked)
        mm_footer(x, name, locked_hints, 2u);
    else
        mm_footer(x, name, hints, 3u);
    if (name == NULL && has) name_art(x, art.name, (float)c->width * 0.5f, (float)c->height - mm_s(x, 48.0f), mm_s(x, 44.0f),
                                      UI_ALIGN_CENTER, 1.0f);
    return 1;
}

/* ---- Course select ------------------------------------------------------- */

static const PortableFrontendCourseSlot *course_at(const PortableFrontendFlow *flow, unsigned cup, unsigned track)
{
    for (size_t i = 0u; i < flow->config.course_count; ++i)
        if (flow->config.courses[i].cup == cup && flow->config.courses[i].track == track) return &flow->config.courses[i];
    return NULL;
}

static void course_label(MmCtx *x, unsigned cup, unsigned track, const UiTextStyle *style, float px, float py,
                         UiAlign align, float max_w, float alpha)
{
    const PortableFrontendCourseSlot *slot = course_at(x->flow, cup, track);
    const char *name = slot != NULL ? portable_menu_course_name(slot->content_id) : NULL;
    if (name != NULL)
    {
        UiTextStyle fitted = ui_text_fit(style, name, max_w);
        ui_text_draw_centered(x->c, &fitted, px, py, align, name, alpha);
        return;
    }
    const UiImage *art = ui_art_image(x->menu->art, x->view->pack, PORTABLE_COURSE_NAME + cup * 4u + track, 0);
    if (art != NULL)
        name_art(x, PORTABLE_COURSE_NAME + cup * 4u + track, px, py, style->size * 0.9f, align, alpha);
    else
    {
        char text[24];
        snprintf(text, sizeof text, slot != NULL ? "Course %u" : "---", slot != NULL ? (unsigned)slot->content_id : 0u);
        ui_text_draw_centered(x->c, style, px, py, align, text, alpha);
    }
}

static uint32_t cup_icon(const PortableAssetPackView *pack, unsigned course_cup, int *locked_art)
{
    const PortableTrackSet set = portable_track_set_of_cup((uint8_t)course_cup);
    const unsigned index = course_cup - portable_track_set_first_cup(set);
    (void)pack;
    *locked_art = 0;
    if (set == PORTABLE_TRACK_SET_CUSTOM) return PORTABLE_COURSE_CUP + 15u + index;
    return PORTABLE_COURSE_CUP + portable_track_set_first_cup(set) + index;
}

static void cup_name_text(unsigned course_cup, char out[40])
{
    const char *name = portable_menu_cup_name(course_cup);
    if (name != NULL)
        snprintf(out, 40, "%s", name);
    else
        snprintf(out, 40, "Custom Cup %u", course_cup - 10u);
}

static void preview_card(MmCtx *x, unsigned cup, unsigned track, float dx, float dy, float dw, float dh)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    const float appear = mm_ease_out(mm_stagger(x, 4.0f, 16.0f));
    dx += (1.0f - appear) * 260.0f;
    mm_panel(x, dx, dy, dw, dh, 30.0f, appear);
    const float ix = dx + 30.0f, iy = dy + 30.0f, iw = dw - 60.0f, ih = dh - 210.0f;
    UiPaint well = ui_paint_linear(0.0f, mm_y(x, iy), 0xff1d6fd0u, 0.0f, mm_y(x, iy + ih), 0xff0a3a8au);
    ui_fill_round_rect(c, mm_x(x, ix), mm_y(x, iy), mm_s(x, iw), mm_s(x, ih), mm_s(x, 20.0f), 0.0f, &well, 1.2f, appear);
    const uint32_t id = PORTABLE_COURSE_PREVIEW + cup * 4u + track;
    const UiImage *image = mm_art(x, id);
    const float swap = mm_ease_out(mm_clamp01((float)(x->tick % 100000u - x->menu->smooth_key[5]) / 8.0f));
    if (x->menu->smooth_value[5] != (float)(cup * 4u + track) || x->menu->smooth_key[5] > x->tick)
    {
        x->menu->smooth_value[5] = (float)(cup * 4u + track);
        x->menu->smooth_key[5] = x->tick % 100000u;
    }
    if (image != NULL)
    {
        const int map = image->width == image->height; /* Course maps are square silhouettes. */
        float scale = mm_s(x, iw - 40.0f) / (float)image->width;
        if ((float)image->height * scale > mm_s(x, ih - 40.0f)) scale = mm_s(x, ih - 40.0f) / (float)image->height;
        const float w = (float)image->width * scale, h = (float)image->height * scale;
        const float cx = mm_x(x, ix + iw * 0.5f), cy = mm_y(x, iy + ih * 0.5f);
        if (map)
            ui_draw_image(c, image, cx - w * 0.5f + mm_s(x, 6.0f), cy - h * 0.5f + mm_s(x, 8.0f), w, h, appear * swap * 0.5f,
                          UI_IMAGE_SILHOUETTE, 0xff04224fu, 0.0f);
        ui_draw_image(c, image, cx - w * 0.5f, cy - h * 0.5f, w, h, appear * swap, 0u, MM_WHITE, map ? 0.0f : mm_s(x, 16.0f));
    }
    else
    {
        UiTextStyle none = ui_text_style(UI_FONT_BLACK, mm_s(x, 40.0f), 0xb0ffffffu);
        ui_text_draw_centered(c, &none, mm_x(x, ix + iw * 0.5f), mm_y(x, iy + ih * 0.5f), UI_ALIGN_CENTER, "No preview", appear);
    }
    UiTextStyle name = ui_text_style(UI_FONT_ITALIC, mm_s(x, 54.0f), MM_WHITE);
    name.shadow_color = 0x80002a5au;
    name.shadow_y = mm_s(x, 4.0f);
    name.shadow_blur = mm_s(x, 6.0f);
    course_label(x, cup, track, &name, mm_x(x, dx + dw * 0.5f), mm_y(x, dy + dh - 140.0f), UI_ALIGN_CENTER,
                 mm_s(x, dw - 60.0f), appear);
    char cup_text[40];
    cup_name_text(cup, cup_text);
    UiTextStyle sub = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), 0xff0b4f9cu);
    ui_text_draw_centered(c, &sub, mm_x(x, dx + dw * 0.5f), mm_y(x, dy + dh - 76.0f), UI_ALIGN_CENTER, cup_text, appear);
    /* GP: the saved trophy and rank for this cup and class. */
    if (flow->mode_cursor == 0u && x->view->progress != NULL && cup < PORTABLE_FRONTEND_ARENA_CUP && flow->race_speed < 3u)
    {
        const int grade = x->view->progress->grade[cup / 5u][cup % 5u][flow->race_speed];
        const unsigned rating = x->view->progress->rating[cup / 5u][cup % 5u][flow->race_speed];
        if (grade >= 0 && grade <= 2)
            mm_image_fit(x, PORTABLE_COURSE_TROPHY + (unsigned)grade * 16u + (x->tick / 7u) % 16u, mm_x(x, dx + dw - 90.0f),
                         mm_y(x, dy + 100.0f), mm_s(x, 130.0f), mm_s(x, 130.0f), appear, 0u);
        if (rating <= 7u)
            mm_image_fit(x, PORTABLE_COURSE_RANK + rating, mm_x(x, dx + 140.0f), mm_y(x, dy + 76.0f), mm_s(x, 210.0f),
                         mm_s(x, 80.0f), appear, 0u);
    }
}

static void set_tabs(MmCtx *x, float dx, float dy)
{
    const PortableFrontendFlow *flow = x->flow;
    static const char *const names[3] = {"Super Circuit", "Extra", "Custom"};
    static const PortableTrackSet sets[3] = {PORTABLE_TRACK_SET_GBA, PORTABLE_TRACK_SET_EXTRA, PORTABLE_TRACK_SET_CUSTOM};
    const int battle = flow->mode_cursor == 3u;
    const int base = portable_frontend_flow_group_allowed(flow, 0u);
    int present[3] = {battle && base, 0, 0}, count = battle && base;
    for (size_t i = 0u; base && !battle && i < flow->config.course_count; ++i)
        for (unsigned s = 0u; s < 3u; ++s)
            if (portable_track_set_of_cup(flow->config.courses[i].cup) == sets[s]) present[s] = 1;
    for (unsigned s = 1u; !battle && s < 3u; ++s) count += present[s];
    count += present[0] && !battle;
    for (uint8_t g = 1u; g <= flow->mesh_group_count; ++g)
        count += portable_frontend_flow_group_allowed(flow, g) && portable_frontend_flow_mesh_group_open(flow, g);
    if (count < 2) return;
    const PortableTrackSet current = portable_track_set_of_cup(flow->course_cup);
    UiCanvas *c = x->c;
    float px = mm_x(x, dx);
    const float py = mm_y(x, dy), h = mm_s(x, 58.0f);
    px += mm_button_glyph(x, px, py + h * 0.5f, mm_s(x, 44.0f), MM_BUTTON_L, 1.0f) + mm_s(x, 16.0f);
    const float right = mm_x(x, 1820.0f);
    for (unsigned s = 0u; s < 3u + flow->mesh_group_count; ++s)
    {
        const char *name;
        int on;
        if (s < 3u)
        {
            if (!present[s]) continue;
            name = battle ? "Battle Courses" : names[s];
            on = flow->mesh_group == 0u && (battle || sets[s] == current);
        }
        else
        {
            const uint8_t group = (uint8_t)(s - 2u);
            if (!portable_frontend_flow_group_allowed(flow, group) ||
                !portable_frontend_flow_mesh_group_open(flow, group)) continue;
            name = flow->mesh_group_names[group - 1u];
            on = flow->mesh_group == group;
        }
        UiTextStyle text = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), on ? 0xff3a2a00u : MM_WHITE);
        text = ui_text_fit(&text, name, mm_s(x, 360.0f));
        const float w = ui_text_width(&text, name) + mm_s(x, 56.0f);
        if (px + w > right) break;
        UiPaint pill = on ? ui_paint_linear(0.0f, py, 0xfffff38cu, 0.0f, py + h, 0xffffc21au) : ui_paint_solid(0x50002a5au);
        ui_fill_round_rect(c, px, py, w, h, h * 0.5f, 0.25f, &pill, 1.2f, 1.0f);
        ui_text_draw_centered(c, &text, px + w * 0.5f, py + h * 0.5f, UI_ALIGN_CENTER, name, 1.0f);
        px += w + mm_s(x, 12.0f);
    }
    mm_button_glyph(x, px + mm_s(x, 4.0f), py + h * 0.5f, mm_s(x, 44.0f), MM_BUTTON_R, 1.0f);
}

static const char *mesh_cup_name(const PortableFrontendFlow *flow, size_t index, char out[40])
{
    const PortableFrontendMeshCup *cup = &flow->mesh_cups[index];
    if (cup->name != NULL && cup->name[0])
        return cup->name;
    unsigned number = 1u;
    for (size_t i = 0u; i < index; ++i)
        number += flow->mesh_cups[i].group == cup->group;
    snprintf(out, 40, "Cup %u", number);
    return out;
}

/* A 3D cup group: the same cup row / course list / preview layout as the
 * stock sets, labelled from the cup and course data. */
static int mesh_course_page(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    const PortableFrontendMeshCup *cup = &flow->mesh_cups[flow->mesh_cup];
    const uint32_t mode = portable_frontend_flow_mesh_mode(flow);
    set_tabs(x, 120.0f, 170.0f);
    unsigned open[PORTABLE_FRONTEND_MESH_CUP_CAPACITY], count = 0u;
    for (size_t i = 0u; i < flow->mesh_cup_count; ++i)
        if (flow->mesh_cups[i].group + 1u == flow->mesh_group && portable_frontend_flow_mesh_cup_open(flow, i))
            open[count++] = (unsigned)i;
    const float gap = 18.0f, span = 960.0f;
    const float tile = fminf(168.0f, (span - gap * (float)(count ? count - 1u : 0u)) / (float)(count ? count : 1u));
    for (unsigned index = 0u; index < count; ++index)
    {
        const int selected = open[index] == flow->mesh_cup;
        const float pop = mm_ease_back(mm_stagger(x, (float)index * 2.0f, 12.0f));
        const float size = tile * (selected ? 1.08f : 1.0f) * pop;
        const float dx = 120.0f + (float)index * (tile + gap) + (tile - size) * 0.5f, dy = 252.0f + (168.0f - size) * 0.5f;
        const float px = mm_x(x, dx), py = mm_y(x, dy), ps = mm_s(x, size);
        UiPaint shadow = ui_paint_solid(0x40002050u);
        ui_fill_round_rect(c, px, py + ps * 0.05f, ps, ps, ps * 0.16f, 0.0f, &shadow, ps * 0.1f, pop);
        UiPaint card = selected ? ui_paint_linear(0.0f, py, 0xfffff6a0u, 0.0f, py + ps, 0xffffc21au)
                                : ui_paint_linear(0.0f, py, 0xe8ffffffu, 0.0f, py + ps, 0xd0cfe8f6u);
        ui_fill_round_rect(c, px, py, ps, ps, ps * 0.16f, 0.0f, &card, 1.2f, mm_clamp01(pop * 1.4f));
        char fallback[40], words[40];
        const char *name = mesh_cup_name(flow, open[index], fallback);
        /* The tile shows the cup's first word ("Mushroom" of "Mushroom Cup"). */
        snprintf(words, sizeof words, "%s", name);
        char *space = strchr(words, ' ');
        if (space != NULL && space != words) *space = 0;
        UiTextStyle label = ui_text_style(UI_FONT_BLACK, ps * 0.24f, selected ? 0xff3a2a00u : 0xff0b4f9cu);
        label = ui_text_fit(&label, words, ps * 0.86f);
        ui_text_draw_centered(c, &label, px + ps * 0.5f, py + ps * 0.5f, UI_ALIGN_CENTER, words, mm_clamp01(pop * 1.4f));
        if (selected)
            mm_corner_brackets(x, px, py, ps, ps, MM_YELLOW, mm_stagger(x, 6.0f, 6.0f));
    }
    char fallback[40];
    UiTextStyle cup_style = ui_text_style(UI_FONT_ITALIC, mm_s(x, 50.0f), MM_WHITE);
    cup_style.shadow_color = 0x70002a5au;
    cup_style.shadow_y = mm_s(x, 4.0f);
    cup_style.shadow_blur = mm_s(x, 6.0f);
    const char *cup_text = mesh_cup_name(flow, flow->mesh_cup, fallback);
    cup_style = ui_text_fit(&cup_style, cup_text, mm_s(x, 940.0f));
    ui_text_draw_centered(c, &cup_style, mm_x(x, 130.0f), mm_y(x, 474.0f), UI_ALIGN_LEFT, cup_text, mm_stagger(x, 4.0f, 10.0f));
    /* Course rows; ones the selected mode cannot race are dimmed. */
    const unsigned rows = cup->course_count;
    const float pitch = fminf(96.0f, 384.0f / (float)rows), bar_h = pitch - 14.0f;
    const unsigned highlighted = flow->mode_cursor == 0u ? flow->gp_preview_track % rows : flow->course_track;
    const int picking = flow->course_phase >= 1u;
    for (unsigned row = 0u; row < rows; ++row)
    {
        const float appear = mm_ease_out(mm_stagger(x, 3.0f + (float)row * 2.0f, 12.0f));
        const float dy = 520.0f + (float)row * pitch, dx = 120.0f + (1.0f - appear) * -500.0f, dw = 900.0f;
        const int lit = row == highlighted;
        const int selected = picking && lit && flow->mode_cursor != 0u;
        const int usable = (cup->course_modes[row] & mode) != 0u;
        if (selected)
            mm_tab(x, dx, dy, dw, bar_h, "", NULL, 1, appear, MM_ICON_NONE, 0u, 0);
        else
        {
            UiPaint bar = lit ? ui_paint_linear(0.0f, mm_y(x, dy), 0xf0ffffffu, 0.0f, mm_y(x, dy + bar_h), 0xf0d8f0ffu)
                              : ui_paint_linear(0.0f, mm_y(x, dy), 0x90ffffffu, 0.0f, mm_y(x, dy + bar_h), 0x70d0e8f8u);
            ui_fill_chevron_bar(c, mm_x(x, dx), mm_y(x, dy), mm_s(x, dw), mm_s(x, bar_h), mm_s(x, 20.0f), &bar, 1.2f,
                                appear * (usable ? 1.0f : 0.5f));
        }
        char number[4];
        snprintf(number, sizeof number, "%u", row + 1u);
        const float text = fminf(40.0f, bar_h * 0.45f);
        UiTextStyle num = ui_text_style(UI_FONT_ITALIC, mm_s(x, text), selected ? 0xff8a5a00u : 0xff3fa0e8u);
        ui_text_draw_centered(c, &num, mm_x(x, dx + 54.0f), mm_y(x, dy + bar_h * 0.5f), UI_ALIGN_CENTER, number, appear);
        const char *name = cup->course_names[row] != NULL && cup->course_names[row][0] ? cup->course_names[row] : "Course";
        UiTextStyle label = ui_text_style(UI_FONT_BLACK, mm_s(x, text * 0.95f),
                                          selected ? 0xff3a2a00u : usable ? MM_INK : 0xff7d8a96u);
        label = ui_text_fit(&label, name, mm_s(x, dw - 160.0f));
        ui_text_draw_centered(c, &label, mm_x(x, dx + 100.0f), mm_y(x, dy + bar_h * 0.5f), UI_ALIGN_LEFT, name,
                              appear * (usable ? 1.0f : 0.6f));
    }
    /* Preview card: META picture, course name and the providing group. */
    const float dx = 1100.0f, dy = 230.0f, dw = 720.0f, dh = 700.0f;
    const float appear = mm_ease_out(mm_stagger(x, 4.0f, 16.0f));
    const float cx0 = dx + (1.0f - appear) * 260.0f;
    mm_panel(x, cx0, dy, dw, dh, 30.0f, appear);
    const float ix = cx0 + 30.0f, iy = dy + 30.0f, iw = dw - 60.0f, ih = dh - 210.0f;
    UiPaint well = ui_paint_linear(0.0f, mm_y(x, iy), 0xff1d6fd0u, 0.0f, mm_y(x, iy + ih), 0xff0a3a8au);
    ui_fill_round_rect(c, mm_x(x, ix), mm_y(x, iy), mm_s(x, iw), mm_s(x, ih), mm_s(x, 20.0f), 0.0f, &well, 1.2f, appear);
    PortableNativeImage art;
    if (x->view->mesh_preview != NULL &&
        x->view->mesh_preview(x->view->mesh_preview_owner, cup->course_ids[highlighted], &art) && art.width && art.height)
    {
        const UiImage image = {(const uint32_t *)(const void *)art.pixels, art.width, art.height};
        const float scale = fminf(mm_s(x, iw - 40.0f) / (float)art.width, mm_s(x, ih - 40.0f) / (float)art.height);
        const float w = (float)art.width * scale, h = (float)art.height * scale;
        ui_draw_image(c, &image, mm_x(x, ix + iw * 0.5f) - w * 0.5f, mm_y(x, iy + ih * 0.5f) - h * 0.5f, w, h, appear,
                      UI_IMAGE_NEAREST, MM_WHITE, mm_s(x, 12.0f));
    }
    else
    {
        UiTextStyle none = ui_text_style(UI_FONT_BLACK, mm_s(x, 40.0f), 0xb0ffffffu);
        ui_text_draw_centered(c, &none, mm_x(x, ix + iw * 0.5f), mm_y(x, iy + ih * 0.5f), UI_ALIGN_CENTER,
                              (cup->explore_only >> highlighted) & 1u ? "Explore only" : "No preview", appear);
    }
    const char *course_name = cup->course_names[highlighted] != NULL && cup->course_names[highlighted][0]
                                  ? cup->course_names[highlighted] : "Course";
    UiTextStyle name = ui_text_style(UI_FONT_ITALIC, mm_s(x, 54.0f), MM_WHITE);
    name.shadow_color = 0x80002a5au;
    name.shadow_y = mm_s(x, 4.0f);
    name.shadow_blur = mm_s(x, 6.0f);
    name = ui_text_fit(&name, course_name, mm_s(x, dw - 60.0f));
    ui_text_draw_centered(c, &name, mm_x(x, cx0 + dw * 0.5f), mm_y(x, dy + dh - 140.0f), UI_ALIGN_CENTER, course_name, appear);
    const char *group = flow->mesh_group_names[flow->mesh_group - 1u];
    UiTextStyle sub = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), 0xff0b4f9cu);
    sub = ui_text_fit(&sub, group, mm_s(x, dw - 60.0f));
    ui_text_draw_centered(c, &sub, mm_x(x, cx0 + dw * 0.5f), mm_y(x, dy + dh - 76.0f), UI_ALIGN_CENTER, group, appear);
    if (flow->mode_cursor == 0u && flow->course_phase == 1u)
    {
        const float pulse = 0.75f + 0.25f * sinf(x->t * 6.0f);
        UiTextStyle go = ui_text_style(UI_FONT_ITALIC, mm_s(x, 64.0f), MM_YELLOW);
        go.outline = mm_s(x, 6.0f);
        go.outline_color = 0xff7a3d00u;
        go.shadow_color = 0x70001a40u;
        go.shadow_y = mm_s(x, 6.0f);
        go.shadow_blur = mm_s(x, 8.0f);
        ui_text_draw_centered(c, &go, mm_x(x, 570.0f), mm_y(x, 520.0f + 384.0f + 36.0f), UI_ALIGN_CENTER,
                              "Start this cup?", pulse);
    }
    const int start = flow->course_phase >= 1u;
    const MmHint phase0[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, "OK"}, {MM_BUTTON_DPAD, "Choose"},
                             {MM_BUTTON_LR, "Course set"}};
    const MmHint phase1[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, (cup->explore_only >> highlighted) & 1u ? "Explore" : "Start"},
                             {MM_BUTTON_DPAD, "Choose"}, {MM_BUTTON_SELECT, "Explore"}};
    const MmHint gp[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, "Start"}, {MM_BUTTON_SELECT, "Explore"}};
    if (!start)
        mm_footer(x, NULL, phase0, flow->platform != 0u ? 3u : 4u); /* A platform has one set. */
    else if (flow->mode_cursor == 0u)
        mm_footer(x, NULL, gp, 3u);
    else
        mm_footer(x, NULL, phase1, 4u);
    return 1;
}

static void ghost_choice(MmCtx *x);

int mm_page_course(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    mm_background(x, 0);
    const char *title, *subtitle;
    mm_mode_names(flow, &title, &subtitle);
    mm_header(x, title, subtitle);
    if (flow->mesh_group != 0u && flow->mesh_cup < flow->mesh_cup_count)
        return mesh_course_page(x);
    const int battle = flow->mode_cursor == 3u;
    const unsigned cup = battle ? PORTABLE_FRONTEND_ARENA_CUP : flow->course_cup;
    const unsigned track = flow->mode_cursor == 0u ? flow->gp_preview_track : flow->course_track;
    float list_y = 470.0f;
    if (battle)
        set_tabs(x, 120.0f, 170.0f);
    if (!battle)
    {
        set_tabs(x, 120.0f, 170.0f);
        const PortableTrackSet set = portable_track_set_of_cup(flow->course_cup);
        const unsigned first = portable_track_set_first_cup(set);
        const uint8_t access = portable_frontend_flow_cup_access(flow);
        const float tile = 168.0f, gap = 22.0f;
        for (unsigned index = 0u; index < 5u; ++index)
        {
            const unsigned course_cup = first + index;
            const int custom = set == PORTABLE_TRACK_SET_CUSTOM;
            const int open = (access >> index) & 1u;
            if (custom && !open) continue;
            const int selected = course_cup == flow->course_cup;
            const float pop = mm_ease_back(mm_stagger(x, (float)index * 2.0f, 12.0f));
            const float grow = selected ? 1.08f : 1.0f;
            const float size = tile * grow * pop;
            const float dx = 120.0f + (float)index * (tile + gap) + (tile - size) * 0.5f, dy = 252.0f + (tile - size) * 0.5f;
            const float px = mm_x(x, dx), py = mm_y(x, dy), ps = mm_s(x, size);
            UiPaint shadow = ui_paint_solid(0x40002050u);
            ui_fill_round_rect(c, px, py + ps * 0.05f, ps, ps, ps * 0.16f, 0.0f, &shadow, ps * 0.1f, pop);
            UiPaint card = selected ? ui_paint_linear(0.0f, py, 0xfffff6a0u, 0.0f, py + ps, 0xffffc21au)
                                    : ui_paint_linear(0.0f, py, 0xe8ffffffu, 0.0f, py + ps, 0xd0cfe8f6u);
            ui_fill_round_rect(c, px, py, ps, ps, ps * 0.16f, 0.0f, &card, 1.2f, mm_clamp01(pop * 1.4f));
            int locked_art;
            uint32_t icon = cup_icon(x->view->pack, course_cup, &locked_art);
            if (mm_art(x, icon) == NULL) icon = PORTABLE_COURSE_CUP + index;
            const float spin = selected ? 1.0f + 0.04f * sinf(x->t * 6.0f) : 1.0f;
            mm_image_fit(x, icon, px + ps * 0.5f, py + ps * 0.5f, ps * 0.8f * spin, ps * 0.8f * spin, mm_clamp01(pop * 1.4f),
                         open ? 0u : UI_IMAGE_GRAY);
            if (!open)
            {
                /* Padlock. */
                const float lx = px + ps * 0.5f, ly = py + ps * 0.56f, lr = ps * 0.16f;
                UiPaint body = ui_paint_solid(0xff39424du);
                ui_stroke_circle(c, lx, ly - lr * 0.7f, lr * 0.75f, lr * 0.28f, 0xff39424du, 1.0f);
                ui_fill_round_rect(c, lx - lr, ly - lr * 0.5f, lr * 2.0f, lr * 1.6f, lr * 0.3f, 0.0f, &body, 1.2f, 1.0f);
            }
            if (selected)
                mm_corner_brackets(x, px, py, ps, ps, MM_YELLOW, mm_stagger(x, 6.0f, 6.0f));
        }
        char cup_text[40];
        cup_name_text(flow->course_cup, cup_text);
        UiTextStyle cup_style = ui_text_style(UI_FONT_ITALIC, mm_s(x, 50.0f), MM_WHITE);
        cup_style.shadow_color = 0x70002a5au;
        cup_style.shadow_y = mm_s(x, 4.0f);
        cup_style.shadow_blur = mm_s(x, 6.0f);
        ui_text_draw_centered(c, &cup_style, mm_x(x, 130.0f), mm_y(x, 474.0f), UI_ALIGN_LEFT, cup_text, mm_stagger(x, 4.0f, 10.0f));
        list_y = 520.0f;
    }
    else
        list_y = 250.0f;
    /* Track list. In GP the cycling preview is lit; elsewhere phase 1 picks. */
    const int picking = flow->course_phase >= 1u;
    for (unsigned row = 0u; row < 4u; ++row)
    {
        const float appear = mm_ease_out(mm_stagger(x, 3.0f + (float)row * 2.0f, 12.0f));
        const float dy = list_y + (float)row * 106.0f;
        const int highlighted = row == track;
        const int selected = picking && highlighted && flow->mode_cursor != 0u;
        const float dx = 120.0f + (1.0f - appear) * -500.0f;
        const float dw = 900.0f, dh = 90.0f;
        UiPaint bar;
        if (selected)
        {
            mm_tab(x, dx, dy, dw, dh, "", NULL, 1, appear, MM_ICON_NONE, 0u, 0);
            bar = ui_paint_solid(0u);
        }
        else
        {
            bar = highlighted ? ui_paint_linear(0.0f, mm_y(x, dy), 0xf0ffffffu, 0.0f, mm_y(x, dy + dh), 0xf0d8f0ffu)
                              : ui_paint_linear(0.0f, mm_y(x, dy), 0x90ffffffu, 0.0f, mm_y(x, dy + dh), 0x70d0e8f8u);
            if (flow->mode_cursor == 0u && flow->course_phase == 1u)
                bar = ui_paint_linear(0.0f, mm_y(x, dy), 0xf8ffffffu, 0.0f, mm_y(x, dy + dh), 0xf0e2f4ffu);
            ui_fill_chevron_bar(c, mm_x(x, dx), mm_y(x, dy), mm_s(x, dw), mm_s(x, dh), mm_s(x, 20.0f), &bar, 1.2f, appear);
        }
        char number[4];
        snprintf(number, sizeof number, "%u", row + 1u);
        UiTextStyle num = ui_text_style(UI_FONT_ITALIC, mm_s(x, 40.0f), selected ? 0xff8a5a00u : 0xff3fa0e8u);
        ui_text_draw_centered(c, &num, mm_x(x, dx + 54.0f), mm_y(x, dy + dh * 0.5f), UI_ALIGN_CENTER, number, appear);
        UiTextStyle label = ui_text_style(UI_FONT_BLACK, mm_s(x, 38.0f), selected ? 0xff3a2a00u : MM_INK);
        course_label(x, cup, row, &label, mm_x(x, dx + 100.0f), mm_y(x, dy + dh * 0.5f), UI_ALIGN_LEFT,
                     mm_s(x, dw - 160.0f), appear);
    }
    preview_card(x, cup, track, 1100.0f, 230.0f, 720.0f, 700.0f);
    if (flow->mode_cursor == 0u && flow->course_phase == 1u)
    {
        /* GP confirmation: pulsing start prompt over the track list. */
        const float pulse = 0.75f + 0.25f * sinf(x->t * 6.0f);
        UiTextStyle go = ui_text_style(UI_FONT_ITALIC, mm_s(x, 64.0f), MM_YELLOW);
        go.outline = mm_s(x, 6.0f);
        go.outline_color = 0xff7a3d00u;
        go.shadow_color = 0x70001a40u;
        go.shadow_y = mm_s(x, 6.0f);
        go.shadow_blur = mm_s(x, 8.0f);
        ui_text_draw_centered(c, &go, mm_x(x, 570.0f), mm_y(x, list_y + 4.0f * 106.0f + 30.0f), UI_ALIGN_CENTER,
                              "Start this cup?", pulse);
    }
    const char *confirm = flow->course_phase == 0u && !battle ? "OK" : flow->mode_cursor == 0u || flow->course_phase >= 1u ? "Start" : "OK";
    if (flow->mode_cursor == 1u && flow->course_phase == 1u) confirm = "OK";
    MmHint hints[4] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, confirm}, {MM_BUTTON_DPAD, "Choose"},
        {MM_BUTTON_LR, "Course set"}};
    int sets = 0;
    for (uint8_t g = 1u; g <= flow->mesh_group_count; ++g)
        sets |= portable_frontend_flow_group_allowed(flow, g) && portable_frontend_flow_mesh_group_open(flow, g);
    sets = sets && (flow->course_phase == 0u || (battle && flow->course_phase == 1u));
    mm_footer(x, NULL, hints, sets ? 4u : 3u);
    if (flow->course_phase == 2u && flow->mode_cursor == 1u) ghost_choice(x);
    return 1;
}

/* ---- Time Trial ghost choice ------------------------------------------- */

static void ghost_choice(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    ui_tint_rect(c, 0, 0, c->width, c->height, 0xff001830u, 0.45f);
    const float dw = 900.0f, dh = flow->ghost_choices ? 520.0f : 320.0f, dx = 510.0f, dy = 360.0f - dh * 0.25f;
    mm_panel(x, dx, dy, dw, dh, 30.0f, 1.0f);
    UiPaint face = ui_paint_linear(0.0f, mm_y(x, dy), 0xf4ffffffu, 0.0f, mm_y(x, dy + dh), 0xf4e2f1fau);
    ui_fill_round_rect(c, mm_x(x, dx), mm_y(x, dy), mm_s(x, dw), mm_s(x, dh), mm_s(x, 30.0f), 0.0f, &face, 1.2f, 1.0f);
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 56.0f), 0xff0b5fb8u);
    ui_text_draw_centered(c, &head, mm_x(x, 960.0f), mm_y(x, dy + 80.0f), UI_ALIGN_CENTER,
                          flow->ghost_choices ? "Race a ghost" : "Ready to race?", 1.0f);
    static const char *const names[3] = {"Your ghost", "Received ghost 1", "Received ghost 2"};
    const PortableTimeTrialRecords *records = x->view->records;
    float ry = dy + 150.0f;
    for (unsigned i = 0u; i < 3u && flow->ghost_choices; ++i)
    {
        if (!((flow->ghost_choices >> i) & 1u)) continue;
        const int selected = flow->ghost_choice == i;
        if (selected)
        {
            UiPaint bar = ui_paint_linear(0.0f, mm_y(x, ry), 0xfffff38cu, 0.0f, mm_y(x, ry + 90.0f), 0xffffc61au);
            ui_fill_round_rect(c, mm_x(x, dx + 40.0f), mm_y(x, ry), mm_s(x, dw - 80.0f), mm_s(x, 90.0f), mm_s(x, 16.0f), 0.0f,
                               &bar, 1.2f, 1.0f);
        }
        UiTextStyle text = ui_text_style(UI_FONT_BLACK, mm_s(x, 40.0f), selected ? 0xff3a2a00u : MM_INK);
        ui_text_draw_centered(c, &text, mm_x(x, dx + 80.0f), mm_y(x, ry + 45.0f), UI_ALIGN_LEFT, names[i], 1.0f);
        if (records != NULL)
        {
            const uint32_t ticks = i == 0u ? records->ghost.ticks : records->received[i - 1u].ticks;
            if (ticks != 0u)
            {
                char time[16];
                mm_format_ticks(ticks, time);
                ui_text_draw_centered(c, &text, mm_x(x, dx + dw - 80.0f), mm_y(x, ry + 45.0f), UI_ALIGN_RIGHT, time, 1.0f);
            }
        }
        ry += 104.0f;
    }
    if (!flow->ghost_choices)
    {
        UiTextStyle text = ui_text_style(UI_FONT_BOLD, mm_s(x, 32.0f), MM_INK_SOFT);
        ui_text_draw_centered(c, &text, mm_x(x, 960.0f), mm_y(x, dy + 180.0f), UI_ALIGN_CENTER,
                              "No ghost saved for this course yet.", 1.0f);
    }
    const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_A, "Start"}};
    mm_footer(x, NULL, hints, 2u);
}

/* ---- Records ------------------------------------------------------------- */

static void confirm_dialog(MmCtx *x, const char *question, const char *detail)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    ui_tint_rect(c, 0, 0, c->width, c->height, 0xff001830u, 0.5f);
    const float dw = 860.0f, dh = 380.0f, dx = 530.0f, dy = 300.0f;
    mm_panel(x, dx, dy, dw, dh, 30.0f, 1.0f);
    UiPaint face = ui_paint_linear(0.0f, mm_y(x, dy), 0xf6ffffffu, 0.0f, mm_y(x, dy + dh), 0xf6e2f1fau);
    ui_fill_round_rect(c, mm_x(x, dx), mm_y(x, dy), mm_s(x, dw), mm_s(x, dh), mm_s(x, 30.0f), 0.0f, &face, 1.2f, 1.0f);
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 48.0f), 0xff0b5fb8u);
    head = ui_text_fit(&head, question, mm_s(x, dw - 80.0f));
    ui_text_draw_centered(c, &head, mm_x(x, 960.0f), mm_y(x, dy + 80.0f), UI_ALIGN_CENTER, question, 1.0f);
    if (detail != NULL)
    {
        UiTextStyle text = ui_text_style(UI_FONT_BOLD, mm_s(x, 30.0f), MM_INK_SOFT);
        ui_text_draw_centered(c, &text, mm_x(x, 960.0f), mm_y(x, dy + 150.0f), UI_ALIGN_CENTER, detail, 1.0f);
    }
    for (unsigned choice = 0u; choice < 2u; ++choice)
    {
        const int selected = flow->record_erase_choice == choice;
        mm_tab(x, dx + 90.0f + (float)choice * 360.0f, dy + 220.0f, 320.0f, 100.0f, choice ? "Yes" : "No", NULL,
               selected, 1.0f, MM_ICON_NONE, 0u, 0);
    }
}

int mm_page_records(MmCtx *x)
{
    const PortableFrontendFlow *flow = x->flow;
    UiCanvas *c = x->c;
    mm_background(x, 0);
    mm_header(x, "Records", flow->course_phase >= 4u ? "Ghosts" : NULL);
    uint8_t entries[PORTABLE_FRONTEND_RECORDS_MAX];
    const unsigned count = portable_frontend_flow_records_list(flow, entries);
    unsigned index = 0u;
    while (index < count && entries[index] != flow->course_cup * 4u + flow->course_track) ++index;
    /* Scrolling course list on the left. */
    const unsigned visible = 7u;
    const float scroll_target = index >= visible - 2u ? (float)(index - (visible - 2u)) : 0.0f;
    const float scroll = mm_follow(x, 4u, 9u, scroll_target, 0.3f);
    const int saved[4] = {c->clip_x0, c->clip_y0, c->clip_x1, c->clip_y1};
    ui_canvas_clip(c, saved[0], (int)mm_y(x, 196.0f), saved[2], (int)mm_y(x, 196.0f + 104.0f * (float)visible));
    for (unsigned row = 0u; row < count; ++row)
    {
        const float dy = 200.0f + ((float)row - scroll) * 104.0f;
        if (dy < 100.0f || dy > 1000.0f) continue;
        const int selected = row == index;
        const unsigned cup = entries[row] / 4u, track = entries[row] % 4u;
        const float appear = mm_ease_out(mm_stagger(x, (float)row, 12.0f));
        if (selected)
            mm_tab(x, 130.0f, dy, 820.0f, 88.0f, "", NULL, 1, appear, MM_ICON_NONE, 0u, 0);
        else
        {
            UiPaint bar = ui_paint_linear(0.0f, mm_y(x, dy), 0xb0ffffffu, 0.0f, mm_y(x, dy + 88.0f), 0x90d0e8f8u);
            ui_fill_chevron_bar(c, mm_x(x, 130.0f), mm_y(x, dy), mm_s(x, 820.0f), mm_s(x, 88.0f), mm_s(x, 20.0f), &bar, 1.2f, appear);
        }
        UiTextStyle label = ui_text_style(UI_FONT_BLACK, mm_s(x, 36.0f), selected ? 0xff3a2a00u : MM_INK);
        course_label(x, cup, track, &label, mm_x(x, 180.0f), mm_y(x, dy + 44.0f), UI_ALIGN_LEFT, mm_s(x, 720.0f), appear);
    }
    ui_canvas_clip(c, saved[0], saved[1], saved[2], saved[3]);
    /* Board on the right. */
    const float bx = 1040.0f, by = 200.0f, bw = 780.0f, bh = 740.0f;
    const float appear = mm_ease_out(mm_stagger(x, 4.0f, 14.0f));
    mm_panel(x, bx + (1.0f - appear) * 200.0f, by, bw, bh, 30.0f, appear);
    const PortableTimeTrialRecords *records = x->view->records;
    UiTextStyle head = ui_text_style(UI_FONT_ITALIC, mm_s(x, 44.0f), MM_WHITE);
    head.shadow_color = 0x70002a5au;
    head.shadow_y = mm_s(x, 3.0f);
    head.shadow_blur = mm_s(x, 5.0f);
    UiTextStyle row_style = ui_text_style(UI_FONT_BLACK, mm_s(x, 36.0f), 0xff102a44u);
    UiTextStyle time_style = ui_text_style(UI_FONT_BLACK, mm_s(x, 40.0f), 0xff102a44u);
    if (flow->course_phase == 4u || flow->course_phase == 5u || flow->course_phase == 6u)
    {
        ui_text_draw_centered(c, &head, mm_x(x, bx + bw * 0.5f), mm_y(x, by + 60.0f), UI_ALIGN_CENTER, "Ghost slots", appear);
        static const char *const slots[3] = {"Your ghost", "Received 1", "Received 2"};
        for (unsigned slot = 0u; slot < 3u; ++slot)
        {
            const float ry = by + 130.0f + (float)slot * 130.0f;
            const int selected = flow->record_ghost_slot == slot;
            mm_tab(x, bx + 40.0f, ry, bw - 80.0f, 104.0f, slots[slot], NULL, selected, appear,
                   slot == 0u ? MM_ICON_GHOST : MM_ICON_PEOPLE, 0u, 0);
            uint32_t ticks = 0u;
            if (records != NULL)
                ticks = slot == 0u ? (records->ghost_status == PORTABLE_TIME_TRIAL_GHOST_STORE_OK ? records->ghost.ticks : 0u)
                                   : (records->received[slot - 1u].status == PORTABLE_TIME_TRIAL_GHOST_STORE_OK
                                          ? records->received[slot - 1u].ticks : 0u);
            char time[16];
            if (ticks) mm_format_ticks(ticks, time);
            else snprintf(time, sizeof time, "Empty");
            UiTextStyle t = ui_text_style(UI_FONT_BLACK, mm_s(x, 34.0f), selected ? 0xff3a2a00u : MM_INK_SOFT);
            ui_text_draw_centered(c, &t, mm_x(x, bx + bw - 90.0f), mm_y(x, ry + 52.0f), UI_ALIGN_RIGHT, time, appear);
        }
        UiTextStyle help = ui_text_style(UI_FONT_BOLD, mm_s(x, 28.0f), 0xff17324du);
        ui_text_draw_wrapped(c, &help, mm_x(x, bx + bw * 0.5f), mm_y(x, by + 560.0f), mm_s(x, bw - 80.0f), mm_s(x, 38.0f), 3,
                             UI_ALIGN_CENTER,
                             flow->record_ghost_slot == 0u ? "Export a copy of your ghost to the save folder to share it."
                                                           : "Put a friend's ghost file in the save folder, then receive it here.",
                             appear);
    }
    else
    {
        ui_text_draw_centered(c, &head, mm_x(x, bx + bw * 0.5f), mm_y(x, by + 60.0f), UI_ALIGN_CENTER, "Best times", appear);
        if (records != NULL && records->displayed_records_ready)
            for (unsigned row = 0u; row < 6u; ++row)
            {
                const PortableGpResultsRecord *record = &records->displayed_records[row];
                const float ry = by + 140.0f + (float)row * 92.0f + (row == 5u ? 30.0f : 0.0f);
                char label[16], time[16];
                if (row < 5u) snprintf(label, sizeof label, "%u.", row + 1u);
                else snprintf(label, sizeof label, "Lap");
                const unsigned cs = record->time_centiseconds;
                snprintf(time, sizeof time, "%u'%02u\"%02u", cs / 6000u, cs / 100u % 60u, cs % 100u);
                UiPaint stripe = ui_paint_solid(row == 5u ? 0x50ffd21eu : (row & 1u) ? 0x30ffffffu : 0x18ffffffu);
                ui_fill_round_rect(c, mm_x(x, bx + 40.0f), mm_y(x, ry - 38.0f), mm_s(x, bw - 80.0f), mm_s(x, 76.0f),
                                   mm_s(x, 14.0f), 0.0f, &stripe, 1.2f, appear);
                ui_text_draw_centered(c, &row_style, mm_x(x, bx + 80.0f), mm_y(x, ry), UI_ALIGN_LEFT, label, appear);
                const uint32_t driver_id = record->roster_driver_id != 0u ? record->roster_driver_id
                                           : record->flags < 8u ? stock_driver_for_class(x, record->flags) : 0u;
                const char *name = driver_id != 0u ? portable_menu_driver_name(driver_id) : NULL;
                if (name != NULL)
                    ui_text_draw_centered(c, &row_style, mm_x(x, bx + 180.0f), mm_y(x, ry), UI_ALIGN_LEFT, name, appear);
                ui_text_draw_centered(c, &time_style, mm_x(x, bx + bw - 80.0f), mm_y(x, ry), UI_ALIGN_RIGHT, time, appear);
            }
        else
        {
            UiTextStyle none = ui_text_style(UI_FONT_BOLD, mm_s(x, 32.0f), 0xff17324du);
            ui_text_draw_centered(c, &none, mm_x(x, bx + bw * 0.5f), mm_y(x, by + 300.0f), UI_ALIGN_CENTER,
                                  count ? "Loading records..." : "No courses to show.", appear);
        }
    }
    if (records != NULL && records->notice != NULL)
    {
        UiTextStyle notice = ui_text_style(UI_FONT_BLACK, mm_s(x, 30.0f), MM_YELLOW);
        notice.shadow_color = 0x80001a40u;
        notice.shadow_y = mm_s(x, 3.0f);
        ui_text_draw_centered(c, &notice, mm_x(x, bx + bw * 0.5f), mm_y(x, by + bh + 26.0f), UI_ALIGN_CENTER,
                              portable_ui_text_resolve(NULL, records->notice), 1.0f);
    }
    if (flow->course_phase == 4u)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_SELECT, "Erase"},
                                {MM_BUTTON_A, flow->record_ghost_slot == 0u ? "Export" : "Receive"}};
        mm_footer(x, NULL, hints, 3u);
    }
    else if (flow->course_phase == 2u)
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Back"}, {MM_BUTTON_SELECT, "Erase"}, {MM_BUTTON_LR, "Course set"},
                                {MM_BUTTON_A, "Ghosts"}};
        mm_footer(x, NULL, hints, 4u);
    }
    else
    {
        const MmHint hints[] = {{MM_BUTTON_B, "Cancel"}, {MM_BUTTON_A, "Confirm"}};
        mm_footer(x, NULL, hints, 2u);
    }
    if (flow->course_phase == 3u) confirm_dialog(x, "Erase this course's records?", "Times and the ghost are removed.");
    if (flow->course_phase == 5u)
        confirm_dialog(x, flow->record_ghost_slot ? "Erase this received ghost?" : "Erase your ghost?", "Your times are kept.");
    if (flow->course_phase == 6u) confirm_dialog(x, "Receive a ghost file?", "Loads the ghost file from the save folder.");
    return 1;
}
