#ifndef PORTABLE_MODERN_MENU_HOST_H
#define PORTABLE_MODERN_MENU_HOST_H

#include "modern_menu.h"
#include "pause_menu.h"
#include "software_frame.h"

/* Window flag added on platforms without desktop windows (Android). */
#ifdef __ANDROID__
#define PORTABLE_WINDOW_ALWAYS_FULLSCREEN SDL_WINDOW_FULLSCREEN
#else
#define PORTABLE_WINDOW_ALWAYS_FULLSCREEN 0u
#endif

/* Shared SDL compositor. Accelerated UI retains target textures at native
 * output resolution; the bounded CPU fallback keeps its streaming canvas. */
typedef struct
{
    ModernMenu *menu;
    void *texture; /* SDL_Texture */
    uint32_t *pixels;
    size_t pixel_capacity;
    uint8_t pixels_valid;
    int width, height, texture_width, texture_height;
    int fixed_width, fixed_height; /* Nonzero: render at this size (hidden aid windows). */
    uint8_t gamepad;               /* Last input came from a controller. */
    uint32_t *race;                /* Pause: contiguous copy of the race picture. */
    size_t race_capacity;
    int pause_width, pause_height; /* Dimensions of the owned frozen pause picture. */
    uint32_t
        *card_pixels; /* Temporary presented board beneath a transition; never the clean race. */
    size_t card_capacity;
    int result_width, result_height;
    uint8_t results_active;      /* Frozen clean race retained across result stages. */
    const char *results_capture; /* Borrowed static stage name. */
    uint32_t results_capture_tick, results_entry_tick;
    uint32_t pause_frames;         /* Pause frames presented (one per game tick); 0 when closed. */
    void *layer_renderer;          /* Borrowed until parent renderer closes. Capture only. */
    uint8_t scene_layer_presented; /* Last frame includes a GPU background under the canvas. */
    void *gpu_ui, *gpu_ui_target, *gpu_ui_commands; /* Renderer-owned U7 resources. */
    void *geometry_ui, *backend_renderer;
    void *composed_target, *card_target, *race_texture;
    int gpu_ui_width, gpu_ui_height, card_width, card_height;
    int race_texture_width, race_texture_height;
    void *pending_background; /* Borrowed only during one compositor call. */
    float pending_x, pending_y, pending_w, pending_h;
    uint8_t force_cpu, retain_only, gpu_only, accelerated_frame;
    uint8_t gpu_ui_attempted, gpu_ui_reported, gpu_ui_failure_reported;
} ModernMenuHost;

/* Tracks whether the player last used a controller or the keyboard. */
void modern_menu_host_event(ModernMenuHost *host, const void *sdl_event);
/* Renders and presents one frame; 0 on failure (renderer or flow state). */
int modern_menu_host_present(ModernMenuHost *host, void *sdl_renderer, const ModernMenuView *view);
int modern_menu_host_present_setup(ModernMenuHost *host, void *sdl_renderer,
                                   const ModernMenuSetup *setup);
/* Shared compositor entry for content-driven shell scenes. The callback draws
 * one view; input and camera clocks stay with its owner. */
typedef int (*ModernMenuCanvasDraw)(ModernMenu *menu, UiCanvas *canvas, void *owner);
int modern_menu_host_present_canvas(ModernMenuHost *host, void *sdl_renderer,
                                    ModernMenuCanvasDraw draw, void *owner);
/* A scene client may return a borrowed SDL_Texture background and draw a
 * premultiplied transparent canvas over it. One compositor owns presentation. */
typedef int (*ModernMenuLayerDraw)(ModernMenu *menu, UiCanvas *canvas, void *sdl_renderer,
                                   void **background, void *owner);
int modern_menu_host_present_layer(ModernMenuHost *host, void *sdl_renderer,
                                   ModernMenuLayerDraw draw, void *owner);
/* In-race pause over the frozen race frame, placed where the race presents
 * it (fit matches portable_software_frame_present). subtitle is optional;
 * capture names the screen capture taken once the menu has opened. */
int modern_menu_host_present_pause(ModernMenuHost *host, void *sdl_renderer,
                                   const PortableSoftwareFrame *frame, int fit,
                                   const PortablePauseMenu *pause, const char *subtitle,
                                   const char *capture);
/* Call while not paused so the next pause replays its entrance. Paused races
 * present once per tick, so the pause animates by presented frames. */
void modern_menu_host_pause_closed(ModernMenuHost *host);
int modern_menu_host_present_results(ModernMenuHost *host, void *sdl_renderer,
                                     const PortableSoftwareFrame *frame, int fit,
                                     const ModernMenuResults *results, const char *capture);
int modern_menu_host_present_transition(ModernMenuHost *host, void *sdl_renderer,
                                        const PortableSoftwareFrame *frame, int fit,
                                        const ModernMenuTransition *transition,
                                        const char *capture);
void modern_menu_host_results_closed(ModernMenuHost *host);
const char *modern_menu_host_result_action_label(uint8_t code);
void modern_menu_host_format_time(char *text, size_t capacity, uint32_t centiseconds);
int modern_menu_host_write_ppm(ModernMenuHost *host, const char *path);
void modern_menu_host_content_changed(ModernMenuHost *host);
/* Creates the menu owner when needed; NULL on allocation failure. */
PortableMenuDriverModel *modern_menu_host_driver_model(ModernMenuHost *host);
void modern_menu_host_close(ModernMenuHost *host);

#endif
