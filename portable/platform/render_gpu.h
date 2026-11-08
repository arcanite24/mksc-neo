#ifndef PORTABLE_RENDER_GPU_H
#define PORTABLE_RENDER_GPU_H

#include "render_scene.h"
#include "settings.h"
#include <SDL3/SDL.h>

typedef struct PortableRenderGpu PortableRenderGpu;
/* Renderer/device are borrowed. Close before destroying the renderer. A null
 * result names an unavailable capability; the caller may select CPU/GLES. */
PortableRenderGpu *portable_render_gpu_create(SDL_Renderer *renderer, char *error, size_t capacity);
/* Submit at the compositor's frame boundary, before its SDL_Renderer draws.
 * The returned wrapper is borrowed until resize/close. No content pointer is
 * retained by command recording; cached mesh identities remain borrowed until
 * forget_meshes, which must precede replacing/freeing those resources. */
SDL_Texture *portable_render_gpu_draw(PortableRenderGpu *gpu, const PortableRenderFrame *frame,
                                      size_t width, size_t height, char *error, size_t capacity);
/* Opt-in presentation only. Null restores the unmodified low-cost path. */
void portable_render_gpu_set_hd2d(PortableRenderGpu *gpu, const PortableSettings *settings);
/* Number of owned immutable vertex/index buffer pairs; material variants share
 * a pair only when both array identities and both counts are identical. */
size_t portable_render_gpu_geometry_count(const PortableRenderGpu *gpu);
void portable_render_gpu_forget_meshes(PortableRenderGpu *gpu);
/* Blocking capture for explicit developer captures,
 * never the normal live presentation path. Destination contains straight ARGB. */
int portable_render_gpu_read_pixels(PortableRenderGpu *gpu, uint32_t *pixels, size_t stride,
                                    char *error, size_t capacity);
/* Complete submitted work without readback. For compositor timing, the caller
 * must SDL_RenderPresent first: SDL_FlushRenderer alone does not submit SDL_GPU
 * compositor commands. Normal frames stay queued. */
int portable_render_gpu_wait_idle(PortableRenderGpu *gpu, char *error, size_t capacity);
void portable_render_gpu_close(PortableRenderGpu *gpu);

#endif
