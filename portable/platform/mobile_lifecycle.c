#include "mobile_lifecycle.h"

#ifdef PORTABLE_ENABLE_SDL3
static SDL_AtomicInt pending;
static int watching;

/* SDL may call watches off the game thread. Never touch an owner here. */
static bool SDLCALL mobile_lifecycle_event(void *context, SDL_Event *event)
{
    (void)context;
    int bit = 0;
    switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND: bit = PORTABLE_MOBILE_BACKGROUND; break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND: bit = PORTABLE_MOBILE_FOREGROUND; break;
    case SDL_EVENT_TERMINATING: bit = PORTABLE_MOBILE_TERMINATING; break;
    default: return true;
    }
    int previous;
    do {
        previous = SDL_GetAtomicInt(&pending);
    } while (!SDL_CompareAndSwapAtomicInt(&pending, previous, previous | bit));
    return true;
}

void portable_mobile_lifecycle_hint(void)
{
    (void)SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
}

void portable_mobile_lifecycle_start(void)
{
    if (!watching) watching = SDL_AddEventWatch(mobile_lifecycle_event, NULL);
}

void portable_mobile_lifecycle_stop(void)
{
    if (watching) SDL_RemoveEventWatch(mobile_lifecycle_event, NULL);
    watching = 0;
    (void)SDL_SetAtomicInt(&pending, 0);
}

unsigned portable_mobile_lifecycle_take(void)
{
    return (unsigned)SDL_SetAtomicInt(&pending, 0);
}

int portable_mobile_back(const SDL_Event *event)
{
    return event != NULL && event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
           event->key.scancode == SDL_SCANCODE_AC_BACK;
}
#else
void portable_mobile_lifecycle_hint(void) {}
void portable_mobile_lifecycle_start(void) {}
void portable_mobile_lifecycle_stop(void) {}
unsigned portable_mobile_lifecycle_take(void) { return 0u; }
int portable_mobile_back(const SDL_Event *event) { (void)event; return 0; }
#endif
