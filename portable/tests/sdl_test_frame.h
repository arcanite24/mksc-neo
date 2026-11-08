#ifndef PORTABLE_SDL_TEST_FRAME_H
#define PORTABLE_SDL_TEST_FRAME_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* SDL3 readback owns a surface and may use a different pixel format/pitch. */
static int read_test_frame(SDL_Renderer *renderer,uint32_t pixels[240u*160u])
{
    SDL_Surface *surface=SDL_RenderReadPixels(renderer,NULL);
    if(surface==NULL)return 0;
    const int valid=surface->w==240&&surface->h==160&&
        SDL_ConvertPixels(240,160,surface->format,surface->pixels,surface->pitch,
            SDL_PIXELFORMAT_ARGB8888,pixels,240*(int)sizeof *pixels);
    SDL_DestroySurface(surface);return valid;
}

#endif
