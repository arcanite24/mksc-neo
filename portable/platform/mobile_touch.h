#ifndef PORTABLE_MOBILE_TOUCH_H
#define PORTABLE_MOBILE_TOUCH_H

#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#else
typedef union SDL_Event SDL_Event;
#endif
#include <stddef.h>
#include <stdint.h>

enum { PORTABLE_MOBILE_TOUCH_BUTTONS = 10, PORTABLE_MOBILE_TOUCH_FINGERS = 16 };

typedef struct
{
    float x, y, radius; /* Physical output pixels, including safe edge margins. */
    uint16_t frontend_mask, driving_mask;
    const char *label;
} PortableMobileTouchButton;

/* Returns the full count, copying at most capacity rows. Invalid sizes return
 * zero. NULL out permits querying the count without writing any rows. */
size_t portable_mobile_touch_layout(int width, int height, PortableMobileTouchButton *out, size_t capacity);
void portable_mobile_touch_configure(int enabled);
void portable_mobile_touch_clear(void);
uint16_t portable_mobile_touch_event(const SDL_Event *event, void *renderer);
uint16_t portable_mobile_touch_driving(void);
/* Hop/item taps survive release until the host admits a race tick. Held
 * driving() and drawing never consume these one-tick action edges. */
uint16_t portable_mobile_touch_take_driving_pressed(void);
uint16_t portable_mobile_touch_frontend(void);
uint16_t portable_mobile_touch_take_pressed(void);
/* Draw once after the world/menu texture and before SDL_RenderPresent. */
int portable_mobile_touch_draw(void *renderer);
/* Call before destroying the renderer that owns the overlay texture. */
void portable_mobile_touch_close(void);

#endif
