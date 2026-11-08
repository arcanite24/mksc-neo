#include "scene_3d_workers.h"

#if defined(PORTABLE_ENABLE_SDL3) && !defined(__EMSCRIPTEN__)
#include <SDL3/SDL.h>
#include <stdlib.h>

enum
{
    MAX_WORKERS = 7,
    MIN_PARALLEL_PIXELS = 262144
};

typedef struct
{
    PortableScene3D target;
    PortableScene3DProjection projection;
    PortableScene3DVertex vertices[4];
    PortableScene3DSampler sampler;
    const void *userdata;
} RowJob;

typedef struct
{
    SDL_Thread *thread;
    SDL_Semaphore *start;
    SDL_Semaphore *done; /* Shared completion semaphore, owned by the pool. */
    RowJob job;
    int stop;
    int result;
} RowWorker;

typedef struct
{
    SDL_Semaphore *done;
    unsigned count;
    RowWorker workers[MAX_WORKERS];
} RowPool;

static int draw_rows(RowJob *job)
{
    const PortableScene3DVertex first[3] = {job->vertices[0], job->vertices[1], job->vertices[2]};
    const PortableScene3DVertex second[3] = {job->vertices[0], job->vertices[2], job->vertices[3]};
    return portable_scene_3d_triangle(&job->target, &job->projection, first, job->sampler,
                                      job->userdata) &&
           portable_scene_3d_triangle(&job->target, &job->projection, second, job->sampler,
                                      job->userdata);
}

static int SDLCALL worker_main(void *userdata)
{
    RowWorker *worker = userdata;
    for (;;)
    {
        SDL_WaitSemaphore(worker->start);
        if (worker->stop)
            return 0;
        worker->result = draw_rows(&worker->job);
        SDL_SignalSemaphore(worker->done);
    }
}

static void pool_close(RowPool *pool)
{
    if (!pool)
        return;
    /* Every submission joins before returning, so workers are idle here.
     * Publish stop through each start semaphore before releasing any storage. */
    for (unsigned i = 0; i < pool->count; ++i)
    {
        RowWorker *worker = &pool->workers[i];
        if (worker->thread)
        {
            worker->stop = 1;
            SDL_SignalSemaphore(worker->start);
        }
    }
    for (unsigned i = 0; i < pool->count; ++i)
    {
        RowWorker *worker = &pool->workers[i];
        if (worker->thread)
            SDL_WaitThread(worker->thread, NULL);
        if (worker->start)
            SDL_DestroySemaphore(worker->start);
    }
    if (pool->done)
        SDL_DestroySemaphore(pool->done);
    free(pool);
}

static RowPool *pool_create(unsigned count)
{
    RowPool *pool = calloc(1, sizeof *pool);
    if (!pool)
        return NULL;
    pool->count = count;
    pool->done = SDL_CreateSemaphore(0);
    if (!pool->done)
    {
        pool_close(pool);
        return NULL;
    }
    for (unsigned i = 0; i < count; ++i)
    {
        RowWorker *worker = &pool->workers[i];
        worker->done = pool->done;
        worker->start = SDL_CreateSemaphore(0);
        if (worker->start)
            worker->thread = SDL_CreateThread(worker_main, "ground rows", worker);
        if (!worker->thread)
        {
            pool_close(pool);
            return NULL;
        }
    }
    return pool;
}

static RowJob row_job(const PortableScene3D *scene, const PortableScene3DProjection *projection,
                      const PortableScene3DVertex vertices[4], PortableScene3DSampler sampler,
                      const void *userdata, size_t top, size_t bottom)
{
    RowJob job;
    job.target = *scene;
    /* A borrowed band owns neither depth nor workers and is never begun/closed. */
    job.target.worker_pool = NULL;
    job.target.worker_pool_attempted = 1;
    job.target.clip_top = top;
    job.target.clip_bottom = bottom;
    job.projection = *projection;
    for (unsigned i = 0; i < 4; ++i)
        job.vertices[i] = vertices[i];
    job.sampler = sampler;
    job.userdata = userdata;
    return job;
}

int portable_scene_3d_workers_quad(PortableScene3D *scene,
                                   const PortableScene3DProjection *projection,
                                   const PortableScene3DVertex vertices[4],
                                   PortableScene3DSampler sampler, const void *userdata)
{
    const size_t rows = scene->clip_bottom - scene->clip_top;
    if (rows < MAX_WORKERS + 1u || scene->width < (MIN_PARALLEL_PIXELS + rows - 1u) / rows)
        return -1;
    if (!scene->worker_pool && !scene->worker_pool_attempted)
    {
        scene->worker_pool_attempted = 1;
        const int cores = SDL_GetNumLogicalCPUCores();
        if (cores > 1)
        {
            const unsigned count = cores > MAX_WORKERS ? MAX_WORKERS : (unsigned)(cores - 1);
            scene->worker_pool = pool_create(count);
        }
    }
    RowPool *pool = scene->worker_pool;
    if (!pool)
        return -1;
    const size_t bands = pool->count + 1u;
    const size_t band_rows = rows / bands, extra = rows % bands;
    size_t top = scene->clip_top;
    for (unsigned i = 0; i < pool->count; ++i)
    {
        const size_t bottom = top + band_rows + (i < extra ? 1u : 0u);
        pool->workers[i].job = row_job(scene, projection, vertices, sampler, userdata, top, bottom);
        top = bottom;
    }
    RowJob main_job =
        row_job(scene, projection, vertices, sampler, userdata, top, scene->clip_bottom);
    /* Start signals publish complete jobs; done waits publish each result and
     * all pixel/depth writes before callers reuse content or resize the owner. */
    for (unsigned i = 0; i < pool->count; ++i)
        SDL_SignalSemaphore(pool->workers[i].start);
    int result = draw_rows(&main_job);
    for (unsigned i = 0; i < pool->count; ++i)
        SDL_WaitSemaphore(pool->done);
    for (unsigned i = 0; i < pool->count; ++i)
        result = pool->workers[i].result && result;
    return result;
}

void portable_scene_3d_workers_close(PortableScene3D *scene)
{
    pool_close(scene->worker_pool);
    scene->worker_pool = NULL;
}

#else

int portable_scene_3d_workers_quad(PortableScene3D *scene,
                                   const PortableScene3DProjection *projection,
                                   const PortableScene3DVertex vertices[4],
                                   PortableScene3DSampler sampler, const void *userdata)
{
    (void)scene;
    (void)projection;
    (void)vertices;
    (void)sampler;
    (void)userdata;
    return -1;
}

void portable_scene_3d_workers_close(PortableScene3D *scene)
{
    (void)scene;
}

#endif
