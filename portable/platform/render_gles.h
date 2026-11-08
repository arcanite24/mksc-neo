#ifndef PORTABLE_RENDER_GLES_H
#define PORTABLE_RENDER_GLES_H

#include "render_scene.h"
#include <SDL3/SDL.h>

typedef struct PortableRenderGles PortableRenderGles;

/* Borrows an opengles2 SDL_Renderer with an ES3 context. No second context,
 * window or presentation owner is created. Close before destroying renderer. */
PortableRenderGles *portable_render_gles_create(SDL_Renderer *renderer, char *error,
                                                size_t capacity);
/* At the shared compositor boundary. The returned SDL texture is borrowed
 * until resize/reset/close. Immutable mesh/image identities stay borrowed until
 * forget_meshes, which must precede freeing or replacing their generation. */
SDL_Texture *portable_render_gles_draw(PortableRenderGles *gles,
                                      const PortableRenderFrame *frame, size_t width,
                                      size_t height, char *error, size_t capacity);
int portable_render_gles_read_pixels(PortableRenderGles *gles, uint32_t *pixels,
                                     size_t stride, char *error, size_t capacity);
/* Owned immutable buffer pairs, shared across material-only mesh variants. */
size_t portable_render_gles_geometry_count(const PortableRenderGles *gles);
void portable_render_gles_forget_meshes(PortableRenderGles *gles);
/* Call on SDL_EVENT_RENDER_DEVICE_RESET, including resets that reuse the same
 * context address. Old GL names are abandoned; the next draw rebuilds them. */
void portable_render_gles_invalidate(PortableRenderGles *gles);
/* Complete queued scene and compositor work without readback. */
int portable_render_gles_wait_idle(PortableRenderGles *gles, char *error, size_t capacity);
void portable_render_gles_close(PortableRenderGles *gles);

#endif
