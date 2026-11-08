#include "first_run.h"

#include <stdio.h>
#include <string.h>

#include "rom_import_host.h"
#include "asset_pack.h"
#include "content_registry.h"
#include "rom_import.h"

#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#include "startup_pack.h"
#include "loading_screen.h"

#include "modern_menu_host.h"
#include "mobile_lifecycle.h"
#include "mobile_touch.h"
#include "settings.h"
#include "software_frame.h"

typedef struct FirstRunChoice {
    SDL_Mutex *mutex;
    char path[4096];
    int state; /* 0 idle, 1 dialog open, 2 path ready, 3 cancelled, 4 dialog failed */
} FirstRunChoice;

static void SDLCALL dialog_done(void *userdata, const char *const *files, int filter)
{
    FirstRunChoice *choice = userdata;
    (void)filter;
    SDL_LockMutex(choice->mutex);
    if (files == NULL) choice->state = 4;
    else if (files[0] == NULL) choice->state = 3;
    else {
        SDL_strlcpy(choice->path, files[0], sizeof choice->path);
        choice->state = 2;
    }
    SDL_UnlockMutex(choice->mutex);
}

static uint32_t screen_tick(void)
{
    return (uint32_t)(SDL_GetTicks() * 597275u / 10000000u);
}

static int present(ModernMenuHost *host, SDL_Renderer *renderer, const ModernMenuSetup *setup)
{
    if (!modern_menu_host_present_setup(host, renderer, setup)) return 0;
    /* Screen captures wait for the entrance animation to settle. */
    static ModernMenuSetupScreen shown = (ModernMenuSetupScreen)-1;
    static uint32_t since;
    if (setup->screen != shown) {
        shown = setup->screen;
        since = setup->tick;
    }
    if (setup->tick - since < 45u) return 1;
    PortableSoftwareFrame frame;
    static const char *const names[] = {"first-run", "first-run-importing", "first-run-reimport",
                                        "first-run-preferences"};
    return !portable_software_frame_init(&frame, host->pixels, (size_t)host->width, (size_t)host->height) ||
           portable_screen_capture(&frame, names[setup->screen]);
}

/* Imports path over pack_path. A stale pack is replaced only once the new
 * one is complete; the ROM path is remembered for the next reimport. */
static int import_rom(const char *path, const char *pack_path, int stale, char *error, size_t error_size)
{
    char target[4096];
    if (snprintf(target, sizeof target, "%s%s", pack_path, stale ? ".new" : "") >= (int)sizeof target) {
        SDL_strlcpy(error, "pack path too long", error_size);
        return 0;
    }
    if (stale) SDL_RemovePath(target); /* a leftover from an interrupted reimport */
    if (!portable_rom_import_file(path, target, error, error_size)) return 0;
    if (stale && !SDL_RenamePath(target, pack_path)) {
        snprintf(error, error_size, "cannot replace %.300s: %.150s", pack_path, SDL_GetError());
        SDL_RemovePath(target);
        return 0;
    }
    portable_store_rom_path(path);
    return 1;
}

typedef struct
{
    char path[4096];
    const char *pack_path;
    int stale, ok;
    char error[512];
    SDL_AtomicInt done;
} ImportJob;

static int SDLCALL import_worker(void *data)
{
    ImportJob *job = data;
    job->ok = import_rom(job->path, job->pack_path, job->stale, job->error, sizeof job->error);
    SDL_SetAtomicInt(&job->done, 1);
    return 0;
}

/* A short page of basics after the first import, saved to settings.json. */
enum { SETUP_DRIVING, SETUP_MENUS, SETUP_DISPLAY, SETUP_ROWS };

static void preferences_rows(ModernMenuSetup *setup, const PortableSettings *settings)
{
#ifdef SDL_PLATFORM_ANDROID
    setup->row_count = SETUP_DISPLAY; /* Always fullscreen. */
#else
    setup->row_count = SETUP_ROWS;
#endif
    setup->rows[SETUP_DISPLAY] = (ModernMenuOptionRow){"Display", {0},
        "Play in a window you can resize, or fill the whole screen. Options can switch it later.", 1u};
    snprintf(setup->rows[SETUP_DISPLAY].value, sizeof setup->rows[0].value, "%s",
             settings->fullscreen ? "Fullscreen" : "Window");
    setup->rows[SETUP_DRIVING] = (ModernMenuOptionRow){
        "Drift Extras",
        {0},
        settings->driving_profile == 2u
            ? "2D handling stays original; modern physics is on 3D courses. Three drift tiers: charge, then release. Hop in air; land to boost. Separate records."
        : settings->driving_profile
            ? "2D handling stays original; modern physics is on 3D courses. Hold Hop + steer + accelerate; three tiers, release to boost. Separate records."
            : "2D handling stays original; modern physics is on 3D courses. Original drift reward. Extras use separate records.",
        1u};
    snprintf(setup->rows[SETUP_DRIVING].value, sizeof setup->rows[0].value, "%s",
             settings->driving_profile == 2u ? "Drift Tiers + Air Boost"
             : settings->driving_profile     ? "Drift Tiers"
                                             : "Off");
    setup->rows[SETUP_MENUS] = (ModernMenuOptionRow){
        "Menus",
        {0},
        "Modern menus fill the screen in this style; Classic shows the original 240x160 menus.",
        1u};
    snprintf(setup->rows[SETUP_MENUS].value, sizeof setup->rows[0].value, "%s",
             settings->classic_menus ? "Classic" : "Modern");
}

static void preferences_change(PortableSettings *settings, unsigned row, int delta)
{
    if (row == SETUP_DISPLAY) settings->fullscreen = !settings->fullscreen;
    else if (row == SETUP_DRIVING) settings->driving_profile = (uint8_t)((settings->driving_profile + 3 + delta) % 3);
    else if (row == SETUP_MENUS) settings->classic_menus = !settings->classic_menus;
}

int portable_first_run(const char *rom_path, int hidden, char *pack_path, size_t capacity)
{
    char *directory = portable_data_directory();
    if (directory == NULL) {
        fprintf(stderr, "first run: no data directory: %s\n", SDL_GetError());
        return -1;
    }
    const int length = snprintf(pack_path, capacity, "%sgame.pack", directory);
    char settings_path[4096];
    const int settings_length = snprintf(settings_path, sizeof settings_path, "%ssettings.json", directory);
    if (settings_length <= 0 || (size_t)settings_length >= sizeof settings_path) settings_path[0] = '\0';
    SDL_free(directory);
    if (length <= 0 || (size_t)length >= capacity) return -1;
    const int keep = rom_path != NULL && SDL_strcmp(rom_path, "keep") == 0;
    char stored[4096] = {0};
    if (rom_path != NULL && SDL_strcmp(rom_path, "-") == 0) {
        if (!portable_stored_rom_path(stored, sizeof stored)) {
            fputs("first run: no remembered ROM path\n", stderr);
            return -1;
        }
        rom_path = stored;
    } else if (keep) {
        rom_path = NULL;
    }
    int stale = 0;
    uint32_t old_version = 0;
    if (SDL_GetPathInfo(pack_path, NULL)) {
        int readable = 0;
        readable = portable_startup_pack_probe(pack_path, hidden, &old_version);
        if (readable != 1) { portable_loading_close(); return readable < 0 ? 0 : -1; }
        if (old_version >= MKSC_ROM_IMPORT_VERSION) return 1;
        printf("first run: %s was converted by importer version %u; this build imports version %u\n",
               pack_path, (unsigned)old_version, (unsigned)MKSC_ROM_IMPORT_VERSION);
        fflush(stdout);
        if (keep) {
            puts("first run: keeping the old pack");
            return 1;
        }
        portable_startup_pack_discard();
        portable_loading_close();
        stale = 1;
        /* The screen offers the given ROM, else the remembered one. */
        if (rom_path != NULL && rom_path != stored) SDL_strlcpy(stored, rom_path, sizeof stored);
        else if (rom_path == NULL) portable_stored_rom_path(stored, sizeof stored);
        if (!hidden) rom_path = NULL;
    } else if (keep) {
        fputs("first run: no pack to keep\n", stderr);
        return -1;
    }

    portable_mobile_lifecycle_hint();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        fprintf(stderr, "first run: SDL init failed: %s\n", SDL_GetError());
        return -1;
    }
    portable_mobile_lifecycle_start();
    SDL_Window *window =
        SDL_CreateWindow("MKSC Neo", 1280, 720,
                         hidden ? SDL_WINDOW_HIDDEN
                                : SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                      PORTABLE_WINDOW_ALWAYS_FULLSCREEN);
    SDL_Renderer *renderer = NULL;
    if (window != NULL && !hidden)
    {
        renderer = SDL_CreateGPURenderer(NULL, window);
        if (renderer == NULL)
        {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            renderer = SDL_CreateRenderer(window, "opengles2");
            if (renderer == NULL)
            {
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
                renderer = SDL_CreateRenderer(window, "opengles2");
            }
        }
    }
    if (window != NULL && renderer == NULL)
        renderer = SDL_CreateRenderer(window, hidden ? "software" : NULL);
    FirstRunChoice choice = {SDL_CreateMutex(), {0}, 0};
    ModernMenuHost host = {0};
    if (hidden)
    {
        host.fixed_width = 1280;
        host.fixed_height = 720;
    }
    if (renderer == NULL || choice.mutex == NULL)
    {
        fprintf(stderr, "first run: SDL video failed: %s\n", SDL_GetError());
        if (choice.mutex)
            SDL_DestroyMutex(choice.mutex);
        portable_mobile_lifecycle_stop();
        portable_mobile_touch_close();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
        return -1;
    }
    if (!hidden) (void)SDL_SetRenderVSync(renderer, 1);
    if (rom_path != NULL) {
        SDL_strlcpy(choice.path, rom_path, sizeof choice.path);
        choice.state = 2;
    }
    static const SDL_DialogFileFilter filters[] = {{"Game Boy Advance ROM or ZIP", "gba;zip"}, {"All files", "*"}};
    char status[512] = {0}, detail[160];
    snprintf(detail, sizeof detail,
             "Your game data came from an older converter (version %u; this build makes version %u). "
             "Convert your ROM again to get everything new.",
             (unsigned)old_version, (unsigned)MKSC_ROM_IMPORT_VERSION);
    ModernMenuSetup setup = {.screen = stale ? MODERN_SETUP_REIMPORT : MODERN_SETUP_IMPORT, .detail = detail,
                             .rom_path = stored};
    ImportJob job;
    SDL_Thread *worker = NULL;
    int importing = 0;
    PortableSettings settings = {0};
    char settings_error[PORTABLE_SETTINGS_ERROR_CAPACITY];
    const char *preferences_aid = hidden ? SDL_getenv("MKSC_AID_FIRST_RUN_PREFERENCES") : NULL;
    const int aid_preferences = preferences_aid != NULL &&
        (strcmp(preferences_aid, "1") == 0 || strcmp(preferences_aid, "save-failure") == 0 ||
         strcmp(preferences_aid, "air") == 0);
    const int aid_save_failure = aid_preferences && strcmp(preferences_aid, "save-failure") == 0;
    const int aid_air = aid_preferences && strcmp(preferences_aid, "air") == 0;
    unsigned preferences_step = 0u;
    int result = 0, running = 1;
    while (running) {
        /* Exercise the same event handler as keyboard users, after import has
         * reached preferences. Default hidden runs still keep their old path. */
        if (aid_preferences && setup.screen == MODERN_SETUP_PREFERENCES) {
            static const SDL_Keycode keys[] = {SDLK_RIGHT, SDLK_DOWN, SDLK_RIGHT,
                                               SDLK_DOWN, SDLK_DOWN, SDLK_RETURN};
            static const SDL_Keycode air_keys[] = {SDLK_RIGHT, SDLK_RIGHT, SDLK_DOWN, SDLK_RIGHT,
                                                   SDLK_DOWN, SDLK_DOWN, SDLK_RETURN};
            const SDL_Keycode *sequence = aid_air ? air_keys : keys;
            const size_t sequence_count = aid_air ? sizeof air_keys / sizeof air_keys[0]
                                                 : sizeof keys / sizeof keys[0];
            if (aid_air && preferences_step == 2u) {
                /* Capture the selected full Air value before the next ordinary
                 * Down event moves to Menus. Both Right events already ran. */
                if (settings.driving_profile != 2u) { result = -1; break; }
                setup.tick = screen_tick();
                if (!present(&host, renderer, &setup)) { result = -1; break; }
                setup.tick += 60u;
                if (!present(&host, renderer, &setup)) { result = -1; break; }
            }
            if (preferences_step < sequence_count) {
                const SDL_Keycode key = sequence[preferences_step++];
                SDL_Event press = {0}, release = {0};
                press.type = SDL_EVENT_KEY_DOWN;
                release.type = SDL_EVENT_KEY_UP;
                press.key.key = release.key.key = key;
                if (!SDL_PushEvent(&press) || !SDL_PushEvent(&release)) {
                    result = -1;
                    break;
                }
            } else if (aid_save_failure && setup.status_error) {
                /* The failed save must leave both the page and edited values
                 * intact, without publishing a partial settings file. */
                PortableSettings saved;
                if (settings.driving_profile != 1u || settings.classic_menus != 1u ||
                    portable_settings_load(settings_path, &saved, settings_error) != 0) {
                    result = -1;
                    break;
                }
                /* Hidden acceptance must also render the readable, settled
                 * error page before ordinary Quit ends this failed attempt. */
                setup.tick = screen_tick();
                if (!present(&host, renderer, &setup)) { result = -1; break; }
                setup.tick += 60u;
                if (!present(&host, renderer, &setup)) { result = -1; break; }
                puts("first run preferences: PASS save failure retained page and choices");
                SDL_Event quit = {0};
                quit.type = SDL_EVENT_QUIT;
                if (!SDL_PushEvent(&quit)) { result = -1; break; }
            }
        }
        const unsigned lifecycle = portable_mobile_lifecycle_take();
        if (lifecycle & PORTABLE_MOBILE_BACKGROUND) { portable_mobile_touch_clear(); SDL_ResetKeyboard(); }
        if ((lifecycle & PORTABLE_MOBILE_TERMINATING) && !importing) running = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND || event.type == SDL_EVENT_DID_ENTER_BACKGROUND ||
                event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { portable_mobile_touch_clear(); SDL_ResetKeyboard(); }
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) setup.gamepad = 1u;
            else if (event.type == SDL_EVENT_KEY_DOWN) setup.gamepad = 0u;
            if (event.type == SDL_EVENT_QUIT) {
                if (!importing) running = 0;
                continue;
            }
            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                SDL_OpenGamepad(event.gdevice.which);
                continue;
            }
            if (setup.screen == MODERN_SETUP_IMPORTING) continue;
            if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != NULL) {
                if (setup.screen == MODERN_SETUP_PREFERENCES) continue;
                SDL_LockMutex(choice.mutex);
                if (choice.state != 1) {
                    SDL_strlcpy(choice.path, event.drop.data, sizeof choice.path);
                    choice.state = 2;
                }
                SDL_UnlockMutex(choice.mutex);
                continue;
            }
            const uint16_t touch = portable_mobile_touch_event(&event, renderer);
            const int key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat ? (int)event.key.key : 0;
            const int button = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? event.gbutton.button : -1;
            const int back = key == SDLK_ESCAPE || portable_mobile_back(&event) || (touch & PORTABLE_FRONTEND_BUTTON_B) ||
                             button == SDL_GAMEPAD_BUTTON_EAST || button == SDL_GAMEPAD_BUTTON_BACK;
            /* A tap on the drop zone also opens the ROM picker (touch screens). */
            const int tap = event.type == SDL_EVENT_FINGER_UP && setup.screen == MODERN_SETUP_IMPORT;
            const int confirm = key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE || tap ||
                                button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_START ||
                                (touch & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START));
            if (setup.screen == MODERN_SETUP_PREFERENCES) {
                const int up = key == SDLK_UP || button == SDL_GAMEPAD_BUTTON_DPAD_UP ||
                                 (touch & PORTABLE_FRONTEND_BUTTON_UP);
                const int down = key == SDLK_DOWN || button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ||
                                 (touch & PORTABLE_FRONTEND_BUTTON_DOWN);
                const int left = key == SDLK_LEFT || button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                                 (touch & PORTABLE_FRONTEND_BUTTON_LEFT);
                const int right = key == SDLK_RIGHT || button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ||
                                 (touch & PORTABLE_FRONTEND_BUTTON_RIGHT);
                const int start_row = setup.cursor >= setup.row_count;
                if (up && setup.cursor > 0u) --setup.cursor;
                else if (down && setup.cursor < setup.row_count) ++setup.cursor;
                else if ((left || right || (confirm && !start_row)) && !start_row) {
                    if (confirm) ++setup.cursor;
                    else preferences_change(&settings, setup.cursor, left ? -1 : 1);
                    preferences_rows(&setup, &settings);
                    portable_mobile_touch_configure(settings.touch_controls);
                } else if ((confirm && start_row) || back) {
                    if (settings_path[0] == '\0' || !portable_settings_save(settings_path, &settings)) {
                        fprintf(stderr, "first run: could not save %s\n", settings_path);
                        snprintf(status, sizeof status,
                                 "Could not save preferences. Check that the settings folder is writable, then try Start again.");
                        setup.status = status;
                        setup.status_error = 1u;
                        continue;
                    }
                    if (aid_preferences) puts("first run preferences: PASS saved ordinary input choices");
                    result = 1;
                    running = 0;
                }
                continue;
            }
            if (back) {
                /* Fresh start: quit. Stale pack: keep playing it. */
                if (stale) {
                    puts("first run: keeping the old pack");
                    result = 1;
                }
                running = 0;
            } else if (confirm) {
                SDL_LockMutex(choice.mutex);
                const int idle = choice.state != 1 && choice.state != 2;
                const int remembered = idle && stale && stored[0] != '\0';
                if (remembered) {
                    SDL_strlcpy(choice.path, stored, sizeof choice.path);
                    choice.state = 2;
                } else if (idle) {
                    choice.state = 1;
                }
                SDL_UnlockMutex(choice.mutex);
                if (idle && !remembered)
                    SDL_ShowOpenFileDialog(dialog_done, &choice, window, filters, 2, NULL, false);
            }
        }
        if (importing && SDL_GetAtomicInt(&job.done)) {
            if (worker != NULL) SDL_WaitThread(worker, NULL);
            worker = NULL;
            importing = 0;
            if (job.ok) {
                /* After a first import, offer the basics once; reimports and
                 * automated runs go straight to the game. */
                const int loaded = settings_path[0] != '\0'
                                       ? portable_settings_load(settings_path, &settings, settings_error) : -1;
                if (!stale && loaded == 0) {
                    setup.screen = MODERN_SETUP_PREFERENCES;
                    setup.cursor = 0u;
                    setup.status = NULL;
                    preferences_rows(&setup, &settings);
                    portable_mobile_touch_configure(settings.touch_controls);
                }
                if (setup.screen == MODERN_SETUP_PREFERENCES && hidden && !aid_preferences) {
                    /* Automated runs show the page once (for screen
                     * captures) and keep the defaults. */
                    setup.tick = screen_tick();
                    (void)present(&host, renderer, &setup);
                    setup.tick += 60u;
                    (void)present(&host, renderer, &setup);
                }
                if (setup.screen != MODERN_SETUP_PREFERENCES || (hidden && !aid_preferences)) {
                    result = 1;
                    running = 0;
                }
            } else {
                fprintf(stderr, "first run: %s\n", job.error);
                SDL_strlcpy(status, job.error, sizeof status);
                setup.screen = stale ? MODERN_SETUP_REIMPORT : MODERN_SETUP_IMPORT;
                setup.status = status;
                setup.status_error = 1u;
                stored[0] = '\0'; /* the remembered ROM failed: ask for the file */
                if (hidden) {
                    result = -1;
                    running = 0;
                }
            }
        }
        SDL_LockMutex(choice.mutex);
        const int state = choice.state;
        char path[4096];
        SDL_strlcpy(path, choice.path, sizeof path);
        if (state >= 2) choice.state = 0;
        SDL_UnlockMutex(choice.mutex);
        if (state == 4) {
            snprintf(status, sizeof status, "No file dialog is available here. Drop the ROM onto this window instead.");
            setup.status = status;
            setup.status_error = 1u;
            fprintf(stderr, "first run: file dialog unavailable: %s\n", SDL_GetError());
        } else if (state == 2 && !importing) {
            if (stale) printf("first run: importing again from %s\n", path);
            memset(&job, 0, sizeof job);
            SDL_strlcpy(job.path, path, sizeof job.path);
            job.pack_path = pack_path;
            job.stale = stale;
            importing = 1;
            worker = SDL_CreateThread(import_worker, "rom import", &job);
            if (worker == NULL) import_worker(&job); /* No threads: import here. */
            setup.screen = MODERN_SETUP_IMPORTING;
            setup.status = NULL;
            setup.status_error = 0u;
        }
        if (!running) break;
        setup.tick = screen_tick();
        if (!present(&host, renderer, &setup)) {
            fprintf(stderr, "first run: present failed: %s\n", SDL_GetError());
            result = -1;
            running = 0;
        }
        if (running && !hidden) SDL_Delay(4);
    }
    fflush(stdout);
    if (worker != NULL) SDL_WaitThread(worker, NULL);
    /* A dialog left open owns a pointer to choice: wait for it to finish. */
    for (;;) {
        SDL_LockMutex(choice.mutex);
        const int open = choice.state == 1;
        SDL_UnlockMutex(choice.mutex);
        if (!open) break;
        SDL_PumpEvents();
        SDL_Delay(16);
    }
    SDL_DestroyMutex(choice.mutex);
    modern_menu_host_close(&host);
    portable_mobile_lifecycle_stop();
    portable_mobile_touch_close();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    return result;
}

#else

int portable_first_run(const char *rom_path, int hidden, char *pack_path, size_t capacity)
{
    (void)rom_path;
    (void)hidden;
    (void)pack_path;
    (void)capacity;
    fputs("the first-run screen requires the SDL build; use --import ROM PACK\n", stderr);
    return -1;
}

#endif
