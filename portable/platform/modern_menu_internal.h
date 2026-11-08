#ifndef PORTABLE_MODERN_MENU_INTERNAL_H
#define PORTABLE_MODERN_MENU_INTERNAL_H

#include "modern_menu.h"
#include "ui_art.h"
#include "ui_font.h"
#include "ui_draw.h"

/* Shared drawing context for one modern-menu frame. Layout is authored on a
 * 1920x1080 design stage scaled uniformly (u pixels per design unit) and
 * centered; backgrounds, header and footer bars span the whole window. */
typedef struct
{
    UiCanvas *c;
    const ModernMenuView *view;
    const PortableFrontendFlow *flow; /* The page being shown (see visible_page). */
    ModernMenu *menu;
    float u, ox, oy;   /* Design-unit scale and stage origin in pixels. */
    float left, right; /* Window edges in design units (may exceed 0..1920). */
    float top, bottom;
    float t;           /* Seconds of menu time (flow ticks). */
    uint32_t tick;
    float enter;       /* 0..1 entrance progress of the current screen. */
    uint32_t screen_ticks;
    uint32_t elapsed;  /* Flow ticks since the previous rendered frame (0..8). */
} MmCtx;

enum { MM_SMOOTH_SLOTS = 10 };

struct ModernMenu
{
    UiArtCache *art;
    PortableMenuDriverModel *driver_model; /* Lazy: 3D-only drivers' select preview. */
    uint32_t *field; /* Half-resolution background scratch. */
    size_t field_capacity;
    uint32_t screen_key, screen_tick, last_tick;
    uint8_t screen_valid;
    /* Exponentially smoothed cursor positions (design units), one per slot. */
    float smooth_value[MM_SMOOTH_SLOTS];
    uint32_t smooth_key[MM_SMOOTH_SLOTS];
};

/* Palette. */
#define MM_INK 0xff2e3440u
#define MM_INK_SOFT 0xff5b6573u
#define MM_WHITE 0xffffffffu
#define MM_YELLOW 0xffffd21eu
#define MM_YELLOW_DEEP 0xffe7a300u
#define MM_RED 0xffe5262cu
#define MM_BLUE_DEEP 0xff0b5fb8u
#define MM_SHADOW 0x66000000u

typedef enum
{
    MM_ICON_NONE,
    MM_ICON_TROPHY,
    MM_ICON_CLOCK,
    MM_ICON_FLAG,
    MM_ICON_BALLOON,
    MM_ICON_GEAR,
    MM_ICON_GHOST,
    MM_ICON_PEOPLE,
    MM_ICON_PERSON,
    MM_ICON_PLAY,
    MM_ICON_LIST
} MmIcon;

/* Semantic buttons for prompts; drawn as pad glyphs or keyboard keys. */
typedef enum
{
    MM_BUTTON_A,
    MM_BUTTON_B,
    MM_BUTTON_START,
    MM_BUTTON_SELECT,
    MM_BUTTON_L,
    MM_BUTTON_R,
    MM_BUTTON_LR,
    MM_BUTTON_DPAD
} MmButton;

typedef struct
{
    MmButton button;
    const char *label;
} MmHint;

static inline float mm_x(const MmCtx *x, float design)
{
    return x->ox + design * x->u;
}
static inline float mm_y(const MmCtx *x, float design)
{
    return x->oy + design * x->u;
}
static inline float mm_s(const MmCtx *x, float design)
{
    return design * x->u;
}
/* Sets up the design transform and per-screen timing (key names the screen). */
void mm_context_init(MmCtx *x, ModernMenu *menu, const ModernMenuView *view, const PortableFrontendFlow *flow,
                     UiCanvas *canvas, uint32_t tick, uint32_t key);
float mm_clamp01(float value);
float mm_ease_out(float value);      /* Cubic. */
float mm_ease_back(float value);     /* Overshooting settle. */
/* Entrance progress for an element appearing delay_ticks into the screen. */
float mm_stagger(const MmCtx *x, float delay_ticks, float duration_ticks);
/* Smoothly follows target in slot (keyed so a new list snaps). */
float mm_follow(MmCtx *x, unsigned slot, uint32_t key, float target, float rate);

void mm_background(MmCtx *x, int swoosh);
void mm_header(MmCtx *x, const char *title, const char *subtitle);
void mm_footer(MmCtx *x, const char *center, const MmHint *hints, unsigned hint_count);
/* Draws a prompt glyph centered vertically on y; returns its width (pixels). */
float mm_button_glyph(MmCtx *x, float px, float py, float height, MmButton button, float alpha);
float mm_button_glyph_width(MmCtx *x, float height, MmButton button);
/* Frosted panel (design units). */
void mm_panel(MmCtx *x, float dx, float dy, float dw, float dh, float radius, float alpha);
/* Big menu tab (design units). badge is optional text at the right end. */
void mm_tab(MmCtx *x, float dx, float dy, float dw, float dh, const char *label, const char *badge,
            int selected, float alpha, MmIcon icon, uint32_t portrait_id, int disabled);
void mm_icon(MmCtx *x, MmIcon icon, float cx, float cy, float radius, uint32_t color, float alpha);
/* Animated selection brackets around a rectangle (pixels). */
void mm_corner_brackets(MmCtx *x, float px, float py, float pw, float ph, uint32_t color,
                        float alpha);
/* Pack image helpers; return 0 when the image is missing. */
int mm_image(MmCtx *x, uint32_t id, float px, float py, float pw, float ph, float alpha,
             unsigned flags, float radius);
int mm_image_fit(MmCtx *x, uint32_t id, float cx, float cy, float max_w, float max_h, float alpha,
                 unsigned flags);
const UiImage *mm_art(MmCtx *x, uint32_t id);
/* A 3D-domain driver's portrait: the texture of its kind-110 row's material. */
int mm_portrait_3d(MmCtx *x, uint32_t driver_id, UiImage *out);
/* Displays a driver/course name: text when known, else the pack's name art. */
void mm_title_text(MmCtx *x, const UiTextStyle *style, float px, float py, UiAlign align,
                   const char *text, float alpha);
/* Formats race ticks as M'SS"cc. */
void mm_format_ticks(uint32_t ticks, char out[16]);

/* Kart turntable on a pedestal; preview_id is a frontend preview animation. */
void mm_kart_showcase(MmCtx *x, uint32_t preview_id, float cx, float cy, float size, float alpha,
                      uint32_t tick, float hop);

void mm_title_logo(MmCtx *x, float cx, float cy, float scale, float alpha);

/* Pages. */
int mm_page_title(MmCtx *x, int intro);
int mm_page_mode(MmCtx *x);
int mm_page_time_trial(MmCtx *x);
int mm_page_character(MmCtx *x);
int mm_page_course(MmCtx *x);
int mm_page_records(MmCtx *x);
void mm_quick_options(MmCtx *x);
void mm_options_overlay(MmCtx *x, const ModernMenuOptions *options);
void mm_games_overlay(MmCtx *x, const ModernMenuGames *games);
void mm_mods_overlay(MmCtx *x, const ModernMenuMods *mods);
void mm_mode_names(const PortableFrontendFlow *flow, const char **title, const char **subtitle);

#endif
