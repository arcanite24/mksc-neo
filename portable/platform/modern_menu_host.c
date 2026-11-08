#include "graphics_options.h"
#include "startup_trace.h"
#include "modern_menu_host.h"
#include "mobile_touch.h"
#include "ui_gpu.h"
#include "ui_geometry.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Accelerated UI uses native targets (up to 16M pixels). These lower limits
 * bound only the CPU fallback, which the compositor upscales when needed. */
#ifdef __ANDROID__
enum
{
    MODERN_MENU_PIXEL_LIMIT = 1600 * 900
};
#else
enum
{
    MODERN_MENU_PIXEL_LIMIT = 2560 * 1440
};
#endif

static void close_backend(ModernMenuHost *host);

void modern_menu_host_event(ModernMenuHost *host, const void *sdl_event)
{
    const SDL_Event *event = sdl_event;
    if (host == NULL || event == NULL)
        return;
    if (event->type == SDL_EVENT_RENDER_DEVICE_RESET ||
        event->type == SDL_EVENT_RENDER_TARGETS_RESET)
    {
        /* The renderer identity can survive a device reset. Keep the owned
         * race and presentation clocks, but rebuild all renderer resources. */
        close_backend(host);
        SDL_DestroyTexture(host->texture);
        host->texture = NULL;
        host->texture_width = host->texture_height = 0;
        host->gpu_ui_attempted = host->gpu_ui_reported = host->gpu_ui_failure_reported = 0u;
        return;
    }
    if (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
        (event->type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
         (event->gaxis.value > 16000 || event->gaxis.value < -16000)))
        host->gamepad = 1u;
    else if (event->type == SDL_EVENT_KEY_DOWN)
        host->gamepad = 0u;
}

static void close_backend(ModernMenuHost *host)
{
    ui_gpu_close(host->gpu_ui);
    ui_geometry_close(host->geometry_ui);
    ui_draw_list_close(host->gpu_ui_commands);
    SDL_DestroyTexture(host->gpu_ui_target);
    SDL_DestroyTexture(host->composed_target);
    SDL_DestroyTexture(host->card_target);
    SDL_DestroyTexture(host->race_texture);
    host->gpu_ui = host->geometry_ui = host->gpu_ui_commands = NULL;
    host->gpu_ui_target = host->composed_target = host->card_target = host->race_texture = NULL;
    host->gpu_ui_width = host->gpu_ui_height = 0;
    host->card_width = host->card_height = 0;
    host->race_texture_width = host->race_texture_height = 0;
    host->accelerated_frame = host->pixels_valid = 0u;
    host->pending_background = NULL;
    host->force_cpu = host->retain_only = host->gpu_only = 0u;
}

static void choose_backend(ModernMenuHost *host, SDL_Renderer *renderer)
{
    if (host->backend_renderer != renderer)
    {
        close_backend(host);
        SDL_DestroyTexture(host->texture);
        host->texture = NULL;
        host->backend_renderer = renderer;
        host->gpu_ui_attempted = host->gpu_ui_reported = host->gpu_ui_failure_reported = 0u;
    }
    if (host->gpu_ui_attempted)
        return;
    const uint64_t backend_started = portable_startup_clock();
    host->gpu_ui_attempted = 1u;
    char error[256] = {0};
    if (SDL_GetGPURendererDevice(renderer) != NULL)
        host->gpu_ui = ui_gpu_create(renderer, error, sizeof error);
    if (host->gpu_ui == NULL)
        host->geometry_ui = ui_geometry_create(renderer, error, sizeof error);
    portable_startup_phase("UI shader/pipeline creation", backend_started);
    if (host->gpu_ui != NULL || host->geometry_ui != NULL)
    {
        host->gpu_ui_commands = ui_draw_list_create(4096u, 16u * 1024u * 1024u);
        if (host->gpu_ui_commands == NULL)
            close_backend(host);
    }
}

static int accelerated(const ModernMenuHost *host)
{
    return !host->force_cpu && (host->gpu_ui != NULL || host->geometry_ui != NULL);
}

static int ensure_pixels(ModernMenuHost *host)
{
    const size_t count = (size_t)host->width * (size_t)host->height;
    if (count <= host->pixel_capacity)
        return 1;
    uint32_t *pixels = realloc(host->pixels, count * sizeof *pixels);
    if (pixels == NULL)
        return 0;
    host->pixels = pixels;
    host->pixel_capacity = count;
    host->pixels_valid = 0u;
    return 1;
}

static int ensure_size(ModernMenuHost *host, SDL_Renderer *renderer)
{
    choose_backend(host, renderer);
    int width = host->fixed_width, height = host->fixed_height;
    if (width <= 0 || height <= 0)
    {
        if (!SDL_GetCurrentRenderOutputSize(renderer, &width, &height) || width <= 0 || height <= 0)
            return 0;
        if (width < 320)
        {
            height = (int)(((int64_t)height * 320 + width - 1) / width);
            width = 320;
        }
        if (height < 200)
        {
            width = (int)(((int64_t)width * 200 + height - 1) / height);
            height = 200;
        }
        const int limit = accelerated(host) ? 16 * 1024 * 1024 : MODERN_MENU_PIXEL_LIMIT;
        while ((int64_t)width * height > limit || width > 16384 || height > 16384)
        {
            width = width * 3 / 4;
            height = height * 3 / 4;
        }
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (int64_t)width * height > 16 * 1024 * 1024)
        return 0;
    if (width != host->width || height != host->height)
        host->pixels_valid = 0u;
    host->width = width;
    host->height = height;
    if (accelerated(host))
        return 1;
    if (!ensure_pixels(host))
        return 0;
    if (host->texture == NULL || host->texture_width != width || host->texture_height != height)
    {
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                 SDL_TEXTUREACCESS_STREAMING, width, height);
        if (texture == NULL)
            return 0;
        SDL_DestroyTexture(host->texture);
        host->texture = texture;
        (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        (void)SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
        host->texture_width = width;
        host->texture_height = height;
    }
    return 1;
}

static int ensure_targets(ModernMenuHost *host, SDL_Renderer *renderer)
{
    if (host->gpu_ui_target != NULL && host->composed_target != NULL &&
        host->gpu_ui_width == host->width && host->gpu_ui_height == host->height)
        return 1;
    SDL_Texture *ui = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_TARGET, host->width, host->height);
    SDL_Texture *composed =
        ui != NULL ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                       host->width, host->height)
                   : NULL;
    if (composed == NULL)
    {
        SDL_DestroyTexture(ui);
        return 0;
    }
    SDL_DestroyTexture(host->gpu_ui_target);
    SDL_DestroyTexture(host->composed_target);
    host->gpu_ui_target = ui;
    host->composed_target = composed;
    host->gpu_ui_width = host->width;
    host->gpu_ui_height = host->height;
    return 1;
}

static int capture_composed(ModernMenuHost *host, SDL_Renderer *renderer, int retained)
{
    if (!ensure_pixels(host))
        return 0;
    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    if (retained && host->accelerated_frame &&
        !SDL_SetRenderTarget(renderer, host->composed_target))
        return 0;
    SDL_Surface *surface = SDL_RenderReadPixels(renderer, NULL);
    const int restored = SDL_SetRenderTarget(renderer, previous);
    if (surface == NULL || !restored)
    {
        SDL_DestroySurface(surface);
        return 0;
    }
    SDL_Surface *converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(surface);
    if (converted == NULL)
        return 0;
    SDL_Surface *destination = SDL_CreateSurfaceFrom(
        host->width, host->height, SDL_PIXELFORMAT_ARGB8888, host->pixels, host->width * 4);
    const int valid =
        destination != NULL && SDL_SetSurfaceBlendMode(converted, SDL_BLENDMODE_NONE) &&
        SDL_BlitSurfaceScaled(converted, NULL, destination, NULL, SDL_SCALEMODE_NEAREST);
    SDL_DestroySurface(destination);
    SDL_DestroySurface(converted);
    host->pixels_valid = (uint8_t)(valid != 0);
    return valid;
}

typedef struct
{
    const ModernMenuView *view;
    const ModernMenuSetup *setup;
    const ModernMenuPause *pause;
    const ModernMenuResults *results;
    const ModernMenuTransition *transition;
    ModernMenuCanvasDraw draw;
    ModernMenuLayerDraw layer;
    void *owner;
    int width, height;
} MenuDraw;

static int draw_canvas(ModernMenuHost *host, SDL_Renderer *renderer, const MenuDraw *request,
                       UiCanvas *canvas, void **background)
{
    const float sx = (float)canvas->width / (float)request->width;
    const float sy = (float)canvas->height / (float)request->height;
    if (request->layer)
        return request->layer(host->menu, canvas, renderer, background, request->owner);
    if (request->draw)
        return request->draw(host->menu, canvas, request->owner);
    if (request->view)
        return modern_menu_render(host->menu, request->view, canvas);
    if (request->setup)
        return modern_menu_render_setup(host->menu, request->setup, canvas);
    if (request->pause)
    {
        ModernMenuPause view = *request->pause;
        if (host->force_cpu)
            view.external_background = 0u;
        view.frame_x *= sx;
        view.frame_y *= sy;
        view.frame_w *= sx;
        view.frame_h *= sy;
        return modern_menu_render_pause(host->menu, &view, canvas);
    }
    if (request->results)
    {
        ModernMenuResults view = *request->results;
        if (host->force_cpu)
            view.external_background = 0u;
        view.frame_x *= sx;
        view.frame_y *= sy;
        view.frame_w *= sx;
        view.frame_h *= sy;
        return modern_menu_render_results(host->menu, &view, canvas);
    }
    ModernMenuTransition view = *request->transition;
    if (host->force_cpu)
        view.external_background = 0u;
    view.frame_x *= sx;
    view.frame_y *= sy;
    view.frame_w *= sx;
    view.frame_h *= sy;
    return modern_menu_render_transition(host->menu, &view, canvas);
}

static int present_layers(ModernMenuHost *host, SDL_Renderer *renderer, const ModernMenuView *view,
                          const ModernMenuSetup *setup, const ModernMenuPause *pause,
                          const ModernMenuResults *results, const ModernMenuTransition *transition,
                          ModernMenuCanvasDraw draw, ModernMenuLayerDraw layer, void *owner)
{
    if (host == NULL || renderer == NULL ||
        (view == NULL && setup == NULL && pause == NULL && results == NULL && transition == NULL &&
         draw == NULL && layer == NULL))
        return 0;
    if (host->menu == NULL && (host->menu = modern_menu_create()) == NULL)
        return 0;
    if (!ensure_size(host, renderer))
        return 0;
    const MenuDraw request = {view, setup, pause, results,     transition,
                              draw, layer, owner, host->width, host->height};
    int recording = accelerated(host);
    if (layer != NULL && !ensure_pixels(host))
        return 0;
    UiCanvas canvas;
    ui_canvas_init(&canvas, recording && layer == NULL ? NULL : host->pixels, host->width,
                   host->height, host->width);
    if (recording)
    {
        host->pixels_valid = 0u;
        ui_draw_list_reset(host->gpu_ui_commands);
        ui_canvas_bind_sink(&canvas, ui_draw_list_sink(host->gpu_ui_commands));
    }
    void *background = NULL;
    const uint64_t canvas_started = portable_startup_clock();
    if (!draw_canvas(host, renderer, &request, &canvas, &background))
        return 0;
    static int first_canvas = 1;
    if (first_canvas) { portable_startup_phase("first menu artwork/fonts", canvas_started); first_canvas = 0; }
    if (background == NULL)
        background = host->pending_background;
    SDL_Texture *picture = NULL;
    int rendered = 0;
    if (recording)
    {
        char error[256] = {0};
        const int prepared =
            host->gpu_ui ? ui_gpu_prepare(host->gpu_ui, host->gpu_ui_commands, host->width,
                                          host->height, error, sizeof error)
                         : ui_geometry_prepare(host->geometry_ui, host->gpu_ui_commands,
                                               host->width, host->height, error, sizeof error);
        SDL_Texture *previous = SDL_GetRenderTarget(renderer);
        const SDL_FRect whole = {0, 0, (float)host->width, (float)host->height};
        if (prepared && ensure_targets(host, renderer) &&
            SDL_SetRenderTarget(renderer, host->gpu_ui_target))
        {
            rendered = host->gpu_ui
                           ? ui_gpu_draw(host->gpu_ui, &whole, error, sizeof error)
                           : ui_geometry_draw(host->geometry_ui, &whole, error, sizeof error);
            if (rendered && SDL_SetRenderTarget(renderer, host->composed_target))
            {
                const SDL_FRect placement = host->pending_background != NULL
                                                ? (SDL_FRect){host->pending_x, host->pending_y,
                                                              host->pending_w, host->pending_h}
                                                : whole;
                rendered = SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) &&
                           SDL_RenderClear(renderer) &&
                           (background == NULL ||
                            (SDL_SetTextureBlendMode(background, SDL_BLENDMODE_NONE) &&
                             SDL_RenderTexture(renderer, background, NULL, &placement))) &&
                           SDL_SetTextureBlendMode(host->gpu_ui_target,
                                                   canvas.premultiplied_alpha
                                                       ? SDL_BLENDMODE_BLEND_PREMULTIPLIED
                                                       : SDL_BLENDMODE_NONE) &&
                           SDL_RenderTexture(renderer, host->gpu_ui_target, NULL, &whole);
            }
            else
                rendered = 0;
            if (!SDL_SetRenderTarget(renderer, previous))
                return 0;
        }
        if (rendered)
        {
            picture = host->composed_target;
            host->pixels_valid = 0u;
            if (!host->gpu_ui_reported)
            {
                printf("GPU UI compositor: driver=%s commands=%zu full_canvas_upload=0\n",
                       host->gpu_ui ? SDL_GetGPUDeviceDriver(SDL_GetGPURendererDevice(renderer))
                                    : SDL_GetRendererName(renderer),
                       ui_draw_list_count(host->gpu_ui_commands));
                host->gpu_ui_reported = 1u;
            }
        }
        else
        {
            if (!host->gpu_ui_failure_reported)
            {
                fprintf(stderr, "GPU UI fallback: %s %s\n", error,
                        ui_draw_list_error(host->gpu_ui_commands));
                host->gpu_ui_failure_reported = 1u;
            }
            if (host->gpu_only)
                return 0;
            host->force_cpu = 1u;
            if (!ensure_size(host, renderer))
            {
                host->force_cpu = 0u;
                return 0;
            }
            ui_canvas_init(&canvas, host->pixels, host->width, host->height, host->width);
            background = NULL;
            const int valid = draw_canvas(host, renderer, &request, &canvas, &background);
            host->force_cpu = 0u;
            if (!valid)
                return 0;
        }
    }
    host->accelerated_frame = (uint8_t)(rendered != 0);
    if (!rendered)
    {
        /* A transparent CPU layer is not the composed world picture. */
        host->pixels_valid = (uint8_t)(background == NULL);
        if (!SDL_SetTextureBlendMode(host->texture, canvas.premultiplied_alpha
                                                        ? SDL_BLENDMODE_BLEND_PREMULTIPLIED
                                                        : SDL_BLENDMODE_NONE) ||
            !SDL_UpdateTexture(host->texture, NULL, host->pixels, host->width * 4))
            return 0;
        picture = host->texture;
    }
    host->layer_renderer = renderer;
    host->scene_layer_presented = background != NULL;
    if (host->retain_only)
        return 1;
    int out_w, out_h;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h))
        return 0;
    float scale = (float)out_w / (float)host->width;
    if ((float)out_h / (float)host->height < scale)
        scale = (float)out_h / (float)host->height;
    const SDL_FRect target = {((float)out_w - (float)host->width * scale) * .5f,
                              ((float)out_h - (float)host->height * scale) * .5f,
                              (float)host->width * scale, (float)host->height * scale};
    if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) || !SDL_RenderClear(renderer) ||
        (!rendered && background != NULL &&
         !SDL_RenderTexture(renderer, background, NULL, &target)) ||
        !SDL_SetTextureBlendMode(picture, rendered ? SDL_BLENDMODE_NONE
                                                   : (canvas.premultiplied_alpha
                                                          ? SDL_BLENDMODE_BLEND_PREMULTIPLIED
                                                          : SDL_BLENDMODE_NONE)) ||
        !SDL_RenderTexture(renderer, picture, NULL, &target) ||
        !portable_mobile_touch_draw(renderer))
        return 0;
    if ((rendered || background != NULL) && SDL_GetRenderTarget(renderer) == NULL &&
        getenv("MKSC_SCREEN_DIR") != NULL && !capture_composed(host, renderer, 0))
        return 0;
    if (SDL_GetRenderTarget(renderer) != NULL && SDL_GetRenderWindow(renderer) != NULL)
        return SDL_FlushRenderer(renderer);
    return SDL_RenderPresent(renderer);
}

static int present_canvas(ModernMenuHost *host, SDL_Renderer *renderer, const ModernMenuView *view,
                          const ModernMenuSetup *setup, const ModernMenuPause *pause,
                          const ModernMenuResults *results, const ModernMenuTransition *transition,
                          ModernMenuCanvasDraw draw, void *owner)
{
    return present_layers(host, renderer, view, setup, pause, results, transition, draw, NULL,
                          owner);
}

int modern_menu_host_present_layer(ModernMenuHost *host, void *renderer, ModernMenuLayerDraw draw,
                                   void *owner)
{
    return draw != NULL &&
           present_layers(host, renderer, NULL, NULL, NULL, NULL, NULL, NULL, draw, owner);
}

int modern_menu_host_present(ModernMenuHost *host, void *sdl_renderer, const ModernMenuView *view)
{
    return view != NULL &&
           present_canvas(host, sdl_renderer, view, NULL, NULL, NULL, NULL, NULL, NULL);
}

int modern_menu_host_present_setup(ModernMenuHost *host, void *sdl_renderer,
                                   const ModernMenuSetup *setup)
{
    return setup != NULL &&
           present_canvas(host, sdl_renderer, NULL, setup, NULL, NULL, NULL, NULL, NULL);
}

int modern_menu_host_present_canvas(ModernMenuHost *host, void *sdl_renderer,
                                    ModernMenuCanvasDraw draw, void *owner)
{
    return draw != NULL &&
           present_canvas(host, sdl_renderer, NULL, NULL, NULL, NULL, NULL, draw, owner);
}

static int upload_race(ModernMenuHost *host, SDL_Renderer *renderer, int width, int height)
{
    if (!accelerated(host))
        return 1;
    if (host->race_texture == NULL || host->race_texture_width != width ||
        host->race_texture_height != height)
    {
        SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                 SDL_TEXTUREACCESS_STATIC, width, height);
        if (texture == NULL)
            goto failed;
        SDL_DestroyTexture(host->race_texture);
        host->race_texture = texture;
        host->race_texture_width = width;
        host->race_texture_height = height;
    }
    if (SDL_SetTextureScaleMode(host->race_texture, SDL_SCALEMODE_LINEAR) &&
        SDL_UpdateTexture(host->race_texture, NULL, host->race, width * 4))
        return 1;
failed:
    /* A failed upload cannot become a reusable frozen snapshot next frame. */
    SDL_DestroyTexture(host->race_texture);
    host->race_texture = NULL;
    host->race_texture_width = host->race_texture_height = 0;
    return 0;
}

static int cpu_race_fallback(ModernMenuHost *host, SDL_Renderer *renderer, float *x, float *y,
                             float *w, float *h)
{
    if (!host->gpu_ui_failure_reported)
    {
        fprintf(stderr, "GPU UI fallback: frozen race upload: %s\n", SDL_GetError());
        host->gpu_ui_failure_reported = 1u;
    }
    const int width = host->width, height = host->height;
    host->pending_background = NULL;
    host->force_cpu = 1u;
    if (!ensure_size(host, renderer))
    {
        host->force_cpu = 0u;
        return 0;
    }
    *x *= (float)host->width / (float)width;
    *y *= (float)host->height / (float)height;
    *w *= (float)host->width / (float)width;
    *h *= (float)host->height / (float)height;
    return 1;
}

static void background_race(ModernMenuHost *host, float x, float y, float w, float h)
{
    host->pending_background = host->race_texture;
    host->pending_x = x;
    host->pending_y = y;
    host->pending_w = w;
    host->pending_h = h;
}

static int capture_named(ModernMenuHost *host, const char *name)
{
    const char *directory = getenv("MKSC_SCREEN_DIR");
    if (name == NULL || directory == NULL || directory[0] == '\0')
        return 1;
    if (!host->pixels_valid &&
        !capture_composed(host, host->layer_renderer,
                          host->accelerated_frame &&
                              SDL_GetRenderTarget(host->layer_renderer) == NULL))
        return 0;
    PortableSoftwareFrame shown;
    return portable_software_frame_init(&shown, host->pixels, (size_t)host->width,
                                        (size_t)host->height) &&
           portable_screen_capture(&shown, name);
}

static const char *pause_label(PortablePauseAction action, ModernMenuPauseKind *kind)
{
    switch (action)
    {
    case PORTABLE_PAUSE_ACTION_CONTINUE:
        *kind = MODERN_PAUSE_CONTINUE;
        return "Continue";
    case PORTABLE_PAUSE_ACTION_RETRY:
        *kind = MODERN_PAUSE_RETRY;
        return "Restart";
    case PORTABLE_PAUSE_ACTION_GP_RETRY:
        *kind = MODERN_PAUSE_RETRY;
        return "Restart (uses a life)";
    case PORTABLE_PAUSE_ACTION_CHANGE_TRACK:
        *kind = MODERN_PAUSE_CHANGE_COURSE;
        return "Change Course";
    case PORTABLE_PAUSE_ACTION_CHANGE_CHARACTER:
        *kind = MODERN_PAUSE_CHANGE_DRIVER;
        return "Change Driver";
    default:
        *kind = MODERN_PAUSE_QUIT;
        return "Quit";
    }
}

int modern_menu_host_present_pause(ModernMenuHost *host, void *sdl_renderer,
                                   const PortableSoftwareFrame *frame, int fit,
                                   const PortablePauseMenu *pause, const char *subtitle,
                                   const char *capture)
{
    if (host == NULL || frame == NULL || frame->pixels == NULL || pause == NULL ||
        pause->item_count == 0u || pause->item_count > MODERN_MENU_PAUSE_ITEMS)
        return 0;
    const size_t scale = portable_software_frame_scale(frame),
                 stride = portable_software_frame_stride(frame);
    const size_t width = frame->width * scale, height = frame->height * scale;
    if (width * height > host->race_capacity)
    {
        uint32_t *grown = realloc(host->race, width * height * sizeof *grown);
        if (grown == NULL)
            return 0;
        host->race = grown;
        host->race_capacity = width * height;
    }
    const int frozen = host->pause_frames != 0u && host->pause_width == (int)width &&
                       host->pause_height == (int)height;
    if (!frozen)
        for (size_t y = 0u; y < height; ++y)
            for (size_t x = 0u; x < width; ++x)
                host->race[y * width + x] = frame->pixels[y * stride + x] | 0xff000000u;
    host->pause_width = (int)width;
    host->pause_height = (int)height;
    /* Place the race where portable_software_frame_present would, mapped
     * from output pixels into the (possibly capped) canvas. */
    SDL_Renderer *renderer = sdl_renderer;
    int out_w, out_h;
    if (host->menu == NULL && (host->menu = modern_menu_create()) == NULL)
        return 0;
    if (!ensure_size(host, renderer) || !SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h) ||
        out_w <= 0 || out_h <= 0)
        return 0;
    double fit_scale = (double)out_w / (double)width;
    if ((double)out_h / (double)height < fit_scale)
        fit_scale = (double)out_h / (double)height;
    if (!fit && fit_scale * (double)scale >= 1.0)
        fit_scale = (int)(fit_scale * (double)scale) / (double)scale;
    const double to_canvas = (double)host->width / (double)out_w;
    ModernMenuPause view = {
        .frame = host->race,
        .frame_width = (int)width,
        .frame_height = (int)height,
        .frame_x = (float)(((double)out_w - (double)width * fit_scale) * 0.5 * to_canvas),
        .frame_y = (float)(((double)out_h - (double)height * fit_scale) * 0.5 * to_canvas),
        .frame_w = (float)((double)width * fit_scale * to_canvas),
        .frame_h = (float)((double)height * fit_scale * to_canvas),
        .subtitle = subtitle,
        .item_count = pause->item_count,
        .cursor = pause->cursor,
        .tick = host->pause_frames++,
        .gamepad = host->gamepad,
    };
    if (accelerated(host))
    {
        if ((!frozen || host->race_texture == NULL) &&
            !upload_race(host, renderer, (int)width, (int)height))
        {
            if (!cpu_race_fallback(host, renderer, &view.frame_x, &view.frame_y, &view.frame_w,
                                   &view.frame_h))
                return 0;
        }
        else
        {
            view.external_background = 1u;
            background_race(host, view.frame_x, view.frame_y, view.frame_w, view.frame_h);
        }
    }
    ModernMenuOptions graphics;
    if (pause->graphics.open)
    {
        portable_graphics_modern_options(&graphics, &pause->graphics);
        view.graphics = &graphics;
    }
    for (unsigned i = 0u; i < pause->item_count; ++i)
        view.labels[i] = pause_label((PortablePauseAction)pause->items[i], &view.kinds[i]);
    const int presented =
        present_canvas(host, sdl_renderer, NULL, NULL, &view, NULL, NULL, NULL, NULL);
    host->pending_background = NULL;
    host->force_cpu = 0u;
    if (!presented)
        return 0;
    if (capture == NULL || host->pause_frames != PORTABLE_PAUSE_MENU_OPEN_TICKS)
        return 1;
    return capture_named(host, capture);
}

void modern_menu_host_pause_closed(ModernMenuHost *host)
{
    if (host != NULL)
        host->pause_frames = 0u;
}

const char *modern_menu_host_result_action_label(uint8_t code)
{
    static const char *const labels[5] = {"Try Again", "Change Course", "Change Driver", "Replay",
                                          "Quit"};
    return code < 5u ? labels[code] : "Continue";
}

void modern_menu_host_format_time(char *text, size_t capacity, uint32_t centiseconds)
{
    (void)snprintf(text, capacity, "%u:%02u.%02u", centiseconds / 6000u, centiseconds / 100u % 60u,
                   centiseconds % 100u);
}

void modern_menu_host_results_closed(ModernMenuHost *host)
{
    if (host != NULL)
    {
        host->results_active = 0u;
        host->results_capture = NULL;
    }
}

int modern_menu_host_present_results(ModernMenuHost *host, void *sdl_renderer,
                                     const PortableSoftwareFrame *frame, int fit,
                                     const ModernMenuResults *results, const char *capture)
{
    if (host == NULL || results == NULL || frame == NULL || frame->pixels == NULL)
        return 0;
    SDL_Renderer *renderer = sdl_renderer;
    const int new_race = !host->results_active;
    const int refresh = new_race || results->live_frame;
    if (refresh)
    {
        const size_t scale = portable_software_frame_scale(frame);
        const size_t width = frame->width * scale, height = frame->height * scale;
        if (width == 0u || height == 0u || width > 16384u || height > 16384u ||
            portable_software_frame_stride(frame) < width)
            return 0;
        if (width * height > host->race_capacity)
        {
            uint32_t *grown = realloc(host->race, width * height * sizeof *grown);
            if (grown == NULL)
                return 0;
            host->race = grown;
            host->race_capacity = width * height;
        }
        for (size_t y = 0u; y < height; ++y)
            for (size_t x = 0u; x < width; ++x)
                host->race[y * width + x] =
                    frame->pixels[y * portable_software_frame_stride(frame) + x] | 0xff000000u;
        host->result_width = (int)width;
        host->result_height = (int)height;
        host->results_active = 1u;
        if (new_race)
            host->results_entry_tick = results->tick;
    }
    int out_w, out_h;
    if (!ensure_size(host, renderer) || !SDL_GetCurrentRenderOutputSize(renderer, &out_w, &out_h) ||
        out_w <= 0 || out_h <= 0)
        return 0;
    double scale = (double)out_w / host->result_width;
    if ((double)out_h / host->result_height < scale)
        scale = (double)out_h / host->result_height;
    const size_t internal_scale = portable_software_frame_scale(frame);
    if (!fit && scale * (double)internal_scale >= 1.0)
        scale = (int)(scale * (double)internal_scale) / (double)internal_scale;
    ModernMenuResults view = *results;
    view.frame = host->race;
    view.frame_width = host->result_width;
    view.frame_height = host->result_height;
    view.frame_x =
        (float)(((double)out_w - host->result_width * scale) * 0.5 * host->width / out_w);
    view.frame_y =
        (float)(((double)out_h - host->result_height * scale) * 0.5 * host->height / out_h);
    view.frame_w = (float)(host->result_width * scale * host->width / out_w);
    view.frame_h = (float)(host->result_height * scale * host->height / out_h);
    view.gamepad = host->gamepad;
    if (capture != host->results_capture)
    {
        host->results_capture = capture;
        host->results_capture_tick = view.tick;
    }
    if (accelerated(host))
    {
        if ((refresh || host->race_texture == NULL) &&
            !upload_race(host, renderer, host->result_width, host->result_height))
        {
            if (!cpu_race_fallback(host, renderer, &view.frame_x, &view.frame_y, &view.frame_w,
                                   &view.frame_h))
                return 0;
        }
        else
        {
            view.external_background = 1u;
            background_race(host, view.frame_x, view.frame_y, view.frame_w, view.frame_h);
        }
    }
    const uint32_t elapsed = view.tick - host->results_entry_tick;
    if (elapsed < 16u && accelerated(host))
    {
        host->retain_only = host->gpu_only = 1u;
        const int board = present_canvas(host, renderer, NULL, NULL, NULL, &view, NULL, NULL, NULL);
        host->retain_only = 0u;
        SDL_Texture *previous = SDL_GetRenderTarget(renderer);
        if (board && (host->card_target == NULL || host->card_width != host->width ||
                      host->card_height != host->height))
        {
            SDL_Texture *card =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                  host->width, host->height);
            if (card != NULL)
            {
                SDL_DestroyTexture(host->card_target);
                host->card_target = card;
                host->card_width = host->width;
                host->card_height = host->height;
            }
        }
        SDL_Texture *card =
            board && host->card_width == host->width && host->card_height == host->height
                ? host->card_target
                : NULL;
        const int copied = card != NULL && SDL_SetRenderTarget(renderer, card) &&
                           SDL_SetTextureBlendMode(host->composed_target, SDL_BLENDMODE_NONE) &&
                           SDL_RenderTexture(renderer, host->composed_target, NULL, NULL);
        const int restored = SDL_SetRenderTarget(renderer, previous);
        if (!restored)
        {
            host->gpu_only = 0u;
            return 0;
        }
        if (copied)
        {
            host->pending_background = card;
            host->pending_x = host->pending_y = 0;
            host->pending_w = (float)host->width;
            host->pending_h = (float)host->height;
            const ModernMenuTransition transition = {.pack = view.pack,
                                                     .title = view.title,
                                                     .subtitle = "Race complete",
                                                     .course_id = view.course_id,
                                                     .elapsed = elapsed,
                                                     .duration = 16u,
                                                     .kind = MODERN_TRANSITION_RESULTS,
                                                     .gamepad = view.gamepad,
                                                     .external_background = 1u};
            const int presented =
                present_canvas(host, renderer, NULL, NULL, NULL, NULL, &transition, NULL, NULL);
            host->gpu_only = 0u;
            host->pending_background = NULL;
            if (presented)
                return capture_named(host, "results-card-modern");
        }
        else
        {
            host->gpu_only = 0u;
        }
        /* Reconstruct both CPU layers from the immutable race if either GPU
         * pass failed; never read the partial board back into the CPU path. */
        host->pending_background = NULL;
        host->force_cpu = 1u;
        if (!ensure_size(host, renderer))
        {
            host->force_cpu = 0u;
            return 0;
        }
        view.external_background = 0u;
        view.frame_x =
            (float)(((double)out_w - host->result_width * scale) * .5 * host->width / out_w);
        view.frame_y =
            (float)(((double)out_h - host->result_height * scale) * .5 * host->height / out_h);
        view.frame_w = (float)(host->result_width * scale * host->width / out_w);
        view.frame_h = (float)(host->result_height * scale * host->height / out_h);
    }
    if (elapsed < 16u)
    {
        if (host->menu == NULL && (host->menu = modern_menu_create()) == NULL)
        {
            host->force_cpu = 0u;
            return 0;
        }
        if (!ensure_pixels(host))
        {
            host->force_cpu = 0u;
            return 0;
        }
        UiCanvas canvas;
        ui_canvas_init(&canvas, host->pixels, host->width, host->height, host->width);
        ModernMenuResults cpu_view = view;
        cpu_view.external_background = 0u;
        if (!modern_menu_render_results(host->menu, &cpu_view, &canvas))
        {
            host->force_cpu = 0u;
            return 0;
        }
        const size_t count = (size_t)host->width * (size_t)host->height;
        if (count > host->card_capacity)
        {
            uint32_t *grown = realloc(host->card_pixels, count * sizeof *grown);
            if (grown == NULL)
            {
                host->force_cpu = 0u;
                return 0;
            }
            host->card_pixels = grown;
            host->card_capacity = count;
        }
        memcpy(host->card_pixels, host->pixels, count * sizeof *host->card_pixels);
        const ModernMenuTransition card = {.pack = view.pack,
                                           .frame = host->card_pixels,
                                           .frame_width = host->width,
                                           .frame_height = host->height,
                                           .frame_x = 0.0f,
                                           .frame_y = 0.0f,
                                           .frame_w = (float)host->width,
                                           .frame_h = (float)host->height,
                                           .title = view.title,
                                           .subtitle = "Race complete",
                                           .course_id = view.course_id,
                                           .elapsed = elapsed,
                                           .duration = 16u,
                                           .kind = MODERN_TRANSITION_RESULTS,
                                           .gamepad = view.gamepad};
        const int presented =
            present_canvas(host, renderer, NULL, NULL, NULL, NULL, &card, NULL, NULL);
        host->force_cpu = 0u;
        host->pending_background = NULL;
        return presented && capture_named(host, "results-card-modern");
    }
    const int presented = present_canvas(host, renderer, NULL, NULL, NULL, &view, NULL, NULL, NULL);
    host->pending_background = NULL;
    host->force_cpu = 0u;
    if (!presented)
        return 0;
    /* Capture the settled overlay, not its empty first entrance frame. */
    if (view.tick - host->results_capture_tick < 32u)
        return 1;
    return capture_named(host, capture);
}

/* Entry cards borrow no simulation storage across calls. Pack padded physical
 * rows into the same owned picture used by pause/results before presenting. */
int modern_menu_host_present_transition(ModernMenuHost *host, void *sdl_renderer,
                                        const PortableSoftwareFrame *frame, int fit,
                                        const ModernMenuTransition *transition, const char *capture)
{
    if (host == NULL || sdl_renderer == NULL || transition == NULL)
        return 0;
    ModernMenuTransition view = *transition;
    if (frame != NULL)
    {
        if (frame->pixels == NULL || host->results_active)
            return 0;
        const size_t internal_scale = portable_software_frame_scale(frame);
        const size_t width = frame->width * internal_scale, height = frame->height * internal_scale;
        const size_t stride = portable_software_frame_stride(frame);
        if (width == 0u || height == 0u || width > 16384u || height > 16384u || stride < width)
            return 0;
        if (width * height > host->race_capacity)
        {
            uint32_t *grown = realloc(host->race, width * height * sizeof *grown);
            if (grown == NULL)
                return 0;
            host->race = grown;
            host->race_capacity = width * height;
        }
        for (size_t y = 0u; y < height; ++y)
            for (size_t x = 0u; x < width; ++x)
                host->race[y * width + x] = frame->pixels[y * stride + x] | 0xff000000u;
        int out_w, out_h;
        if (!ensure_size(host, sdl_renderer) ||
            !SDL_GetCurrentRenderOutputSize(sdl_renderer, &out_w, &out_h) || out_w <= 0 ||
            out_h <= 0)
            return 0;
        double scale = (double)out_w / (double)width;
        if ((double)out_h / (double)height < scale)
            scale = (double)out_h / (double)height;
        if (!fit && scale * (double)internal_scale >= 1.0)
            scale = (int)(scale * (double)internal_scale) / (double)internal_scale;
        view.frame = host->race;
        view.frame_width = (int)width;
        view.frame_height = (int)height;
        view.frame_x = (float)(((double)out_w - (double)width * scale) * 0.5 * host->width / out_w);
        view.frame_y =
            (float)(((double)out_h - (double)height * scale) * 0.5 * host->height / out_h);
        view.frame_w = (float)((double)width * scale * host->width / out_w);
        view.frame_h = (float)((double)height * scale * host->height / out_h);
        if (accelerated(host))
        {
            if (!upload_race(host, sdl_renderer, (int)width, (int)height))
            {
                if (!cpu_race_fallback(host, sdl_renderer, &view.frame_x, &view.frame_y,
                                       &view.frame_w, &view.frame_h))
                    return 0;
            }
            else
            {
                view.external_background = 1u;
                background_race(host, view.frame_x, view.frame_y, view.frame_w, view.frame_h);
            }
        }
    }
    view.gamepad = host->gamepad;
    const int presented =
        present_canvas(host, sdl_renderer, NULL, NULL, NULL, NULL, &view, NULL, NULL);
    host->pending_background = NULL;
    host->force_cpu = 0u;
    return presented && capture_named(host, capture);
}

int modern_menu_host_write_ppm(ModernMenuHost *host, const char *path)
{
    if (host == NULL || path == NULL)
        return 0;
    if (!host->pixels_valid &&
        (host->layer_renderer == NULL ||
         !capture_composed(host, host->layer_renderer,
                           host->accelerated_frame &&
                               SDL_GetRenderTarget(host->layer_renderer) == NULL)))
        return 0;
    if (host->pixels == NULL)
        return 0;
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    int ok = fprintf(file, "P6\n%d %d\n255\n", host->width, host->height) > 0;
    unsigned char *row = malloc((size_t)host->width * 3u);
    ok = ok && row != NULL;
    for (int y = 0; ok && y < host->height; ++y)
    {
        const uint32_t *source = host->pixels + (size_t)y * (size_t)host->width;
        for (int x = 0; x < host->width; ++x)
        {
            row[x * 3] = (unsigned char)(source[x] >> 16);
            row[x * 3 + 1] = (unsigned char)(source[x] >> 8);
            row[x * 3 + 2] = (unsigned char)source[x];
        }
        ok = fwrite(row, 3u, (size_t)host->width, file) == (size_t)host->width;
    }
    free(row);
    if (fclose(file) != 0)
        ok = 0;
    return ok;
}

void modern_menu_host_content_changed(ModernMenuHost *host)
{
    if (host == NULL)
        return;
    modern_menu_content_changed(host->menu);
    ui_gpu_forget_images(host->gpu_ui);
    ui_geometry_forget_images(host->geometry_ui);
    ui_draw_list_reset(host->gpu_ui_commands);
    SDL_DestroyTexture(host->card_target);
    host->card_target = NULL;
    SDL_DestroyTexture(host->race_texture);
    host->race_texture = NULL;
    host->pixels_valid = host->accelerated_frame = host->results_active = 0u;
    host->pending_background = NULL;
    host->pause_frames = 0u;
}

PortableMenuDriverModel *modern_menu_host_driver_model(ModernMenuHost *host)
{
    if (host == NULL || (host->menu == NULL && (host->menu = modern_menu_create()) == NULL)) return NULL;
    return modern_menu_driver_model(host->menu);
}

void modern_menu_host_close(ModernMenuHost *host)
{
    if (host == NULL)
        return;
    close_backend(host);
    if (host->texture != NULL)
        SDL_DestroyTexture(host->texture);
    modern_menu_destroy(host->menu);
    free(host->pixels);
    free(host->race);
    free(host->card_pixels);
    *host = (ModernMenuHost){0};
}
