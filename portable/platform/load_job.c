#include "load_job.h"

static int SDLCALL load_worker(void *data)
{
    PortableLoadJob *job = data;
    const int ok = job->work(job->user, job);
    /* Cancellation wins even if it races completion. Publish the result only
     * after all writes to the candidate, with SDL's atomic memory barriers. */
    if (!SDL_CompareAndSwapAtomicInt(&job->state, PORTABLE_LOAD_RUNNING,
                                    ok ? PORTABLE_LOAD_READY : PORTABLE_LOAD_FAILED))
        SDL_SetAtomicInt(&job->state, PORTABLE_LOAD_CANCELLED);
    return 0;
}
int portable_load_start(PortableLoadJob *job, PortableLoadWork work, void *user)
{
    if (!job || !work || portable_load_state(job) != PORTABLE_LOAD_IDLE || job->thread)
        return 0;
    job->work = work;
    job->user = user;
    SDL_SetAtomicInt(&job->phase, 0);
    SDL_SetAtomicInt(&job->state, PORTABLE_LOAD_RUNNING);
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    /* The existing single-threaded web distribution cannot create workers. */
    load_worker(job);
#else
    /* Content loading runs the same deep pack/course/actor decode chains as the
     * main thread (converted courses overflow macOS's 512 KiB default), so the
     * worker gets a main-thread-sized 8 MiB stack. */
    const SDL_PropertiesID props = SDL_CreateProperties();
    if (props) {
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, (void *)load_worker);
        SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "content-load");
        SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, job);
        SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 8 * 1024 * 1024);
        job->thread = SDL_CreateThreadWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!job->thread) {
        SDL_SetAtomicInt(&job->state, PORTABLE_LOAD_FAILED);
        return 0;
    }
#endif
    return 1;
}
PortableLoadState portable_load_state(PortableLoadJob *job)
{
    return (PortableLoadState)SDL_GetAtomicInt(&job->state);
}
void portable_load_cancel(PortableLoadJob *job)
{
    (void)SDL_CompareAndSwapAtomicInt(&job->state, PORTABLE_LOAD_RUNNING,
                                     PORTABLE_LOAD_CANCELLING);
}
int portable_load_cancelled(PortableLoadJob *job)
{
    return portable_load_state(job) == PORTABLE_LOAD_CANCELLING;
}
void portable_load_phase(PortableLoadJob *job, int phase)
{
    SDL_SetAtomicInt(&job->phase, phase);
}
PortableLoadState portable_load_finish(PortableLoadJob *job)
{
    const PortableLoadState state = portable_load_state(job);
    if (state < PORTABLE_LOAD_READY) return PORTABLE_LOAD_IDLE;
    if (job->thread) SDL_WaitThread(job->thread, NULL);
    job->thread = NULL;
    job->work = NULL;
    job->user = NULL;
    SDL_SetAtomicInt(&job->state, PORTABLE_LOAD_IDLE);
    return state;
}
