#include "mobile_touch.h"

#include "driver_input.h"
#include "frontend_flow.h"
#include "ui_canvas.h"
#include "ui_font.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    SDL_TouchID device;
    SDL_FingerID id;
    int button;
    uint8_t active;
} TouchFinger;

static struct
{
    int enabled;
    TouchFinger fingers[PORTABLE_MOBILE_TOUCH_FINGERS];
    uint16_t frontend, driving, pressed, driving_pressed;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    int width, height;
    uint16_t drawn_mask;
    uint8_t dirty;
} touch;

size_t portable_mobile_touch_layout(int width, int height, PortableMobileTouchButton *out, size_t capacity)
{
    if (width <= 0 || height <= 0) return 0u;
    const float short_side = (float)(width < height ? width : height);
    float radius = (float)width / 15.0f;
    if (radius > (float)height / 13.0f) radius = (float)height / 13.0f;
    const float margin = short_side * 0.035f;
    const float left = margin + 3.0f * radius;
    const float right = (float)width - left;
    const float y = (float)height - margin - 3.0f * radius;
    const float step = 1.85f * radius;
    const PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS] = {
        {left - step, y, radius, PORTABLE_FRONTEND_BUTTON_LEFT, PORTABLE_INPUT_LEFT, "<"},
        {left + step, y, radius, PORTABLE_FRONTEND_BUTTON_RIGHT, PORTABLE_INPUT_RIGHT, ">"},
        {left, y - step, radius, PORTABLE_FRONTEND_BUTTON_UP, PORTABLE_INPUT_UP, "UP"},
        {left, y + step, radius, PORTABLE_FRONTEND_BUTTON_DOWN, PORTABLE_INPUT_DOWN, "DN"},
        {right + step, y, radius, PORTABLE_FRONTEND_BUTTON_A, PORTABLE_INPUT_ACCELERATE, "A"},
        {right, y + step, radius, PORTABLE_FRONTEND_BUTTON_B, PORTABLE_INPUT_BRAKE, "B"},
        {right - step, y, radius, PORTABLE_FRONTEND_BUTTON_L, PORTABLE_INPUT_ITEM, "ITEM"},
        {right, y - step, radius, PORTABLE_FRONTEND_BUTTON_R, PORTABLE_INPUT_HOP, "HOP"},
        {(float)width * 0.58f, (float)height - margin - radius, radius,
         PORTABLE_FRONTEND_BUTTON_START, 0u, "START"},
        {(float)width * 0.42f, (float)height - margin - radius, radius,
         PORTABLE_FRONTEND_BUTTON_SELECT, 0u, "SEL"}
    };
    if (out != NULL)
    {
        const size_t count = capacity < PORTABLE_MOBILE_TOUCH_BUTTONS ? capacity : PORTABLE_MOBILE_TOUCH_BUTTONS;
        memcpy(out, buttons, count * sizeof *out);
    }
    return PORTABLE_MOBILE_TOUCH_BUTTONS;
}

void portable_mobile_touch_clear(void)
{
    memset(touch.fingers, 0, sizeof touch.fingers);
    touch.frontend = touch.driving = touch.pressed = touch.driving_pressed = 0u;
    touch.dirty = 1u;
}

void portable_mobile_touch_configure(int enabled)
{
    enabled = enabled != 0;
    if (touch.enabled != enabled || !enabled) portable_mobile_touch_clear();
    touch.enabled = enabled;
}

static uint16_t recompute(const PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS])
{
    const uint16_t previous = touch.frontend;
    const uint16_t previous_driving = touch.driving;
    touch.frontend = touch.driving = 0u;
    for (unsigned i = 0u; i < PORTABLE_MOBILE_TOUCH_FINGERS; ++i)
        if (touch.fingers[i].active && touch.fingers[i].button >= 0)
        {
            const PortableMobileTouchButton *button = &buttons[touch.fingers[i].button];
            touch.frontend |= button->frontend_mask;
            touch.driving |= button->driving_mask;
        }
    const uint16_t pressed = (uint16_t)(touch.frontend & (uint16_t)~previous);
    touch.pressed |= pressed;
    touch.driving_pressed |= (uint16_t)(touch.driving & (uint16_t)~previous_driving &
                                        (PORTABLE_INPUT_HOP | PORTABLE_INPUT_ITEM));
    return pressed;
}

uint16_t portable_mobile_touch_event(const SDL_Event *event, void *renderer)
{
    if (!touch.enabled || event == NULL) return 0u;
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_DID_ENTER_BACKGROUND)
    {
        portable_mobile_touch_clear();
        return 0u;
    }
    if (event->type != SDL_EVENT_FINGER_DOWN && event->type != SDL_EVENT_FINGER_MOTION &&
        event->type != SDL_EVENT_FINGER_UP && event->type != SDL_EVENT_FINGER_CANCELED) return 0u;
    TouchFinger *finger = NULL, *free_slot = NULL;
    for (unsigned i = 0u; i < PORTABLE_MOBILE_TOUCH_FINGERS; ++i)
    {
        TouchFinger *slot = &touch.fingers[i];
        if (slot->active && slot->device == event->tfinger.touchID && slot->id == event->tfinger.fingerID)
            finger = slot;
        else if (!slot->active && free_slot == NULL) free_slot = slot;
    }
    const int release = event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED;
    const int valid = isfinite(event->tfinger.x) && isfinite(event->tfinger.y) &&
                      event->tfinger.x >= 0.0f && event->tfinger.x <= 1.0f &&
                      event->tfinger.y >= 0.0f && event->tfinger.y <= 1.0f;
    if (finger == NULL)
    {
        if (event->type != SDL_EVENT_FINGER_DOWN || !valid || free_slot == NULL) return 0u;
        finger = free_slot;
        *finger = (TouchFinger){event->tfinger.touchID, event->tfinger.fingerID, -1, 1u};
    }
    const int canceled_button = event->type == SDL_EVENT_FINGER_CANCELED ? finger->button : -1;
    finger->button = -1;
    if (release) finger->active = 0u;
    int width = 0, height = 0;
    if (renderer != NULL) (void)SDL_GetCurrentRenderOutputSize(renderer, &width, &height);
    PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS];
    /* Geometry is irrelevant for release; a harmless valid layout still gives
     * the other fingers their semantic masks when output sizing is unavailable. */
    portable_mobile_touch_layout(width > 0 ? width : 240, height > 0 ? height : 160,
                                 buttons, PORTABLE_MOBILE_TOUCH_BUTTONS);
    if (!release && valid && width > 0 && height > 0)
    {
        const float x = event->tfinger.x * (float)width, y = event->tfinger.y * (float)height;
        for (unsigned i = 0u; i < PORTABLE_MOBILE_TOUCH_BUTTONS; ++i)
        {
            const float dx = x - buttons[i].x, dy = y - buttons[i].y;
            if (dx * dx + dy * dy <= buttons[i].radius * buttons[i].radius)
            {
                finger->button = (int)i;
                break;
            }
        }
    }
    const uint16_t pressed = recompute(buttons);
    if (canceled_button >= 0)
        touch.driving_pressed &= (uint16_t)~buttons[canceled_button].driving_mask;
    return pressed;
}

uint16_t portable_mobile_touch_driving(void) { return touch.enabled ? touch.driving : 0u; }
uint16_t portable_mobile_touch_take_driving_pressed(void)
{
    const uint16_t pressed = touch.enabled ? touch.driving_pressed : 0u;
    touch.driving_pressed = 0u;
    return pressed;
}
uint16_t portable_mobile_touch_frontend(void) { return touch.enabled ? touch.frontend : 0u; }
uint16_t portable_mobile_touch_take_pressed(void)
{
    const uint16_t pressed = touch.enabled ? touch.pressed : 0u;
    touch.pressed = 0u;
    return pressed;
}

void portable_mobile_touch_close(void)
{
    if (touch.texture != NULL) SDL_DestroyTexture(touch.texture);
    free(touch.pixels);
    touch.texture = NULL;
    touch.pixels = NULL;
    touch.renderer = NULL;
    touch.width = touch.height = 0;
    portable_mobile_touch_clear();
}

static int rebuild_overlay(SDL_Renderer *renderer, int width, int height)
{
    if (touch.renderer != renderer || touch.width != width || touch.height != height)
    {
        if (touch.texture != NULL) SDL_DestroyTexture(touch.texture);
        touch.texture = NULL;
        touch.renderer = renderer;
        uint32_t *pixels = realloc(touch.pixels, (size_t)width * (size_t)height * sizeof *pixels);
        if (pixels == NULL) return SDL_SetError("Cannot allocate touch overlay");
        touch.pixels = pixels;
        touch.width = width;
        touch.height = height;
        touch.dirty = 1u;
    }
    if (touch.texture == NULL)
    {
        touch.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                          width, height);
        if (touch.texture == NULL || !SDL_SetTextureBlendMode(touch.texture, SDL_BLENDMODE_BLEND)) return 0;
        touch.dirty = 1u;
    }
    if (!touch.dirty && touch.drawn_mask == touch.frontend) return 1;
    memset(touch.pixels, 0, (size_t)width * (size_t)height * sizeof *touch.pixels);
    UiCanvas canvas;
    ui_canvas_init(&canvas, touch.pixels, width, height, width);
    PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS];
    portable_mobile_touch_layout(width, height, buttons, PORTABLE_MOBILE_TOUCH_BUTTONS);
    for (unsigned i = 0u; i < PORTABLE_MOBILE_TOUCH_BUTTONS; ++i)
    {
        const PortableMobileTouchButton *button = &buttons[i];
        const int held = (touch.frontend & button->frontend_mask) != 0u;
        const UiPaint body = ui_paint_solid(held ? 0xffffd21eu : 0xff102b48u);
        ui_fill_circle(&canvas, button->x, button->y, button->radius, &body, 1.0f, 1.0f);
        ui_stroke_circle(&canvas, button->x, button->y, button->radius - 1.0f, 2.0f,
                          held ? 0xffffffffu : 0xffb8d8f3u, 1.0f);
        UiTextStyle label = ui_text_style(UI_FONT_BLACK, button->radius * 0.73f,
                                          held ? 0xff142b46u : 0xffffffffu);
        label = ui_text_fit(&label, button->label, button->radius * 1.7f);
        ui_text_draw_centered(&canvas, &label, button->x, button->y, UI_ALIGN_CENTER, button->label, 1.0f);
    }
    /* UiCanvas blends into opaque targets. Export its button art with an
     * explicit straight-alpha circle mask; untouched output stays transparent. */
    for (size_t i = 0u; i < (size_t)width * (size_t)height; ++i)
        touch.pixels[i] &= UINT32_C(0x00ffffff);
    for (unsigned i = 0u; i < PORTABLE_MOBILE_TOUCH_BUTTONS; ++i)
    {
        const PortableMobileTouchButton *button = &buttons[i];
        const int opacity = (touch.frontend & button->frontend_mask) != 0u ? 225 : 155;
        int x0 = (int)floorf(button->x - button->radius - 1.0f), y0 = (int)floorf(button->y - button->radius - 1.0f);
        int x1 = (int)ceilf(button->x + button->radius + 1.0f), y1 = (int)ceilf(button->y + button->radius + 1.0f);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > width) x1 = width;
        if (y1 > height) y1 = height;
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x)
        {
            const float dx = (float)x + 0.5f - button->x, dy = (float)y + 0.5f - button->y;
            float coverage = button->radius + 0.5f - sqrtf(dx * dx + dy * dy);
            if (coverage < 0.0f) coverage = 0.0f;
            if (coverage > 1.0f) coverage = 1.0f;
            uint32_t *pixel = &touch.pixels[(size_t)y * (size_t)width + (size_t)x];
            const uint32_t alpha = (uint32_t)((float)opacity * coverage);
            if (alpha > (*pixel >> 24u))
                *pixel = (*pixel & UINT32_C(0x00ffffff)) | alpha << 24u;
        }
    }
    if (!SDL_UpdateTexture(touch.texture, NULL, touch.pixels, width * 4)) return 0;
    touch.drawn_mask = touch.frontend;
    touch.dirty = 0u;
    return 1;
}

int portable_mobile_touch_draw(void *renderer)
{
    if (!touch.enabled) return 1;
    int width = 0, height = 0;
    if (renderer == NULL || !SDL_GetCurrentRenderOutputSize(renderer, &width, &height) || width <= 0 || height <= 0)
        return 0;
    /* Touch targets follow physical output geometry. Cap only the cached art
     * canvas, as the modern menu does, so large/high-DPI windows stay usable. */
    while ((uint64_t)(unsigned)width * (unsigned)height > UINT64_C(1440000) || width > 4096 || height > 4096)
    {
        width = (int)((int64_t)width * 3 / 4);
        height = (int)((int64_t)height * 3 / 4);
        if (width < 1) width = 1;
        if (height < 1) height = 1;
    }
    if (!rebuild_overlay(renderer, width, height)) return 0;
    if (!SDL_SetTextureScaleMode(touch.texture, SDL_SCALEMODE_LINEAR)) return 0;
    return SDL_RenderTexture(renderer, touch.texture, NULL, NULL);
}
