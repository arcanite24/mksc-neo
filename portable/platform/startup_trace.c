#include "startup_trace.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#endif
static uint64_t launch;
uint64_t portable_startup_clock(void)
{
#ifdef PORTABLE_ENABLE_SDL3
    return SDL_GetTicksNS();
#else
    return 0u;
#endif
}
void portable_startup_begin(void) { launch = portable_startup_clock(); }
void portable_startup_phase(const char *phase, uint64_t since)
{
    if (getenv("MKSC_STARTUP_TIMING") != NULL)
        fprintf(stderr, "startup timing: %s %.3f ms\n", phase,
                (double)(portable_startup_clock() - since) / 1000000.0);
}
void portable_startup_mark(const char *phase) { portable_startup_phase(phase, launch); }
