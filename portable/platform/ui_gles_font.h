#ifndef PORTABLE_UI_GLES_FONT_H
#define PORTABLE_UI_GLES_FONT_H

#include <SDL3/SDL.h>

typedef struct UiGlesFont UiGlesFont;

/* Private glyph pass on SDL's existing GLES2 context. Preparation uploads only
 * vertices; SDF textures remain owned and pinned by UiGeometry's shared cache. */
UiGlesFont *ui_gles_font_create(SDL_Renderer *renderer);
int ui_gles_font_prepare(UiGlesFont *font, const SDL_Vertex *vertices, size_t count, char *error,
                         size_t capacity);
int ui_gles_font_begin(UiGlesFont *font, const SDL_FRect *target, int width, int height,
                       char *error, size_t capacity);
int ui_gles_font_draw(UiGlesFont *font, SDL_Texture *texture, size_t first, size_t count,
                      const SDL_Rect *clip, int width, int height, float scale, float outline,
                      float softness, uint32_t color, float alpha);
int ui_gles_font_end(UiGlesFont *font);
void ui_gles_font_forget(UiGlesFont *font);
void ui_gles_font_close(UiGlesFont *font);

#endif
