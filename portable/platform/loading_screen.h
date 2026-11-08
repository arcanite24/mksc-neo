#ifndef PORTABLE_LOADING_SCREEN_H
#define PORTABLE_LOADING_SCREEN_H
#include "load_job.h"
/* Startup surface is transferred to the shell, never a second game window. */
int portable_loading_begin(int hidden);
void portable_loading_close(void);
int portable_loading_take(SDL_Window **window, SDL_Renderer **renderer);
/* A modal preparation interval: only this renderer and the job atomics are
 * accessed on main; the paused shell/candidate belong exclusively to work.
 * 1 success, 0 failure, -1 cancel, -2 close. Always joins before returning. */
int portable_loading_run(SDL_Window *window, SDL_Renderer *renderer,
    const char *title, const char *const *phases, unsigned phase_count,
    PortableLoadWork work, void *user);
/* The same with a line naming what is loading (for example the mods). */
int portable_loading_run_detail(SDL_Window *window, SDL_Renderer *renderer,
    const char *title, const char *detail, const char *const *phases, unsigned phase_count,
    PortableLoadWork work, void *user);
#endif
