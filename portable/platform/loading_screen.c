#include "loading_screen.h"
#include "startup_trace.h"
#include "mobile_lifecycle.h"
#include "modern_menu_host.h"
#include "software_frame.h"
#include "sdl_gamepad.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static SDL_Window *startup_window;
static SDL_Renderer *startup_renderer;

/* The modern menu's loading card, drawn on a CPU canvas capped like the menu
 * host's and stretched to the window. The still layer is cached; each frame
 * only the card's live rectangle is redrawn and uploaded. */
enum { LOADING_PIXEL_LIMIT = 1920 * 1200 };
typedef struct {
    ModernMenu *menu;
    SDL_Texture *texture;
    uint32_t *still, *frame;
    int width, height, cancelling, cancellable, gamepad;
    const char *title;
    unsigned capture;
} LoadingSurface;

static void loading_surface_close(LoadingSurface *surface)
{
    SDL_DestroyTexture(surface->texture);
    free(surface->still);
    free(surface->frame);
    modern_menu_destroy(surface->menu);
    *surface = (LoadingSurface){0};
}

static int loading_canvas_size(SDL_Renderer *renderer, int *width, int *height)
{
    int w, h;
    if (!SDL_GetCurrentRenderOutputSize(renderer, &w, &h)) return 0;
    if (w <= 0 || h <= 0) return -1; /* Minimized. */
    /* Automated runs capture at the modern menus' fixed canvas size. */
    const char *fixed = getenv("MKSC_MODERN_MENU_SIZE");
    int fw, fh;
    if (fixed != NULL && sscanf(fixed, "%dx%d", &fw, &fh) == 2 && fw > 0 && fh > 0) { w = fw; h = fh; }
    double scale = 1.0;
    if ((double)w * (double)h > (double)LOADING_PIXEL_LIMIT)
        scale = sqrt((double)LOADING_PIXEL_LIMIT / ((double)w * (double)h));
    *width = (int)((double)w * scale) > 0 ? (int)((double)w * scale) : 1;
    *height = (int)((double)h * scale) > 0 ? (int)((double)h * scale) : 1;
    return 1;
}

static int draw_loading(SDL_Renderer *renderer, LoadingSurface *surface, const char *title, const char *detail,
                        const char *const *phases, unsigned phase, unsigned total, int cancelling, int cancellable,
                        int gamepad)
{
    int width, height;
    const int sized = loading_canvas_size(renderer, &width, &height);
    if (sized <= 0) return sized < 0;
    if (surface->menu == NULL && (surface->menu = modern_menu_create()) == NULL) return 0;
    int rebuild = surface->texture == NULL || width != surface->width || height != surface->height ||
                  cancelling != surface->cancelling || cancellable != surface->cancellable || gamepad != surface->gamepad || title != surface->title;
    if (surface->texture == NULL || width != surface->width || height != surface->height) {
        SDL_DestroyTexture(surface->texture);
        free(surface->still);
        free(surface->frame);
        const size_t pixels = (size_t)width * (size_t)height;
        surface->still = malloc(pixels * sizeof *surface->still);
        surface->frame = malloc(pixels * sizeof *surface->frame);
        surface->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             width, height);
        if (surface->still == NULL || surface->frame == NULL || surface->texture == NULL) return 0;
        (void)SDL_SetTextureScaleMode(surface->texture, SDL_SCALEMODE_LINEAR);
        (void)SDL_SetTextureBlendMode(surface->texture, SDL_BLENDMODE_NONE);
        surface->width = width;
        surface->height = height;
    }
    const ModernMenuLoading view = {
        .title = title, .detail = detail, .phases = phases, .phase_count = total, .phase = phase,
        .cancelling = cancelling, .cancellable = (uint8_t)(cancellable != 0), .tick = (uint32_t)(SDL_GetTicks() * 597u / 10000u), .gamepad = (uint8_t)gamepad};
    UiCanvas canvas;
    int rect[4];
    modern_menu_loading_live_rect(width, height, rect);
    if (rebuild) {
        ui_canvas_init(&canvas, surface->still, width, height, width);
        if (!modern_menu_render_loading(surface->menu, &view, &canvas, 0)) return 0;
        memcpy(surface->frame, surface->still, (size_t)width * (size_t)height * sizeof *surface->frame);
        surface->cancelling = cancelling;
        surface->cancellable = cancellable;
        surface->gamepad = gamepad;
        surface->title = title;
    } else
        for (int y = rect[1]; y < rect[3]; ++y)
            memcpy(surface->frame + (size_t)y * (size_t)width + (size_t)rect[0],
                   surface->still + (size_t)y * (size_t)width + (size_t)rect[0],
                   (size_t)(rect[2] - rect[0]) * sizeof *surface->frame);
    ui_canvas_init(&canvas, surface->frame, width, height, width);
    if (!modern_menu_render_loading(surface->menu, &view, &canvas, 1)) return 0;
    const SDL_Rect live = {rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]};
    if (rebuild ? !SDL_UpdateTexture(surface->texture, NULL, surface->frame, width * 4)
                : live.w > 0 && live.h > 0 &&
                  !SDL_UpdateTexture(surface->texture, &live,
                                     surface->frame + (size_t)live.y * (size_t)width + (size_t)live.x, width * 4))
        return 0;
    if (getenv("MKSC_SCREEN_DIR") && getenv("MKSC_STARTUP_TIMING")) {
        /* Each loading screen's first frame, and one a second later. */
        static unsigned screens;
        if (surface->capture == 0u || surface->capture == 60u) {
            char name[40];
            if (surface->capture == 0u) ++screens;
            snprintf(name, sizeof name, "loading-%u%s", screens, surface->capture ? "-live" : "");
            PortableSoftwareFrame frame = {.pixels = surface->frame, .width = (size_t)width,
                .height = (size_t)height, .stride = (size_t)width};
            (void)portable_screen_capture(&frame, name);
        }
        ++surface->capture;
    }
    if (!SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) || !SDL_RenderClear(renderer) ||
        !SDL_RenderTexture(renderer, surface->texture, NULL, NULL))
        return 0;
    return SDL_RenderPresent(renderer);
}

int portable_loading_begin(int hidden)
{
    if (startup_window) return 1;
    const uint64_t since = portable_startup_clock();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) return 0;
    portable_startup_phase("SDL init", since);
    portable_mobile_lifecycle_start();
    /* Hidden automated runs keep the shell's plain 1x window flags. */
    startup_window = SDL_CreateWindow("MKSC Neo - Loading", 960, 640, SDL_WINDOW_HIDDEN |
        (hidden ? 0u : SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | PORTABLE_WINDOW_ALWAYS_FULLSCREEN));
    /* Same preference order as the shell, which inherits this renderer. */
    if (startup_window && !hidden)
        startup_renderer = SDL_CreateGPURenderer(NULL, startup_window);
    for (int major = 3; startup_window && !hidden && !startup_renderer && major >= 2; --major) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        startup_renderer = SDL_CreateRenderer(startup_window, "opengles2");
    }
    if (startup_window && !startup_renderer)
        startup_renderer = SDL_CreateRenderer(startup_window, hidden ? "software" : NULL);
    portable_startup_phase("loading SDL/window/GPU init", since);
    static const char *const phases[] = {"Opening game data"};
    LoadingSurface surface = {0};
    const int presented = startup_renderer && draw_loading(startup_renderer, &surface,
        "Starting your game", NULL, phases, 0, 1, 0, 0, 0);
    if (presented && !hidden) {
        SDL_ShowWindow(startup_window);
        (void)draw_loading(startup_renderer, &surface, "Starting your game", NULL, phases, 0, 1, 0, 0, 0);
    }
    loading_surface_close(&surface);
    if (!presented) {
        portable_loading_close();
        return 0;
    }
    portable_startup_mark("first loading frame");
    return 1;
}
void portable_loading_close(void)
{
    if (startup_window) portable_mobile_lifecycle_stop();
    SDL_DestroyRenderer(startup_renderer);
    SDL_DestroyWindow(startup_window);
    startup_renderer = NULL;
    startup_window = NULL;
}
int portable_loading_take(SDL_Window **window, SDL_Renderer **renderer)
{
    if (!startup_window) return 0;
    *window = startup_window;
    *renderer = startup_renderer;
    startup_window = NULL;
    startup_renderer = NULL;
    return 1;
}
int portable_loading_run(SDL_Window *window, SDL_Renderer *renderer,
    const char *title, const char *const *phases, unsigned phase_count,
    PortableLoadWork work, void *user)
{
    return portable_loading_run_detail(window, renderer, title, NULL, phases, phase_count, work, user);
}

int portable_loading_run_detail(SDL_Window *window, SDL_Renderer *renderer,
    const char *title, const char *detail, const char *const *phases, unsigned phase_count,
    PortableLoadWork work, void *user)
{
    if (!window) window = startup_window;
    if (!renderer) renderer = startup_renderer;
    if (!renderer || !phase_count || !phases) return 0;
    SDL_Gamepad *controller = portable_sdl_gamepad_open_first();
    LoadingSurface surface = {0};
    if (!draw_loading(renderer, &surface, title, detail, phases, 0, phase_count, 0, 1, controller != NULL)) {
        if (controller) SDL_CloseGamepad(controller);
        loading_surface_close(&surface);
        return 0;
    }
    PortableLoadJob job = {0};
    if (!portable_load_start(&job, work, user)) {
        (void)portable_load_finish(&job);
        if (controller) SDL_CloseGamepad(controller);
        loading_surface_close(&surface);
        fprintf(stderr, "content worker: %s\n", SDL_GetError());
        return 0;
    }
    const uint64_t since = portable_startup_clock();
    unsigned frames = 0;
    int quit = 0, failed = 0;
    SDL_Event deferred[32];
    unsigned deferred_count = 0;
    while (portable_load_state(&job) < PORTABLE_LOAD_READY) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            portable_sdl_gamepad_device_event(&controller, &event);
            if ((event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED ||
                 event.type == SDL_EVENT_RENDER_DEVICE_RESET || event.type == SDL_EVENT_RENDER_TARGETS_RESET ||
                 event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST) &&
                deferred_count < sizeof deferred / sizeof deferred[0]) deferred[deferred_count++] = event;
            if (event.type == SDL_EVENT_RENDER_DEVICE_RESET || event.type == SDL_EVENT_RENDER_TARGETS_RESET) {
                SDL_DestroyTexture(surface.texture); /* Recreated with its still layer. */
                surface.texture = NULL;
            }
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) ||
                (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) ||
                portable_mobile_back(&event)) {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                    quit = 2;
                else if (!quit) quit = 1;
                portable_load_cancel(&job);
            }

        }
        unsigned phase = (unsigned)SDL_GetAtomicInt(&job.phase);
        if (phase >= phase_count) phase = phase_count - 1u;
        if (!failed && !draw_loading(renderer, &surface, title, detail, phases, phase, phase_count, quit, 1,
                                     controller != NULL)) {
            failed = 1;
            portable_load_cancel(&job);
        }
        ++frames;
        SDL_Delay(16);
    }
    const PortableLoadState result = portable_load_finish(&job);
    if (controller) SDL_CloseGamepad(controller);
    loading_surface_close(&surface);
    portable_startup_phase(title, since);
    if (getenv("MKSC_STARTUP_TIMING")) fprintf(stderr, "loading frames: %s %u\n", title, frames);
    for (unsigned i = 0; i < deferred_count; ++i) SDL_PushEvent(&deferred[i]);
    if (quit == 2) return -2;
    if (quit || result == PORTABLE_LOAD_CANCELLED) return -1;
    return !failed && result == PORTABLE_LOAD_READY;
}
