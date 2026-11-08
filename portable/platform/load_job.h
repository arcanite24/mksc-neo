#ifndef PORTABLE_LOAD_JOB_H
#define PORTABLE_LOAD_JOB_H
#include <SDL3/SDL.h>

typedef enum {
    PORTABLE_LOAD_IDLE, PORTABLE_LOAD_RUNNING, PORTABLE_LOAD_CANCELLING,
    PORTABLE_LOAD_READY, PORTABLE_LOAD_FAILED, PORTABLE_LOAD_CANCELLED
} PortableLoadState;
typedef struct PortableLoadJob PortableLoadJob;
typedef int (*PortableLoadWork)(void *user, PortableLoadJob *job);
/* Zero-initialize. The worker exclusively owns user until finish joins it.
 * Main may only read state/phase and request cancellation in that interval. */
struct PortableLoadJob {
    SDL_Thread *thread;
    SDL_AtomicInt state, phase;
    PortableLoadWork work;
    void *user;
};
int portable_load_start(PortableLoadJob *job, PortableLoadWork work, void *user);
PortableLoadState portable_load_state(PortableLoadJob *job);
void portable_load_cancel(PortableLoadJob *job);
int portable_load_cancelled(PortableLoadJob *job);
void portable_load_phase(PortableLoadJob *job, int phase);
/* Returns 0 while running/idle; otherwise joins, resets, returns terminal state. */
PortableLoadState portable_load_finish(PortableLoadJob *job);
#endif
