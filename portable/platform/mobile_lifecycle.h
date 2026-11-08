#ifndef PORTABLE_MOBILE_LIFECYCLE_H
#define PORTABLE_MOBILE_LIFECYCLE_H

#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#else
typedef union SDL_Event SDL_Event;
#endif

enum {
    PORTABLE_MOBILE_BACKGROUND = 1u,
    PORTABLE_MOBILE_FOREGROUND = 2u,
    PORTABLE_MOBILE_TERMINATING = 4u
};

void portable_mobile_lifecycle_hint(void);
void portable_mobile_lifecycle_start(void);
void portable_mobile_lifecycle_stop(void);
/* The active leaf host consumes flags before advancing its schedulers. */
unsigned portable_mobile_lifecycle_take(void);
int portable_mobile_back(const SDL_Event *event);

#endif
