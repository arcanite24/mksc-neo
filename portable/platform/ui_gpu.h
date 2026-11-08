#ifndef PORTABLE_UI_GPU_H
#define PORTABLE_UI_GPU_H

#include "ui_draw_list.h"
#include <SDL3/SDL.h>

typedef struct UiGpu UiGpu;

/* Borrows T2's renderer/device; creates no window or presentation owner. */
UiGpu *ui_gpu_create(SDL_Renderer *renderer, char *error, size_t capacity);
/* Preflight the entire frame before drawing: textures and immutable per-draw
 * uniform states stay alive through the compositor's next submission. */
int ui_gpu_prepare(UiGpu *gpu, const UiDrawList *list, int width, int height,
                   char *error, size_t capacity);
int ui_gpu_draw(UiGpu *gpu, const SDL_FRect *target, char *error, size_t capacity);
void ui_gpu_forget_images(UiGpu *gpu);
size_t ui_gpu_uploaded_bytes(const UiGpu *gpu);
void ui_gpu_close(UiGpu *gpu);

#endif
