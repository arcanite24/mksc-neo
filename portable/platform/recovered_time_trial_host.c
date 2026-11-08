#include "mobile_lifecycle.h"
#include "mobile_touch.h"
#include "replay_file.h"
#include "rom_import_host.h"
#include "mod_identity.h"
#include "file_replace.h"
#include "time_trial_ghost_exchange.h"
#include "recovered_time_trial_host.h"
#ifdef PORTABLE_ENABLE_SDL3
#include "sdl_gamepad.h"
#include "input_display.h"
#endif

#include "character_renderer.h"
#include "character_menu_renderer.h"
#include "driver_input.h"
#include "frame_scheduler.h"
#include "frontend_flow.h"
#include "frontend_image_renderer.h"
#include "pause_menu_renderer.h"
#include "gp_results_renderer.h"
#include "hud_renderer.h"
#include "time_trial_results.h"
#include "gp_countdown_renderer.h"
#include "menu_font.h"
#include "personal_best.h"
#include "time_trial_result.h"
#include "time_trial_ghost_store.h"
#include "time_trial_ghost_save.h"
#include "time_trial_ghost_library.h"
#include "time_trial_records.h"
#include "race_interpolation.h"
#include "frontend_config.h"
#include "course_menu_renderer.h"
#include "time_trial_ghost_playback.h"
#include "pause_menu.h"
#ifdef PORTABLE_ENABLE_SDL3
#include "modern_menu_host.h"
#endif
#include "recovered_time_trial_replay.h"
#include "recovered_time_trial_session.h"
#include "recovered_race_smoke.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>

static uint64_t performance_now_ns(uint64_t frequency)
{
    const uint64_t counter = SDL_GetPerformanceCounter();
    const uint64_t seconds = counter / frequency;
    const uint64_t remainder = counter % frequency;

    return seconds * UINT64_C(1000000000) + remainder * UINT64_C(1000000000) / frequency;
}

static int prepare_ghost_playback(PortableRecoveredTimeTrialHostScene *scene,
                                  const uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE],
                                  PortableTimeTrialGhostPlayback *playback,
                                  const PortableCharacterAssetView **character)
{
    const PortableRecoveredRaceView *race = NULL;
    const uint8_t driver = portable_time_trial_ghost_record_driver(record);

    if (scene == NULL || scene->ghost_prepare == NULL || character == NULL ||
        !scene->ghost_prepare(scene->ghost_user, driver,
            portable_time_trial_ghost_record_roster_driver(record), &race, character) ||
        race == NULL || portable_time_trial_ghost_playback_init(playback, race, record) !=
            PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK)
        return 0;
    PortableCharacterContent content;
    scene->presentation.ghost_character_id = 0u;
    if (portable_content_registry_saved_driver(scene->frontend_pack, driver,
            portable_time_trial_ghost_record_roster_driver(record), &content) == PORTABLE_CONTENT_OK)
        scene->presentation.ghost_character_id = content.id;
    return 1;
}

static void set_time_trial_window_title(SDL_Window *window, uint32_t ghost_ticks,
                                        uint8_t ghost_driver)
{
    char title[256];
    if (window == NULL)
        return;
    if (ghost_ticks != 0u)
        snprintf(title, sizeof title,
                 "MKSC Time Trial | ghost D%u %u ticks | arrows drive | "
                 "Shift hop | C item | Tab horn | P pause | R restart | X rescue | "
                 "Esc courses",
                 (unsigned)ghost_driver, (unsigned)ghost_ticks);
    else
        snprintf(title, sizeof title,
                 "MKSC Time Trial | no saved ghost | arrows drive | "
                 "Shift hop | C item | Tab horn | P pause | R restart | X rescue | "
                 "Esc courses");
    SDL_SetWindowTitle(window, title);
}

static uint16_t sampled_buttons(SDL_Gamepad *controller)
{
    return portable_sdl_keyboard_driving() | portable_sdl_gamepad_driving(controller);
}

static uint16_t sampled_pause_buttons(SDL_Gamepad *controller)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    uint16_t buttons = 0u;
    if (keys[SDL_SCANCODE_LEFT])
        buttons |= PORTABLE_FRONTEND_BUTTON_LEFT;
    if (keys[SDL_SCANCODE_RIGHT])
        buttons |= PORTABLE_FRONTEND_BUTTON_RIGHT;
    if (keys[SDL_SCANCODE_UP])
        buttons |= PORTABLE_FRONTEND_BUTTON_UP;
    if (keys[SDL_SCANCODE_DOWN])
        buttons |= PORTABLE_FRONTEND_BUTTON_DOWN;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_Z])
        buttons |= PORTABLE_FRONTEND_BUTTON_A;
    if (keys[SDL_SCANCODE_X])
        buttons |= PORTABLE_FRONTEND_BUTTON_B;
    if (keys[SDL_SCANCODE_TAB])
        buttons |= PORTABLE_FRONTEND_BUTTON_SELECT;
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_P])
        buttons |= PORTABLE_FRONTEND_BUTTON_START;
    buttons |= portable_sdl_gamepad_frontend(controller);
    return buttons;
}

static uint16_t pause_key_button(SDL_Scancode key)
{
    switch (key)
    {
    case SDL_SCANCODE_LEFT:
        return PORTABLE_FRONTEND_BUTTON_LEFT;
    case SDL_SCANCODE_RIGHT:
        return PORTABLE_FRONTEND_BUTTON_RIGHT;
    case SDL_SCANCODE_UP:
        return PORTABLE_FRONTEND_BUTTON_UP;
    case SDL_SCANCODE_DOWN:
        return PORTABLE_FRONTEND_BUTTON_DOWN;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_Z:
        return PORTABLE_FRONTEND_BUTTON_A;
    case SDL_SCANCODE_X:
        return PORTABLE_FRONTEND_BUTTON_B;
    case SDL_SCANCODE_TAB:
        return PORTABLE_FRONTEND_BUTTON_SELECT;
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_P:
        return PORTABLE_FRONTEND_BUTTON_START;
    default:
        return 0u;
    }
}

static void record_action(FILE *recording, PortableRecoveredReplayAction action, int value,
                          int *error)
{
    if (recording != NULL && !portable_recovered_replay_write_action_file(recording, action, value))
        *error = 1;
}

static int write_telemetry(const PortableRecoveredTimeTrialHostScene *scene,
                           const PortableRecoveredTimeTrialSnapshot *snapshot)
{
    char temporary[4096];
    FILE *file;
    const PortableRacePoint target = scene->race->rules.route[snapshot->next_route_index];

    if (scene->telemetry_path == NULL)
        return 1;
    if (snprintf(temporary, sizeof temporary, "%s.tmp", scene->telemetry_path) >=
            (int)sizeof temporary ||
        (file = fopen(temporary, "w")) == NULL)
        return 0;
    if (fprintf(file, "%u %d %d %u %d %d %d %u %u %u %u %u %u\n", (unsigned)snapshot->phase,
                snapshot->position_x_q16, snapshot->position_y_q16, (unsigned)snapshot->heading,
                snapshot->speed_q16, target.x, target.y, (unsigned)snapshot->recovery_phase,
                snapshot->rescue_count, snapshot->laps_completed,
                (unsigned)snapshot->mushrooms_remaining, (unsigned)snapshot->mushroom_active,
                (unsigned)snapshot->mushroom_timer) < 0 ||
        fflush(file) != 0 || fclose(file) != 0 ||
        !portable_file_replace(temporary, scene->telemetry_path))
        return 0;
    return 1;
}

#endif

static int render_trial_speedometer(const PortableRecoveredTimeTrialHostScene *scene,
                                    const PortableRecoveredTimeTrialSnapshot *snapshot,
                                    PortableSoftwareFrame *frame)
{
    PortableAssetSectionView dial_section, needle_section;
    if (!scene->frontend_pack)
        return 1;
    if (portable_asset_pack_find_section(scene->frontend_pack,
                                         PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS, 0x27041u,
                                         &dial_section) != PORTABLE_ASSET_PACK_OK ||
        portable_asset_pack_find_section(scene->frontend_pack,
                                         PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS, 0x27042u,
                                         &needle_section) != PORTABLE_ASSET_PACK_OK)
        return 1; /* Optional old-pack presentation. */
    PortableNativeImage dial, needle;
    return portable_native_image_open(dial_section.bytes, dial_section.size, &dial) &&
           portable_native_image_open(needle_section.bytes, needle_section.size, &needle) &&
           portable_hud_render_speedometer(&dial, &needle, snapshot->speed_q16,
                                           scene->presentation.sine, scene->presentation.sine_count,
                                           frame);
}

static int render_time_trial_with_board(const PortableRecoveredTimeTrialHostScene *scene,
                                        const PortableRecoveredTimeTrialSnapshot *snapshot,
                                        const PortableRecoveredTimeTrialSnapshot *ghost_snapshot,
                                        const PortableCharacterAssetView *ghost_character,
                                        PortableSoftwareFrame *frame, uint8_t pause_cursor,
                                        uint8_t pause_open_ticks, uint16_t pause_palette_clock,
                                        uint8_t board_visible, PortableLapBoardHud *capture_hud,
                                        const PortablePauseGraphics *pause_graphics)
{
    const PortableCharacterViewInput character_view = {
        .selected = 1u,
        .source_flags = snapshot != NULL ? snapshot->source_flags : 0u,
        .camera_yaw = snapshot != NULL ? snapshot->camera_yaw : 0u,
        .driver_angle = snapshot != NULL ? snapshot->driver_angle : 0u,
        .steering_response_q8 = snapshot != NULL ? snapshot->steering_response_q8 : 0,
        .speed_q16 = snapshot != NULL ? snapshot->speed_q16 : 0,
        .position_82 = snapshot != NULL ? snapshot->steering_position_82 : 0u,
        .camera_adjustment = snapshot != NULL ? snapshot->camera_adjustment : 0u,
    };
    PortableRaceSceneView presentation;
    PortableRacePresentationState state = {
        .course_props = snapshot->course_props,
        .lava_bubbles = snapshot->lava_bubbles,
        .thwomps = snapshot->thwomps,
        .crabs = snapshot->crabs,
        .seagulls = snapshot->seagulls,
        .piranhas = snapshot->piranhas,
        .snowmen = snapshot->snowmen,
        .mice = snapshot->mice,
        .thunderclouds = snapshot->thunderclouds,
        .falling_stars = snapshot->falling_stars,
        .penguins = snapshot->penguins,
        .boos = snapshot->boos,
        .cannonballs = snapshot->cannonballs,
        .flattened = snapshot->flattened,
        .art_motion = snapshot->art_motion,
        .sky_motion = snapshot->sky_motion,
        .ambient = snapshot->ambient,
        .snow_weather = snapshot->snow_weather,
        .ambient_rules = scene->race != NULL ? scene->race->ambient_rules : NULL,
        .driver_affine_x = snapshot->affine.inverse_x_q8,
        .driver_affine_y = snapshot->affine.inverse_y_q8,
        .character_affine_scale = snapshot->lightning_timer ? 384u : 256u,
        .palette_bank = snapshot->palette_bank,
        .position_x_q16 = snapshot->position_x_q16,
        .position_y_q16 = snapshot->position_y_q16,
        .height_q16 = snapshot->height_q16,
        .heading = snapshot->heading,
        .camera_yaw = snapshot->camera_yaw,
        .character_view_angle =
            portable_character_view_pose(&character_view,
                                         scene->race != NULL ? scene->race->view_rules : NULL)
                .angle,
        .art_pose = portable_character_view_pose(
            &character_view, scene->race != NULL ? scene->race->view_rules : NULL),
        .art_pose_valid = 1u,
        .ghost_position_x_q16 = ghost_snapshot != NULL ? ghost_snapshot->position_x_q16 : 0,
        .ghost_position_y_q16 = ghost_snapshot != NULL ? ghost_snapshot->position_y_q16 : 0,
        .ghost_height_q16 = ghost_snapshot != NULL ? ghost_snapshot->height_q16 : 0,
        .ghost_affine_x = ghost_snapshot != NULL ? ghost_snapshot->affine.inverse_x_q8 : 0u,
        .ghost_affine_y = ghost_snapshot != NULL ? ghost_snapshot->affine.inverse_y_q8 : 0u,
        .ghost_affine_scale =
            ghost_snapshot != NULL && ghost_snapshot->lightning_timer ? 384u : 256u,
        .ghost_flattened = ghost_snapshot != NULL ? ghost_snapshot->flattened : 0u,
        .ghost_palette_bank = ghost_snapshot != NULL ? ghost_snapshot->palette_bank : 0u,
        .ghost_art_motion =
            ghost_snapshot != NULL ? ghost_snapshot->art_motion : (PortableDriverArtMotion){0},
        .ghost_visible = (uint8_t)(ghost_snapshot != NULL && ghost_character != NULL &&
                                   !ghost_snapshot->driver_hidden),
        .charged_particle_age = snapshot->charged_particle_age,
        .modern_drift_enabled = snapshot->modern_drift_enabled,
        .modern_drift = snapshot->modern_drift,
        .modern_air_enabled = snapshot->modern_air_enabled,
        .modern_air = snapshot->modern_air,
        .boost_particle_age = snapshot->boost_particle_age,
        .boost_particle_birth_height_q16 = snapshot->boost_particle_birth_height_q16,
        .phase = snapshot->phase,
        .laps_completed = snapshot->lap_hud.initialized
                              ? (uint16_t)(snapshot->lap_hud.displayed_lap - 1u)
                              : snapshot->laps_completed,
        .lap_count = snapshot->lap_count,
        .countdown_remaining = snapshot->countdown_remaining,
        .next_route_index = snapshot->next_route_index,
        .total_ticks = snapshot->total_ticks,
        .marker_clock = snapshot->marker_clock,
        .marker_source_flags = snapshot->source_flags,
        .ghost_marker_source_flags = ghost_snapshot ? ghost_snapshot->source_flags : 0u,
        .paused = snapshot->paused,
        .pause_open_ticks = pause_open_ticks,
        .recovery_phase = snapshot->recovery_phase,
        .recovery_phase_ticks = snapshot->recovery_phase_ticks,
        .suppress_player = snapshot->driver_hidden,
        .show_time_trial_items = 1u,
        .mushrooms_remaining = snapshot->mushrooms_remaining,
    };
    if (snapshot->phase == PORTABLE_RACE_PHASE_FINISHED && snapshot->lap_count > 0u &&
        snapshot->lap_count <= PORTABLE_RACE_MAX_LAPS)
    {
        const uint64_t cs = (uint64_t)snapshot->lap_ticks[snapshot->lap_count - 1u] *
                            PORTABLE_FRAME_TICK_NUMERATOR * 100u / PORTABLE_FRAME_TICK_DENOMINATOR;
        state.result_clock_mode = 2u;
        state.result_lap_centiseconds = (uint16_t)(cs > UINT16_C(0xea5f) ? UINT16_C(0xea5f) : cs);
    }
    if (snapshot->finish_camera.snapshot.active)
    {
        state.camera_position_override = 1u;
        state.camera_position_x_q16 = snapshot->finish_camera.snapshot.x_q16;
        state.camera_position_y_q16 = snapshot->finish_camera.snapshot.y_q16;
        state.source_sky_parallax = 1u;
    }
    memcpy(state.boo_attacks, snapshot->boo_attacks, sizeof state.boo_attacks);
    state.lava_smoke = snapshot->lava_smoke;
    state.recovery_fx = snapshot->recovery_fx;
    state.surface_trails = snapshot->surface_trails;
    state.impact_effects = snapshot->impact_effects;
    state.entry_splash = snapshot->entry_splash;

    if (ghost_snapshot != NULL)
    {
        const PortableCharacterViewInput ghost_view = {
            .camera_yaw = snapshot->camera_yaw,
            .driver_angle = ghost_snapshot->driver_angle,
            .steering_response_q8 = ghost_snapshot->steering_response_q8,
            .speed_q16 = ghost_snapshot->speed_q16,
            .position_82 = ghost_snapshot->steering_position_82,
            .camera_adjustment = ghost_snapshot->camera_adjustment,
        };
        state.ghost_art_pose = portable_character_view_pose(
            &ghost_view, scene->race != NULL ? scene->race->view_rules : NULL);
        state.ghost_visual_yaw=portable_character_visual_yaw(
            &ghost_view,scene->race!=NULL?scene->race->view_rules:NULL);
        state.ghost_art_pose_valid = 1u;
        state.ghost_character_view_angle = state.ghost_art_pose.angle;
    }

    presentation = scene->presentation;
    state.visual_yaw=portable_character_visual_yaw(&character_view,
        scene->race!=NULL?scene->race->view_rules:NULL);
    presentation.ghost_character = ghost_character;
    if (capture_hud &&
        !portable_race_scene_capture_lap_board_hud(&presentation, &state, capture_hud))
        return 0;
    state.result_board_visible = (uint8_t)(board_visible != 0u);
    state.suppress_native_hud = (uint8_t)(board_visible > 1u);
    if (!portable_race_scene_render(&presentation, &state, frame))
        return 0;
    if(state.modern_air_enabled && !portable_screen_capture_air(frame,"tt",&state.modern_air))
        return 0;
    if(state.modern_drift_enabled && !portable_screen_capture_drift(frame,"tt",&state.modern_drift))
        return 0;
    if (snapshot->phase != PORTABLE_RACE_PHASE_FINISHED &&
        (!snapshot->paused || pause_open_ticks <= 2u) &&
        !render_trial_speedometer(scene, snapshot, frame))
        return 0;
    PortableSoftwareFrame centered =
        portable_software_frame_native_view(frame, PORTABLE_FRAME_CENTER);
    frame = &centered;
    if (snapshot->lakitu_visible &&
        portable_frontend_image_draw_scaled(
            scene->frontend_pack, portable_gp_lakitu_image(snapshot->lakitu_art_bank), frame,
            snapshot->lakitu_x, snapshot->lakitu_y, 0, 0, (int)frame->width, (int)frame->height,
            snapshot->lakitu_scale_q8) != PORTABLE_FRONTEND_PRESENTATION_OK)
        return 0;
    if (!state.paused &&
        !portable_gp_countdown_render(scene->frontend_pack, snapshot->phase,
                                      scene->race->rules.countdown_ticks,
                                      snapshot->countdown_remaining, snapshot->total_ticks, frame))
        return 0;
    if (state.paused && !scene->modern_menus)
    {
        PortablePauseMenu menu;
        portable_pause_menu_init(&menu, 0u);
        menu.cursor = pause_cursor;
        menu.open_ticks = pause_open_ticks;
        menu.palette_clock = pause_palette_clock;
        if (pause_graphics != NULL)
            menu.graphics = *pause_graphics;
        const PortableMenuPaletteProfile profile =
            portable_gp_results_menu_profile(scene->lap_board);
        if (portable_pause_menu_render_menu_palette(scene->frontend_pack, &menu, &profile, frame) !=
            PORTABLE_FRONTEND_PRESENTATION_OK)
            return 0;
    }
    return 1;
}

int portable_recovered_time_trial_render_with_ghost(
    const PortableRecoveredTimeTrialHostScene *scene,
    const PortableRecoveredTimeTrialSnapshot *snapshot,
    const PortableRecoveredTimeTrialSnapshot *ghost_snapshot,
    const PortableCharacterAssetView *ghost_character, PortableSoftwareFrame *frame,
    uint8_t pause_cursor, uint8_t pause_open_ticks)
{
    return render_time_trial_with_board(scene, snapshot, ghost_snapshot, ghost_character, frame,
                                        pause_cursor, pause_open_ticks, 0u, 0u, NULL, NULL);
}

#ifdef PORTABLE_ENABLE_SDL3
struct PortableTimeTrialHost
{
    PortableRecoveredTimeTrialSession session;
    PortableRecoveredTimeTrialSnapshot snapshot;
    PortableRecoveredTimeTrialSnapshot previous_snapshot, render_snapshot;
    PortableRecoveredTimeTrialSnapshot previous_ghost, render_ghost;
    uint8_t history_ready;
    uint64_t last_present_ns;
    PortableFrameScheduler scheduler;
    PortableFrameScheduler presentation_scheduler;
    PortablePauseMenu pause_menu;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture, *render_texture;
    uint32_t *render_pixels;
    SDL_Gamepad *controller;
    uint32_t pixels[240u * 160u];
    PortableSoftwareFrame frame;
    uint8_t race_frame_captured;
    PortableRumbleTracker rumble;
    uint16_t display_buttons;
    PortableUiTextBank ui_text;
    uint64_t performance_frequency;
    char best_path[4096];
    char ghost_path[4096];
    char preference_directory[4096];
    PortableGhostLibraryInventory ghost_inventory;
    PortableGhostSaveMenu ghost_save;
    uint8_t saved_ghost[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    uint32_t ghost_ticks;
    uint8_t ghost_driver;
    PortableTimeTrialGhostPlayback ghost_playback;
    PortableRecoveredTimeTrialSnapshot ghost_snapshot;
    const PortableCharacterAssetView *ghost_character;
    int ghost_active;
    PortableTimeTrialGhostPlayback result_replay;
    PortableRecoveredTimeTrialSnapshot replay_snapshot, previous_replay;
    uint8_t replay_active, replay_unavailable;
    uint64_t aid_now;
    uint32_t aid_frames, aid_results_frames, modern_result_ticks;
    uint8_t aid_save_ghost, aid_ghost_saved;
    uint16_t aid_ghost_frames;
    uint16_t aid_control_keys;
    uint8_t aid_control_interval, aid_control_remaining;
    uint8_t aid_manual_rescue_done;
    enum
    {
        TT_PAUSE_AID_WAIT,
        TT_PAUSE_AID_RELEASE_PAUSE,
        TT_PAUSE_AID_CAPTURE,
        TT_PAUSE_AID_GRAPHICS,
        TT_PAUSE_AID_RELEASE_CONTINUE,
        TT_PAUSE_AID_DONE
    } pause_aid_phase;
    uint32_t best_ticks;
    PortablePersonalBestTable records;
    uint8_t finish_audio_ready, finish_audio_record;
    uint32_t recorded_ticks;
    FILE *recording;
    int new_best;
    int finish_saved;
    /* 801E248's lap board over the finish camera. */
    PortableGpResultsFlow board_flow, board_display;
    uint16_t board_times[PORTABLE_RACE_MAX_LAPS + 1u];
    uint8_t board_visible;
    PortableGpResultsRecord result_records[6];
    PortableGpResultsAssetView result_board;
    uint8_t result_tiles[PORTABLE_GP_RESULTS_TILE_BYTES];
    PortableNativeImage result_portraits[6], result_names[6];
    uint8_t ghost_save_visible, result_action_done, source_results;
    uint8_t result_emphasis[6];
    uint16_t result_held, result_repeat_ticks;
    PortableLapBoardHud board_hud;
    uint8_t board_hud_ready;
    uint32_t finish_scene_clock;
    int pause_captured;
    unsigned aid_graphics_step, aid_graphics_wait; /* MKSC_AID_PAUSE_GRAPHICS route position. */
    uint8_t aid_graphics_held, pause_graphics_checked;
    int focused;
    int error;
    int quit;
    int borrowed_surface;
    int owns_sdl;
    uint8_t mobile_pause_cancel,mobile_results_back,aid_mobile_results_stage;
    uint16_t mobile_pressed;
    uint16_t prelude_remaining;
    uint16_t previous_pause_buttons;
    uint16_t event_pause_buttons;
    int pause_menu_active;
    ModernMenuHost modern; /* Modern pause overlay. */
    PortableRecoveredTimeTrialHostScene scene;
    PortableScene3D scene_3d;
    PortableModelCatalog models;
    PortableRecoveredTimeTrialHostOutcome outcome;
    int needs_present;
};

static uint64_t time_trial_host_now(const PortableTimeTrialHost *host)
{
    return host->scene.input_aid ? host->aid_now : performance_now_ns(host->performance_frequency);
}

/* Mod scripts start over with every attempt, like the race itself. */
static int restart_scripts(PortableTimeTrialHost *host)
{
    return portable_script_host_begin_time_trial(host->scene.scripts, &host->session,
                                                 host->scene.race->rules.compatibility.course_id);
}

static void stop_result_replay(PortableTimeTrialHost *host)
{
    if (host->replay_active)
    {
        host->session.marker_clock = host->result_replay.session.marker_clock;
        host->session.marker_display_clock = host->result_replay.session.marker_display_clock;
        host->snapshot.marker_clock = host->replay_snapshot.marker_clock;
    }
    if (host->replay_active && host->scene.audio_result_replay != NULL &&
        !host->scene.audio_result_replay(host->scene.audio_user, 0))
        host->error = 1;
    host->replay_active = 0u;
    host->history_ready = 0u;
    host->needs_present = 1;
    host->aid_results_frames = 0u;
    portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host), 0);
}

static void start_result_replay(PortableTimeTrialHost *host)
{
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    if (!portable_recovered_time_trial_ghost_candidate(&host->session, record))
    {
        host->replay_unavailable = 1u;
        host->needs_present = 1;
        return;
    }
    if (portable_time_trial_ghost_playback_init(&host->result_replay, host->scene.race, record) !=
        PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK)
    {
        host->error = 1;
        return;
    }
    host->result_replay.session.marker_clock = host->session.marker_clock;
    host->result_replay.session.marker_display_clock = host->session.marker_display_clock;
    if (portable_recovered_time_trial_snapshot(&host->result_replay.session,
                                               &host->replay_snapshot) !=
        PORTABLE_RECOVERED_TIME_TRIAL_OK)
    {
        fputs("finished Time Trial replay initialization failed\n", stderr);
        host->error = 1;
        return;
    }
    host->replay_active = 1u;
    host->history_ready = 0u;
    host->needs_present = 1;
    host->replay_unavailable = 0u;
    if (host->scene.audio_result_replay != NULL)
    {
        if (!host->scene.audio_result_replay(host->scene.audio_user, 1))
            host->error = 1;
    }
    else if (host->scene.audio_restart != NULL &&
             !host->scene.audio_restart(host->scene.audio_user))
        host->error = 1;
    portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host), host->focused);
}

static void prepare_ghost_save(PortableTimeTrialHost *host)
{
    uint8_t record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    host->ghost_save = (PortableGhostSaveMenu){.status = PORTABLE_GHOST_SAVE_UNAVAILABLE};
    if (!portable_time_trial_result_is_valid(&host->snapshot) ||
        !portable_recovered_time_trial_ghost_candidate(&host->session, record))
    {
        host->replay_unavailable = 1u;
        return;
    }
    host->replay_unavailable = 0u;
    const PortableGhostLibraryStatus status = portable_time_trial_ghost_library_query(
        host->preference_directory, host->snapshot.compatibility.course_id, &host->ghost_inventory);
    host->ghost_save.used_slots = host->ghost_inventory.used;
    host->ghost_save.replaces_course = (uint8_t)(status == PORTABLE_GHOST_LIBRARY_REUSE);
    host->ghost_save.status = status == PORTABLE_GHOST_LIBRARY_IO_ERROR ? PORTABLE_GHOST_SAVE_FAILED
                              : status == PORTABLE_GHOST_LIBRARY_FULL   ? PORTABLE_GHOST_SAVE_FULL
                                                                        : PORTABLE_GHOST_SAVE_READY;
}

static void handle_ghost_save(PortableTimeTrialHost *host, uint16_t triggered)
{
    if (!host->ghost_save.prompt && !host->ghost_save.full_list &&
        host->ghost_save.status != PORTABLE_GHOST_SAVE_SAVED &&
        (triggered & PORTABLE_FRONTEND_BUTTON_A))
        prepare_ghost_save(host);
    const PortableGhostSaveAction action =
        portable_time_trial_ghost_save_tick(&host->ghost_save, triggered);
    if (action == PORTABLE_GHOST_SAVE_ACTION_BACK)
    {
        host->outcome.return_to_selection = 1;
        host->outcome.return_destination = PORTABLE_TIME_TRIAL_RETURN_COURSE;
        host->quit = 1;
    }
    else if (action == PORTABLE_GHOST_SAVE_ACTION_ERASE)
    {
        const uint32_t course = host->ghost_inventory.courses[host->ghost_save.erase_cursor];
        const int erased =
            portable_time_trial_records_erase_ghost(host->preference_directory, course);
        if (erased)
            prepare_ghost_save(host);
        else
            host->ghost_save.erase_failed = 1u;
    }
    else if (action == PORTABLE_GHOST_SAVE_ACTION_WRITE)
    {
        uint8_t candidate[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
        if (!portable_recovered_time_trial_ghost_candidate(&host->session, candidate) ||
            portable_time_trial_ghost_library_save(
                host->preference_directory, &host->snapshot.compatibility, candidate,
                host->saved_ghost) != PORTABLE_TIME_TRIAL_GHOST_STORE_OK)
        {
            host->ghost_save.status = PORTABLE_GHOST_SAVE_FAILED;
            fprintf(stderr, "course=%u mode=Time Trial state=ghost save failed\n",
                    host->snapshot.compatibility.course_id);
            return;
        }
        host->ghost_save.status = PORTABLE_GHOST_SAVE_SAVED;
        host->ghost_ticks = portable_time_trial_ghost_record_time(host->saved_ghost);
        host->ghost_driver = portable_time_trial_ghost_record_driver(host->saved_ghost);
        host->ghost_character = NULL;
        host->ghost_active = prepare_ghost_playback(&host->scene, host->saved_ghost,
                                                    &host->ghost_playback, &host->ghost_character);
        set_time_trial_window_title(host->window, host->ghost_ticks, host->ghost_driver);
    }
}

static uint16_t board_centiseconds(uint32_t ticks)
{
    const uint64_t value =
        (uint64_t)ticks * PORTABLE_FRAME_TICK_NUMERATOR * 100u / PORTABLE_FRAME_TICK_DENOMINATOR;
    return (uint16_t)(value > UINT16_C(0xea5f) ? UINT16_C(0xea5f) : value);
}

static void trial_result_records(const PortableTimeTrialHost *host, PortableGpResultsRecord out[6])
{
    PortableGpResultsRecord actual[6];
    for (unsigned row = 0u; row < 6u; ++row)
    {
        const PortablePersonalBestRecord *record =
            row < 5u ? &host->records.races[row] : &host->records.best_lap;
        actual[row] =
            (PortableGpResultsRecord){.flags = record->ticks ? record->character_class : 0x10u,
                                      .time_centiseconds = board_centiseconds(record->ticks),
                                      .roster_driver_id = record->roster_driver_id};
    }
    portable_time_trial_results_merge(host->scene.race->time_trial_record_defaults, actual, out);
}

static void begin_lap_board(PortableTimeTrialHost *host)
{
    const unsigned laps = host->snapshot.lap_count;
    portable_gp_results_flow_init(&host->board_flow);
    host->board_visible = 0u;
    host->board_hud_ready = 0u;
    host->ghost_save_visible = 0u;
    host->result_action_done = 0u;
    host->result_held = host->result_repeat_ticks = 0u;
    host->aid_results_frames = 0u;
    host->modern_result_ticks = 0u;
    host->aid_ghost_frames = 0u;
    host->aid_ghost_saved = 0u;
    host->finish_scene_clock = host->session.scene_clock;
    memset(host->board_times, 0, sizeof host->board_times);
    for (unsigned lap = 0u; lap < laps && lap < PORTABLE_RACE_MAX_LAPS; ++lap)
        host->board_times[lap] = board_centiseconds(host->snapshot.lap_ticks[lap]);
    if (laps <= PORTABLE_RACE_MAX_LAPS)
        host->board_times[laps] = board_centiseconds(host->snapshot.total_ticks);
}

/* 22704 advances on accepted presentation ticks, never render cadence. */
static void tick_lap_board(PortableTimeTrialHost *host, uint16_t buttons, uint16_t triggered)
{
    PortableGpResultsFlow *flow = &host->board_flow;
    uint16_t repeated = triggered;
    const uint16_t direction = buttons & UINT16_C(0x00c0);
    if (direction != host->result_held)
    {
        host->result_held = direction;
        host->result_repeat_ticks = 0u;
    }
    else if (direction && ++host->result_repeat_ticks >= PORTABLE_FRONTEND_REPEAT_DELAY &&
             (host->result_repeat_ticks - PORTABLE_FRONTEND_REPEAT_DELAY) %
                     PORTABLE_FRONTEND_REPEAT_PERIOD ==
                 0u)
        repeated |= direction;
    if (!host->source_results && flow->state == 3u)
    {
        ++flow->total_ticks;
        if (flow->total_ticks > 540u)
            host->ghost_save_visible = 1u;
        return;
    }
    if (portable_time_trial_results_tick(
            flow, (uint8_t)host->snapshot.lap_count, host->result_records,
            host->result_board.record_identities, host->result_board.menu_option_codes, repeated,
            triggered, (uint8_t)host->replay_unavailable) != PORTABLE_GP_RESULTS_OK)
    {
        host->error = 1;
        return;
    }
    if (flow->reveal_pending)
    {
        host->board_display = *flow;
        host->board_visible = 1u;
    }
    if (flow->menu_navigation_song_pending && host->scene.audio_cue)
        if (!host->scene.audio_cue(host->scene.audio_user, flow->menu_navigation_song_id))
            host->error = 1;
    if (flow->menu_confirm_song_pending && host->scene.audio_cue)
        if (!host->scene.audio_cue(host->scene.audio_user, flow->menu_confirm_song_id))
            host->error = 1;
    host->needs_present = 1;
}

static int render_trial_results(PortableTimeTrialHost *host)
{
    if (host->scene.modern_menus) return 1;
    const PortableGpResultsFlow *flow = &host->board_flow;
    const PortableGpResultsAssetView *art = &host->result_board;
    PortableSoftwareFrame centered =
        portable_software_frame_native_view(&host->frame, PORTABLE_FRAME_CENTER);
    if (flow->state <= 3u)
    {
        if (!host->board_visible)
            return 1;
        if (!host->board_hud_ready ||
            !portable_gp_results_render_laps_with_hud(art, &host->board_display, host->board_times,
                                                      (uint8_t)host->snapshot.lap_count,
                                                      &host->board_hud, &host->frame))
            return 0;
        return host->board_display.reveal_progress != 8u ||
               portable_screen_capture(&host->frame, "tt-finish");
    }
    if (flow->state == 4u)
    {
        if (!flow->exit_wipe_pending)
            return 1;
        return portable_gp_results_render_exit_wipe(
            art, flow, host->board_times, (uint8_t)host->snapshot.lap_count, 0u, &centered);
    }
    if (flow->state <= 6u)
        return 1; /* cleared BG0 while the record map is prepared */
    if (flow->state == 7u && flow->record_slide_pending)
        return portable_gp_results_render_record_slide(art, flow, &centered) &&
               (flow->record_slide_progress < 32u ||
                portable_screen_capture(&host->frame, "tt-record-slide"));
    if (flow->state == 8u && flow->menu_reveal_pending)
        return portable_gp_results_render_menu_reveal(art, flow, &centered);
    /* Setup turns retain the last completed map. The record commands are
     * immutable after source case6; menu cursor/palette remain fixed-tick. */
    PortableGpResultsFlow display = *flow;
    display.record_rows_pending = 1u;
    display.record_row_count = 6u;
    if (flow->state == 7u || flow->state == 8u)
    {
        display.record_slide_pending = display.map_dirty_pending = 1u;
        display.record_slide_progress = 59u;
        display.slide_band_count = 9u;
        static const uint8_t rows[9] = {0u, 2u, 4u, 6u, 8u, 10u, 12u, 15u, 17u};
        for (unsigned i = 0u; i < 9u; ++i)
            display.slide_bands[i] = (PortableGpResultsSlideBandCommand){rows[i], 2u, 0u};
        return portable_gp_results_render_record_slide(art, &display, &centered);
    }
    display.map_dirty_pending = 1u;
    if (flow->state >= 11u)
    {
        if (!portable_gp_results_render_menu_selection(
                art, &display, host->replay_unavailable ? 2u : 0u, flow->total_ticks, &centered))
            return 0;
        return portable_screen_capture(&host->frame, "tt-results-menu");
    }
    return portable_gp_results_render_record_setup(art, &display, &centered);
}

static int render_ghost_save(PortableTimeTrialHost *host)
{
    if (host->scene.modern_menus) return 1;
    PortableSoftwareFrame centered =
        portable_software_frame_native_view(&host->frame, PORTABLE_FRAME_CENTER);
    PortableSoftwareFrame *frame = &centered;
    const PortableAssetPackView *pack = host->scene.frontend_pack;
    const PortableGhostSaveMenu *menu = &host->ghost_save;
    if (!host->ghost_save_visible)
        return 1;
    const char *status = menu->status == PORTABLE_GHOST_SAVE_UNAVAILABLE
                             ? "GHOST RECORDING UNAVAILABLE"
                         : menu->status == PORTABLE_GHOST_SAVE_FULL   ? "A: FREE A GHOST SLOT"
                         : menu->status == PORTABLE_GHOST_SAVE_FAILED ? "SAVE FAILED - A: RETRY"
                         : menu->status == PORTABLE_GHOST_SAVE_SAVED  ? "GHOST SAVED"
                                                                      : "A: SAVE GHOST";
    char best[32];
    const uint16_t best_cs = host->scene.race->time_trial_record_defaults != NULL
                                 ? host->result_records[0].time_centiseconds
                                 : board_centiseconds(host->best_ticks);
    snprintf(best, sizeof best, "%s %u'%02u\"%02u", host->new_best ? "NEW BEST" : "BEST",
             best_cs / 6000u, best_cs / 100u % 60u, best_cs % 100u);
    /* Native-only notices: the sub_8005590 window and font. */
    if (portable_menu_panel(pack, frame, 0, 96, 240u, 64u) != PORTABLE_FRONTEND_PRESENTATION_OK)
        return 0;
    (void)portable_menu_text(pack, frame, 8, 104, best);
    (void)portable_menu_text(pack, frame, 8, 120, status);
    (void)portable_menu_text(pack, frame, 8, 136,
                             host->replay_unavailable ? "REPLAY RECORDING UNAVAILABLE"
                                                      : "B: CLOSE  SELECT: SAVE GHOST");
    const char *screen =
        host->board_visible && host->board_display.reveal_progress == 8u ? "tt-finish" : NULL;
    if (menu->full_list)
    {
        if (portable_menu_panel(pack, frame, 0, 8, 240u, 88u) != PORTABLE_FRONTEND_PRESENTATION_OK)
            return 0;
        (void)portable_menu_text(pack, frame, 8, 16, "GHOST STORAGE FULL");
        const uint32_t course = host->ghost_inventory.courses[menu->erase_cursor];
        PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY];
        PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY];
        PortableFrontendConfig config;
        if (!build_frontend_config(pack, drivers, courses, &config))
            return 0;
        size_t index = 0u;
        while (index < config.course_count && courses[index].content_id != course)
            ++index;
        PortableFrontendPresentationStatus drawn = PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
        if (index < config.course_count)
        {
            drawn = portable_frontend_image_draw_scaled(
                pack, PORTABLE_COURSE_NAME + courses[index].cup * 4u + courses[index].track, frame,
                20, 50, 0, 0, 240, 160, 256u);
            if (drawn != PORTABLE_FRONTEND_PRESENTATION_OK &&
                (drawn != PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION ||
                 portable_track_set_of_cup(courses[index].cup) != PORTABLE_TRACK_SET_CUSTOM))
                return 0;
        }
        if (drawn != PORTABLE_FRONTEND_PRESENTATION_OK)
        {
            char name[40];
            snprintf(name, sizeof name, "COURSE %u", course);
            (void)portable_menu_text(pack, frame, 24, 50, name);
        }
        char slots[48];
        snprintf(slots, sizeof slots, "%u/%u  UP/DOWN: CHOOSE", menu->erase_cursor + 1u,
                 menu->used_slots < PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS
                     ? menu->used_slots
                     : PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS);
        (void)portable_menu_text(pack, frame, 8, 32, slots);
        (void)portable_menu_text(pack, frame, 8, 72,
                                 menu->erase_failed ? "ERASE FAILED - A: RETRY"
                                                    : "A: ERASE GHOST  B: BACK");
        screen = "tt-ghost-full";
    }
    if (menu->prompt)
    {
        if (portable_menu_panel(pack, frame, 0, 40, 240u, 64u) != PORTABLE_FRONTEND_PRESENTATION_OK)
            return 0;
        const char *question = menu->full_list         ? "ERASE GHOST? TIMES ARE KEPT."
                               : menu->replaces_course ? "REPLACE THIS COURSE'S GHOST?"
                                                       : "SAVE THIS GHOST?";
        (void)portable_menu_text_centered(pack, frame, 120, 48, question);
        (void)portable_menu_text_centered(pack, frame, 84, 72, "NO");
        (void)portable_menu_text_centered(pack, frame, 156, 72, "YES");
        if (portable_menu_frame(pack, frame, menu->yes ? 132 : 60, 64, 48u, 32u) !=
            PORTABLE_FRONTEND_PRESENTATION_OK)
            return 0;
        screen = "tt-ghost-prompt";
    }
    return screen == NULL || portable_screen_capture(&host->frame, screen);
}

static int present_modern_trial_results(PortableTimeTrialHost *host)
{
    const PortableGpResultsFlow *flow = &host->board_flow;
    ModernMenuResults view = {.pack = host->scene.frontend_pack,
        .course_id = host->snapshot.compatibility.course_id, .title = "Time Trial Results",
        .subtitle = "Lap Times", .tick = host->modern_result_ticks};
    char names[8][32];
    const int records = flow->state >= 7u;
    const unsigned laps = host->snapshot.lap_count;
    const unsigned rows = records ? 6u : laps + 1u;
    view.row_count = rows;
    if (rows > MODERN_MENU_RESULT_ROWS ||
        rows > sizeof host->result_records / sizeof host->result_records[0]) return 0;
    for (unsigned i = 0u; i < rows; ++i)
    {
        ModernMenuResultRow *row = &view.rows[i];
        const uint32_t cs = records ? host->result_records[i].time_centiseconds : host->board_times[i];
        modern_menu_host_format_time(row->value, sizeof row->value, cs);
        if (records)
        {
            PortableCharacterContent driver;
            const PortableGpResultsRecord *record = &host->result_records[i];
            const int target = (record->flags & 0x10u) != 0u;
            if (!target && portable_content_registry_saved_driver(view.pack, record->flags & 15u,
                    record->roster_driver_id, &driver) == PORTABLE_CONTENT_OK)
                row->driver_id = driver.id;
            if (target) row->name = i == 5u ? "Target Lap" : "Target Race";
            row->rank = i < 5u ? i + 1u : 0u;
            row->highlighted = record->emphasized;
            (void)snprintf(row->detail, sizeof row->detail, "%s", target ? "Default target" : i == 5u ? "Best lap" : "Best race");
        }
        else
        {
            if (i == laps) (void)snprintf(names[i], sizeof names[i], "Total");
            else (void)snprintf(names[i], sizeof names[i], "Lap %u", i + 1u);
            row->name = names[i];
            row->highlighted = (uint8_t)(i == laps);
        }
    }
    if (records) view.subtitle = "Personal Records";
    const char *capture = records ? "tt-records-modern" : "tt-laps-modern";
    if (flow->state >= 11u)
    {
        view.item_count = 5u;
        view.cursor = flow->menu_selection;
        for (unsigned i = 0u; i < 5u; ++i)
        {
            const uint8_t code = host->result_board.menu_option_codes[i];
            view.labels[i] = modern_menu_host_result_action_label(code);
            if (code == 3u && host->replay_unavailable) view.disabled_mask |= (uint8_t)(1u << i);
        }
        view.hint = "Up/Down: Choose   Enter: OK   Tab / Back: Save Ghost";
        capture = "tt-results-modern";
    }
    else view.hint = flow->state >= 4u ? "Enter / A: Continue" : "Showing lap times...";
    if (host->ghost_save_visible)
    {
        const PortableGhostSaveMenu *menu = &host->ghost_save;
        view.title = "Save Ghost";
        view.item_count = 0u;
        view.cursor = 0u;
        view.disabled_mask = 0u;
        view.notice = menu->status == PORTABLE_GHOST_SAVE_UNAVAILABLE ? "Ghost recording unavailable"
            : menu->status == PORTABLE_GHOST_SAVE_FAILED ? "Save failed. Confirm to retry."
            : menu->status == PORTABLE_GHOST_SAVE_SAVED ? "Ghost saved"
            : "Confirm to save this race's ghost.";
        view.hint = "Enter / A: Save   Esc / B: Close";
        capture = "tt-ghost-modern";
        if (menu->full_list)
        {
            view.subtitle = "Ghost Storage Full";
            view.notice = menu->erase_failed ? "Erase failed. Confirm to retry." : "Select a ghost to erase; its times are kept.";
            const unsigned count = menu->used_slots < PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS ? menu->used_slots : PORTABLE_TIME_TRIAL_LOCAL_GHOST_SLOTS;
            const unsigned selected = menu->erase_cursor;
            view.row_count = 1u;
            view.rows[0] = (ModernMenuResultRow){.name = names[0], .highlighted = 1u};
            (void)snprintf(names[0], sizeof names[0], "Course %u", host->ghost_inventory.courses[selected]);
            (void)snprintf(view.rows[0].value, sizeof view.rows[0].value, "%u / %u", selected + 1u, count);
            view.hint = "Up/Down: Choose   Enter / A: Erase   Esc / B: Back";
            capture = "tt-ghost-full-modern";
        }
        if (menu->prompt)
        {
            view.notice = menu->full_list ? "Erase this ghost? Times are kept." : menu->replaces_course ? "Replace this course's ghost?" : "Save this ghost?";
            view.item_count = 2u;
            view.labels[0] = "No"; view.labels[1] = "Yes";
            view.cursor = menu->yes ? 1u : 0u;
            view.hint = "Left/Right: Choose   Enter / A: OK   Esc / B: Back";
            capture = "tt-ghost-prompt-modern";
        }
    }
    return modern_menu_host_present_results(&host->modern, host->renderer, &host->frame,
        host->scene.fit_scaling, &view, capture);
}

static int trial_aid_graphics(PortableTimeTrialHost *host);

static int trial_resize_surface(PortableTimeTrialHost *host)
{
    const size_t width =
        portable_software_frame_window_width(host->renderer, host->scene.aspect_width);
    const uint8_t scale = host->scene.internal_scale > 1u ? host->scene.internal_scale : 1u;
    if (width == host->frame.width && host->frame.scale == scale &&
        (host->frame.scale == 1u || host->render_texture != NULL))
        return 1;
    PortableSoftwareFrame frame = host->frame;
    frame.width = width;
    frame.scale = scale;
    uint32_t *pixels = NULL;
    SDL_Texture *texture = NULL;
    if (width != 240u || frame.scale > 1u)
    {
        pixels = malloc(portable_software_frame_pixel_count(&frame) * sizeof(uint32_t));
        SDL_ScaleMode filter = SDL_SCALEMODE_NEAREST;
        if (!SDL_GetTextureScaleMode(host->texture, &filter))
            goto fail;
        texture =
            SDL_CreateTexture(host->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                              (int)(width * frame.scale), 160 * frame.scale);
        if (pixels == NULL || texture == NULL || !SDL_SetTextureScaleMode(texture, filter))
            goto fail;
    }
    free(host->render_pixels);
    SDL_DestroyTexture(host->render_texture);
    host->render_pixels = pixels;
    host->render_texture = texture;
    frame.pixels = pixels != NULL ? pixels : host->pixels;
    host->frame = frame;
    return 1;
fail:
    free(pixels);
    SDL_DestroyTexture(texture);
    return 0;
}

int portable_time_trial_host_end(PortableTimeTrialHost *host,
                                 PortableRecoveredTimeTrialHostOutcome *out)
{
    if (host == NULL)
        return 1;
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    portable_mobile_touch_clear();
    portable_script_host_end_race(scene->scripts);
    host->session.script_hooks = NULL;
    host->outcome.marker_clock =
        host->replay_active ? host->result_replay.session.marker_clock : host->session.marker_clock;
    host->outcome.marker_display_clock = host->replay_active
                                             ? host->result_replay.session.marker_display_clock
                                             : host->session.marker_display_clock;
    if (scene->input_aid && scene->pause_capture_path != NULL && host->outcome.finished)
    {
        if (!host->pause_captured || host->pause_aid_phase != TT_PAUSE_AID_DONE)
        {
            SDL_SetError("Time Trial pause capture/resume aid incomplete");
            host->error = 1;
        }
        else
            puts("Time Trial pause aid: PASS captured and resumed through ordinary input");
    }

    if (host->error)
        fprintf(stderr, "recovered Time Trial presentation failed: %s\n", SDL_GetError());
    const int flush_failed = host->recording != NULL && fflush(host->recording) != 0;
    const int close_failed = host->recording != NULL && fclose(host->recording) != 0;
    if (flush_failed || close_failed)
    {
        fprintf(stderr, "Time Trial replay close failed: %s\n", scene->record_path);
        host->error = 1;
    }
    modern_menu_host_close(&host->modern);
    portable_model_catalog_close(&host->models);
    portable_scene_3d_close(&host->scene_3d);
    if (host->borrowed_surface)
        *(SDL_Gamepad **)scene->host_controller_slot = host->controller;
    else
    {
        if (host->controller != NULL)
            SDL_CloseGamepad(host->controller);
        if (host->owns_sdl) portable_mobile_touch_close();
        SDL_DestroyTexture(host->texture);
        SDL_DestroyRenderer(host->renderer);
        SDL_DestroyWindow(host->window);
        if (host->owns_sdl) {
            portable_mobile_lifecycle_stop();
            SDL_Quit();
        }
    }
    SDL_DestroyTexture(host->render_texture);
    free(host->render_pixels);
    const int result = host->error;
    if (out != NULL)
        *out = host->outcome;
    free(host);
    return result;
}

PortableTimeTrialHost *
portable_time_trial_host_begin(const PortableRecoveredTimeTrialHostScene *configuration)
{
    if (configuration == NULL || configuration->race == NULL ||
        configuration->internal_scale > 8u || configuration->artwork_lod > 2u ||
        configuration->race_renderer > 1u ||
        (configuration->aspect_width != 0u &&
         (configuration->aspect_width < 240u || configuration->aspect_width > 1024u)))
        return NULL;
    const int borrowed =
        configuration->host_window != NULL || configuration->host_renderer != NULL ||
        configuration->host_texture != NULL || configuration->host_controller_slot != NULL;
    if (borrowed &&
        (configuration->host_window == NULL || configuration->host_renderer == NULL ||
         configuration->host_texture == NULL || configuration->host_controller_slot == NULL))
        return NULL;
    if ((configuration->audio_set_paused == NULL) != (configuration->audio_render_frame == NULL) ||
        (configuration->prelude_ticks != 0u && configuration->audio_render_frame == NULL))
        return NULL;
    PortableTimeTrialHost *host = calloc(1, sizeof *host);
    if (host == NULL)
        return NULL;
    host->scene = *configuration;
    host->scene.presentation.scene_3d = configuration->race_renderer ? &host->scene_3d : NULL;
    const char *replay_aid = getenv("MKSC_AID_TT_RESULTS_REPLAY");
    const char *save_aid = getenv("MKSC_AID_TT_SAVE_GHOST");
    host->aid_save_ghost = (uint8_t)(configuration->input_aid && save_aid && strcmp(save_aid, "1") == 0);
    host->aid_control_interval =
        (uint8_t)(configuration->input_aid &&
            ((replay_aid && strcmp(replay_aid, "1") == 0) || host->aid_save_ghost) ? 4u : 1u);
    if (configuration->lap_board != NULL)
    {
        host->result_board = *configuration->lap_board;
        host->source_results =
            (uint8_t)(configuration->lap_board->followup_tiles != NULL &&
                      configuration->lap_board->followup_map != NULL &&
                      configuration->lap_board->record_patterns != NULL &&
                      configuration->lap_board->followup_palette != NULL &&
                      configuration->lap_board->scroll_map != NULL &&
                      configuration->lap_board->object_tiles != NULL &&
                      configuration->lap_board->object_palette != NULL &&
                      configuration->lap_board->menu_palette != NULL &&
                      configuration->lap_board->menu_pulse_base_palette != NULL &&
                      configuration->lap_board->menu_pulse_factors != NULL);
        if (host->source_results && (!configuration->lap_board->record_identities ||
                                     !configuration->lap_board->menu_option_codes))
        {
            portable_model_catalog_close(&host->models);
            portable_scene_3d_close(&host->scene_3d);
            free(host);
            return 0;
        }
        if (configuration->lap_board->followup_tiles != NULL &&
            configuration->lap_board->followup_tile_bytes == sizeof host->result_tiles)
        {
            memcpy(host->result_tiles, configuration->lap_board->followup_tiles,
                   sizeof host->result_tiles);
            if (configuration->result_course_name_tiles != NULL)
                memcpy(host->result_tiles + 0xc00u, configuration->result_course_name_tiles, 768u);
            host->result_board.followup_tiles = host->result_tiles;
        }
    }

    host->frame = (PortableSoftwareFrame){host->pixels, 240u, 160u, &host->ui_text, 1u, 0u,
        configuration->artwork_lod,NULL};
    if (!portable_ui_text_load(host->scene.frontend_pack, &host->ui_text))
    {
        portable_model_catalog_close(&host->models);
        portable_scene_3d_close(&host->scene_3d);
        free(host);
        return NULL;
    }
    if (configuration->race_renderer &&
        !portable_model_catalog_open(&host->models, configuration->frontend_pack))
        fprintf(stderr, "Optional model catalog unavailable; using billboard art.\n");
    host->scene.presentation.models = configuration->race_renderer ? &host->models : NULL;
    host->focused = 1;
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    PortableRecoveredTimeTrialHostOutcome *outcome = &host->outcome;
    host->borrowed_surface = borrowed;
    if (borrowed)
    {
        host->window = scene->host_window;
        host->renderer = scene->host_renderer;
        host->texture = scene->host_texture;
        host->controller = *(SDL_Gamepad **)scene->host_controller_slot;
    }
    host->frame.scale = scene->internal_scale > 1u ? scene->internal_scale : 1u;
    host->prelude_remaining = scene->prelude_ticks;
    *outcome = (PortableRecoveredTimeTrialHostOutcome){0};
    if (portable_recovered_time_trial_init(&host->session, scene->race) !=
            PORTABLE_RECOVERED_TIME_TRIAL_OK ||
        !restart_scripts(host))
        goto fail;
    host->session.marker_clock = scene->marker_clock;
    host->session.marker_display_clock = scene->marker_display_clock;
    if (scene->record_path != NULL)
    {
        host->recording = fopen(scene->record_path, "w");
        if (host->recording == NULL ||
            !portable_recovered_replay_write_header_file(host->recording, &host->session))
        {
            if (host->recording != NULL)
                fclose(host->recording);
            host->recording = NULL;
            fprintf(stderr, "could not create Time Trial replay: %s\n", scene->record_path);
            goto fail;
        }
    }
    if (!host->borrowed_surface) portable_mobile_lifecycle_hint();
    if (!host->borrowed_surface && !SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
    {
        if (host->recording != NULL)
            fclose(host->recording);
        host->recording = NULL;
        goto fail;
    }

    host->owns_sdl = !host->borrowed_surface;
    if (host->owns_sdl) portable_mobile_lifecycle_start();

    {
        char *preference_directory = scene->save_directory != NULL
                                         ? SDL_strdup(scene->save_directory)
                                         : portable_data_directory();
        if (preference_directory != NULL)
        {
            if (snprintf(host->preference_directory, sizeof host->preference_directory, "%s",
                         preference_directory) >= (int)sizeof host->preference_directory)
                host->preference_directory[0] = '\0';
            if (snprintf(host->best_path, sizeof host->best_path, "%scourse-%u-solo%s.pb",
                         preference_directory, scene->race->rules.compatibility.course_id,
                         portable_mod_identity_record_suffix()) >= (int)sizeof host->best_path)
                host->best_path[0] = '\0';
            if (snprintf(host->ghost_path, sizeof host->ghost_path, "%scourse-%u-solo%s.ghost",
                         preference_directory, scene->race->rules.compatibility.course_id,
                         portable_mod_identity_record_suffix()) >= (int)sizeof host->ghost_path)
                host->ghost_path[0] = '\0';
            SDL_free(preference_directory);
            if (host->best_path[0] != '\0')
            {
                const PortablePersonalBestStatus best_status = portable_personal_best_load_table(
                    host->best_path, &scene->race->rules.compatibility, &host->records);
                host->best_ticks = host->records.races[0].ticks;
                if (best_status != PORTABLE_PERSONAL_BEST_OK &&
                    best_status != PORTABLE_PERSONAL_BEST_NOT_FOUND)
                    fprintf(stderr, "ignored invalid personal best file: %s\n", host->best_path);
            }
            char playback_path[4096];
            int valid_path =
                snprintf(playback_path, sizeof playback_path, "%s", host->ghost_path) > 0;
            if (scene->ghost_received_slot != 0u)
                valid_path = portable_time_trial_ghost_received_path(
                    playback_path, host->preference_directory, scene->ghost_received_slot);
            if (!scene->ghost_hidden && valid_path && playback_path[0] != '\0')
            {
                const PortableTimeTrialGhostStoreStatus ghost_status =
                    portable_time_trial_ghost_store_load(
                        playback_path, &scene->race->rules.compatibility, host->saved_ghost);
                if (ghost_status == PORTABLE_TIME_TRIAL_GHOST_STORE_OK)
                {
                    host->ghost_ticks = portable_time_trial_ghost_record_time(host->saved_ghost);
                    host->ghost_driver = portable_time_trial_ghost_record_driver(host->saved_ghost);
                    host->ghost_active = prepare_ghost_playback(
                        &host->scene, host->saved_ghost, &host->ghost_playback, &host->ghost_character);
                    if (scene->ghost_prepare != NULL && !host->ghost_active)
                        fputs("ignored unresolved ghost driver\n", stderr);
                    if(host->aid_save_ghost)
                        printf("Time Trial ghost loaded: path=%s active=%u ticks=%u\n",
                            playback_path,(unsigned)host->ghost_active,(unsigned)host->ghost_ticks);
                }
                else if (ghost_status == PORTABLE_TIME_TRIAL_GHOST_STORE_INCOMPATIBLE)
                {
                    char reason[2200];
                    if (portable_time_trial_ghost_store_describe_refusal(playback_path, reason,
                                                                         sizeof reason))
                        fprintf(stderr, "ghost %s refused: %s\n", playback_path, reason);
                    else
                        fprintf(stderr,
                                "ghost %s refused: recorded with "
                                "different course or rule content\n",
                                playback_path);
                }
                else if (ghost_status != PORTABLE_TIME_TRIAL_GHOST_STORE_NOT_FOUND)
                    fprintf(stderr, "ignored invalid ghost file: %s\n", playback_path);
            }
        }
        if (scene->require_selected_ghost && !host->ghost_active)
        {
            fprintf(stderr, "course=%u mode=Time Trial state=selected ghost unavailable slot=%u\n",
                    scene->race->rules.compatibility.course_id, scene->ghost_received_slot);
            goto fail;
        }
    }

    if (host->borrowed_surface)
    {
        host->window = scene->host_window;
        host->renderer = scene->host_renderer;
        host->texture = scene->host_texture;
        host->controller = *(SDL_Gamepad **)scene->host_controller_slot;
    }
    else
    {
        host->window =
            SDL_CreateWindow("MKSC Time Trial | arrows drive | Shift hop | C item | P pause | "
                             "R restart | X rescue | Esc courses",
                             960, 640, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        host->renderer = host->window != NULL ? SDL_CreateRenderer(host->window, NULL) : NULL;
        if (host->renderer != NULL)
            SDL_SetRenderVSync(host->renderer, 1);
        host->texture = host->renderer != NULL
                            ? SDL_CreateTexture(host->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                SDL_TEXTUREACCESS_STREAMING, 240, 160)
                            : NULL;
        if (host->texture != NULL && !SDL_SetTextureScaleMode(host->texture, SDL_SCALEMODE_NEAREST))
        {
            SDL_DestroyTexture(host->texture);
            host->texture = NULL;
        }
        host->controller = portable_sdl_gamepad_open_first();
    }
    set_time_trial_window_title(host->window, host->ghost_ticks, host->ghost_driver);
    host->error = host->texture == NULL;
    if (!trial_resize_surface(host))
        goto fail;
    host->performance_frequency = SDL_GetPerformanceFrequency();
    portable_frame_scheduler_init(&host->scheduler);
    portable_frame_scheduler_init(&host->presentation_scheduler);
    if (scene->audio_set_paused != NULL && !scene->audio_set_paused(scene->audio_user, 0))
        host->error = 1;

    if (host->error)
        goto fail;
    return host;
fail:
    host->error = 1;
    portable_time_trial_host_end(host, NULL);
    return NULL;
}

void portable_time_trial_host_event(PortableTimeTrialHost *host, const SDL_Event *input)
{
    if (host == NULL || input == NULL || host->quit || host->error)
        return;
    const SDL_Event event = *input;
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    PortableRecoveredTimeTrialHostOutcome *outcome = &host->outcome;
    modern_menu_host_event(&host->modern, input);
    const uint16_t touch = portable_mobile_touch_event(input, host->renderer);
    if ((touch & PORTABLE_FRONTEND_BUTTON_SELECT) && host->focused &&
        !host->session.paused && !host->finish_saved && scene->audio_horn != NULL &&
        !scene->audio_horn(scene->audio_user)) host->error = 1;
    if ((portable_mobile_back(input) || ((touch & PORTABLE_FRONTEND_BUTTON_B) &&
         (host->session.paused || host->finish_saved || host->replay_active))) && host->focused) {
        if (host->replay_active) stop_result_replay(host);
        else if (host->session.paused) host->mobile_pause_cancel = 1u;
        else if (host->finish_saved) {
            if(host->ghost_save_visible)host->mobile_pressed|=PORTABLE_FRONTEND_BUTTON_B;
            else host->mobile_results_back=1u;
        }
        else {
            portable_recovered_time_trial_set_paused(&host->session, 1);
            host->history_ready = 0u;
            portable_pause_menu_init(&host->pause_menu, sampled_pause_buttons(host->controller));
            host->pause_menu_active = 1;
            if (scene->audio_set_paused != NULL && !scene->audio_set_paused(scene->audio_user, 1))
                host->error = 1;
            record_action(host->recording, PORTABLE_RECOVERED_REPLAY_PAUSE, 1, &host->error);
            portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host), 0);
        }
        return;
    }

    if (event.type == SDL_EVENT_QUIT)
    {
        host->quit = 1;
        outcome->return_to_selection = 1;
        outcome->quit_requested = 1;
    }
    if (host->replay_active &&
        (event.type == SDL_EVENT_GAMEPAD_REMOVED || event.type == SDL_EVENT_GAMEPAD_REMAPPED) &&
        event.gdevice.which == portable_sdl_gamepad_id(host->controller))
    {
        stop_result_replay(host);
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            SDL_CloseGamepad(host->controller);
            host->controller = NULL;
        }
        host->event_pause_buttons = 0u;
        return;
    }
    if (host->replay_active && event.type >= SDL_EVENT_WINDOW_FIRST &&
        event.type <= SDL_EVENT_WINDOW_LAST)
    {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
            event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
        {
            host->focused = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            host->history_ready = 0u;
            host->event_pause_buttons = 0u;
            if (scene->audio_set_paused != NULL &&
                !scene->audio_set_paused(scene->audio_user, !host->focused))
                host->error = 1;
        }
        host->needs_present = 1;
        return;
    }
    if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
    {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && host->focused)
        {
            const int was_paused = host->session.paused;
            host->focused = 0;
            host->needs_present = 1; /* Paint the pause immediately; its clock stops unfocused. */
            host->history_ready = 0u;
            portable_mobile_touch_clear();
            host->mobile_pause_cancel = 0u; host->mobile_pressed = 0u;host->mobile_results_back=0u;
            host->event_pause_buttons = 0u;
            /* Finished menus own their inputs already. Suspending them must
             * not create a second, hidden race pause menu underneath. */
            if (host->session.runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED) {
                portable_recovered_time_trial_set_paused(&host->session, 1);
                portable_pause_menu_init(&host->pause_menu, UINT16_C(0x00ff));
                host->pause_menu_active = 1;
                if (!was_paused)
                    record_action(host->recording, PORTABLE_RECOVERED_REPLAY_PAUSE, 1, &host->error);
            }
            if (scene->audio_set_paused != NULL && !scene->audio_set_paused(scene->audio_user, 1))
                host->error = 1;
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
        {
            host->focused = 1;
            host->needs_present = 1;
            if (host->session.runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED) {
                portable_frame_scheduler_reset(&host->presentation_scheduler, time_trial_host_now(host), 1);
                if (scene->audio_set_paused != NULL && !scene->audio_set_paused(scene->audio_user, 0))
                    host->error = 1;
            }
        }
        if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            host->needs_present = 1;
    }
    if (event.type == SDL_EVENT_GAMEPAD_ADDED && host->controller == NULL)
        host->controller = SDL_OpenGamepad(event.gdevice.which);
    if ((event.type == SDL_EVENT_GAMEPAD_REMOVED || event.type == SDL_EVENT_GAMEPAD_REMAPPED) &&
        event.gdevice.which == portable_sdl_gamepad_id(host->controller))
    {
        portable_recovered_time_trial_set_paused(&host->session, 1);
        host->event_pause_buttons = 0u;
        portable_pause_menu_init(&host->pause_menu, UINT16_C(0x00ff));
        host->pause_menu_active = 1;
        if (scene->audio_set_paused != NULL && !scene->audio_set_paused(scene->audio_user, 1))
            host->error = 1;
        record_action(host->recording, PORTABLE_RECOVERED_REPLAY_PAUSE, 1, &host->error);
        portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host), 0);
        if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            SDL_CloseGamepad(host->controller);
            host->controller = NULL;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && host->focused &&
        event.gbutton.which == portable_sdl_gamepad_id(host->controller))
    {
        host->event_pause_buttons |= portable_sdl_gamepad_frontend_button(event.gbutton.button);
        switch (event.gbutton.button)
        {
        case SDL_GAMEPAD_BUTTON_BACK:
            /* Select is the original horn; restart stays in the pause menu. */
            if (host->focused && !host->session.paused && !host->finish_saved &&
                scene->audio_horn != NULL && !scene->audio_horn(scene->audio_user))
                host->error = 1;
            break;
        case SDL_GAMEPAD_BUTTON_WEST:
            if (host->focused && !host->session.paused &&
                portable_recovered_time_trial_rescue(&host->session) ==
                    PORTABLE_RECOVERED_TIME_TRIAL_OK)
                record_action(host->recording, PORTABLE_RECOVERED_REPLAY_RESCUE, 0, &host->error);
            break;
        default:
            break;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_UP &&
        event.gbutton.which == portable_sdl_gamepad_id(host->controller))
        host->event_pause_buttons &=
            (uint16_t)~portable_sdl_gamepad_frontend_button(event.gbutton.button);
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
    {
        host->event_pause_buttons |= pause_key_button(event.key.scancode);
        switch (event.key.scancode)
        {
        case SDL_SCANCODE_ESCAPE:
            if (host->replay_active)
            {
                stop_result_replay(host);
                break;
            }
            if (host->ghost_save.prompt)
            {
                host->ghost_save.prompt = 0u;
                break;
            }
            if (host->ghost_save.full_list)
            {
                host->ghost_save.full_list = 0u;
                break;
            }
            host->quit = 1;
            outcome->return_to_selection = 1;
            outcome->return_destination = PORTABLE_TIME_TRIAL_RETURN_COURSE;
            break;
        case SDL_SCANCODE_R:
            host->replay_active = 0u;
            host->replay_unavailable = 0u;
            host->history_ready = 0u;
            portable_recovered_time_trial_restart(&host->session);
            if (!restart_scripts(host))
                host->error = 1;
            if (host->ghost_active &&
                portable_time_trial_ghost_playback_restart(&host->ghost_playback) !=
                    PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK)
                host->error = 1;
            host->pause_menu_active = 0;
            host->prelude_remaining = scene->prelude_ticks;
            if (scene->audio_restart != NULL && !scene->audio_restart(scene->audio_user))
                host->error = 1;
            record_action(host->recording, PORTABLE_RECOVERED_REPLAY_RESTART, 0, &host->error);
            portable_recovered_time_trial_set_paused(&host->session, !host->focused);
            if (scene->audio_set_paused != NULL &&
                !scene->audio_set_paused(scene->audio_user, host->session.paused))
                host->error = 1;
            if (!host->focused)
                record_action(host->recording, PORTABLE_RECOVERED_REPLAY_PAUSE, 1, &host->error);
            host->new_best = 0;
            host->finish_saved = 0;
            host->board_visible = 0u;
            host->finish_audio_ready = 0u;
            host->ghost_save = (PortableGhostSaveMenu){0};
            portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host),
                                           host->focused && !host->session.paused);
            break;
        case SDL_SCANCODE_TAB:
            if (host->focused && !host->session.paused && !host->finish_saved &&
                scene->audio_horn != NULL && !scene->audio_horn(scene->audio_user))
                host->error = 1;
            break;
        case SDL_SCANCODE_X:
            if (host->focused && !host->session.paused &&
                portable_recovered_time_trial_rescue(&host->session) ==
                    PORTABLE_RECOVERED_TIME_TRIAL_OK)
                record_action(host->recording, PORTABLE_RECOVERED_REPLAY_RESCUE, 0, &host->error);
            break;
        default:
            break;
        }
    }
    if (event.type == SDL_EVENT_KEY_UP)
        host->event_pause_buttons &= (uint16_t)~pause_key_button(event.key.scancode);
}

/* Independent admission counts remain local to one host frame. Replay and
 * pause presentation use the same host owners and scheduler order. */
typedef struct
{
    uint64_t now;
    size_t presentation_tick_count;
} TrialFrameStep;

static int trial_frame_input_aid(PortableTimeTrialHost *host)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    const char *mobile_results = getenv("MKSC_AID_MOBILE_RESULTS");
    if (scene->input_aid && mobile_results != NULL && strcmp(mobile_results,"1") == 0 &&
        host->finish_saved && host->board_flow.state == 11u) {
        if (host->aid_mobile_results_stage == 0u) {
            SDL_Event events[] = {
                {.type=SDL_EVENT_WILL_ENTER_BACKGROUND},{.type=SDL_EVENT_DID_ENTER_BACKGROUND},
                {.type=SDL_EVENT_WILL_ENTER_FOREGROUND},{.type=SDL_EVENT_DID_ENTER_FOREGROUND}};
            for (unsigned i=0u;i<4u;++i) if (!SDL_PushEvent(&events[i])) {
                host->error=1;return 0;
            }
            host->aid_mobile_results_stage=1u;
        } else if (host->aid_mobile_results_stage == 1u) {
            if (!host->focused || host->session.paused || host->pause_menu_active) {
                fprintf(stderr,"Time Trial mobile results: hidden pause owner after foreground course=%u state=%u\n",
                    host->snapshot.compatibility.course_id,host->board_flow.state);
                host->error=1;return 0;
            }
            const SDL_Event back={.key={.type=SDL_EVENT_KEY_DOWN,.scancode=SDL_SCANCODE_AC_BACK}};
            portable_time_trial_host_event(host,&back);
            host->aid_mobile_results_stage=2u;
        }
    }
    if (scene->input_aid && ++host->aid_frames > 60000u)
    {
        fprintf(stderr, "Time Trial course=%u input aid timed out\n",
                host->session.runtime.session.compatibility.course_id);
        host->error = 1;
        return 0;
    }
    if (scene->input_aid && scene->pause_capture_path != NULL)
    {
        SDL_Event aid = {.key = {.type = SDL_EVENT_KEY_UP, .scancode = SDL_SCANCODE_P}};
        if (host->pause_aid_phase == TT_PAUSE_AID_RELEASE_PAUSE)
        {
            portable_time_trial_host_event(host, &aid);
            host->pause_aid_phase = TT_PAUSE_AID_CAPTURE;
        }
        else if (host->pause_aid_phase == TT_PAUSE_AID_RELEASE_CONTINUE)
        {
            aid.key.scancode = SDL_SCANCODE_RETURN;
            portable_time_trial_host_event(host, &aid);
            host->pause_aid_phase = TT_PAUSE_AID_DONE;
        }
        else if (host->pause_aid_phase == TT_PAUSE_AID_CAPTURE && host->pause_captured &&
                 !host->pause_graphics_checked && scene->graphics.current != NULL &&
                 getenv("MKSC_AID_PAUSE_GRAPHICS") != NULL)
            host->pause_aid_phase = TT_PAUSE_AID_GRAPHICS;
        else if (host->pause_aid_phase == TT_PAUSE_AID_GRAPHICS)
        {
            if (!trial_aid_graphics(host))
            {
                const PortableSettings *now = scene->graphics.current(scene->graphics.user);
                if (now->linear_filter || now->internal_scale != 1u || host->pause_menu.graphics.open)
                {
                    SDL_SetError("Time Trial pause graphics aid: settings were not restored");
                    host->error = 1;
                }
                host->pause_aid_phase = TT_PAUSE_AID_CAPTURE;
                host->pause_graphics_checked = 1;
            }
        }
        else if (host->pause_aid_phase == TT_PAUSE_AID_CAPTURE && host->pause_captured)
        {
            aid.type = SDL_EVENT_KEY_DOWN;
            aid.key.scancode = SDL_SCANCODE_RETURN;
            portable_time_trial_host_event(host, &aid);
            host->pause_aid_phase = TT_PAUSE_AID_RELEASE_CONTINUE;
        }
        else if (host->pause_aid_phase == TT_PAUSE_AID_WAIT && host->focused &&
                 !host->replay_active && !host->session.paused &&
                 host->session.scene_clock >= 500u &&
                 host->session.runtime.session.phase == PORTABLE_RACE_PHASE_RACING)
        {
            aid.type = SDL_EVENT_KEY_DOWN;
            portable_time_trial_host_event(host, &aid);
            host->pause_aid_phase = TT_PAUSE_AID_RELEASE_PAUSE;
        }
    }
    const char *manual_rescue = getenv("MKSC_AID_MANUAL_RESCUE");
    if (scene->input_aid && manual_rescue && strcmp(manual_rescue, "1") == 0 &&
        !host->aid_manual_rescue_done && !host->replay_active && host->focused &&
        !host->session.paused &&
        host->session.runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
        (host->snapshot.contact_audio.serial || host->snapshot.boost_voice.serial))
    {
        const uint32_t before = host->session.runtime.session.rescue_count;
        SDL_Event rescue = {.gbutton = {.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN,
                                        .which = portable_sdl_gamepad_id(host->controller),
                                        .button = SDL_GAMEPAD_BUTTON_WEST}};
        portable_time_trial_host_event(host, &rescue);
        rescue.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
        portable_time_trial_host_event(host, &rescue);
        if (host->session.runtime.session.rescue_count != before)
        {
            host->aid_manual_rescue_done = 1u;
            puts("Time Trial manual rescue: controller West applied after audio cue");
        }
    }
    return 1;
}

static uint16_t trial_aid_driving_buttons(PortableTimeTrialHost *host, uint16_t buttons)
{
    /* Holding feedback input for four turns keeps the original
     * bounded ghost recorder usable in the results-replay route. */
    if (host->session.runtime.session.phase != PORTABLE_RACE_PHASE_RACING)
    {
        host->aid_control_keys = 0u;
        host->aid_control_remaining = 0u;
    }
    else
    {
        if (!host->aid_control_remaining)
        {
            host->aid_control_keys = portable_recovered_race_smoke_control(&host->session.runtime);
            host->aid_control_remaining = host->aid_control_interval;
        }
        --host->aid_control_remaining;
    }
    const uint16_t source = host->aid_control_keys;
    buttons = (uint16_t)(((source & 0x8000u) ? PORTABLE_INPUT_ACCELERATE : 0u) |
                         ((source & 0x4000u) ? PORTABLE_INPUT_BRAKE : 0u) |
                         ((source & 0x0100u) ? PORTABLE_INPUT_RIGHT : 0u) |
                         ((source & 0x0200u) ? PORTABLE_INPUT_LEFT : 0u) |
                         ((source & 0x0010u) ? PORTABLE_INPUT_HOP : 0u));
    /* Exercise a rejected airborne press, then a fresh
     * grounded mushroom press through the existing shell. */
    if (host->session.runtime.session.phase == PORTABLE_RACE_PHASE_RACING)
    {
        const uint32_t elapsed = host->session.runtime.session.elapsed_ticks;
        if (elapsed >= 120u && elapsed <= 390u)
        {
            const uint32_t edge = elapsed % 120u;
            if (edge < 2u)
                buttons |= PORTABLE_INPUT_HOP;
            if (edge == 2u || edge == 30u)
                buttons |= PORTABLE_INPUT_ITEM;
        }
        /* Course jumps can reject every early press. Retry
         * through ordinary input when the driver is grounded. */
        if (elapsed > 390u && elapsed % 120u == 30u && host->session.mushrooms_remaining &&
            !host->session.driver.item_requests_b2 && host->session.driver.hop.mode == 0u &&
            !(host->session.driver.steering.state_flags & UINT32_C(0x10800)))
            buttons |= PORTABLE_INPUT_ITEM;
    }
    const char *wall = getenv("MKSC_AID_WALL_CONTACT");
    buttons = portable_recovered_wall_contact_aid(buttons,
                                                  wall && strcmp(wall, "left") == 0    ? -1
                                                  : wall && strcmp(wall, "right") == 0 ? 1
                                                                                       : 0,
                                                  &host->session.runtime.session);
    const char *water_rescue = getenv("MKSC_AID_WATER_RESCUE");
    buttons = portable_recovered_water_rescue_aid(
        buttons, water_rescue != NULL && strcmp(water_rescue, "1") == 0, &host->session.driver);
    const char *modern_drift=getenv("MKSC_AID_MODERN_DRIFT");
    buttons=portable_recovered_modern_drift_aid(buttons,
        modern_drift!=NULL && strcmp(modern_drift,"1")==0,&host->session.driver,
        host->session.runtime.recovered_dependencies->modern_drift_rules);
    const char *modern_air=getenv("MKSC_AID_MODERN_AIR");
    buttons=portable_recovered_modern_air_aid(buttons,modern_air==NULL?0:strcmp(modern_air,"once")==0?2:strcmp(modern_air,"1")==0,
        &host->session.driver,host->session.runtime.recovered_dependencies->modern_drift_rules);
    return buttons;
}

static int trial_advance_race_ticks(PortableTimeTrialHost *host, uint64_t now)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    uint16_t tick_buttons[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t tick_count = 0u;
    int dropped_debt = 0;
    const int active = host->focused && (host->replay_active || !host->session.paused) &&
                       host->prelude_remaining == 0u;

    if (portable_frame_scheduler_update(
            &host->scheduler, now, active, sampled_buttons(host->controller), tick_buttons,
            PORTABLE_FRAME_MAX_CATCH_UP, &tick_count, &dropped_debt) != PORTABLE_FRAME_SCHEDULER_OK)
    {
        host->error = 1;
        return 0;
    }
    (void)dropped_debt;
    for (size_t tick = 0; tick < tick_count; ++tick)
    {
        PortableRaceTickDisposition disposition;
        if (host->replay_active)
        {
            host->previous_replay = host->replay_snapshot;
            if (portable_time_trial_ghost_playback_tick(&host->result_replay, &disposition) !=
                    PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK ||
                portable_recovered_time_trial_snapshot(&host->result_replay.session,
                                                       &host->replay_snapshot) !=
                    PORTABLE_RECOVERED_TIME_TRIAL_OK)
            {
                fputs("finished Time Trial replay tick failed\n", stderr);
                host->error = 1;
                break;
            }
            host->history_ready = 1u;
            if (host->replay_snapshot.phase == PORTABLE_RACE_PHASE_FINISHED)
            {
                if (host->replay_snapshot.total_ticks != host->snapshot.total_ticks)
                {
                    fputs("finished Time Trial replay time mismatch\n", stderr);
                    host->error = 1;
                    break;
                }
                ++host->outcome.replays_completed;
                stop_result_replay(host);
                break;
            }
            if (host->result_replay.ended)
            {
                fputs("finished Time Trial replay ended before finish\n", stderr);
                host->error = 1;
                break;
            }
            continue;
        }
        if (portable_recovered_time_trial_snapshot(&host->session, &host->previous_snapshot) !=
                PORTABLE_RECOVERED_TIME_TRIAL_OK ||
            (host->ghost_active && portable_recovered_time_trial_snapshot(
                                       &host->ghost_playback.session, &host->previous_ghost) !=
                                       PORTABLE_RECOVERED_TIME_TRIAL_OK))
        {
            host->error = 1;
            break;
        }
        host->history_ready = 1u;
        if (scene->input_aid)
            tick_buttons[tick] = trial_aid_driving_buttons(host, tick_buttons[tick]);
        if (!scene->input_aid)
            tick_buttons[tick] |= portable_mobile_touch_take_driving_pressed();
        if (portable_recovered_time_trial_tick(&host->session, tick_buttons[tick], &disposition) !=
            PORTABLE_RECOVERED_TIME_TRIAL_OK)
        {
            fprintf(
                stderr,
                "recovered Time Trial course=%u hop=%u requests=%04x Lakitu=%u/%u tick failed: runtime=%d "
                "driver=%d phase=%d pose=0x%08x,0x%08x\n",
                host->session.runtime.session.compatibility.course_id,
                host->session.driver.hop.mode, host->session.driver.hop.recovery_requests,
                host->session.hazard_lakitu.phase, (unsigned)host->session.hazard_lakitu.callback,
                host->session.last_runtime_status, host->session.runtime.last_driver_status,
                host->session.runtime.session.phase, (uint32_t)host->session.driver.pose.position_x,
                (uint32_t)host->session.driver.pose.position_y);
            host->error = 1;
            break;
        }
        if (host->ghost_active)
        {
            PortableRaceTickDisposition ghost_disposition;
            if (portable_time_trial_ghost_playback_tick(&host->ghost_playback,
                                                        &ghost_disposition) !=
                PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK)
            {
                fputs("local ghost playback tick failed\n", stderr);
                host->error = 1;
                break;
            }
        }
        if (host->recording != NULL &&
            !portable_recovered_replay_write_tick_file(host->recording, host->recorded_ticks,
                                                  tick_buttons[tick], disposition, &host->session))
        {
            fprintf(stderr, "Time Trial replay write failed: %s\n", scene->record_path);
            host->error = 1;
            break;
        }
        ++host->recorded_ticks;
        host->display_buttons = tick_buttons[tick];
    }
    /* A simulation error still reaches the original presentation control
     * checkpoint. Only scheduler admission failure skips that second pass. */
    return 1;
}

/* Opt-in route (MKSC_AID_PAUSE_GRAPHICS=1) through the pause menu's Graphics page with ordinary key events:
 * Select opens it, Filter goes Linear and Internal Scale 2 (live), both are put back, B closes it. */
static int trial_aid_graphics(PortableTimeTrialHost *host)
{
    static const struct { SDL_Scancode key; const char *capture; } steps[] = {
        {SDL_SCANCODE_TAB, NULL},   {SDL_SCANCODE_UNKNOWN, "tt-pause-graphics-open"},
        {SDL_SCANCODE_DOWN, NULL},  {SDL_SCANCODE_DOWN, NULL}, {SDL_SCANCODE_DOWN, NULL},
        {SDL_SCANCODE_RIGHT, NULL}, {SDL_SCANCODE_DOWN, NULL}, {SDL_SCANCODE_DOWN, NULL},
        {SDL_SCANCODE_DOWN, NULL},  {SDL_SCANCODE_RIGHT, NULL},
        {SDL_SCANCODE_UNKNOWN, "tt-pause-graphics-edited"},
        {SDL_SCANCODE_LEFT, NULL},  {SDL_SCANCODE_UP, NULL},   {SDL_SCANCODE_UP, NULL},
        {SDL_SCANCODE_UP, NULL},    {SDL_SCANCODE_LEFT, NULL}, {SDL_SCANCODE_X, NULL}};
    const unsigned count = (unsigned)(sizeof steps / sizeof steps[0]);
    if (host->aid_graphics_wait != 0u)
    {
        --host->aid_graphics_wait;
        return 1;
    }
    if (host->aid_graphics_step >= count)
        return 0;
    SDL_Event key = {.key = {.type = host->aid_graphics_held ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN,
                             .scancode = steps[host->aid_graphics_step].key}};
    if (steps[host->aid_graphics_step].key == SDL_SCANCODE_UNKNOWN)
    {
        const char *directory = getenv("MKSC_SCREEN_DIR");
        char path[4096];
        const char *name = steps[host->aid_graphics_step].capture;
        const int modern = host->scene.modern_menus;
        if (directory == NULL || snprintf(path, sizeof path, "%s/%s.ppm", directory, name) >= (int)sizeof path ||
            !(modern ? modern_menu_host_write_ppm(&host->modern, path)
                     : portable_software_frame_write_ppm(&host->frame, path)))
        {
            host->error = 1;
            return 1;
        }
        if (strcmp(name, "tt-pause-graphics-edited") == 0)
        {
            const PortableSettings *now = host->scene.graphics.current != NULL
                ? host->scene.graphics.current(host->scene.graphics.user) : NULL;
            if (now == NULL || !now->linear_filter || now->internal_scale != 2u ||
                host->frame.scale != 2u || !host->pause_menu.graphics.open)
            {
                SDL_SetError("Time Trial pause graphics aid: edits did not apply");
                host->error = 1;
                return 1;
            }
            printf("Time Trial pause graphics: PASS filter=linear internal_scale=2 surface scale=%u\n",
                   (unsigned)host->frame.scale);
        }
        ++host->aid_graphics_step;
        host->aid_graphics_wait = 4u;
        return 1;
    }
    portable_time_trial_host_event(host, &key);
    if (host->aid_graphics_held)
    {
        ++host->aid_graphics_step;
        host->aid_graphics_wait = steps[host->aid_graphics_step - 1u].key == SDL_SCANCODE_TAB ||
                                  steps[host->aid_graphics_step - 1u].key == SDL_SCANCODE_RIGHT ? 16u : 2u;
    }
    host->aid_graphics_held = (uint8_t)!host->aid_graphics_held;
    return 1;
}

/* The pause menu's Graphics page changed a setting: take the values this race presents with. The surface
 * follows on the next frame (trial_resize_surface); Artwork Detail and Race Renderer wait for the next race. */
static void trial_apply_graphics(PortableTimeTrialHost *host, const PortableSettings *settings)
{
    PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    scene->fit_scaling = settings->fit_scaling;
    scene->interpolate = settings->interpolate;
    scene->render_fps = (uint16_t)settings->render_fps;
    scene->internal_scale = settings->internal_scale;
    scene->aspect_width = (uint16_t)settings->aspect_width;
    if (host->render_texture != NULL)
        (void)SDL_SetTextureScaleMode(host->render_texture,
                                      settings->linear_filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    host->needs_present = 1;
}

/* Pause actions retain their recording/audio/reset order before the next
 * presentation snapshot; retry does not consume a race tick here. */
static void trial_apply_pause_action(PortableTimeTrialHost *host, PortablePauseAction pause_action)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    PortableRecoveredTimeTrialHostOutcome *outcome = &host->outcome;
    if (pause_action == PORTABLE_PAUSE_ACTION_CONTINUE)
    {
        portable_recovered_time_trial_set_paused(&host->session, 0);
        host->pause_menu_active = 0;
        if (scene->audio_set_paused != NULL && !scene->audio_set_paused(scene->audio_user, 0))
            host->error = 1;
        record_action(host->recording, PORTABLE_RECOVERED_REPLAY_PAUSE, 0, &host->error);
        portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host), 1);
    }
    else if (pause_action == PORTABLE_PAUSE_ACTION_RETRY)
    {
        host->history_ready = 0u;
        portable_recovered_time_trial_restart(&host->session);
        if (!restart_scripts(host))
            host->error = 1;
        if (host->ghost_active &&
            portable_time_trial_ghost_playback_restart(&host->ghost_playback) !=
                PORTABLE_TIME_TRIAL_GHOST_PLAYBACK_OK)
            host->error = 1;
        host->prelude_remaining = scene->prelude_ticks;
        host->pause_menu_active = 0;
        if (scene->audio_restart != NULL && !scene->audio_restart(scene->audio_user))
            host->error = 1;
        record_action(host->recording, PORTABLE_RECOVERED_REPLAY_RESTART, 0, &host->error);
        host->new_best = 0;
        host->finish_saved = 0;
        host->board_visible = 0u;
        host->finish_audio_ready = 0u;
        host->ghost_save = (PortableGhostSaveMenu){0};
        portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host), host->focused);
    }
    else if (pause_action >= PORTABLE_PAUSE_ACTION_CHANGE_TRACK &&
             pause_action <= PORTABLE_PAUSE_ACTION_QUIT)
    {
        static const PortableRecoveredTimeTrialReturn returns[] = {
            PORTABLE_TIME_TRIAL_RETURN_COURSE,
            PORTABLE_TIME_TRIAL_RETURN_CHARACTER,
            PORTABLE_TIME_TRIAL_RETURN_MODE,
        };
        outcome->return_to_selection = 1;
        outcome->return_destination = returns[pause_action - PORTABLE_PAUSE_ACTION_CHANGE_TRACK];
        host->quit = 1;
    }
}

/* Exercise the same Select/save-confirmation controls as a player. Keep this
 * opt-in sequence outside the default result aid and never bypass its UI. */
static uint16_t trial_aid_save_ghost_buttons(PortableTimeTrialHost *host)
{
    if (++host->aid_ghost_frames > 120u || !host->source_results ||
        (host->ghost_save_visible &&
         host->ghost_save.status != PORTABLE_GHOST_SAVE_READY &&
         host->ghost_save.status != PORTABLE_GHOST_SAVE_SAVED))
    {
        fprintf(stderr,"course=%u mode=Time Trial state=ghost-save aid failed status=%u "
            "record_valid=%u exhausted=%d remaining=%d cursor=%zu\n",
            host->snapshot.compatibility.course_id,(unsigned)host->ghost_save.status,
            host->session.ghost_recording_valid,host->session.ghost_encoder.exhausted,
            host->session.ghost_encoder.remaining,host->session.ghost_encoder.cursor);
        host->error = 1;
        return 0u;
    }
    if (host->scene.modern_menus && getenv("MKSC_AID_RESULTS_HOLD") != NULL && host->ghost_save_visible &&
        host->modern_result_ticks - host->modern.results_capture_tick < 34u)
        return 0u; /* Let each modern prompt become visible before confirming. */
    if (host->aid_ghost_frames & 1u)
        return 0u; /* Release every ordinary button edge. */
    if (!host->ghost_save_visible)
        return PORTABLE_FRONTEND_BUTTON_SELECT;
    if (host->ghost_save.status == PORTABLE_GHOST_SAVE_SAVED)
    {
        char identity[PORTABLE_MOD_IDENTITY_TEXT_CAPACITY];
        portable_mod_identity_describe(portable_mod_identity_current(),identity,sizeof identity);
        printf("Time Trial ghost saved: path=%s ticks=%u mods=%s\n",
            host->ghost_path,(unsigned)portable_time_trial_ghost_record_time(host->saved_ghost),identity);
        host->aid_ghost_saved = 1u;
        host->aid_results_frames = 0u;
        return PORTABLE_FRONTEND_BUTTON_B;
    }
    if (host->ghost_save.prompt && !host->ghost_save.yes)
        return PORTABLE_FRONTEND_BUTTON_RIGHT;
    return PORTABLE_FRONTEND_BUTTON_A;
}

static int trial_tick_controls(PortableTimeTrialHost *host)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    PortableRecoveredTimeTrialHostOutcome *outcome = &host->outcome;
    const uint16_t touch_buttons = (uint16_t)(portable_mobile_touch_frontend() |
                                            portable_mobile_touch_take_pressed());
    uint16_t pause_buttons =
        (uint16_t)(sampled_pause_buttons(host->controller) | host->event_pause_buttons |
                   touch_buttons | host->mobile_pressed);
    host->mobile_pressed = 0u;
    int mobile_confirm=0;
    if(host->mobile_results_back&&host->focused&&host->board_flow.state==11u){
        for(unsigned i=0u;i<5u;++i)if(host->result_board.menu_option_codes[i]==1u){
            host->board_flow.menu_selection=(uint8_t)i;
            pause_buttons=PORTABLE_FRONTEND_BUTTON_A;host->mobile_results_back=0u;mobile_confirm=1;break;
        }
    }
    if (host->session.paused && host->mobile_pause_cancel &&
        host->pause_menu.open_ticks >= PORTABLE_PAUSE_MENU_OPEN_TICKS) {
        host->pause_menu.cursor = 0u;
        host->pause_menu.buttons &= (uint16_t)~PORTABLE_FRONTEND_BUTTON_A;
        pause_buttons |= PORTABLE_FRONTEND_BUTTON_A;
        host->mobile_pause_cancel = 0u;
    }
    if (scene->input_aid && host->finish_saved && !host->source_results &&
        host->board_flow.total_ticks >= 655u)
        pause_buttons = PORTABLE_FRONTEND_BUTTON_B;
    if (scene->input_aid && host->finish_saved && host->board_flow.state == 11u &&
        host->aid_mobile_results_stage == 0u)
    {
        ++host->aid_results_frames;
        /* Ordinary cursor edges can inspect one complete replay,
         * then choose Change Track from the retained action menu. */
        const char *replay_aid = getenv("MKSC_AID_TT_RESULTS_REPLAY");
        const unsigned target =
            replay_aid && strcmp(replay_aid, "1") == 0 && host->outcome.replays_completed == 0u
                ? 1u
                : 2u;
        if (host->aid_save_ghost && !host->aid_ghost_saved)
            pause_buttons = trial_aid_save_ghost_buttons(host);
        else if (host->aid_results_frames >= (scene->modern_menus && getenv("MKSC_AID_RESULTS_HOLD") != NULL ? 40u : 8u) && !(host->aid_results_frames & 1u))
            pause_buttons = host->board_flow.menu_selection == target
                                ? PORTABLE_FRONTEND_BUTTON_A
                                : PORTABLE_FRONTEND_BUTTON_DOWN;
        if (host->error)
            return 0;
    }
    PortablePauseAction pause_action = PORTABLE_PAUSE_ACTION_NONE;
    if (!host->session.paused &&
        host->session.runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED &&
        (pause_buttons & (uint16_t)~host->previous_pause_buttons &
         PORTABLE_FRONTEND_BUTTON_START) != 0u)
    {
        if (portable_recovered_time_trial_set_paused(&host->session, 1) !=
            PORTABLE_RECOVERED_TIME_TRIAL_OK)
        {
            host->error = 1;
            return 0;
        }
        portable_pause_menu_init(&host->pause_menu, pause_buttons);
        host->pause_menu_active = 1;
        if (scene->audio_set_paused != NULL && !scene->audio_set_paused(scene->audio_user, 1))
            host->error = 1;
        record_action(host->recording, PORTABLE_RECOVERED_REPLAY_PAUSE, 1, &host->error);
    }
    if (host->session.paused && host->pause_menu_active)
    {
        pause_action = portable_pause_menu_tick(&host->pause_menu, pause_buttons);
        const PortableSettings *graphics =
            portable_graphics_pause_update(&host->pause_menu.graphics, &scene->graphics);
        if (graphics != NULL)
            trial_apply_graphics(host, graphics);
        if (host->pause_menu.cue != 0u && scene->audio_cue != NULL &&
            !scene->audio_cue(scene->audio_user, host->pause_menu.cue))
            host->error = 1;
    }
    if (host->session.runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED && host->finish_saved &&
        host->focused)
    {
        if (scene->modern_menus && !host->replay_active) ++host->modern_result_ticks;
        const uint16_t triggered = (uint16_t)((pause_buttons & ~host->previous_pause_buttons) |
            (mobile_confirm ? PORTABLE_FRONTEND_BUTTON_A : 0u));
        if (host->replay_active)
        {
            if (triggered & PORTABLE_FRONTEND_BUTTON_B)
                stop_result_replay(host);
        }
        else if (host->ghost_save_visible)
        {
            if (host->source_results && (triggered & PORTABLE_FRONTEND_BUTTON_B) &&
                !host->ghost_save.prompt && !host->ghost_save.full_list)
                host->ghost_save_visible = 0u;
            else
                handle_ghost_save(host, triggered);
        }
        else
        {
            if (host->board_flow.state == 11u && (triggered & PORTABLE_FRONTEND_BUTTON_SELECT))
                host->ghost_save_visible = 1u;
            else
                tick_lap_board(host, pause_buttons, triggered);
            if (host->board_flow.state == 13u && !host->result_action_done)
            {
                host->result_action_done = 1u;
                const unsigned action = host->board_flow.action_dispatch_code;
                if (action == 6u)
                {
                    start_result_replay(host);
                    host->board_flow.state = 11u;
                    host->result_action_done = 0u;
                }
                else if (action == 1u)
                {
                    SDL_Event retry = {0};
                    retry.type = SDL_EVENT_KEY_DOWN;
                    retry.key.scancode = SDL_SCANCODE_R;
                    portable_time_trial_host_event(host, &retry);
                }
                else
                {
                    if (host->aid_mobile_results_stage == 2u) {
                        if (action != 2u) {
                            fprintf(stderr,"Time Trial mobile results: Back dispatched unexpected action=%u\n",action);
                            host->error=1;return 0;
                        }
                        host->aid_mobile_results_stage=3u;
                        puts("Time Trial mobile results: PASS foreground has no hidden pause; Back returned Course");
                    }
                    outcome->return_to_selection = 1;
                    outcome->return_destination = action == 2u ? PORTABLE_TIME_TRIAL_RETURN_COURSE
                                                  : action == 3u
                                                      ? PORTABLE_TIME_TRIAL_RETURN_CHARACTER
                                                      : PORTABLE_TIME_TRIAL_RETURN_MODE;
                    host->quit = 1;
                }
            }
        }
    }
    host->previous_pause_buttons = pause_buttons;

    trial_apply_pause_action(host, pause_action);
    return !host->error && !host->quit;
}

/* Snapshot and finish-audio latches precede the callback, then rumble and
 * prelude countdown advance once per admitted presentation tick. */
static int trial_tick_audio(PortableTimeTrialHost *host)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    if (portable_recovered_time_trial_snapshot(&host->session, &host->snapshot) !=
        PORTABLE_RECOVERED_TIME_TRIAL_OK)
    {
        host->error = 1;
        return 0;
    }
    PortableRecoveredTimeTrialSnapshot *audio_snapshot =
        host->replay_active ? &host->replay_snapshot : &host->snapshot;
    if (audio_snapshot->phase == PORTABLE_RACE_PHASE_FINISHED)
    {
        if (!host->replay_active && !host->finish_audio_ready)
        {
            host->finish_audio_record = (uint8_t)portable_time_trial_finish_record_with_defaults(
                audio_snapshot, &host->records, scene->race->time_trial_record_defaults);
            host->finish_audio_ready = 1u;
        }
        audio_snapshot->finish_audio_ready = 1u;
        audio_snapshot->finish_audio_record = host->replay_active ? 0u : host->finish_audio_record;
        audio_snapshot->finish_audio_replay = host->replay_active;
    }
    if (scene->audio_render_frame != NULL &&
        !scene->audio_render_frame(
            scene->audio_user, sampled_buttons(host->controller),
            host->replay_active
                ? host->replay_snapshot.phase == PORTABLE_RACE_PHASE_COUNTDOWN
                : (host->prelude_remaining != 0u ||
                   host->session.runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN),
            host->replay_active ? &host->replay_snapshot : &host->snapshot))
    {
        fprintf(stderr, "Time Trial audio failed: course=%u tick=%u contact-song=%u\n",
                host->snapshot.compatibility.course_id, host->snapshot.total_ticks,
                host->snapshot.contact_audio.song_id);
        host->error = 1;
        return 0;
    }
    if (scene->rumble && !host->replay_active)
        portable_sdl_gamepad_race_rumble(host->controller, &host->rumble,
                                         &host->snapshot.engine_sound,
                                         &host->snapshot.contact_audio, &host->snapshot.boost_voice,
                                         host->snapshot.phase == PORTABLE_RACE_PHASE_RACING);
    if (host->prelude_remaining != 0u)
        --host->prelude_remaining;
    return 1;
}

static int trial_advance_presentation_ticks(PortableTimeTrialHost *host, uint64_t now,
                                            size_t *presentation_tick_count)
{
    uint16_t ignored[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t presentation_ticks = 0u;
    int dropped_presentation_debt = 0;
    if (portable_frame_scheduler_update(&host->presentation_scheduler, now, host->focused, 0u,
                                        ignored, PORTABLE_FRAME_MAX_CATCH_UP, &presentation_ticks,
                                        &dropped_presentation_debt) != PORTABLE_FRAME_SCHEDULER_OK)
    {
        host->error = 1;
        return 0;
    }
    (void)dropped_presentation_debt;
    *presentation_tick_count = presentation_ticks;
    for (size_t tick = 0u; tick < presentation_ticks; ++tick)
    {
        if (!trial_tick_controls(host))
            break;
        if (!trial_tick_audio(host))
            break;
    }
    if (host->error || host->quit)
        return 0;
    return 1;
}

/* Save the first valid finish and prepare its record art before drawing;
 * later result/replay frames reuse the retained owner and pre-finish latch. */
static int trial_finish_result(PortableTimeTrialHost *host)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    PortableRecoveredTimeTrialHostOutcome *outcome = &host->outcome;
    if (host->snapshot.phase == PORTABLE_RACE_PHASE_FINISHED && !host->finish_saved)
    {
        host->finish_saved = 1;
        begin_lap_board(host);
        if (!portable_time_trial_result_is_valid(&host->snapshot))
        {
            fputs("rejected invalid finished Time Trial result\n", stderr);
            host->error = 1;
            return 0;
        }
        if (scene->input_aid && (host->snapshot.mushrooms_remaining != 0u ||
                                 host->session.driver.item_requests_b2 != 0u))
        {
            fputs("Time Trial smoke did not consume all three mushroom requests\n", stderr);
            host->error = 1;
            return 0;
        }
        outcome->finished = 1;
        outcome->result_ticks = host->snapshot.total_ticks;
        uint32_t previous_times[6], best_lap = UINT32_MAX;
        PortableGpResultsRecord previous_records[6];
        trial_result_records(host, previous_records);
        for (unsigned row = 0u; row < 6u; ++row)
            previous_times[row] = previous_records[row].time_centiseconds;
        for (unsigned lap = 0u; lap < host->snapshot.lap_count; ++lap)
            if (host->snapshot.lap_ticks[lap] < best_lap)
                best_lap = host->snapshot.lap_ticks[lap];
        portable_time_trial_results_emphasis(previous_times,
                                             board_centiseconds(host->snapshot.total_ticks),
                                             board_centiseconds(best_lap), host->result_emphasis);
        if (host->best_path[0] != '\0' && portable_time_trial_personal_best_update(
                                              host->best_path, &host->snapshot, &host->best_ticks,
                                              &host->new_best) != PORTABLE_PERSONAL_BEST_OK)
            fprintf(stderr, "failed to save personal best: %s\n", host->best_path);
        else if (host->best_path[0] == '\0')
            host->best_ticks = host->snapshot.total_ticks;
        if (scene->race->time_trial_record_defaults != NULL)
            host->new_best = board_centiseconds(host->snapshot.total_ticks) < previous_times[0];
        prepare_ghost_save(host);
        /* Retry compares against the just-saved table; the current finish
         * retains its latched pre-race audio decision. */
        if (host->best_path[0] != '\0')
            (void)portable_personal_best_load_table(host->best_path, &host->snapshot.compatibility,
                                                    &host->records);
    }
    if (host->finish_saved && host->board_flow.state <= 1u)
    {
        trial_result_records(host, host->result_records);
        for (unsigned row = 0u; row < 6u; ++row)
        {
            PortableGpResultsRecord *record = &host->result_records[row];
            record->emphasized = host->result_emphasis[row];
            PortableGpResultsRecordArt *art = &host->result_board.record_art[row];
            *art = (PortableGpResultsRecordArt){.roster_driver_id = record->roster_driver_id};
            if (record->roster_driver_id == 0u || (record->flags & 0x10u))
                continue;
            PortableCharacterContent character;
            if (portable_content_registry_saved_driver(scene->frontend_pack, record->flags & 7u,
                                                       record->roster_driver_id,
                                                       &character) != PORTABLE_CONTENT_OK)
                continue;
            if (portable_character_roster_image(scene->frontend_pack, character.id,
                                                PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT,
                                                &host->result_portraits[row]))
                art->portrait = &host->result_portraits[row];
            if (portable_character_roster_image(scene->frontend_pack, character.id,
                                                PORTABLE_CHARACTER_ROSTER_BOARD_NAME,
                                                &host->result_names[row]))
                art->name = &host->result_names[row];
        }
    }
    return 1;
}

static int trial_build_frame(PortableTimeTrialHost *host, int smooth)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    if (portable_recovered_time_trial_snapshot(&host->session, &host->snapshot) !=
        PORTABLE_RECOVERED_TIME_TRIAL_OK)
    {
        host->error = 1;
        return 0;
    }
    if (!write_telemetry(scene, &host->snapshot))
    {
        fprintf(stderr, "Time Trial telemetry write failed: %s\n", scene->telemetry_path);
        host->error = 1;
        return 0;
    }
    if (!trial_finish_result(host))
        return 0;
    if (host->ghost_active && portable_recovered_time_trial_snapshot(&host->ghost_playback.session,
                                                                     &host->ghost_snapshot) !=
                                  PORTABLE_RECOVERED_TIME_TRIAL_OK)
    {
        host->error = 1;
        return 0;
    }
    host->render_snapshot = host->snapshot;
    host->render_ghost = host->ghost_snapshot;
    if (smooth && host->history_ready)
    {
        const uint32_t alpha = portable_frame_scheduler_alpha(&host->scheduler);
        portable_trial_interpolate(&host->previous_snapshot, &host->snapshot, alpha,
                                   &host->render_snapshot);
        if (host->ghost_active)
            portable_trial_interpolate(&host->previous_ghost, &host->ghost_snapshot, alpha,
                                       &host->render_ghost);
    }
    if (host->replay_active)
    {
        host->render_snapshot = host->replay_snapshot;
        if (smooth && host->history_ready)
            portable_trial_interpolate(&host->previous_replay, &host->replay_snapshot,
                                       portable_frame_scheduler_alpha(&host->scheduler),
                                       &host->render_snapshot);
    }
    if (!trial_resize_surface(host))
    {
        host->error = 1;
        return 0;
    }
    PortableLapBoardHud *capture_hud = !host->replay_active && host->finish_saved &&
                                               host->board_flow.state >= 2u &&
                                               !host->board_hud_ready
                                           ? &host->board_hud
                                           : NULL;
    if (!render_time_trial_with_board(
            scene, &host->render_snapshot,
            host->ghost_active && !host->replay_active ? &host->render_ghost : NULL,
            host->ghost_active && !host->replay_active ? host->ghost_character : NULL, &host->frame,
            host->pause_menu.cursor, host->pause_menu.open_ticks, host->pause_menu.palette_clock,
            (uint8_t)(host->replay_active            ? 0u
                      : host->board_flow.state >= 4u ? 2u
                                                     : host->board_visible),
            capture_hud, &host->pause_menu.graphics))
    {
        host->error = 1;
        return 0;
    }
    if (capture_hud)
        host->board_hud_ready = 1u;
    return 1;
}

/* The impact capture retains its original immediate API error return;
 * other capture failures set host->error and use the common frame epilogue. */
static int trial_capture_world_effects(PortableTimeTrialHost *host)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    if (!host->replay_active && host->snapshot.snow_weather.enabled &&
        host->session.runtime.session.phase == PORTABLE_RACE_PHASE_RACING)
    {
        const char *name = host->session.scene_clock < 240u   ? "tt-snow-0"
                           : host->session.scene_clock < 360u ? "tt-snow-1"
                                                              : "tt-snow-2";
        if (!portable_screen_capture(&host->frame, name))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active &&
        host->session.driver.source_hazard_kind == PORTABLE_DRIVER_HAZARD_VOID)
    {
        const char *sink = host->snapshot.fall_mode == 2u && host->snapshot.fall_frames_76 == 14u &&
                                   !host->snapshot.driver_hidden
                               ? "tt-sink-14"
                           : host->snapshot.fall_mode == 2u &&
                                   host->snapshot.fall_frames_76 == 15u &&
                                   host->snapshot.driver_hidden
                               ? "tt-sink-15"
                           : host->snapshot.fall_mode == 6u && !host->snapshot.fall_frames_76 &&
                                   !host->snapshot.driver_hidden
                               ? "tt-sink-return"
                               : NULL;
        if (sink != NULL && !portable_screen_capture(&host->frame, sink))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && host->snapshot.recovery_fx.visible)
    {
        const char *effect =
            (host->session.hazard_effect_72 & 15u) == 12u && host->snapshot.palette_bank == 9u
                ? "tt-ice-frozen"
            : host->snapshot.recovery_fx.thaw_age >= 2u ? "tt-ice-thaw"
                                                        : NULL;
        if (effect != NULL && !portable_screen_capture(&host->frame, effect))
        {
            host->error = 1;
            return 0;
        }
        for (unsigned i = 0u; i < PORTABLE_RECOVERY_DRIPS; ++i)
            if (host->snapshot.recovery_fx.drips[i].age >= 2u &&
                !portable_screen_capture(&host->frame, "tt-water-drip"))
            {
                host->error = 1;
                return 0;
            }
        if (host->error)
            return 0;
    }
    if (!host->replay_active)
        for (unsigned i = 0u; i < PORTABLE_IMPACT_EFFECT_SLOTS; ++i)
        {
            const PortableImpactEffectSlot *impact = &host->snapshot.impact_effects.slots[i];
            if (impact->active && impact->age >= 2u)
            {
                char name[24];
                (void)snprintf(name, sizeof name, "tt-impact-%u", impact->variant);
                if (!portable_screen_capture(&host->frame, name))
                    return -1;
            }
        }
    if (!host->replay_active && host->snapshot.surface_trails.visible)
    {
        int trail_drawn = 0;
        for (unsigned i = 0u; i < PORTABLE_SURFACE_TRAIL_SLOTS; ++i)
        {
            const PortableSurfaceTrailSlot *slot = &host->snapshot.surface_trails.slots[i];
            if (slot->age && (slot->age & 3u) && slot->kind >= 1u && slot->kind <= 5u)
            {
                trail_drawn = 1;
                char name[24];
                (void)snprintf(name, sizeof name, "tt-trail-kind%u", slot->kind);
                if (!portable_screen_capture(&host->frame, name))
                {
                    host->error = 1;
                    return 0;
                }
            }
        }
        if (host->error)
            return 0;
        if (trail_drawn && host->snapshot.surface_trails.extra_spray &&
            !portable_screen_capture(&host->frame, "tt-trail-spray"))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && host->snapshot.finish_camera.phase == 10u &&
        !portable_screen_capture(&host->frame, "tt-finish-camera"))
    {
        host->error = 1;
        return 0;
    }
    if (!host->replay_active && getenv("MKSC_CAPTURE_AMBIENT") != NULL &&
        host->snapshot.ambient.initialized && host->session.ambient.post_serial >= 10u)
    {
        const uint32_t serial = host->session.ambient.post_serial;
        const char *name = serial < 30u   ? "tt-ambient-0"
                           : serial < 50u ? "tt-ambient-1"
                                          : "tt-ambient-2";
        if (!portable_screen_capture(&host->frame, name))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && host->snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
        host->snapshot.speed_q16 > 65536 && host->snapshot.art_motion.wheel_palette_valid)
    {
        const uint8_t wheel = host->snapshot.art_motion.wheel_phase;
        const char *name = wheel == 0u   ? "tt-wheels-0"
                           : wheel == 1u ? "tt-wheels-1"
                                         : "tt-wheels-2";
        if (!portable_screen_capture(&host->frame, name))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && scene->presentation.driver_projection != NULL &&
        host->session.driver.hop.mode == 1u && host->snapshot.height_q16 >= 16384 &&
        (!portable_screen_capture(&host->frame, "tt-airborne") ||
         !portable_race_hop_capture(&host->frame, "tt", host->snapshot.height_q16)))
    {
        host->error = 1;
        return 0;
    }
    if (scene->presentation.entry_splash != NULL &&
        scene->presentation.entry_splash->frame_count != 0u &&
        host->snapshot.entry_splash.age >= 20u && host->snapshot.entry_splash.age <= 28u &&
        !portable_screen_capture(&host->frame, host->session.driver.source_hazard_kind ==
                                                       PORTABLE_DRIVER_HAZARD_LAVA
                                                   ? "tt-lava-entry"
                                                   : "tt-water-entry"))
    {
        host->error = 1;
        return 0;
    }
    if (!host->replay_active && host->snapshot.affine.mode != 0u &&
        (host->snapshot.affine.inverse_x_q8 != 256u || host->snapshot.affine.inverse_y_q8 != 256u))
    {
        const char *effect =
            host->snapshot.affine.mode == 1u   ? "tt-landing"
            : host->snapshot.affine.mode == 4u ? "tt-unshrink"
            : host->snapshot.affine.mode == 7u && host->snapshot.affine.inverse_y_q8 != 384u
                ? "tt-crush-rebound"
                : NULL;
        if (effect != NULL && !portable_screen_capture(&host->frame, effect))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && host->snapshot.phase == PORTABLE_RACE_PHASE_COUNTDOWN &&
        host->snapshot.countdown_remaining <= 100u &&
        !portable_screen_capture(&host->frame, "tt-countdown"))
    {
        host->error = 1;
        return 0;
    }
    return 1;
}

static int trial_draw_result_overlays(PortableTimeTrialHost *host)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    const char *script_message = portable_script_host_message(scene->scripts);
    if (!host->replay_active && !host->pause_menu_active && script_message != NULL)
    {
        PortableSoftwareFrame centered =
            portable_software_frame_native_view(&host->frame, PORTABLE_FRAME_CENTER);
        if (!portable_menu_notice(scene->frontend_pack, &centered, script_message) ||
            !portable_screen_capture(&host->frame, "tt-script-message"))
        {
            host->error = 1;
            return 0;
        }
    }
    if (host->replay_active)
    {
        PortableSoftwareFrame right =
            portable_software_frame_native_view(&host->frame, PORTABLE_FRAME_RIGHT);
        PortableAssetSectionView label;
        if (portable_asset_pack_find_section(scene->frontend_pack,
                                             PORTABLE_ASSET_SECTION_FRONTEND_GRAPHICS, 0x27040u,
                                             &label) == PORTABLE_ASSET_PACK_OK &&
            portable_frontend_image_draw(scene->frontend_pack, 0x27040u, &right, 168, 16, 0, 0, 240,
                                         160) != PORTABLE_FRONTEND_PRESENTATION_OK)
        {
            host->error = 1;
            return 0;
        }
        if (host->replay_snapshot.phase == PORTABLE_RACE_PHASE_FINISHED &&
            !render_trial_speedometer(scene, &host->replay_snapshot, &host->frame))
        {
            host->error = 1;
            return 0;
        }
        if (!portable_screen_capture(&host->frame, "tt-replay") ||
            (host->replay_snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
             host->replay_snapshot.total_ticks >= 300u &&
             !portable_screen_capture(&host->frame, "tt-replay-active")))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && host->finish_saved &&
        host->snapshot.phase == PORTABLE_RACE_PHASE_FINISHED)
    {
        if (scene->lap_board != NULL && !render_trial_results(host))
        {
            host->error = 1;
            return 0;
        }
        if (host->board_visible && (host->board_display.reveal_progress == 1u ||
                                    host->board_display.reveal_progress == 3u ||
                                    host->board_display.reveal_progress == 7u))
        {
            char name[24];
            (void)snprintf(name, sizeof name, "tt-lap-slide-%u",
                           host->board_display.reveal_progress);
            if (!portable_screen_capture(&host->frame, name))
            {
                host->error = 1;
                return 0;
            }
        }
        if (!render_ghost_save(host))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && host->finish_saved)
    {
        const uint32_t ticks = host->session.scene_clock - host->finish_scene_clock;
        const char *name = ticks >= 220u  ? "tt-after-finish-220"
                           : ticks >= 80u ? "tt-after-finish-80"
                           : ticks >= 32u ? "tt-after-finish-32"
                                          : NULL;
        if (name && !portable_screen_capture(&host->frame, name))
        {
            host->error = 1;
            return 0;
        }
    }
    return 1;
}

static int trial_capture_frame_end(PortableTimeTrialHost *host)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    if (scene->input_aid && scene->pause_capture_path != NULL && host->snapshot.paused &&
        getenv("MKSC_SCREEN_DIR") != NULL)
    {
        const unsigned opening = host->pause_menu.open_ticks;
        if (opening == 1u || opening == 4u || opening == 8u)
        {
            char name[24];
            snprintf(name, sizeof name, "tt-pause-%u", opening);
            if (!portable_screen_capture(&host->frame, name))
            {
                host->error = 1;
                return 0;
            }
        }
    }
    if (!host->pause_captured && scene->pause_capture_path != NULL && host->snapshot.paused &&
        host->pause_menu.open_ticks >= PORTABLE_PAUSE_MENU_OPEN_TICKS)
    {
        if (!portable_software_frame_write_ppm(&host->frame, scene->pause_capture_path))
        {
            host->error = 1;
            return 0;
        }
        host->pause_captured = 1;
    }
    if (scene->input_display && !host->replay_active && !host->snapshot.paused &&
        host->snapshot.phase != PORTABLE_RACE_PHASE_FINISHED)
    {
        int16_t stick_x, stick_y;
        portable_sdl_gamepad_stick(host->controller, &stick_x, &stick_y);
        portable_input_display_render_corner(&host->frame, host->display_buttons, stick_x, stick_y);
    }
    if (!host->replay_active && host->snapshot.sky_motion.initialized &&
        host->snapshot.phase == PORTABLE_RACE_PHASE_RACING && host->snapshot.total_ticks >= 300u)
    {
        char sky_capture[40];
        (void)snprintf(sky_capture, sizeof sky_capture, "tt-sky-%u",
                       host->snapshot.compatibility.course_id);
        if (!portable_screen_capture(&host->frame, sky_capture))
        {
            host->error = 1;
            return 0;
        }
    }
    if (!host->replay_active && !host->snapshot.paused &&
        host->snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
        !portable_race_motion_capture(&host->frame, "tt", host->snapshot.total_ticks))
    {
        host->error = 1;
        return 0;
    }
    const char *capture = getenv("MKSC_RACE_FRAME");
    if (!host->race_frame_captured && host->snapshot.total_ticks >= 300u && capture != NULL &&
        capture[0] != '\0')
    {
        if (!portable_software_frame_write_ppm(&host->frame, capture))
        {
            host->error = 1;
            return 0;
        }
        host->race_frame_captured = 1u;
    }
    return 1;
}

static int trial_frame_present(PortableTimeTrialHost *host, const TrialFrameStep *step)
{
    const PortableRecoveredTimeTrialHostScene *scene = &host->scene;
    const int smooth = scene->interpolate && host->focused &&
                       (host->replay_active || !host->session.paused) &&
                       host->prelude_remaining == 0u;
    if ((smooth && !portable_present_due(host->scheduler.last_ns, scene->render_fps,
                                         &host->last_present_ns)) ||
        (!smooth && step->presentation_tick_count == 0u && !host->needs_present))
    {
        SDL_Delay(1);
        return 0;
    }
    /* The modern board owns an immutable race picture. Continue source
     * result clocks, but avoid drawing a hidden world again on every turn. */
    const int frozen_results = scene->modern_menus && host->modern.results_active &&
        !host->replay_active && host->finish_saved && host->snapshot.phase == PORTABLE_RACE_PHASE_FINISHED;
    if (!frozen_results && !trial_build_frame(host, smooth))
        return 0;
    const int captured = frozen_results ? 1 : trial_capture_world_effects(host);
    if (captured < 0)
        return -1;
    if (!captured || !trial_draw_result_overlays(host) || !trial_capture_frame_end(host))
        return 0;
    const int modern_pause = scene->modern_menus && host->session.paused && host->pause_menu_active &&
                             !host->replay_active;
    if (!modern_pause) modern_menu_host_pause_closed(&host->modern);
    const int modern_results = scene->modern_menus && !host->replay_active && host->finish_saved &&
                               host->snapshot.phase == PORTABLE_RACE_PHASE_FINISHED;
    if (!modern_results) modern_menu_host_results_closed(&host->modern);
    const uint32_t countdown = scene->race->rules.countdown_ticks;
    const uint32_t entry_elapsed = (uint32_t)(scene->prelude_ticks - host->prelude_remaining) +
        (host->snapshot.countdown_remaining <= countdown ? countdown - host->snapshot.countdown_remaining : 0u);
    const int modern_entry = scene->modern_menus && !host->replay_active && !modern_pause &&
        !host->session.paused && host->snapshot.phase == PORTABLE_RACE_PHASE_COUNTDOWN && entry_elapsed < 48u;
    const ModernMenuTransition entry = {.pack = scene->frontend_pack, .title = "Time Trial",
        .subtitle = "Ready to race", .course_id = host->snapshot.compatibility.course_id,
        .elapsed = entry_elapsed, .duration = 48u, .kind = MODERN_TRANSITION_ENTRY};
    if (!(modern_entry ? modern_menu_host_present_transition(&host->modern, host->renderer, &host->frame,
            scene->fit_scaling, &entry, "tt-entry-modern")
        : modern_results ? present_modern_trial_results(host) : modern_pause ? modern_menu_host_present_pause(&host->modern, host->renderer, &host->frame,
                                                         scene->fit_scaling, &host->pause_menu, "Time Trial",
                                                         "tt-pause-modern")
                       : portable_software_frame_present(
                             host->renderer, host->render_texture != NULL ? host->render_texture : host->texture,
                             &host->frame, scene->fit_scaling)))
    {
        host->error = 1;
        return 0;
    }
    if (!scene->input_aid)
        SDL_Delay(smooth && scene->render_fps == 0u ? 0u : 1u);
    return 1;
}

int portable_time_trial_host_frame(PortableTimeTrialHost *host)
{
    if (host == NULL || host->error)
        return -1;
    if (host->quit)
        return 1;
    const unsigned lifecycle = portable_mobile_lifecycle_take();
    if (lifecycle & PORTABLE_MOBILE_BACKGROUND) {
        portable_mobile_touch_clear();
        host->mobile_pause_cancel = 0u; host->mobile_pressed = 0u;host->mobile_results_back=0u;
        SDL_ResetKeyboard();
        if (host->replay_active) stop_result_replay(host);
        const SDL_Event lost = {.type = SDL_EVENT_WINDOW_FOCUS_LOST};
        if (host->focused) portable_time_trial_host_event(host, &lost);
        if (host->recording != NULL && fflush(host->recording) != 0) host->error = 1;
    }
    if (lifecycle & PORTABLE_MOBILE_FOREGROUND) {
        const SDL_Event gained = {.type = SDL_EVENT_WINDOW_FOCUS_GAINED};
        portable_time_trial_host_event(host, &gained);
        portable_frame_scheduler_reset(&host->scheduler, time_trial_host_now(host), 0);
        portable_frame_scheduler_reset(&host->presentation_scheduler, time_trial_host_now(host), host->focused);
    }
    if (lifecycle & PORTABLE_MOBILE_TERMINATING) {
        portable_mobile_touch_clear();
        if (host->recording != NULL && fflush(host->recording) != 0) host->error = 1;
        host->quit = 1;
        host->outcome.quit_requested = 1;
        return host->error ? -1 : 1;
    }
    if (host->error) return -1;
    if (host->session.paused || host->finish_saved)
        (void)portable_mobile_touch_take_driving_pressed();
    if (!host->focused || host->session.paused || host->prelude_remaining != 0u)
        host->history_ready = 0u;
    TrialFrameStep step = {.now = host->scene.input_aid ? (host->aid_now += UINT64_C(16742707))
                                                        : time_trial_host_now(host)};
    if (!trial_frame_input_aid(host))
        return -1;
    if (!trial_advance_race_ticks(host, step.now))
        goto frame_complete;
    if (!trial_advance_presentation_ticks(host, step.now, &step.presentation_tick_count))
        goto frame_complete;
    if (trial_frame_present(host, &step) < 0)
        return -1;
frame_complete:
    host->needs_present = 0;
    return host->error ? -1 : host->quit ? 1 : 0;
}

#endif

int portable_recovered_time_trial_host_run(const PortableRecoveredTimeTrialHostScene *scene,
                                           PortableRecoveredTimeTrialHostOutcome *outcome)
{
#ifdef PORTABLE_ENABLE_SDL3
    if (outcome == NULL)
        return 1;
    PortableTimeTrialHost *host = portable_time_trial_host_begin(scene);
    if (host == NULL)
        return 1;
    for (;;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            portable_time_trial_host_event(host, &event);
        if (portable_time_trial_host_frame(host) != 0)
            break;
    }
    return portable_time_trial_host_end(host, outcome);
#else
    (void)scene;
    (void)outcome;
    fprintf(stderr, "Recovered Time Trial requires the SDL build.\n");
    return 1;
#endif
}
