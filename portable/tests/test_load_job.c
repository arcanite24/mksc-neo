#include "load_job.h"
#include "loading_screen.h"
#include <stdio.h>
#include <stdlib.h>

/* Side effects stay in checks, so never compile them out with NDEBUG. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "load job line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

typedef struct { SDL_AtomicInt release, entered; int value, fail; } Fixture;
static int work(void *user, PortableLoadJob *job)
{
    Fixture *f = user;
    portable_load_phase(job, 2);
    SDL_SetAtomicInt(&f->entered, 1);
    while (!SDL_GetAtomicInt(&f->release) && !portable_load_cancelled(job)) SDL_Delay(1);
    f->value = 42;
    return !f->fail;
}
static void wait_entered(Fixture *f)
{
    const Uint64 start = SDL_GetTicks();
    while (!SDL_GetAtomicInt(&f->entered)) { CHECK(SDL_GetTicks() - start < 5000); SDL_Delay(1); }
}
static PortableLoadState finish(PortableLoadJob *job)
{
    const Uint64 start = SDL_GetTicks();
    while (portable_load_state(job) < PORTABLE_LOAD_READY) { CHECK(SDL_GetTicks() - start < 5000); SDL_Delay(1); }
    return portable_load_finish(job);
}
static int event_work(void *user, PortableLoadJob *job)
{
    const int close = *(const int *)user;
    SDL_Delay(30);
    SDL_Event event = {0};
    event.type = close == 1 ? SDL_EVENT_QUIT : close == 2 ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_KEY_DOWN;
    if (close == 2) event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
    else if (!close) event.key.key = SDLK_ESCAPE;
    CHECK(SDL_PushEvent(&event));
    const Uint64 start = SDL_GetTicks();
    while (!portable_load_cancelled(job)) { CHECK(SDL_GetTicks() - start < 5000); SDL_Delay(1); }
    return 1;
}
int main(void)
{
    PortableLoadJob job = {0};
    Fixture f = {0};
    CHECK(portable_load_finish(&job) == PORTABLE_LOAD_IDLE);
    CHECK(portable_load_start(&job, work, &f));
    wait_entered(&f);
    CHECK(portable_load_state(&job) == PORTABLE_LOAD_RUNNING);
    CHECK(SDL_GetAtomicInt(&job.phase) == 2);
    CHECK(!portable_load_start(&job, work, &f));
    CHECK(portable_load_finish(&job) == PORTABLE_LOAD_IDLE);
    SDL_SetAtomicInt(&f.release, 1);
    CHECK(finish(&job) == PORTABLE_LOAD_READY && f.value == 42);
    CHECK(portable_load_state(&job) == PORTABLE_LOAD_IDLE && !job.thread);
    f = (Fixture){.fail = 1};
    CHECK(portable_load_start(&job, work, &f));
    wait_entered(&f);
    SDL_SetAtomicInt(&f.release, 1);
    CHECK(finish(&job) == PORTABLE_LOAD_FAILED);
    f = (Fixture){0};
    CHECK(portable_load_start(&job, work, &f));
    wait_entered(&f);
    portable_load_cancel(&job);
    portable_load_cancel(&job);
    CHECK(finish(&job) == PORTABLE_LOAD_CANCELLED && f.value == 42);
    /* Reuse after cancellation and a late cancellation after publication. */
    f = (Fixture){0};
    SDL_SetAtomicInt(&f.release, 1);
    CHECK(portable_load_start(&job, work, &f));
    const Uint64 start = SDL_GetTicks();
    while (portable_load_state(&job) == PORTABLE_LOAD_RUNNING) { CHECK(SDL_GetTicks() - start < 5000); SDL_Delay(1); }
    portable_load_cancel(&job);
    CHECK(finish(&job) == PORTABLE_LOAD_READY);
    /* The real modal presenter must pump input while work is still running,
     * distinguish closing from cancelling, and join before releasing data. */
    CHECK(portable_loading_begin(1));
    const char *const phases[] = {"Testing loading"};
    int close = 0;
    CHECK(portable_loading_run(NULL, NULL, "Loading", phases, 1, event_work, &close) == -1);
    close = 1;
    CHECK(portable_loading_run(NULL, NULL, "Loading", phases, 1, event_work, &close) == -2);
    close = 2;
    CHECK(portable_loading_run(NULL, NULL, "Loading", phases, 1, event_work, &close) == -1);
    portable_loading_close();
    SDL_Quit();
    puts("load job: PASS (handoff, failure, cancellation, reuse, responsive presenter)");
    return 0;
}
