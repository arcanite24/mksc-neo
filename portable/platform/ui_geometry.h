#ifndef PORTABLE_UI_GEOMETRY_H
#define PORTABLE_UI_GEOMETRY_H

#include "ui_draw_list.h"
#include <SDL3/SDL.h>

typedef struct UiGeometry UiGeometry;

/* Borrows the existing accelerated SDL renderer. Preparation owns all geometry
 * and pins its cached textures until draw; neither operation presents a frame. */
UiGeometry *ui_geometry_create(SDL_Renderer *renderer, char *error, size_t capacity);
int ui_geometry_prepare(UiGeometry *geometry, const UiDrawList *list, int width, int height,
                        char *error, size_t capacity);
int ui_geometry_draw(UiGeometry *geometry, const SDL_FRect *target, char *error, size_t capacity);
void ui_geometry_forget_images(UiGeometry *geometry);
size_t ui_geometry_uploaded_bytes(const UiGeometry *geometry);
void ui_geometry_close(UiGeometry *geometry);

#endif
