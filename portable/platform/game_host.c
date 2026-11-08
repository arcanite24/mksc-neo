#include "startup_trace.h"
#include "loading_screen.h"
#include "course_editor_host.h"
#include "menu_names.h"
#include "rom_import_host.h"
#include "ui_background_host.h"
#include "mod_manager.h"
#include "game_library.h"
#include "mod_install.h"
#include "mod_store_job.h"
#include "driving_profile.h"
#include "presentation_pack.h"
#include "file_dialog.h"
#include "game_import_host.h"
#include "source_path.h"
#include "mod_menu_renderer.h"
#include "mod_loader.h"
#include "mesh_cup_menu.h"
#include "game_host_internal.h"
#include "graphics_options.h"
#include "time_trial_records.h"
#include "gp_progress_renderer.h"
#include "local_players.h"
#include "menu_font.h"
#include "menu_chrome_renderer.h"
#include "title_renderer.h"
#include "frontend_image_renderer.h"
#include "frontend_motion_asset.h"
#include "replay_file.h"
#include "modern_menu_host.h"
#include "course_3d_host.h"
#include "mesh_time_trial_pilot.h"
#include "input_script.h"
#include "mesh_battle_pilot.h"
#include "controller_input.h"
#include "driver_input.h"
#include "race_interpolation.h"
#include <inttypes.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef PORTABLE_ENABLE_SDL3
#include "mobile_lifecycle.h"
#include "mobile_touch.h"

static void game_shell_idle(void)
{
#ifndef __EMSCRIPTEN__
    SDL_Delay(1);
#endif
}

/* Native options are a shell overlay: they do not advance game state or
 * append original frontend events while the player edits preferences. */
static const PortableSetting option_fields[] = {PORTABLE_SETTING_WINDOW_SCALE,
                                                PORTABLE_SETTING_FULLSCREEN,
                                                PORTABLE_SETTING_VSYNC,
                                                PORTABLE_SETTING_FILTER,
                                                PORTABLE_SETTING_VOLUME,
                                                PORTABLE_SETTING_DEADZONE,
                                                PORTABLE_SETTING_SHORT_TRANSITIONS,
                                                PORTABLE_SETTING_INTERPOLATE,
                                                PORTABLE_SETTING_RENDER_FPS,
                                                PORTABLE_SETTING_INTERNAL_SCALE,
                                                PORTABLE_SETTING_FIT_SCALING,
                                                PORTABLE_SETTING_ASPECT_WIDTH,
                                                PORTABLE_SETTING_RUMBLE,
                                                PORTABLE_SETTING_INPUT_DISPLAY,
                                                PORTABLE_SETTING_MIRROR,
                                                PORTABLE_SETTING_FIX_DOPPLER_WRAP,
                                                PORTABLE_SETTING_TOUCH_CONTROLS};
enum
{
    OPTION_ROWS = sizeof option_fields / sizeof option_fields[0]
};
static void render_settings(const PortableAssetPackView *pack, PortableSoftwareFrame *frame,
                            const PortableSettings *settings, unsigned cursor, const char *notice)
{
    static const char *const labels[OPTION_ROWS] = {
        "WINDOW SCALE",  "FULLSCREEN",     "VSYNC",           "FILTER",
        "MASTER VOLUME", "STICK DEADZONE", "FAST MENUS",      "INTERPOLATION",
        "RENDER FPS",    "INTERNAL SCALE", "DISPLAY SCALING", "ASPECT RATIO",
        "RUMBLE",        "INPUT DISPLAY",  "MIRROR MODE",     "FIX DOPPLER BUG", "TOUCH CONTROLS"};
    const unsigned first = cursor >= PORTABLE_OPTION_PAGE_ROWS ? cursor - PORTABLE_OPTION_PAGE_ROWS + 1u : 0u;
    portable_option_page(pack, frame, "OPTIONS", first, PORTABLE_OPTION_PAGE_ROWS, OPTION_ROWS);
    for (unsigned line = 0u; line < PORTABLE_OPTION_PAGE_ROWS; ++line)
    {
        const unsigned row = first + line;
        const PortableSetting field = option_fields[row];
        char value[24];
        portable_option_value_text(settings, field, value);
        portable_option_row(pack, frame, line, row == cursor, labels[row], value);
    }
    portable_menu_text_wrapped(pack, frame, 12, PORTABLE_OPTION_FOOTER_Y, 27u, PORTABLE_OPTION_ROW_STEP, 2u,
                               notice != NULL ? notice
                               : cursor == PORTABLE_SETTING_DEADZONE
                                   ? "A/ENTER EDIT BINDINGS - B/ESC BACK"
                                   : "B/ESC BACK - CHANGES SAVE AUTOMATICALLY");
}

static void render_enhancements(const PortableAssetPackView *pack,PortableSoftwareFrame *frame,
    const PortableSettings *settings,unsigned cursor,const char *notice)
{
    portable_option_page(pack,frame,"ENHANCEMENTS",0u,5u,5u);
    portable_option_row(pack,frame,0u,cursor==0u,"WIDE UI BACKGROUNDS",settings->wide_ui_backgrounds?"ON":"OFF");
    portable_option_row(pack, frame, 1u, cursor == 1u, "DRIFT EXTRAS",
               settings->driving_profile == 2u ? "TIERS+AIR"
               : settings->driving_profile     ? "TIERS"
                                               : "OFF");
    portable_option_row(pack, frame, 2u, cursor == 2u, "ARTWORK DETAIL",
               settings->artwork_lod == 2u ? "MAX"
               : settings->artwork_lod     ? "HIGH"
                                           : "ORIGINAL");
    portable_option_row(pack, frame, 3u, cursor == 3u, "RACE RENDERER",
               settings->race_renderer ? "3D" : "ORIGINAL");
    portable_option_row(pack, frame, 4u, cursor == 4u, "IMMERSIVE CAMERA",
               settings->immersive_camera ? "ON" : "OFF");
    if (cursor == 4u)
        portable_menu_text_wrapped(pack, frame, 12, 100, 27u, 14, 1u,
                                   "CLOSER CHASE ON 3D COURSES.");
    if (cursor == 3u)
    {
        portable_menu_text_wrapped(pack, frame, 12, 100, 27u, 14, 1u,
                                   "3D; ORIGINAL IF UNSUPPORTED.");
    }
    if (cursor == 1u)
        portable_menu_text_wrapped(pack, frame, 12, 100, 27u, 14, 1u,
                                   "ORIGINAL 2D; MODERN ON 3D.");
    if (cursor == 2u)
        portable_menu_text_wrapped(pack, frame, 12, 100, 27u, 14, 1u,
                                   "DETAIL OF AVAILABLE ART.");
    portable_menu_text_wrapped(
        pack, frame, 12, PORTABLE_OPTION_FOOTER_Y, 27u, PORTABLE_OPTION_ROW_STEP, 2u,
        notice         ? notice
        : cursor == 1u ? (settings->driving_profile == 2u
                              ? "HOP IN AIR; LAND TO BOOST. LEFT/RIGHT CHANGE; B BACK."
                          : settings->driving_profile
                              ? "3 TIERS; RELEASE TO BOOST. LEFT/RIGHT CHANGE; B BACK."
                              : "OFF: ORIGINAL DRIFT REWARD. LEFT/RIGHT CHANGE; B BACK.")
        : cursor != 0u ? "LEFT/RIGHT CHANGE - B BACK"
                       : "LEFT/RIGHT ENABLE - A IMPORT TITLE ART - B BACK");
}

static const char *save_setting_edit(const char *path, PortableSettings *saved,
                                     PortableSetting field, uint32_t value)
{
    (void)portable_setting_set(saved, field, value);
    return path[0] != '\0' && portable_settings_save(path, saved)
               ? "SAVED"
               : "SAVE FAILED - CHANGE ACTIVE THIS SESSION";
}

/* Modern Enhancements rows after Race Renderer: a preset, then each switch. */
enum { EFFECT_PRESET_ROW = 5u, EFFECT_ROWS = 6u, IMMERSIVE_CAMERA_ROW = EFFECT_PRESET_ROW + EFFECT_ROWS + 1u,
       HD2D_PRESET_ROW = IMMERSIVE_CAMERA_ROW + 1u, HD2D_ROWS = 7u };
_Static_assert(HD2D_PRESET_ROW + HD2D_ROWS < MODERN_MENU_OPTION_ROWS, "Options capacity");
static const PortableSetting effect_fields[EFFECT_ROWS] = {
    PORTABLE_SETTING_SMOOTH_3D,    PORTABLE_SETTING_DYNAMIC_CAMERA, PORTABLE_SETTING_CAMERA_SHAKE,
    PORTABLE_SETTING_DRIFT_SPARKS, PORTABLE_SETTING_BOOST_EFFECTS,  PORTABLE_SETTING_KART_TRAILS};

/* 0 Low Power (all off), 1 Full (all on), 2 Custom. */
static unsigned effects_preset(const PortableSettings *settings)
{
    unsigned on = 0u;
    for (unsigned i = 0u; i < EFFECT_ROWS; ++i)
        on += portable_setting_get(settings, effect_fields[i]) != 0u;
    return on == EFFECT_ROWS ? 1u : on ? 2u : 0u;
}

/* Binding rows: player 1 keys, player 2 keys, then gamepad buttons. */
enum
{
    BINDING_ROWS = 20u,
    BINDING_KEY_ROWS = 16u
};
static PortableSetting binding_field(unsigned row)
{
    return row < 8u ? (PortableSetting)(PORTABLE_SETTING_KEY_ACCELERATE + row)
           : row < BINDING_KEY_ROWS
               ? (PortableSetting)(PORTABLE_SETTING_KEY2_ACCELERATE + row - 8u)
               : (PortableSetting)(PORTABLE_SETTING_PAD_ACCELERATE + row - BINDING_KEY_ROWS);
}
/* A binding's key or button name in capitals. */
static void binding_name(const PortableSettings *settings, unsigned row, char name[16])
{
    const uint32_t value = portable_setting_get(settings, binding_field(row));
    const char *source = row < BINDING_KEY_ROWS
                             ? SDL_GetScancodeName((SDL_Scancode)value)
                             : SDL_GetGamepadStringForButton((SDL_GamepadButton)value);
    if (source == NULL || source[0] == '\0')
        snprintf(name, 16u, "CODE %u", value);
    else
    {
        size_t i = 0u;
        for (; source[i] != '\0' && i + 1u < 16u; ++i)
        {
            char c = source[i];
            name[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c == '_' ? ' ' : c;
        }
        name[i] = '\0';
    }
}

/* One group per page: player 1 keys, player 2 keys, then pad buttons. */
static void render_bindings(const PortableAssetPackView *pack, PortableSoftwareFrame *frame,
                            const PortableSettings *settings, unsigned cursor, int capturing,
                            const char *notice)
{
    static const char *const actions[] = {"ACCELERATE", "BRAKE", "LEFT",   "RIGHT",
                                          "HOP",        "ITEM",  "AIM UP", "AIM DOWN"};
    static const uint8_t pad_actions[4] = {0u, 1u, 4u, 5u};
    const unsigned group = cursor < 8u ? 0u : cursor < BINDING_KEY_ROWS ? 8u : BINDING_KEY_ROWS;
    const unsigned count = group < BINDING_KEY_ROWS ? 8u : BINDING_ROWS - BINDING_KEY_ROWS;
    const unsigned offset = cursor - group;
    const unsigned first = offset >= PORTABLE_OPTION_PAGE_ROWS ? offset - PORTABLE_OPTION_PAGE_ROWS + 1u : 0u;
    portable_option_page(pack, frame,
                group == 0u   ? "PLAYER 1 KEYS"
                : group == 8u ? "PLAYER 2 KEYS"
                              : "GAMEPAD BUTTONS",
                first, PORTABLE_OPTION_PAGE_ROWS, count);
    for (unsigned line = 0u; line < PORTABLE_OPTION_PAGE_ROWS && first + line < count; ++line)
    {
        const unsigned row = group + first + line;
        char name[16];
        binding_name(settings, row, name);
        portable_option_row(pack, frame, line, row == cursor,
                   actions[row < BINDING_KEY_ROWS ? row % 8u : pad_actions[row - BINDING_KEY_ROWS]],
                   name);
    }
    portable_menu_text_wrapped(pack, frame, 12, PORTABLE_OPTION_FOOTER_Y, 27u, PORTABLE_OPTION_ROW_STEP, 2u,
                               notice != NULL ? notice
                               : capturing
                                   ? (cursor < BINDING_KEY_ROWS ? "PRESS A KEY - ESC CANCEL"
                                                                : "PRESS PAD BUTTON - START CANCEL")
                                   : "B/ESC BACK - SELECT/TAB RESET THIS BINDING");
}

/* The modern menus animate their own entrances, so they skip the original
 * fades; the original menus keep them unless Fast Menus is on. */
static uint8_t menu_short_transitions(const PortableSettings *settings)
{
    return (uint8_t)(settings->short_transitions || !settings->classic_menus);
}

static int apply_settings_field(SDL_Window *window, SDL_Renderer *renderer, SDL_Texture *texture,
                                SDL_AudioStream *stream, PortableFrontendFlow *flow,
                                const PortableSettings *settings, PortableSetting field,
                                int recording)
{
    switch (field)
    {
    case PORTABLE_SETTING_INTERNAL_SCALE:
    case PORTABLE_SETTING_FIT_SCALING:
    case PORTABLE_SETTING_INTERPOLATE:
    case PORTABLE_SETTING_RENDER_FPS:
    case PORTABLE_SETTING_MIRROR:
        portable_mod_identity_set_mirror(settings->mirror);
        return 1;
    case PORTABLE_SETTING_TOUCH_CONTROLS:
        portable_mobile_touch_configure(settings->touch_controls);
        return 1;
    case PORTABLE_SETTING_RUMBLE:
    case PORTABLE_SETTING_INPUT_DISPLAY:
    case PORTABLE_SETTING_FIX_DOPPLER_WRAP:
    case PORTABLE_SETTING_WIDE_UI_BACKGROUNDS:
    case PORTABLE_SETTING_ARTWORK_LOD:
    case PORTABLE_SETTING_RACE_RENDERER:
        return 1; /* Next race. */
    case PORTABLE_SETTING_ASPECT_WIDTH:
        if (settings->aspect_width == 0u)
            return 1;
        return SDL_SetWindowSize(window, (int)settings->aspect_width * (int)settings->window_scale,
                                 160 * (int)settings->window_scale);
    case PORTABLE_SETTING_WINDOW_SCALE:
        return SDL_SetWindowSize(
            window,
            (int)portable_software_frame_window_width(renderer, settings->aspect_width) *
                (int)settings->window_scale,
            160 * (int)settings->window_scale);
    case PORTABLE_SETTING_FULLSCREEN:
        return SDL_SetWindowFullscreen(window, settings->fullscreen != 0u || PORTABLE_WINDOW_ALWAYS_FULLSCREEN != 0u);
    case PORTABLE_SETTING_VSYNC:
        return SDL_SetRenderVSync(renderer, settings->vsync);
    case PORTABLE_SETTING_FILTER:
        return SDL_SetTextureScaleMode(texture, settings->linear_filter ? SDL_SCALEMODE_LINEAR
                                                                        : SDL_SCALEMODE_NEAREST);
    case PORTABLE_SETTING_VOLUME:
        return SDL_SetAudioStreamGain(stream, (float)settings->master_volume / 100.0f);
    case PORTABLE_SETTING_DEADZONE:
        return portable_sdl_gamepad_set_deadzone(settings->steering_deadzone);
    case PORTABLE_SETTING_SHORT_TRANSITIONS:
    case PORTABLE_SETTING_CLASSIC_MENUS:
        flow->short_transitions = recording ? 0u : menu_short_transitions(settings);
        return 1;
    default:
        return 0;
    }
}

/* Race audio tables are keyed by stock driver; roster drivers use their
 * voice driver's. Smokes log the substitution. */
static uint32_t voice_driver(const PortableAssetPackView *pack, uint32_t character_id, int log)
{
    PortableCharacterContent voice;
    if (portable_content_registry_voice_driver(pack, character_id, &voice) != PORTABLE_CONTENT_OK)
        return character_id;
    if (log && voice.id != character_id)
        printf("race voice: character=%u voice=%u\n", character_id, voice.id);
    return voice.id;
}

static int load_menu_records(const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
                             PortableTimeTrialRecords *records, const char *save_directory)
{
    if (records->course_id == flow->selected_course)
        return 1;
    char *directory =
        save_directory != NULL ? SDL_strdup(save_directory) : portable_data_directory();
    uint32_t driver = 0u;
    for (size_t slot = 0u; slot < flow->config.driver_count; ++slot)
        if (flow->config.drivers[slot].available && flow->config.drivers[slot].content_id != 0u)
        {
            driver = flow->config.drivers[slot].content_id;
            break;
        }
    const uint8_t erase_failed = records->course_id == 0u ? records->erase_failed : 0u;
    const char *notice = records->notice;
    const int loaded =
        directory != NULL &&
        portable_time_trial_records_load(pack, flow->selected_course, driver, directory, records);
    SDL_free(directory);
    records->erase_failed = erase_failed;
    records->notice = notice;
    return loaded;
}

static int gp_progress_path_with_suffix(char path[4096],const char *suffix)
{
    const char *override = getenv("MKSC_GP_PROGRESS");
    if (override != NULL && override[0] != '\0')
    {
        const int length =
            snprintf(path, 4096, "%s%s", override, suffix);
        return length > 0 && length < 4096;
    }
    char *directory = portable_data_directory();
    if (directory == NULL)
        return 0;
    const int length = snprintf(path, 4096, "%sgp-progress%s.bin", directory,
                                suffix);
    SDL_free(directory);
    return length > 0 && length < 4096;
}

static int gp_progress_path(char path[4096])
{
    return gp_progress_path_with_suffix(path,portable_mod_identity_progress_suffix());
}

static int refresh_gp_cup_access_with_suffix(PortableFrontendFlow *flow, PortableGpProgress *out,
                                           const char *suffix)
{
    char path[4096];
    PortableGpProgress progress;
    if (!gp_progress_path_with_suffix(path,suffix))
        return 0;
    const PortableGpProgressStatus status = portable_gp_progress_load(path, &progress);
    if (status == PORTABLE_GP_PROGRESS_NOT_FOUND)
        portable_gp_progress_init(&progress);
    else if (status != PORTABLE_GP_PROGRESS_OK)
    {
        fprintf(stderr, "GP progress load failed: %s (%s)\n", path,
                portable_gp_progress_status_name(status));
        return 0;
    }
    uint8_t masks[3];
    for (uint8_t speed = 0u; speed < 3u; ++speed)
        masks[speed] = portable_gp_progress_cup_access(&progress, speed);
    if (!portable_frontend_flow_set_cup_access(flow, masks))
        return 0;
    for (uint8_t speed = 0u; speed < 3u; ++speed)
        masks[speed] = portable_gp_progress_extra_cup_access(&progress, speed);
    if (!portable_frontend_flow_set_extra_cup_access(flow, masks))
        return 0;
    flow->title_variant = portable_gp_progress_title_variant(&progress);
    *out = progress;
    return 1;
}

static int refresh_gp_cup_access(PortableFrontendFlow *flow, PortableGpProgress *out)
{
    return refresh_gp_cup_access_with_suffix(flow,out,portable_mod_identity_progress_suffix());
}

static int persist_cup_ceremony(PortableGpCeremonySession *ceremony)
{
    char path[4096];
    if (!gp_progress_path(path))
        return 0;
    const PortableGpProgressStatus status = portable_gp_progress_record_ceremony(path, ceremony);
    if (status != PORTABLE_GP_PROGRESS_OK)
    {
        fprintf(stderr, "GP progress save failed: %s (%s)\n", path,
                portable_gp_progress_status_name(status));
        return 0;
    }
    return 1;
}

static uint16_t frontend_key_button(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_UP:
        return PORTABLE_FRONTEND_BUTTON_UP;
    case SDLK_DOWN:
        return PORTABLE_FRONTEND_BUTTON_DOWN;
    case SDLK_LEFT:
        return PORTABLE_FRONTEND_BUTTON_LEFT;
    case SDLK_RIGHT:
        return PORTABLE_FRONTEND_BUTTON_RIGHT;
    case SDLK_RETURN:
    case SDLK_Z:
        return PORTABLE_FRONTEND_BUTTON_A;
    case SDLK_ESCAPE:
    case SDLK_X:
        return PORTABLE_FRONTEND_BUTTON_B;
    case SDLK_SPACE:
        return PORTABLE_FRONTEND_BUTTON_START;
    case SDLK_TAB:
        return PORTABLE_FRONTEND_BUTTON_SELECT;
    case SDLK_A:
        return PORTABLE_FRONTEND_BUTTON_L;
    case SDLK_S:
        return PORTABLE_FRONTEND_BUTTON_R;
    default:
        return 0u;
    }
}

static uint16_t frontend_scancode_button(SDL_Scancode code)
{
    switch (code)
    {
    case SDL_SCANCODE_UP:
        return PORTABLE_FRONTEND_BUTTON_UP;
    case SDL_SCANCODE_DOWN:
        return PORTABLE_FRONTEND_BUTTON_DOWN;
    case SDL_SCANCODE_LEFT:
        return PORTABLE_FRONTEND_BUTTON_LEFT;
    case SDL_SCANCODE_RIGHT:
        return PORTABLE_FRONTEND_BUTTON_RIGHT;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_Z:
        return PORTABLE_FRONTEND_BUTTON_A;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_X:
        return PORTABLE_FRONTEND_BUTTON_B;
    case SDL_SCANCODE_SPACE:
        return PORTABLE_FRONTEND_BUTTON_START;
    case SDL_SCANCODE_TAB:
        return PORTABLE_FRONTEND_BUTTON_SELECT;
    case SDL_SCANCODE_A:
        return PORTABLE_FRONTEND_BUTTON_L;
    case SDL_SCANCODE_S:
        return PORTABLE_FRONTEND_BUTTON_R;
    default:
        return 0u;
    }
}

static uint16_t frontend_keyboard_state_buttons(void)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    uint16_t buttons = 0u;
    if (keys == NULL)
        return 0u;
    if (keys[SDL_SCANCODE_UP])
        buttons |= PORTABLE_FRONTEND_BUTTON_UP;
    if (keys[SDL_SCANCODE_DOWN])
        buttons |= PORTABLE_FRONTEND_BUTTON_DOWN;
    if (keys[SDL_SCANCODE_LEFT])
        buttons |= PORTABLE_FRONTEND_BUTTON_LEFT;
    if (keys[SDL_SCANCODE_RIGHT])
        buttons |= PORTABLE_FRONTEND_BUTTON_RIGHT;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER] || keys[SDL_SCANCODE_Z])
        buttons |= PORTABLE_FRONTEND_BUTTON_A;
    if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_X])
        buttons |= PORTABLE_FRONTEND_BUTTON_B;
    if (keys[SDL_SCANCODE_SPACE])
        buttons |= PORTABLE_FRONTEND_BUTTON_START;
    if (keys[SDL_SCANCODE_TAB])
        buttons |= PORTABLE_FRONTEND_BUTTON_SELECT;
    if (keys[SDL_SCANCODE_A])
        buttons |= PORTABLE_FRONTEND_BUTTON_L;
    if (keys[SDL_SCANCODE_S])
        buttons |= PORTABLE_FRONTEND_BUTTON_R;
    return buttons;
}

static void frontend_keyboard_event(const SDL_KeyboardEvent *event, uint16_t *held,
                                    uint16_t *pressed)
{
    uint16_t button;
    if (event == NULL || held == NULL || pressed == NULL)
        return;
    button = frontend_scancode_button(event->scancode);
    if (button == 0u)
        button = frontend_key_button(event->key);
    if (event->type == SDL_EVENT_KEY_DOWN)
    {
        *held |= button;
        if (!event->repeat)
            *pressed |= button;
    }
    else if (event->type == SDL_EVENT_KEY_UP)
        *held &= (uint16_t)~button;
}

static uint16_t frontend_buttons(SDL_Gamepad *controller, uint16_t keyboard_buttons)
{
    uint16_t buttons = keyboard_buttons | portable_mobile_touch_frontend();
    buttons |= portable_sdl_gamepad_frontend(controller);
    return buttons;
}

uint64_t frontend_now_ns(uint64_t frequency)
{
    const uint64_t counter = SDL_GetPerformanceCounter();
    return counter / frequency * UINT64_C(1000000000) +
           counter % frequency * UINT64_C(1000000000) / frequency;
}

static int prepare_ghost_race(void *user, uint8_t driver_class, uint8_t roster_driver,
                              const PortableRecoveredRaceView **out,
                              const PortableCharacterAssetView **character)
{
    GhostRaceContext *context = user;
    PortableContentSelection selection = {0};
    if (context == NULL || context->pack == NULL || context->storage == NULL ||
        context->presentation_storage == NULL || out == NULL || character == NULL ||
        portable_content_registry_saved_driver(context->pack, driver_class, roster_driver,
                                               &selection.character) != PORTABLE_CONTENT_OK)
        return 0;
    selection.course = context->course;
    if (portable_course_open_race(context->pack, &selection, context->storage, &context->race,
                                  portable_mod_identity_mirror()) != PORTABLE_RECOVERED_PACK_OK ||
        portable_course_open_presentation(context->pack, &selection, context->storage,
                                          &context->race, context->presentation_storage,
                                          &context->presentation, portable_mod_identity_mirror()) !=
            PORTABLE_RECOVERED_PACK_OK)
        return 0;
    *out = &context->race;
    *character = &context->presentation.character;
    return 1;
}

typedef struct
{
    PortableRecoveredPackStorage *storage, *ghost_storage;
    PortablePresentationPackStorage *ghost_presentation, *presentation;
    PortableRecoveredRaceView race;
    PortablePresentationPackView view;
    GhostRaceContext ghost_context;
    PortableRecoveredTimeTrialHostScene scene;
    PortableTimeTrialHost *host;
} SelectedTimeTrial;

static void release_selected_session(SelectedTimeTrial *selected)
{
    if (selected == NULL)
        return;
    free(selected->presentation);
    free(selected->ghost_presentation);
    free(selected->ghost_storage);
    free(selected->storage);
    free(selected);
}

static SelectedTimeTrial *prepare_selected_session(const PortableAssetPackView *pack,
                                                   uint32_t course_id, uint32_t character_id,
                                                   SDL_Window *window, SDL_Renderer *renderer,
                                                   SDL_Texture *texture, SDL_Gamepad **controller,
                                                   RouteAudioHost *audio)
{
    SelectedTimeTrial *selected = calloc(1, sizeof *selected);
    if (selected == NULL)
        return NULL;
    selected->storage = malloc(sizeof *selected->storage);
    selected->ghost_storage = malloc(sizeof *selected->ghost_storage);
    selected->ghost_presentation = malloc(sizeof *selected->ghost_presentation);
    selected->presentation = malloc(sizeof *selected->presentation);
    selected->ghost_context =
        (GhostRaceContext){.pack = pack,
                           .storage = selected->ghost_storage,
                           .presentation_storage = selected->ghost_presentation};
    PortableContentSelection selection;
    if (selected->storage == NULL || selected->ghost_storage == NULL ||
        selected->ghost_presentation == NULL || selected->presentation == NULL)
        goto fail;
    if (portable_content_registry_select(pack, course_id, character_id, &selection) !=
            PORTABLE_CONTENT_OK ||
        portable_course_open_race(pack, &selection, selected->storage, &selected->race,
                                  portable_mod_identity_mirror()) != PORTABLE_RECOVERED_PACK_OK ||
        portable_course_open_presentation(
            pack, &selection, selected->storage, &selected->race, selected->presentation,
            &selected->view, portable_mod_identity_mirror()) != PORTABLE_RECOVERED_PACK_OK)
        goto fail;
    if (selected->race.dependencies.response_rules == NULL || selected->race.view_rules == NULL)
    {
        fprintf(stderr, "Course %u mode Time Trial: missing imported rules; reimport your ROM.\n",
                course_id);
        goto fail;
    }
    if (selected->view.hud.item_palette_selectors == NULL ||
        selected->view.hud.item_palette_count != PORTABLE_HUD_ITEM_PALETTE_COUNT)
    {
        fprintf(
            stderr,
            "Course %u mode Time Trial: missing item HUD palette selectors; reimport your ROM.\n",
            course_id);
        goto fail;
    }
    selected->ghost_context.course = selection.course;
    selected->scene = (PortableRecoveredTimeTrialHostScene){
        .race = &selected->race,
        .frontend_pack = pack,
        .lap_board = &selected->view.gp_results,
        .result_course_name_tiles =
            selected->view.gp_opening.tiles ? selected->view.gp_opening.tiles + 0xe0u : NULL,
        .record_path = getenv("MKSC_TIME_TRIAL_RECORD"),
        .telemetry_path = getenv("MKSC_TIME_TRIAL_TELEMETRY"),
        .pause_capture_path = getenv("MKSC_PAUSE_CAPTURE"),
        .host_window = window,
        .host_renderer = renderer,
        .host_texture = texture,
        .host_controller_slot = controller,
        .ghost_user = &selected->ghost_context,
        .ghost_prepare = prepare_ghost_race,
        .prelude_ticks = audio != NULL ? 244u : 0u,
        .audio_user = audio,
        .audio_set_paused = audio != NULL ? route_audio_set_paused : NULL,
        .audio_restart = audio != NULL ? route_audio_restart : NULL,
        .audio_result_replay = audio != NULL ? route_audio_result_replay : NULL,
        .audio_cue = audio != NULL ? route_audio_cue : NULL,
        .audio_horn = audio != NULL ? route_audio_horn : NULL,
        .audio_render_frame = audio != NULL ? route_audio_render : NULL,
        .presentation =
            {
                .course_id = selection.course.id,
                .character_id = selection.character.id,
                .gp_hud = &selected->view.gp_race_hud,
                .gp_markers = &selected->view.gp_minimap_markers,
                .world_map = selected->view.world_map,
                .tileset = selected->view.tileset,
                .sky = &selected->view.sky,
                .character = &selected->view.character,
                .charged_exit_particle = &selected->view.charged_exit_particle,
                .mushroom_burst = &selected->view.mushroom_burst,
                .coin_pickup = &selected->view.coin_pickup,
                .lava_smoke = &selected->view.lava_smoke,
                .entry_splash = &selected->view.entry_splash,
                .recovery_thaw = &selected->view.recovery_thaw,
                .recovery_drip = &selected->view.recovery_drip,
                .recovery_fx_rules = selected->view.recovery_fx_rules,
                .surface_trail_art = selected->view.surface_trail_art,
                .surface_trail_rules = selected->view.surface_trail_rules,
                .coin_effect_art = selected->view.coin_effect_art,
                .impact_effect_art = selected->view.impact_effect_art,
                .coin_effect_rules = selected->view.coin_effect_rules,
                .driver_projection = selected->view.driver_projection,
                .weather_profile = selected->view.weather_profile,
                .scenery = &selected->view.scenery,
                .lava_bubble_art = selected->view.lava_bubble_art,
                .thwomp_art = selected->view.thwomp_art,
                .crab_art = selected->view.crab_art,
                .seagull_art = selected->view.seagull_art,
                .piranha_art = selected->view.piranha_art,
                .snowman_art = selected->view.snowman_art,
                .mouse_art = selected->view.mouse_art,
                .thundercloud_art = selected->view.thundercloud_art,
                .falling_star_art = selected->view.falling_star_art,
                .penguin_art = selected->view.penguin_art,
                .boo_art = selected->view.boo_art,
                .cannonball_art = selected->view.cannonball_art,
                .minimap = &selected->view.minimap,
                .hud = &selected->view.hud,
                .sine = selected->storage->sine,
                .sine_count = 1024u,
            },
    };
    return selected;
fail:
    release_selected_session(selected);
    return NULL;
}

static int run_selected_session(const PortableAssetPackView *pack, uint32_t course_id,
                                uint32_t character_id,
                                PortableRecoveredTimeTrialHostOutcome *outcome, SDL_Window *window,
                                SDL_Renderer *renderer, SDL_Texture *texture,
                                SDL_Gamepad **controller, RouteAudioHost *audio)
{
    SelectedTimeTrial *selected = prepare_selected_session(pack, course_id, character_id, window,
                                                           renderer, texture, controller, audio);
    if (selected == NULL)
        return 1;
    const int result = portable_recovered_time_trial_host_run(&selected->scene, outcome);
    release_selected_session(selected);
    return result;
}

int run_direct_time_trial_sdl(const PortableAssetPackView *pack, uint32_t course_id,
                              uint32_t character_id)
{
    PortableRecoveredTimeTrialHostOutcome outcome = {0};
    return run_selected_session(pack, course_id, character_id, &outcome, NULL, NULL, NULL, NULL,
                                NULL);
}

static PortableFrontendPage frontend_return_page(PortableRecoveredTimeTrialReturn destination)
{
    switch (destination)
    {
    case PORTABLE_TIME_TRIAL_RETURN_COURSE:
        return PORTABLE_FRONTEND_PAGE_COURSE;
    case PORTABLE_TIME_TRIAL_RETURN_CHARACTER:
        return PORTABLE_FRONTEND_PAGE_CHARACTER;
    case PORTABLE_TIME_TRIAL_RETURN_MODE:
        return PORTABLE_FRONTEND_PAGE_MODE;
    default:
        return 0;
    }
}

typedef struct
{
    uint8_t cup, engine_class;
    uint32_t character;
    uint8_t shell_route;
    const char *save_directory;
    uint8_t quick_run, quick_options_mask, replay_aid, battle, arena, final_seed;
    uint8_t players, track;         /* Aid player count and selected course track. */
    const char *tour, *tour_frames; /* Menu tour: button script, optional PPM folder. */
} GameInputAid;

/* Menu tour: one script token per settled screen. The screen is captured
 * before each press; '.' only waits. Presses are held two frames so a
 * zero-tick frame cannot drop one. */
typedef struct
{
    size_t index;
    uint32_t idle, wait, shots;
    uint64_t wall_admission;
    uint16_t wall_delay_ms;
    uint8_t wall_delay_initialized;
    uint16_t held;
    uint8_t hold, motion_started, mesh_fall_seen, mesh_item_settled_frames;
    size_t gp_race_token[4]; /* Tour index + 1 that drove each 3D GP course, */
    uint32_t gp_race_credits[4]; /* with the retry credits it started on. */
    uint8_t gp_attempt[4]; /* Retries spent on each 3D GP course. */
#ifdef PORTABLE_ENABLE_TEST_HOOKS
    uint8_t device_reported;
#endif
    size_t motion_event;
    uint32_t motion_origin, motion_last_tick;
    const char *motion_directory;
} MenuTour;

/* Sample motion relative to ordinary menu input, including locked transitions.
 * Settled-page tours alone cannot show easing, intermediate fades or retargeting. */
static int menu_tour_motion_capture(MenuTour *tour, const PortableFrontendFlow *flow,
                                    const PortableSoftwareFrame *frame)
{
    static const uint32_t samples[] = {1u,  2u,  4u,  6u,  8u,  9u,  12u, 16u,
                                       24u, 32u, 48u, 64u, 80u, 96u, 128u};
    if (tour->motion_directory == NULL || !tour->motion_started ||
        tour->motion_last_tick == flow->tick)
        return 1;
    tour->motion_last_tick = flow->tick;
    const uint32_t age = flow->tick - tour->motion_origin;
    for (size_t i = 0u; i < sizeof samples / sizeof samples[0]; ++i)
    {
        if (age != samples[i])
            continue;
        char path[4096];
        const int size = snprintf(path, sizeof path, "%s/%03zu-%03u.ppm", tour->motion_directory,
                                  tour->motion_event, (unsigned)age);
        if (size <= 0 || (size_t)size >= sizeof path ||
            !portable_software_frame_write_ppm(frame, path))
            return 0;
        printf("Frontend motion capture: event=%zu age=%u page=%u\n", tour->motion_event,
               (unsigned)age, (unsigned)flow->page);
        break;
    }
    return 1;
}

#ifdef PORTABLE_ENABLE_TEST_HOOKS
/* Drive ordinary menu routes through the same semantic device adapters used
 * by live input. This checks mappings, not physical controller acceptance. */
static uint16_t menu_tour_device_button(MenuTour *tour, uint16_t button, SDL_Renderer *renderer,
                                        int touch_enabled, const char **failure)
{
    const char *device = getenv("MKSC_MENU_TOUR_DEVICE");
    if (device == NULL) return button;
    static const uint16_t masks[10] = {
        PORTABLE_FRONTEND_BUTTON_UP,PORTABLE_FRONTEND_BUTTON_DOWN,
        PORTABLE_FRONTEND_BUTTON_LEFT,PORTABLE_FRONTEND_BUTTON_RIGHT,
        PORTABLE_FRONTEND_BUTTON_A,PORTABLE_FRONTEND_BUTTON_B,
        PORTABLE_FRONTEND_BUTTON_START,PORTABLE_FRONTEND_BUTTON_SELECT,
        PORTABLE_FRONTEND_BUTTON_L,PORTABLE_FRONTEND_BUTTON_R};
    unsigned index = 0u;
    while (index < 10u && masks[index] != button) ++index;
    if (index == 10u) { *failure = "menu tour device button"; return 0u; }
    uint16_t result = 0u;
    if (strcmp(device,"keyboard") == 0) {
        static const SDL_Keycode keys[10] = {SDLK_UP,SDLK_DOWN,SDLK_LEFT,SDLK_RIGHT,
            SDLK_RETURN,SDLK_ESCAPE,SDLK_SPACE,SDLK_TAB,SDLK_A,SDLK_S};
        SDL_KeyboardEvent event = {.type=SDL_EVENT_KEY_DOWN,.key=keys[index]};
        uint16_t held = 0u;
        frontend_keyboard_event(&event,&held,&result);
        event.type = SDL_EVENT_KEY_UP;
        frontend_keyboard_event(&event,&held,&result);
    } else if (strcmp(device,"gamepad") == 0) {
        static const Uint8 buttons[10] = {SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,
            SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT,SDL_GAMEPAD_BUTTON_SOUTH,
            SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
        result = portable_sdl_gamepad_frontend_button(buttons[index]);
    } else if (strcmp(device,"touch") == 0) {
        int width = 0, height = 0;
        if (!SDL_GetCurrentRenderOutputSize(renderer,&width,&height) || width <= 0 || height <= 0) {
            *failure = "menu tour touch output size";
            return 0u;
        }
        PortableMobileTouchButton buttons[PORTABLE_MOBILE_TOUCH_BUTTONS];
        portable_mobile_touch_layout(width,height,buttons,PORTABLE_MOBILE_TOUCH_BUTTONS);
        portable_mobile_touch_configure(1);
        for (unsigned i = 0u; i < PORTABLE_MOBILE_TOUCH_BUTTONS; ++i) {
            if (buttons[i].frontend_mask != button) continue;
            SDL_Event event = {0};
            event.type = SDL_EVENT_FINGER_DOWN;
            event.tfinger.touchID = 1;
            event.tfinger.fingerID = 1;
            event.tfinger.x = buttons[i].x / (float)width;
            event.tfinger.y = buttons[i].y / (float)height;
            (void)portable_mobile_touch_event(&event,renderer);
            result = portable_mobile_touch_take_pressed();
            event.type = SDL_EVENT_FINGER_UP;
            (void)portable_mobile_touch_event(&event,renderer);
            break;
        }
        portable_mobile_touch_clear();
        portable_mobile_touch_configure(touch_enabled);
    } else { *failure = "menu tour device must be keyboard, gamepad or touch"; }
    if (result != button && *failure == NULL) *failure = "menu tour device mapping";
    if (*failure == NULL && !tour->device_reported) {
        printf("Menu tour device adapter: %s\n",device);
        tour->device_reported = 1u;
    }
    return result;
}
#endif

static uint16_t menu_tour_button(const PortableFrontendFlow *flow, const GameInputAid *aid,
                                 MenuTour *tour, const PortableSoftwareFrame *frame,
                                 ModernMenuHost *modern, SDL_Renderer *renderer,
                                 int touch_enabled, int *finished, const char **failure)
{
    if (tour->hold != 0u)
    {
        --tour->hold;
        return tour->held;
    }
    if (flow->buttons != 0u || flow->transition_ticks != 0u)
    {
        tour->idle = 0u;
        return 0u;
    }
    if (++tour->idle < (tour->wait != 0u ? tour->wait : 30u))
        return 0u;
    if (!tour->wall_delay_initialized)
    {
        const char *delay = getenv("MKSC_MENU_TOUR_DELAY_MS");
        unsigned milliseconds = 0u;
        if (delay != NULL)
        {
            if (!*delay)
            {
                *failure = "menu tour delay must be 0..1000ms";
                return 0u;
            }
            for (const char *p = delay; *p; ++p)
            {
                if (*p < '0' || *p > '9' || milliseconds > 100u ||
                    (milliseconds == 100u && *p != '0'))
                {
                    *failure = "menu tour delay must be 0..1000ms";
                    return 0u;
                }
                milliseconds = milliseconds * 10u + (unsigned)(*p - '0');
            }
        }
        tour->wall_delay_ms = (uint16_t)milliseconds;
        tour->wall_admission = SDL_GetTicks();
        tour->wall_delay_initialized = 1u;
    }
    if (tour->wall_delay_ms != 0u)
    {
        const uint64_t now = SDL_GetTicks();
        if (now - tour->wall_admission < tour->wall_delay_ms)
            return 0u;
        tour->wall_admission = now;
    }
    const char token = aid->tour[tour->index];
    if (aid->tour_frames != NULL)
    {
        char path[4096];
        const int length =
            snprintf(path, sizeof path, "%s/%03u.ppm", aid->tour_frames, (unsigned)tour->shots++);
        if (length <= 0 || (size_t)length >= sizeof path ||
            !(modern != NULL ? modern_menu_host_write_ppm(modern, path)
                             : portable_software_frame_write_ppm(frame, path)))
        {
            *failure = "menu tour frame capture";
            return 0u;
        }
    }
    if (token == '\0')
    {
        *finished = 1;
        return 0u;
    }
    static const char tokens[] = "ABSEUDLRlr";
    static const uint16_t buttons[] = {
        PORTABLE_FRONTEND_BUTTON_A,     PORTABLE_FRONTEND_BUTTON_B,
        PORTABLE_FRONTEND_BUTTON_START, PORTABLE_FRONTEND_BUTTON_SELECT,
        PORTABLE_FRONTEND_BUTTON_UP,    PORTABLE_FRONTEND_BUTTON_DOWN,
        PORTABLE_FRONTEND_BUTTON_LEFT,  PORTABLE_FRONTEND_BUTTON_RIGHT,
        PORTABLE_FRONTEND_BUTTON_L,     PORTABLE_FRONTEND_BUTTON_R};
    tour->motion_origin = flow->tick;
    tour->motion_event = tour->index;
    tour->motion_started = 1u;
    tour->motion_last_tick = UINT32_MAX;
    ++tour->index;
    tour->idle = 0u;
    tour->wait = token == '.' ? 90u : 30u;
    if (token == '.')
        return 0u;
    if (token == 'I') return 0u; /* Caller waits for a settled mesh inventory capture. */
    if (token == 'G' || token == 'J' || token == 'K' || token == 'T' || token == 'Y') return 0u; /* T captures a30-tick route pulse; T/K tap GP items. */
    if (token == 'V') { tour->mesh_fall_seen = 0u; return 0u; }
    if (token == 'H') {
        SDL_Event press = {0}, release = {0};
        press.type = SDL_EVENT_KEY_DOWN;
        release.type = SDL_EVENT_KEY_UP;
        press.key.key = release.key.key = SDLK_F5;
        press.key.scancode = release.key.scancode = SDL_SCANCODE_F5;
        if (!SDL_PushEvent(&press) || !SDL_PushEvent(&release))
            *failure = "menu tour F5 event";
        return 0u;
    }
    if(token=='F'){
        const char *path=getenv("MKSC_MENU_TOUR_DROP_FILE");
        if(path==NULL || path[0]=='\0'){
            *failure="menu tour drop file missing";return 0u;
        }
        SDL_Event event={0};event.type=SDL_EVENT_DROP_FILE;event.drop.data=path;
        if(!SDL_PushEvent(&event))*failure="menu tour drop event";
        return 0u;
    }
    const char *found = strchr(tokens, token);
    if (found == NULL)
    {
        *failure = "menu tour script token";
        return 0u;
    }
    tour->held = buttons[found - tokens];
#ifdef PORTABLE_ENABLE_TEST_HOOKS
    tour->held = menu_tour_device_button(tour,tour->held,renderer,touch_enabled,failure);
#else
    (void)renderer; (void)touch_enabled;
#endif
    tour->hold = 1u;
    return tour->held;
}

#ifdef PORTABLE_ENABLE_TEST_HOOKS
/* Presentation coverage resumes from injected prior standings. This is never
 * a claim that the aid won the preceding races or the final race. */
static int seed_final_cup(const PortableAssetPackView *pack,
                          const PortableGpCupDefinition *definition, uint32_t character,
                          uint8_t engine, uint8_t wanted_rank, PortableGpCupSession *out)
{
    PortableContentSelection selection;
    PortableGpScoringRules scoring;
    if (wanted_rank >= 8u || !portable_recovered_gp_scoring(pack, &scoring) ||
        portable_content_registry_select(pack, definition->course_ids[3], character, &selection) !=
            PORTABLE_CONTENT_OK)
        return 0;
    if (selection.character.character_class >= PORTABLE_GP_RACER_COUNT)
        return 0;
    const uint8_t driver_class = (uint8_t)selection.character.character_class;
    for (unsigned rank = 0u; rank < 8u; ++rank)
        if (scoring.finish_points[rank] > 9u)
            return 0;
    PortableGpRaceRoster *roster = malloc(sizeof *roster);
    PortableRecoveredPackStorage *storage = malloc(sizeof *storage);
    PortableRecoveredRaceView race_view;
    if (roster == NULL || storage == NULL ||
        portable_course_open_race(pack, &selection, storage, &race_view,
                                  portable_mod_identity_mirror()) != PORTABLE_RECOVERED_PACK_OK ||
        portable_gp_race_roster_init_for_rules(roster, &race_view.rules, driver_class) !=
            PORTABLE_GP_ROSTER_OK)
    {
        free(storage);
        free(roster);
        return 0;
    }
    PortableGpCupSession next = {0};
    next.course_id = definition->course_ids[3];
    next.course_header_index = definition->source_headers[3];
    next.cup_index = definition->cup_index;
    next.track_set = definition->track_set;
    next.course_in_cup = 3u;
    next.engine_class = engine;
    next.retry_credits = scoring.initial_retry_credits;
    next.race_mode_flags = 0x0100u;
    next.selected_driver_class = driver_class;
    next.next_mode = 8u;
    next.stage = PORTABLE_GP_CUP_STAGE_NEXT_RACE;
    next.driving_quality_sample_mask = 7u;
    memcpy(next.course_ids, definition->course_ids, sizeof next.course_ids);
    /* Rank and quality fixtures are independent: rank five supplies the
     * rating-6 branch absent from the ordinary aid's completed cups. */
    static const int16_t prior_samples[8] = {1000, 500, 250, 100, 0, -400, -250, -1000};
    for (unsigned race = 0u; race < 3u; ++race)
    {
        next.driving_quality.samples[race] = prior_samples[wanted_rank];
        next.finishing_coins[race] = 35u;
    }
    for (unsigned racer = 0u; racer < 8u; ++racer)
        next.racer_driver_classes[racer] = roster->racers[racer].driver_class;
    free(storage);
    free(roster);
    unsigned cpu = 1u;
    for (unsigned rank = 0u; rank < 8u; ++rank)
    {
        const uint8_t racer = rank == wanted_rank ? 0u : (uint8_t)cpu++;
        next.race_order[rank] = racer;
        next.standings_order[rank] = racer;
        next.rows[racer].standing_rank = (uint8_t)rank;
        /* A twelve-point fixture gap survives any stock final-race award. */
        next.cumulative_points[racer] = (uint8_t)((7u - rank) * 12u);
    }
    *out = next;
    return 1;
}
#endif

static uint16_t menu_aid_button(const PortableFrontendFlow *flow, const GameInputAid *aid,
                                uint8_t trial_completed)
{
    if (flow->buttons != 0u || flow->transition_ticks != 0u)
        return 0u;
    if (flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU && aid->players > 1u)
    {
        if (flow->player_count_open)
            return flow->players < aid->players ? PORTABLE_FRONTEND_BUTTON_RIGHT
                                                : PORTABLE_FRONTEND_BUTTON_A;
        return flow->title_cursor < 1u ? PORTABLE_FRONTEND_BUTTON_DOWN : PORTABLE_FRONTEND_BUTTON_A;
    }
    if (flow->page == PORTABLE_FRONTEND_PAGE_TITLE_MENU)
        return PORTABLE_FRONTEND_BUTTON_A;
    if (flow->page == PORTABLE_FRONTEND_PAGE_MODE)
    {
        const uint8_t wanted = aid->battle      ? 3u
                               : aid->quick_run ? 2u
                                                : (uint8_t)(aid->shell_route && !trial_completed);
        if (flow->mode_phase == 0u && flow->mode_cursor != wanted)
            return flow->mode_cursor < wanted ? PORTABLE_FRONTEND_BUTTON_RIGHT
                                              : PORTABLE_FRONTEND_BUTTON_LEFT;
        if (aid->quick_run && flow->mode_phase == 0u)
        {
            const uint8_t values[3] = {flow->quick_run_options.five_laps,
                                       flow->quick_run_options.items_off,
                                       flow->quick_run_options.coins_off};
            for (uint8_t row = 0u; row < 3u; ++row)
            {
                if (values[row] == ((aid->quick_options_mask >> row) & 1u))
                    continue;
                if (!flow->quick_options_open)
                    return PORTABLE_FRONTEND_BUTTON_SELECT;
                return flow->quick_options_cursor == row ? PORTABLE_FRONTEND_BUTTON_RIGHT
                                                         : PORTABLE_FRONTEND_BUTTON_DOWN;
            }
            if (flow->quick_options_open)
                return PORTABLE_FRONTEND_BUTTON_A;
        }
        if (flow->mode_phase == 0u)
            return PORTABLE_FRONTEND_BUTTON_A;
        return flow->race_speed == aid->engine_class ? PORTABLE_FRONTEND_BUTTON_A
                                                     : PORTABLE_FRONTEND_BUTTON_RIGHT;
    }
    if (flow->page == PORTABLE_FRONTEND_PAGE_TIME_TRIAL)
        return trial_completed ? PORTABLE_FRONTEND_BUTTON_B : PORTABLE_FRONTEND_BUTTON_A;
    if (flow->page == PORTABLE_FRONTEND_PAGE_CHARACTER)
    {
        if (aid->shell_route && trial_completed && flow->mode_cursor == 1u)
            return PORTABLE_FRONTEND_BUTTON_B;
        if (flow->character_locked)
            return PORTABLE_FRONTEND_BUTTON_START;
        /* Local players take distinct stock corners of the first page. */
        static const uint8_t local_slots[4] = {0u, 2u, 6u, 8u};
        const uint32_t wanted =
            flow->players > 1u
                ? flow->config.drivers[local_slots[flow->character_player & 3u]].content_id
                : aid->character;
        const PortableFrontendDriverSlot *current =
            portable_frontend_flow_driver_at(flow, flow->character_page, flow->character_cursor);
        if (current != NULL && current->content_id == wanted)
            return PORTABLE_FRONTEND_BUTTON_A;
        /* Explore the real menu's navigation on copies, then issue one direction. */
        static const uint16_t directions[4] = {
            PORTABLE_FRONTEND_BUTTON_UP, PORTABLE_FRONTEND_BUTTON_DOWN,
            PORTABLE_FRONTEND_BUTTON_LEFT, PORTABLE_FRONTEND_BUTTON_RIGHT};
        enum
        {
            STATES = PORTABLE_FRONTEND_DRIVER_CAPACITY
        };
        uint8_t queue[STATES], seen[STATES] = {0};
        uint16_t first[STATES] = {0};
        const uint8_t start = (uint8_t)(flow->character_page * PORTABLE_FRONTEND_DRIVER_SLOTS +
                                        flow->character_cursor);
        unsigned read = 0u, write = 1u;
        queue[0] = start;
        seen[start] = 1u;
        while (read < write)
        {
            const uint8_t state = queue[read++];
            for (unsigned direction = 0u; direction < 4u; ++direction)
            {
                PortableFrontendFlow probe = *flow;
                probe.character_page = (uint8_t)(state / PORTABLE_FRONTEND_DRIVER_SLOTS);
                probe.character_cursor = (uint8_t)(state % PORTABLE_FRONTEND_DRIVER_SLOTS);
                probe.event_count = 0u;
                if (!portable_frontend_flow_tick(&probe, directions[direction]))
                    return 0u;
                const uint8_t next =
                    (uint8_t)(probe.character_page * PORTABLE_FRONTEND_DRIVER_SLOTS +
                              probe.character_cursor);
                if (next >= STATES || seen[next])
                    continue;
                seen[next] = 1u;
                queue[write++] = next;
                first[next] = state == start ? directions[direction] : first[state];
                if (flow->config.drivers[next].content_id == wanted)
                    return first[next];
            }
        }
    }
    if (flow->page == PORTABLE_FRONTEND_PAGE_COURSE)
    {
        if (aid->shell_route && trial_completed && flow->mode_cursor == 1u)
            return PORTABLE_FRONTEND_BUTTON_B;
        if (aid->battle)
            return flow->course_track == aid->arena ? PORTABLE_FRONTEND_BUTTON_A
                                                    : PORTABLE_FRONTEND_BUTTON_DOWN;
        if (portable_track_set_of_cup(flow->course_cup) != portable_track_set_of_cup(aid->cup))
            return PORTABLE_FRONTEND_BUTTON_SELECT;
        if ((aid->quick_run || (aid->shell_route && flow->mode_cursor == 1u)) &&
            flow->course_phase == 1u && flow->course_track != aid->track)
            return PORTABLE_FRONTEND_BUTTON_DOWN;
        return flow->course_cup == aid->cup ? PORTABLE_FRONTEND_BUTTON_A
                                            : PORTABLE_FRONTEND_BUTTON_RIGHT;
    }
    return 0u;
}

/* One shell owns menu state and borrowed child scenes. Helpers below retain
 * the event -> child scene -> fixed menu ticks -> launch -> draw order. */
typedef struct GameShell
{
    const PortableAssetPackView *pack;
    const PortableAssetPackView *pack_3d;
    PortableCourse3DHost *preview_3d;
    PortableCourse3DHost *editor_previous_preview;
    CourseEditorHost *editor;
    uint32_t mesh_audio_generation;
    uint8_t mesh_audio_active;
    const GameInputAid *aid;
    PortableScriptHost *scripts;
    PortableSettings settings, persisted_settings;
    char settings_path[4096];
    int options_open, binding_capture;
    unsigned enhancements_cursor;
    uint8_t profile_request;
    const PortableAssetPackView *base_pack;
    const PortableAssetPackView *base_pack_3d;
    PortableStore *store, *store_loading;
    PortableStoreJob *store_job;
    unsigned store_cursor,store_source,store_action;
    uint8_t store_open,store_details,store_downloading,store_remove_confirm;
    char store_download_path[4096];
    PortableModInstall *mod_install;
    PortableFileDialog *mod_dialog;
    PortableModManager *mod_manager;
    PortableModManagerCandidate *mod_candidate;
    struct GameShell *mod_prepared;
    char mods_directory[4096],mod_notice[PORTABLE_MOD_ERROR_CAPACITY];
    unsigned mod_cursor,mod_detail_page;
    unsigned library_game,library_row;
    /* Owned copies of each imported game's card art: the manager caches only one preview. */
    uint32_t *library_art[MODERN_MENU_GAMES_MAX];
    unsigned library_art_size[MODERN_MENU_GAMES_MAX][2];
    uint8_t library_return;
    uint8_t mod_order,mod_apply_pending,mod_action_open,mod_action_cursor,mod_notice_error;
    uint8_t mod_unsaved_open, mod_unsaved_cursor, mod_exit_target;
    /* One-step changes (request 4) name what they load; on failure the
     * staged selection returns to the active one. */
    uint8_t mod_advanced;
    char mod_apply_label[128];
    char store_remove_pending[PORTABLE_MOD_ID_CAPACITY]; /* Removed after it loads off. */
    uint8_t mod_dialog_folder, mod_dialog_game;
    PortableGameImportJob *game_import;
    uint8_t watch_mods, mod_watch_initialized, mod_watch_saf_warning;
    uint64_t mod_watch_poll, mod_watch_changed, mod_watch_signature;
    unsigned options_cursor, binding_cursor;
    uint16_t options_buttons;
    const char *options_notice;
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    /* 3D cups on the ordinary course screen, one group per providing mod. */
    const PortableModSet *mods;
    PortableFrontendMeshCup mesh_cups[PORTABLE_FRONTEND_MESH_CUP_CAPACITY];
    const char *mesh_group_names[PORTABLE_FRONTEND_MESH_GROUP_CAPACITY];
    uint8_t mesh_group_platforms[PORTABLE_FRONTEND_MESH_GROUP_CAPACITY];
    PortableFrontendFlow flow;
    SelectedTimeTrial *selected_trial;
    PortableGpHost *selected_gp;
    PortableGpInputRecording *quick_recording;
    uint8_t quick_playback, aid_quick_replayed;
    PortableGpCeremonyHost *selected_ceremony;
    PortableFrontendPresentationScratch *scratch;
    PortableUiBackgroundHost ui_background;
    PortableFrameScheduler scheduler;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *gpu_aid_target;
    SDL_Gamepad *controller;
    FILE *record_file;
    PortableFrontendInputReplayStatus record_status;
    PortableAudioCommand audio_commands[512];
    PortableAudioCommandStream audio_stream;
    PortableFrontendAudio frontend_audio;
    PortableGpCupDefinition gp_cup_definition;
    PortableGpCupSession gp_cup_progress;
    uint8_t gp_cup_progress_ready, gp_retry_credits, gp_retry_pending;
    uint32_t marker_clock, marker_display_clock;
    uint64_t aid_now;
    uint32_t aid_frames, aid_ceremony_frames;
    uint8_t aid_completed, aid_trial_completed;
    PortableFrontendPage aid_page;
    RouteAudioHost *audio;
    size_t next_audio_command, next_record_event;
    PortableTimeTrialRecords records;
    PortableGpProgress menu_progress;
    char incoming_ghost[4096];
    const char *record_path;
    uint32_t record_tick;
    uint8_t menu_frame_written;
    MenuTour tour;
    uint16_t keyboard_buttons, keyboard_pressed;
    uint16_t mesh_driving_pressed;
    PortableUiTextBank ui_text;
    PortableSoftwareRenderTarget menu_target;
    PortableSoftwareFrame frame;
    ModernMenuHost modern;
    uint64_t frequency, mesh_present_ns, mesh_aid_remainder;
    uint32_t mesh_aid_subframes, mesh_aid_last_tick, mesh_aid_last_alpha;
    uint32_t mesh_aid_between, mesh_aid_moving, mesh_aid_captures, mesh_aid_shots, mesh_aid_shot_tick;
    uint32_t mesh_aid_listed_shots;
    uint16_t mesh_aid_buttons;
    PortableMeshTimeTrialPilotState mesh_pilot[PORTABLE_MESH_LOCAL_MAX];
    PortableMeshInputSegments mesh_input_segments;
    uint32_t mesh_input_race;
    uint32_t mesh_gp_routes[4];
    uint8_t mesh_gp_routes_enabled;
    PortableMeshHazardSeekState mesh_hazard_seek;
    uint8_t mesh_hazard_seek_enabled;
    uint32_t mesh_hazard_seek_race; /* GP course ordinal, or UINT32_MAX for every race */
    uint32_t mesh_hazard_seek_families; /* actor family mask, 0 for all */
    uint8_t mesh_aid_fractional, mesh_aid_presented;
    uint8_t mesh_benchmark, mesh_benchmark_ready;
    uint32_t mesh_benchmark_warmup, mesh_benchmark_samples, mesh_benchmark_batches;
    uint64_t mesh_benchmark_cpu_ns, mesh_benchmark_completed_ns;
    uint64_t mesh_benchmark_batch_start, mesh_benchmark_worst_ns;
    uint64_t mesh_benchmark_worst_frame_ns;
    size_t mesh_benchmark_width, mesh_benchmark_height;
    unsigned mesh_benchmark_views;
    int focused, quit, error, needs_present, wait_for_neutral;
    const char *failure_detail;
} GameShell;

static int game_shell_modern_active(const GameShell *shell)
{
    return shell->store_open || !shell->settings.classic_menus;
}

static const char *edit_effects(GameShell *shell, unsigned row)
{
    if (row >= HD2D_PRESET_ROW)
    {
        if (row == HD2D_PRESET_ROW)
        {
            const int enabled = portable_settings_hd2d_preset(&shell->settings) != 1u;
            portable_settings_hd2d(&shell->settings, enabled);
            portable_settings_hd2d(&shell->persisted_settings, enabled);
        }
        else
        {
            const PortableSetting field = (PortableSetting)(PORTABLE_SETTING_HD2D_DOF + row - HD2D_PRESET_ROW - 1u);
            const uint32_t value = !portable_setting_get(&shell->settings, field);
            (void)portable_setting_set(&shell->settings, field, value);
            (void)portable_setting_set(&shell->persisted_settings, field, value);
        }
        return shell->settings_path[0] && portable_settings_save(shell->settings_path, &shell->persisted_settings)
            ? "SAVED" : "SAVE FAILED - CHANGE ACTIVE THIS SESSION";
    }
    if (row == EFFECT_PRESET_ROW)
    {
        /* Full drops to Low Power; Low Power and Custom go to Full. */
        const uint32_t value = effects_preset(&shell->settings) == 1u ? 0u : 1u;
        for (unsigned i = 0u; i < EFFECT_ROWS; ++i)
        {
            (void)portable_setting_set(&shell->settings, effect_fields[i], value);
            if (i + 1u < EFFECT_ROWS)
                (void)portable_setting_set(&shell->persisted_settings, effect_fields[i], value);
        }
        return save_setting_edit(shell->settings_path, &shell->persisted_settings,
                                 effect_fields[EFFECT_ROWS - 1u], value);
    }
    const PortableSetting field = effect_fields[row - EFFECT_PRESET_ROW - 1u];
    const uint32_t value = !portable_setting_get(&shell->settings, field);
    (void)portable_setting_set(&shell->settings, field, value);
    return save_setting_edit(shell->settings_path, &shell->persisted_settings, field, value);
}

/* Applies one changed setting to the window, renderer and audio, then saves it like the Options screen.
 * Returns 1 when the value took effect; *notice then names the save result or why it was refused. */
static int game_shell_commit_setting(GameShell *shell, PortableSetting field, uint32_t value, const char **notice)
{
    PortableSettings changed = shell->settings;
    if (!portable_setting_set(&changed, field, value))
        return 0;
    if (!apply_settings_field(shell->window, shell->renderer, shell->texture, shell->audio->queue.stream,
                              &shell->flow, &changed, field, shell->record_file != NULL))
    {
        fprintf(stderr, "option %s: %s\n", portable_setting_name(field), SDL_GetError());
        *notice = "OPTION UNAVAILABLE ON THIS DISPLAY";
        return 0;
    }
    shell->settings = changed;
    *notice = save_setting_edit(shell->settings_path, &shell->persisted_settings, field, value);
    return 1;
}

/* The in-race pause page edits the same settings through the same code as Options. */
static const PortableSettings *game_shell_graphics_current(void *user)
{
    return &((GameShell *)user)->settings;
}

static const PortableSettings *game_shell_graphics_edit(void *user, unsigned row, int delta, const char **notice)
{
    GameShell *shell = user;
    PortableSetting field;
    if (!portable_graphics_row_field(row, &field))
    {
        *notice = edit_effects(shell, HD2D_PRESET_ROW);
        return &shell->settings;
    }
    const int value = portable_option_step(field, (int)portable_setting_get(&shell->settings, field), delta);
    return value >= 0 && game_shell_commit_setting(shell, field, (uint32_t)value, notice) ? &shell->settings : NULL;
}

static int game_shell_mesh_preview(const void *owner, uint32_t course_id, PortableNativeImage *out)
{
    return portable_course_3d_host_preview_image(owner, course_id, out);
}

/* Every installed 3D course joins a cup of its provider's group: mods in
 * load order, then courses no mod provides (built-in 3D content). */
static int game_shell_list_mesh_cups(GameShell *shell)
{
    const size_t count = portable_course_3d_host_listing_count(shell->preview_3d);
    PortableMeshCupMenuRow *rows = count ? calloc(count, sizeof *rows) : NULL;
    size_t *orders = count ? calloc(count, sizeof *orders) : NULL;
    size_t providers[PORTABLE_FRONTEND_MESH_GROUP_CAPACITY];
    uint8_t groups = 0u;
    int ok = !count || (rows && orders);
    for (size_t i = 0; ok && i < count; ++i)
    {
        PortableCourse3DListing listing;
        ok = portable_course_3d_host_listing(shell->preview_3d, i, &listing);
        orders[i] = SIZE_MAX;
        if (ok && !portable_mods_course_3d_provider(shell->mods, listing.course_id, &orders[i]))
            orders[i] = SIZE_MAX;
        rows[i] = (PortableMeshCupMenuRow){listing.course_id, listing.cup_id, listing.slot, listing.modes,
                                           listing.sort_key, listing.name, listing.cup_name, UINT8_MAX,
                                           listing.raceable};
        /* Insert this provider in ascending order; SIZE_MAX (none) sorts last. */
        uint8_t at = 0u;
        while (at < groups && providers[at] < orders[i])
            ++at;
        if (ok && (at == groups || providers[at] != orders[i]))
        {
            if (groups == PORTABLE_FRONTEND_MESH_GROUP_CAPACITY)
            {
                if (at == groups)
                    continue; /* Listed groups keep their place; later ones drop. */
                --groups;
            }
            memmove(&providers[at + 1u], &providers[at], (size_t)(groups - at) * sizeof *providers);
            providers[at] = orders[i];
            ++groups;
        }
    }
    size_t kept = 0u;
    for (size_t i = 0; ok && i < count; ++i)
        for (uint8_t g = 0u; g < groups; ++g)
            if (providers[g] == orders[i])
            {
                rows[i].group = g;
                rows[kept++] = rows[i];
                break;
            }
    if (ok && kept != count)
        fprintf(stderr, "3D cups: more than %u course providers; %zu courses unlisted\n",
                PORTABLE_FRONTEND_MESH_GROUP_CAPACITY, count - kept);
    for (uint8_t g = 0u; ok && g < groups; ++g)
    {
        const char *name = NULL;
        for (size_t i = 0; providers[g] != SIZE_MAX && i < kept && !name; ++i)
            if (rows[i].group == g)
                name = portable_mods_course_3d_provider(shell->mods, rows[i].course_id, NULL);
        shell->mesh_group_names[g] = name ? name : "3D courses";
        /* A group is its provider's platform once that provider lists drivers. */
        shell->mesh_group_platforms[g] = 0u;
        for (size_t d = 0u; providers[g] < UINT8_MAX && d < shell->config.driver_count; ++d)
            if (shell->drivers[d].platform == providers[g] + 1u)
                shell->mesh_group_platforms[g] = (uint8_t)(providers[g] + 1u);
    }
    int overflow = 0;
    const size_t cups = ok ? portable_mesh_cup_menu_build(rows, kept, shell->mesh_cups,
                                                          PORTABLE_FRONTEND_MESH_CUP_CAPACITY, &overflow) : 0u;
    free(rows);
    free(orders);
    if (!ok || overflow)
    {
        fprintf(stderr, "3D cups: %s\n", overflow ? "more installed 3D cups than the menu lists"
                                                  : "catalog listing failed");
        return 0;
    }
    if (!portable_frontend_flow_set_mesh_cups(&shell->flow, shell->mesh_cups, cups,
                                              shell->mesh_group_names, shell->mesh_group_platforms,
                                              cups ? groups : 0u))
        return 0;
    if (cups)
        printf("3D cups: groups=%u cups=%zu\n", (unsigned)groups, cups);
    return 1;
}

/* The content platform a 3D-domain section belongs to: its providing mod's
 * load order + 1 when that mod also provides 3D courses (an imported game),
 * else 0 (the base game). */
static uint8_t game_shell_platform_of(const GameShell *shell, uint32_t kind, uint32_t id)
{
    size_t order = SIZE_MAX;
    if (!portable_mods_section_3d_provider(shell->mods, kind, id, &order) || order >= UINT8_MAX ||
        !portable_mods_provides_3d_kind(shell->mods, order, PORTABLE_COURSE_3D_KIND))
        return 0u;
    return (uint8_t)(order + 1u);
}

/* 3D-only drivers of the base game join the grid after the 2D roster; each
 * platform's drivers (its own versions of stock drivers included) follow on
 * pages of their own, in load order. Rows are named from their DRV3 rows. */
static void game_shell_add_3d_drivers(GameShell *shell)
{
    PortableDriver3D rows[PORTABLE_DRIVER_3D_CAPACITY], base[PORTABLE_DRIVER_3D_CAPACITY];
    PortableDriver3D own[PORTABLE_DRIVER_3D_CAPACITY];
    uint8_t platforms[PORTABLE_DRIVER_3D_CAPACITY];
    const size_t count = portable_driver_3d_list(shell->pack_3d, rows, PORTABLE_DRIVER_3D_CAPACITY);
    size_t base_count = 0u;
    for (size_t i = 0u; i < count; ++i)
    {
        platforms[i] = game_shell_platform_of(shell, PORTABLE_ASSET_SECTION_DRIVER_3D, rows[i].id);
        if (!platforms[i])
            base[base_count++] = rows[i];
    }
    if (!portable_frontend_add_3d_drivers(base, base_count, shell->drivers, &shell->config))
        fputs("3D drivers do not fit the character grid; listing none\n", stderr);
    for (unsigned platform = 1u; platform < UINT8_MAX; ++platform)
    {
        size_t own_count = 0u;
        for (size_t i = 0u; i < count; ++i)
            if (platforms[i] == platform)
                own[own_count++] = rows[i];
        if (own_count && !portable_frontend_add_platform_drivers(own, own_count, (uint8_t)platform,
                                                                 shell->drivers, &shell->config))
            fprintf(stderr, "Platform %u drivers do not fit the character grid; listing none\n",
                    platform);
    }
}

/* Initial boot and validated mod application share native content owners.
 * These preparation phases neither create SDL resources nor publish identity. */
static int game_shell_prepare_content(GameShell *shell,const char *progress_suffix)
{
    if (!portable_ui_text_load(shell->pack, &shell->ui_text))
    {
        fprintf(stderr, "Invalid UI text content\n");
        return 1;
    }
    const uint64_t content_started = portable_startup_clock();
    shell->preview_3d = portable_course_3d_host_create(
        shell->pack_3d != NULL ? shell->pack_3d : shell->pack,
        shell->mod_notice, sizeof shell->mod_notice);
    portable_startup_phase("3D content/browser", content_started);
    if (shell->preview_3d == NULL)
    {
        fprintf(stderr, "3D course content: %s\n", shell->mod_notice);
        return 1;
    }
    shell->menu_target.frame.ui_text = &shell->ui_text;
    if (!build_frontend_config(shell->pack, shell->drivers, shell->courses, &shell->config))
    {
        fputs("faithful frontend configuration is unavailable\n", stderr);
        return 1;
    }
    if (shell->pack_3d != NULL && shell->pack_3d != shell->pack)
        game_shell_add_3d_drivers(shell);
    if (!portable_frontend_flow_init(&shell->flow, &shell->config) ||
        !portable_frontend_flow_settle_title(&shell->flow))
    {
        fputs("faithful frontend configuration is unavailable\n", stderr);
        return 1;
    }
    if (!game_shell_list_mesh_cups(shell))
    {
        fputs("3D cup listing is unavailable\n", stderr);
        return 1;
    }
    if (shell->pack_3d != NULL && shell->pack_3d != shell->pack)
    {
        PortableKart3D karts[PORTABLE_KART_3D_CAPACITY];
        PortableFrontendKartSlot slots[PORTABLE_FRONTEND_KART_CAPACITY];
        const size_t count = portable_kart_3d_list(shell->pack_3d, karts, PORTABLE_KART_3D_CAPACITY);
        size_t listed = 0u;
        for (size_t index = 0u; index < count && listed < PORTABLE_FRONTEND_KART_CAPACITY; ++index)
            if (karts[index].flags & PORTABLE_KART_3D_SELECTABLE)
                slots[listed++] = (PortableFrontendKartSlot){
                    karts[index].id, karts[index].owner,
                    (uint8_t)((karts[index].flags & PORTABLE_KART_3D_OWNER_DEFAULT) != 0u)};
        (void)portable_frontend_flow_set_karts(&shell->flow, slots, listed);
    }
    if(progress_suffix!=NULL && !refresh_gp_cup_access_with_suffix(
        &shell->flow,&shell->menu_progress,progress_suffix)){
        shell->failure_detail="target GP progress is unreadable or invalid";
        return 1;
    }
    if (!portable_frontend_configure_motion(shell->pack, &shell->flow))
    {
        fputs("frontend motion content is invalid\n", stderr);
        return 1;
    }
    return 0;
}

static void game_shell_prepare_audio(GameShell *shell)
{
    shell->audio = malloc(sizeof *shell->audio);
    if (shell->audio != NULL)
        memset(shell->audio, 0, sizeof *shell->audio);
    if (shell->audio == NULL)
    {
        shell->failure_detail = "audio host allocation failed";
        shell->error = 1;
    }
    if (!shell->error)
    {
        PortableAudioStreamStatus status = portable_audio_command_stream_init(
            &shell->audio_stream, shell->audio_commands,
            sizeof shell->audio_commands / sizeof shell->audio_commands[0]);
        if (status != PORTABLE_AUDIO_STREAM_OK)
        {
            shell->failure_detail = portable_audio_stream_status_name(status);
            shell->error = 1;
        }
    }
    if (!shell->error &&
        !portable_frontend_audio_init(&shell->frontend_audio, &shell->flow, &shell->audio_stream))
    {
        shell->failure_detail = "frontend audio initialization failed";
        shell->error = 1;
    }
    if (!shell->error)
    {
        PortableAudioSessionStatus status =
            portable_audio_session_init(&shell->audio->session, shell->pack);
        if (status != PORTABLE_AUDIO_SESSION_OK)
        {
            shell->failure_detail = portable_audio_session_status_name(status);
            shell->error = 1;
        }
    }
    if (!shell->error &&
        !portable_hazard_sound_rules_load(shell->pack, &shell->audio->hazard_rules))
    {
        shell->failure_detail = "frontend character audio rules";
        shell->error = 1;
    }
    if (!shell->error &&
        portable_character_audio_rules_valid(&shell->audio->hazard_rules.character_cues))
    {
        uint8_t players[2][8];
        for (unsigned table = 0; table < 2u; ++table)
            for (unsigned i = 0; i < 8u; ++i)
            {
                const PortableCharacterRevCue *cue =
                    table ? &shell->audio->hazard_rules.character_cues.launch_revs[i]
                          : &shell->audio->hazard_rules.character_cues.menu_revs[i];
                players[table][i] = portable_audio_session_song_player(shell->pack, cue->song);
            }
        if (!portable_frontend_audio_set_character_cues(&shell->frontend_audio,
                                                        &shell->audio->hazard_rules.character_cues,
                                                        (const uint8_t (*)[8])players))
        {
            shell->failure_detail = "frontend character audio player metadata";
            shell->error = 1;
        }
    }
}

static void game_shell_start_audio(GameShell *shell)
{
    if (!shell->error)
    {
        shell->audio->command_sequence = 0u;
        while (shell->next_audio_command < shell->audio_stream.count)
            if (portable_audio_session_apply(
                    &shell->audio->session,
                    &shell->audio_stream.commands[shell->next_audio_command++]) !=
                PORTABLE_AUDIO_SESSION_OK)
            {
                shell->failure_detail =
                    portable_audio_session_status_name(shell->audio->session.status);
                shell->error = 1;
                break;
            }
    }
}

static int game_shell_startup_content(void *data, PortableLoadJob *job)
{
    GameShell *shell = data;
    if (game_shell_prepare_content(shell, portable_mod_identity_progress_suffix())) return 0;
    if (portable_load_cancelled(job)) return 0;
    portable_load_phase(job, 1);
    game_shell_prepare_audio(shell);
    game_shell_start_audio(shell);
    return !shell->error;
}

/* Prepared owners contain no SDL resources and have not published identity. */
static void game_shell_discard_mod_candidate(GameShell *shell)
{
    if(shell->mod_prepared!=NULL){
        portable_course_3d_host_close(shell->mod_prepared->preview_3d);
        free(shell->mod_prepared->audio);
        free(shell->mod_prepared->scratch);
        free(shell->mod_prepared);
        shell->mod_prepared=NULL;
    }
    portable_mod_manager_candidate_free(shell->mod_candidate);
    shell->mod_candidate=NULL;
}

static PortableModRoots game_mod_roots(const char *primary,const PortableSettings *settings)
{
    PortableModRoots roots = {.directories={primary},.count=1u};
    for (size_t i = 0u; i < settings->mod_directory_count; ++i) {
        const char *path=settings->mod_directories[i];
        int present=0;
        for (size_t root=0u;root<roots.count;++root)
            present|=strcmp(roots.directories[root],path)==0;
        if (!present) roots.directories[roots.count++]=path;
    }
    return roots;
}

static PortableModRoots game_shell_mod_roots(const GameShell *shell)
{
    return game_mod_roots(shell->mods_directory,&shell->settings);
}

static int game_shell_open_mod_manager(GameShell *shell)
{
    shell->library_return=0;
    if (shell->mod_manager != NULL) {
        shell->options_open = 4;
        shell->mod_action_open = 0u;
        return 1;
    }
    if(shell->mods_directory[0]=='\0'){
        char *directory=portable_data_directory();
        int length=directory?snprintf(shell->mods_directory,sizeof shell->mods_directory,
            "%smods",directory):-1;
        SDL_free(directory);
        if(length<=0 || (size_t)length>=sizeof shell->mods_directory){
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"Mod directory unavailable");
            shell->options_notice=shell->mod_notice;
            return 0;
        }
    }
    /* A new installation has no mod folder yet. Opening its manager creates
     * only this directory; discovery and validation remain read-only. */
    (void)SDL_CreateDirectory(shell->mods_directory);
    portable_mod_manager_free(shell->mod_manager);
    const PortableModRoots roots=game_shell_mod_roots(shell);
    shell->mod_manager=portable_mod_manager_create_roots(&roots,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.mods,shell->settings.mod_count);
    if(shell->mod_manager==NULL){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Cannot allocate mod manager");
        shell->options_notice=shell->mod_notice;
        return 0;
    }
    portable_mod_manager_set_sources(shell->mod_manager,(const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.asset_sources);
    shell->mod_cursor=0u;shell->mod_order=0u;shell->mod_detail_page=0u;
    shell->mod_action_open=0u;shell->mod_action_cursor=0u;
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",portable_mod_manager_notice(shell->mod_manager));
    shell->options_open=4;
    return 1;
}

static int game_shell_mods_dirty(const GameShell *shell)
{
    if (shell->mod_manager == NULL) return 0;
    for(unsigned kind=0;kind<PORTABLE_ASSET_SOURCE_COUNT;++kind)
        if(strcmp(portable_mod_manager_source(shell->mod_manager,kind),shell->settings.asset_sources[kind]))return 1;
    if (portable_mod_manager_count(shell->mod_manager) != shell->settings.mod_count) return 1;
    for (size_t i = 0u; i < shell->settings.mod_count; ++i)
        if (strcmp(portable_mod_manager_id(shell->mod_manager,i),shell->settings.mods[i]) != 0)
            return 1;
    return 0;
}

/* Targets: enhancements, leave Options, quit, back from the load order to
 * the list. A staged selection survives category changes; only an explicit
 * decision may discard it. */
static void game_shell_discard_install(GameShell *shell);
static void game_shell_mod_exit(GameShell *shell, uint8_t target)
{
    if (target == 4u && !game_shell_mods_dirty(shell)) {
        if (shell->mod_manager == NULL) (void)game_shell_open_mod_manager(shell);
        shell->mod_order = 0u;
        /* Back on the Load order row it was opened from (the list's top
         * when the Select shortcut opened it with Advanced hidden). */
        shell->mod_cursor = shell->mod_advanced
            ? (unsigned)(portable_mod_menu_row_count(shell->mod_manager, 1) - PORTABLE_MOD_MENU_ADVANCED_ACTIONS)
            : 0u;
        shell->mod_detail_page = 0u;
        shell->mod_action_open = 0u;
        shell->mod_exit_target = 0u;
        shell->options_open = shell->library_return ? 7 : 4;
    } else if (game_shell_mods_dirty(shell)) {
        game_shell_discard_install(shell);
        shell->mod_dialog_folder=0u;
        game_shell_discard_mod_candidate(shell);
        shell->mod_unsaved_open = 1u;
        shell->mod_unsaved_cursor = 0u;
        shell->mod_exit_target = target;
        shell->mod_action_open = 0u;
        shell->options_open = 4;
        puts("mod changes pending: choose Apply, Discard or Keep editing");
    } else {
        shell->options_open = target == 1u ? 3 : 0;
        shell->quit = target == 3u;
        portable_mod_manager_free(shell->mod_manager);
        shell->mod_manager = NULL;
        shell->mod_exit_target = 0u;
    }
    shell->wait_for_neutral = 1;
    shell->needs_present = 1;
}

static uint64_t game_shell_mod_path_stamp(uint64_t hash,const char *path)
{
    SDL_PathInfo info={0};
    const uint64_t exists=(uint64_t)portable_source_path_info(path,&info);
    const uint64_t values[]={exists,
        (uint64_t)info.modify_time,info.size,(uint64_t)info.type};
    for (size_t i=0u;i<sizeof values/sizeof values[0];++i)
        hash=(hash^values[i])*UINT64_C(1099511628211);
    return hash;
}

/* A bounded metadata scan of known sources, never recursive file reads. Root
 * directory stamps detect additions; fresh discovery follows before loading. */
static void game_shell_watch_mods(GameShell *shell)
{
    if (!shell->watch_mods || shell->editor != NULL || shell->selected_gp != NULL || shell->selected_trial != NULL ||
        shell->selected_ceremony != NULL || portable_course_3d_host_active(shell->preview_3d) ||
        shell->record_file != NULL || (shell->audio != NULL && shell->audio->capture_file != NULL) ||
        shell->store_open || shell->binding_capture || shell->mod_dialog != NULL ||
        shell->mod_unsaved_open || shell->mod_apply_pending || shell->options_open >= 5 ||
        game_shell_mods_dirty(shell)) return;
    const uint64_t now=SDL_GetTicks();
    if (now-shell->mod_watch_poll < 250u) return;
    shell->mod_watch_poll=now;
    const PortableModRoots roots=game_shell_mod_roots(shell);
    if (roots.directories[0][0]=='\0') return;
    for(size_t root=0;root<roots.count;++root)
        if(portable_source_path_is_uri(roots.directories[root])){
            if(!shell->mod_watch_saf_warning){
                fputs("mod watch: picker-backed folders require Reload from disk or F5\n",stderr);
                snprintf(shell->mod_notice,sizeof shell->mod_notice,
                    "Picker folders: use Reload from disk or F5 after editing.");
                shell->mod_watch_saf_warning=1u;
                shell->needs_present=1;
            }
            shell->mod_watch_initialized=0u;
            shell->mod_watch_changed=0u;
            return;
        }
    shell->mod_watch_saf_warning=0u;
    (void)SDL_CreateDirectory(roots.directories[0]);
    if (shell->mod_manager==NULL)
        shell->mod_manager=portable_mod_manager_create_roots(&roots,
            (const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.mods,shell->settings.mod_count);
    const PortableModCatalog *catalog=portable_mod_manager_catalog(shell->mod_manager);
    if (catalog==NULL) return;
    uint64_t stamp=UINT64_C(14695981039346656037);
    for (size_t i=0u;i<roots.count;++i)
        stamp=game_shell_mod_path_stamp(stamp,roots.directories[i]);
    char path[4096],relative[4096];
    for (size_t i=0u;i<catalog->count;++i) {
        const PortableModCatalogEntry *entry=&catalog->entries[i];
        /* Invalid/ambiguous rows still need to notice an in-place repair;
         * their decoded manifest cannot identify a reliable source yet. */
        for (size_t source=0u;source<roots.count;++source) {
            const int manifest_size=snprintf(relative,sizeof relative,"%s/mod.json",entry->id);
            if (manifest_size>0 && (size_t)manifest_size<sizeof relative &&
                portable_source_path_resolve(roots.directories[source],relative,path,sizeof path)==1) stamp=game_shell_mod_path_stamp(stamp,path);
            const int zip_size=snprintf(relative,sizeof relative,"%s.zip",entry->id);
            if (zip_size>0 && (size_t)zip_size<sizeof relative &&
                portable_source_path_resolve(roots.directories[source],relative,path,sizeof path)==1) stamp=game_shell_mod_path_stamp(stamp,path);
        }
        if (!entry->valid) continue;
        if (entry->source_root >= roots.count) continue;
        const char *root=roots.directories[entry->source_root];
        const char *files[]={"mod.json",entry->manifest.pack,entry->manifest.script};
        if (entry->from_zip) {
            const int size=snprintf(relative,sizeof relative,"%s.zip",entry->id);
            if (size>0 && (size_t)size<sizeof relative &&
                portable_source_path_resolve(root,relative,path,sizeof path)==1) stamp=game_shell_mod_path_stamp(stamp,path);
        } else for (size_t file=0u;file<3u;++file) {
            if (files[file][0]=='\0') continue;
            const int size=snprintf(relative,sizeof relative,"%s/%s",entry->id,files[file]);
            if (size>0 && (size_t)size<sizeof relative &&
                portable_source_path_resolve(root,relative,path,sizeof path)==1) stamp=game_shell_mod_path_stamp(stamp,path);
        }
    }
    if (!shell->mod_watch_initialized || stamp!=shell->mod_watch_signature) {
        shell->mod_watch_signature=stamp;
        shell->mod_watch_changed=shell->mod_watch_initialized?now:0u;
        shell->mod_watch_initialized=1u;
    } else if (shell->mod_watch_changed!=0u && now-shell->mod_watch_changed >= 500u) {
        shell->mod_watch_changed=0u;
        if (game_shell_open_mod_manager(shell)) shell->mod_apply_pending=3u;
    }
}

static GameShell *game_shell_prepare_replacement_width(const GameShell *shell,
    const PortableAssetPackView *pack,const PortableAssetPackView *pack_3d,const PortableModSet *mods,
    size_t menu_width,char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    char progress_suffix[48];
    if(!portable_driving_profile_progress_suffix(pack,shell->settings.mirror,progress_suffix)){
        snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"Selected modern drift content is invalid");
        return NULL;
    }
    GameShell *prepared=calloc(1u,sizeof *prepared);
    uint32_t *pixels=malloc(menu_width*160u*sizeof *pixels);
    if(prepared==NULL || pixels==NULL){
        free(prepared);free(pixels);
        snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"Cannot allocate menu validation owners");
        return NULL;
    }
    prepared->base_pack=shell->base_pack;
    prepared->base_pack_3d=shell->base_pack_3d;
    prepared->pack=pack;
    prepared->pack_3d=pack_3d;
    prepared->mods=mods;

    prepared->settings=shell->settings;
    prepared->gp_retry_credits=3u;
    prepared->scratch=calloc(1u,sizeof *prepared->scratch);
    int failed=prepared->scratch==NULL || game_shell_prepare_content(prepared,progress_suffix);
    prepared->flow.short_transitions=menu_short_transitions(&prepared->settings);
    if(!failed){
        game_shell_prepare_audio(prepared);
        game_shell_start_audio(prepared);
        failed=prepared->error;
    }
    if(!failed){
        prepared->scratch->extended_backgrounds=shell->settings.wide_ui_backgrounds;
        prepared->scratch->title_background_pack=shell->ui_background.bytes?&shell->ui_background.view:NULL;
        PortableSoftwareFrame frame={.pixels=pixels,.width=menu_width,.height=160u,.ui_text=&prepared->ui_text};
        failed=portable_frontend_presentation_render(prepared->pack,&prepared->flow,&frame,
            prepared->scratch)!=PORTABLE_FRONTEND_PRESENTATION_OK;
        prepared->scratch->title_background_pack=NULL;
    }
    free(pixels);
    if(failed){
        snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"Candidate menu/audio content failed: %s",
            prepared->failure_detail?prepared->failure_detail:"invalid frontend");
        portable_course_3d_host_close(prepared->preview_3d);
        free(prepared->audio);free(prepared->scratch);free(prepared);
        return NULL;
    }
    return prepared;
}

static size_t game_shell_prepare_width(const GameShell *shell)
{
    return shell->settings.wide_ui_backgrounds
        ? portable_software_frame_window_width(shell->renderer, shell->settings.aspect_width) : 240u;
}
static GameShell *game_shell_prepare_replacement(const GameShell *shell,
    const PortableAssetPackView *pack, const PortableAssetPackView *pack_3d,
    const PortableModSet *mods, char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    return game_shell_prepare_replacement_width(shell, pack, pack_3d, mods,
                                                game_shell_prepare_width(shell), error);
}

typedef struct {
    GameShell *shell;
    uint8_t digest[32];
    int validated;
} InstallPreflight;

static int game_shell_validate_install_pack(const PortableAssetPackView *pack,void *context,
    char error[PORTABLE_MOD_ERROR_CAPACITY])
{
    InstallPreflight *check=context;
    if(check->validated && memcmp(check->digest,pack->deterministic_digest,sizeof check->digest)==0)return 1;
    GameShell *prepared=game_shell_prepare_replacement(check->shell,pack,pack,NULL,error);
    if(prepared==NULL)return 0;
    /* Decode every selectable course/driver combination with the ordinary race
     * and artwork decoders. This reads content without advancing a simulation. */
    PortableRecoveredPackStorage *race_storage=calloc(1u,sizeof *race_storage);
    PortablePresentationPackStorage *art_storage=calloc(1u,sizeof *art_storage);
    int valid=race_storage!=NULL && art_storage!=NULL;
    if(!valid)snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"Cannot allocate archive content preflight");
    for(size_t course=0u;valid && course<prepared->config.course_count;++course){
        if(!prepared->courses[course].available)continue;
        for(size_t driver=0u;valid && driver<prepared->config.driver_count;++driver){
            if(!prepared->drivers[driver].available)continue;
            PortableContentSelection selection;
            PortableRecoveredRaceView race;
            PortablePresentationPackView art;
            valid=portable_content_registry_select(pack,prepared->courses[course].content_id,
                prepared->drivers[driver].content_id,&selection)==PORTABLE_CONTENT_OK &&
                portable_recovered_pack_open_race(pack,&selection,race_storage,&race)==PORTABLE_RECOVERED_PACK_OK &&
                portable_presentation_pack_open(pack,&selection,race_storage,&race,art_storage,&art)==PORTABLE_RECOVERED_PACK_OK;
            if(!valid)snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"Invalid race content: course %u driver %u",
                prepared->courses[course].content_id,prepared->drivers[driver].content_id);
        }
    }
    free(art_storage);free(race_storage);
    const size_t width=check->shell->settings.wide_ui_backgrounds?
        portable_software_frame_window_width(check->shell->renderer,check->shell->settings.aspect_width):240u;
    uint32_t *pixels=malloc(width*160u*sizeof *pixels);
    if(pixels==NULL){valid=0;snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"Cannot allocate archive menu preflight");}
    const PortableFrontendPage pages[]={PORTABLE_FRONTEND_PAGE_TITLE_INTRO,PORTABLE_FRONTEND_PAGE_TITLE_MENU,
        PORTABLE_FRONTEND_PAGE_MODE,PORTABLE_FRONTEND_PAGE_TIME_TRIAL,PORTABLE_FRONTEND_PAGE_CHARACTER,
        PORTABLE_FRONTEND_PAGE_COURSE,PORTABLE_FRONTEND_PAGE_RECORDS};
    for(size_t page=0u;valid && page<sizeof pages/sizeof pages[0];++page){
        PortableFrontendFlow flow=prepared->flow;
        flow.page=flow.pending_page=pages[page];flow.page_ticks=120u;flow.transition_ticks=0u;
        PortableSoftwareFrame frame={.pixels=pixels,.width=width,.height=160u,.ui_text=&prepared->ui_text};
        valid=portable_frontend_presentation_render(pack,&flow,&frame,prepared->scratch)==PORTABLE_FRONTEND_PRESENTATION_OK;
        if(!valid)snprintf(error,PORTABLE_MOD_ERROR_CAPACITY,"Invalid menu content: page %u",(unsigned)pages[page]);
    }
    portable_course_3d_host_close(prepared->preview_3d);
    free(pixels);free(prepared->audio);free(prepared->scratch);free(prepared);
    if(valid){memcpy(check->digest,pack->deterministic_digest,sizeof check->digest);check->validated=1;}
    return valid;
}

/* Validates, saves and loads the staged selection in one step (no review). */
static void game_shell_apply_now(GameShell *shell,const char *label)
{
    snprintf(shell->mod_apply_label,sizeof shell->mod_apply_label,"%s",label);
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"Loading: %s",label);
    shell->mod_notice_error=0u;
    shell->mod_apply_pending=4u;
    shell->needs_present=1;shell->wait_for_neutral=1;
}

static const char *game_shell_mod_name(const GameShell *shell,const char *id)
{
    const PortableModCatalog *catalog=portable_mod_manager_catalog(shell->mod_manager);
    for(size_t i=0u;catalog!=NULL && i<catalog->count;++i)
        if(strcmp(catalog->entries[i].id,id)==0 && catalog->entries[i].valid &&
           catalog->entries[i].manifest.name[0]!='\0')return catalog->entries[i].manifest.name;
    return id;
}

/* Turns a mod on (with the dependencies it needs) or off (with mods needing
 * it) and loads the result right away. */
static void game_shell_toggle_now(GameShell *shell,const char *id)
{
    char label[128];
    const char *name=game_shell_mod_name(shell,id);
    if(portable_mod_manager_index(shell->mod_manager,id)>=0){
        const size_t removed=portable_mod_manager_disable(shell->mod_manager,id);
        if(removed>1u)snprintf(label,sizeof label,"Turning off %s and %u mods that need it",name,(unsigned)(removed-1u));
        else snprintf(label,sizeof label,"Turning off %s",name);
    }else if(portable_mod_manager_enable(shell->mod_manager,id))
        snprintf(label,sizeof label,"Turning on %s",name);
    else{
        shell->mod_notice_error=1u;
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",portable_mod_manager_notice(shell->mod_manager));
        fprintf(stderr,"mod selection rejected: %s\n",shell->mod_notice);
        return;
    }
    game_shell_apply_now(shell,label);
}

static void game_shell_discard_install(GameShell *shell)
{
    portable_file_dialog_free(shell->mod_dialog);shell->mod_dialog=NULL;
    shell->mod_dialog_game=0u;
    portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;
}

/* Install is one step: validate the archive with the current selection,
 * replace or add the ZIP, turn it on and load it. expected_id/version (the
 * Store's listing) must match before anything is written. */
static void game_shell_install(GameShell *shell,const char *path,const char *expected_id,const char *expected_version)
{
    if(shell->options_open!=4 || shell->mod_dialog!=NULL || shell->mod_unsaved_open)return;
    portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;
    char ids[PORTABLE_MOD_LIMIT][PORTABLE_MOD_ID_CAPACITY]={{0}};
    const size_t count=portable_mod_manager_count(shell->mod_manager);
    for(size_t i=0u;i<count;++i)
        snprintf(ids[i],sizeof ids[i],"%s",portable_mod_manager_id(shell->mod_manager,i));
    const PortableModRoots roots=game_shell_mod_roots(shell);
    shell->mod_install=portable_mod_install_prepare_roots_domains(shell->base_pack,shell->base_pack_3d,&roots,path,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,count,shell->mod_notice);
    InstallPreflight check={.shell=shell};
    if(shell->mod_install!=NULL && !portable_mod_install_validate_roots_domains_with(shell->mod_install,
        shell->base_pack,shell->base_pack_3d,&roots,(const char (*)[PORTABLE_MOD_ID_CAPACITY])ids,count,
        game_shell_validate_install_pack,&check,shell->mod_notice)){
        portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;
    }
    /* A staged edit may omit a currently enabled mod. An update must also
     * preserve both the running selection and the list used on relaunch. */
    const PortableSettings *selections[]={&shell->settings,&shell->persisted_settings};
    for(size_t selection=0u;selection<2u && shell->mod_install!=NULL;++selection){
        const PortableSettings *settings=selections[selection];
        const char *id=portable_mod_install_manifest(shell->mod_install)->id;
        int enabled=0;
        for(size_t i=0u;i<settings->mod_count;++i)enabled|=strcmp(settings->mods[i],id)==0;
        if(enabled && !portable_mod_install_validate_roots_domains_with(shell->mod_install,shell->base_pack,shell->base_pack_3d,&roots,
            (const char (*)[PORTABLE_MOD_ID_CAPACITY])settings->mods,settings->mod_count,game_shell_validate_install_pack,&check,shell->mod_notice)){
            portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;
        }
    }
    const PortableModManifest *manifest=portable_mod_install_manifest(shell->mod_install);
    if(manifest!=NULL && expected_id!=NULL &&
       (strcmp(manifest->id,expected_id)!=0 || strcmp(manifest->version,expected_version)!=0)){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Archive manifest differs from store ID/version; nothing installed");
        portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;manifest=NULL;
    }
    shell->mod_detail_page=0u;
    shell->needs_present=1;shell->wait_for_neutral=1;
    shell->mod_notice_error=1u;
    if(manifest==NULL){
        fprintf(stderr,"mod archive rejected: %s\n",shell->mod_notice);
        portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;
        return;
    }
    char id[PORTABLE_MOD_ID_CAPACITY],label[128];
    snprintf(id,sizeof id,"%s",manifest->id);
    snprintf(label,sizeof label,"Installing %s %s",manifest->name[0]?manifest->name:manifest->id,manifest->version);
    if(!portable_mod_install_publish(shell->mod_install,shell->mod_notice)){
        fprintf(stderr,"mod archive installation failed: %s\n",shell->mod_notice);
        portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;
        return;
    }
    printf("mod archive installed: %s %s\n",manifest->id,manifest->version);
    portable_mod_install_free(shell->mod_install);shell->mod_install=NULL;
    (void)portable_mod_manager_refresh(shell->mod_manager);
    modern_menu_host_content_changed(&shell->modern);
    if(!portable_mod_manager_enable(shell->mod_manager,id)){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Installed %s but cannot turn it on: %s",
            id,portable_mod_manager_notice(shell->mod_manager));
        fprintf(stderr,"mod selection rejected: %s\n",shell->mod_notice);
        return;
    }
    game_shell_apply_now(shell,label);
}

static void game_shell_add_mod_folder(GameShell *shell,const char *path)
{
    shell->mod_notice_error=1u;
    if (shell->mod_unsaved_open || game_shell_mods_dirty(shell)) {
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Apply or discard staged changes before adding a folder.");
        return;
    }
    if (shell->settings.mod_directory_count >= 4u || strlen(path) >= sizeof shell->settings.mod_directories[0]) {
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"At most four extra folders; paths must fit 4095 bytes.");
        return;
    }
    PortableModRoots roots=game_shell_mod_roots(shell);
    for (size_t i=0u;i<shell->settings.mod_directory_count;++i)
        if (strcmp(path,shell->settings.mod_directories[i])==0) {
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"Folder is already remembered.");
            return;
        }
    /* A temporary --mods-dir primary can be remembered for future launches
     * without searching that same directory twice in this launch. */
    if (strcmp(path,shell->mods_directory)!=0) roots.directories[roots.count++]=path;
    if (!portable_mod_roots_valid(&roots)) {
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Folder is already present or its path is invalid.");
        return;
    }
    PortableModManager *manager=portable_mod_manager_create_roots(&roots,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.mods,shell->settings.mod_count);
    portable_mod_manager_set_sources(manager,(const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.asset_sources);
    PortableModManagerCandidate *candidate=manager?portable_mod_manager_prepare_domains(manager,
        shell->base_pack,shell->base_pack_3d,NULL):NULL;
    if (candidate==NULL) {
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",manager?
            portable_mod_manager_notice(manager):"Cannot allocate folder validation");
        portable_mod_manager_free(manager);
        return;
    }
    portable_mod_manager_candidate_free(candidate);
    PortableModManager *persisted=portable_mod_manager_create_roots(&roots,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->persisted_settings.mods,
        shell->persisted_settings.mod_count);
    portable_mod_manager_set_sources(persisted,(const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->persisted_settings.asset_sources);
    candidate=persisted?portable_mod_manager_prepare_domains(persisted,
        shell->base_pack,shell->base_pack_3d,NULL):NULL;
    if (candidate==NULL) {
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",persisted?
            portable_mod_manager_notice(persisted):"Cannot validate saved selection in new folder");
        portable_mod_manager_free(persisted);portable_mod_manager_free(manager);return;
    }
    portable_mod_manager_candidate_free(candidate);portable_mod_manager_free(persisted);
    PortableSettings *saved=malloc(sizeof *saved);
    if (saved==NULL) {portable_mod_manager_free(manager);return;}
    *saved=shell->persisted_settings;
    saved->mod_directory_count=shell->settings.mod_directory_count+1u;
    memcpy(saved->mod_directories,shell->settings.mod_directories,sizeof saved->mod_directories);
    snprintf(saved->mod_directories[saved->mod_directory_count-1u],sizeof saved->mod_directories[0],"%s",path);
    if (shell->settings_path[0]=='\0' || !portable_settings_save(shell->settings_path,saved)) {
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Cannot save folder; previous folders remain active.");
        portable_mod_manager_free(manager);free(saved);return;
    }
    shell->persisted_settings=*saved;
    shell->settings.mod_directory_count=saved->mod_directory_count;
    memcpy(shell->settings.mod_directories,saved->mod_directories,sizeof saved->mod_directories);
    free(saved);
    portable_mod_manager_free(shell->mod_manager);shell->mod_manager=manager;
    shell->mod_notice_error=0u;
    shell->mod_cursor=0u;shell->mod_detail_page=0u;
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"Folder remembered. Turn its mods on in the list.");
    printf("mod folder remembered: %s\n",path);
}

/* Native choosers are modal on some hosts; automated runs drop files instead
 * and fail here rather than wait for a person. */
static int game_shell_chooser_allowed(GameShell *shell)
{
    if (shell->aid == NULL) return 1;
    shell->failure_detail = "native file chooser requested in an automated run";
    shell->error = 1;
    return 0;
}

static int game_shell_names_game_rom(const char *path)
{
    const char *dot=strrchr(path,'.');
    return dot && (!SDL_strcasecmp(dot,".nds") || !SDL_strcasecmp(dot,".z64") ||
                   !SDL_strcasecmp(dot,".v64") || !SDL_strcasecmp(dot,".n64"));
}

/* Another game's content becomes a generated mod in the primary folder. */
static const PortableModCatalogEntry *game_shell_imported_game(const GameShell *shell,const char *id)
{
    const PortableModCatalog *catalog=portable_mod_manager_catalog(shell->mod_manager);
    for(size_t i=0u;catalog!=NULL && i<catalog->count;++i)
        if(strcmp(catalog->entries[i].id,id)==0)return &catalog->entries[i];
    return NULL;
}

static void game_shell_start_game_import(GameShell *shell,const char *path)
{
    if(shell->game_import!=NULL || (shell->options_open!=4 && shell->options_open!=7))return;
    shell->game_import=portable_game_import_start(path,shell->mods_directory);
    shell->mod_notice_error=shell->game_import==NULL;
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",shell->game_import?
        "Reading the game. B cancels.":"Cannot start the import.");
    if(shell->game_import)printf("game import started: %s\n",path);
    shell->mod_detail_page=0u;shell->needs_present=1;shell->wait_for_neutral=1;
}

/* Import row (id NULL: any imported game) or a Games card: an older
 * importer's mod is updated from that game's remembered ROM while it still
 * exists; otherwise the player picks a DS or N64 ROM, raw or zipped. */
static void game_shell_choose_game(GameShell *shell,const char *id)
{
    static const char *const games[]={MKSC_MKDS_MOD_ID,MKSC_MK64_MOD_ID};
    for(unsigned i=0;i<2u;++i){
        if(id!=NULL && strcmp(id,games[i])!=0)continue;
        const PortableModCatalogEntry *entry=game_shell_imported_game(shell,games[i]);
        char version[32],path[4096];
        portable_game_import_version(games[i],version,sizeof version);
        SDL_PathInfo info={0};
        if(entry!=NULL && entry->valid && strcmp(entry->manifest.version,version)!=0 &&
            portable_game_import_stored_rom(games[i],path,sizeof path) && portable_source_path_info(path,&info) &&
            info.type==SDL_PATHTYPE_FILE){
            game_shell_start_game_import(shell,path);
            return;
        }
    }
    shell->mod_dialog_folder=0u;
    if(!game_shell_chooser_allowed(shell))return;
    shell->mod_dialog=portable_file_dialog_choose(shell->window,"Nintendo DS / Nintendo 64 ROMs","nds;z64;v64;n64;zip");
    shell->mod_dialog_game=shell->mod_dialog!=NULL;
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",shell->mod_dialog?
        "Choose your Mario Kart DS or Mario Kart 64 (US) ROM. B cancels.":"File chooser unavailable; drop a .nds, .z64, .v64 or .n64 ROM here.");
}

static void game_shell_poll_game_import(GameShell *shell)
{
    if(shell->game_import==NULL)return;
    uint32_t done=0u,total=0u;
    char message[PORTABLE_MOD_ERROR_CAPACITY];
    const int state=portable_game_import_poll(shell->game_import,&done,&total,message,sizeof message);
    if(state==PORTABLE_GAME_IMPORT_RUNNING){
        char progress[PORTABLE_MOD_ERROR_CAPACITY];
        snprintf(progress,sizeof progress,"Importing game: step %u of %u. B cancels.",
            (unsigned)done,(unsigned)(total?total:1u));
        if(strcmp(progress,shell->mod_notice)!=0){
            memcpy(shell->mod_notice,progress,sizeof progress);shell->needs_present=1;
        }
        return;
    }
    char id[PORTABLE_MOD_ID_CAPACITY]="";
    if(portable_game_import_mod_id(shell->game_import)!=NULL)
        snprintf(id,sizeof id,"%s",portable_game_import_mod_id(shell->game_import));
    portable_game_import_free(shell->game_import);shell->game_import=NULL;
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",message);
    shell->mod_notice_error=state==0;
    printf("game import %s: %s\n",state==1?"finished":state<0?"cancelled":"failed",message);
    shell->mod_detail_page=0u;shell->needs_present=1;
    if(state==1){
        (void)portable_mod_manager_refresh(shell->mod_manager);modern_menu_host_content_changed(&shell->modern);
        /* The imported game is turned on (or reloaded after an update) now. */
        char label[160];
        snprintf(label,sizeof label,"Loading %s",game_shell_mod_name(shell,id));
        if(id[0]!='\0' && portable_mod_manager_enable(shell->mod_manager,id))game_shell_apply_now(shell,label);
        else if(id[0]!='\0'){
            shell->mod_notice_error=1u;
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"Imported %s but cannot turn it on: %s",
                id,portable_mod_manager_notice(shell->mod_manager));
        }
    }
}

static void game_shell_store_load(GameShell *shell)
{
    if(shell->store_job)return;
    if(!SDL_CreateDirectory(shell->mods_directory)){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Cannot create mods directory");return;
    }
    shell->store_loading=calloc(1,sizeof *shell->store_loading);
    if(!shell->store_loading)return;
    const char *url=shell->store_source?shell->settings.store_urls[shell->store_source-1u]:"builtin://official";
    shell->store_job=portable_store_job_load(url,shell->mods_directory,shell->store_loading);
    if(!shell->store_job){free(shell->store_loading);shell->store_loading=NULL;}
    shell->store_cursor=0u;shell->store_details=0u;
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",shell->store_job?"Loading catalog...":"Cannot start store worker");
}
static void game_shell_store_add(GameShell *shell,const char *url)
{
    if(shell->store_job)return;
    if(!portable_store_url_valid(url)||!strcmp(url,"builtin://official")){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Copy an HTTPS index URL, or drop a local index.json onto Store.");return;
    }
    PortableSettings *saved=malloc(sizeof *saved);
    if(!saved)return;
    *saved=shell->persisted_settings;
    unsigned i=0;while(i<saved->store_url_count&&strcmp(saved->store_urls[i],url))++i;
    if(i==saved->store_url_count){
        if(i==4u){free(saved);snprintf(shell->mod_notice,sizeof shell->mod_notice,"Four added stores maximum. Forget one first.");return;}
        snprintf(saved->store_urls[i],sizeof saved->store_urls[i],"%s",url);++saved->store_url_count;
    }
    if(!shell->settings_path[0]||!portable_settings_save(shell->settings_path,saved)){
        free(saved);snprintf(shell->mod_notice,sizeof shell->mod_notice,"Cannot save store URL; settings unchanged");return;
    }
    shell->persisted_settings=*saved;
    shell->settings.store_url_count=saved->store_url_count;
    memcpy(shell->settings.store_urls,saved->store_urls,sizeof saved->store_urls);
    free(saved);shell->store_source=i+1u;shell->store_open=1u;game_shell_store_load(shell);
}
static void game_shell_store_poll(GameShell *shell)
{
    if(!shell->store_job)return;
    const int status=portable_store_job_poll(shell->store_job,shell->mod_notice);
    if(!status)return;
    portable_store_job_free(shell->store_job);shell->store_job=NULL;
    shell->mod_notice_error=status<0;
    if(status<0)fprintf(stderr,"store: %s\n",shell->mod_notice);
    if(shell->store_downloading){
        shell->store_downloading=0u;
        if(status>0){
            const PortableStoreEntry *e=&shell->store->entries[shell->store_cursor];
            game_shell_install(shell,shell->store_download_path,e->id,e->version);
        }
        remove(shell->store_download_path);
    }else{
        if(status>0){free(shell->store);shell->store=shell->store_loading;shell->store_loading=NULL;
            printf("store catalog: %s (%u mods)%s\n",shell->store->id,shell->store->count,shell->store->offline?" offline":"");}
        free(shell->store_loading);shell->store_loading=NULL;
        if(status<0){
            shell->store_source=0u;
            for(unsigned i=0;shell->store&&i<shell->settings.store_url_count;++i)
                if(!strcmp(shell->store->source,shell->settings.store_urls[i]))shell->store_source=i+1u;
        }
    }
    shell->needs_present=1;shell->wait_for_neutral=1;
}
/* Deletes an installed primary-folder ZIP that no selection uses. */
static int game_shell_store_remove_archive(GameShell *shell,const char *id)
{
    const PortableSettings *sets[]={&shell->settings,&shell->persisted_settings};
    for(unsigned k=0;k<2u;++k)for(unsigned i=0;i<sets[k]->mod_count;++i)if(!strcmp(id,sets[k]->mods[i])){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s is still on; nothing removed.",id);
        shell->mod_notice_error=1u;return 0;
    }
    char path[4096];int n=snprintf(path,sizeof path,"%s/%s.zip",shell->mods_directory,id);
    if(n<0||(size_t)n>=sizeof path||remove(path)!=0){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Could not remove archive");
        shell->mod_notice_error=1u;return 0;
    }
    printf("store removed: %s\n",id);
    (void)portable_mod_manager_refresh(shell->mod_manager);
    modern_menu_host_content_changed(&shell->modern);
    snprintf(shell->mod_notice,sizeof shell->mod_notice,"Archive removed");
    shell->mod_notice_error=0u;
    return 1;
}
static void game_shell_store_remove(GameShell *shell)
{
    const char *id=shell->store->entries[shell->store_cursor].id;
    const PortableModCatalog *catalog=portable_mod_manager_catalog(shell->mod_manager);
    const PortableModCatalogEntry *installed=NULL;
    for(size_t i=0;catalog&&i<catalog->count;++i)if(!strcmp(id,catalog->entries[i].id))installed=&catalog->entries[i];
    if(!installed||!installed->from_zip||installed->source_root){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Only ZIPs in the primary mods folder can be removed here.");return;
    }
    if(!shell->store_remove_confirm){shell->store_remove_confirm=1u;
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Remove this installed ZIP? Confirm again, or Back to cancel.");return;}
    shell->store_remove_confirm=0u;
    int enabled=portable_mod_manager_index(shell->mod_manager,id)>=0;
    for(unsigned i=0;i<shell->settings.mod_count;++i)enabled|=!strcmp(id,shell->settings.mods[i]);
    if(!enabled){(void)game_shell_store_remove_archive(shell,id);return;}
    /* One step: turn it (and mods needing it) off, load that, then remove. */
    if(game_shell_mods_dirty(shell)){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Apply or discard staged load-order changes first.");
        shell->mod_notice_error=1u;return;
    }
    char label[128];
    const char *name=game_shell_mod_name(shell,id);
    const size_t removed=portable_mod_manager_disable(shell->mod_manager,id);
    if(removed>1u)snprintf(label,sizeof label,"Removing %s and turning off %u mods that need it",name,(unsigned)(removed-1u));
    else snprintf(label,sizeof label,"Removing %s",name);
    snprintf(shell->store_remove_pending,sizeof shell->store_remove_pending,"%s",id);
    game_shell_apply_now(shell,label);
}
static void game_shell_store_input(GameShell *shell,uint16_t pressed)
{
    if(shell->store_job)return;
    const unsigned count=shell->store?shell->store->count:0u;
    if(pressed&PORTABLE_FRONTEND_BUTTON_B){
        if(shell->store_remove_confirm)shell->store_remove_confirm=0u;
        else if(shell->store_details)shell->store_details=0u;
        else shell->store_open=0u;
        shell->mod_notice[0]='\0';return;
    }
    if(shell->store_details){
        if(pressed&PORTABLE_FRONTEND_BUTTON_RIGHT)++shell->mod_detail_page;
        if((pressed&PORTABLE_FRONTEND_BUTTON_LEFT)&&shell->mod_detail_page)--shell->mod_detail_page;
        if(pressed&PORTABLE_FRONTEND_BUTTON_UP){shell->store_action=0u;shell->store_remove_confirm=0u;}
        if(pressed&PORTABLE_FRONTEND_BUTTON_DOWN)shell->store_action=1u;
        if(pressed&PORTABLE_FRONTEND_BUTTON_A){
            if(shell->store_action)game_shell_store_remove(shell);
            else {
                int n=snprintf(shell->store_download_path,sizeof shell->store_download_path,"%s/.store-download.zip",shell->mods_directory);
                if(n<0||(size_t)n>=sizeof shell->store_download_path)return;
                shell->store_job=portable_store_job_download(shell->store,shell->store_cursor,shell->store_download_path);
                shell->store_downloading=shell->store_job!=NULL;
                snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",shell->store_job?"Downloading and verifying archive...":"Cannot start download");
            }
        }
        return;
    }
    if((pressed&PORTABLE_FRONTEND_BUTTON_UP)&&shell->store_cursor)--shell->store_cursor;
    else if((pressed&PORTABLE_FRONTEND_BUTTON_DOWN)&&shell->store_cursor<count+3u)++shell->store_cursor;
    else if(pressed&PORTABLE_FRONTEND_BUTTON_A){
        if(shell->store_cursor<count){shell->store_details=1u;shell->store_action=0u;shell->mod_detail_page=0u;shell->mod_notice[0]='\0';}
        else if(shell->store_cursor==count){
            shell->store_source=(shell->store_source+1u)%(shell->settings.store_url_count+1u);game_shell_store_load(shell);
        }else if(shell->store_cursor==count+1u)game_shell_store_load(shell);
        else if(shell->store_cursor==count+2u){
            char *url=SDL_GetClipboardText();game_shell_store_add(shell,url);SDL_free(url);
        }else if(shell->store_source){
            PortableSettings *saved=malloc(sizeof *saved);if(!saved)return;
            *saved=shell->persisted_settings;
            unsigned i=shell->store_source-1u;
            for(;i+1u<saved->store_url_count;++i)memcpy(saved->store_urls[i],saved->store_urls[i+1u],sizeof saved->store_urls[i]);
            --saved->store_url_count;
            if(portable_settings_save(shell->settings_path,saved)){
                shell->persisted_settings=*saved;shell->settings.store_url_count=saved->store_url_count;
                memcpy(shell->settings.store_urls,saved->store_urls,sizeof saved->store_urls);
                shell->store_source=0u;game_shell_store_load(shell);
            }else snprintf(shell->mod_notice,sizeof shell->mod_notice,"Cannot save settings; store kept");
            free(saved);
        }else snprintf(shell->mod_notice,sizeof shell->mod_notice,"The built-in store is always available");
    }
}

static void game_shell_poll_install(GameShell *shell)
{
    game_shell_store_poll(shell);
    game_shell_poll_game_import(shell);
    if(shell->options_open==7 && portable_mod_manager_preview_poll(shell->mod_manager))shell->needs_present=1;
    if(shell->mod_dialog==NULL)return;
    char path[4096];const int state=portable_file_dialog_poll(shell->mod_dialog,path,sizeof path);
    if(state==0)return;
    portable_file_dialog_free(shell->mod_dialog);shell->mod_dialog=NULL;
    if(state==1) {
        if (shell->mod_dialog_folder) game_shell_add_mod_folder(shell,path);
        else if (shell->mod_dialog_game) game_shell_start_game_import(shell,path);
        else game_shell_install(shell,path,NULL,NULL);
    } else snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",state==2?"Selection cancelled":"Selection failed");
    shell->mod_dialog_folder=0u;shell->mod_dialog_game=0u;
    shell->needs_present=1;
    shell->wait_for_neutral=1;
}

static void game_shell_move_mod(GameShell *shell, int direction)
{
    const PortableModCatalog *catalog = portable_mod_manager_catalog(shell->mod_manager);
    const size_t count = portable_mod_manager_count(shell->mod_manager);
    const char *id = shell->mod_order
        ? portable_mod_manager_id(shell->mod_manager,shell->mod_cursor)
        : catalog && shell->mod_cursor < catalog->count
            ? catalog->entries[shell->mod_cursor].id : NULL;
    const int index = portable_mod_manager_index(shell->mod_manager,id);
    shell->mod_notice[0] = '\0';
    shell->mod_notice_error = 0u;
    if (index < 0) {
        shell->mod_notice_error = 1u;
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Enable this mod before changing order");
        return;
    }
    size_t destination = (size_t)index;
    if (direction < 0 && destination > 0u) --destination;
    if (direction > 0 && destination + 1u < count) ++destination;
    (void)portable_mod_manager_move(shell->mod_manager,(size_t)index,destination);
    if (shell->mod_order) shell->mod_cursor = (unsigned)destination;
}

static unsigned game_shell_mod_detail_pages(const GameShell *shell,
    const PortableModManager *manager, unsigned cursor, int order)
{
    return game_shell_modern_active(shell)
        ? modern_menu_mod_detail_pages(manager,cursor,order,shell->mod_advanced,shell->mod_notice)
        : portable_mod_menu_pages(manager,cursor,order,shell->mod_advanced,shell->mod_notice);
}

static void game_shell_mod_action(GameShell *shell, uint16_t pressed)
{
    if (pressed & PORTABLE_FRONTEND_BUTTON_B) {
        shell->mod_action_open = 0u;
        shell->wait_for_neutral = 1;
    } else if ((pressed & PORTABLE_FRONTEND_BUTTON_UP) && shell->mod_action_cursor > 0u) {
        --shell->mod_action_cursor;
    } else if ((pressed & PORTABLE_FRONTEND_BUTTON_DOWN) && shell->mod_action_cursor < 4u) {
        ++shell->mod_action_cursor;
    } else if (pressed & PORTABLE_FRONTEND_BUTTON_A) {
        if (shell->mod_action_cursor < 2u) {
            game_shell_move_mod(shell,shell->mod_action_cursor == 0u ? -1 : 1);
            shell->mod_detail_page = 0u;
        } else if (shell->mod_action_cursor == 2u) {
            (void)portable_mod_manager_remove(shell->mod_manager,shell->mod_cursor);
            const size_t count = portable_mod_manager_count(shell->mod_manager);
            if (shell->mod_cursor >= count) shell->mod_cursor = count ? (unsigned)count - 1u : 0u;
            shell->mod_detail_page = 0u;
        } else {
            const unsigned pages = game_shell_mod_detail_pages(shell,shell->mod_manager,shell->mod_cursor,1);
            shell->mod_detail_page = (shell->mod_detail_page +
                (shell->mod_action_cursor == 3u ? pages - 1u : 1u)) % pages;
        }
        shell->mod_action_open = 0u;
        shell->wait_for_neutral = 1;
    }
}

static void game_shell_library_input(GameShell *shell,uint16_t pressed)
{
    if(pressed&PORTABLE_FRONTEND_BUTTON_UP){if(shell->library_row)--shell->library_row;}
    else if(pressed&PORTABLE_FRONTEND_BUTTON_DOWN){if(shell->library_row<4)++shell->library_row;}
    else if(pressed&(PORTABLE_FRONTEND_BUTTON_LEFT|PORTABLE_FRONTEND_BUTTON_RIGHT)){
        const int direction=(pressed&PORTABLE_FRONTEND_BUTTON_LEFT)?-1:1;
        if(!shell->library_row){
            const unsigned count=(unsigned)portable_game_library_count();
            shell->library_game=(shell->library_game+count+(direction<0?count-1u:1u))%count;
        }else if(shell->library_row<=PORTABLE_ASSET_SOURCE_COUNT){
            portable_mod_manager_cycle_source(shell->mod_manager,shell->library_row-1u,direction);
            printf("game asset source staged: kind=%u source=%s\n",shell->library_row-1u,
                portable_mod_manager_source(shell->mod_manager,shell->library_row-1u));
        }
        shell->mod_notice[0]=0;
    }else if((pressed&PORTABLE_FRONTEND_BUTTON_START)||
             (shell->library_row==4 && (pressed&PORTABLE_FRONTEND_BUTTON_A))){
        shell->library_return=1;shell->mod_apply_pending=1;
    }else if(!shell->library_row && (pressed&(PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_SELECT))){
        const PortableLibraryGame *game=portable_game_library_at(shell->library_game);
        const PortableModCatalogEntry *entry=portable_game_library_entry(shell->mod_manager,shell->library_game);
        if(game->importer==PORTABLE_GAME_BASE)
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"Super Circuit is the base game and is always available.");
        else if(entry&&entry->valid && !(pressed&PORTABLE_FRONTEND_BUTTON_SELECT)){
            shell->mod_notice_error=!portable_mod_manager_toggle(shell->mod_manager,game->id);
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",shell->mod_notice_error?
                portable_mod_manager_notice(shell->mod_manager):"Selection staged. Apply asset choices to save.");
        }else game_shell_choose_game(shell,game->id);
    }else if(shell->library_row>=1 && shell->library_row<=PORTABLE_ASSET_SOURCE_COUNT && (pressed&PORTABLE_FRONTEND_BUTTON_A))
        portable_mod_manager_cycle_source(shell->mod_manager,shell->library_row-1u,1);
}

static void game_shell_mod_input(GameShell *shell,uint16_t pressed)
{
    if(shell->store_open && shell->options_open==4){game_shell_store_input(shell,pressed);return;}
    if (shell->mod_unsaved_open) {
        if (pressed & PORTABLE_FRONTEND_BUTTON_B) {
            shell->mod_unsaved_open = 0u;
            shell->mod_exit_target = 0u;
            if(shell->library_return)shell->options_open=7;
            puts("mod changes kept for editing");
        } else if ((pressed & PORTABLE_FRONTEND_BUTTON_UP) && shell->mod_unsaved_cursor > 0u)
            --shell->mod_unsaved_cursor;
        else if ((pressed & PORTABLE_FRONTEND_BUTTON_DOWN) && shell->mod_unsaved_cursor < 2u)
            ++shell->mod_unsaved_cursor;
        else if (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START)) {
            shell->mod_unsaved_open = 0u;
            if (shell->mod_unsaved_cursor == 0u) shell->mod_apply_pending = 1u;
            else if (shell->mod_unsaved_cursor == 1u) {
                puts("mod changes discarded");
                portable_mod_manager_free(shell->mod_manager);
                shell->mod_manager = NULL;
                game_shell_mod_exit(shell,shell->mod_exit_target);
            } else {shell->mod_exit_target = 0u;if(shell->library_return)shell->options_open=7;puts("mod changes kept for editing");}
            shell->wait_for_neutral = 1;
        }
        return;
    }
    if (shell->game_import != NULL) {
        if (pressed & PORTABLE_FRONTEND_BUTTON_B) portable_game_import_cancel(shell->game_import);
        return;
    }
    if (shell->mod_action_open && shell->options_open == 4) {
        game_shell_mod_action(shell,pressed);
        return;
    }
    if(pressed & PORTABLE_FRONTEND_BUTTON_B){
        if(shell->mod_dialog!=NULL){
            game_shell_discard_install(shell);shell->options_open=shell->library_return?7:4;
        }else if(shell->options_open==5){
            game_shell_discard_mod_candidate(shell);
            shell->options_open=shell->library_return?7:4;
            shell->mod_exit_target=0u;
        }else if(shell->mod_order && shell->options_open==4){
            game_shell_mod_exit(shell,4u);
        }else{
            game_shell_mod_exit(shell,1u);
            shell->options_notice=NULL;
        }
        shell->mod_notice[0]='\0';
        shell->wait_for_neutral=1;
        return;
    }
    if(shell->mod_dialog!=NULL)return;
    if(shell->options_open==7){game_shell_library_input(shell,pressed);return;}
    const int order=shell->options_open==5 || shell->mod_order;
    const PortableModCatalog *catalog=portable_mod_manager_catalog(shell->mod_manager);
    size_t count=order?portable_mod_manager_count(shell->mod_manager):portable_mod_menu_row_count(shell->mod_manager,shell->mod_advanced);
    if((pressed & PORTABLE_FRONTEND_BUTTON_UP) && shell->mod_cursor>0u){
        --shell->mod_cursor;if(shell->options_open!=5)shell->mod_notice[0]='\0';
    }else if((pressed & PORTABLE_FRONTEND_BUTTON_DOWN) && shell->mod_cursor+1u<count){
        ++shell->mod_cursor;if(shell->options_open!=5)shell->mod_notice[0]='\0';
    }
    else if(pressed & (PORTABLE_FRONTEND_BUTTON_LEFT|PORTABLE_FRONTEND_BUTTON_RIGHT)){
        unsigned pages=game_shell_mod_detail_pages(shell,shell->mod_manager,shell->mod_cursor,order);
        shell->mod_detail_page=(shell->mod_detail_page+pages+
            ((pressed & PORTABLE_FRONTEND_BUTTON_LEFT)?pages-1u:1u))%pages;
        return;
    }else if(shell->options_open==5){
        if(pressed & (PORTABLE_FRONTEND_BUTTON_A|PORTABLE_FRONTEND_BUTTON_START))
            shell->mod_apply_pending=2u;
        return;
    }else if(pressed & PORTABLE_FRONTEND_BUTTON_SELECT){
        /* Shortcut to the advanced load order and back. */
        if(shell->mod_order)game_shell_mod_exit(shell,4u);
        else{shell->mod_order=1u;shell->mod_cursor=0u;}
    }else if(order && (pressed & PORTABLE_FRONTEND_BUTTON_START)){
        shell->mod_apply_pending=1u;
        shell->mod_notice_error=0u;
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Validating pending mod selection");
    }else if(order && (pressed & (PORTABLE_FRONTEND_BUTTON_L|PORTABLE_FRONTEND_BUTTON_R))){
        game_shell_move_mod(shell,(pressed & PORTABLE_FRONTEND_BUTTON_L) ? -1 : 1);
    }else if(order && (pressed & PORTABLE_FRONTEND_BUTTON_A) && shell->mod_cursor<count){
        shell->mod_notice[0]='\0';
        shell->mod_notice_error=0u;
        if (game_shell_modern_active(shell)) {
            shell->mod_action_open = 1u;
            shell->mod_action_cursor = 0u;
            return;
        }
        (void)portable_mod_manager_remove(shell->mod_manager,shell->mod_cursor);
    }else if(!order && (pressed & PORTABLE_FRONTEND_BUTTON_A)){
        shell->mod_notice[0]='\0';
        shell->mod_notice_error=0u;
        switch(portable_mod_menu_row(shell->mod_manager,shell->mod_cursor,shell->mod_advanced)){
        case PORTABLE_MOD_ROW_MOD:
            game_shell_toggle_now(shell,catalog->entries[shell->mod_cursor].id);
            break;
        case PORTABLE_MOD_ROW_INSTALL:
            shell->mod_dialog_folder=0u;
            if(!game_shell_chooser_allowed(shell))return;
            shell->mod_dialog=portable_file_dialog_choose(shell->window,"Mod ZIP archives","zip");
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",shell->mod_dialog?
                "Choose a mod ZIP archive. B cancels.":"File chooser unavailable; drop a ZIP onto this manager.");
            break;
        case PORTABLE_MOD_ROW_IMPORT:
            game_shell_choose_game(shell,NULL);
            break;
        case PORTABLE_MOD_ROW_STORE:
            shell->store_open=1u;game_shell_store_load(shell);
            break;
        case PORTABLE_MOD_ROW_WORKSHOP:
            if (game_shell_mods_dirty(shell)) {
                snprintf(shell->mod_notice,sizeof shell->mod_notice,"Apply or discard staged mods before opening the editor.");
            } else {
                shell->editor=course_editor_host_create(shell->mods_directory);
                if(!shell->editor)snprintf(shell->mod_notice,sizeof shell->mod_notice,"Could not allocate Course Workshop.");
                shell->wait_for_neutral=1;
            }
            break;
        case PORTABLE_MOD_ROW_ADVANCED:
            shell->mod_advanced=!shell->mod_advanced;
            break;
        case PORTABLE_MOD_ROW_ORDER:
            shell->mod_order=1u;shell->mod_cursor=0u;
            break;
        case PORTABLE_MOD_ROW_RELOAD:
            shell->mod_apply_pending=3u;
            break;
        case PORTABLE_MOD_ROW_FOLDER:
            if (game_shell_mods_dirty(shell)) {
                snprintf(shell->mod_notice,sizeof shell->mod_notice,"Apply or discard staged changes before adding a folder.");
            } else {
                if(!game_shell_chooser_allowed(shell))return;
                shell->mod_dialog=portable_file_dialog_choose_folder(shell->window);
                shell->mod_dialog_folder=1u;
                snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",shell->mod_dialog?
                    "Choose an additional mod folder. B cancels.":"Folder chooser unavailable.");
            }
            break;
        }
    }
    count=shell->mod_order?portable_mod_manager_count(shell->mod_manager):portable_mod_menu_row_count(shell->mod_manager,shell->mod_advanced);
    if(shell->mod_cursor>=count)shell->mod_cursor=count?(unsigned)count-1u:0u;
    shell->mod_detail_page=0u;
}

/* These are ordinary driver controls, never actor/progress or saved-state edits. */
static int game_shell_mesh_segments(GameShell *shell)
{
    const char *text = getenv("MKSC_AID_MESH_INPUTS");
    const char *race = getenv("MKSC_AID_MESH_INPUT_RACE");
    if (portable_mesh_input_race_parse(race, &shell->mesh_input_race) != PORTABLE_INPUT_SCRIPT_OK ||
        (race && (!text || !shell->aid || !shell->aid->tour)))
    {
        fputs("MKSC_AID_MESH_INPUT_RACE requires a menu tour, MKSC_AID_MESH_INPUTS and GP ordinal0..3\n",
            stderr);
        return 0;
    }
    if (!text)
        return 1;
    if (!shell->aid || !shell->aid->tour ||
        portable_mesh_input_segments_parse(text, &shell->mesh_input_segments) != PORTABLE_INPUT_SCRIPT_OK)
    {
        fputs("MKSC_AID_MESH_INPUTS requires a menu tour and ordered START:COUNT:STEER:BUTTONS segments "
              "(32 segments/4096 turns; steering -32767..32767; buttons 0..7)\n", stderr);
        return 0;
    }
    return 1;
}

static int game_shell_mesh_segment_input(GameShell *shell, const PortableMeshRacer *racer,
                                          unsigned player, PortableMeshKartInput *input)
{
    if (player || !shell->mesh_input_segments.count)
        return 1;
    uint32_t current_race = UINT32_MAX;
    if (shell->mesh_input_race != UINT32_MAX)
    {
        current_race = portable_course_3d_host_gp_course_index(shell->preview_3d);
        if (racer->mode != 1u || current_race >= 4u)
        {
            fprintf(stderr, "3D course=%u mode=%u tick=%u input race scope requires an active GP course\n",
                racer->course->id, racer->mode, racer->state.tick);
            return 0;
        }
    }
    PortableMeshKartInput selected;
    const int found = portable_mesh_input_segments_sample_race(&shell->mesh_input_segments,
        shell->mesh_input_race, current_race, racer->state.tick, &selected);
    if (found < 0 || (found && racer->state.phase != PORTABLE_MESH_RACER_RACING))
    {
        fprintf(stderr, "3D course=%u mode=%u tick=%u input segment requires a valid live racing turn\n",
                racer->course->id, racer->mode, racer->state.tick);
        return 0;
    }
    if (!found)
        return 1;
    *input = selected;
    for (size_t i = 0; i < shell->mesh_input_segments.count; ++i)
    {
        const PortableMeshInputSegment *segment = &shell->mesh_input_segments.segments[i];
        if (segment->first_tick == racer->state.tick)
        {
            if (shell->mesh_input_race != UINT32_MAX)
                printf("3D input race scope: course=%u race=%u tick=%u\n", racer->course->id,
                    current_race, racer->state.tick);
            printf("3D input segment: course=%u mode=%u player=%u tick=%u count=%u steering=%d buttons=%u\n",
                racer->course->id, racer->mode, player, racer->state.tick, segment->count,
                selected.steering, selected.buttons);
        }
    }
    return 1;
}

/* Explicit menu-tour choices only; no simulation or saved settings field. */
static int game_shell_gp_routes(GameShell *shell)
{
    const char *value = getenv("MKSC_AID_MESH_GP_ROUTES");
    if (!value)
        return 1;
    if (!shell->aid || !shell->aid->tour)
        goto invalid;
    for (unsigned i = 0; i < 4u; ++i)
    {
        uint32_t route = UINT32_MAX;
        if (!strncmp(value, "auto", 4u))
            value += 4;
        else
        {
            if (*value < '0' || *value > '9')
                goto invalid;
            route = 0;
            do
            {
                const unsigned digit = (unsigned)(*value++ - '0');
                if (route > (16383u - digit) / 10u)
                    goto invalid;
                route = route * 10u + digit;
            } while (*value >= '0' && *value <= '9');
        }
        shell->mesh_gp_routes[i] = route;
        if (*value != (i == 3u ? '\0' : ','))
            goto invalid;
        if (i != 3u)
            ++value;
    }
    shell->mesh_gp_routes_enabled = 1;
    return 1;
invalid:
    fputs("MKSC_AID_MESH_GP_ROUTES requires a GP menu tour and four auto or 0..16383 values\n", stderr);
    return 0;
}

/* Explicit menu-tour aid choice: MKSC_AID_MESH_HAZARDS=seek[:RACE[:FAMILY,...]]
 * parks the first driver on live course actors for contact routes. RACE 0..3
 * scopes it to that GP course ordinal until its first contact, so the other
 * races stay ordinary ('*' keeps every race); families narrow its targets. */
static int game_shell_hazard_seek(GameShell *shell)
{
    static const char *const families[] = {"crabs", "snowmen", "penguins", "mice", "piranhas",
        "seagulls", "lava_bubbles", "thwomps", "cannonballs", "boos", "rocks", "teepees",
        "falling_stars", "thunderclouds", "common_props", "mechakoopas", "kamek", "pier_boos"};
    const char *value = getenv("MKSC_AID_MESH_HAZARDS");
    if (!value)
        return 1;
    shell->mesh_hazard_seek_race = UINT32_MAX;
    shell->mesh_hazard_seek_families = 0;
    int valid = shell->aid && shell->aid->tour && !strncmp(value, "seek", 4u);
    const char *cursor = value + 4;
    if (valid && *cursor == ':')
    {
        ++cursor;
        if (*cursor >= '0' && *cursor <= '3')
            shell->mesh_hazard_seek_race = (uint32_t)(*cursor++ - '0');
        else if (*cursor == '*')
            ++cursor;
        else
            valid = 0;
        if (valid && *cursor == ':')
            do
            {
                ++cursor;
                const size_t length = strcspn(cursor, ",");
                unsigned f = 0;
                while (f < sizeof families / sizeof families[0] &&
                       (strlen(families[f]) != length || strncmp(cursor, families[f], length)))
                    ++f;
                if (f == sizeof families / sizeof families[0])
                    valid = 0;
                else
                    shell->mesh_hazard_seek_families |= UINT32_C(1) << f;
                cursor += length;
            } while (valid && *cursor == ',');
    }
    if (!valid || *cursor)
    {
        fputs("MKSC_AID_MESH_HAZARDS requires a menu tour and seek[:0..3|*[:FAMILY,...]]\n", stderr);
        return 0;
    }
    shell->mesh_hazard_seek_enabled = 1;
    return 1;
}

static int game_shell_hazard_seek_live(GameShell *shell)
{
    /* A scoped GP seek stops after its first contact so the race stays winnable. */
    return shell->mesh_hazard_seek_enabled &&
           (shell->mesh_hazard_seek_race == UINT32_MAX ||
            (portable_course_3d_host_gp_course_index(shell->preview_3d) == shell->mesh_hazard_seek_race &&
             !shell->mesh_hazard_seek.contacts));
}

static int game_shell_mesh_hazard_seek(GameShell *shell, const PortableMeshRacer *racer,
                                       PortableMeshKartInput *input)
{
    PortableCourse3DAudioView view;
    if (!game_shell_hazard_seek_live(shell) || !portable_course_3d_host_audio(shell->preview_3d, &view) ||
        !view.actor_events || !view.items)
        return 1;
    const uint32_t before = shell->mesh_hazard_seek.target;
    if (!portable_mesh_time_trial_pilot_seek_hazard(&shell->mesh_hazard_seek, racer, 0, view.items,
            view.source_actor_config, view.source_actors, view.actor_events,
            shell->mesh_hazard_seek_families, input))
    {
        fprintf(stderr, "3D course=%u mode=%u tick=%u hazard seek requires live course actors\n",
                racer->course->id, racer->mode, racer->state.tick);
        return 0;
    }
    if (shell->mesh_hazard_seek.target != before)
        printf("3D hazard seek: course=%u tick=%u lap=%u target=%d goal=%d,%d,%d\n",
               racer->course->id, racer->state.tick, racer->state.lap,
               (int)shell->mesh_hazard_seek.target - 1, shell->mesh_hazard_seek.goal_q16[0],
               shell->mesh_hazard_seek.goal_q16[1], shell->mesh_hazard_seek.goal_q16[2]);
    return 1;
}

static int game_shell_setup(GameShell *shell, const PortableSettings *initial,
                            const PortableSettings *persisted, const char *initial_path)
{
    if (!game_shell_gp_routes(shell) || !game_shell_mesh_segments(shell) ||
        !game_shell_hazard_seek(shell))
        return 1;
    shell->settings = *initial;
    shell->persisted_settings = *persisted;
    portable_ui_background_host_init(&shell->ui_background);
    if (initial_path != NULL)
        snprintf(shell->settings_path, sizeof shell->settings_path, "%s", initial_path);
    shell->gp_retry_credits = 3u;
    shell->record_status = PORTABLE_FRONTEND_INPUT_REPLAY_OK;
    shell->focused = 1;
    shell->needs_present = 1;
    shell->record_path = getenv("MKSC_FRONTEND_RECORD");
    shell->tour.motion_directory =
        shell->aid != NULL && shell->aid->tour != NULL ? getenv("MKSC_FRONTEND_MOTION_DIR") : NULL;
    if (shell->tour.motion_directory != NULL && !SDL_CreateDirectory(shell->tour.motion_directory))
    {
        fputs("frontend motion directory unavailable\n", stderr);
        return 1;
    }
    static const char *const content_phases[] = {"Preparing course browser", "Preparing music and menus"};
    const int loaded = portable_loading_run(NULL, NULL, "Getting ready to race", content_phases, 2,
                                            game_shell_startup_content, shell);
    if (loaded != 1) { shell->quit = loaded < 0; return 1; }
    portable_menu_names_set_3d_drivers(shell->pack_3d);
    if (shell->aid != NULL && shell->aid->tour == NULL)
    {
        int found = 0;
        for (size_t driver = 0u; driver < shell->config.driver_count; ++driver)
            found |= shell->drivers[driver].content_id == shell->aid->character;
        int arena_found = 0, shell_course_found = 0;
        for (size_t course = 0u; course < shell->config.course_count; ++course)
        {
            arena_found |= shell->courses[course].cup == PORTABLE_FRONTEND_ARENA_CUP &&
                           shell->courses[course].track == shell->aid->arena;
            shell_course_found |= shell->courses[course].available &&
                                  shell->courses[course].cup == shell->aid->cup &&
                                  shell->courses[course].track == shell->aid->track;
        }
        if (shell->aid->shell_route && !shell_course_found)
        {
            fputs("shell aid cup/track is absent from the course catalog\n", stderr);
            return 1;
        }
        const PortableTrackSet aid_set = portable_track_set_of_cup(shell->aid->cup);
        PortableFrontendFlow probe = shell->flow;
        probe.course_cup = shell->aid->cup;
        probe.race_speed = shell->aid->engine_class;
        probe.mode_cursor = shell->aid->quick_run ? 2u : 0u;
        if (!found || shell->aid->engine_class >= 3u ||
            (shell->aid->battle
                 ? !arena_found
                 : (aid_set == PORTABLE_TRACK_SET_ARENA || aid_set == PORTABLE_TRACK_SET_COUNT ||
                    !(portable_frontend_flow_cup_access(&probe) &
                      (1u << (shell->aid->cup - portable_track_set_first_cup(aid_set)))))))
        {
            fputs("GP cup aid selection is unavailable or locked\n", stderr);
            return 1;
        }
    }
    if (shell->record_path != NULL && shell->record_path[0] != '\0')
    {
        shell->record_file = fopen(shell->record_path, "wb");
        if (shell->record_file == NULL ||
            portable_frontend_input_replay_write_header_file(shell->record_file) !=
                PORTABLE_FRONTEND_INPUT_REPLAY_OK)
        {
            if (shell->record_file != NULL)
                fclose(shell->record_file);
            shell->record_file = NULL;
            fprintf(stderr, "could not create frontend replay: %s\n", shell->record_path);
            return 1;
        }
    }
    shell->scratch = calloc(1u, sizeof *shell->scratch);
    if (shell->scratch == NULL || !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
    {
        if (shell->record_file != NULL)
            fclose(shell->record_file);
        free(shell->scratch);
        shell->scratch = NULL;
        shell->record_file = NULL;
        return 1;
    }
    portable_mobile_lifecycle_start();
    portable_sdl_input_configure(&shell->settings);
    /* Automated runs keep the original menus and their timing unless asked. */
    if (shell->aid != NULL && getenv("MKSC_AID_MODERN_MENUS") == NULL)
        shell->settings.classic_menus = 1u;
    if (shell->aid != NULL)
    {
        shell->modern.fixed_width = 1280;
        shell->modern.fixed_height = 720;
    }
    const char *benchmark = getenv("MKSC_AID_MESH_BENCHMARK");
    if (benchmark)
    {
        if (!shell->aid || strcmp(benchmark, "1") || shell->aid->tour_frames ||
            getenv("MKSC_SCREEN_DIR") || getenv("MKSC_FRONTEND_MOTION_DIR"))
        {
            fputs("MKSC_AID_MESH_BENCHMARK requires aid, value1 and captures disabled\n", stderr);
            return 1;
        }
        shell->mesh_benchmark = 1;
    }
    const char *subframes = getenv("MKSC_AID_MESH_SUBFRAMES");
    if (shell->aid != NULL && subframes != NULL)
    {
        if (strcmp(subframes, "2") == 0) shell->mesh_aid_subframes = 2u;
        else if (strcmp(subframes, "3") == 0) shell->mesh_aid_subframes = 3u;
        else
        {
            fputs("MKSC_AID_MESH_SUBFRAMES must be 2 or 3\n", stderr);
            return 1;
        }
    }
    const char *modern_size = getenv("MKSC_MODERN_MENU_SIZE");
    if (modern_size != NULL &&
        sscanf(modern_size, "%dx%d", &shell->modern.fixed_width, &shell->modern.fixed_height) != 2)
        shell->modern.fixed_width = shell->modern.fixed_height = 0;
    shell->flow.short_transitions = menu_short_transitions(&shell->settings);
    if (shell->record_file != NULL && shell->flow.short_transitions)
    {
        fputs("frontend input recording uses original transition timing\n", stderr);
        shell->flow.short_transitions = 0u;
    }
    if (!shell->error)
    {
        PortableSdlAudioStatus status = portable_sdl_audio_queue_open(&shell->audio->queue);
        if (status != PORTABLE_SDL_AUDIO_OK)
        {
            shell->failure_detail = portable_sdl_audio_status_name(status);
            shell->error = 1;
        }
    }
    if (!shell->error && !SDL_SetAudioStreamGain(shell->audio->queue.stream,
                                                 (float)shell->settings.master_volume / 100.0f))
    {
        shell->failure_detail = "master audio gain";
        shell->error = 1;
    }
    const uint64_t video_started = portable_startup_clock();
    const int inherited_window = portable_loading_take(&shell->window, &shell->renderer);
    if (inherited_window) {
        /* The loading surface becomes the shell window at the size it would
         * have been created with; automated runs keep their 1x hidden window. */
        const int scale = shell->aid != NULL ? 1 : (int)shell->settings.window_scale;
        (void)SDL_SetWindowSize(shell->window,
            (shell->settings.aspect_width ? shell->settings.aspect_width : 240) * scale, 160 * scale);
        if (shell->aid == NULL)
            (void)SDL_SetWindowFullscreen(shell->window, shell->settings.fullscreen || PORTABLE_WINDOW_ALWAYS_FULLSCREEN);
        else
            (void)SDL_SyncWindow(shell->window);
        SDL_SetWindowTitle(shell->window,
            "MKSC | arrows move | Enter/A choose | Space/Start advance | Esc/B back");
    }
    if (!inherited_window) shell->window = SDL_CreateWindow(
        "MKSC | arrows move | Enter/A choose | Space/Start advance | Esc/B back",
        shell->aid != NULL
            ? (shell->settings.aspect_width != 0u ? shell->settings.aspect_width : 240)
            : (shell->settings.aspect_width != 0u ? shell->settings.aspect_width : 240) *
                  (int)shell->settings.window_scale,
        shell->aid != NULL ? 160 : 160 * (int)shell->settings.window_scale,
        shell->aid != NULL ? SDL_WINDOW_HIDDEN
                           : SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                 (shell->settings.fullscreen ? SDL_WINDOW_FULLSCREEN : 0u) |
                                 PORTABLE_WINDOW_ALWAYS_FULLSCREEN);
    const int gpu_aid = shell->aid != NULL && getenv("MKSC_AID_GPU") != NULL &&
        strcmp(getenv("MKSC_AID_GPU"), "1") == 0;
    const int gles_aid = shell->aid != NULL && getenv("MKSC_AID_GLES") != NULL &&
        strcmp(getenv("MKSC_AID_GLES"), "1") == 0;
    if (inherited_window && (gpu_aid || gles_aid)) {
        SDL_DestroyRenderer(shell->renderer);
        shell->renderer = NULL;
    }
    if (shell->window != NULL && shell->renderer == NULL)
    {
        if (gpu_aid && !gles_aid)
            shell->renderer = SDL_CreateGPURenderer(NULL, NULL);
        else if (shell->aid == NULL)
            shell->renderer = SDL_CreateGPURenderer(NULL, shell->window);
        if (shell->renderer == NULL && (gles_aid || (shell->aid == NULL && !gpu_aid)))
        {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            shell->renderer = SDL_CreateRenderer(shell->window, "opengles2");
            if (shell->renderer == NULL)
            {
                /* ES2 still accelerates SDL menu geometry on older handhelds;
                 * depth scenes select the portable fallback there. */
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
                shell->renderer = SDL_CreateRenderer(shell->window, "opengles2");
            }
        }
        if (shell->renderer == NULL && !gpu_aid && !gles_aid)
            shell->renderer = SDL_CreateRenderer(shell->window, shell->aid != NULL ? "software" : NULL);
    }
    if ((gpu_aid || gles_aid) && shell->renderer != NULL)
    {
        const int width = shell->modern.fixed_width > 0 ? shell->modern.fixed_width : 1280;
        const int height = shell->modern.fixed_height > 0 ? shell->modern.fixed_height : 720;
        shell->gpu_aid_target = SDL_CreateTexture(shell->renderer, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET, width, height);
        if (shell->gpu_aid_target == NULL || !SDL_SetRenderTarget(shell->renderer, shell->gpu_aid_target))
        {
            shell->failure_detail = "offscreen GPU aid target";
            shell->error = 1;
        }
    }
    if (shell->renderer != NULL && shell->aid == NULL &&
        !SDL_SetRenderVSync(shell->renderer, shell->settings.vsync))
        fprintf(stderr, "requested VSync unavailable: %s\n", SDL_GetError());
    shell->texture = shell->renderer != NULL
                         ? SDL_CreateTexture(shell->renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, 240, 160)
                         : NULL;
    if (shell->texture != NULL &&
        !SDL_SetTextureScaleMode(shell->texture, shell->settings.linear_filter
                                                     ? SDL_SCALEMODE_LINEAR
                                                     : SDL_SCALEMODE_NEAREST))
    {
        shell->failure_detail = "texture scale mode";
        shell->error = 1;
    }
    portable_startup_phase("window/GPU init", video_started);
    shell->controller = portable_sdl_gamepad_open_first();
    if (shell->texture == NULL)
    {
        shell->failure_detail = "SDL video surface";
        shell->error = 1;
    }
    shell->frequency = SDL_GetPerformanceFrequency();
    portable_frame_scheduler_init(&shell->scheduler);
    return 0;
}

/* A child scene receives its original SDL event before the shell advances it. */
static void game_shell_poll_events(GameShell *shell)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (shell->editor && event.type==SDL_EVENT_DROP_FILE) continue;
        /* Time Trial borrows this reference and handles its own hotplug.
         * GP owns a separate SDL reference; the shell must also stay current. */
        if (shell->selected_trial == NULL)
            portable_sdl_gamepad_device_event(&shell->controller, &event);
        modern_menu_host_event(&shell->modern, &event);
        if (portable_course_3d_host_active(shell->preview_3d))
            portable_course_3d_host_device_event(shell->preview_3d, &event);
        if (event.type == SDL_EVENT_RENDER_DEVICE_RESET)
        {
            portable_course_3d_host_reset_renderer(shell->preview_3d);
            shell->needs_present = 1;
        }
        else if (event.type == SDL_EVENT_RENDER_TARGETS_RESET)
            shell->needs_present = 1;
        if (portable_course_3d_host_driving(shell->preview_3d))
        {
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                event.type == SDL_EVENT_GAMEPAD_REMOVED || portable_mobile_back(&event) ||
                (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                 (event.key.key == SDLK_P || event.key.key == SDLK_SPACE || event.key.key == SDLK_ESCAPE)))
                portable_course_3d_host_set_paused(shell->preview_3d, 1);
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                event.gbutton.which == portable_sdl_gamepad_id(shell->controller))
            {
                static const uint16_t actions[4] = {PORTABLE_INPUT_ACCELERATE,
                    PORTABLE_INPUT_BRAKE, PORTABLE_INPUT_HOP, PORTABLE_INPUT_ITEM};
                for (unsigned binding = 0u; binding < 4u; ++binding)
                    if (event.gbutton.button == shell->settings.pad_buttons[binding])
                        shell->mesh_driving_pressed |= actions[binding];
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                event.key.scancode > 0 && event.key.scancode < SDL_SCANCODE_COUNT)
            {
                bool keys[SDL_SCANCODE_COUNT] = {false};
                keys[event.key.scancode] = true;
                shell->mesh_driving_pressed |= portable_keyboard_driving_buttons(
                    keys, SDL_SCANCODE_COUNT, &shell->settings);
            }
        }
        if (shell->binding_capture && shell->focused)
        {
            const int key = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
            const int pad = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                            event.gbutton.which == portable_sdl_gamepad_id(shell->controller);
            if ((key && (event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_AC_BACK)) ||
                (pad && event.gbutton.button == SDL_GAMEPAD_BUTTON_START))
            {
                shell->binding_capture = 0;
                shell->options_notice = NULL;
                shell->wait_for_neutral = 1;
            }
            else if ((key && shell->binding_cursor < BINDING_KEY_ROWS) ||
                     (pad && shell->binding_cursor >= BINDING_KEY_ROWS))
            {
                const PortableSetting field = binding_field(shell->binding_cursor);
                const uint32_t value = key ? (uint32_t)event.key.scancode : event.gbutton.button;
                if (portable_setting_set(&shell->settings, field, value))
                {
                    portable_sdl_input_configure(&shell->settings);
                    shell->options_notice = save_setting_edit(
                        shell->settings_path, &shell->persisted_settings, field, value);
                    shell->binding_capture = 0;
                    shell->wait_for_neutral = 1;
                }
                else
                    shell->options_notice = "RESERVED SYSTEM CONTROL - TRY ANOTHER";
            }
            if (key || pad)
            {
                shell->needs_present = 1;
                continue;
            }
        }
        if (event.type == SDL_EVENT_DROP_FILE)
        {
            if(event.drop.data!=NULL && shell->options_open>=4 && shell->options_open<=7){
                if((shell->options_open==4||shell->options_open==7) && shell->mod_dialog==NULL) {
                    if(shell->store_open){
                        char url[PORTABLE_STORE_URL_CAPACITY];
                        if(portable_store_file_url(event.drop.data,url,sizeof url))game_shell_store_add(shell,url);
                        shell->needs_present=1;continue;
                    }
                    SDL_PathInfo info={0};
                    const int inspected=portable_source_path_info(event.drop.data,&info);
                    if (inspected && info.type==SDL_PATHTYPE_DIRECTORY) {
                        game_shell_add_mod_folder(shell,event.drop.data);
                        shell->needs_present=1;
                        shell->wait_for_neutral=1;
                    } else if (!inspected && portable_source_path_is_uri(event.drop.data)) {
                        snprintf(shell->mod_notice,sizeof shell->mod_notice,
                            "Cannot inspect picked source; reselect the file or folder.");
                        shell->mod_notice_error=1u;
                        shell->needs_present=1;
                        shell->wait_for_neutral=1;
                    } else if (game_shell_names_game_rom(event.drop.data))
                        game_shell_start_game_import(shell,event.drop.data);
                    else game_shell_install(shell,event.drop.data,NULL,NULL);
                }
                continue;
            }
            if (event.drop.data != NULL && shell->selected_trial == NULL &&
                shell->selected_gp == NULL && shell->selected_ceremony == NULL)
            {
                const int size = snprintf(shell->incoming_ghost, sizeof shell->incoming_ghost, "%s",
                                          event.drop.data);
                if (size < 0 || size >= (int)sizeof shell->incoming_ghost)
                {
                    shell->incoming_ghost[0] = '\0';
                    shell->records.notice = "FILE PATH TOO LONG";
                }
                else
                    shell->records.notice = "FILE READY - CHOOSE RECEIVED SLOT";
                shell->needs_present = 1;
            }
            continue;
        }
        if (shell->selected_ceremony != NULL)
        {
            if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
            {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    shell->focused = 0;
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    shell->focused = 1;
            }
            portable_gp_ceremony_host_event(shell->selected_ceremony, &event);
            continue;
        }
        if (shell->selected_gp != NULL)
        {
            if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
            {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                {
                    shell->focused = 0;
                    shell->keyboard_buttons = 0u;
                    shell->keyboard_pressed = 0u;
                }
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    shell->focused = 1;
            }
            portable_gp_host_event(shell->selected_gp, &event);
            continue;
        }
        if (shell->selected_trial != NULL)
        {
            if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
            {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                {
                    shell->focused = 0;
                    shell->keyboard_buttons = 0u;
                    shell->keyboard_pressed = 0u;
                }
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    shell->focused = 1;
            }
            portable_time_trial_host_event(shell->selected_trial->host, &event);
            continue;
        }
        if (event.type == SDL_EVENT_QUIT)
        {
            game_shell_mod_exit(shell,3u);
            continue;
        }
        if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
        {
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                shell->focused = 0;
                shell->keyboard_buttons = 0u;
                shell->keyboard_pressed = 0u;
                portable_mobile_touch_clear();
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
            {
                shell->focused = 1;
                shell->needs_present = 1;
            }
            if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                shell->needs_present = 1;
        }
        if (shell->editor && !shell->editor_previous_preview &&
            course_editor_host_event(shell->editor,&event,shell->window)) {
            shell->needs_present=1;
            continue;
        }
        const uint16_t touch_pressed = portable_mobile_touch_event(&event, shell->renderer);
        if (shell->binding_capture && (touch_pressed & PORTABLE_FRONTEND_BUTTON_B))
        {
            shell->binding_capture = 0;
            shell->options_notice = NULL;
            shell->wait_for_neutral = 1;
        }
        if (touch_pressed != 0u || event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_MOTION)
            shell->needs_present = 1;
        if (portable_mobile_back(&event))
        {
            shell->keyboard_pressed |= PORTABLE_FRONTEND_BUTTON_B;
            shell->needs_present = 1;
        }
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
            frontend_keyboard_event(&event.key, &shell->keyboard_buttons, &shell->keyboard_pressed);
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F5 && !shell->store_open &&
            !shell->binding_capture && shell->mod_dialog == NULL && !shell->mod_unsaved_open &&
            !portable_course_3d_host_active(shell->preview_3d) &&
            shell->options_open != 5) {
            if (game_shell_open_mod_manager(shell)) shell->mod_apply_pending=3u;
        }
    }
}

static int game_shell_tick_trial(GameShell *shell, uint64_t now)
{
    const int status = portable_time_trial_host_frame(shell->selected_trial->host);
    if (status == 0)
        return 1;
    PortableRecoveredTimeTrialHostOutcome outcome;
    const int closed = portable_time_trial_host_end(shell->selected_trial->host, &outcome);
    shell->marker_clock = outcome.marker_clock;
    shell->marker_display_clock = outcome.marker_display_clock;
    release_selected_session(shell->selected_trial);
    shell->selected_trial = NULL;
    if (status < 0 || closed)
    {
        shell->failure_detail = "race scene frame";
        shell->error = 1;
        return 0;
    }
    if (shell->aid != NULL && (shell->aid->shell_route || shell->aid->tour != NULL))
    {
        if (!outcome.finished || outcome.result_ticks == 0u)
        {
            shell->failure_detail = "shell Time Trial did not finish";
            shell->error = 1;
            return 0;
        }
        shell->aid_trial_completed = 1u;
        if (shell->audio != NULL &&
            (!shell->audio->finish_audio.started || shell->audio->finish_audio.countdown != 0u ||
             !portable_audio_session_song_loaded(&shell->audio->session,
                                                 shell->audio->finish_audio.music)))
        {
            shell->failure_detail = "shell Time Trial finish audio";
            shell->error = 1;
            return 0;
        }
        printf(
            "Game shell Time Trial: PASS course=%u ticks=%u results=returned finish_music=%u replays=%u\n",
            shell->flow.selected_course, outcome.result_ticks,
            shell->audio != NULL ? shell->audio->finish_audio.music : 0u,
            outcome.replays_completed);
        fflush(stdout);
    }
    if (outcome.quit_requested)
    {
        shell->quit = 1;
        return 0;
    }
    if (!outcome.return_to_selection ||
        !portable_frontend_flow_cancel_race_to(&shell->flow,
                                               frontend_return_page(outcome.return_destination)))
    {
        shell->error = 1;
        return 0;
    }
    if (shell->record_file != NULL)
    {
        shell->record_status =
            portable_frontend_input_replay_write_cancel_file(shell->record_file, &shell->flow);
        if (shell->record_status != PORTABLE_FRONTEND_INPUT_REPLAY_OK)
        {
            shell->error = 1;
            return 0;
        }
    }
    if (!route_audio_return_menu(shell->audio))
    {
        shell->failure_detail = "menu audio return";
        shell->error = 1;
        return 0;
    }
    portable_frame_scheduler_reset(&shell->scheduler, now, shell->focused);
    shell->keyboard_buttons = 0u;
    shell->keyboard_pressed = 0u;
    portable_mobile_touch_clear();
    shell->wait_for_neutral = 1;
    shell->needs_present = 1;
    return 1;
}

static int game_shell_tick_ceremony(GameShell *shell, uint64_t now)
{
    if (shell->aid != NULL && ++shell->aid_ceremony_frames % 120u == 0u)
    {
        const SDL_Event confirm = {.key = {.type = SDL_EVENT_KEY_DOWN, .key = SDLK_RETURN}};
        portable_gp_ceremony_host_event(shell->selected_ceremony, &confirm);
    }
    const int status = portable_gp_ceremony_host_frame(shell->selected_ceremony, now);
    if (status == 0)
    {
        if (shell->aid == NULL)
            game_shell_idle();
        return 1;
    }
    uint8_t ceremony_quit = 0;
    const int closed = portable_gp_ceremony_host_end(shell->selected_ceremony, &ceremony_quit);
    shell->selected_ceremony = NULL;
    if (status < 0 || closed)
    {
        shell->failure_detail = "GP ceremony frame";
        shell->error = 1;
        return 0;
    }
    if (ceremony_quit)
    {
        shell->quit = 1;
        return 0;
    }
    if (!portable_frontend_flow_cancel_race_to(&shell->flow, PORTABLE_FRONTEND_PAGE_MODE) ||
        !route_audio_return_menu(shell->audio))
    {
        shell->failure_detail = "GP ceremony menu return";
        shell->error = 1;
        return 0;
    }
    if (shell->record_file != NULL &&
        portable_frontend_input_replay_write_cancel_file(shell->record_file, &shell->flow) !=
            PORTABLE_FRONTEND_INPUT_REPLAY_OK)
    {
        shell->error = 1;
        return 0;
    }
    if (shell->aid != NULL)
    {
        printf("GP menu cup: PASS cup=%u class=%u character=%u end=ceremony seeded=%u\n",
               shell->aid->cup, shell->aid->engine_class, shell->aid->character,
               shell->aid->final_seed != 0u);
        shell->aid_completed = 1u;
        shell->quit = 1;
    }
    shell->gp_cup_progress_ready = 0;
    shell->keyboard_buttons = 0;
    shell->keyboard_pressed = 0;
    shell->wait_for_neutral = 1;
    shell->needs_present = 1;
    portable_frame_scheduler_reset(&shell->scheduler, now, shell->focused);
    return 1;
}

static int game_shell_tick_gp(GameShell *shell, uint64_t now)
{
    const int status = portable_gp_host_frame(shell->selected_gp);
    if (status == 0)
    {
        if (shell->aid == NULL)
            game_shell_idle();
        return 1;
    }
    PortableGpRaceHostOutcome gp_outcome;
    const int closed = portable_gp_host_end(shell->selected_gp, &gp_outcome);
    shell->marker_clock = gp_outcome.marker_clock;
    shell->marker_display_clock = gp_outcome.marker_display_clock;
    if (shell->gp_cup_progress_ready)
    {
        shell->gp_cup_progress.marker_clock = shell->marker_clock;
        shell->gp_cup_progress.marker_display_clock = shell->marker_display_clock;
    }
    shell->selected_gp = NULL;
    if (status < 0 || closed)
    {
        shell->failure_detail = "GP race frame";
        shell->error = 1;
        return 0;
    }
    if (shell->aid != NULL && shell->aid->shell_route)
    {
        if (!shell->aid_trial_completed || !gp_outcome.results_completed)
        {
            shell->failure_detail = "shell GP did not finish";
            shell->error = 1;
            return 0;
        }
        puts("Game shell: PASS title -> Time Trial -> results -> GP race -> results");
        shell->aid_completed = 1u;
        shell->quit = 1;
        return 1;
    }
    PortableFrontendPage destination = PORTABLE_FRONTEND_PAGE_COURSE;
    int return_to_menu = 1;
    if (gp_outcome.quit_requested)
    {
        shell->quit = 1;
        return 0;
    }
    if (gp_outcome.results_completed)
    {
        if (shell->quick_playback && shell->aid != NULL)
            shell->aid_quick_replayed = 1u;
        if (gp_outcome.scene_request != UINT32_C(1))
        {
            shell->failure_detail = "GP results scene request";
            shell->error = 1;
            return 0;
        }
        if (shell->aid != NULL)
            printf(
                "GP menu results: course=%u track=%u credits=%u next=%u lakitu_pulses=%u course_loops=%u lap_cues=%u final_lap=%u/%u\n",
                shell->flow.selected_course,
                shell->gp_cup_progress_ready ? shell->gp_cup_progress.course_in_cup : 0u,
                shell->gp_retry_credits, gp_outcome.next_mode,
                shell->audio != NULL ? shell->audio->lakitu_pulses : 0u,
                shell->audio != NULL ? shell->audio->course_loop_starts : 0u,
                shell->audio != NULL ? shell->audio->lap_cues : 0u,
                shell->audio != NULL ? shell->audio->final_lap_starts : 0u,
                shell->audio != NULL ? shell->audio->final_lap_restores : 0u);
        if (shell->aid != NULL)
            fflush(stdout);
        switch (gp_outcome.next_mode)
        {
        case 1u:
            if (shell->flow.mode_cursor >= 2u)
            {
                shell->quick_playback = 0u;
                return_to_menu = 0;
                break;
            }
            if (shell->gp_retry_credits == 0u)
            {
                shell->failure_detail = "GP retry without credit";
                shell->error = 1;
                break;
            }
            --shell->gp_retry_credits;
            shell->gp_retry_pending = 1u;
            if (shell->gp_cup_progress_ready)
                shell->gp_cup_progress.retry_credits = shell->gp_retry_credits;
            return_to_menu = 0;
            break;
        case 3u:
            destination = PORTABLE_FRONTEND_PAGE_COURSE;
            break;
        case 4u:
            if (shell->aid != NULL && !shell->aid->quick_run && !shell->aid->battle)
            {
                printf("GP menu cup: PASS cup=%u class=%u character=%u end=game-over\n",
                       shell->aid->cup, shell->aid->engine_class, shell->aid->character);
                shell->aid_completed = 1u;
                shell->quit = 1;
            }
            destination = PORTABLE_FRONTEND_PAGE_MODE;
            break;
        case 2u:
            if (shell->flow.mode_cursor >= 2u)
                destination = PORTABLE_FRONTEND_PAGE_CHARACTER;
            else
                return_to_menu = 0;
            break;
        case 6u:
            if (shell->flow.mode_cursor != 2u || shell->quick_recording == NULL ||
                !shell->quick_recording->complete)
            {
                shell->failure_detail = "Quick Run replay unavailable";
                shell->error = 1;
                break;
            }
            shell->quick_playback = 1u;
            return_to_menu = 0;
            break;
        case 8u:
        {
            if (!gp_outcome.cup_session_ready || gp_outcome.cup_session.next_mode != 8u ||
                (gp_outcome.cup_session.race_mode_flags & UINT16_C(0x0f00)) != UINT16_C(0x0100))
            {
                shell->failure_detail = "GP standings transition";
                shell->error = 1;
                break;
            }
            if (portable_gp_cup_session_advance(&gp_outcome.cup_session, &shell->gp_cup_definition,
                                                &shell->gp_cup_progress) !=
                PORTABLE_GP_CUP_SESSION_OK)
            {
                shell->failure_detail = "GP next-race handoff";
                shell->error = 1;
                break;
            }
            shell->gp_cup_progress_ready = 1u;
            shell->flow.selected_course = shell->gp_cup_progress.course_id;
            return_to_menu = 0;
            break;
        }
        case 7u:
        {
            PortableGpCeremonySession ceremony;
            if (!gp_outcome.cup_session_ready ||
                portable_gp_ceremony_session_init(&ceremony, &gp_outcome.cup_session) !=
                    PORTABLE_GP_CEREMONY_OK ||
                !persist_cup_ceremony(&ceremony) ||
                !refresh_gp_cup_access(&shell->flow, &shell->menu_progress))
            {
                shell->failure_detail = "GP ceremony setup";
                shell->error = 1;
                break;
            }
            if (shell->aid != NULL)
                printf("GP menu ceremony: coins=%u extra-unlocked=%u rank=%u rating=%u score=%d\n",
                       ceremony.finishing_coin_total, ceremony.extra_cup_unlocked,
                       ceremony.player_standing_rank, ceremony.performance_rating,
                       ceremony.driving_score);
            shell->selected_ceremony = portable_gp_ceremony_host_begin(
                shell->pack, &ceremony, shell->flow.selected_character, shell->window,
                shell->renderer, shell->texture, shell->audio);
            if (shell->selected_ceremony == NULL ||
                !portable_gp_ceremony_host_video(shell->selected_ceremony,
                                                 shell->settings.internal_scale,
                                                 shell->settings.fit_scaling))
            {
                shell->failure_detail = "GP ceremony begin";
                shell->error = 1;
                break;
            }
            portable_gp_ceremony_host_modern(shell->selected_ceremony, (uint8_t)!shell->settings.classic_menus);
            if (shell->settings.wide_ui_backgrounds)
                portable_gp_ceremony_host_aspect(shell->selected_ceremony,shell->settings.aspect_width);
            if (shell->flow.short_transitions)
                portable_gp_ceremony_host_allow_skip(shell->selected_ceremony);
            return_to_menu = 0;
            break;
        }
        default:
            shell->failure_detail = "GP results action";
            shell->error = 1;
            break;
        }
        if (shell->error)
            return 0;
    }
    if (return_to_menu && !portable_frontend_flow_cancel_race_to(&shell->flow, destination))
    {
        shell->failure_detail = "GP results frontend handoff";
        shell->error = 1;
        return 0;
    }
    if (return_to_menu && shell->record_file != NULL)
    {
        shell->record_status =
            portable_frontend_input_replay_write_cancel_file(shell->record_file, &shell->flow);
        if (shell->record_status != PORTABLE_FRONTEND_INPUT_REPLAY_OK)
        {
            shell->error = 1;
            return 0;
        }
    }
    if (return_to_menu && !route_audio_return_menu(shell->audio))
    {
        shell->failure_detail = "menu audio return";
        shell->error = 1;
        return 0;
    }
    if (return_to_menu)
    {
        shell->gp_cup_progress_ready = 0u;
        shell->gp_retry_pending = 0u;
        shell->quick_playback = 0u;
    }
    if (return_to_menu && gp_outcome.results_completed && shell->aid != NULL && shell->aid->battle)
    {
        if (shell->flow.page != PORTABLE_FRONTEND_PAGE_COURSE || gp_outcome.cup_session_ready)
        {
            shell->failure_detail = "Battle result handoff";
            shell->error = 1;
            return 0;
        }
        printf("Battle menu: PASS arena=%u class=%u character=%u end=arena-select\n",
               shell->aid->arena, shell->aid->engine_class, shell->aid->character);
        shell->aid_completed = 1u;
        shell->quit = 1;
    }
    if (return_to_menu && gp_outcome.results_completed && shell->aid != NULL &&
        shell->aid->quick_run)
    {
        if (shell->flow.page != PORTABLE_FRONTEND_PAGE_COURSE || gp_outcome.cup_session_ready ||
            (shell->aid->replay_aid && !shell->aid_quick_replayed))
        {
            shell->failure_detail = "Quick Run result handoff";
            shell->error = 1;
            return 0;
        }
        printf("Quick Run menu: PASS cup=%u class=%u character=%u end=course-select\n",
               shell->aid->cup, shell->aid->engine_class, shell->aid->character);
        shell->aid_completed = 1u;
        shell->quit = 1;
    }
    shell->keyboard_buttons = 0u;
    shell->keyboard_pressed = 0u;
    portable_mobile_touch_clear();
    shell->wait_for_neutral = 1;
    portable_frame_scheduler_reset(&shell->scheduler, now, shell->focused);
    shell->needs_present = 1;
    return 1;
}

typedef enum
{
    GAME_SHELL_TICK_FAILED = -1,
    GAME_SHELL_TICK_MENU = 0,
    GAME_SHELL_TICK_OPTIONS = 1
} GameShellMenuTick;

/* 3D-only drivers (kind110) speak with their stock donor's voice. */
static int mesh_voice(const PortableAssetPackView *pack, uint32_t driver_id,
                        PortableCharacterContent *voice)
{
    PortableDriver3D row;
    if (portable_driver_3d_find(pack, driver_id, &row))
        driver_id = row.donor;
    return portable_content_registry_voice_driver(pack, driver_id, voice) == PORTABLE_CONTENT_OK &&
           voice->character_class < 8u;
}

static int game_shell_mesh_audio(GameShell *shell)
{
    if (!shell->audio)
        return 1;
    PortableCourse3DAudioView view;
    if (!portable_course_3d_host_audio(shell->preview_3d, &view))
    {
        if (shell->mesh_audio_active)
        {
            if (!route_audio_rebind_pack(shell->audio, shell->pack) ||
                !route_audio_return_menu(shell->audio))
                return 0;
            shell->mesh_audio_active = 0;
            printf("3D audio return: menu pack restored\n");
        }
        return route_audio_render(shell->audio, 0u, 0, NULL);
    }
    if (!shell->mesh_audio_active || shell->mesh_audio_generation != view.generation)
    {
        PortableCharacterContent voice;
        if (!mesh_voice(view.pack, view.driver_id, &voice))
        {
            fprintf(stderr, "3D audio course=%u mode=%u tick=%u: invalid voice donor for driver=%u\n",
                    view.course_id, view.mode, view.ordinal, view.driver_id);
            return 0;
        }
        PortableMeshAudioConfig config = {
            .pack = view.pack, .course_id = view.course_id, .driver_id = view.driver_id,
            .music_kind = view.music_kind, .music_id = view.music_id, .mode = view.mode,
            .sound_set_id = view.sound_set_id,
            .voice_class = (uint8_t)voice.character_class,
            .engine_class = (uint8_t)view.engine_class,
            .items_enabled = (uint8_t)(view.items != NULL), .human_mask = view.human_mask};
        if (view.items)
            for (unsigned i = 0; i < view.racer_count; ++i)
            {
                PortableCharacterContent donor;
                if (!view.driver_ids || !mesh_voice(view.pack, view.driver_ids[i], &donor))
                    return 0;
                config.donor[i] = (uint8_t)donor.character_class;
                config.driver_ids[i] = view.driver_ids[i];
            }
        if (!route_audio_mesh_begin(shell->audio, &config))
            return 0;
        shell->mesh_audio_active = 1;
        shell->mesh_audio_generation = view.generation;
        printf("3D audio begin: course=%u mode=%u driver=%u voice=%u generation=%u music=%s\n",
               view.course_id, view.mode, view.driver_id, voice.id, view.generation,
               view.music_kind == UINT32_MAX ? "none" : "authored");
    }
    const PortableMeshRacerState *state = view.state;
    const PortableMeshAudioFrame frame = {
        .tick = view.ordinal,
        .phase = view.finished ? PORTABLE_MESH_AUDIO_FINISHED
                 : state->phase == PORTABLE_MESH_RACER_COUNTDOWN ? PORTABLE_MESH_AUDIO_COUNTDOWN
                                                               : PORTABLE_MESH_AUDIO_RACING,
        .countdown_remaining = state->countdown_ticks, .laps_completed = state->lap,
        .lap_count = view.lap_count, .speed_q16 = state->kart.speed_q16,
        .target_speed_q16 = view.rules->forward_speed_q16, .buttons = view.buttons,
        .hopped = view.events.hopped, .landed = view.events.landed,
        .boost_released = view.events.released, .panel_started = view.events.panel,
        .drifting = (uint8_t)(state->kart.drift_direction != 0 && state->kart.grounded),
        .drift_tier = state->kart.drift_tier,
        .respawning = state->kart.respawn_ticks != 0u || state->respawn_grace != 0u,
        .paused = view.paused, .finished = view.finished, .rank = view.rank,
        .revenge_bomb = view.revenge_bomb,
        .racer_count = view.racer_count, .items = view.items, .item_events = view.item_events,
        .actor_events = view.actor_events,
        .source_actor_config = view.source_actor_config, .source_actors = view.source_actors,
        .racers = view.racers, .human_mask = view.human_mask,
        .grounded = state->kart.grounded, .surface_sound = view.surface_sound,
        .start_boost = (uint8_t)(state->kart.rocket_ticks != 0u), .hazard_hit = view.events.hazard,
        .emitters = view.emitters, .emitter_count = view.emitter_count,
        .listener_q16 = {state->kart.position_q16[0], state->kart.position_q16[1], state->kart.position_q16[2]}};
    return route_audio_mesh_tick(shell->audio, &frame);
}

static void game_shell_battle_aid_failure(const PortableMeshRaceSession *race, char token)
{
    if (!race || race->mode != 16u || race->state.tick <= 12000u)
        return;
    const unsigned count = race->state.player_count > 8u ? 8u : race->state.player_count;
    for (unsigned i = 0; i < count; ++i)
    {
        const PortableMeshRacerState *racer = &race->state.racers[i];
        const PortableMeshKartState *kart = &racer->kart;
        const PortableMeshItemRacer *inventory = &race->items.inventory.racers[i];
        uint64_t held = 0, orbit = 0, pending = 0;
        for (unsigned a = 0; a < PORTABLE_MESH_ITEMS_ACTORS; ++a)
            if (race->items.actors[a].owner == i)
            {
                const uint8_t phase = race->items.actors[a].phase;
                if (phase == PORTABLE_MESH_ACTOR_HELD)
                    held |= UINT64_C(1) << a;
                if (phase == PORTABLE_MESH_ACTOR_ORBIT)
                    orbit |= UINT64_C(1) << a;
                if (phase == PORTABLE_MESH_ACTOR_ORBIT_PENDING)
                    pending |= UINT64_C(1) << a;
            }
        PortableMeshKartInput drive = {0};
        PortableMeshItemControls combat = {0};
        char detail[192] = {0};
        const PortableMeshStatus status = portable_mesh_battle_pilot(race, i, &drive, &combat,
                                                                    detail, sizeof detail);
        fprintf(stderr, "3D Battle aid failure: token=%c tick=%u racer=%u balloons=%u "
            "position=%d,%d,%d forward=%d,%d,%d speed=%d steer_filtered=%d "
            "held=%04x active=%u timeline=%d phase=%u respawn=%u grace=%u "
            "actors_held=%016" PRIx64 " actors_orbit=%016" PRIx64 " actors_pending=%016" PRIx64
            " pilot=%d buttons=%u steer=%d item=%u aim=%u,%u detail=%s\n",
            token, (unsigned)race->state.tick, i, (unsigned)race->battle.balloons[i],
            (int)kart->position_q16[0], (int)kart->position_q16[1], (int)kart->position_q16[2],
            (int)kart->forward_q30[0], (int)kart->forward_q30[1], (int)kart->forward_q30[2],
            (int)kart->speed_q16, (int)kart->steer_filtered_q16,
            (unsigned)inventory->held_item, (unsigned)inventory->active, (int)inventory->timeline_index,
            (unsigned)racer->phase, (unsigned)kart->respawn_ticks, (unsigned)racer->respawn_grace,
            held, orbit, pending, (int)status, (unsigned)drive.buttons, (int)drive.steering,
            (unsigned)combat.item_pressed, (unsigned)combat.aim_backward, (unsigned)combat.aim_forward,
            detail);
    }
}

/* Optional bounded preturn diagnostics. Parsing is local and overflow-checked;
 * no cached state, accepted input or simulation owner is modified. */
static int game_shell_mesh_trace_number(const char **cursor, uint32_t *out)
{
    const char *p=*cursor;
    uint32_t value=0;
    if (*p<'0' || *p>'9') return 0;
    do
    {
        const uint32_t digit=(uint32_t)(*p-'0');
        if (value>(UINT32_MAX-digit)/10u) return 0;
        value=value*10u+digit;
        ++p;
    } while (*p>='0' && *p<='9');
    *cursor=p;
    *out=value;
    return 1;
}
static int game_shell_mesh_trace(const PortableMeshRacer *racer,
    const PortableMeshTimeTrialPilotState *pilot, unsigned player, char token,
    PortableMeshKartInput input)
{
    const char *range=getenv("MKSC_AID_MESH_TRACE");
    if (range==NULL) return 1;
    const char *cursor=range;
    uint32_t first=0,last=0;
    int valid=game_shell_mesh_trace_number(&cursor,&first);
    if (valid && *cursor==':')
    {
        ++cursor;
        valid=game_shell_mesh_trace_number(&cursor,&last);
    }
    else valid=0;
    if (!valid || *cursor || last<first || last-first>=4096u)
    {
        fprintf(stderr,"3D course=%u mode=%u tick=%u aid: invalid MKSC_AID_MESH_TRACE; "
            "expected START:END inclusive, at most4096 ticks\n",racer->course->id,racer->mode,
            racer->state.tick);
        return 0;
    }
    const PortableMeshRacerState *state=&racer->state;
    if (state->tick<first || state->tick>last) return 1;
    const PortableMeshKartState *kart=&state->kart;
    printf("3D aid trace: course=%u mode=%u tick=%u player=%u token=%c "
        "pos=%d,%d,%d velocity=%d,%d,%d forward=%d,%d,%d grounded=%u "
        "surface=%u triangle=%u panel=%u boosts=%u,%u,%u "
        "respawn=%u grace=%u last_respawn=%u lap=%u gate=%u armed=%u phase=%u "
        "route_active=%u route=%u from=%u to=%u edge=%u buttons=%u steer=%d "
        "speed=%d outside=%d drift=%d charge=%u tier=%u\n",
        racer->course->id,racer->mode,state->tick,player,token,
        kart->position_q16[0],kart->position_q16[1],kart->position_q16[2],
        kart->velocity_q16[0],kart->velocity_q16[1],kart->velocity_q16[2],
        kart->forward_q30[0],kart->forward_q30[1],kart->forward_q30[2],kart->grounded,
        kart->contact_surface,kart->contact_triangle,kart->panel_ticks,
        kart->boost_ticks[0],kart->boost_ticks[1],kart->boost_ticks[2],
        kart->respawn_ticks,state->respawn_grace,state->last_respawn,state->lap,
        state->checkpoint,state->crossing_armed,state->phase,pilot->active,pilot->cpu.route,
        pilot->cpu.from_node,pilot->cpu.to_node,pilot->cpu.selected_edge,input.buttons,input.steering,
        kart->speed_q16,kart->outside_drift_q16,kart->drift_direction,
        kart->drift_charge,kart->drift_tier);
    PortableCourse3DCheckpoint current;
    if (!portable_course_3d_checkpoint(racer->course,state->checkpoint,&current))
    {
        printf("3D aid gates: course=%u tick=%u player=%u invalid_current=%u\n",
            racer->course->id,state->tick,player,state->checkpoint);
        return 1;
    }
    /* Bound even unusual authored branching graphs; report omitted edges. */
    const uint32_t count=current.edge_count<16u ? current.edge_count : 16u;
    for (uint32_t edge=0;edge<count;++edge)
    {
        uint32_t target;
        PortableCourse3DCheckpoint gate;
        if (current.edge_first>UINT32_MAX-edge ||
            !portable_course_3d_checkpoint_edge(racer->course,current.edge_first+edge,&target) ||
            !portable_course_3d_checkpoint(racer->course,target,&gate))
        {
            printf("3D aid gates: course=%u tick=%u player=%u invalid_edge=%u\n",
                racer->course->id,state->tick,player,edge);
            continue;
        }
        printf("3D aid gate: course=%u tick=%u player=%u from=%u to=%u "
            "center=%d,%d,%d normal=%d,%d,%d right=%d,%d,%d half=%d,%d flags=%u stage=%u key=%u\n",
            racer->course->id,state->tick,player,state->checkpoint,target,
            gate.center_q16[0],gate.center_q16[1],gate.center_q16[2],
            gate.normal_q30[0],gate.normal_q30[1],gate.normal_q30[2],
            gate.right_q30[0],gate.right_q30[1],gate.right_q30[2],
            gate.half_width_q16,gate.half_height_q16,gate.flags,gate.stage,gate.key_group);
    }
    if (count<current.edge_count)
        printf("3D aid gates: course=%u tick=%u player=%u omitted=%u\n",
            racer->course->id,state->tick,player,current.edge_count-count);
    return 1;
}

typedef struct
{
    uint32_t clock, phase, other_phase;
} MeshSkyRetryObservation;

static MeshSkyRetryObservation mesh_sky_retry_before(GameShell *shell, uint16_t pressed)
{
    MeshSkyRetryObservation observed = {0};
    PortableCourse3DAudioView view;
    if (shell->aid && shell->aid->tour && (pressed & PORTABLE_FRONTEND_BUTTON_A) &&
        portable_course_3d_host_audio(shell->preview_3d, &view) && view.source_actors &&
        view.source_actor_config && view.source_actor_config->sky.progress.enabled)
    {
        observed.clock = view.source_actors->sky.clock;
        observed.phase = view.source_actors->sky.motion.phase;
        observed.other_phase = view.source_actors->sky.other_motion[0].phase;
    }
    return observed;
}

static void mesh_sky_retry_after(GameShell *shell, MeshSkyRetryObservation before)
{
    PortableCourse3DAudioView view;
    if (!before.clock || !portable_course_3d_host_audio(shell->preview_3d, &view) ||
        !view.source_actors || !view.source_actor_config ||
        !view.source_actor_config->sky.progress.enabled || view.source_actors->sky.clock)
        return;
    const PortableMeshSky *sky = &view.source_actors->sky;
    printf("3D sky aid reset: course=%u controller=%u before_tick=%u before_phase=%u "
           "tick=%u phase=%u progress=%u checksum=%016llx before_other_phase=%u "
           "other_phase=%u other_progress=%u\n", view.course_id,
        view.source_actor_config->sky.rules.controller, before.clock, before.phase, sky->clock,
        sky->motion.phase, sky->progress[0].high_water,
        (unsigned long long)portable_mesh_sky_checksum(&view.source_actor_config->sky, sky),
        before.other_phase, sky->other_motion[0].phase, sky->progress[1].high_water);
}

/* Settings consume an admitted tick with neutral frontend input; they do
 * not bypass the visible title animation, replay writer or audio owner. */
static GameShellMenuTick game_shell_tick_settings(GameShell *shell, uint16_t buttons)
{
    uint16_t pressed = (uint16_t)(buttons & ~shell->options_buttons);
    shell->options_buttons = buttons;
    if (shell->editor_previous_preview && !portable_course_3d_host_active(shell->preview_3d)) {
        shell->preview_3d=shell->editor_previous_preview;
        shell->editor_previous_preview=NULL;
        course_editor_host_test_return(shell->editor);
        shell->wait_for_neutral=1;
    }
    if (shell->editor && !shell->editor_previous_preview) {
        course_editor_host_tick(shell->editor,buttons);
        if (!course_editor_host_active(shell->editor)) {
            course_editor_host_close(shell->editor);shell->editor=NULL;
            (void)portable_mod_manager_refresh(shell->mod_manager);
            shell->wait_for_neutral=1;
        } else {
            PortableCourse3DHost *test=course_editor_host_test(shell->editor,
                shell->pack_3d ? shell->pack_3d : shell->pack,&shell->settings);
            if(test){shell->editor_previous_preview=shell->preview_3d;shell->preview_3d=test;}
        }
        shell->needs_present=1;
        return GAME_SHELL_TICK_OPTIONS;
    }
    if (portable_course_3d_host_active(shell->preview_3d))
    {
        const unsigned locals = portable_course_3d_host_local_count(shell->preview_3d);
        if (locals != 0u)
        {
            PortableMeshKartInput inputs[PORTABLE_MESH_LOCAL_MAX];
            portable_course_3d_host_sample_locals(shell->preview_3d, inputs, &buttons);
            const uint16_t pressed_driving = shell->mesh_driving_pressed |
                portable_mobile_touch_take_driving_pressed();
            shell->mesh_driving_pressed = 0;
            if (pressed_driving & PORTABLE_INPUT_ACCELERATE) inputs[0].buttons |= PORTABLE_MESH_ACCELERATE;
            if (pressed_driving & PORTABLE_INPUT_BRAKE) inputs[0].buttons |= PORTABLE_MESH_BRAKE;
            if (pressed_driving & PORTABLE_INPUT_HOP) inputs[0].buttons |= PORTABLE_MESH_HOP;
            if (shell->aid != NULL && shell->aid->tour != NULL &&
                (shell->aid->tour[shell->tour.index] == 'G' ||
                 shell->aid->tour[shell->tour.index] == 'Y' ||
             shell->aid->tour[shell->tour.index] == 'J' ||
             shell->aid->tour[shell->tour.index] == 'T' ||
             shell->aid->tour[shell->tour.index] == 'K' ||
             shell->aid->tour[shell->tour.index] == 'I'))
            {
                const PortableMeshRaceSession *race = portable_course_3d_host_race(shell->preview_3d);
                if (race && race->state.tick % 600u == 0u && race->mode != 16u)
                    for (unsigned player = 0; player < locals; ++player)
                    {
                        const PortableMeshRacerState *racer = &race->state.racers[player];
                        printf("3D shared route aid: course=%u tick=%u player=%u lap=%u gate=%u "
                            "pos=%d,%d,%d speed=%d respawn_ticks=%u\n", race->course->id, race->state.tick,
                            player, racer->lap, racer->checkpoint, racer->kart.position_q16[0],
                            racer->kart.position_q16[1], racer->kart.position_q16[2],
                            racer->kart.speed_q16, racer->kart.respawn_ticks);
                    }
                if (race == NULL || (race->state.tick > 12000u &&
                    race->state.tick > portable_mesh_time_trial_pilot_tick_budget(race->course)))
                {
                    game_shell_battle_aid_failure(race, shell->aid->tour[shell->tour.index]);
                    shell->failure_detail = "3D shared race aid requires a live race within its tick budget";
                    shell->error = 1;
                    return GAME_SHELL_TICK_FAILED;
                }
                for (unsigned player = 0; player < locals; ++player)
                {
                    /* The GP aid drives its planned corners with a margin, as CPUs
                     * drive their class share: at the planned limit a crest corner
                     * reached airborne is overshot. */
                    PortableMeshKartRules aid_rules = *race->rules;
                    const uint32_t aid_course = portable_course_3d_host_gp_course_index(shell->preview_3d);
                    if (race->mode == 1u) /* 0.9, less 0.05 per retry of this course */
                        aid_rules.cpu_pace_ratio_q16 = 58982 - 3277 *
                            (aid_course < 4u && shell->tour.gp_attempt[aid_course] < 3u
                                 ? shell->tour.gp_attempt[aid_course] : 3);
                    const PortableMeshRacer racer = {.course = race->course, .rules = &aid_rules,
                        .fall_limit_q16 = race->fall_limit_q16, .spawn_grid = race->spawn_grids[player],
                        .mode = race->mode, .state = race->state.racers[player],
                        .geometry = race->geometry};
                    char detail[256];
                    PortableMeshItemControls combat = {0};
                    const PortableMeshTimeTrialPilotState previous_pilot = shell->mesh_pilot[player];
                    uint32_t route_hint = UINT32_MAX;
                    const uint32_t cup_index = portable_course_3d_host_gp_course_index(shell->preview_3d);
                    if (shell->mesh_gp_routes_enabled)
                    {
                        if (race->mode != 1u || cup_index >= 4u)
                        {
                            shell->failure_detail = "GP route hints require an active GP course";
                            shell->error = 1;
                            return GAME_SHELL_TICK_FAILED;
                        }
                        route_hint = shell->mesh_gp_routes[cup_index];
                    }
                    PortableMeshStatus status;
                    if (race->mode == 16u)
                        status = portable_mesh_battle_pilot(race, player, &inputs[player], &combat,
                            detail, sizeof detail);
                    else if (shell->mesh_gp_routes_enabled)
                        status = portable_mesh_time_trial_pilot_step_route(&shell->mesh_pilot[player],
                            &shell->mesh_pilot[player], &racer, route_hint, &inputs[player], detail,
                            sizeof detail);
                    else
                        status = portable_mesh_time_trial_pilot_step(&shell->mesh_pilot[player],
                            &shell->mesh_pilot[player], &racer, &inputs[player], detail, sizeof detail);
                    if (status != PORTABLE_MESH_OK)
                    {
                        fprintf(stderr, "3D course=%u mode=%u player=%u aid: %s\n", race->course->id,
                            race->mode, player + 1u, detail);
                        shell->failure_detail = "3D shared race ordinary route inputs";
                        shell->error = 1;
                        return GAME_SHELL_TICK_FAILED;
                    }
                    const PortableMeshTimeTrialPilotState *pilot = &shell->mesh_pilot[player];
                    if (shell->mesh_gp_routes_enabled && (!previous_pilot.active ||
                        previous_pilot.cpu.course_id != pilot->cpu.course_id ||
                        race->state.tick < previous_pilot.last_tick))
                    {
                        char requested[16];
                        if (route_hint == UINT32_MAX)
                            snprintf(requested, sizeof requested, "auto");
                        else
                            snprintf(requested, sizeof requested, "%u", route_hint);
                        printf("3D GP route hint: course=%u index=%u requested=%s route=%u\n",
                            race->course->id, cup_index, requested, pilot->cpu.route);
                    }
                    if (!player && race->mode != 16u &&
                        !game_shell_mesh_hazard_seek(shell, &racer, &inputs[player]))
                    {
                        shell->failure_detail = "3D shared race hazard seek aid";
                        shell->error = 1;
                        return GAME_SHELL_TICK_FAILED;
                    }
                    if (!game_shell_mesh_segment_input(shell, &racer, player, &inputs[player]))
                    {
                        shell->failure_detail = "3D shared race ordinary input segment";
                        shell->error = 1;
                        return GAME_SHELL_TICK_FAILED;
                    }
                    if (!game_shell_mesh_trace(&racer,pilot,player,
                        shell->aid->tour[shell->tour.index],inputs[player]))
                    {
                        shell->failure_detail="invalid bounded 3D aid trace range";
                        shell->error=1;
                        return GAME_SHELL_TICK_FAILED;
                    }
                    if (race->mode != 16u && pilot->active && (!previous_pilot.active ||
                        previous_pilot.cpu.course_id != pilot->cpu.course_id ||
                        previous_pilot.cpu.route != pilot->cpu.route ||
                        race->state.tick < previous_pilot.last_tick))
                        printf("3D route aid: course=%u mode=%u player=%u route=%u tick=%u\n",
                            race->course->id, race->mode, player + 1u, pilot->cpu.route,
                            race->state.tick);
                    if (race->items_enabled && race->mode == 16u)
                    {
                        if (shell->aid->tour[shell->tour.index] != 'G')
                            combat = (PortableMeshItemControls){0};
                        portable_course_3d_host_item_input(shell->preview_3d, player, combat);
                    }
                    else if (race->items_enabled)
                    {
                        const char token=shell->aid->tour[shell->tour.index];
                        PortableMeshItemControls controls=token=='J' ? (PortableMeshItemControls){0} :
                            (PortableMeshItemControls){
                                .item_pressed=(uint8_t)((token=='G' && race->state.tick%120u<30u) ||
                                    ((token=='K' || (token=='T' && race->mode==1u)) && race->state.tick%120u==0u)),
                                .aim_backward=(uint8_t)((race->state.tick/240u)&1u),
                                .aim_forward=(uint8_t)(!((race->state.tick/240u)&1u))};
                        if (token=='G' && pilot->active &&
                            (race->mode==1u || race->mode==4u || race->mode==8u))
                        {
                            controls.item_pressed=portable_mesh_time_trial_pilot_item_press_rules(&racer,pilot,
                                &race->items.inventory.racers[player],&race->items.status.racers[player],race->item_rules,
                                inputs[player],controls.item_pressed);
                            controls.item_pressed=portable_mesh_time_trial_pilot_defensive_hold(
                                &race->items,&racer.state,race->item_rules,player,inputs[player],controls.item_pressed);
                        }
                        if (!player && game_shell_hazard_seek_live(shell))
                            controls.item_pressed=portable_mesh_time_trial_pilot_seek_item(
                                &shell->mesh_hazard_seek,controls.item_pressed);
                        portable_course_3d_host_item_input(shell->preview_3d,player,controls);
                    }
                }
            }
            const MeshSkyRetryObservation sky_retry = mesh_sky_retry_before(shell, pressed);
            portable_course_3d_host_tick_locals(shell->preview_3d, inputs, buttons);
            mesh_sky_retry_after(shell, sky_retry);
            shell->needs_present = 1;
            if (!portable_course_3d_host_active(shell->preview_3d))
                shell->wait_for_neutral = 1; /* The closing press stays with the 3D host. */
            if (portable_course_3d_host_launch_failed(shell->preview_3d))
            {
                fprintf(stderr, "3D cup launch failed: %s\n", portable_course_3d_host_error(shell->preview_3d));
                shell->failure_detail = "3D cup course could not start in the selected mode";
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
            return GAME_SHELL_TICK_OPTIONS;
        }
        uint16_t driving = portable_sdl_keyboard_driving() |
            portable_sdl_gamepad_driving(shell->controller) | shell->mesh_driving_pressed |
            portable_mobile_touch_take_driving_pressed();
        shell->mesh_driving_pressed = 0u;
        PortableMeshKartInput input = {0};
        if (driving & PORTABLE_INPUT_ACCELERATE) input.buttons |= PORTABLE_MESH_ACCELERATE;
        if (driving & PORTABLE_INPUT_BRAKE) input.buttons |= PORTABLE_MESH_BRAKE;
        if (driving & PORTABLE_INPUT_HOP) input.buttons |= PORTABLE_MESH_HOP;
        int16_t x = 0, y = 0;
        portable_sdl_gamepad_stick(shell->controller, &x, &y);
        const int32_t magnitude = x < 0 ? -(int32_t)x : x;
        const int32_t deadzone = shell->settings.steering_deadzone;
        if (magnitude > deadzone)
        {
            const int32_t scaled = (magnitude - deadzone) * 32767 / (32768 - deadzone);
            input.steering = (int16_t)(x < 0 ? -scaled : scaled);
        }
        const uint16_t digital = portable_sdl_keyboard_driving();
        if ((digital & PORTABLE_INPUT_LEFT) || (shell->controller != NULL &&
            SDL_GetGamepadButton(shell->controller, SDL_GAMEPAD_BUTTON_DPAD_LEFT)))
            input.steering = -32767;
        if ((digital & PORTABLE_INPUT_RIGHT) || (shell->controller != NULL &&
            SDL_GetGamepadButton(shell->controller, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)))
            input.steering = 32767;
        if (shell->aid != NULL && shell->aid->tour != NULL &&
            shell->aid->tour[shell->tour.index] == 'V')
        {
            const PortableMeshTimeTrialSession *session =
                portable_course_3d_host_session(shell->preview_3d);
            if (session == NULL || (session->state.tick > 3600u && !shell->tour.mesh_fall_seen))
            {
                shell->failure_detail = "3D Time Trial natural fall aid did not respawn";
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
            if (!shell->tour.mesh_fall_seen && session->state.kart.respawn_ticks != 0u)
            {
                shell->tour.mesh_fall_seen = 1u;
                printf("3D route aid: natural respawn course=%u tick=%u\n",
                    session->course->id, session->state.tick);
            }
            input = (PortableMeshKartInput){0};
            if (!shell->tour.mesh_fall_seen && session->state.phase == PORTABLE_MESH_TT_RACING)
                input = (PortableMeshKartInput){32767, PORTABLE_MESH_ACCELERATE};
        }
        if (shell->aid != NULL && shell->aid->tour != NULL &&
            (shell->aid->tour[shell->tour.index] == 'G' ||
             shell->aid->tour[shell->tour.index] == 'Y' ||
             shell->aid->tour[shell->tour.index] == 'J' ||
             shell->aid->tour[shell->tour.index] == 'T' ||
             shell->aid->tour[shell->tour.index] == 'I'))
        {
            const PortableMeshTimeTrialSession *session =
                portable_course_3d_host_session(shell->preview_3d);
            char detail[256];
            if (session != NULL && session->state.tick % 600u == 0u)
                printf("3D route aid: tick=%u lap=%u gate=%u pos=%d,%d,%d speed=%d\n",
                    session->state.tick, session->state.lap, session->state.checkpoint,
                    session->state.kart.position_q16[0], session->state.kart.position_q16[1],
                    session->state.kart.position_q16[2], session->state.kart.speed_q16);
            if (session != NULL && session->state.tick > 12000u &&
                session->state.tick > portable_mesh_time_trial_pilot_tick_budget(session->course))
            {
                shell->failure_detail = "3D Time Trial route aid exceeded its tick budget";
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
            if (session == NULL || portable_mesh_time_trial_pilot_step(&shell->mesh_pilot[0],
                    &shell->mesh_pilot[0], session, &input, detail, sizeof detail) != PORTABLE_MESH_OK)
            {
                shell->failure_detail = "3D Time Trial route aid";
                if (session != NULL) fprintf(stderr, "%s\n", detail);
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
            if (!game_shell_mesh_hazard_seek(shell, session, &input))
            {
                shell->failure_detail = "3D Time Trial hazard seek aid";
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
            if (!game_shell_mesh_segment_input(shell, session, 0, &input))
            {
                shell->failure_detail = "3D Time Trial ordinary input segment";
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
            if (!game_shell_mesh_trace(session, &shell->mesh_pilot[0], 0,
                    shell->aid->tour[shell->tour.index], input))
            {
                shell->failure_detail = "invalid bounded 3D aid trace range";
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
        }
        portable_course_3d_host_item_input(shell->preview_3d, 0, (PortableMeshItemControls){
            .item_pressed = (uint8_t)((driving & PORTABLE_INPUT_ITEM) != 0u),
            .aim_backward = (uint8_t)((driving & PORTABLE_INPUT_DOWN) != 0u ||
                y > shell->settings.steering_deadzone || (shell->controller &&
                SDL_GetGamepadButton(shell->controller, SDL_GAMEPAD_BUTTON_DPAD_DOWN))),
            .aim_forward = (uint8_t)((driving & PORTABLE_INPUT_UP) != 0u ||
                y < -(int32_t)shell->settings.steering_deadzone || (shell->controller &&
                SDL_GetGamepadButton(shell->controller, SDL_GAMEPAD_BUTTON_DPAD_UP)))});
        if (shell->aid && shell->aid->tour && (shell->aid->tour[shell->tour.index] == 'G' ||
            shell->aid->tour[shell->tour.index] == 'Y'))
        {
            PortableCourse3DAudioView accepted;
            if (portable_course_3d_host_audio(shell->preview_3d, &accepted) && accepted.items)
                portable_course_3d_host_item_input(shell->preview_3d, 0,
                    (PortableMeshItemControls){.item_pressed =
                        (uint8_t)(accepted.state->tick % 120u < 30u)});
        }
        else if (shell->aid && shell->aid->tour && shell->aid->tour[shell->tour.index] == 'J')
            portable_course_3d_host_item_input(shell->preview_3d, 0, (PortableMeshItemControls){0});
        /* Observe ordinary menu Retry; never alter the accepted owner. */
        const MeshSkyRetryObservation sky_retry = mesh_sky_retry_before(shell, pressed);
        portable_course_3d_host_tick_driving(shell->preview_3d, input, buttons);
        mesh_sky_retry_after(shell, sky_retry);
        shell->needs_present = 1;
        if (!portable_course_3d_host_active(shell->preview_3d))
            shell->wait_for_neutral = 1;
        if (portable_course_3d_host_launch_failed(shell->preview_3d))
        {
            fprintf(stderr, "3D cup launch failed: %s\n", portable_course_3d_host_error(shell->preview_3d));
            shell->failure_detail = "3D cup course could not start in the selected mode";
            shell->error = 1;
            return GAME_SHELL_TICK_FAILED;
        }
        return GAME_SHELL_TICK_OPTIONS;
    }
    /* A confirmed 3D cup row: race it (or fly through it) in the selected
     * mode. Menu tours alone keep Select on the 2D sets as a development
     * route into the format browser for older scripted suites. */
    const int mesh_request = shell->flow.mesh_request != 0u;
    if (!shell->options_open && shell->flow.page == PORTABLE_FRONTEND_PAGE_COURSE &&
        shell->flow.transition_ticks == 0u && portable_course_3d_host_available(shell->preview_3d) &&
        (mesh_request ||
         (shell->aid != NULL && shell->aid->tour != NULL && shell->flow.mesh_group == 0u &&
          shell->flow.course_phase < 2u && (pressed & PORTABLE_FRONTEND_BUTTON_SELECT))))
    {
        const int explore = shell->flow.mesh_request == PORTABLE_FRONTEND_MESH_EXPLORE;
        const uint32_t course = shell->flow.mesh_request_course;
        shell->flow.mesh_request = 0u;
        char *directory = shell->aid != NULL && shell->aid->save_directory != NULL
            ? SDL_strdup(shell->aid->save_directory) : portable_data_directory();
        const PortableAssetPackView *mesh_pack = shell->pack_3d != NULL ? shell->pack_3d : shell->pack;
        const uint32_t mode = portable_frontend_flow_mesh_mode(&shell->flow);
        if (shell->mesh_gp_routes_enabled && mode != 1u)
        {
            SDL_free(directory);
            shell->failure_detail = "GP route hints require a 3D GP cup";
            shell->error = 1;
            return GAME_SHELL_TICK_FAILED;
        }
        uint32_t drivers[PORTABLE_MESH_LOCAL_MAX] = {shell->flow.selected_character};
        if (shell->flow.players > 1u)
            for (unsigned player = 0; player < shell->flow.players; ++player)
                drivers[player] = shell->flow.player_characters[player];
        /* Rivals come from the locked platform's drivers. */
        uint32_t roster[PORTABLE_DRIVER_3D_CAPACITY];
        size_t roster_count = 0u;
        for (size_t d = 0u; d < shell->config.driver_count && roster_count < PORTABLE_DRIVER_3D_CAPACITY; ++d)
        {
            const PortableFrontendDriverSlot *slot = &shell->drivers[d];
            size_t seen = 0u;
            while (seen < roster_count && roster[seen] != slot->content_id)
                ++seen;
            if (slot->content_id && slot->available && slot->platform == shell->flow.platform &&
                seen == roster_count)
                roster[roster_count++] = slot->content_id;
        }
        (void)portable_course_3d_host_set_roster(shell->preview_3d, roster, roster_count);
        const int configured = directory != NULL && portable_course_3d_host_configure_race(
            shell->preview_3d, mesh_pack->deterministic_digest, mode, shell->flow.players, shell->flow.race_speed,
            drivers, directory, &shell->settings);
        SDL_free(directory);
        const uint32_t karts[PORTABLE_MESH_LOCAL_MAX] = {shell->flow.players > 1u ? 0u : shell->flow.selected_kart};
        if (!configured || !portable_course_3d_host_set_karts(shell->preview_3d, karts))
        {
            shell->failure_detail = "3D selected mode/player/driver configuration";
            shell->error = 1;
            return GAME_SHELL_TICK_FAILED;
        }
        shell->mesh_driving_pressed = 0u;
        portable_course_3d_host_set_graphics(shell->preview_3d,
            (PortableGraphicsHook){shell, game_shell_graphics_current, game_shell_graphics_edit});
        if (!mesh_request)
            portable_course_3d_host_show(shell->preview_3d);
        else if (!portable_course_3d_host_launch(shell->preview_3d, course, explore))
        {
            /* Unsupported content fails loudly, naming course and mode. */
            fprintf(stderr, "3D cup launch failed: %s\n", portable_course_3d_host_error(shell->preview_3d));
            shell->failure_detail = "3D cup course could not start in the selected mode";
            shell->error = 1;
            return GAME_SHELL_TICK_FAILED;
        }
        shell->needs_present = 1;
        return GAME_SHELL_TICK_OPTIONS;
    }
    if (!shell->options_open && (shell->aid == NULL || shell->aid->tour != NULL) &&
        shell->flow.page == PORTABLE_FRONTEND_PAGE_TITLE_MENU &&
        shell->flow.transition_ticks == 0u &&
        (shell->flow.short_transitions || shell->flow.page_ticks > 69u) &&
        shell->flow.title_cursor == 3u &&
        (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START)))
    {
        shell->options_open = 1;
        shell->options_cursor = 0u;
        shell->options_notice = NULL;
    }
    /* The modern options show their pages as tabs: L and R switch between them. */
    if (((shell->options_open >= 1 && shell->options_open <= 4)||shell->options_open==7) && !shell->binding_capture &&
        shell->game_import==NULL &&
        shell->mod_dialog == NULL &&
        !shell->mod_unsaved_open && !shell->store_open &&
        game_shell_modern_active(shell) &&
        (pressed & (PORTABLE_FRONTEND_BUTTON_L | PORTABLE_FRONTEND_BUTTON_R)))
    {
        const int current=shell->options_open==7?4:shell->options_open-1;
        const int next = (current + ((pressed & PORTABLE_FRONTEND_BUTTON_L) ? 4 : 1)) % 5;
        if(next>=3){
            if(game_shell_open_mod_manager(shell))shell->options_open=next==4?7:4;
        }else shell->options_open=next+1;
        shell->library_return=shell->options_open==7;
        if(shell->options_open==7)puts("Imported games library opened");
        shell->mod_action_open = 0u;
        shell->binding_cursor = 0u;
        shell->enhancements_cursor = 0u;
        shell->options_notice = NULL;
        pressed = 0u;
    }
    if (shell->options_open)
    {
        if (shell->options_open >= 4 && shell->options_open <= 7)
        {
            game_shell_mod_input(shell,pressed);
        }
        else if (shell->options_open == 2)
        {
            if (!shell->binding_capture)
            {
                if (pressed & PORTABLE_FRONTEND_BUTTON_B)
                {
                    shell->options_open = 1;
                    shell->options_notice = NULL;
                    shell->wait_for_neutral = 1;
                }
                else if ((pressed & PORTABLE_FRONTEND_BUTTON_UP) && shell->binding_cursor > 0u)
                    --shell->binding_cursor;
                else if ((pressed & PORTABLE_FRONTEND_BUTTON_DOWN) &&
                         shell->binding_cursor + 1u < BINDING_ROWS)
                    ++shell->binding_cursor;
                else if (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))
                {
                    shell->binding_capture = 1;
                    shell->options_notice = NULL;
                }
                else if (pressed & PORTABLE_FRONTEND_BUTTON_SELECT)
                {
                    PortableSettings defaults;
                    portable_settings_defaults(&defaults);
                    const PortableSetting field = binding_field(shell->binding_cursor);
                    const uint32_t value = portable_setting_get(&defaults, field);
                    (void)portable_setting_set(&shell->settings, field, value);
                    portable_sdl_input_configure(&shell->settings);
                    shell->options_notice = save_setting_edit(
                        shell->settings_path, &shell->persisted_settings, field, value);
                }
            }
        }
        else if (shell->options_open == 3)
        {
            if (pressed & PORTABLE_FRONTEND_BUTTON_B) {
                shell->options_open = 1;
                shell->options_notice = NULL;
                shell->wait_for_neutral = 1;
            } else if (pressed & PORTABLE_FRONTEND_BUTTON_SELECT) {
                (void)game_shell_open_mod_manager(shell);
            } else if (pressed & PORTABLE_FRONTEND_BUTTON_UP) {
                if(shell->enhancements_cursor>0u)--shell->enhancements_cursor;
                shell->options_notice=NULL;
            } else if (pressed & PORTABLE_FRONTEND_BUTTON_DOWN) {
                /* Menu Style is listed only on the modern page; the classic
                 * page switches back through the --no-classic-menus option. */
                if(shell->enhancements_cursor<(game_shell_modern_active(shell)?HD2D_PRESET_ROW+HD2D_ROWS:4u))
                    ++shell->enhancements_cursor;
                shell->options_notice=NULL;
            } else if (shell->enhancements_cursor==(game_shell_modern_active(shell)?IMMERSIVE_CAMERA_ROW:4u) &&
                       (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT |
                                   PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))) {
                const uint32_t value=!shell->settings.immersive_camera;
                (void)portable_setting_set(&shell->settings,PORTABLE_SETTING_IMMERSIVE_CAMERA,value);
                shell->options_notice=save_setting_edit(shell->settings_path,&shell->persisted_settings,
                    PORTABLE_SETTING_IMMERSIVE_CAMERA,value);
            } else if (game_shell_modern_active(shell) && shell->enhancements_cursor>=EFFECT_PRESET_ROW &&
                       (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT |
                                   PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))) {
                shell->options_notice=edit_effects(shell,shell->enhancements_cursor);
            } else if (shell->enhancements_cursor==(game_shell_modern_active(shell)?4u:3u) &&
                       (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT |
                                   PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))) {
                const uint32_t value=!shell->settings.race_renderer;
                (void)portable_setting_set(&shell->settings,PORTABLE_SETTING_RACE_RENDERER,value);
                shell->options_notice=save_setting_edit(shell->settings_path,&shell->persisted_settings,
                    PORTABLE_SETTING_RACE_RENDERER,value);
            } else if (shell->enhancements_cursor==(game_shell_modern_active(shell)?3u:2u) &&
                       (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT |
                                   PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))) {
                const unsigned delta=(pressed & PORTABLE_FRONTEND_BUTTON_LEFT)?2u:1u;
                const uint32_t value=(shell->settings.artwork_lod+delta)%3u;
                (void)portable_setting_set(&shell->settings,PORTABLE_SETTING_ARTWORK_LOD,value);
                shell->options_notice=save_setting_edit(shell->settings_path,&shell->persisted_settings,
                    PORTABLE_SETTING_ARTWORK_LOD,value);
            } else if (shell->enhancements_cursor==2u &&
                       (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT |
                                   PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))) {
                const uint32_t value = !shell->settings.classic_menus;
                (void)portable_setting_set(&shell->settings,PORTABLE_SETTING_CLASSIC_MENUS,value);
                shell->flow.short_transitions = shell->record_file != NULL ? 0u : menu_short_transitions(&shell->settings);
                shell->options_notice = save_setting_edit(shell->settings_path,&shell->persisted_settings,
                    PORTABLE_SETTING_CLASSIC_MENUS,value);
                shell->needs_present = 1;
            } else if (shell->enhancements_cursor==1u &&
                       (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT |
                                   PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START))) {
                const unsigned delta=(pressed & PORTABLE_FRONTEND_BUTTON_LEFT)?2u:1u;
                shell->profile_request=(uint8_t)((shell->settings.driving_profile+delta)%3u+1u);
            } else if (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT)) {
                const uint32_t value = !shell->settings.wide_ui_backgrounds;
                (void)portable_setting_set(&shell->settings,PORTABLE_SETTING_WIDE_UI_BACKGROUNDS,value);
                shell->options_notice = save_setting_edit(shell->settings_path,&shell->persisted_settings,
                    PORTABLE_SETTING_WIDE_UI_BACKGROUNDS,value);
            } else if (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START)) {
                (void)portable_ui_background_host_choose(&shell->ui_background,shell->window);
                /* The stationary owner's notice also reflects cancellation. */
                shell->options_notice = portable_ui_background_host_notice(&shell->ui_background);
            }
        }
        else if (pressed & PORTABLE_FRONTEND_BUTTON_SELECT)
        {
            shell->options_open = 3;
            shell->enhancements_cursor=0u;
            shell->options_notice = NULL;
        }
        else if (shell->options_cursor == PORTABLE_SETTING_DEADZONE &&
                 (pressed & (PORTABLE_FRONTEND_BUTTON_A | PORTABLE_FRONTEND_BUTTON_START)))
        {
            shell->options_open = 2;
            shell->binding_cursor = 0u;
            shell->options_notice = NULL;
        }
        else if (pressed & PORTABLE_FRONTEND_BUTTON_B)
        {
            game_shell_mod_exit(shell,2u);
        }
        else if (pressed & PORTABLE_FRONTEND_BUTTON_UP)
        {
            if (shell->options_cursor > 0u)
                --shell->options_cursor;
        }
        else if (pressed & PORTABLE_FRONTEND_BUTTON_DOWN)
        {
            if (shell->options_cursor + 1u < OPTION_ROWS)
                ++shell->options_cursor;
        }
        else if (pressed & (PORTABLE_FRONTEND_BUTTON_LEFT | PORTABLE_FRONTEND_BUTTON_RIGHT))
        {
            const PortableSetting field = option_fields[shell->options_cursor];
            const int delta = (pressed & PORTABLE_FRONTEND_BUTTON_LEFT) ? -1 : 1;
            const int old = (int)portable_setting_get(&shell->settings, field);
            const int value = portable_option_step(field, old, delta);
            if (value >= 0)
                (void)game_shell_commit_setting(shell, field, (uint32_t)value, &shell->options_notice);
        }
        /* Settings own the buttons while the visible title keeps
         * its fixed-tick animation. Record the same neutral ticks. */
        if (!portable_frontend_flow_tick(&shell->flow, 0u))
        {
            shell->failure_detail = "options background tick";
            shell->error = 1;
            return GAME_SHELL_TICK_FAILED;
        }
        if (shell->record_file != NULL)
        {
            shell->record_status = portable_frontend_input_replay_write_tick_file(
                shell->record_file, shell->record_tick++, 0u, &shell->flow);
            if (shell->record_status != PORTABLE_FRONTEND_INPUT_REPLAY_OK)
            {
                shell->error = 1;
                return GAME_SHELL_TICK_FAILED;
            }
        }
        if (!route_audio_render(shell->audio, 0u, 0, NULL))
        {
            shell->failure_detail = "options audio render";
            shell->error = 1;
            return GAME_SHELL_TICK_FAILED;
        }
        return GAME_SHELL_TICK_OPTIONS;
    }
    return GAME_SHELL_TICK_MENU;
}

static void game_shell_record_actions(GameShell *shell)
{
    while (shell->next_record_event < shell->flow.event_count)
    {
        const PortableFrontendEvent *record_event = &shell->flow.events[shell->next_record_event++];
        if (record_event->type == PORTABLE_FRONTEND_EVENT_MOVE ||
            record_event->type == PORTABLE_FRONTEND_EVENT_BACK ||
            record_event->type == PORTABLE_FRONTEND_EVENT_TRACK_SET)
            shell->records.notice = NULL;
        if (record_event->type != PORTABLE_FRONTEND_EVENT_RECORD_ERASE &&
            record_event->type != PORTABLE_FRONTEND_EVENT_GHOST_ERASE &&
            record_event->type != PORTABLE_FRONTEND_EVENT_GHOST_EXPORT &&
            record_event->type != PORTABLE_FRONTEND_EVENT_GHOST_RECEIVE &&
            record_event->type != PORTABLE_FRONTEND_EVENT_RECEIVED_GHOST_ERASE)
            continue;
        char *directory = portable_data_directory();
        PortableTimeTrialGhostStoreStatus status = PORTABLE_TIME_TRIAL_GHOST_STORE_IO_ERROR;
        int receive_mods_differ = 0;
        if (record_event->type == PORTABLE_FRONTEND_EVENT_GHOST_EXPORT ||
            record_event->type == PORTABLE_FRONTEND_EVENT_GHOST_RECEIVE)
        {
            if (directory != NULL)
            {
                char fallback[4096];
                const int size = snprintf(fallback, sizeof fallback, "%sincoming.ghost", directory);
                if (record_event->type == PORTABLE_FRONTEND_EVENT_GHOST_EXPORT)
                    status = portable_time_trial_ghost_export(shell->pack, directory,
                                                              record_event->value);
                else if (shell->incoming_ghost[0] != '\0' ||
                         (size > 0 && size < (int)sizeof fallback))
                {
                    const char *incoming =
                        shell->incoming_ghost[0] != '\0' ? shell->incoming_ghost : fallback;
                    status = portable_time_trial_ghost_receive(shell->pack, incoming, directory,
                                                               record_event->value);
                    char reason[2200];
                    receive_mods_differ = status == PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE &&
                                          portable_time_trial_ghost_store_describe_refusal(
                                              incoming, reason, sizeof reason);
                }
            }
            shell->records.notice =
                status == PORTABLE_TIME_TRIAL_GHOST_STORE_OK
                    ? (record_event->type == PORTABLE_FRONTEND_EVENT_GHOST_EXPORT
                           ? "EXPORTED TO SAVE FOLDER"
                           : "GHOST RECEIVED - TIMES KEPT")
                : status == PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND
                    ? "NO FILE - DROP A GHOST FILE HERE"
                : status == PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE
                    ? (receive_mods_differ ? "FILE NEEDS OTHER MODS"
                                           : "FILE USES DIFFERENT CONTENT")
                : status == PORTABLE_TIME_TRIAL_GHOST_STORE_INVALID
                    ? "INVALID GHOST - SLOT KEPT"
                    : "FILE OPERATION FAILED - SLOT KEPT";
            shell->records.erase_failed = 0u;
        }
        else
        {
            const int erased =
                directory != NULL &&
                (record_event->type == PORTABLE_FRONTEND_EVENT_RECEIVED_GHOST_ERASE
                     ? portable_time_trial_ghost_received_erase(directory, record_event->value)
                 : record_event->type == PORTABLE_FRONTEND_EVENT_GHOST_ERASE
                     ? portable_time_trial_records_erase_ghost(directory, record_event->value)
                     : portable_time_trial_records_erase(directory, record_event->value));
            shell->records.erase_failed = (uint8_t)!erased;
            shell->records.notice = NULL;
        }
        SDL_free(directory);
        shell->records.course_id = 0u;
    }
}

/* Preserve admission, event/save delivery, audio, replay recording and event
 * consumption order for every fixed menu tick, including catch-up batches. */
static int game_shell_advance_menu(GameShell *shell, uint64_t now)
{
    uint16_t tick_buttons[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t tick_count = 0u;
    int dropped = 0;
    shell->keyboard_pressed |= portable_mobile_touch_take_pressed();
    uint16_t live_buttons = frontend_buttons(
        shell->controller, (uint16_t)(shell->keyboard_buttons | shell->keyboard_pressed |
                                      frontend_keyboard_state_buttons()));
    if (shell->wait_for_neutral)
    {
        if (live_buttons == 0u)
            shell->wait_for_neutral = 0;
        live_buttons = 0u;
    }
    /* A fractional presentation must retain the last sampled tour input.
     * Predict admission on a copy so only the real scheduler accepts ticks. */
    int tour_admission = 1;
    if (shell->mesh_aid_fractional)
    {
        PortableFrameScheduler probe = shell->scheduler;
        uint16_t probe_buttons[PORTABLE_FRAME_MAX_CATCH_UP];
        size_t admitted = 0u;
        int discarded = 0;
        if (portable_frame_scheduler_update(&probe, now, shell->focused, 0u,
            probe_buttons, PORTABLE_FRAME_MAX_CATCH_UP, &admitted, &discarded) !=
            PORTABLE_FRAME_SCHEDULER_OK)
            return 0;
        tour_admission = admitted != 0u;
    }
    if (shell->aid != NULL && shell->aid->tour != NULL && !shell->wait_for_neutral &&
        tour_admission)
    {
        int finished = 0;
        const PortableMeshTimeTrialSession *mesh = portable_course_3d_host_session(shell->preview_3d);
        const PortableMeshRaceSession *mesh_race = portable_course_3d_host_race(shell->preview_3d);
        /* With MKSC_AID_GP_RETRY_TOKENS the script names one race token per
         * cup course. A failed race retried replays its own token; the aid varies only its corner
         * margin per attempt, since an identical replay finishes identically. */
        const uint32_t gp_course = portable_course_3d_host_gp_course_index(shell->preview_3d);
        if (mesh_race && gp_course < 4u && strchr("GJK", shell->aid->tour[shell->tour.index]) &&
            getenv("MKSC_AID_GP_RETRY_TOKENS"))
        {
            const uint32_t credits = portable_course_3d_host_gp_retry_credits(shell->preview_3d);
            size_t *token = &shell->tour.gp_race_token[gp_course];
            if (*token && *token - 1u < shell->tour.index &&
                credits < shell->tour.gp_race_credits[gp_course])
            {
                ++shell->tour.gp_attempt[gp_course];
                printf("3D GP aid retry: course=%u token=%zu replaces=%zu attempt=%u\n", gp_course,
                       *token - 1u, shell->tour.index, shell->tour.gp_attempt[gp_course]);
                shell->tour.index = *token - 1u;
            }
            else if (!*token || *token - 1u > shell->tour.index)
            {
                *token = shell->tour.index + 1u;
                shell->tour.gp_attempt[gp_course] = 0;
            }
            shell->tour.gp_race_credits[gp_course] = credits;
        }
        const char mesh_token = shell->aid->tour[shell->tour.index];
        if (mesh_token == 'K' && (mesh_race == NULL || mesh_race->mode != 1u))
        {
            shell->failure_detail = "K item aid requires a live shared 3D GP race";
            shell->error = 1;
            return 0;
        }
        int waiting_mesh_sky = 0;
        if (mesh_token == 'Y')
        {
            PortableCourse3DAudioView accepted;
            const int shared_sky = mesh_race && mesh_race->mode == 8u &&
                portable_course_3d_host_local_count(shell->preview_3d) == 2u;
            if ((!mesh && !shared_sky) || !portable_course_3d_host_audio(shell->preview_3d,&accepted) ||
                !accepted.source_actors || !accepted.source_actor_config ||
                !accepted.source_actor_config->sky.progress.enabled || accepted.finished ||
                (shared_sky && accepted.source_actor_config->sky.rules.controller != 2u))
            {
                shell->failure_detail = "sky phase aid requires a live progress TT or two-player speed-pan VS";
                shell->error = 1;
                return 0;
            }
            const PortableMeshSky *sky=&accepted.source_actors->sky;
            waiting_mesh_sky = shared_sky
                ? sky->motion.phase != 0 || sky->other_motion[0].phase == 0
                : accepted.source_actor_config->sky.rules.controller == PORTABLE_SKY_MOTION_SUNSET
                    ? sky->motion.phase != 6 : sky->motion.phase == 0;
            if (!waiting_mesh_sky && shell->tour.idle + 1u >=
                (shell->tour.wait ? shell->tour.wait : 30u))
            {
                if (shared_sky)
                {
                    const uint64_t checksum = portable_mesh_sky_checksum(
                        &accepted.source_actor_config->sky, sky);
                    for (unsigned racer = 0; racer < 2u; ++racer)
                    {
                        PortableSkyMotionSnapshot snapshot;
                        if (!portable_mesh_sky_snapshot_racer(&accepted.source_actor_config->sky,
                                                              sky, racer, 0, &snapshot))
                        {
                            shell->failure_detail = "sky VS aid snapshot";
                            shell->error = 1;
                            return 0;
                        }
                        printf("3D sky VS aid: course=%u controller=%u racer=%u tick=%u phase=%u "
                               "progress=%u yaw=0 scroll0=%d scroll1=%d checksum=%016llx\n",
                            accepted.course_id, accepted.source_actor_config->sky.rules.controller,
                            racer, sky->clock, racer ? sky->other_motion[0].phase : sky->motion.phase,
                            sky->progress[racer].high_water, snapshot.scroll_x_q16[0],
                            snapshot.scroll_x_q16[1], (unsigned long long)checksum);
                    }
                }
                else
                printf("3D sky aid: accepted course=%u controller=%u phase=%u tick=%u progress=%u\n",
                    accepted.course_id,accepted.source_actor_config->sky.rules.controller,
                    sky->motion.phase,sky->clock,sky->progress[0].high_water);
            }
        }
        int waiting_mesh_item = 0;
        if (mesh_token == 'I')
        {
            PortableCourse3DAudioView accepted;
            uint16_t held = 0;
            if (!portable_course_3d_host_audio(shell->preview_3d, &accepted) || !accepted.items ||
                portable_mesh_items_hud(accepted.items, 0u, &held) != PORTABLE_MESH_ITEMS_OK ||
                accepted.finished)
            {
                shell->failure_detail = "mesh item capture aid requires a live declared item owner";
                shell->error = 1;
                return 0;
            }
            waiting_mesh_item = accepted.state->phase != PORTABLE_MESH_RACER_RACING ||
                accepted.items->inventory.racers[0].active || held <= 2u;
            if (waiting_mesh_item)
                shell->tour.mesh_item_settled_frames = 0;
            else if (shell->tour.mesh_item_settled_frames < 2u)
                ++shell->tour.mesh_item_settled_frames;
            /* Tour captures the previously presented frame before this tick's
             * draw. Give the accepted inventory one draw before capturing it. */
            waiting_mesh_item |= shell->tour.mesh_item_settled_frames < 2u;
            if (!waiting_mesh_item && shell->tour.idle + 1u >=
                (shell->tour.wait ? shell->tour.wait : 30u))
                printf("3D item aid: settled course=%u item=%04x tick=%u\n",
                       accepted.course_id, held, accepted.state->tick);
        }
        else
            shell->tour.mesh_item_settled_frames = 0;
        const int waiting_mesh_finish = waiting_mesh_sky || waiting_mesh_item ||
            ((mesh_token == 'G' || mesh_token == 'J' || mesh_token == 'K') && (mesh_race != NULL ? !mesh_race->state.results_ready :
                mesh != NULL && mesh->state.phase != PORTABLE_MESH_TT_FINISHED)) ||
            (mesh_token == 'V' && mesh != NULL &&
                (!shell->tour.mesh_fall_seen || mesh->state.kart.respawn_ticks != 0u));
        if (((mesh_token == 'G' || mesh_token == 'J' || mesh_token == 'T') && mesh == NULL && mesh_race == NULL) ||
            (mesh_token == 'V' && mesh == NULL))
        {
            shell->failure_detail = "mesh race aid requires a live compatible 3D race";
            shell->error = 1;
            return 0;
        }
        /* Tours finish the current press, then wait for asynchronous imports
         * and store transfers rather than consuming inputs while busy. */
        if (!waiting_mesh_finish && ((shell->game_import == NULL && shell->store_job == NULL) || shell->tour.hold != 0u))
        {
            /* A cup launch leaves its confirming press in the paused flow;
             * while the 3D host owns input, only its own state matters. */
            PortableFrontendFlow *tour_flow = &shell->flow, quiet;
            if (portable_course_3d_host_active(shell->preview_3d) && shell->flow.buttons != 0u)
            {
                quiet = shell->flow;
                quiet.buttons = 0u;
                tour_flow = &quiet;
            }
            live_buttons = menu_tour_button(tour_flow, shell->aid, &shell->tour, &shell->frame,
                                        (game_shell_modern_active(shell) || portable_course_3d_host_active(shell->preview_3d))
                                            ? &shell->modern : NULL,
                                        shell->renderer,shell->settings.touch_controls,
                                        &finished, &shell->failure_detail);
        }
        if (shell->failure_detail != NULL)
        {
            shell->error = 1;
            return 0;
        }
        if (finished)
        {
            if (shell->mesh_benchmark && shell->mesh_benchmark_samples != 300u)
            {
                shell->failure_detail = "3D benchmark incomplete300presentations";
                shell->error = 1;
                return 0;
            }
            printf("Menu tour: PASS presses=%zu screens=%u page=%u ticks=%u short=%u\n",
                   shell->tour.index, (unsigned)shell->tour.shots, shell->flow.page,
                   shell->flow.tick, (unsigned)shell->flow.short_transitions);
            if (shell->mesh_aid_subframes)
                printf("3D GPU pacing: subframes=%u between=%u moving=%u immutable=1\n",
                    shell->mesh_aid_subframes, shell->mesh_aid_between, shell->mesh_aid_moving);
            fflush(stdout);
            shell->aid_completed = 1u;
            shell->quit = 1;
            return 0;
        }
        shell->mesh_aid_buttons = live_buttons;
    }
    else if (shell->mesh_aid_fractional && !shell->wait_for_neutral)
        live_buttons = shell->mesh_aid_buttons;
    else if (shell->aid != NULL && !shell->wait_for_neutral)
        live_buttons = menu_aid_button(&shell->flow, shell->aid, shell->aid_trial_completed);
    if (portable_frame_scheduler_update(&shell->scheduler, now, shell->focused, live_buttons,
                                        tick_buttons, PORTABLE_FRAME_MAX_CATCH_UP, &tick_count,
                                        &dropped) != PORTABLE_FRAME_SCHEDULER_OK)
    {
        shell->failure_detail = "frontend clock";
        shell->error = 1;
        return 0;
    }
    if (tick_count != 0u)
        shell->keyboard_pressed = 0u;
    for (size_t tick = 0u; tick < tick_count; ++tick)
    {
        const GameShellMenuTick menu_tick = game_shell_tick_settings(shell, tick_buttons[tick]);
        if (menu_tick == GAME_SHELL_TICK_FAILED)
            break;
        if (menu_tick == GAME_SHELL_TICK_OPTIONS)
        {
            if ((portable_course_3d_host_active(shell->preview_3d) || shell->mesh_audio_active) &&
                !game_shell_mesh_audio(shell))
            {
                shell->failure_detail = "3D shared audio tick";
                shell->error = 1;
                break;
            }
            continue;
        }
        if (shell->flow.page == PORTABLE_FRONTEND_PAGE_COURSE && shell->flow.course_phase == 2u &&
            shell->flow.mode_cursor == 1u && shell->flow.transition_ticks == 0u)
        {
            if (!load_menu_records(shell->pack, &shell->flow, &shell->records,
                                   shell->aid != NULL ? shell->aid->save_directory : NULL))
            {
                shell->failure_detail = "Time Trial ghost selection content";
                shell->error = 1;
                break;
            }
            const uint8_t mask = portable_time_trial_records_ghost_choices(&shell->records);
            if (mask != shell->flow.ghost_choices)
            {
                if (!portable_frontend_flow_set_ghost_choices(&shell->flow, mask))
                {
                    shell->failure_detail = "Time Trial ghost choices";
                    shell->error = 1;
                    break;
                }
                if (shell->record_file != NULL &&
                    portable_frontend_input_replay_write_ghost_choices_file(shell->record_file, mask) !=
                        PORTABLE_FRONTEND_INPUT_REPLAY_OK)
                {
                    shell->failure_detail = "Time Trial ghost choice recording";
                    shell->error = 1;
                    break;
                }
            }
        }
        if (shell->flow.page == PORTABLE_FRONTEND_PAGE_TITLE_MENU && !shell->flow.player_count_open)
        {
            int pads = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&pads);
            SDL_free(ids);
            shell->flow.max_players =
                (uint8_t)(shell->aid != NULL ? PORTABLE_LOCAL_PLAYERS_MAX
                                             : portable_local_players_max((unsigned)pads));
        }
        shell->flow.kart_choice = (uint8_t)game_shell_modern_active(shell);
        if (!portable_frontend_flow_tick(&shell->flow, tick_buttons[tick]))
        {
            shell->failure_detail = "frontend state tick";
            shell->error = 1;
            break;
        }
        game_shell_record_actions(shell);
        if (shell->flow.page == PORTABLE_FRONTEND_PAGE_TITLE_INTRO &&
            shell->flow.transition_ticks == 0u && shell->flow.page_ticks == 0u &&
            !portable_frontend_flow_settle_title(&shell->flow))
        {
            shell->failure_detail = "title menu settle";
            shell->error = 1;
            break;
        }
        if (!portable_frontend_audio_tick(&shell->frontend_audio, &shell->flow,
                                          &shell->audio_stream))
        {
            shell->failure_detail = "frontend audio timeline";
            shell->error = 1;
            break;
        }
        while (shell->next_audio_command < shell->audio_stream.count)
            if (portable_audio_session_apply(
                    &shell->audio->session,
                    &shell->audio_stream.commands[shell->next_audio_command++]) !=
                PORTABLE_AUDIO_SESSION_OK)
            {
                shell->failure_detail =
                    portable_audio_session_status_name(shell->audio->session.status);
                shell->error = 1;
                break;
            }
        if (shell->error || !route_audio_render(shell->audio, tick_buttons[tick], 0, NULL))
        {
            if (shell->failure_detail == NULL)
                shell->failure_detail = "audio render failed";
            shell->error = 1;
            break;
        }
        if (shell->record_file != NULL)
        {
            shell->record_status = portable_frontend_input_replay_write_tick_file(
                shell->record_file, shell->record_tick++, tick_buttons[tick], &shell->flow);
            if (shell->record_status != PORTABLE_FRONTEND_INPUT_REPLAY_OK)
            {
                shell->error = 1;
                break;
            }
        }
        /* Audio, save actions and the input recorder have consumed
         * this batch. Presentation keeps its own animation timestamps. */
        if (shell->flow.event_count != 0u)
        {
            if (shell->record_file != NULL)
            {
                shell->record_status =
                    portable_frontend_input_replay_write_consume_file(shell->record_file, &shell->flow);
                if (shell->record_status != PORTABLE_FRONTEND_INPUT_REPLAY_OK)
                {
                    shell->error = 1;
                    break;
                }
            }
            else
                portable_frontend_flow_consume_events(&shell->flow);
            shell->frontend_audio.next_event = 0u;
            shell->next_record_event = 0u;
        }
        shell->audio_stream.count = 0u;
        shell->next_audio_command = 0u;
    }
    if (tick_count != 0u)
        shell->needs_present = 1;
    portable_course_3d_host_set_presentation(shell->preview_3d,
        shell->aid != NULL && !shell->mesh_aid_fractional ? 65536u :
            portable_frame_scheduler_alpha(&shell->scheduler),
        (shell->settings.interpolate || shell->settings.smooth_3d) &&
            (shell->aid == NULL || shell->mesh_aid_fractional),
        dropped || !shell->focused);
    if ((shell->aid == NULL || shell->mesh_aid_fractional) && shell->focused &&
        (shell->settings.interpolate || shell->settings.smooth_3d) &&
        portable_course_3d_host_world_active(shell->preview_3d))
        shell->needs_present = portable_present_due(now, shell->settings.render_fps,
                                                   &shell->mesh_present_ns);
    else
        shell->mesh_present_ns = 0u;
    return !shell->error;
}

typedef enum
{
    GAME_SHELL_LAUNCH_FAILED = -1,
    GAME_SHELL_STAY_IN_MENU = 0,
    GAME_SHELL_LAUNCHED = 1
} GameShellLaunch;

static GameShellLaunch game_shell_begin_race(GameShell *shell)
{
    if (shell->flow.page == PORTABLE_FRONTEND_PAGE_RACE) portable_mobile_touch_clear();
    if (shell->flow.page == PORTABLE_FRONTEND_PAGE_RACE && !shell->settings.classic_menus)
    {
        const ModernMenuTransition loading = {.pack = shell->pack, .title = "Loading race",
            .course_id = shell->flow.selected_course, .duration = 1u, .kind = MODERN_TRANSITION_LOADING};
        if (!modern_menu_host_present_transition(&shell->modern, shell->renderer, NULL, 1,
                                                 &loading, "race-loading-modern"))
        {
            shell->failure_detail = "modern race loading card";
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
    }

    if (shell->flow.page == PORTABLE_FRONTEND_PAGE_RACE && shell->flow.players > 1u)
    {
        PortableMultiRaceScene multi = {shell->pack,
                                        shell->flow.selected_course,
                                        shell->flow.players,
                                        (uint8_t)(shell->flow.mode_cursor == 3u),
                                        shell->flow.race_speed,
                                        (uint8_t)(shell->aid != NULL),
                                        shell->settings.fit_scaling,
                                        {0},
                                        shell->flow.vs_options,
                                        shell->renderer,
                                        shell->audio,
                                        shell->settings.rumble,
                                        shell->settings.input_display,
                                        shell->settings.fix_doppler_wrap,
                                        shell->scripts,
                                        shell->settings.internal_scale,
                                        (uint8_t)!shell->settings.classic_menus,
                                        shell->settings.artwork_lod,
                                        shell->settings.race_renderer,
                                        shell->settings.aspect_width,
                                        shell->settings.render_fps,
                                        shell->settings.interpolate,
                                        {shell, game_shell_graphics_current, game_shell_graphics_edit}};
        for (unsigned player = 0u; player < shell->flow.players; ++player)
            multi.characters[player] = shell->flow.player_characters[player];
        if (!route_audio_begin_race(
                shell->audio, shell->flow.selected_course,
                voice_driver(shell->pack, shell->flow.player_characters[0], shell->aid != NULL),
                shell->flow.race_speed))
        {
            shell->failure_detail = "local race audio start";
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        PortableMultiRaceOutcome multi_outcome;
        if (run_multi_race_sdl(&multi, &multi_outcome) != 0)
        {
            shell->failure_detail = "local multiplayer race";
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        if (multi_outcome.quit_requested)
        {
            shell->quit = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        if (!portable_frontend_flow_cancel_race_to(&shell->flow, PORTABLE_FRONTEND_PAGE_COURSE) ||
            !route_audio_return_menu(shell->audio))
        {
            shell->failure_detail = "local multiplayer menu return";
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        if (shell->record_file != NULL &&
            portable_frontend_input_replay_write_cancel_file(shell->record_file, &shell->flow) !=
                PORTABLE_FRONTEND_INPUT_REPLAY_OK)
        {
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        if (shell->aid != NULL)
        {
            if (!multi_outcome.completed || shell->flow.page != PORTABLE_FRONTEND_PAGE_COURSE)
            {
                shell->failure_detail = "local multiplayer did not reach results";
                shell->error = 1;
                return GAME_SHELL_LAUNCH_FAILED;
            }
            printf("%s local menu: PASS players=%u course=%u\n", multi.battle ? "Battle" : "VS",
                   multi.players, multi.course_id);
            shell->aid_completed = 1u;
            shell->quit = 1;
        }
        shell->needs_present = 1;
        return GAME_SHELL_LAUNCHED;
    }
    if (shell->flow.page == PORTABLE_FRONTEND_PAGE_RACE)
    {
        if (shell->flow.mode_cursor != 1u)
        {
            /* GP needs all four tracks; Quick Run only the chosen one. */
            if (shell->flow.mode_cursor != 3u && !shell->gp_cup_progress_ready &&
                (shell->flow.mode_cursor == 2u
                     ? !(portable_frontend_cup_tracks(shell->pack, shell->flow.course_cup,
                                                      &shell->gp_cup_definition) &
                         (1u << (shell->flow.course_track & 3u)))
                     : !portable_frontend_gp_cup(shell->pack, shell->flow.course_cup,
                                                 &shell->gp_cup_definition)))
            {
                fprintf(stderr, "cup=%u track=%u mode=%s state=cup catalog lacks this %s\n",
                        shell->flow.course_cup, shell->flow.course_track,
                        shell->flow.mode_cursor == 2u ? "Quick Run" : "GP",
                        shell->flow.mode_cursor == 2u ? "track" : "four-track cup");
                shell->failure_detail = "GP cup catalog (reimport version 2)";
                shell->error = 1;
                return GAME_SHELL_LAUNCH_FAILED;
            }
#ifdef PORTABLE_ENABLE_TEST_HOOKS
            if (shell->aid != NULL && shell->aid->final_seed && !shell->gp_cup_progress_ready)
            {
                if (!seed_final_cup(shell->pack, &shell->gp_cup_definition,
                                    shell->flow.selected_character, shell->flow.race_speed,
                                    (uint8_t)(shell->aid->final_seed - 1u),
                                    &shell->gp_cup_progress))
                {
                    shell->failure_detail = "test final standings seed";
                    shell->error = 1;
                    return GAME_SHELL_LAUNCH_FAILED;
                }
                shell->gp_cup_progress_ready = 1u;
                shell->gp_retry_credits = shell->gp_cup_progress.retry_credits;
                shell->flow.selected_course = shell->gp_cup_progress.course_id;
                printf("GP menu seed: final-race resume cup=%u class=%u rank=%u\n", shell->aid->cup,
                       shell->aid->engine_class, shell->aid->final_seed - 1u);
            }
#endif
            if (!route_audio_begin_race(
                    shell->audio, shell->flow.selected_course,
                    voice_driver(shell->pack, shell->flow.selected_character, shell->aid != NULL),
                    shell->flow.race_speed))
            {
                shell->failure_detail = "GP race audio start";
                shell->error = 1;
                return GAME_SHELL_LAUNCH_FAILED;
            }
            if (!shell->gp_cup_progress_ready && !shell->gp_retry_pending)
            {
                PortableGpScoringRules scoring;
                if (!portable_recovered_gp_scoring(shell->pack, &scoring))
                {
                    shell->failure_detail = "GP scoring/retry rules";
                    shell->error = 1;
                    return GAME_SHELL_LAUNCH_FAILED;
                }
                shell->gp_retry_credits = scoring.initial_retry_credits;
            }
            shell->gp_retry_pending = 0u;
            if (shell->flow.mode_cursor == 2u && shell->quick_recording == NULL)
            {
                shell->quick_recording = calloc(1, sizeof *shell->quick_recording);
                if (shell->quick_recording == NULL)
                {
                    shell->failure_detail = "Quick Run input recording";
                    shell->error = 1;
                    return GAME_SHELL_LAUNCH_FAILED;
                }
            }
            const PortableGpHostScene configuration = {
                shell->pack,
                shell->flow.selected_course,
                shell->flow.selected_character,
                shell->window,
                shell->renderer,
                shell->texture,
                shell->gp_cup_progress_ready ? &shell->gp_cup_progress : NULL,
                shell->gp_cup_progress_ready ? shell->gp_cup_progress.course_in_cup : 0u,
                shell->gp_cup_progress_ready
                    ? shell->gp_cup_progress.course_header_index
                    : shell->gp_cup_definition
                          .source_headers[shell->flow.mode_cursor == 2u ? shell->flow.course_track
                                                                        : 0u],
                shell->audio,
                shell->flow.race_speed,
                shell->gp_cup_definition.cup_index,
                shell->gp_retry_credits,
                shell->flow.mode_cursor == 3u ? (uint8_t)PORTABLE_TRACK_SET_ARENA
                                              : shell->gp_cup_definition.track_set,
                (uint8_t)(shell->aid != NULL),
                shell->settings.interpolate,
                shell->settings.render_fps,
                (uint8_t)(shell->flow.mode_cursor == 2u),
                shell->flow.quick_run_options,
                shell->flow.mode_cursor == 2u ? shell->quick_recording : NULL,
                shell->quick_playback,
                shell->aid != NULL ? shell->aid->replay_aid : 0u,
                (uint8_t)(shell->aid != NULL && shell->aid->final_seed != 0u),
                shell->settings.internal_scale,
                shell->settings.fit_scaling,
                shell->settings.aspect_width,
                (uint8_t)(shell->flow.mode_cursor == 3u),
                shell->scripts,
                shell->settings.rumble,
                shell->settings.input_display,
                shell->settings.fix_doppler_wrap,
                shell->flow.short_transitions,
                shell->marker_clock,
                shell->marker_display_clock,
                (uint8_t)!shell->settings.classic_menus,
                shell->settings.artwork_lod,
                shell->settings.race_renderer,
                {shell, game_shell_graphics_current, game_shell_graphics_edit}};
            if (shell->aid != NULL)
            {
                printf("%s menu launch: cup=%u class=%u track=%u course=%u credits=%u\n",
                       shell->flow.mode_cursor == 3u ? "Battle"
                       : configuration.quick_run     ? "Quick Run"
                                                     : "GP",
                       configuration.cup_index, configuration.engine_class,
                       configuration.course_in_cup, configuration.course_id,
                       configuration.retry_credits);
                fflush(stdout);
            }
            shell->selected_gp = portable_gp_host_begin(&configuration);
            if (shell->selected_gp == NULL)
            {
                shell->failure_detail = "GP race begin";
                shell->error = 1;
                return GAME_SHELL_LAUNCH_FAILED;
            }
            return GAME_SHELL_LAUNCHED;
        }
        if (!route_audio_begin_race(
                shell->audio, shell->flow.selected_course,
                voice_driver(shell->pack, shell->flow.selected_character, shell->aid != NULL), 2u))
        {
            shell->failure_detail = "race audio start";
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        shell->selected_trial = prepare_selected_session(
            shell->pack, shell->flow.selected_course, shell->flow.selected_character, shell->window,
            shell->renderer, shell->texture, &shell->controller, shell->audio);
        if (shell->selected_trial == NULL)
        {
            shell->failure_detail = "race content";
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        shell->selected_trial->scene.aspect_width = shell->settings.aspect_width;
        shell->selected_trial->scene.marker_clock = shell->marker_clock;
        shell->selected_trial->scene.marker_display_clock = shell->marker_display_clock;
        shell->selected_trial->scene.internal_scale = shell->settings.internal_scale;
        shell->selected_trial->scene.fit_scaling = shell->settings.fit_scaling;
        shell->selected_trial->scene.artwork_lod = shell->settings.artwork_lod;
        shell->selected_trial->scene.race_renderer = shell->settings.race_renderer;
        shell->selected_trial->scene.modern_menus = !shell->settings.classic_menus;
        shell->selected_trial->scene.interpolate = shell->settings.interpolate;
        shell->selected_trial->scene.render_fps = shell->settings.render_fps;
        shell->selected_trial->scene.ghost_received_slot = shell->flow.ghost_choice;
        /* 58B0 GHOST DISPLAY: YOU covers the local ghost, TRADE received ones. */
        shell->selected_trial->scene.ghost_hidden =
            (uint8_t)((shell->flow.ghost_display_off &
                       (shell->flow.ghost_choice == 0u ? 1u : 2u)) != 0u);
        shell->selected_trial->scene.require_selected_ghost =
            (uint8_t)(shell->flow.ghost_choices != 0u &&
                      !shell->selected_trial->scene.ghost_hidden);
        shell->selected_trial->scene.input_aid = (uint8_t)(shell->aid != NULL);
        shell->selected_trial->scene.rumble = shell->settings.rumble;
        shell->selected_trial->scene.input_display = shell->settings.input_display;
        shell->selected_trial->scene.save_directory =
            shell->aid != NULL ? shell->aid->save_directory : NULL;
        shell->selected_trial->scene.scripts = shell->scripts;
        shell->selected_trial->scene.graphics = (PortableGraphicsHook){shell, game_shell_graphics_current, game_shell_graphics_edit};
        shell->selected_trial->host = portable_time_trial_host_begin(&shell->selected_trial->scene);
        if (shell->selected_trial->host == NULL)
        {
            release_selected_session(shell->selected_trial);
            shell->selected_trial = NULL;
            shell->failure_detail = "race scene begin";
            shell->error = 1;
            return GAME_SHELL_LAUNCH_FAILED;
        }
        return GAME_SHELL_LAUNCHED;
    }
    return GAME_SHELL_STAY_IN_MENU;
}

static void modern_options(const GameShell *shell, ModernMenuOptions *options)
{
    static const char *const labels[OPTION_ROWS] = {
        "Window Scale",  "Fullscreen",     "VSync",          "Filter",
        "Master Volume", "Stick Deadzone", "Fast Menus",     "Interpolation",
        "Render FPS",    "Internal Scale", "Display Scaling", "Aspect Ratio",
        "Rumble",        "Input Display",  "Mirror Mode",    "Fix Doppler Bug", "Touch Controls"};
    static const char *const help[OPTION_ROWS] = {
        "Window size as a multiple of the original 240x160 screen.",
        "Fill the whole display.",
        "Sync presentation to the display to avoid tearing.",
        "Nearest keeps pixels crisp; Linear smooths them in races.",
        "Overall game volume.",
        "How far the stick moves before steering starts. Press (A) to edit controls.",
        "Skip the original screen fades in the classic menus.",
        "Smooth motion between game ticks at high refresh rates.",
        "Frame rate cap while racing. Uncapped follows the display.",
        "Race rendering resolution as a multiple of the original.",
        "Integer keeps square pixels; Fit uses the whole window.",
        "Widescreen races show more of the track.",
        "Controller rumble on bumps, hits and boosts.",
        "Show the pressed buttons on screen.",
        "Race every course mirrored.",
        "Fixes the original engine-sound wrap glitch.",
        "On-screen buttons drive menus and Player 1. Hold several buttons to steer, accelerate and hop."};
    static const char *const actions[] = {"Accelerate", "Brake", "Left", "Right",
                                          "Hop", "Item", "Aim Up", "Aim Down"};
    static const uint8_t pad_actions[4] = {0u, 1u, 4u, 5u};
    *options = (ModernMenuOptions){0};
    options->notice = shell->options_notice;
    if (shell->options_open == 1)
    {
        options->page = MODERN_MENU_OPTIONS_MAIN;
        options->title = "Options";
        options->cursor = shell->options_cursor;
        for (unsigned row = 0u; row < OPTION_ROWS; ++row)
        {
            ModernMenuOptionRow *out = &options->rows[options->row_count++];
            out->label = labels[row];
            out->help = help[row];
            out->adjustable = 1u;
            char value[24];
            portable_option_value_text(&shell->settings, option_fields[row], value);
            portable_option_title_case(value);
            snprintf(out->value, sizeof out->value, "%s", value);
        }
    }
    else if (shell->options_open == 2)
    {
        options->page = MODERN_MENU_OPTIONS_BINDINGS;
        options->title = "Controls";
        options->cursor = shell->binding_cursor;
        options->capturing = (uint8_t)shell->binding_capture;
        options->hint = shell->binding_capture ? NULL : "Select resets";
        static char row_labels[BINDING_ROWS][32];
        for (unsigned row = 0u; row < BINDING_ROWS; ++row)
        {
            ModernMenuOptionRow *out = &options->rows[options->row_count++];
            const char *action = actions[row < BINDING_KEY_ROWS ? row % 8u : pad_actions[row - BINDING_KEY_ROWS]];
            snprintf(row_labels[row], sizeof row_labels[row], "%s %s",
                     row < 8u ? "Player 1" : row < BINDING_KEY_ROWS ? "Player 2" : "Controller", action);
            out->label = row_labels[row];
            out->help = row < BINDING_KEY_ROWS ? "Press (A), then the new key. Esc cancels."
                                               : "Press (A), then the new controller button. Start cancels.";
            char name[16];
            binding_name(&shell->settings, row, name);
            portable_option_title_case(name);
            snprintf(out->value, sizeof out->value, "%s", name);
        }
    }
    else
    {
        options->page = MODERN_MENU_OPTIONS_ENHANCEMENTS;
        options->title = "Enhancements";
        options->cursor = shell->enhancements_cursor;
        options->hint = "Select: mods";
        ModernMenuOptionRow *row = &options->rows[options->row_count++];
        row->label = "Wide UI Backgrounds";
        row->help = "Widescreen art behind the classic menus. Press (A) to import title art.";
        row->adjustable = 1u;
        snprintf(row->value, sizeof row->value, "%s", shell->settings.wide_ui_backgrounds ? "On" : "Off");
        row = &options->rows[options->row_count++];
        row->label = "Drift Extras";
        row->help =
            shell->settings.driving_profile == 2u
                ? "2D handling stays original; modern physics is on 3D courses. Three drift tiers: charge, then release. Hop in air; land to boost. Separate records."
            : shell->settings.driving_profile
                ? "2D handling stays original; modern physics is on 3D courses. Hold Hop + steer + accelerate; three tiers, release to boost. Separate records."
                : "2D handling stays original; modern physics is on 3D courses. Original drift reward. Extras use separate records.";
        row->adjustable = 1u;
        snprintf(row->value, sizeof row->value, "%s",
                 shell->settings.driving_profile == 2u ? "Drift Tiers + Air Boost"
                 : shell->settings.driving_profile     ? "Drift Tiers"
                                                       : "Off");
        row = &options->rows[options->row_count++];
        row->label = "Menu Style";
        row->help = "Classic shows the original 240x160 menus.";
        row->adjustable = 1u;
        snprintf(row->value, sizeof row->value, "%s", shell->settings.classic_menus ? "Classic" : "Modern");
        row = &options->rows[options->row_count++];
        row->label = "Artwork Detail";
        row->help = "High favors more detailed artwork. Max uses the highest available detail. Sprites without alternatives keep their original art.";
        row->adjustable = 1u;
        snprintf(row->value,sizeof row->value,"%s",shell->settings.artwork_lod==2u?"Max":
            shell->settings.artwork_lod?"High":"Original");
        row = &options->rows[options->row_count++];
        row->label = "Race Renderer";
        row->help = "Optional 3D graphics. Unsupported content uses the Original renderer.";
        row->adjustable = 1u;
        snprintf(row->value,sizeof row->value,"%s",shell->settings.race_renderer?"3D":"Original");
        static const char *const effect_labels[EFFECT_ROWS] = {
            "Smooth 3D Motion", "Dynamic Camera", "Camera Shake",
            "Drift Sparks",     "Boost Effects",  "Skid Marks & Dust"};
        static const char *const effect_help[EFFECT_ROWS] = {
            "Draw 3D races between game ticks at the display rate, even with Interpolation off.",
            "The 3D camera widens with speed and boosts and swings into drifts.",
            "The 3D camera jolts on landings, boosts, hits and crashes.",
            "Glowing drift sparks in each charge color and mini-turbo bursts.",
            "Exhaust flames, a boost glow and wind streaks.",
            "Tyre marks, drift smoke, dust on rough ground and landing puffs."};
        static const char *const presets[3] = {"Low Power", "Full", "Custom"};
        row = &options->rows[options->row_count++];
        row->label = "3D Effects";
        row->help = "Full turns these six motion and particle effects on; Low Power turns them off. HD-2D Graphics has its own preset.";
        row->adjustable = 1u;
        snprintf(row->value, sizeof row->value, "%s", presets[effects_preset(&shell->settings)]);
        for (unsigned i = 0u; i < EFFECT_ROWS; ++i)
        {
            row = &options->rows[options->row_count++];
            row->label = effect_labels[i];
            row->help = effect_help[i];
            row->adjustable = 1u;
            snprintf(row->value, sizeof row->value, "%s",
                     portable_setting_get(&shell->settings, effect_fields[i]) ? "On" : "Off");
        }
        row = &options->rows[options->row_count++];
        row->label = "Immersive Camera";
        row->help = "Closer, lower chase on 3D courses. Dynamic Camera adds speed, pitch and lean; Camera Shake controls gentle jolts. Off restores MK8 framing.";
        row->adjustable = 1u;
        snprintf(row->value, sizeof row->value, "%s", shell->settings.immersive_camera ? "On" : "Off");
        row = &options->rows[options->row_count++];
        row->label = "HD-2D Graphics";
        row->help = "Extra Juicy enables all seven effects. For high-performance GPU devices; Original, CPU and GLES rendering stay unchanged.";
        row->adjustable = 1u;
        static const char *const hd_presets[] = {"Off", "Extra Juicy", "Custom"};
        snprintf(row->value, sizeof row->value, "%s", hd_presets[portable_settings_hd2d_preset(&shell->settings)]);
        static const char *const hd_labels[] = {"Tilt-Shift", "Bloom", "Ambient Occlusion", "Color Grading", "Atmospheric Haze", "Sprite Lighting", "3D Resolution"};
        static const char *const hd_help[] = {
            "Softens the distant scene and near screen edge; keeps the racing line and HUD sharp. GPU only.",
            "A soft glow around bright scenery and effects. GPU only.",
            "Depth-based contact shading around nearby geometry. GPU only.",
            "Warm highlights, cool shadows and richer colors. GPU only.",
            "Depth-based aerial haze gives distant scenery a softer silhouette. GPU only.",
            "Warm overhead light and a shaded lower edge ground billboards in the scene. GPU only.",
            "High renders 3D worlds at a minimum of 6x internal scale. Uses more GPU memory and fill rate."};
        for (unsigned i = 0; i < HD2D_ROWS; ++i)
        {
            row = &options->rows[options->row_count++];
            row->label = hd_labels[i]; row->help = hd_help[i]; row->adjustable = 1u;
            const int enabled = portable_setting_get(&shell->settings, (PortableSetting)(PORTABLE_SETTING_HD2D_DOF + i)) != 0;
            snprintf(row->value, sizeof row->value, "%s", i == 6u ? (enabled ? "High (6x+)" : "Game setting") : (enabled ? "On" : "Off"));
        }
    }
}

/* Classic character page: a 3D-only driver's model in its default kart (the
 * chosen one once locked), turning where the stock preview kart turns. */
static int classic_driver_preview(void *owner, const PortableFrontendFlow *flow, uint32_t driver_id,
                                  int center_x, int center_y, unsigned size, PortableSoftwareFrame *frame)
{
    GameShell *shell = owner;
    if (shell->pack_3d == NULL || shell->pack_3d == shell->pack)
        return 0;
    const PortableMenuDriverModelPose pose = {
        .driver_id = driver_id,
        .kart_id = flow->character_locked ? flow->selected_kart : 0u,
        .size = size,
        .yaw = 0.6 + (double)flow->tick * 0.04,
        .tick = flow->character_locked ? flow->tick - flow->character_lock_tick : flow->tick,
        .celebrate = flow->character_locked,
    };
    return portable_menu_driver_model_draw_frame(modern_menu_host_driver_model(&shell->modern), shell->pack_3d,
                                                 &pose, frame, center_x, center_y);
}

static int classic_driver_portrait(void *owner, uint32_t driver_id, int left, int top, int width, int height,
                                   PortableSoftwareFrame *frame)
{
    const GameShell *shell = owner;
    if (shell->pack_3d == NULL || shell->pack_3d == shell->pack)
        return 0;
    return portable_menu_driver_portrait_draw_frame(shell->pack_3d, driver_id, frame, left, top, width, height,
                                                    0xfff4f6fau);
}

static int game_shell_render_modern(GameShell *shell)
{
    ModernMenuOptions options;
    ModernMenuGames games={.manager=shell->mod_manager,.game=shell->library_game,.row=shell->library_row,
        .notice=shell->mod_notice,.notice_error=shell->mod_notice_error};
    if(shell->options_open==7){
        /* The manager caches one preview, so each ready image is copied into the shell once and every
         * imported card keeps drawing its own copy while another game has the focus. */
        const size_t count=portable_game_library_count()<MODERN_MENU_GAMES_MAX?portable_game_library_count():MODERN_MENU_GAMES_MAX;
        int busy=0;
        for(size_t n=0;n<count;++n){
            const size_t i=n==0?shell->library_game:(n-1u<shell->library_game?n-1u:n);
            const PortableModCatalogEntry *entry=portable_game_library_entry(shell->mod_manager,i);
            if(!entry||!entry->valid){free(shell->library_art[i]);shell->library_art[i]=NULL;continue;}
            if(!shell->library_art[i]&&!busy){
                const PortableNativeImage *art=portable_mod_manager_preview_image(shell->mod_manager,entry->id,shell->base_pack_3d);
                if(art&&!art->palette&&art->pixels&&art->width&&art->height){
                    const size_t bytes=(size_t)art->width*art->height*sizeof *shell->library_art[i];
                    shell->library_art[i]=malloc(bytes);
                    if(shell->library_art[i]){
                        memcpy(shell->library_art[i],art->pixels,bytes);
                        shell->library_art_size[i][0]=art->width;shell->library_art_size[i][1]=art->height;
                    }
                }else busy=1;
            }
            if(shell->library_art[i])games.art[i]=(UiImage){shell->library_art[i],shell->library_art_size[i][0],shell->library_art_size[i][1]};
            else games.art_loading[i]=1;
        }
    }else for(size_t i=0;i<MODERN_MENU_GAMES_MAX;++i){free(shell->library_art[i]);shell->library_art[i]=NULL;}
    const ModernMenuMods mods = {
        .store = shell->store, .store_open = shell->store_open && shell->options_open==4,
        .store_cursor = shell->store_cursor, .store_details = shell->store_details,
        .store_action = shell->store_action, .store_busy = shell->store_job != NULL,
        .manager = shell->mod_manager,
        .cursor = shell->mod_unsaved_open ? shell->mod_unsaved_cursor : shell->mod_cursor,
        .show_order = shell->mod_order || shell->options_open == 5,
        .reviewing = shell->mod_unsaved_open ? 3u : shell->options_open == 5 ? 1u : 0u,
        .advanced = shell->mod_advanced,
        .detail_page = shell->mod_detail_page,
        .action_open = shell->mod_action_open,
        .action_cursor = shell->mod_action_cursor,
        .notice_error = shell->mod_notice_error && shell->mod_notice[0] != '\0',
        .notice = shell->mod_notice
    };
    if (shell->options_open >= 1 && shell->options_open <= 3)
        modern_options(shell, &options);
    else if(shell->options_open==7)
        options=(ModernMenuOptions){.page=MODERN_MENU_OPTIONS_GAMES,.title="Imported games",.games=&games};
    else if (shell->options_open >= 4 && shell->options_open <= 5)
        options = (ModernMenuOptions){.page=MODERN_MENU_OPTIONS_MODS,.title="Mods",.mods=&mods};
    const ModernMenuView view = {
        .pack_3d = shell->pack_3d,
        .pack = shell->pack,
        .flow = &shell->flow,
        .progress = &shell->menu_progress,
        .records = &shell->records,
        .options = shell->options_open >= 1 && shell->options_open <= 7 ? &options : NULL,
        .gamepad = shell->modern.gamepad,
        .driving_profile = shell->settings.driving_profile,
        .mesh_preview = game_shell_mesh_preview,
        .mesh_preview_owner = shell->preview_3d,
    };
    if (!modern_menu_host_present(&shell->modern, shell->renderer, &view))
    {
        fprintf(stderr, "page=%u mode=%u state=modern menu render failed\n", shell->flow.page,
                shell->flow.mode_cursor);
        shell->failure_detail = "modern menu presentation";
        shell->error = 1;
        return 0;
    }
    shell->needs_present = 0;
    return 1;
}

static int mesh_benchmark_begin(GameShell *shell, uint64_t *start, uint64_t *checksum)
{
    *start = 0;
    *checksum = 0;
    if (!shell->mesh_benchmark || shell->mesh_benchmark_samples == 300u)
        return 1;
    size_t width, height;
    unsigned views;
    if (!portable_course_3d_host_benchmark_info(shell->preview_3d, &width, &height, &views))
    {
        if (shell->mesh_benchmark_samples)
        {
            fprintf(stderr, "3D benchmark interrupted batch=%u sample=%u\n",
                    shell->mesh_benchmark_batches, shell->mesh_benchmark_samples);
            return 0;
        }
        shell->mesh_benchmark_warmup = 0;
        return 1;
    }
    if (shell->mesh_benchmark_warmup < 64u)
    {
        ++shell->mesh_benchmark_warmup;
        return 1;
    }
    char error[192];
    if (!shell->mesh_benchmark_ready)
    {
        if (!portable_course_3d_host_wait_idle(shell->preview_3d, error, sizeof error))
        {
            fprintf(stderr, "3D benchmark warmup: %s\n", error);
            return 0;
        }
        shell->mesh_benchmark_width = width;
        shell->mesh_benchmark_height = height;
        shell->mesh_benchmark_views = views;
        shell->mesh_benchmark_ready = 1;
    }
    if (width != shell->mesh_benchmark_width || height != shell->mesh_benchmark_height ||
        views != shell->mesh_benchmark_views)
        return 0;
    const PortableMeshTimeTrialSession *session =
        portable_course_3d_host_session(shell->preview_3d);
    *checksum = portable_mesh_time_trial_checksum(&session->state);
    *start = SDL_GetTicksNS();
    if (!(shell->mesh_benchmark_samples % 16u))
        shell->mesh_benchmark_batch_start = *start;
    return 1;
}
static int mesh_benchmark_end(GameShell *shell, uint64_t start, uint64_t checksum)
{
    if (!start)
        return 1;
    const uint64_t submitted = SDL_GetTicksNS();
    const PortableMeshTimeTrialSession *session =
        portable_course_3d_host_session(shell->preview_3d);
    if (!session || checksum != portable_mesh_time_trial_checksum(&session->state) ||
        submitted < start)
        return 0;
    shell->mesh_benchmark_cpu_ns += submitted - start;
    ++shell->mesh_benchmark_samples;
    if (shell->mesh_benchmark_samples % 16u && shell->mesh_benchmark_samples != 300u)
        return 1;
    char error[192];
    if (!portable_course_3d_host_wait_idle(shell->preview_3d, error, sizeof error))
    {
        fprintf(stderr, "3D benchmark batch=%u sample=%u: %s\n", shell->mesh_benchmark_batches,
                shell->mesh_benchmark_samples, error);
        return 0;
    }
    const uint64_t completed = SDL_GetTicksNS();
    if (completed < shell->mesh_benchmark_batch_start ||
        checksum != portable_mesh_time_trial_checksum(&session->state))
        return 0;
    const uint64_t elapsed = completed - shell->mesh_benchmark_batch_start;
    shell->mesh_benchmark_completed_ns += elapsed;
    if (elapsed > shell->mesh_benchmark_worst_ns)
        shell->mesh_benchmark_worst_ns = elapsed;
    const uint64_t batch_frames =
        shell->mesh_benchmark_samples % 16u ? shell->mesh_benchmark_samples % 16u : 16u;
    const uint64_t frame_ns = elapsed / batch_frames + (elapsed % batch_frames != 0u);
    if (frame_ns > shell->mesh_benchmark_worst_frame_ns)
        shell->mesh_benchmark_worst_frame_ns = frame_ns;
    ++shell->mesh_benchmark_batches;
    if (shell->mesh_benchmark_samples == 300u)
    {
        size_t width, height;
        unsigned views;
        if (!portable_course_3d_host_benchmark_info(shell->preview_3d, &width, &height, &views))
            return 0;
        printf(
            "3D offscreen completed throughput: width=%zu height=%zu views=%u samples=300 batches=%u cpu_submit_ns=%" PRIu64
            " completed_ns=%" PRIu64 " worst_batch_ns=%" PRIu64 " worst_batch_frame_ns=%" PRIu64
            " immutable=1\n",
            width, height, views, shell->mesh_benchmark_batches, shell->mesh_benchmark_cpu_ns,
            shell->mesh_benchmark_completed_ns, shell->mesh_benchmark_worst_ns,
            shell->mesh_benchmark_worst_frame_ns);
    }
    return 1;
}

static int game_shell_render_menu(GameShell *shell)
{
    if (shell->needs_present)
    {
        if (shell->editor && !shell->editor_previous_preview) {
            const int okay=modern_menu_host_present_canvas(&shell->modern,shell->renderer,
                course_editor_host_render,shell->editor);
            shell->needs_present=0;
            if(!okay){shell->failure_detail="course editor presentation";shell->error=1;}
            return okay;
        }
        if (portable_course_3d_host_active(shell->preview_3d))
        {
            portable_course_3d_host_set_gamepad(shell->preview_3d, shell->modern.gamepad);
            const PortableMeshTimeTrialSession *mesh =
                portable_course_3d_host_session(shell->preview_3d);
            const uint64_t checksum = shell->mesh_aid_fractional && mesh != NULL ?
                portable_mesh_time_trial_checksum(&mesh->state) : 0u;
            uint64_t benchmark_start, benchmark_checksum;
            if (!mesh_benchmark_begin(shell, &benchmark_start, &benchmark_checksum))
            {
                shell->failure_detail = "3D benchmark interrupted or warmup failed";
                shell->error = 1;
                return 0;
            }
            if (!modern_menu_host_present_layer(&shell->modern, shell->renderer,
                                                portable_course_3d_host_render_layer,
                                                shell->preview_3d))
            {
                shell->failure_detail = "3D course preview presentation";
                shell->error = 1;
                return 0;
            }
            if (!mesh_benchmark_end(shell, benchmark_start, benchmark_checksum))
            {
                shell->failure_detail = "3D benchmark immutable/batch completion failed";
                shell->error = 1;
                return 0;
            }
            /* Aid-only: every 240th shared-race tick to MKSC_AID_MESH_RACE_SHOTS, and
             * every 20th while racer 0 owns a live item or one lies within 20 m
             * of it (at most 60, from tick MKSC_AID_MESH_RACE_SHOTS_FROM). */
            const PortableMeshRaceSession *shot_race = portable_course_3d_host_race(shell->preview_3d);
            const char *shots = shell->aid != NULL ? getenv("MKSC_AID_MESH_RACE_SHOTS") : NULL;
            int shot_item = 0;
            for (unsigned i = 0; shot_race && i < PORTABLE_MESH_ITEMS_ACTORS; ++i)
            {
                const PortableMeshItemActor *actor = &shot_race->items.actors[i];
                const int32_t *at = shot_race->state.racers[0].kart.position_q16;
                int64_t square = 0;
                for (unsigned axis = 0; axis < 3u; ++axis)
                {
                    const int64_t delta = ((int64_t)actor->motion.position_q16[axis] - at[axis]) >> 16;
                    square += delta * delta;
                }
                shot_item |= actor->phase && (actor->owner == 0u || square < 400);
            }
            const char *shots_from = shell->aid != NULL ? getenv("MKSC_AID_MESH_RACE_SHOTS_FROM") : NULL;
            /* Aid-only shot spacing in ticks (default 240). */
            const char *every = shell->aid != NULL ? getenv("MKSC_AID_MESH_RACE_SHOTS_EVERY") : NULL;
            const unsigned long shots_every = every && strtoul(every, NULL, 10) ? strtoul(every, NULL, 10) : 240ul;
            /* Time Trials shoot from their own session. */
            const uint32_t shot_tick = shot_race ? shot_race->state.tick : mesh ? mesh->state.tick : 0u;
            const uint32_t shot_course = shot_race ? shot_race->course->id : mesh ? mesh->course->id : 0u;
            if (shots && shots[0] && shot_tick &&
                shot_tick >= (shots_from ? strtoul(shots_from, NULL, 10) : 0ul) &&
                shot_tick % (shot_item ? 20u : shots_every) == 0u &&
                shot_tick != shell->mesh_aid_shot_tick && shell->mesh_aid_shots < 60u)
            {
                char path[4096];
                const int length = snprintf(path, sizeof path, "%s/race-%u-%05u.ppm", shots,
                    shot_course, shot_tick);
                if (length > 0 && (size_t)length < sizeof path && modern_menu_host_write_ppm(&shell->modern, path))
                    ++shell->mesh_aid_shots;
                shell->mesh_aid_shot_tick = shot_tick;
            }
            if (shell->mesh_aid_fractional && mesh != NULL)
            {
                if (checksum != portable_mesh_time_trial_checksum(&mesh->state) ||
                    (SDL_GetGPURendererDevice(shell->renderer) == NULL &&
                     strcmp(SDL_GetRendererName(shell->renderer), "opengles2") != 0))
                {
                    shell->failure_detail = "fractional GPU presentation changed simulation";
                    shell->error = 1;
                    return 0;
                }
                const uint32_t alpha = portable_frame_scheduler_alpha(&shell->scheduler);
                if (shell->mesh_aid_presented && shell->mesh_aid_last_tick == mesh->state.tick &&
                    shell->mesh_aid_last_alpha != alpha)
                {
                    ++shell->mesh_aid_between;
                    if (mesh->state.kart.speed_q16 != 0) ++shell->mesh_aid_moving;
                }
                /* Aid-only readback of a few same-tick subframes; never on by default. */
                const char *screens = getenv("MKSC_AID_MESH_SUBFRAME_DIR");
                if (screens && screens[0] && shell->mesh_aid_captures < 12u &&
                    mesh->state.kart.speed_q16 != 0 && !mesh->state.kart.respawn_ticks)
                {
                    char path[4096];
                    const int length = snprintf(path, sizeof path, "%s/mesh-subframe-%u-%u.ppm",
                        screens, mesh->state.tick, alpha);
                    if (length <= 0 || (size_t)length >= sizeof path ||
                        !modern_menu_host_write_ppm(&shell->modern, path))
                    {
                        shell->failure_detail = "fractional mesh frame capture";
                        shell->error = 1;
                        return 0;
                    }
                    ++shell->mesh_aid_captures;
                    printf("3D subframe capture: tick=%u alpha=%u checksum=%016llx\n",
                        mesh->state.tick, alpha, (unsigned long long)checksum);
                }
                shell->mesh_aid_last_tick = mesh->state.tick;
                shell->mesh_aid_last_alpha = alpha;
                shell->mesh_aid_presented = 1u;
            }
            /* Aid-only stills at listed race ticks (countdown included):
             * MKSC_AID_MESH_SHOTS=TICK,... into MKSC_AID_MESH_SUBFRAME_DIR. */
            const char *listed = shell->aid != NULL && mesh != NULL ? getenv("MKSC_AID_MESH_SHOTS") : NULL;
            const char *shot_dir = getenv("MKSC_AID_MESH_SUBFRAME_DIR");
            if (listed && shot_dir && shot_dir[0])
            {
                unsigned index = 0;
                for (const char *at = listed; *at && index < 32u; ++index)
                {
                    char *end;
                    const unsigned long tick = strtoul(at, &end, 10);
                    if (end == at) break;
                    if (!(shell->mesh_aid_listed_shots & (1u << index)) && mesh->state.tick >= tick)
                    {
                        char path[4096];
                        const int length = snprintf(path, sizeof path, "%s/mesh-shot-%lu.ppm", shot_dir, tick);
                        shell->mesh_aid_listed_shots |= 1u << index;
                        if (length > 0 && (size_t)length < sizeof path &&
                            modern_menu_host_write_ppm(&shell->modern, path))
                            printf("3D shot: tick=%u file=%s\n", mesh->state.tick, path);
                    }
                    at = *end == ',' ? end + 1 : end;
                }
            }
            shell->needs_present = 0;
            return 1;
        }
        if (game_shell_modern_active(shell))
        {
            if (shell->flow.page == PORTABLE_FRONTEND_PAGE_RECORDS && shell->flow.course_phase >= 2u &&
                shell->flow.transition_ticks == 0u && shell->flow.selected_course != 0u)
            {
                if (!load_menu_records(shell->pack, &shell->flow, &shell->records,
                                       shell->aid != NULL ? shell->aid->save_directory : NULL))
                {
                    fprintf(stderr, "course=%u mode=Time Trial records state=load failed\n",
                            shell->flow.selected_course);
                    shell->failure_detail = "Time Trial records content";
                    shell->error = 1;
                    return 0;
                }
            }
            else if (shell->flow.page != PORTABLE_FRONTEND_PAGE_RECORDS &&
                     !(shell->flow.page == PORTABLE_FRONTEND_PAGE_COURSE &&
                       shell->flow.course_phase == 2u && shell->flow.mode_cursor == 1u))
                shell->records = (PortableTimeTrialRecords){0};
            const uint64_t render_start = portable_startup_clock();
            const int rendered = game_shell_render_modern(shell);
            /* Games-screen frames over a few ms show up as stalls; the route
             * test asserts none is long (MKSC_STARTUP_TIMING). */
            if (shell->options_open == 7) portable_startup_phase("games screen frame", render_start);
            return rendered;
        }
        const size_t menu_width = shell->settings.wide_ui_backgrounds
            ? portable_software_frame_window_width(shell->renderer, shell->settings.aspect_width)
            : 240u;
        if (!portable_software_render_target_resize(shell->renderer, &shell->menu_target, menu_width,
                                                    160u, shell->settings.internal_scale,
                                                    shell->settings.linear_filter))
        {
            shell->failure_detail = "menu sampling surface";
            shell->error = 1;
            return 0;
        }
        shell->frame = shell->menu_target.frame;
        shell->scratch->extended_backgrounds = shell->settings.wide_ui_backgrounds;
        shell->scratch->title_background_pack = shell->settings.wide_ui_backgrounds && menu_width > 240u
            ? portable_ui_background_host_load(&shell->ui_background) : NULL;
        PortableSoftwareFrame centered = portable_software_frame_native_view(&shell->frame, 1u);
        shell->scratch->character_preview = (PortableCharacterPreviewHook){classic_driver_preview, classic_driver_portrait, shell};
        const PortableFrontendPresentationStatus presentation_status =
            portable_frontend_presentation_render(shell->pack, &shell->flow, &shell->frame,
                                                  shell->scratch);
        if (presentation_status != PORTABLE_FRONTEND_PRESENTATION_OK)
        {
            shell->failure_detail = portable_frontend_presentation_status_name(presentation_status);
            shell->error = 1;
            return 0;
        }
        if (shell->flow.page == PORTABLE_FRONTEND_PAGE_COURSE && shell->flow.mode_cursor == 0u &&
            shell->flow.course_phase == 0u &&
            !portable_gp_progress_render(shell->pack, &shell->flow, &shell->menu_progress,
                                         &centered))
        {
            shell->failure_detail = "GP cup records render";
            shell->error = 1;
            return 0;
        }
        if (shell->flow.page == PORTABLE_FRONTEND_PAGE_RECORDS && shell->flow.course_phase >= 2u &&
            shell->flow.transition_ticks == 0u && shell->flow.selected_course != 0u)
        {
            if (!load_menu_records(shell->pack, &shell->flow, &shell->records,
                                   shell->aid != NULL ? shell->aid->save_directory : NULL))
            {
                fprintf(stderr, "course=%u mode=Time Trial records state=load failed\n",
                        shell->flow.selected_course);
                shell->failure_detail = "Time Trial records content";
                shell->error = 1;
                return 0;
            }
            if (portable_time_trial_records_render(shell->pack, &shell->flow, &shell->records,
                                                   &centered) !=
                PORTABLE_FRONTEND_PRESENTATION_OK)
            {
                fprintf(stderr, "course=%u mode=Time Trial records state=render failed\n",
                        shell->flow.selected_course);
                shell->failure_detail = "Time Trial records render";
                shell->error = 1;
                return 0;
            }
        }
        else if (shell->flow.page == PORTABLE_FRONTEND_PAGE_COURSE &&
                 shell->flow.course_phase == 2u && shell->flow.mode_cursor == 1u)
        {
            if (shell->flow.transition_ticks == 0u && shell->flow.ghost_choices != 0u &&
                portable_time_trial_ghost_choice_render(shell->pack, &shell->flow, &shell->records,
                                                        &centered) !=
                    PORTABLE_FRONTEND_PRESENTATION_OK)
            {
                shell->failure_detail = "Time Trial ghost choice render";
                shell->error = 1;
                return 0;
            }
        }
        else if (shell->flow.page != PORTABLE_FRONTEND_PAGE_RECORDS)
        {
            shell->records = (PortableTimeTrialRecords){0};
        }
        if (shell->options_open == 1)
            render_settings(shell->pack,&centered,&shell->settings,shell->options_cursor,shell->options_notice);
        if (shell->options_open == 3)
            render_enhancements(shell->pack,&centered,&shell->settings,shell->enhancements_cursor,shell->options_notice);
        if ((shell->options_open >= 4 && shell->options_open <= 5) &&
            portable_mod_menu_render(shell->pack,&shell->flow,shell->mod_manager,
                shell->mod_unsaved_open?shell->mod_unsaved_cursor:shell->mod_cursor,
                shell->options_open==5 || shell->mod_order,shell->mod_advanced,shell->mod_notice,shell->mod_detail_page,
                shell->mod_unsaved_open?3:shell->options_open==5,&shell->frame) != PORTABLE_FRONTEND_PRESENTATION_OK)
        {
            shell->failure_detail="mod manager presentation";
            shell->error=1;
            return 0;
        }
        if (shell->options_open == 2)
            render_bindings(shell->pack, &centered, &shell->settings, shell->binding_cursor,
                            shell->binding_capture, shell->options_notice);
        const char *menu_frame = getenv("MKSC_MENU_FRAME");
        if (menu_frame != NULL && (shell->menu_frame_written & 1u) == 0u &&
            shell->flow.page == PORTABLE_FRONTEND_PAGE_MODE && shell->flow.transition_ticks == 0u)
        {
            if (!portable_software_frame_write_ppm(&shell->frame, menu_frame))
            {
                shell->failure_detail = "menu frame capture";
                shell->error = 1;
                return 0;
            }
            shell->menu_frame_written |= 1u;
        }
        const char *character_frame = getenv("MKSC_CHARACTER_FRAME");
        if (character_frame != NULL && (shell->menu_frame_written & 2u) == 0u &&
            shell->flow.page == PORTABLE_FRONTEND_PAGE_CHARACTER && shell->flow.character_locked &&
            shell->flow.transition_ticks == 0u)
        {
            if (!portable_software_frame_write_ppm(&shell->frame, character_frame))
            {
                shell->failure_detail = "character frame capture";
                shell->error = 1;
                return 0;
            }
            shell->menu_frame_written |= 2u;
        }
        if (!menu_tour_motion_capture(&shell->tour, &shell->flow, &shell->frame))
        {
            shell->failure_detail = "frontend motion capture";
            shell->error = 1;
            return 0;
        }
        if (!portable_software_frame_present(shell->renderer, shell->menu_target.texture,
                                             &shell->frame, shell->settings.fit_scaling))
        {
            shell->failure_detail = "SDL frame present";
            shell->error = 1;
            return 0;
        }
        shell->needs_present = 0;
    }
    return 1;
}

/* Child scenes release borrowed references before the owning shell closes
 * replay/audio resources and finally destroys its SDL surface. */
static int game_shell_close(GameShell *shell)
{
    if (shell->selected_ceremony != NULL &&
        portable_gp_ceremony_host_end(shell->selected_ceremony, NULL))
        shell->error = 1;
    if (shell->selected_gp != NULL && portable_gp_host_end(shell->selected_gp, NULL))
        shell->error = 1;
    if (shell->selected_trial != NULL)
    {
        if (portable_time_trial_host_end(shell->selected_trial->host, NULL))
            shell->error = 1;
        release_selected_session(shell->selected_trial);
    }
    if (shell->record_file != NULL && fflush(shell->record_file) != 0)
    {
        shell->record_status = PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR;
        shell->error = 1;
    }
    if (shell->record_file != NULL && fclose(shell->record_file) != 0)
    {
        shell->record_status = PORTABLE_FRONTEND_INPUT_REPLAY_IO_ERROR;
        shell->error = 1;
    }
    if (shell->error)
    {
        if (shell->record_status != PORTABLE_FRONTEND_INPUT_REPLAY_OK)
            fprintf(stderr, "faithful Time Trial host failed: frontend replay %s\n",
                    portable_frontend_input_replay_status_name(shell->record_status));
        else if (shell->failure_detail != NULL)
            fprintf(stderr, "faithful Time Trial host failed: %s\n", shell->failure_detail);
        else
            fprintf(stderr, "faithful Time Trial host failed: %s\n", SDL_GetError());
    }
    if (shell->controller != NULL)
        SDL_CloseGamepad(shell->controller);
    if (shell->audio != NULL)
    {
        if (!route_audio_capture_close(shell->audio))
            shell->error = 1;
        portable_sdl_audio_queue_close(&shell->audio->queue);
        free(shell->audio);
    }
    portable_software_render_target_close(&shell->menu_target);
    if(shell->editor_previous_preview) {
        shell->preview_3d=shell->editor_previous_preview;shell->editor_previous_preview=NULL;
    }
    course_editor_host_close(shell->editor);shell->editor=NULL;
    portable_course_3d_host_close(shell->preview_3d);
    modern_menu_host_close(&shell->modern);
    portable_mobile_lifecycle_stop();
    portable_mobile_touch_close();
    SDL_DestroyTexture(shell->texture);
    SDL_DestroyTexture(shell->gpu_aid_target);
    SDL_DestroyRenderer(shell->renderer);
    SDL_DestroyWindow(shell->window);
    portable_store_job_free(shell->store_job);
    free(shell->store);free(shell->store_loading);
    if(shell->store_download_path[0])remove(shell->store_download_path);
    SDL_Quit();
    free(shell->scratch);
    portable_ui_background_host_close(&shell->ui_background);
    game_shell_discard_mod_candidate(shell);
    game_shell_discard_install(shell);
    portable_game_import_free(shell->game_import);
    for(size_t i=0;i<MODERN_MENU_GAMES_MAX;++i)free(shell->library_art[i]);
    portable_mod_manager_free(shell->mod_manager);
    portable_mod_manager_drain();
    free(shell->quick_recording);
    return shell->error || (shell->aid != NULL && !shell->aid_completed);
}

static void game_shell_mobile_lifecycle(GameShell *shell)
{
    if (shell->selected_gp != NULL || shell->selected_trial != NULL || shell->selected_ceremony != NULL) return;
    const unsigned pending = portable_mobile_lifecycle_take();
    if (pending & PORTABLE_MOBILE_BACKGROUND)
    {
        shell->focused = 0;
        portable_course_3d_host_set_paused(shell->preview_3d, 1);
        shell->mesh_driving_pressed = 0u;
        shell->keyboard_buttons = shell->keyboard_pressed = 0u;
        shell->wait_for_neutral = 1;
        portable_mobile_touch_clear();
        SDL_ResetKeyboard();
        if (shell->record_file != NULL && fflush(shell->record_file) != 0) shell->error = 1;
        if (shell->audio != NULL) portable_sdl_audio_queue_pause(&shell->audio->queue, 1);
    }
    if (pending & PORTABLE_MOBILE_FOREGROUND)
    {
        shell->focused = 1;
        shell->needs_present = 1;
        if (shell->audio != NULL) portable_sdl_audio_queue_pause(&shell->audio->queue, 0);
    }
    if (pending & (PORTABLE_MOBILE_BACKGROUND | PORTABLE_MOBILE_FOREGROUND))
        portable_frame_scheduler_reset(&shell->scheduler,
            shell->aid != NULL ? shell->aid_now : frontend_now_ns(shell->frequency), shell->focused);
    if (pending & PORTABLE_MOBILE_TERMINATING) shell->quit = 1;
}

/* One accepted shell iteration is shared by desktop looping and browser
 * scheduling. Scene ticks keep their own fixed-step admission and ordering. */
static int game_shell_iterate(GameShell *shell)
{
    game_shell_watch_mods(shell);
    if (shell->aid != NULL && shell->aid->shell_route && shell->flow.page != shell->aid_page)
    {
        shell->aid_page = shell->flow.page;
        printf("Game shell menu: page=%u mode=%u course=%u\n", shell->flow.page,
               shell->flow.mode_cursor, shell->flow.selected_course);
        fflush(stdout);
    }
    if (shell->aid != NULL && ++shell->aid_frames > 300000u)
    {
        shell->failure_detail = "GP cup aid timeout";
        shell->error = 1;
        return 0;
    }
    if (shell->aid != NULL && shell->aid->tour != NULL &&
        shell->flow.page >= PORTABLE_FRONTEND_PAGE_RACE &&
        shell->flow.page <= PORTABLE_FRONTEND_PAGE_RESULTS)
    {
        const char *races=getenv("MKSC_MENU_TOUR_RACES");
        if(races==NULL || strcmp(races,"1")!=0){
            shell->failure_detail = "menu tour left the menus for a race";
            shell->error = 1;
            return 0;
        }
    }
    if (shell->aid != NULL)
    {
        const PortableMeshTimeTrialSession *mesh =
            portable_course_3d_host_session(shell->preview_3d);
        shell->mesh_aid_fractional = shell->mesh_aid_subframes && mesh != NULL &&
            mesh->state.tick >= 320u && mesh->state.tick < 352u &&
            portable_course_3d_host_world_active(shell->preview_3d);
        if (shell->mesh_aid_fractional)
        {
            /* Carry the rational GBA tick remainder across render samples. */
            const uint64_t denominator = UINT64_C(262144) * shell->mesh_aid_subframes;
            const uint64_t elapsed = UINT64_C(4389) * UINT64_C(1000000000) +
                                     shell->mesh_aid_remainder;
            shell->aid_now += elapsed / denominator;
            shell->mesh_aid_remainder = elapsed % denominator;
        }
        else
            shell->aid_now += UINT64_C(16742707);
    }
    game_shell_mobile_lifecycle(shell);
    game_shell_poll_events(shell);
    game_shell_mobile_lifecycle(shell);
    /* Android can block inside the event pump until foregrounding. Sample
     * wall time afterwards so reset clocks never receive an older timestamp. */
    const uint64_t now = shell->aid != NULL ? shell->aid_now : frontend_now_ns(shell->frequency);
    game_shell_poll_install(shell);
    const int background_import = portable_ui_background_host_poll(&shell->ui_background);
    if (background_import != 0) {
        shell->options_notice = portable_ui_background_host_notice(&shell->ui_background);
        shell->needs_present = 1;
    }
    if (shell->quit)
        return 0;
    if (shell->selected_trial != NULL)
    {
        if (!game_shell_tick_trial(shell, now))
            return 0;
        return 1;
    }
    if (shell->selected_ceremony != NULL)
    {
        if (!game_shell_tick_ceremony(shell, now))
            return 0;
        return 1;
    }
    if (shell->selected_gp != NULL)
    {
        if (!game_shell_tick_gp(shell, now))
            return 0;
        return 1;
    }
    if (!game_shell_advance_menu(shell, now))
        return 0;
    const GameShellLaunch launched = game_shell_begin_race(shell);
    if (launched == GAME_SHELL_LAUNCH_FAILED)
        return 0;
    if (launched == GAME_SHELL_LAUNCHED)
        return 1;
    if (!game_shell_render_menu(shell))
        return 0;
    SDL_SetWindowTitle(
        shell->window,
        "MKSC | arrows move | Enter/A choose | Space/Start advance | Esc/B back");
    if (shell->aid == NULL)
        game_shell_idle();
    return 1;
}

typedef struct
{
    GameShell shell;
    PortableAssetPackView base;
    uint8_t *base_bytes, *section_bytes;
    PortableAssetSectionView *sections;
    PortableModSet *mods;
    PortableScriptHost *scripts;
    PortableDrivingProfile *driving_profile;
    GameInputAid aid;
    char *aid_strings[3];
} GameApplication;

static int game_application_close(GameApplication *app)
{
    portable_loading_close();
    const int result = game_shell_close(&app->shell);
    portable_script_host_free(app->scripts);
    portable_mod_identity_set_current(NULL);
    portable_mods_free(app->mods);
    portable_driving_profile_free(app->driving_profile);
    portable_mod_identity_set_driving_profile(NULL);
    for (size_t i = 0u; i < 3u; ++i)
        free(app->aid_strings[i]);
    free(app->sections);
    free(app->section_bytes);
    free(app->base_bytes);
    free(app);
    return result;
}

static GameApplication *game_application_create(const PortableAssetPackView *base,
                                                 const GameInputAid *aid)
{
    if (base == NULL)
        return NULL;
    GameApplication *app = calloc(1u, sizeof *app);
    if (app == NULL)
        return NULL;
    app->base = *base;
#ifdef __EMSCRIPTEN__
    /* main() may return after scheduling and release its input buffer. Keep
     * payloads and any composed section table alive until the shell stops. */
    if (base->size != 0u)
    {
        if (base->bytes == NULL)
            goto failed;
        app->base_bytes = malloc(base->size);
        if (app->base_bytes == NULL)
            goto failed;
        memcpy(app->base_bytes, base->bytes, base->size);
        app->base.bytes = app->base_bytes;
    }
    if (base->resolved_sections != NULL)
    {
        size_t bytes = 0u;
        if (base->section_count > SIZE_MAX / sizeof *app->sections)
            goto failed;
        for (uint32_t i = 0u; i < base->section_count; ++i)
        {
            if (base->resolved_sections[i].size > SIZE_MAX - bytes)
                goto failed;
            bytes += base->resolved_sections[i].size;
        }
        app->sections = calloc(base->section_count, sizeof *app->sections);
        app->section_bytes = malloc(bytes != 0u ? bytes : 1u);
        if (app->sections == NULL || app->section_bytes == NULL)
            goto failed;
        size_t offset = 0u;
        for (uint32_t i = 0u; i < base->section_count; ++i)
        {
            app->sections[i] = base->resolved_sections[i];
            if (app->sections[i].size != 0u)
            {
                if (app->sections[i].bytes == NULL)
                    goto failed;
                memcpy(app->section_bytes + offset, app->sections[i].bytes, app->sections[i].size);
            }
            app->sections[i].bytes = app->section_bytes + offset;
            offset += app->sections[i].size;
        }
        app->base.resolved_sections = app->sections;
    }
#endif
    if (aid != NULL)
    {
        app->aid = *aid;
        const char *strings[3] = {aid->save_directory, aid->tour, aid->tour_frames};
        for (size_t i = 0u; i < 3u; ++i)
            if (strings[i] != NULL)
            {
                const size_t length = strlen(strings[i]) + 1u;
                app->aid_strings[i] = malloc(length);
                if (app->aid_strings[i] == NULL)
                    goto failed;
                memcpy(app->aid_strings[i], strings[i], length);
            }
        app->aid.save_directory = app->aid_strings[0];
        app->aid.tour = app->aid_strings[1];
        app->aid.tour_frames = app->aid_strings[2];
        app->shell.aid = &app->aid;
    }
    return app;
failed:
    (void)game_application_close(app);
    return NULL;
}

static const PortableAssetPackView *game_application_selected_base(const GameApplication *app)
{
    return app->driving_profile!=NULL?portable_driving_profile_pack(app->driving_profile):&app->base;
}

/* Validate every replacement owner before saving or publishing content.
 * SDL video/input/audio resources stay in the existing shell. */
static void game_application_prepare_mods(GameApplication *app, size_t menu_width, PortableLoadJob *job, int review)
{
    const uint64_t prepare_started = portable_startup_clock();
    GameShell *shell=&app->shell;
    shell->mod_notice_error = 1u;
    if(shell->record_file!=NULL || (shell->audio!=NULL && shell->audio->capture_file!=NULL)){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Finish recording before applying mods");
        fputs("mod selection rejected: finish recording before applying mods\n",stderr);
        return;
    }
    game_shell_discard_mod_candidate(shell);
    PortableModManagerCandidate *candidate=portable_mod_manager_prepare_domains(shell->mod_manager,game_application_selected_base(app),&app->base,NULL);
    if(candidate==NULL){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",portable_mod_manager_notice(shell->mod_manager));
        fprintf(stderr,"mod selection rejected: %s\n",shell->mod_notice);
        return;
    }
    if (portable_load_cancelled(job)) { portable_mod_manager_candidate_free(candidate); return; }
    portable_load_phase(job, 1);
    GameShell *prepared=game_shell_prepare_replacement_width(shell,
        candidate->count?portable_mods_pack(candidate->mods):game_application_selected_base(app),
        candidate->count?portable_mods_pack_3d(candidate->mods):&app->base,
        candidate->count?candidate->mods:NULL,menu_width,shell->mod_notice);
    if(prepared==NULL){
        fprintf(stderr,"mod selection rejected: %s\n",shell->mod_notice);
        portable_mod_manager_candidate_free(candidate);return;
    }
    prepared->scripts=candidate->scripts;
    prepared->settings.mod_count=(uint8_t)candidate->count;
    memcpy(prepared->settings.mods,candidate->ids,sizeof prepared->settings.mods);
    memcpy(prepared->settings.asset_sources,candidate->asset_sources,sizeof prepared->settings.asset_sources);
    shell->mod_candidate=candidate;
    shell->mod_prepared=prepared;
    if(review)shell->options_open=5;
    shell->mod_notice_error = 0u;
    shell->mod_cursor=0u;shell->mod_detail_page=0u;
    snprintf(shell->mod_notice,sizeof shell->mod_notice,
        "Validated %u mods. Review order, then A/Start applies; B edits",(unsigned)candidate->count);
    portable_startup_phase("mod order preparation", prepare_started);
    printf("mod selection validated: count=%u%s\n",(unsigned)candidate->count,review?"; awaiting confirmation":"");
}

typedef struct { GameApplication *app; size_t width; int reload, review; } ModPrepareLoad;
static int game_application_prepare_worker(void *data, PortableLoadJob *job)
{
    ModPrepareLoad *load = data;
    GameShell *shell = &load->app->shell;
    if (load->reload && !portable_mod_manager_refresh(shell->mod_manager)) {
        snprintf(shell->mod_notice, sizeof shell->mod_notice, "%s", portable_mod_manager_notice(shell->mod_manager));
        shell->mod_notice_error = 1u;
        return 0;
    }
    if (portable_load_cancelled(job)) return 0;
    game_application_prepare_mods(load->app, load->width, job, load->review);
    return shell->mod_candidate != NULL && shell->mod_prepared != NULL;
}
/* Names the mods the loading screen is preparing, in load order. */
static void game_application_loading_detail(const GameShell *shell, char *text, size_t capacity)
{
    const size_t count = portable_mod_manager_count(shell->mod_manager);
    int written = snprintf(text, capacity, count ? "%u mod%s: " : "No mods: original game content",
                           (unsigned)count, count == 1u ? "" : "s");
    size_t used = written > 0 ? (size_t)written : 0u;
    for (size_t i = 0u; i < count && used + 1u < capacity; ++i) {
        written = snprintf(text + used, capacity - used, "%s%s", i ? ", " : "",
                           game_shell_mod_name(shell, portable_mod_manager_id(shell->mod_manager, i)));
        if (written < 0) break;
        used += (size_t)written;
    }
}

static void game_application_prepare_async(GameApplication *app, int reload, int review, const char *title)
{
    GameShell *shell = &app->shell;
    ModPrepareLoad load = {app, game_shell_prepare_width(shell), reload, review};
    static const char *const phases[] = {"Checking mod content and order", "Preparing courses, music and menus"};
    char detail[512];
    game_application_loading_detail(shell, detail, sizeof detail);
    if (shell->audio) portable_sdl_audio_queue_pause(&shell->audio->queue, 1);
    const int result = portable_loading_run_detail(shell->window, shell->renderer,
        title != NULL ? title : reload ? "Reloading your mods" : "Preparing your mods", detail, phases, 2,
        game_application_prepare_worker, &load);
    if (shell->audio) portable_sdl_audio_queue_pause(&shell->audio->queue, 0);
    if (result != 1) {
        game_shell_discard_mod_candidate(shell);
        if (result < 0) {
            shell->quit = result == -2;
            shell->options_open = shell->library_return ? 7 : 4;
            snprintf(shell->mod_notice, sizeof shell->mod_notice, "Cancelled. Previous mods remain active.");
        }
    }
    shell->wait_for_neutral = 1;
    shell->needs_present = 1;
    portable_frame_scheduler_reset(&shell->scheduler,
        shell->aid ? shell->aid_now : frontend_now_ns(shell->frequency), shell->focused);
}

static int game_application_commit_mods(GameApplication *app,PortableDrivingProfile *profile,int change_profile)
{
    const uint64_t commit_started = portable_startup_clock();
    GameShell *shell=&app->shell,*prepared=shell->mod_prepared;
    PortableModManagerCandidate *candidate=shell->mod_candidate;
    if(prepared==NULL || candidate==NULL)return 0;
    PortableSettings saved=shell->persisted_settings;
    if(change_profile)saved.driving_profile=prepared->settings.driving_profile;
    else{
        saved.mod_count=(uint8_t)candidate->count;
        memcpy(saved.mods,candidate->ids,sizeof saved.mods);
        memcpy(saved.asset_sources,candidate->asset_sources,sizeof saved.asset_sources);
    }
    if(shell->settings_path[0]=='\0' || !portable_settings_save(shell->settings_path,&saved)){
        shell->mod_notice_error = 1u;
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",change_profile?
            "Cannot save driving profile; previous profile remains active":
            "Cannot save mod selection; previous mods remain active");
        fputs(change_profile?"driving profile save failed; previous profile remains active\n":
            "mod selection save failed; previous mods remain active\n",stderr);
        return 0;
    }
    /* Content, target progress and settings are validated. Transfer owners
     * and publish the matching save namespace without further file reads. */
    prepared->persisted_settings=saved;
    memcpy(prepared->settings_path,shell->settings_path,sizeof prepared->settings_path);
    memcpy(prepared->mods_directory,shell->mods_directory,sizeof prepared->mods_directory);
    prepared->aid=shell->aid;prepared->aid_now=shell->aid_now;prepared->aid_frames=shell->aid_frames;
    prepared->tour=shell->tour;prepared->tour.hold=0u;prepared->tour.held=0u;
    prepared->tour.idle=0u;prepared->tour.motion_started=0u;
    prepared->window=shell->window;prepared->renderer=shell->renderer;prepared->texture=shell->texture;
    prepared->gpu_aid_target=shell->gpu_aid_target;
    prepared->controller=shell->controller;prepared->frequency=shell->frequency;
    prepared->focused=shell->focused;prepared->needs_present=1;prepared->wait_for_neutral=1;
    prepared->menu_target=shell->menu_target;prepared->frame=shell->frame;
    prepared->modern=shell->modern;
    prepared->audio->queue=shell->audio->queue;
    (void)SDL_ClearAudioStream(prepared->audio->queue.stream);
    prepared->mod_manager=shell->mod_manager;prepared->mod_cursor=shell->mod_cursor;
    prepared->mod_order=shell->mod_order;prepared->mod_advanced=shell->mod_advanced;
    prepared->options_open=change_profile?3:shell->library_return?7:4;
    prepared->library_game=shell->library_game;prepared->library_row=shell->library_row;
    prepared->library_return=shell->library_return;
    prepared->mod_exit_target=shell->mod_exit_target;
    prepared->watch_mods=shell->watch_mods;
    prepared->enhancements_cursor=change_profile?1u:shell->enhancements_cursor;
    prepared->flow.title_cursor=3u;
    snprintf(prepared->mod_notice,sizeof prepared->mod_notice,"Applied and saved %u mods",(unsigned)candidate->count);
    portable_frame_scheduler_init(&prepared->scheduler);
    portable_ui_background_host_close(&shell->ui_background);
    if(shell->editor_previous_preview) {
        shell->preview_3d=shell->editor_previous_preview;shell->editor_previous_preview=NULL;
    }
    course_editor_host_close(shell->editor);shell->editor=NULL;
    portable_course_3d_host_close(shell->preview_3d);
    free(shell->audio);free(shell->scratch);free(shell->quick_recording);
    portable_store_job_free(shell->store_job);
    /* A Store install loads in one step; the shopper stays in the Store. */
    prepared->store=shell->store;shell->store=NULL;
    prepared->store_open=shell->store_open;prepared->store_source=shell->store_source;
    prepared->store_cursor=shell->store_cursor;prepared->store_details=shell->store_details;
    prepared->store_action=shell->store_action;
    free(shell->store_loading);
    if(shell->store_download_path[0])remove(shell->store_download_path);
    PortableModSet *old_mods=app->mods;
    PortableScriptHost *old_scripts=app->scripts;
    if(candidate->count==0u){
        portable_mods_free(candidate->mods);candidate->mods=NULL;
    }
    PortableDrivingProfile *old_profile=NULL;
    if(change_profile){old_profile=app->driving_profile;app->driving_profile=profile;}
    prepared->base_pack=game_application_selected_base(app);
    prepared->base_pack_3d=&app->base;
    app->mods=candidate->mods;candidate->mods=NULL;
    app->scripts=candidate->scripts;candidate->scripts=NULL;
    *shell=*prepared;
    /* These are the only views into the temporary menu owner's own arrays. */
    shell->config.drivers=shell->drivers;shell->config.courses=shell->courses;
    shell->flow.config=shell->config;
    shell->mods=app->mods;
    if(shell->flow.mesh_cup_count)shell->flow.mesh_cups=shell->mesh_cups;
    if(shell->flow.mesh_group_count){shell->flow.mesh_group_names=shell->mesh_group_names;
        shell->flow.mesh_group_platforms=shell->mesh_group_platforms;}
    shell->audio_stream.commands=shell->audio_commands;
    shell->menu_target.frame.ui_text=&shell->ui_text;shell->frame.ui_text=&shell->ui_text;
    portable_mod_identity_set_current(portable_mods_identity(app->mods));
    portable_mod_identity_set_mirror(shell->settings.mirror);
    /* The immutable candidate's profile and target progress were validated
     * before saving. Its prepared flow already belongs to this namespace. */
    (void)portable_driving_profile_publish(shell->pack);
    portable_menu_names_set_3d_drivers(shell->pack_3d);
    modern_menu_host_content_changed(&shell->modern);
    portable_script_host_free(old_scripts);portable_mods_free(old_mods);
    portable_driving_profile_free(old_profile);
    if(!change_profile)printf("mod selection applied: count=%u; records and ghosts use this content identity\n",(unsigned)candidate->count);
    portable_mod_manager_candidate_free(candidate);
    free(prepared);
    if (!change_profile && shell->mod_exit_target != 0u)
        game_shell_mod_exit(shell,shell->mod_exit_target);
    if(change_profile){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Drift extras saved: %s",
            shell->settings.driving_profile==2u?"TIERS+AIR":shell->settings.driving_profile?"TIERS":"OFF");
        shell->options_notice=shell->mod_notice;
        printf("driving profile applied: %s; separate records and ghosts\n",
            shell->settings.driving_profile==2u?"modern-air":shell->settings.driving_profile?"modern":"original");
    }
    portable_startup_phase("mod order handoff", commit_started);
    return 1;
}

/* A profile edit rebuilds current user layers against the immutable original
 * base. It commits one saved setting, leaving temporary CLI mods/scalars alone. */
static void game_application_change_profile(GameApplication *app,uint8_t wanted)
{
    GameShell *shell=&app->shell;
    if(wanted>2u || wanted==shell->settings.driving_profile)return;
    if(shell->record_file!=NULL || (shell->audio!=NULL && shell->audio->capture_file!=NULL)){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Finish recording before changing driving profile");
        shell->options_notice=shell->mod_notice;return;
    }
    PortableDrivingProfile *profile=wanted?portable_driving_profile_create(&app->base,wanted):NULL;
    if(wanted && profile==NULL){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"Cannot allocate modern driving profile");
        shell->options_notice=shell->mod_notice;return;
    }
    const PortableAssetPackView *base=profile?portable_driving_profile_pack(profile):&app->base;
    const PortableModRoots roots=game_shell_mod_roots(shell);
    PortableModManager *manager=portable_mod_manager_create_roots(&roots,
        (const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.mods,shell->settings.mod_count);
    portable_mod_manager_set_sources(manager,(const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.asset_sources);
    PortableModManagerCandidate *candidate=manager?portable_mod_manager_prepare_domains(manager,base,&app->base,NULL):NULL;
    if(candidate==NULL){
        snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s",manager?
            portable_mod_manager_notice(manager):"Cannot allocate profile mod validation");
        portable_mod_manager_free(manager);portable_driving_profile_free(profile);
        shell->options_notice=shell->mod_notice;return;
    }
    portable_mod_manager_free(manager);
    const PortableAssetPackView *pack=candidate->count?portable_mods_pack(candidate->mods):base;
    InstallPreflight check={.shell=shell};
    if(!game_shell_validate_install_pack(pack,&check,shell->mod_notice)){
        portable_mod_manager_candidate_free(candidate);portable_driving_profile_free(profile);
        shell->options_notice=shell->mod_notice;return;
    }
    GameShell *prepared=game_shell_prepare_replacement(shell,pack,
        candidate->count?portable_mods_pack_3d(candidate->mods):&app->base,
        candidate->count?candidate->mods:NULL,shell->mod_notice);
    if(prepared==NULL){
        portable_mod_manager_candidate_free(candidate);portable_driving_profile_free(profile);
        shell->options_notice=shell->mod_notice;return;
    }
    prepared->scripts=candidate->scripts;
    prepared->settings.driving_profile=wanted;
    game_shell_discard_mod_candidate(shell);
    shell->mod_candidate=candidate;shell->mod_prepared=prepared;
    if(!game_application_commit_mods(app,profile,1)){
        game_shell_discard_mod_candidate(shell);
        portable_driving_profile_free(profile);
        shell->options_notice=shell->mod_notice;
    }
}

static void game_application_mod_request(GameApplication *app)
{
    const uint8_t request=app->shell.mod_apply_pending;
    app->shell.mod_apply_pending=0u;
    const uint8_t profile_request=app->shell.profile_request;
    app->shell.profile_request=0u;
    if(profile_request!=0u)game_application_change_profile(app,(uint8_t)(profile_request-1u));
    else if(request==1u)game_application_prepare_async(app,0,1,NULL);
    else if(request==2u)(void)game_application_commit_mods(app,NULL,0);
    else if(request==3u){
        GameShell *shell=&app->shell;
        if (game_shell_mods_dirty(shell)) {
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"Apply or discard staged changes before Reload.");
            shell->mod_notice_error=1u;
            puts("mod reload rejected: apply or discard staged changes first");
            return;
        }
        const unsigned cursor=shell->mod_cursor;
        const uint8_t order=shell->mod_order;
        game_application_prepare_async(app,1,0,NULL);
        if (shell->mod_candidate != NULL && shell->mod_prepared != NULL) {
            (void)game_application_commit_mods(app,NULL,0);
            if (shell->mod_candidate == NULL) {
                shell->mod_cursor=cursor;shell->mod_order=order;
                snprintf(shell->mod_notice,sizeof shell->mod_notice,"Reloaded content from disk. Selection saved.");
                puts("mod reload applied: content rebuilt from disk; selection saved");
            }
        } else if (shell->mod_notice_error) fprintf(stderr,"mod reload rejected: %s\n",shell->mod_notice);
        const size_t count=shell->options_open==5 || shell->mod_order?
            portable_mod_manager_count(shell->mod_manager):portable_mod_menu_row_count(shell->mod_manager,shell->mod_advanced);
        if (shell->mod_cursor>=count) shell->mod_cursor=count?(unsigned)count-1u:0u;
    } else if(request==4u){
        /* One step: validate, save and load the staged selection, or put
         * the active selection back and say which mod failed and why. */
        GameShell *shell=&app->shell;
        char label[sizeof shell->mod_apply_label];
        memcpy(label,shell->mod_apply_label,sizeof label);
        char remove_id[sizeof shell->store_remove_pending];
        memcpy(remove_id,shell->store_remove_pending,sizeof remove_id);
        shell->store_remove_pending[0]='\0';
        const unsigned cursor=shell->mod_cursor;
        game_application_prepare_async(app,0,0,label);
        shell->mod_cursor=cursor;
        if (shell->mod_candidate != NULL && shell->mod_prepared != NULL &&
            game_application_commit_mods(app,NULL,0)) {
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"Done: %s. %u mod%s on.",label,
                (unsigned)shell->settings.mod_count,shell->settings.mod_count==1u?"":"s");
            shell->mod_notice_error=0u;
            if(remove_id[0]!='\0' && game_shell_store_remove_archive(shell,remove_id))
                snprintf(shell->mod_notice,sizeof shell->mod_notice,"Done: %s. %u mod%s on.",label,
                    (unsigned)shell->settings.mod_count,shell->settings.mod_count==1u?"":"s");
        } else {
            char reason[PORTABLE_MOD_ERROR_CAPACITY];
            memcpy(reason,shell->mod_notice,sizeof reason);
            game_shell_discard_mod_candidate(shell);
            if (shell->options_open == 5) shell->options_open = shell->library_return ? 7 : 4;
            (void)portable_mod_manager_select(shell->mod_manager,
                (const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.mods,shell->settings.mod_count);
            portable_mod_manager_set_sources(shell->mod_manager,
                (const char (*)[PORTABLE_MOD_ID_CAPACITY])shell->settings.asset_sources);
            snprintf(shell->mod_notice,sizeof shell->mod_notice,"%s failed: %s",label,reason);
            shell->mod_notice_error=1u;
        }
        const size_t count=shell->mod_order?portable_mod_manager_count(shell->mod_manager):
            portable_mod_menu_row_count(shell->mod_manager,shell->mod_advanced);
        if (shell->mod_cursor>=count) shell->mod_cursor=count?(unsigned)count-1u:0u;
    }
}

#ifdef __EMSCRIPTEN__
static void game_application_browser_frame(void *context)
{
    GameApplication *app = context;
    if (app->shell.quit || app->shell.error || !game_shell_iterate(&app->shell))
    {
        emscripten_cancel_main_loop();
        const int result = game_application_close(app);
        emscripten_force_exit(result);
    }
    else game_application_mod_request(app);
}
#endif

static int game_application_run(GameApplication *app, const PortableSettings *initial,
                                const PortableSettings *persisted, const char *initial_path)
{
    app->shell.base_pack = game_application_selected_base(app);
    app->shell.base_pack_3d = &app->base;
    app->shell.pack = app->mods != NULL ? portable_mods_pack(app->mods) : app->shell.base_pack;
    app->shell.pack_3d = app->mods != NULL ? portable_mods_pack_3d(app->mods) : app->shell.base_pack_3d;
    app->shell.mods = app->mods;
    if(!portable_driving_profile_publish(app->shell.pack)){
        fputs("Selected driving profile is invalid\n",stderr);return game_application_close(app);
    }
    if(initial->driving_profile)
        printf("Driving profile active: %s; records/ghosts/progress domain=%s\n",
            initial->driving_profile==2u?"modern-air":"modern",portable_mod_identity_progress_suffix());
    app->shell.scripts = app->scripts;
    if (game_shell_setup(&app->shell, initial, persisted, initial_path))
        return game_application_close(app);
    portable_startup_mark("shell ready");
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(game_application_browser_frame, app, 0, 0);
    return 0;
#else
    int first_frame = 1;
    while (!app->shell.quit && !app->shell.error && game_shell_iterate(&app->shell))
    {
        if (first_frame) { portable_startup_mark("first menu frame"); first_frame = 0; }
        game_application_mod_request(app);
    }
    return game_application_close(app);
#endif
}

typedef struct {
    GameApplication *app;
    const PortableSettings *settings;
    const char *root;
} GameStartupLoad;

static int game_application_load(void *data, PortableLoadJob *job)
{
    const GameStartupLoad *load = data;
    GameApplication *app = load->app;
    const PortableSettings *settings = load->settings;
    const char *root = load->root;
    const PortableAssetPackView *base;
    if(settings->driving_profile){
        app->driving_profile=portable_driving_profile_create(&app->base,settings->driving_profile);
        if(app->driving_profile==NULL){fputs("Cannot create modern driving profile\n",stderr);return 0;}
    }
    base = game_application_selected_base(app);
    if(snprintf(app->shell.mods_directory,sizeof app->shell.mods_directory,"%s",root)>=
        (int)sizeof app->shell.mods_directory){
        fputs("mod directory path too long\n",stderr);
        return 0;
    }
    const uint64_t mods_started = portable_startup_clock();
    PortableModSet *mods = NULL;
    if (settings->mod_count != 0u)
    {
        if (root[0] == '\0')
        {
            fputs("mod directory unavailable\n", stderr);
            return 0;
        }
        const PortableModRoots roots=game_mod_roots(root,settings);
        mods =
            portable_mods_load_roots_domains_report(base, &app->base,&roots,
                (const char (*)[PORTABLE_MOD_ID_CAPACITY])settings->mods, settings->mod_count, NULL);
        char source_error[PORTABLE_MOD_ERROR_CAPACITY]={0};
        if(mods && !portable_mods_select_assets(mods,base,&app->base,
            (const char (*)[PORTABLE_MOD_ID_CAPACITY])settings->asset_sources,source_error,sizeof source_error)){
            fprintf(stderr,"%s\n",source_error);portable_mods_free(mods);mods=NULL;
        }
        if (mods == NULL)
        {
            fputs("could not allocate or compose enabled mods\n", stderr);
            return 0;
        }
    }
    portable_startup_phase("mod scan/load/validate", mods_started);
    app->mods = mods;
    if (portable_load_cancelled(job)) return 0;
    portable_load_phase(job, 1);
    /* Preview has no race hooks yet, but edited 3D scripts must still pass
     * the same sandbox/main-chunk validation on a saved-selection relaunch. */
    PortableScriptSource sources_3d[PORTABLE_MOD_LIMIT];
    size_t count_3d = 0u;
    for (size_t i = 0u; i < portable_mods_loaded_count(mods); ++i)
    {
        PortableScriptSource *source = &sources_3d[count_3d];
        if (portable_mods_script_3d(mods, i, &source->id, &source->source, &source->size))
            ++count_3d;
    }
    if (count_3d != 0u)
    {
        PortableScriptHost *validation = portable_script_host_create(sources_3d, count_3d);
        const int valid = validation != NULL &&
            portable_script_host_active_count(validation) == count_3d;
        portable_script_host_free(validation);
        if (!valid)
        {
            fputs("3D mod script validation failed on relaunch\n", stderr);
            return 0;
        }
    }
    /* Ghosts, records and GP progress written this session record the set. */
    PortableScriptSource sources[PORTABLE_MOD_LIMIT];
    size_t script_count = 0u;
    for (size_t i = 0u; i < portable_mods_loaded_count(mods); ++i)
    {
        PortableScriptSource *source = &sources[script_count];
        if (portable_mods_script(mods, i, &source->id, &source->source, &source->size))
            ++script_count;
    }
    PortableScriptHost *scripts = NULL;
    if (script_count != 0u)
    {
        scripts = portable_script_host_create(sources, script_count);
        if (scripts == NULL)
        {
            fputs("could not allocate mod scripts\n", stderr);
            return 0;
        }
        printf("mod scripts: %zu of %zu active\n", portable_script_host_active_count(scripts),
               script_count);
    }
    app->scripts = scripts;
    return 1;
}

static int run_game_shell(const PortableAssetPackView *base, const GameInputAid *aid,
                          const PortableSettingsOverrides *overrides)
{
    portable_screen_capture_reset();
    PortableSettings settings, persisted;
    portable_settings_defaults(&settings);
    char path[4096] = {0}, mods_directory[4096] = {0};
    /* Tours normally isolate settings as well as saves. An explicit opt-in
     * exercises ordinary persisted Options through the same shell owner. */
    const char *tour_settings=aid != NULL && aid->tour != NULL
        ? getenv("MKSC_MENU_TOUR_PERSIST_SETTINGS") : NULL;
    const char *aid_settings=aid != NULL?getenv("MKSC_AID_PERSIST_SETTINGS"):NULL;
    if (aid == NULL || (tour_settings != NULL && strcmp(tour_settings,"1")==0) ||
        (aid_settings != NULL && strcmp(aid_settings,"1")==0))
    {
        char *directory = portable_data_directory();
        if (directory != NULL)
        {
            char detail[PORTABLE_SETTINGS_ERROR_CAPACITY];
            const int length = snprintf(path, sizeof path, "%ssettings.json", directory);
            const int root_length =
                snprintf(mods_directory, sizeof mods_directory, "%smods", directory);
            if (length <= 0 || (size_t)length >= sizeof path || root_length <= 0 ||
                (size_t)root_length >= sizeof mods_directory)
            {
                SDL_free(directory);
                fputs("settings/mod directory path too long\n", stderr);
                return 1;
            }
            const int loaded = portable_settings_load(path, &settings, detail);
            if (loaded < 0)
                fprintf(stderr, "%s: %s; using defaults\n", path, detail);
            if (loaded == 0 && !portable_settings_save(path, &settings))
                fprintf(stderr, "could not create settings: %s\n", path);
            SDL_free(directory);
        }
        else
            fputs("settings folder unavailable; using defaults\n", stderr);
    }
    persisted = settings;
    portable_settings_apply(&settings, overrides);
    if(overrides!=NULL && overrides->no_mods){
        settings.mod_count=0u;memset(settings.mods,0,sizeof settings.mods);
    }
    portable_mod_identity_set_mirror(settings.mirror);
    const char *root =
        overrides != NULL && overrides->mods_dir != NULL ? overrides->mods_dir : mods_directory;
    GameApplication *app = game_application_create(base, aid);
    if (app == NULL)
    {
        fputs("could not allocate application state\n", stderr);
        return 1;
    }
    app->shell.watch_mods=overrides!=NULL && overrides->watch_mods_set && overrides->watch_mods;
    if (!portable_loading_begin(aid != NULL)) return game_application_close(app);
    GameStartupLoad load = {app, &settings, root};
    static const char *const phases[] = {"Loading enabled mods", "Preparing mod scripts"};
    const int loaded = portable_loading_run(NULL, NULL, "Loading your content", phases, 2,
                                            game_application_load, &load);
    if (loaded != 1) { (void)game_application_close(app); return loaded < 0 ? 0 : 1; }
    portable_mod_identity_set_current(portable_mods_identity(app->mods));
    return game_application_run(app, &settings, &persisted, path);
}

int run_time_trial_sdl_settings(const PortableAssetPackView *pack,
                                const PortableSettingsOverrides *overrides)
{
    return run_game_shell(pack, NULL, overrides);
}

int run_battle_menu_smoke(const PortableAssetPackView *pack, uint8_t arena, uint32_t character,
                          uint8_t engine_class, const PortableSettingsOverrides *overrides)
{
    const GameInputAid aid = {.cup = PORTABLE_FRONTEND_ARENA_CUP,
                              .engine_class = engine_class,
                              .character = character,
                              .battle = 1u,
                              .arena = arena};
    return run_game_shell(pack, &aid, overrides);
}

/* VS takes a course content ID; Battle an arena row 0..3. */
int run_local_multiplayer_smoke(const PortableAssetPackView *pack, uint8_t battle, uint8_t players,
                                uint32_t course_id, const PortableSettingsOverrides *overrides)
{
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
    PortableFrontendConfig config;
    if (players < 2u || players > PORTABLE_LOCAL_PLAYERS_MAX ||
        !build_frontend_config(pack, drivers, courses, &config))
        return 1;
    GameInputAid aid = {.engine_class = 0u,
                        .character = drivers[0].content_id,
                        .players = players,
                        .battle = battle,
                        .quick_run = (uint8_t)!battle};
    int found = 0;
    for (size_t i = 0u; i < config.course_count && !found; ++i)
    {
        const PortableFrontendCourseSlot *slot = &config.courses[i];
        if (battle ? (slot->cup == PORTABLE_FRONTEND_ARENA_CUP && slot->track == course_id)
                   : (slot->cup != PORTABLE_FRONTEND_ARENA_CUP && slot->content_id == course_id))
        {
            aid.cup = slot->cup;
            aid.track = slot->track;
            aid.arena = (uint8_t)(battle ? course_id : 0u);
            found = 1;
        }
    }
    if (!found)
    {
        fprintf(stderr, "local multiplayer smoke: %s %u unavailable\n", battle ? "arena" : "course",
                course_id);
        return 1;
    }
    return run_game_shell(pack, &aid, overrides);
}

int run_quick_run_menu_smoke(const PortableAssetPackView *pack, uint8_t cup, uint32_t character,
                             uint8_t engine_class, uint8_t options_mask, uint8_t replay_aid,
                             const PortableSettingsOverrides *overrides)
{
    const GameInputAid aid = {cup, engine_class, character,  0u,   NULL,
                              1u,  options_mask, replay_aid, 0u,   0u,
                              0u,  0u,           0u,         NULL, NULL};
    return run_game_shell(pack, &aid, overrides);
}

int run_gp_cup_menu_smoke(const PortableAssetPackView *pack, uint8_t cup, uint32_t character,
                          uint8_t engine_class, int interpolate, uint16_t render_fps,
                          const PortableSettingsOverrides *settings)
{
    const char *path = getenv("MKSC_GP_PROGRESS");
    if (path == NULL || path[0] == '\0')
    {
        fputs("SDL cup aid requires MKSC_GP_PROGRESS pointing to a scratch save\n", stderr);
        return 1;
    }
    GameInputAid aid = {cup, engine_class, character, 0u, NULL, 0u,   0u,  0u,
                        0u,  0u,           0u,        0u, 0u,   NULL, NULL};
    const char *seed = getenv("MKSC_GP_CUP_FINAL_SEED");
    if (seed != NULL && seed[0] != '\0')
    {
#ifdef PORTABLE_ENABLE_TEST_HOOKS
        if (seed[0] < '0' || seed[0] > '7' || seed[1] != '\0')
        {
            fputs("MKSC_GP_CUP_FINAL_SEED requires rank0..7\n", stderr);
            return 1;
        }
        aid.final_seed = (uint8_t)(seed[0] - '0' + 1);
#else
        fputs("Final standings seeds require PORTABLE_BUILD_TESTS\n", stderr);
        return 1;
#endif
    }
    PortableSettingsOverrides overrides = {0};
    if (settings != NULL)
        overrides = *settings;
    if (interpolate)
    {
        overrides.mask |= (UINT64_C(1) << PORTABLE_SETTING_INTERPOLATE) |
                          (UINT64_C(1) << PORTABLE_SETTING_RENDER_FPS);
        overrides.values.interpolate = 1u;
        overrides.values.render_fps = render_fps;
    }
    return run_game_shell(pack, &aid, &overrides);
}

int run_menu_tour_smoke(const PortableAssetPackView *pack, const char *save_directory,
                        const char *script, const char *frames,
                        const PortableSettingsOverrides *overrides)
{
    char directory[4096];
    const int length = snprintf(directory, sizeof directory, "%s/", save_directory);
    if (length <= 1 || (size_t)length >= sizeof directory || !SDL_CreateDirectory(directory) ||
        (frames != NULL && !SDL_CreateDirectory(frames)))
    {
        fputs("menu tour directory unavailable\n", stderr);
        return 1;
    }
    GameInputAid aid = {0};
    aid.save_directory = directory;
    aid.tour = script;
    aid.tour_frames = frames;
    return run_game_shell(pack, &aid, overrides);
}

/* Development aid selections use decimal bytes, independent of settings. */
static int shell_aid_selection(const char *name, unsigned maximum, uint8_t *out)
{
    const char *value = getenv(name);
    *out = 0u;
    if (value == NULL)
        return 1;
    unsigned parsed = 0u;
    if (*value == '\0')
        goto invalid;
    for (const char *digit = value; *digit != '\0'; ++digit)
    {
        if (*digit < '0' || *digit > '9')
            goto invalid;
        parsed = parsed * 10u + (unsigned)(*digit - '0');
        if (parsed > maximum)
            goto invalid;
    }
    *out = (uint8_t)parsed;
    return 1;
invalid:
    fprintf(stderr, "%s requires a decimal value in0..%u\n", name, maximum);
    return 0;
}

int run_game_shell_smoke(const PortableAssetPackView *pack, const char *save_directory,
                         uint32_t character, const PortableSettingsOverrides *overrides)
{
    const char *progress = getenv("MKSC_GP_PROGRESS");
    if (progress == NULL || progress[0] == '\0')
    {
        fputs("shell aid requires MKSC_GP_PROGRESS pointing to a scratch save\n", stderr);
        return 1;
    }
    char directory[4096];
    const int length = snprintf(directory, sizeof directory, "%s/", save_directory);
    if (length <= 1 || (size_t)length >= sizeof directory || !SDL_CreateDirectory(directory))
    {
        fputs("shell aid save directory unavailable\n", stderr);
        return 1;
    }
    GameInputAid aid = {.character = character, .shell_route = 1u, .save_directory = directory};
    if (!shell_aid_selection("MKSC_SHELL_CUP", 255u, &aid.cup) ||
        !shell_aid_selection("MKSC_SHELL_TRACK", 3u, &aid.track))
        return 1;
    return run_game_shell(pack, &aid, overrides);
}

#endif
