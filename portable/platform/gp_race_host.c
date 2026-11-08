#include "mobile_lifecycle.h"
#include "mobile_touch.h"
#include "game_host_internal.h"
#include "character_menu_renderer.h"
#include "gp_countdown_renderer.h"
#include "gp_opening_renderer.h"
#include "gp_entry.h"
#include "race_interpolation.h"
#include "frontend_image_renderer.h"
#include "pause_menu.h"
#include "pause_menu_renderer.h"
#include "modern_menu_host.h"
#include "input_display.h"
#include "menu_font.h"
#ifdef PORTABLE_ENABLE_TEST_HOOKS
#include "text_number.h"
#endif

#ifdef PORTABLE_ENABLE_SDL3

/* Bounded aid diagnostic: hash typed published race state and RNG after
 * each fixed tick, excluding padding, pointers and presentation interpolation. */
static uint64_t hash_word(uint64_t hash, uint64_t value)
{
    for (unsigned byte = 0u; byte < 8u; ++byte)
    {
        hash = (hash ^ (value & 255u)) * UINT64_C(1099511628211);
        value >>= 8u;
    }
    return hash;
}
#ifdef PORTABLE_ENABLE_TEST_HOOKS
static int digest_trace_request(uint64_t *course, uint64_t *tick)
{
    const char *text = getenv("MKSC_GP_DIGEST_TRACE");
    if (!text) return 0;
    const char *cursor = text, *end = text + strlen(text);
    if (!portable_text_u64(&cursor,end,10,UINT32_MAX,course) ||
        cursor == end || *cursor++ != ':' ||
        !portable_text_u64(&cursor,end,10,59999u,tick) || cursor != end) return -1;
    return 1;
}
#endif

static uint64_t aid_tick_digest(uint64_t hash, const PortableGpRaceSnapshot *s, uint32_t rng,
                                uint32_t course_id, uint32_t host_tick)
{
#ifdef PORTABLE_ENABLE_TEST_HOOKS
    uint64_t course = 0u, tick = 0u;
    const int trace = digest_trace_request(&course,&tick) == 1 && course == course_id;
    unsigned word = 0u;
#define VALUE(value) do { \
    const uint64_t digest_value = (uint64_t)(value); \
    hash = hash_word(hash,digest_value); \
    if (trace && tick == host_tick) \
        fprintf(stderr,"GP digest word: course=%u tick=%u word=%u name=%s value=%016llx\n", \
            course_id,host_tick,word,#value,(unsigned long long)digest_value); \
    ++word; \
} while (0)
#else
    (void)course_id; (void)host_tick;
#define VALUE(value) hash = hash_word(hash, (uint64_t)(value))
#endif
    if (s->modern_drift_enabled)
    {
        VALUE(s->modern_drift.charge_ticks);
        VALUE(s->modern_drift.boost_ticks);
        VALUE(s->modern_drift.charge_tier);
        VALUE(s->modern_drift.boost_tier);
        VALUE(s->modern_drift.previous_held);
        VALUE(s->modern_drift.last_applied_tier);
        VALUE(s->modern_drift_release_serial);
    }
    if (s->modern_air_enabled)
    {
        VALUE(s->modern_air.air_ticks);
        VALUE(s->modern_air.boost_ticks);
        VALUE(s->modern_air.armed);
        VALUE(s->modern_air.tricked);
        VALUE(s->modern_air.previous_hop);
        VALUE(s->modern_air.last_applied);
    }
    VALUE(rng);
    VALUE(s->player_x_q16);
    VALUE(s->player_y_q16);
    VALUE(s->player_height_q16);
    VALUE(s->player_speed_q16);
    VALUE(s->player_heading);
    VALUE(s->camera_yaw);
    VALUE(s->finished_target_angle_ca);
    VALUE(s->finished_initialized);
    VALUE(s->finished_route_row_191);
    VALUE(s->finish_camera.phase);
    VALUE(s->finish_camera.orbit_yaw);
    VALUE(s->finish_camera.hold_ticks);
    VALUE(s->finish_camera.snapshot.x_q16);
    VALUE(s->finish_camera.snapshot.y_q16);
    VALUE(s->finish_camera.snapshot.yaw);
    VALUE(s->finish_camera.snapshot.vertical_offset);
    VALUE(s->finish_camera.snapshot.active);
    VALUE(s->finish_camera.snapshot.request_celebration);
    VALUE(s->finish_camera.snapshot.cancel_effects);
    VALUE(s->player_art_pose.angle);
    VALUE(s->player_art_pose.frame_override);
    VALUE(s->player_art_pose.mirror);
    VALUE(s->player_palette_bank);
    VALUE(s->player_flattened);
    VALUE(s->player_affine.mode);
    VALUE(s->player_affine.row);
    VALUE(s->player_affine.remaining);
    VALUE(s->player_affine.pending_mode);
    VALUE(s->player_art_motion.countdown);
    VALUE(s->player_art_motion.wheel_phase);
    VALUE(s->player_art_motion.bob_phase);
    VALUE(s->player_art_motion.wheel_palette_bank);
    VALUE(s->player_art_motion.wheel_palette_valid);
    VALUE(s->player_art_motion.initialized);
    VALUE(s->player_art_motion.cached_bob_offset);
    VALUE(s->player_affine.inverse_x_q8);
    VALUE(s->player_affine.inverse_y_q8);
    VALUE(s->sky_motion.scroll_x[0]);
    VALUE(s->sky_motion.scroll_x[1]);
    VALUE(s->sky_motion.scroll_x[2]);
    VALUE(s->sky_motion.scroll_y[0]);
    VALUE(s->sky_motion.scroll_y[1]);
    VALUE(s->sky_motion.scroll_y[2]);
    VALUE(s->sky_motion.layer_mask);
    VALUE(s->sky_motion.blend_mode);
    VALUE(s->sky_motion.blend_a);
    VALUE(s->sky_motion.blend_b);
    VALUE(s->sky_motion.split_y);
    VALUE(s->sky_motion.split_layer);
    VALUE(s->sky_motion.split_scroll_x);
    VALUE(s->sky_motion.layer_source[0]);
    VALUE(s->sky_motion.layer_source[1]);
    VALUE(s->sky_motion.layer_source[2]);
    VALUE(s->sky_motion.initialized);
    VALUE(s->sky_motion.road_line);
    VALUE(s->sky_motion.road_mode);
    VALUE(s->sky_motion.split_scroll_y);
    VALUE(s->sky_motion.split_vertical);
    VALUE(s->player_kart_contacts);
    VALUE(s->ambient_digest);
    VALUE(s->snow_weather_digest);
    VALUE(s->main_visual_rng_state);
    VALUE(portable_lava_smoke_digest(&s->lava_smoke));
    VALUE(portable_surface_trail_digest(&s->surface_trails));
    VALUE(portable_coin_effect_digest(&s->coin_effects));
    VALUE(portable_impact_effect_digest(&s->impact_effects));
    VALUE(portable_recovery_fx_digest(&s->recovery_fx));
    VALUE(s->recovery_fx_events);
    VALUE(s->recovery_effect_72);
    VALUE(s->engine_sound.ice_thaw_started);
    VALUE(s->player_view_angle);
    VALUE(s->player_star_active);
    VALUE(s->player_star_uses);
    VALUE(s->lightning_uses);
    VALUE(s->lightning_music_active);
    VALUE(s->lightning_scene_active);
    VALUE(s->lightning_scene_white);
    VALUE(s->music_finished);
    VALUE(s->music_final_lap);
    VALUE(s->phase);
    VALUE(s->laps_completed);
    VALUE(s->lap_count);
    VALUE(s->countdown_remaining);
    VALUE(s->player_route_index);
    VALUE(s->player_rank);
    VALUE(s->finish_transition_phase);
    VALUE(s->results_ready);
    VALUE(s->total_ticks);
    VALUE(s->source_clock_centiseconds);
    VALUE(s->source_clock_phase);
    VALUE(s->source_result_times_ready);
    VALUE(s->rescue_count);
    VALUE(s->recovery_phase);
    VALUE(s->recovery_elapsed_ticks);
    VALUE(s->recovery_phase_ticks);
    VALUE(s->audio_event_count);
    VALUE(s->item_hud);
    VALUE(s->rank_hud.initialized);
    VALUE(s->rank_hud.pending_rank);
    VALUE(s->rank_hud.displayed_rank);
    VALUE(s->rank_hud.badge_delay);
    VALUE(s->rank_hud.color_timer);
    VALUE(s->coin_hud.clock);
    VALUE(s->coin_hud.initialized);
    for (unsigned i = 0u; i < 52u; ++i)
    {
        VALUE(s->rank_hud.pending_cells[i]);
        VALUE(s->rank_hud.displayed_cells[i]);
    }
    for (unsigned i = 0u; i < 4u; ++i)
    {
        VALUE(s->rank_hud.pending_colors[i]);
        VALUE(s->rank_hud.displayed_colors[i]);
        VALUE(s->rank_hud.portrait_cache[i]);
    }
    for (unsigned i = 0u; i < 2u; ++i)
    {
        VALUE(s->rank_hud.pending_separator_colors[i]);
        VALUE(s->rank_hud.displayed_separator_colors[i]);
    }
    for (unsigned i = 0u; i < 3u; ++i)
    {
        VALUE(s->coin_hud.pending_cells[i]);
        VALUE(s->coin_hud.displayed_cells[i]);
        VALUE(s->coin_hud.pending_colors[i]);
        VALUE(s->coin_hud.displayed_colors[i]);
    }
    VALUE(s->coin_hud.warning);
    VALUE(s->lap_hud.pending_lap);
    VALUE(s->lap_hud.displayed_lap);
    VALUE(s->lap_hud.initialized);

    VALUE(s->lakitu_x);
    VALUE(s->lakitu_y);
    VALUE(s->lakitu_visible);
    VALUE(s->lakitu_art_bank);
    VALUE(s->lakitu_scale_q8);
    VALUE(s->fall_mode);
    VALUE(s->fall_timer);
    VALUE(s->fall_frames_76);
    VALUE(s->driver_hidden);
    for (size_t i = 0u; i < PORTABLE_RACE_MAX_LAPS; ++i)
        VALUE(s->lap_ticks[i]);
    for (size_t i = 0u; i < PORTABLE_RACE_MAX_LAPS + 1u; ++i)
        VALUE(s->source_result_times_centiseconds[i]);
    for (unsigned i = 0u; i < PORTABLE_COIN_PICKUP_EFFECT_CAPACITY; ++i)
    {
        const PortableCoinPickupEffect *effect = &s->coin_pickups[i];
        VALUE(effect->x);
        VALUE(effect->y_q16);
        VALUE(effect->upward_speed_q16);
        VALUE(effect->age);
    }
    VALUE(s->boost_particle_age);
    VALUE(s->boost_particle_birth_height_q16);
    VALUE(s->boost_voice.serial);
    VALUE(s->boost_voice.song_id);
    VALUE(s->boost_voice.stop_song);
    VALUE(s->boost_voice.volume);
    VALUE(s->boost_voice.pitch);
    VALUE(s->boost_voice.restart);
    VALUE(s->contact_audio.serial);
    VALUE(s->contact_audio.song_id);
    VALUE(s->contact_audio.stop_song);
    VALUE(s->contact_audio.volume);
    VALUE(s->contact_audio.pitch);
    VALUE(s->contact_audio.restart);
    VALUE(s->falling_stars.count);
    for (size_t i = 0; i < s->falling_stars.count; ++i)
    {
        const PortableFallingStar *a = &s->falling_stars.actors[i];
        VALUE(a->x_q16);
        VALUE(a->y_q16);
        VALUE(a->height_q16);
        VALUE(a->initial_x_q16);
        VALUE(a->offset_q16);
        VALUE(a->velocity_x_q16);
        VALUE(a->velocity_z_q16);
        VALUE(a->timer);
        VALUE(a->delay);
        VALUE(a->affine_q8);
        VALUE(a->angle);
        VALUE(a->state);
        VALUE(a->variant);
        VALUE(a->checkpoint);
        VALUE(a->placement_order);
        VALUE(a->flags);
        VALUE(a->art);
    }
    VALUE(s->thunderclouds.count);
    VALUE(s->thunderclouds.palette_phase[0]);
    VALUE(s->thunderclouds.palette_phase[1]);
    for (size_t i = 0; i < s->thunderclouds.count; ++i)
    {
        const PortableThundercloud *a = &s->thunderclouds.actors[i];
        VALUE(a->x_q16);
        VALUE(a->y_q16);
        VALUE(a->height_q16);
        VALUE(a->state);
        VALUE(a->timer);
        VALUE(a->palette_timer);
        VALUE(a->variant);
        VALUE(a->checkpoint);
        VALUE(a->placement_order);
        VALUE(a->flags);
        VALUE(a->art);
    }
    VALUE(s->mice.count);
    for (size_t i = 0; i < s->mice.count; ++i)
    {
        const PortableMouse *a = &s->mice.actors[i];
        VALUE(a->x_q16);
        VALUE(a->y_q16);
        VALUE(a->height_q16);
        VALUE(a->velocity_x_q16);
        VALUE(a->velocity_y_q16);
        VALUE(a->velocity_z_q16);
        VALUE(a->facing_x_q16);
        VALUE(a->facing_y_q16);
        VALUE(a->timer);
        VALUE(a->affine_y_q8);
        VALUE(a->variant);
        VALUE(a->checkpoint);
        VALUE(a->placement_order);
        VALUE(a->flags);
        VALUE(a->art);
        VALUE(a->walk_phase);
        VALUE(a->walk_ticks);
        VALUE(a->hop_phase);
        VALUE(a->turn_flip);
    }
    VALUE(s->course_props.count);
    for (size_t i = 0; i < s->course_props.count; ++i)
    {
        const PortableCourseProp *a = &s->course_props.actors[i];
        VALUE(a->x_q16);
        VALUE(a->y_q16);
        VALUE(a->height_q16);
        VALUE(a->source_code);
        VALUE(a->timer);
        VALUE(a->state);
        VALUE(a->flags);
        VALUE(a->affine_x_q8);
        VALUE(a->affine_y_q8);
        VALUE(a->art_index);
        VALUE(a->checkpoint);
        VALUE(a->placement_order);
        VALUE(a->contact_phase);
        VALUE(a->mechakoopa.velocity_x_q16);
        VALUE(a->mechakoopa.cycle);
        VALUE(a->mechakoopa.heading);
        VALUE(a->mechakoopa.animation_ticks);
        VALUE(a->mechakoopa.animation_phase);
        VALUE(a->mechakoopa.has_facing);
        VALUE(a->kamek.velocity_x_q16);
        VALUE(a->kamek.velocity_z_q16);
        VALUE(a->kamek.phase);
        VALUE(a->kamek.heading);
        VALUE(a->kamek.has_facing);
        VALUE(a->pier_boo.velocity_z_q16);
        VALUE(a->pier_boo.phase);
        VALUE(a->teepee.velocity_x_q16);
        VALUE(a->teepee.velocity_y_q16);
        VALUE(a->teepee.rider_x_q16);
        VALUE(a->teepee.rider_y_q16);
        VALUE(a->teepee.rider_z_q16);
        VALUE(a->teepee.rider_vx_q16);
        VALUE(a->teepee.rider_vy_q16);
        VALUE(a->teepee.rider_vz_q16);
        VALUE(a->teepee.rider_spin);
        VALUE(a->teepee.rider_spin_rate);
        VALUE(a->teepee.rider_gravity);
        VALUE(a->teepee.rider_heading);
        VALUE(a->teepee.counter);
        VALUE(a->teepee.frame);
        VALUE(a->teepee.kart);
        VALUE(a->teepee.rider_state);
        VALUE(a->lake_rock.home_x_q16);
        VALUE(a->lake_rock.home_y_q16);
        VALUE(a->lake_rock.velocity_z_q16);
        VALUE(a->lake_rock.timer);
        VALUE(a->lake_rock.delay);
        VALUE(a->lake_rock.frame);
    }
    VALUE(s->impact_sounds.serial);
    VALUE(s->impact_sounds.count);
    for (size_t i = 0u; i < s->impact_sounds.count; ++i)
    {
        const PortableCourseImpactSound *event = &s->impact_sounds.events[i];
        VALUE(event->character);
        VALUE(event->rival);
        VALUE(event->variant);
        VALUE(event->volume);
    }
    VALUE(s->thwomp_sounds.serial);
    VALUE(s->thwomp_sounds.count);
    VALUE(s->prop_sounds.serial);
    VALUE(s->prop_sounds.count);
    for (size_t i = 0u; i < s->prop_sounds.count; ++i)
    {
        VALUE(s->prop_sounds.songs[i]);
        VALUE(s->prop_sounds.volumes[i]);
    }
    for (size_t i = 0u; i < s->thwomp_sounds.count; ++i)
        VALUE(s->thwomp_sounds.volumes[i]);
    VALUE(s->piranha_sounds.serial);
    VALUE(s->piranha_sounds.count);
    for (size_t i = 0u; i < s->piranha_sounds.count; ++i)
        VALUE(s->piranha_sounds.songs[i]);
    VALUE(s->cannonball_sounds.serial);
    VALUE(s->cannonball_sounds.count);
    for (size_t i = 0u; i < s->cannonball_sounds.count; ++i)
    {
        VALUE(s->cannonball_sounds.events[i].song_id);
        VALUE(s->cannonball_sounds.events[i].volume);
        VALUE(s->cannonball_sounds.events[i].require_idle);
    }
    for (size_t i = 0u; i < s->audio_event_count; ++i)
    {
        VALUE(s->audio_events[i].song_id);
        VALUE(s->audio_events[i].volume);
        VALUE(s->audio_events[i].pitch);
        VALUE(s->audio_events[i].player);
        VALUE(s->audio_events[i].operation);
        VALUE(s->audio_events[i].music_effect);
    }
    for (size_t i = 0u; i < PORTABLE_GP_OPPONENT_COUNT; ++i)
    {
        const PortableGpOpponentSnapshot *opponent = &s->opponents[i];
        VALUE(opponent->position_x_q16);
        VALUE(opponent->position_y_q16);
        VALUE(opponent->height_q16);
        VALUE(opponent->engine_sound.state_flags);
        VALUE(opponent->engine_sound.speed_q16);
        VALUE(opponent->engine_sound.driver_mode);
        VALUE(opponent->engine_sound.lightning_timer);
        VALUE(opponent->engine_sound.character_flags);
        VALUE(opponent->engine_sound.bomb_walk_audio);
        VALUE(opponent->engine_doppler);
        VALUE(opponent->fall_audio_serial);
        VALUE(opponent->fall_audio_variant);
        VALUE(opponent->heading);
        VALUE(opponent->source_angle);
        VALUE(opponent->steering_response);
        VALUE(opponent->contact_mode);
        VALUE(opponent->contact_phase);
        VALUE(opponent->contact_frame);
        VALUE(opponent->contact_flags);
        VALUE(opponent->driver_class);
        VALUE(opponent->palette_bank);
        VALUE(opponent->flattened);
        VALUE(opponent->visible);
        VALUE(opponent->fall_frames_76);
        VALUE(opponent->driver_hidden);
        VALUE(opponent->affine.mode);
        VALUE(opponent->affine.row);
        VALUE(opponent->affine.remaining);
        VALUE(opponent->affine.pending_mode);
        VALUE(opponent->art_motion.countdown);
        VALUE(opponent->art_motion.wheel_phase);
        VALUE(opponent->art_motion.bob_phase);
        VALUE(opponent->art_motion.wheel_palette_bank);
        VALUE(opponent->art_motion.wheel_palette_valid);
        VALUE(opponent->art_motion.initialized);
        VALUE(opponent->art_motion.cached_bob_offset);
        VALUE(opponent->affine.inverse_x_q8);
        VALUE(opponent->affine.inverse_y_q8);
    }
#undef VALUE
#ifdef PORTABLE_ENABLE_TEST_HOOKS
    if (trace)
        fprintf(stderr,"GP digest tick: course=%u tick=%u hash=%016llx\n",
            course_id,host_tick,(unsigned long long)hash);
#endif
    return hash;
}

typedef struct {
    uint8_t stage, presented;
    uint16_t wait_frames;
    uint32_t source_clock, result_clock, source_rng, visual_rng, host_ticks;
    int32_t x_q16, y_q16, speed_q16;
    uint64_t digest;
} GpPauseAid;

struct PortableGpHost
{
    PortableRecoveredPackStorage *storage;
    PortablePresentationPackStorage *presentations;
    PortablePresentationPackView views[PORTABLE_GP_RACER_COUNT];
    PortableContentSelection selection;
    PortableRecoveredRaceView race;
    PortableGpRaceSession session;
    PortableGpRaceSnapshot snapshot;
    PortableRaceTickDisposition disposition;
    PortableRaceSceneView scene;
    PortableScene3D scene_3d;
    PortableModelCatalog models;
    uint32_t character_ids[PORTABLE_GP_RACER_COUNT];
    PortableNativeImage player_portrait, player_name;
    uint8_t *visual_world_map;
    uint32_t visual_wall_mutation_count;
    PortableRacePresentationState state, previous_state, render_state;
    PortableGpEntry entry;
    PortableGpFinishPortraits finish_portraits;
    uint16_t finish_portrait_ticks;
    uint32_t selected_finish_clock;
    uint8_t finish_owner_reported;
    PortableSkyMotion entry_sky;
    uint32_t *entry_hud_pixels;
    uint8_t history_ready;
    uint64_t last_present_ns, aid_digest;
    uint32_t aid_presents;
    PortableFrameScheduler scheduler;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Gamepad *controller;
    uint8_t paused, resume_armed, aid_freeze_done, aid_freeze_frames;
    GpPauseAid pause_aid;
    uint8_t mobile_results_aid_stage; /* 0 waiting, 1 lifecycle queued, 2 restored, 3 Back queued, 4 advanced */
    uint64_t aid_coin_freeze_digest;
    PortablePauseMenu pause_menu; /* 802C06C list chosen by mode; active only by request */
    uint8_t pause_menu_active, pause_retry_blink;
    ModernMenuHost modern; /* Modern pause/results overlays. */
    PortableFrameScheduler menu_scheduler;
    uint32_t pixels[240u * 160u];
    uint32_t *render_pixels;
    SDL_Texture *render_texture;
    uint8_t render_surface_ready;
    PortableSoftwareFrame frame;
    uint8_t race_frame_captured;
    uint8_t kart_contact_seen;
    /* Diagnostic moments use accepted fixed ticks, never render cadence. */
    PortableRacePresentationState hud_capture_previous;
    uint32_t hud_capture_mask, hud_roulette_start, hud_lap_start, hud_rank_start;
    uint32_t hud_digit_start, hud_color_start;
    uint8_t hud_capture_ready, hud_roulette_active;
    uint8_t finish_capture_mask;
    uint16_t hud_capture_gameplay_laps;

    PortableRumbleTracker rumble;
    uint16_t display_buttons;
    uint8_t skip_results; /* A/Start edge accepted by the post-race board. */
    uint8_t aid_result_confirm_stage;
    PortableUiTextBank ui_text;
    uint32_t host_ticks;
    uint32_t gp_failure_ready_ticks;
    uint8_t next_grid_ranks[PORTABLE_GP_RACER_COUNT];
    PortableGpTimeBoardFlow gp_time_board;
    PortableTimeBoardRow gp_time_rows[8];
    uint8_t gp_time_points[8], gp_time_awards[8];
    PortableGpResultsFlow gp_result_flow;
    PortableGpResultsFlow gp_result_display;
    uint16_t gp_result_times[PORTABLE_RACE_MAX_LAPS + 1u];
    uint8_t gp_result_visible;
    PortableLapBoardHud gp_result_hud;
    uint8_t gp_result_hud_ready;
    PortableGpClockEnableBoundary clock_boundary;
    uint64_t frequency, aid_now;
    uint32_t aid_results_frames;
    int owns_host;
    uint8_t session_ready;
    int focused, quit, app_quit, error, needs_present;
    PortableGpHostScene configuration;
    uint32_t playback_tick;
    PortableGpRaceHostOutcome outcome;
    int owns_sdl;
    uint8_t mobile_pause_cancel,mobile_results_continue;
    PortableGpResultsHost *results;
};

#ifdef PORTABLE_ENABLE_TEST_HOOKS
static void gp_trace_kinematics(const PortableGpHost *host, uint32_t host_tick, const char *stage)
{
    uint64_t course = 0u, tick = 0u;
    if (digest_trace_request(&course,&tick) != 1 ||
        course != host->configuration.course_id || tick != host_tick) return;
    const PortableDriverPilotState *pilot = &host->session.player;
    const int32_t *sine = host->session.player_dependencies.sine_table;
    fprintf(stderr,"GP pilot: course=%u tick=%u stage=%s x=%08x y=%08x "
        "heading=%04x source_heading=%04x sine507=%d sine508=%d sine773=%d sine772=%d\n",
        host->configuration.course_id,host_tick,stage,
        (unsigned)(uint32_t)pilot->pose.position_x,(unsigned)(uint32_t)pilot->pose.position_y,
        (unsigned)(uint16_t)pilot->motion.angle,(unsigned)pilot->steering.source_angle_56,
        (int)sine[507],(int)sine[508],(int)sine[773],(int)sine[772]);
    for (unsigned index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index) {
        const PortableGpRacer *r = &host->session.roster.racers[index];
        fprintf(stderr,"GP kinematics: course=%u tick=%u stage=%s racer=%u "
            "x=%08x y=%08x component_x=%d component_y=%d flags=%08x speed=%d "
            "source_heading=%04x heading=%04x acceleration=%d b4=%04x mode84=%u "
            "state78=%u contact5a=%04x contact_hold=%u banana_timer=%u height=%d\n",
            host->configuration.course_id,host_tick,stage,index,
            (unsigned)(uint32_t)r->position_x_q16,(unsigned)(uint32_t)r->position_y_q16,
            (int)r->component_x,(int)r->component_y,(unsigned)r->source_flags,(int)r->speed_q16,
            (unsigned)r->source_heading,(unsigned)r->heading,(int)r->acceleration_q16,
            (unsigned)r->mushroom_response_flags_b4,(unsigned)r->mushroom_mode_84,
            (unsigned)r->lightning_state_78,(unsigned)r->kart_contact_state_5a,
            (unsigned)r->course_contact_hold,(unsigned)r->banana_hit_timer_38,
            (int)r->world_height_q16);
    }
}
#endif

static int gp_hud_capture(PortableGpHost *host, const PortableRacePresentationState *state,
                          unsigned bit, const char *name)
{
    const uint32_t mask = UINT32_C(1) << bit;
    if (host->hud_capture_mask & mask)
        return 1;
    if (!portable_race_scene_render(&host->scene, state, &host->frame) ||
        !portable_screen_capture(&host->frame, name))
        return 0;
    host->hud_capture_mask |= mask;
    printf("GP HUD moment: %s clock=%u item=%04x lap=%u rank=%u coins=%u\n", name,
           host->session.source_frame_clock, state->gp_item, state->laps_completed, state->gp_rank,
           state->gp_coins);
    return 1;
}

static int gp_hud_moments(PortableGpHost *host)
{
    const char *directory = getenv("MKSC_SCREEN_DIR");
    const char *enabled = getenv("MKSC_CAPTURE_HUD");
    if (!directory || !*directory || !enabled || strcmp(enabled, "1") != 0)
        return 1;
    PortableRacePresentationState current;
    if (!portable_gp_race_presentation_build(&host->session, &host->snapshot, &current))
        return 0;
    const uint32_t clock = host->session.source_frame_clock;
    const PortableGpItemInventory *item =
        &host->session.roster.racers[host->session.local_index].item_inventory;
    const uint8_t roulette = (uint8_t)(item->held_item_17a == PORTABLE_GP_ITEM_ROULETTE);
    if (roulette && !host->hud_roulette_active)
        host->hud_roulette_start = host->session.item_art_clock.calls;
    if (roulette)
    {
        static const uint32_t ages[4] = {1u, 26u, 76u, 155u};
        static const char *const names[4] = {"gp-hud-roulette-1", "gp-hud-roulette-26",
                                             "gp-hud-roulette-76", "gp-hud-roulette-155"};
        const uint32_t age = host->session.item_art_clock.calls - host->hud_roulette_start + 1u;
        for (unsigned i = 0u; i < 4u; ++i)
            if (age == ages[i] && !gp_hud_capture(host, &current, i, names[i]))
                return 0;
    }
    else if (host->hud_roulette_active && item->held_item_17a != 0u &&
             !gp_hud_capture(host, &current, 4u, "gp-hud-item-settled"))
        return 0;
    host->hud_roulette_active = roulette;
    if (host->hud_capture_ready && host->snapshot.phase == PORTABLE_RACE_PHASE_RACING)
    {
        const PortableRacePresentationState *previous = &host->hud_capture_previous;
        if (host->snapshot.laps_completed != host->hud_capture_gameplay_laps)
        {
            host->hud_lap_start = clock;
            if (!gp_hud_capture(host, previous, 5u, "gp-hud-lap-pre") ||
                !gp_hud_capture(host, &current, 6u, "gp-hud-lap-post"))
                return 0;
        }
        if (host->hud_lap_start && clock - host->hud_lap_start == 7u &&
            !gp_hud_capture(host, &current, 7u, "gp-hud-lap-8"))
            return 0;
        if (host->hud_lap_start && clock - host->hud_lap_start == 15u &&
            !gp_hud_capture(host, &current, 8u, "gp-hud-lap-16"))
            return 0;
        if (current.gp_rank_hud.pending_rank != previous->gp_rank_hud.pending_rank)
        {
            if (!(host->hud_capture_mask & (UINT32_C(1) << 9u)))
                host->hud_rank_start = clock;
            if (!gp_hud_capture(host, &current, 9u, "gp-hud-rank-1"))
                return 0;
        }
        if (host->hud_rank_start && clock - host->hud_rank_start == 5u &&
            !gp_hud_capture(host, &current, 10u, "gp-hud-rank-6"))
            return 0;
        if (memcmp(current.gp_coin_hud.displayed_cells, previous->gp_coin_hud.displayed_cells,
                   sizeof current.gp_coin_hud.displayed_cells) != 0)
        {
            if (!(host->hud_capture_mask & (UINT32_C(1) << 11u)))
                host->hud_digit_start = clock;
            if (!gp_hud_capture(host, &current, 11u, "gp-hud-coin-digit-1"))
                return 0;
        }
        if (host->hud_digit_start && clock - host->hud_digit_start == 7u &&
            !gp_hud_capture(host, &current, 12u, "gp-hud-coin-digit-8"))
            return 0;
        if (memcmp(current.gp_coin_hud.displayed_colors, previous->gp_coin_hud.displayed_colors,
                   sizeof current.gp_coin_hud.displayed_colors) != 0)
        {
            if (!(host->hud_capture_mask & (UINT32_C(1) << 13u)))
                host->hud_color_start = clock;
            if (!gp_hud_capture(host, &current, 13u, "gp-hud-coin-color-1"))
                return 0;
        }
        if (host->hud_color_start && clock - host->hud_color_start == 7u &&
            !gp_hud_capture(host, &current, 14u, "gp-hud-coin-color-8"))
            return 0;
    }
    host->hud_capture_previous = current;
    host->hud_capture_ready = 1u;
    host->hud_capture_gameplay_laps = host->snapshot.laps_completed;
    return 1;
}

static int gp_board_confirm_allowed(const PortableGpHost *host)
{
    /* Short transitions may leave either finish route's board early. */
    if (host->configuration.fast_menus && host->configuration.recording == NULL &&
        portable_gp_cup_session_finish_route(&host->session) != PORTABLE_GP_CUP_FINISH_NOT_READY)
        return 1;
    return host->configuration.quick_run
               ? portable_gp_results_advance_allowed(host->gp_failure_ready_ticks,
                                                     host->configuration.fast_menus)
               : portable_gp_time_board_can_confirm(&host->gp_time_board);
}

static int gp_results_video(PortableGpHost *host)
{
    portable_gp_results_host_modern(host->results,host->configuration.pack,
        host->configuration.modern_menus,host->configuration.character_id,host->configuration.course_id);
    return portable_gp_results_host_video(host->results, host->configuration.internal_scale,
                                          host->configuration.aspect_width,
                                          host->configuration.fit_scaling,
                                          &host->frame);
}

/* Reuse an existing presentation interval; the card never delays the race. */
static int gp_modern_entry_active(const PortableGpHost *host, uint32_t *elapsed)
{
    if (!host->configuration.modern_menus) return 0;
    if (host->entry.initialized && !host->entry.snapshot.done)
        *elapsed = host->entry.ticks;
    else if (!host->entry.initialized && host->snapshot.phase == PORTABLE_RACE_PHASE_COUNTDOWN)
    {
        const uint32_t duration = host->race.rules.countdown_ticks;
        if (host->snapshot.countdown_remaining > duration) return 0;
        *elapsed = duration - host->snapshot.countdown_remaining;
    }
    else return 0;
    return *elapsed < 48u;
}

static int gp_present_modern_entry(PortableGpHost *host, uint32_t elapsed, const char *mode)
{
    const ModernMenuTransition view = {.pack = host->configuration.pack,
        .title = mode, .subtitle = "Ready to race", .course_id = host->configuration.course_id,
        .elapsed = elapsed, .duration = 48u, .kind = MODERN_TRANSITION_ENTRY,
        .gamepad = host->modern.gamepad};
    return modern_menu_host_present_transition(&host->modern, host->renderer, &host->frame,
        host->configuration.fit_scaling, &view, host->configuration.battle ? "battle-entry-modern"
        : host->configuration.quick_run ? "quick-entry-modern" : "gp-entry-modern");
}

static uint32_t gp_modern_driver_id(const PortableGpHost *host,uint8_t racer,uint8_t driver_class)
{
    if(racer==host->session.local_index)return host->configuration.character_id;
    PortableCharacterContent driver;
    return portable_content_registry_find_character_class(host->configuration.pack,driver_class,&driver)==PORTABLE_CONTENT_OK?driver.id:0u;
}

static int gp_present_modern_results(PortableGpHost *host)
{
    ModernMenuResults view={.pack=host->configuration.pack,.course_id=host->configuration.course_id,
        .title=host->configuration.quick_run?"Quick Run results":"Grand Prix results",
        .tick=host->configuration.quick_run?host->gp_failure_ready_ticks:host->gp_result_flow.total_ticks,
        .gamepad=host->modern.gamepad,.live_frame=1u,
        .item_count=1u,.labels={"Continue"}};
    const char *capture;
    char lap_labels[MODERN_MENU_RESULT_ROWS][16];
    if(!host->configuration.quick_run&&host->gp_time_board.visible){
        view.row_count=8u;view.tick=host->gp_time_board.ticks;
        for(unsigned rank=0u;rank<8u;++rank){
            const PortableTimeBoardRow *source=&host->gp_time_rows[rank];
            ModernMenuResultRow *row=&view.rows[rank];
            row->rank=rank+1u;row->highlighted=source->selected;
            row->driver_id=gp_modern_driver_id(host,source->racer_index,source->driver_class);
            modern_menu_host_format_time(row->value,sizeof row->value,source->value);
            if(host->gp_time_board.state>=10u)snprintf(row->detail,sizeof row->detail,"+%u points",host->gp_time_awards[source->racer_index]);
        }
        capture=host->gp_time_board.state>=10u?"gp-points-modern":"gp-time-board-modern";
    }else{
        view.row_count=(unsigned)host->snapshot.lap_count+1u;
        if(view.row_count>MODERN_MENU_RESULT_ROWS)return 0;
        for(unsigned i=0u;i<view.row_count;++i){
            ModernMenuResultRow *row=&view.rows[i];
            if(i<host->snapshot.lap_count){
                snprintf(lap_labels[i],sizeof lap_labels[i],"Lap %u",i+1u);
                row->name=lap_labels[i];
            }else row->name="Total";
            row->driver_id=host->configuration.character_id;row->highlighted=(uint8_t)(i==host->snapshot.lap_count);
            modern_menu_host_format_time(row->value,sizeof row->value,host->gp_result_times[i]);
        }
        capture=host->configuration.quick_run?"quick-laps-modern":"gp-laps-modern";
    }
    if(!gp_board_confirm_allowed(host))view.disabled_mask=1u;
    return modern_menu_host_present_results(&host->modern,host->renderer,&host->frame,
        host->configuration.fit_scaling,&view,capture);
}

/* 52F8C/52658 use private display copies; the prepared race grid and RNG
 * are untouched until 52A4C releases the normal countdown owner. */
static int gp_entry_pose(PortableGpHost *host)
{
    PortableRacePresentationState *state = &host->render_state;
    const PortableGpEntrySnapshot *entry = &host->entry.snapshot;
    if (host->scene.opponent_count > PORTABLE_RACE_SCENE_MAX_OPPONENTS) return 0;
    state->position_x_q16 = entry->drivers[0].x_q16;
    state->position_y_q16 = entry->drivers[0].y_q16;
    state->camera_position_override = 1u;
    state->camera_position_x_q16 = entry->camera_x_q16;
    state->camera_position_y_q16 = entry->camera_y_q16;
    state->camera_yaw = entry->camera_yaw;
    state->sky_motion = host->entry_sky.snapshot;
    state->suppress_native_hud = 1u;
    PortableCharacterViewInput input = {.camera_yaw = entry->camera_yaw,
                                        .driver_angle = state->heading,
                                        .source_flags = host->session.player.steering.state_flags,
                                        .selected = 1u};
    state->art_pose = portable_character_view_pose(&input, host->session.view_rules);
    state->art_pose_valid = 1u;
    for (size_t i = 0u; i < PORTABLE_RACE_SCENE_MAX_OPPONENTS && i < host->scene.opponent_count; ++i)
    {
        const size_t racer = portable_gp_race_session_other_racer(&host->session, i);
        if (racer >= PORTABLE_GP_ENTRY_DRIVERS || racer >= PORTABLE_GP_RACER_COUNT) return 0;
        state->opponents[i].position_x_q16 = entry->drivers[racer].x_q16;
        state->opponents[i].position_y_q16 = entry->drivers[racer].y_q16;
        input.driver_angle = state->opponents[i].heading;
        input.selected = 0u;
        input.source_flags = host->session.roster.racers[racer].source_flags;
        const PortableCharacterViewPose pose = portable_character_view_pose(&input, host->session.view_rules);
        state->opponents[i].art_pose.angle = pose.angle;
        state->opponents[i].art_pose.frame_override = pose.frame_override;
        state->opponents[i].art_pose.mirror = pose.mirror;
        state->opponents[i].art_pose_valid = 1u;
    }
    return 1;
}

/* EDAC restores three BG0 bands. Composite their physical pixels so a 4x
 * scene does not pass through a native-size scratch framebuffer. */
static int gp_entry_hud(PortableGpHost *host)
{
    const unsigned progress = host->entry.snapshot.hud_progress;
    if (!host->entry.snapshot.show_hud || progress == 0u)
        return 1;
    PortableSoftwareFrame hud = host->frame;
    hud.pixels = host->entry_hud_pixels;
    hud.stride = 0u;
    if (hud.pixels == NULL)
        return 0;
    portable_software_frame_clear(&hud, PORTABLE_LAP_BOARD_HUD_TRANSPARENT);
    PortableRacePresentationState state = host->state;
    state.suppress_native_hud = 0u;
    if (!portable_race_scene_render_hud(&host->scene, &state, &hud))
        return 0;
    return portable_hud_render_entry_bands(&hud, (uint8_t)progress,
                                           PORTABLE_LAP_BOARD_HUD_TRANSPARENT, &host->frame);
}

/* The pause menu's Graphics page changed a setting: take the values this race presents with. The surface
 * follows on the next frame (gp_resize_surface); Artwork Detail and Race Renderer wait for the next race. */
static void gp_apply_graphics(PortableGpHost *host, const PortableSettings *settings)
{
    PortableGpHostScene *configuration = &host->configuration;
    configuration->fit_scaling = settings->fit_scaling;
    configuration->interpolate = settings->interpolate;
    configuration->render_fps = (uint16_t)settings->render_fps;
    configuration->internal_scale = settings->internal_scale;
    configuration->aspect_width = (uint16_t)settings->aspect_width;
    if (host->render_texture != NULL)
        (void)SDL_SetTextureScaleMode(host->render_texture,
                                      settings->linear_filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
    host->needs_present = 1;
}

static int gp_resize_surface(PortableGpHost *host)
{
    const size_t width =
        portable_software_frame_window_width(host->renderer, host->configuration.aspect_width);
    const uint8_t scale = host->configuration.internal_scale > 1u ? host->configuration.internal_scale : 1u;
    if (width == host->frame.width && host->frame.scale == scale && host->render_surface_ready)
        return 1;
    PortableSoftwareFrame frame = host->frame;
    frame.width = width;
    frame.scale = scale;
    const size_t count = portable_software_frame_pixel_count(&frame);
    uint32_t *pixels = NULL;
    SDL_Texture *texture = NULL;
    if (width != 240u || frame.scale > 1u)
    {
        pixels = malloc(count * sizeof(uint32_t));
        SDL_ScaleMode filter = SDL_SCALEMODE_NEAREST;
        if (!SDL_GetTextureScaleMode(host->texture, &filter))
            goto fail;
        texture =
            SDL_CreateTexture(host->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                              (int)(width * frame.scale), 160 * frame.scale);
        if (pixels == NULL || texture == NULL || !SDL_SetTextureScaleMode(texture, filter))
            goto fail;
    }
    uint32_t *entry_hud = NULL;
    if (host->entry.initialized)
    {
        entry_hud = malloc(count * sizeof *entry_hud);
        if (entry_hud == NULL)
            goto fail;
    }
    free(host->entry_hud_pixels);
    host->entry_hud_pixels = entry_hud;
    free(host->render_pixels);
    SDL_DestroyTexture(host->render_texture);
    host->render_pixels = pixels;
    host->render_texture = texture;
    frame.pixels = pixels != NULL ? pixels : host->pixels;
    host->frame = frame;
    host->render_surface_ready = 1u;
    return 1;
fail:
    free(pixels);
    SDL_DestroyTexture(texture);
    return 0;
}

int portable_gp_host_end(PortableGpHost *host, PortableGpRaceHostOutcome *out)
{
    if (host == NULL)
        return 1;
    const char *require_pause=getenv("MKSC_AID_GP_PAUSE");
    if(host->configuration.input_aid && !host->configuration.quick_run && !host->configuration.battle &&
        require_pause!=NULL && strcmp(require_pause,"1")==0 && host->pause_aid.stage!=4u){
        fprintf(stderr,"GP pause input missing: course=%u stage=%u\n",
            host->configuration.course_id,host->pause_aid.stage);
        host->error=1;
    }
    const char *require_hud = getenv("MKSC_REQUIRE_HUD_MOMENTS");
    if (host->configuration.input_aid && require_hud && strcmp(require_hud, "1") == 0)
    {
        if (host->hud_capture_mask != UINT32_C(0x7fff) || host->finish_capture_mask != 7u)
        {
            fprintf(
                stderr,
                "GP HUD moments missing: course=%u captured=%04x expected=7fff finish=%u expected=7\n",
                host->configuration.course_id, (unsigned)host->hud_capture_mask,
                host->finish_capture_mask);
            host->error = 1;
        }
        else
            puts("GP HUD moments: PASS roulette, lap, rank and coin accepted-tick captures");
    }
    const char *require_kart_contact = getenv("MKSC_REQUIRE_KART_CONTACT");
    if (host->configuration.input_aid && require_kart_contact &&
        strcmp(require_kart_contact, "1") == 0 && !host->kart_contact_seen)
    {
        fprintf(stderr, "GP course=%u driver=%u missing selected kart contact\n",
                host->configuration.course_id, host->configuration.character_id);
        host->error = 1;
    }
    portable_script_host_end_race(host->configuration.scripts);
    host->session.script_hooks = NULL;
    PortableGpRaceHostOutcome *outcome = &host->outcome;
    outcome->marker_clock = host->session.marker_clock;
    outcome->marker_display_clock = host->session.marker_display_clock;
    if (host->configuration.input_aid && host->session_ready)
    {
        printf(
            "%s rendered race: course=%u class=%u ticks=%u frames=%u snapshot_digest=%016llx smooth=%u fps=%u rank=%u rescues=%u race_ticks=%u items=%u reserved=%u/%u mushrooms=%u triple_mushrooms=%u golden_mushrooms=%u cpu_boosts=%u base_q16=%d boost_ticks=%u golden_ticks=%u boost_cap_q16=%u stars=%u cpu_stars=%u lightning=%u star_ticks=%u/%u lightning_ticks=%u boos=%u thefts=%u boo_ticks=%u/%u/%u cpu_greens=%u triple_greens=%u green_hits=%u green_speed=%u/%u green_contacts=%u cpu_reds=%u cpu_spinies=%u triple_reds=%u red_hits=%u red_speed=%u/%u/%u red_trap=%u/%u/%u cpu_bananas=%u banana_hits=%u banana_speed=%u banana_arc=%u/%u banana_cpu_ticks=%u orbit=%u/%u/%u orbit_delays=%u/%u/%u:%u/%u/%u laps=%u completed_laps=%u coins=%u item_boxes=%zu coin_places=%zu course_contacts=%u/%u\n",
            host->configuration.quick_run ? "Quick Run" : "GP", host->configuration.course_id,
            host->configuration.engine_class, host->host_ticks, host->aid_presents,
            (unsigned long long)host->aid_digest, host->configuration.interpolate,
            host->configuration.render_fps, host->snapshot.player_rank, host->snapshot.rescue_count,
            host->snapshot.total_ticks, host->session.roster.item_uses,
            host->session.roster.player_reserved_uses, host->session.roster.cpu_reserved_uses,
            host->session.roster.player_mushroom_uses, host->session.roster.triple_mushroom_uses,
            host->session.roster.golden_mushroom_uses, host->session.roster.cpu_mushroom_uses,
            (int)host->session.player.input.base_speed_q16,
            (unsigned)(host->session.player_dependencies.mushroom_rules)->boost_ticks,
            (unsigned)(host->session.player_dependencies.mushroom_rules)->golden_actor_ticks,
            (host->session.player_dependencies.mushroom_rules)->max_speed_q16,
            host->session.roster.player_star_uses, host->session.roster.cpu_star_uses,
            host->session.roster.lightning_events,
            (unsigned)(host->session.roster.item_status_rules)->star_player_ticks,
            (unsigned)(host->session.roster.item_status_rules)->star_cpu_ticks,
            (unsigned)(host->session.roster.item_status_rules)->lightning_ticks,
            host->session.roster.boo_uses, host->session.roster.boo_thefts,
            (unsigned)(host->session.roster.boo_rules)->invisibility_ticks,
            (unsigned)(host->session.roster.boo_rules)->lock_ticks,
            (unsigned)(host->session.roster.boo_rules)->orbit_ticks,
            host->session.roster.cpu_green_uses, host->session.roster.triple_green_uses,
            host->session.roster.green_shell_hits,
            (unsigned)(host->session.roster.racers[0].green_shell_rules)->launch_speed_bonus,
            (unsigned)(host->session.roster.racers[0].green_shell_rules)->max_speed,
            (unsigned)(host->session.roster.racers[0].green_shell_rules)->wall_contact_budget,
            host->session.roster.cpu_red_uses, host->session.roster.cpu_spiny_uses,
            host->session.roster.triple_red_uses, host->session.roster.red_shell_hits,
            (unsigned)(host->session.roster.racers[0].red_shell_rules)->flight_speed_50,
            (unsigned)(host->session.roster.racers[0].red_shell_rules)->flight_speed_100,
            (unsigned)(host->session.roster.racers[0].red_shell_rules)->flight_speed_150,
            (unsigned)(host->session.roster.racers[0].red_shell_rules)->trap_arm_ticks,
            (unsigned)(host->session.roster.racers[0].red_shell_rules)->trap_lifetime_ticks,
            (unsigned)(host->session.roster.racers[0].red_shell_rules)->trap_lock_ticks,
            host->session.roster.cpu_banana_uses, host->session.roster.banana_hits,
            (unsigned)(host->session.roster.racers[0].banana_rules)->throw_speed_q16,
            (unsigned)(host->session.roster.racers[0].banana_rules)->throw_velocity_q16,
            (unsigned)(host->session.roster.racers[0].banana_rules)->throw_gravity_q16,
            (unsigned)(host->session.roster.racers[0].banana_rules)->cpu_flight_ticks,
            (host->session.roster.racers[0].shell_orbit_rules)->radius_multiplier,
            (host->session.roster.racers[0].shell_orbit_rules)->angle_step,
            (host->session.roster.racers[0].shell_orbit_rules)->animation_period,
            host->session.roster.shell_orbit_delay[0][0],
            host->session.roster.shell_orbit_delay[0][1],
            host->session.roster.shell_orbit_delay[0][2],
            host->session.roster.shell_orbit_delay[1][0],
            host->session.roster.shell_orbit_delay[1][1],
            host->session.roster.shell_orbit_delay[1][2], host->session.rules.lap_count,
            host->snapshot.laps_completed, host->session.roster.racers[0].coin_count_178,
            host->session.item_box_count, host->session.coin_count,
            host->session.course_contact_events, host->session.cpu_course_contact_events);
        fflush(stdout);
    }

    if (host->error)
        fprintf(stderr,
                "GP race host failed: course=%u driver=%u tick=%u phase=%u "
                "result_tick=%u result_state=%u roster_stage=%u: %s\n",
                host->configuration.course_id, host->configuration.character_id, host->host_ticks,
                (unsigned)host->session.runtime.session.phase, host->gp_failure_ready_ticks,
                (unsigned)host->gp_result_flow.state, (unsigned)host->session.failed_roster_stage,
                SDL_GetError());

    if (outcome != NULL && host->app_quit)
        outcome->quit_requested = 1u;
    if (host->results != NULL)
        portable_gp_results_host_end(host->results, NULL, NULL);
    if (host->controller != NULL)
        SDL_CloseGamepad(host->controller);
    if (host->configuration.audio != NULL)
        portable_sdl_audio_queue_pause(&host->configuration.audio->queue, 0);
    portable_mobile_touch_clear();
    if (host->owns_sdl) portable_mobile_touch_close();
    modern_menu_host_close(&host->modern);
    portable_model_catalog_close(&host->models);
    portable_scene_3d_close(&host->scene_3d);
    SDL_DestroyTexture(host->render_texture);
    free(host->render_pixels);
    free(host->entry_hud_pixels);
    if (host->owns_host)
    {
        SDL_DestroyTexture(host->texture);
        SDL_DestroyRenderer(host->renderer);
        SDL_DestroyWindow(host->window);
        if (host->owns_sdl) {
            portable_mobile_lifecycle_stop();
            SDL_Quit();
        }
    }
    free(host->visual_world_map);
    free(host->presentations);
    free(host->storage);
    const int result = host->error;
    if (out != NULL)
        *out = host->outcome;
    free(host);
    return result;
}

PortableGpHost *portable_gp_host_begin(const PortableGpHostScene *configuration)
{
#ifdef PORTABLE_ENABLE_TEST_HOOKS
    uint64_t trace_course, trace_tick;
    if (digest_trace_request(&trace_course,&trace_tick) < 0) {
        SDL_SetError("Invalid MKSC_GP_DIGEST_TRACE; expected COURSE:TICK with tick < 60000");
        return NULL;
    }
#endif
    if (configuration == NULL || configuration->pack == NULL || configuration->retry_credits > 3u ||
        configuration->track_set >= PORTABLE_TRACK_SET_COUNT || configuration->battle > 1u ||
        (configuration->battle != 0u) != (configuration->track_set == PORTABLE_TRACK_SET_ARENA) ||
        configuration->internal_scale > 8u || configuration->artwork_lod > 2u ||
        configuration->race_renderer > 1u ||
        (configuration->aspect_width != 0u &&
         (configuration->aspect_width < 240u || configuration->aspect_width > 1024u)))
        return NULL;
    if (configuration->test_seeded_standings)
    {
#ifndef PORTABLE_ENABLE_TEST_HOOKS
        return NULL;
#else
        if (!configuration->input_aid || configuration->quick_run || configuration->battle ||
            configuration->course_in_cup != 3u || configuration->previous_cup == NULL)
            return NULL;
#endif
    }
    if (configuration->playback && (!configuration->quick_run || configuration->recording == NULL))
        return NULL;
    PortableGpHost *host = calloc(1, sizeof *host);
    if (host == NULL)
        return NULL;
    host->configuration = *configuration;
    host->aid_digest = UINT64_C(14695981039346656037);
    if (configuration->quick_run && configuration->recording != NULL)
    {
        if (configuration->playback)
        {
            if (!configuration->recording->complete || configuration->recording->overflow ||
                configuration->recording->driving_profile != configuration->pack->driving_profile ||
                memcmp(configuration->recording->deterministic_digest,
                       configuration->pack->deterministic_digest,PORTABLE_ASSET_PACK_DIGEST_SIZE) != 0 ||
                configuration->recording->count == 0u ||
                configuration->recording->count > PORTABLE_GP_REPLAY_MAX_TICKS)
            {
                portable_model_catalog_close(&host->models);
                portable_scene_3d_close(&host->scene_3d);
                free(host);
                return NULL;
            }
        }
        else
        {
            configuration->recording->count = 0u;
            configuration->recording->driving_profile = configuration->pack->driving_profile;
            memcpy(configuration->recording->deterministic_digest,
                   configuration->pack->deterministic_digest,PORTABLE_ASSET_PACK_DIGEST_SIZE);
            configuration->recording->complete = 0u;
            configuration->recording->overflow = 0u;
        }
    }
    host->storage = malloc(sizeof *host->storage);
    host->presentations = calloc(PORTABLE_GP_RACER_COUNT, sizeof *host->presentations);
    host->disposition = PORTABLE_RACE_TICK_INVALID;
    host->visual_world_map = NULL;
    host->window = configuration->window;
    host->renderer = configuration->renderer;
    host->texture = configuration->texture;
    host->frame = (PortableSoftwareFrame){host->pixels, 240u, 160u, &host->ui_text, 1u, 0u,
        configuration->artwork_lod,NULL};
    host->frame.scale = configuration->internal_scale > 1u ? configuration->internal_scale : 1u;
    if (!portable_ui_text_load(configuration->pack, &host->ui_text))
        goto fail;
    host->clock_boundary = PORTABLE_GP_CLOCK_ENABLE_ON_FIRST_RACING_TICK;
    host->owns_host = configuration->window == NULL && configuration->renderer == NULL &&
                      configuration->texture == NULL;
    host->focused = 1;
    host->needs_present = 1;
    portable_gp_time_board_init(&host->gp_time_board);
    portable_gp_results_flow_init(&host->gp_result_flow);
    portable_gp_results_flow_init(&host->gp_result_display);
    if (!portable_gp_finish_portraits_init(&host->finish_portraits, 8u, 0u))
        goto fail;
    if (!host->owns_host &&
        (host->window == NULL || host->renderer == NULL || host->texture == NULL))
        goto fail;

    if (host->storage == NULL || host->presentations == NULL ||
        portable_content_registry_select(host->configuration.pack, host->configuration.course_id,
                                         host->configuration.character_id,
                                         &host->selection) != PORTABLE_CONTENT_OK ||
        portable_course_open_race(host->configuration.pack, &host->selection, host->storage,
                                  &host->race,
                                  portable_mod_identity_mirror()) != PORTABLE_RECOVERED_PACK_OK)
        goto fail;
    if (host->race.dependencies.response_rules == NULL || host->race.view_rules == NULL ||
        host->race.rules.roster_rules == NULL || host->race.roulette_rules == NULL ||
        host->race.gp_player_items == NULL || host->race.gp_scoring == NULL ||
        !host->race.gp_scoring->has_quality_rules)
    {
        fprintf(stderr, "Course %u mode %s: missing imported rules; reimport your ROM.\n",
                host->configuration.course_id,
                host->configuration.battle      ? "Battle"
                : host->configuration.quick_run ? "Quick Run"
                                                : "GP");
        goto fail;
    }
    if ((host->race.rules.lap_count == 0u) != (host->configuration.battle != 0u))
        goto fail;
    if (host->configuration.quick_run || host->configuration.battle)
    {
        if (host->configuration.previous_cup != NULL)
            goto fail;
    }
    if (host->configuration.previous_cup != NULL)
    {
        uint8_t seen = 0u;
        if (host->configuration.previous_cup->stage != PORTABLE_GP_CUP_STAGE_NEXT_RACE ||
            host->configuration.previous_cup->course_id != host->configuration.course_id ||
            host->configuration.previous_cup->cup_index != host->configuration.cup_index ||
            host->configuration.previous_cup->track_set != host->configuration.track_set ||
            host->configuration.previous_cup->engine_class != host->configuration.engine_class ||
            host->configuration.previous_cup->selected_driver_class !=
                host->selection.character.character_class)
            goto fail;
        for (uint8_t rank = 0u; rank < PORTABLE_GP_RACER_COUNT; ++rank)
        {
            const uint8_t racer = host->configuration.previous_cup->race_order[rank];
            const uint8_t bit = racer < PORTABLE_GP_RACER_COUNT ? (uint8_t)(1u << racer) : 0u;
            if (bit == 0u || (seen & bit) != 0u)
                goto fail;
            seen = (uint8_t)(seen | bit);
            host->next_grid_ranks[racer] = rank;
        }
        if (seen != UINT8_C(0xff))
            goto fail;
    }
    const PortableGpRaceSessionStatus initialized =
        host->configuration.quick_run
            ? portable_gp_race_session_init_quick_run(
                  &host->session, &host->race, (uint8_t)host->selection.character.character_class,
                  host->configuration.engine_class, host->configuration.quick_run_options)
            : portable_gp_race_session_init_engine_grid_order(
                  &host->session, &host->race, (uint8_t)host->selection.character.character_class,
                  host->configuration.previous_cup != NULL ? host->next_grid_ranks : NULL,
                  host->configuration.engine_class);
    if (initialized != PORTABLE_GP_RACE_SESSION_OK ||
        portable_gp_race_session_handoff_clock_boundary(&host->session, host->clock_boundary) !=
            PORTABLE_GP_RACE_SESSION_OK)
        goto fail;
    host->session_ready = 1u;
    host->session.fix_doppler_wrap = host->configuration.fix_doppler_wrap;
    if (!host->session.player.battle_enabled)
    {
        static const uint8_t no_points[8] = {0};
        if (portable_gp_race_roster_apply_cup_ai_setup(
                &host->session.roster,
                host->configuration.previous_cup != NULL
                    ? host->configuration.previous_cup->cumulative_points
                    : no_points,
                host->configuration.course_in_cup,
                host->configuration.engine_class) != PORTABLE_GP_ROSTER_OK)
            goto fail;
        if (host->session.roster.ai_mode_row_c64 != 0u &&
            (host->session.rules.ai_modes == NULL ||
             host->session.rules.ai_mode_count <
                 ((size_t)host->session.roster.ai_mode_row_c64 + 1u) *
                     host->session.rules.route_count))
            goto fail;
    }
    /* The first GP launch resets DAT_03002344 to zero. The original grid
     * route then makes 103 frame-loop RNG calls before the state aligned
     * with this host's race entry (source frame 270: 0x00797a9b). The host
     * still owes the visible launch presentation for those frames. */
    if (host->configuration.previous_cup == NULL &&
        (portable_gp_race_session_handoff_launch_rng(&host->session, 103u) !=
             PORTABLE_GP_RACE_SESSION_OK ||
         !portable_gp_item_art_clock_begin_direct(&host->session.item_art_clock,
                                                  host->session.runtime.session.countdown_remaining,
                                                  host->race.item_art_intro_rules)))
        goto fail;

    if (host->configuration.previous_cup != NULL &&
        portable_gp_cup_session_resume_race(host->configuration.previous_cup, &host->session) !=
            PORTABLE_GP_CUP_SESSION_OK)
        goto fail;

    host->session.retry_credits = host->configuration.retry_credits;
    if (host->configuration.previous_cup == NULL)
    {
        host->session.marker_clock = host->configuration.marker_clock;
        host->session.marker_display_clock = host->configuration.marker_display_clock;
    }
    if (host->configuration.previous_cup != NULL)
    {
        if (host->configuration.previous_cup->stage != PORTABLE_GP_CUP_STAGE_NEXT_RACE ||
            host->configuration.previous_cup->course_id != host->configuration.course_id ||
            host->configuration.previous_cup->cup_index != host->configuration.cup_index ||
            host->configuration.previous_cup->track_set != host->configuration.track_set ||
            host->configuration.previous_cup->engine_class != host->configuration.engine_class ||
            host->configuration.previous_cup->course_in_cup != host->configuration.course_in_cup ||
            host->configuration.previous_cup->course_header_index !=
                host->configuration.course_header_index ||
            host->configuration.previous_cup->selected_driver_class !=
                host->session.roster.racers[0].driver_class)
            goto fail;
        for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
            if (host->configuration.previous_cup->racer_driver_classes[index] !=
                host->session.roster.racers[index].driver_class)
                goto fail;
    }

    for (size_t index = 0u; index < PORTABLE_GP_RACER_COUNT; ++index)
    {
        /* Racer 0 is the local player's selected driver, which may be a roster
         * driver sharing a stock seat; CPUs use their class's stock driver. */
        PortableContentSelection racer_selection = host->selection;
        if ((index != 0u &&
             portable_content_registry_find_character_class(
                 host->configuration.pack, host->session.roster.racers[index].driver_class,
                 &racer_selection.character) != PORTABLE_CONTENT_OK) ||
            portable_course_open_presentation(
                host->configuration.pack, &racer_selection, host->storage, &host->race,
                &host->presentations[index], &host->views[index],
                portable_mod_identity_mirror()) != PORTABLE_RECOVERED_PACK_OK)
            goto fail;
        host->character_ids[index] = racer_selection.character.id;
    }
    if (configuration->race_renderer &&
        !portable_model_catalog_open(&host->models, configuration->pack))
        fprintf(stderr, "Optional model catalog unavailable; using billboard art.\n");
    if (host->views[0].hud.item_palette_selectors == NULL ||
        host->views[0].hud.item_palette_count != PORTABLE_HUD_ITEM_PALETTE_COUNT)
    {
        fprintf(stderr,
                "Course %u mode %s: missing item HUD palette selectors; reimport your ROM.\n",
                host->configuration.course_id,
                host->configuration.battle      ? "Battle"
                : host->configuration.quick_run ? "Quick Run"
                                                : "GP");
        goto fail;
    }
    if (portable_gp_race_session_configure_hud(&host->session, &host->views[0].gp_race_hud,
                                               &host->views[0].hud) != PORTABLE_GP_RACE_SESSION_OK)
        goto fail;
    if (host->views[0].gp_race_hud.rank_maps == NULL ||
        host->views[0].gp_minimap_markers.tiles == NULL)
    {
        fprintf(stderr, "GP race assets missing: course=%u state=live_hud\n",
                host->configuration.course_id);
        goto fail;
    }
    host->scene = (PortableRaceSceneView){
        .scene_3d = configuration->race_renderer ? &host->scene_3d : NULL,
        .models = configuration->race_renderer ? &host->models : NULL,
        .course_id = host->selection.course.id,
        .character_id = host->selection.character.id,
        .world_map = host->views[0].world_map,
        .tileset = host->views[0].tileset,
        .sky = &host->views[0].sky,
        .character = &host->views[0].character,
        .charged_exit_particle = &host->views[0].charged_exit_particle,
        .mushroom_burst = &host->views[0].mushroom_burst,
        .coin_pickup = &host->views[0].coin_pickup,
        .lava_smoke = &host->views[0].lava_smoke,
        .entry_splash = &host->views[0].entry_splash,
        .recovery_thaw = &host->views[0].recovery_thaw,
        .recovery_drip = &host->views[0].recovery_drip,
        .recovery_fx_rules = host->views[0].recovery_fx_rules,
        .surface_trail_art = host->views[0].surface_trail_art,
        .surface_trail_rules = host->views[0].surface_trail_rules,
        .coin_effect_art = host->views[0].coin_effect_art,
        .impact_effect_art = host->views[0].impact_effect_art,
        .coin_effect_rules = host->views[0].coin_effect_rules,
        .driver_projection = host->views[0].driver_projection,
        .weather_profile = host->views[0].weather_profile,
        .battle_balloon = &host->views[0].battle_balloon,
        .battle_bomb = &host->views[0].battle_bomb,
        .lightning_effect = &host->views[0].lightning_effect,
        .gp_finish_banner = &host->views[0].gp_finish_banner,
        .item_box = &host->views[0].item_box,
        .scenery = &host->views[0].scenery,
        .lava_bubble_art = host->views[0].lava_bubble_art,
        .thwomp_art = host->views[0].thwomp_art,
        .crab_art = host->views[0].crab_art,
        .seagull_art = host->views[0].seagull_art,
        .piranha_art = host->views[0].piranha_art,
        .snowman_art = host->views[0].snowman_art,
        .mouse_art = host->views[0].mouse_art,
        .thundercloud_art = host->views[0].thundercloud_art,
        .falling_star_art = host->views[0].falling_star_art,
        .penguin_art = host->views[0].penguin_art,
        .boo_art = host->views[0].boo_art,
        .cannonball_art = host->views[0].cannonball_art,
        .minimap = &host->views[0].minimap,
        .hud = &host->views[0].hud,
        .gp_hud = &host->views[0].gp_race_hud,
        .gp_markers = &host->views[0].gp_minimap_markers,
        .gp_rank_art =
            host->views[0].gp_results.rank_tiles != NULL ? &host->views[0].gp_results : NULL,
        .sine = host->storage->sine,
        .sine_count = 1024u,
        .opponent_count = (uint8_t)(portable_gp_race_roster_count(&host->session.roster) - 1u)};
    /* loadtrack_loadCoinLocations writes tile 0xc0 into the source visual
     * map at every coin cell. Keep the pilot's terrain cells independent of
     * this presentation copy, then let the normal BG2 window repack include
     * those source course placements. */
    host->visual_wall_mutation_count = 0u;
    host->visual_world_map = malloc(host->scene.world_map.map_bytes);
    if (host->visual_world_map == NULL)
        goto fail;
    memcpy(host->visual_world_map, host->scene.world_map.map, host->scene.world_map.map_bytes);
    for (size_t index = 0u; index < host->session.coin_count; ++index)
    {
        const int32_t x = host->session.coins[index].placement.position_x_q16 / (8 * 65536);
        const int32_t y = host->session.coins[index].placement.position_y_q16 / (8 * 65536);
        if (x < 0 || y < 0 || x >= (int32_t)host->scene.world_map.width_tiles ||
            y >= (int32_t)host->scene.world_map.height_tiles)
            goto fail;
        host->visual_world_map[(size_t)y * host->scene.world_map.width_tiles + (size_t)x] =
            UINT8_C(0xc0);
    }
    host->scene.world_map.map = host->visual_world_map;
    for (size_t index = 0u; index < PORTABLE_GP_OPPONENT_COUNT; ++index)
    {
        host->scene.opponent_characters[index] = &host->views[index + 1u].character;
        host->scene.opponent_character_ids[index] = host->character_ids[index + 1u];
    }
    if (portable_character_roster_image(host->configuration.pack, host->selection.character.id,
                                        PORTABLE_CHARACTER_ROSTER_RACE_PORTRAIT,
                                        &host->player_portrait))
    {
        host->scene.gp_racer_portraits[0] = &host->player_portrait;
        host->views[0].gp_standings.racer_portraits[0] = &host->player_portrait;
    }
    if (portable_character_roster_image(host->configuration.pack, host->selection.character.id,
                                        PORTABLE_CHARACTER_ROSTER_BOARD_NAME, &host->player_name))
        host->views[0].gp_standings.racer_names[0] = &host->player_name;
    /* Scripts start fresh on the fully prepared grid; replays and retries
     * rebuild this host, so they see the same states and random streams. */
    if (!portable_script_host_begin_race(
            host->configuration.scripts, &host->session,
            host->configuration.battle      ? PORTABLE_SCRIPT_MODE_BATTLE
            : host->configuration.quick_run ? PORTABLE_SCRIPT_MODE_QUICK_RUN
                                            : PORTABLE_SCRIPT_MODE_GP,
            host->configuration.course_id, host->configuration.engine_class))
        goto fail;
    if (portable_gp_race_session_snapshot(&host->session, &host->snapshot) !=
            PORTABLE_GP_RACE_SESSION_OK ||
        !portable_gp_race_presentation_build(&host->session, &host->snapshot, &host->state))
        goto fail;
    if (!host->configuration.quick_run && !host->configuration.battle &&
        host->views[0].gp_opening.tiles != NULL)
    {
        PortableGpEntryPoint grid[PORTABLE_GP_ENTRY_DRIVERS];
        for (size_t i = 0u; i < PORTABLE_GP_ENTRY_DRIVERS; ++i)
            grid[i] = (PortableGpEntryPoint){host->session.roster.racers[i].position_x_q16,
                                             host->session.roster.racers[i].position_y_q16};
        if (!portable_gp_entry_init(&host->entry, grid, 0u,
                                    (uint8_t)(host->session.roster.racers[0].rank_flags_ba >> 1u),
                                    host->configuration.course_in_cup))
            goto fail;
        host->entry_sky = host->session.sky_motion;
    }
    if (host->owns_host)
    {
        portable_mobile_lifecycle_hint();
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
            goto fail;
        host->owns_sdl = 1;
        portable_mobile_lifecycle_start();
        host->window = SDL_CreateWindow(
            "MKSC GP | arrows drive | Shift hop | C item | P/Start pause | Esc courses", 960, 640,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        host->renderer = host->window != NULL ? SDL_CreateRenderer(host->window, NULL) : NULL;
        if (host->renderer != NULL)
            SDL_SetRenderVSync(host->renderer, 1);
        host->texture = host->renderer != NULL
                            ? SDL_CreateTexture(host->renderer, SDL_PIXELFORMAT_ARGB8888,
                                                SDL_TEXTUREACCESS_STREAMING, 240, 160)
                            : NULL;
        if (host->texture == NULL || !SDL_SetTextureScaleMode(host->texture, SDL_SCALEMODE_NEAREST))
            goto fail;
    }
    else
        SDL_SetWindowTitle(
            host->window,
            "MKSC GP | arrows drive | Shift hop | C item | P/Start pause | Esc courses");
    if (!gp_resize_surface(host))
        goto fail;
    host->controller = portable_sdl_gamepad_open_first();
    host->frequency = SDL_GetPerformanceFrequency();
    portable_frame_scheduler_init(&host->scheduler);
    portable_frame_scheduler_init(&host->menu_scheduler);
    return host;
fail:
    host->error = 1;
    portable_gp_host_end(host, NULL);
    return NULL;
}

static void gp_set_paused(PortableGpHost *host, int paused)
{
    host->paused = (uint8_t)(paused != 0);
    host->history_ready = 0u;
    host->resume_armed = 0u;
    host->needs_present = 1;
    portable_frame_scheduler_reset(
        &host->scheduler,
        host->configuration.input_aid ? host->aid_now : frontend_now_ns(host->frequency), 0);
    if (host->configuration.audio != NULL)
        portable_sdl_audio_queue_pause(&host->configuration.audio->queue, paused);
}

static uint16_t gp_menu_buttons(SDL_Gamepad *controller)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    uint16_t buttons = portable_sdl_gamepad_frontend(controller) | portable_mobile_touch_frontend();
    if (keys == NULL)
        return buttons;
    if (keys[SDL_SCANCODE_UP])
        buttons |= PORTABLE_FRONTEND_BUTTON_UP;
    if (keys[SDL_SCANCODE_DOWN])
        buttons |= PORTABLE_FRONTEND_BUTTON_DOWN;
    /* Left/Right, B and Select only drive the Graphics page; the recovered list ignores them. */
    if (keys[SDL_SCANCODE_LEFT])
        buttons |= PORTABLE_FRONTEND_BUTTON_LEFT;
    if (keys[SDL_SCANCODE_RIGHT])
        buttons |= PORTABLE_FRONTEND_BUTTON_RIGHT;
    if (keys[SDL_SCANCODE_X])
        buttons |= PORTABLE_FRONTEND_BUTTON_B;
    if (keys[SDL_SCANCODE_TAB])
        buttons |= PORTABLE_FRONTEND_BUTTON_SELECT;
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_Z])
        buttons |= PORTABLE_FRONTEND_BUTTON_A;
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_P])
        buttons |= PORTABLE_FRONTEND_BUTTON_START;
    return buttons;
}

static void gp_open_pause_menu(PortableGpHost *host)
{
    const uint64_t now =
        host->configuration.input_aid ? host->aid_now : frontend_now_ns(host->frequency);
    host->paused = 1u;
    host->history_ready = 0u;
    host->resume_armed = 0u;
    host->needs_present = 1;
    host->pause_menu_active = 1u;
    host->pause_retry_blink = 0u;
    if (host->configuration.quick_run || host->session.player.battle_enabled)
        portable_pause_menu_init(&host->pause_menu, gp_menu_buttons(host->controller));
    else
        portable_pause_menu_init_gp(&host->pause_menu, gp_menu_buttons(host->controller),
                                    host->configuration.retry_credits != 0u);
    portable_frame_scheduler_reset(&host->scheduler, now, 0);
    portable_frame_scheduler_reset(&host->menu_scheduler, now, 1);
    if (host->configuration.audio != NULL && !route_audio_set_paused(host->configuration.audio, 1))
        host->error = 1;
}

static void gp_pause_exit(PortableGpHost *host, uint8_t next_mode)
{
    host->outcome.results_completed = 1u;
    host->outcome.scene_request = UINT32_C(1);
    host->outcome.next_mode = next_mode;
    host->quit = 1;
}

/* 2C7F4: Continue resumes; the others leave with play.c's exit codes. */
static void gp_pause_menu_frame(PortableGpHost *host, uint64_t now)
{
    uint16_t samples[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t count = 0u;
    int dropped = 0;
    if (portable_frame_scheduler_update(&host->menu_scheduler, now, host->focused, 0, samples,
                                        PORTABLE_FRAME_MAX_CATCH_UP, &count,
                                        &dropped) != PORTABLE_FRAME_SCHEDULER_OK)
    {
        host->error = 1;
        return;
    }
    (void)dropped;
    RouteAudioHost *audio = host->configuration.audio;
    for (size_t tick = 0u; tick < count && host->pause_menu_active && !host->quit; ++tick)
    {
        host->needs_present = 1;
        if (host->pause_retry_blink != 0u)
        {
            /* State 100 blinks the spent life for 48 frames before retrying. */
            if (--host->pause_retry_blink == 0u)
                gp_pause_exit(host, 1u);
        }
        else
        {
            uint16_t buttons = host->configuration.input_aid ? 0u :
                (uint16_t)(gp_menu_buttons(host->controller) | portable_mobile_touch_take_pressed());
            if (host->mobile_pause_cancel && host->pause_menu.open_ticks >= PORTABLE_PAUSE_MENU_OPEN_TICKS) {
                host->pause_menu.cursor = 0u;
                host->pause_menu.buttons &= (uint16_t)~PORTABLE_FRONTEND_BUTTON_A;
                buttons |= PORTABLE_FRONTEND_BUTTON_A;
                host->mobile_pause_cancel = 0u;
            }
            const PortablePauseAction action = portable_pause_menu_tick(&host->pause_menu, buttons);
            const PortableSettings *graphics =
                portable_graphics_pause_update(&host->pause_menu.graphics, &host->configuration.graphics);
            if (graphics != NULL)
                gp_apply_graphics(host, graphics);
            if (host->pause_menu.cue != 0u && audio != NULL &&
                !route_audio_menu_cue(audio, host->pause_menu.cue))
            {
                host->error = 1;
                return;
            }
            switch (action)
            {
            case PORTABLE_PAUSE_ACTION_CONTINUE:
                host->pause_menu_active = 0u;
                host->paused = 0u;
                host->history_ready = 0u;
                if (audio != NULL && !route_audio_set_paused(audio, 0))
                {
                    host->error = 1;
                    return;
                }
                portable_frame_scheduler_reset(&host->scheduler, now, host->focused);
                break;
            case PORTABLE_PAUSE_ACTION_GP_RETRY:
                host->pause_retry_blink = 48u;
                break;
            case PORTABLE_PAUSE_ACTION_RETRY:
                gp_pause_exit(host, 1u);
                break;
            case PORTABLE_PAUSE_ACTION_CHANGE_TRACK:
                gp_pause_exit(host, 3u);
                break;
            case PORTABLE_PAUSE_ACTION_CHANGE_CHARACTER:
                gp_pause_exit(host, 2u);
                break;
            case PORTABLE_PAUSE_ACTION_QUIT:
                gp_pause_exit(host, 4u);
                break;
            default:
                break;
            }
        }
        if (audio != NULL && !route_audio_render(audio, 0, 0, NULL))
        {
            host->error = 1;
            return;
        }
    }
}

void portable_gp_host_event(PortableGpHost *host, const SDL_Event *input)
{
    if (host == NULL || input == NULL || host->quit || host->error)
        return;
    modern_menu_host_event(&host->modern, input);
    const SDL_JoystickID id = portable_sdl_gamepad_id(host->controller);
    if (id != 0u &&
        (input->type == SDL_EVENT_GAMEPAD_REMOVED || input->type == SDL_EVENT_GAMEPAD_REMAPPED) &&
        input->gdevice.which == id)
        gp_set_paused(host, 1);
    portable_sdl_gamepad_device_event(&host->controller, input);
    if (host->results != NULL)
    {
        portable_gp_results_host_event(host->results, input);
        return;
    }
    const uint16_t touch = portable_mobile_touch_event(input, host->renderer);
    if ((touch & PORTABLE_FRONTEND_BUTTON_SELECT) && host->focused && !host->paused &&
        host->configuration.audio != NULL && !route_audio_horn(host->configuration.audio))
        host->error = 1;
    if ((touch & PORTABLE_FRONTEND_BUTTON_B) && host->pause_menu_active)
        host->mobile_pause_cancel=1u;
    if ((portable_mobile_back(input) || ((touch & PORTABLE_FRONTEND_BUTTON_B) &&
         host->snapshot.results_ready)) && host->focused) {
        if (host->pause_menu_active) host->mobile_pause_cancel = 1u;
        else if (!host->snapshot.results_ready) gp_open_pause_menu(host);
        else host->mobile_results_continue=1u;
        return;
    }
    const SDL_Event event = *input;

    if (host->focused && !host->pause_menu_active &&
        ((touch & PORTABLE_FRONTEND_BUTTON_START) ||
         (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
          event.gbutton.which == portable_sdl_gamepad_id(host->controller) &&
          event.gbutton.button == SDL_GAMEPAD_BUTTON_START) ||
         (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
          (event.key.scancode == SDL_SCANCODE_P || event.key.scancode == SDL_SCANCODE_SPACE ||
           (host->paused && event.key.scancode == SDL_SCANCODE_RETURN)))))
    {
        /* Start opens the race pause menu; a focus/device freeze resumes. */
        if (!host->paused && !host->snapshot.results_ready &&
            (!host->entry.initialized || host->entry.snapshot.done))
            gp_open_pause_menu(host);
        else if (!host->paused && host->snapshot.results_ready && gp_board_confirm_allowed(host))
            host->skip_results = 1u;
        else if (host->paused && host->resume_armed)
            gp_set_paused(host, 0);
    }
    if (host->focused && !host->paused && !host->pause_menu_active &&
        host->snapshot.results_ready && gp_board_confirm_allowed(host) &&
        ((touch & PORTABLE_FRONTEND_BUTTON_A) ||
         (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
          (event.key.key == SDLK_RETURN || event.key.key == SDLK_Z)) ||
         (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
          event.gbutton.which == portable_sdl_gamepad_id(host->controller) &&
          event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)))
        host->skip_results = 1u;
    if (event.type == SDL_EVENT_QUIT)
    {
        host->app_quit = 1;
        host->quit = 1;
        return;
    }
    if (host->focused && !host->paused && host->configuration.audio != NULL &&
        ((event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
          event.gbutton.which == portable_sdl_gamepad_id(host->controller) &&
          event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) ||
         (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
          event.key.scancode == SDL_SCANCODE_TAB)) &&
        !route_audio_horn(host->configuration.audio))
        host->error = 1;
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        (event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_X))
    {
        if (host->configuration.quick_run && host->configuration.playback &&
            host->configuration.recording != NULL)
        {
            host->results = portable_quick_results_begin(
                &host->ui_text, host->pixels, 1u, host->configuration.audio, host->renderer,
                host->texture, &host->views[0].gp_results, &host->views[0].gp_standings,
                &host->configuration.recording->results);
            if (host->results == NULL || !gp_results_video(host))
                host->error = 1;
            else
                portable_gp_results_source_clock(host->results, host->session.source_frame_clock);
            return;
        }
        host->quit = 1;
        return;
    }
    if ((event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST))
    {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            host->focused = 0;
            portable_mobile_touch_clear();
            host->mobile_pause_cancel = 0u;
            gp_set_paused(host, 1);
        }
        if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
        {
            host->focused = 1;
            host->needs_present = 1;
            /* A completed board owns its own controls. Restore that
             * presentation, without resuming an explicitly opened race menu. */
            if (host->snapshot.results_ready && !host->pause_menu_active)
                gp_set_paused(host, 0);
            portable_frame_scheduler_reset(&host->menu_scheduler,
                host->configuration.input_aid ? host->aid_now : frontend_now_ns(host->frequency), 1);
            if (host->pause_menu_active && host->configuration.audio != NULL)
                portable_sdl_audio_queue_pause(&host->configuration.audio->queue, 0);
        }
        if (event.type == SDL_EVENT_WINDOW_EXPOSED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            host->needs_present = 1;
    }
}

static int gp_standings_background(void *context, PortableSoftwareFrame *frame)
{
    PortableGpHost *host = context;
    if (frame == NULL)
        return portable_gp_race_session_tick(&host->session, 0u, &host->disposition) ==
               PORTABLE_GP_RACE_SESSION_OK;
    PortableRacePresentationState state;
    if (portable_gp_race_session_snapshot(&host->session, &host->snapshot) !=
            PORTABLE_GP_RACE_SESSION_OK ||
        !portable_gp_race_presentation_build(&host->session, &host->snapshot, &state))
        return 0;
    /* 1E7C8 clears both native HUD maps; the world and autonomous racers
     * keep their existing owners behind the transparent standings layers. */
    state.suppress_native_hud = 1u;
    return portable_race_scene_render(&host->scene, &state, frame);
}

static int gp_finish_portraits_tick(PortableGpHost *host)
{
    uint32_t flags[8];
    uint8_t classes[8], seats[8];
    for (unsigned rank = 0u; rank < 8u; ++rank)
    {
        const unsigned racer = host->session.roster.standings[rank];
        if (racer >= 8u)
            return 0;
        const PortableGpRacer *driver = &host->session.roster.racers[racer];
        flags[rank] = driver->source_flags;
        classes[rank] = driver->driver_class;
        seats[rank] = (uint8_t)racer;
    }
    if (!portable_gp_finish_portraits_tick(&host->finish_portraits, flags, classes, seats))
        return 0;
    ++host->finish_portrait_ticks;
    return 1;
}

static int gp_failure_background(void *context, PortableSoftwareFrame *frame)
{
    PortableGpHost *host = context;
    /* Sourceplay stops the world when the result owner sets5C50 done. */
    if (!frame && portable_gp_results_failure_exiting(host->results))
        return 1;
    if (!gp_standings_background(context, frame))
        return 0;
    if (!frame && portable_gp_results_failure_advances_portraits(host->results) &&
        !gp_finish_portraits_tick(host))
        return 0;
    if (frame && host->configuration.modern_menus)return 1;
    if (!frame || !host->session.retry_credits ||
        !portable_gp_results_failure_hud_visible(host->results))
        return 1; /* Source31/514B0 clear HUD maps. */
    return host->gp_result_hud_ready &&
           portable_gp_results_render_laps_with_hud(
               &host->views[0].gp_results, &host->gp_result_display, host->gp_result_times,
               (uint8_t)host->snapshot.lap_count, &host->gp_result_hud, frame) &&
           portable_hud_render_gp_finish_portraits(
               &host->views[0].gp_race_hud, &host->views[0].hud, &host->finish_portraits,
               host->scene.gp_racer_portraits, host->session.source_frame_clock, frame);
}

static int gp_frame_results_active(PortableGpHost *host, uint64_t now)
{
    if ((host->configuration.quick_run || host->session.player.battle_enabled) &&
        host->configuration.input_aid && ++host->aid_results_frames % 4u == 0u)
    {
        portable_quick_results_aid(host->results, (uint8_t)(host->configuration.replay_aid &&
                                                            !host->configuration.playback));
    }
    else if (!host->configuration.quick_run && !host->session.player.battle_enabled &&
             host->configuration.input_aid && ++host->aid_results_frames % 120u == 0u)
    {
        const SDL_Event confirm = {.key = {.type = SDL_EVENT_KEY_DOWN, .key = SDLK_RETURN}};
        portable_gp_results_host_event(host->results, &confirm);
    }
    const int status = portable_gp_results_host_frame(host->results, now);
    if (status == 0)
        return 0;
    uint8_t next_mode = 0, quit_requested = 0;
    const int closed = portable_gp_results_host_end(host->results, &next_mode, &quit_requested);
    host->results = NULL;
    if (status < 0 || closed)
    {
        host->error = 1;
        return -1;
    }
    host->outcome.quit_requested = quit_requested;
    host->outcome.results_completed = (uint8_t)!quit_requested;
    host->outcome.next_mode = next_mode;
    host->outcome.scene_request = quit_requested ? 0u : UINT32_C(1);
    host->app_quit = quit_requested;
    host->quit = 1;
    return 1;
}

/* Input edges and focus/pause handling precede scheduler admission. */
static int gp_mobile_results_aid_enabled(const PortableGpHost *host)
{
    const char *enabled = getenv("MKSC_AID_MOBILE_RESULTS");
    return host->configuration.input_aid && !host->configuration.quick_run &&
           !host->session.player.battle_enabled && enabled != NULL && strcmp(enabled, "1") == 0;
}

static int gp_mobile_results_aid(PortableGpHost *host)
{
    if (!gp_mobile_results_aid_enabled(host)) return 1;
    if (host->mobile_results_aid_stage == 0u && host->snapshot.results_ready &&
        gp_board_confirm_allowed(host))
    {
        const Uint32 types[] = {SDL_EVENT_WILL_ENTER_BACKGROUND, SDL_EVENT_DID_ENTER_BACKGROUND,
                                SDL_EVENT_WILL_ENTER_FOREGROUND, SDL_EVENT_DID_ENTER_FOREGROUND};
        for (size_t i = 0u; i < sizeof types / sizeof types[0]; ++i)
        {
            SDL_Event event = {.type = types[i]};
            if (!SDL_PushEvent(&event)) return 0;
        }
        host->mobile_results_aid_stage = 1u;
    }
    else if (host->mobile_results_aid_stage == 1u)
    {
        SDL_SetError("GP mobile results lifecycle events were not observed");
        return 0;
    }
    else if (host->mobile_results_aid_stage == 2u)
    {
        if (!host->focused || host->paused || host->pause_menu_active)
        {
            SDL_SetError("GP mobile results did not restore its completed board");
            return 0;
        }
        SDL_Event back = {.key = {.type = SDL_EVENT_KEY_DOWN, .scancode = SDL_SCANCODE_AC_BACK}};
        if (!SDL_PushEvent(&back)) return 0;
        back.type = SDL_EVENT_KEY_UP;
        if (!SDL_PushEvent(&back)) return 0;
        host->mobile_results_aid_stage = 3u;
    }
    return 1;
}

static int gp_pause_aid_enabled(const PortableGpHost *host)
{
    const char *enabled=getenv("MKSC_AID_GP_PAUSE");
    return host->configuration.input_aid && !host->configuration.quick_run &&
        !host->configuration.battle && enabled!=NULL && strcmp(enabled,"1")==0;
}

static int gp_pause_aid_key(SDL_Scancode scancode,SDL_Keycode key)
{
    SDL_Event event={.key={.type=SDL_EVENT_KEY_DOWN,.scancode=scancode,.key=key}};
    if(!SDL_PushEvent(&event))return 0;
    event.type=SDL_EVENT_KEY_UP;
    return SDL_PushEvent(&event);
}

static int gp_pause_aid_frozen(const PortableGpHost *host)
{
    const GpPauseAid *aid=&host->pause_aid;
    return host->session.source_frame_clock==aid->source_clock &&
        host->session.source_result_clock.centiseconds==aid->result_clock &&
        host->session.source_rng_state==aid->source_rng &&
        host->session.main_visual_rng_state==aid->visual_rng &&
        host->host_ticks==aid->host_ticks && host->aid_digest==aid->digest &&
        host->session.player.pose.position_x==aid->x_q16 &&
        host->session.player.pose.position_y==aid->y_q16 &&
        host->session.player.motion.speed_q16==aid->speed_q16;
}

/* The old focus/coin-FX freeze completes first. This separate opt-in route
 * opens the real pause owner and waits for its visible capture before Back. */
static int gp_pause_input_aid(PortableGpHost *host)
{
    if(!gp_pause_aid_enabled(host))return 1;
    GpPauseAid *aid=&host->pause_aid;
    if(aid->stage==0u){
        if(!host->aid_freeze_done || host->paused || host->pause_menu_active ||
            host->session.source_frame_clock<240u ||
            host->session.runtime.session.phase!=PORTABLE_RACE_PHASE_RACING)return 1;
        if(!gp_pause_aid_key(SDL_SCANCODE_P,SDLK_P))return 0;
        aid->stage=1u;
        return 1;
    }
    if(aid->stage==4u)return 1;
    if(++aid->wait_frames>512u){SDL_SetError("GP pause input aid timed out at stage %u",aid->stage);return 0;}
    if(aid->stage==1u){
        if(!host->paused || !host->pause_menu_active)return 1;
        aid->source_clock=host->session.source_frame_clock;
        aid->result_clock=host->session.source_result_clock.centiseconds;
        aid->source_rng=host->session.source_rng_state;
        aid->visual_rng=host->session.main_visual_rng_state;
        aid->host_ticks=host->host_ticks;aid->digest=host->aid_digest;
        aid->x_q16=host->session.player.pose.position_x;
        aid->y_q16=host->session.player.pose.position_y;
        aid->speed_q16=host->session.player.motion.speed_q16;
        aid->stage=2u;
    }
    if(aid->stage==2u){
        if(!host->paused || !host->pause_menu_active || !gp_pause_aid_frozen(host)){
            SDL_SetError("GP pause input aid advanced the frozen race");return 0;
        }
        if(host->pause_menu.open_ticks>=PORTABLE_PAUSE_MENU_OPEN_TICKS && aid->presented){
            if(!gp_pause_aid_key(SDL_SCANCODE_AC_BACK,SDLK_AC_BACK))return 0;
            aid->stage=3u;
        }
    }else if(aid->stage==3u){
        if(host->paused || host->pause_menu_active){
            if(!gp_pause_aid_frozen(host)){SDL_SetError("GP pause input aid changed race before Continue");return 0;}
        }else if(host->session.source_frame_clock>aid->source_clock && host->host_ticks>aid->host_ticks){
            aid->stage=4u;
            puts("GP pause input: PASS Start opened visible pause; clocks/RNG frozen; Back Continue resumed");
        }
    }
    return 1;
}

static int gp_frame_prepare_input(PortableGpHost *host, uint64_t now)
{
    if(!gp_pause_input_aid(host)){host->error=1;return -1;}
    if (host->configuration.input_aid && !host->configuration.quick_run &&
        !host->session.player.battle_enabled && host->configuration.recording == NULL &&
        host->snapshot.results_ready && !host->skip_results &&
        !(host->configuration.modern_menus && getenv("MKSC_AID_RESULTS_HOLD") != NULL) &&
        !(gp_mobile_results_aid_enabled(host) && host->mobile_results_aid_stage < 4u))
    {
        const char *confirm_aid = getenv("MKSC_AID_RESULT_CONFIRM");
        if ((host->configuration.fast_menus || (confirm_aid && strcmp(confirm_aid, "1") == 0)) &&
            (host->aid_result_confirm_stage == 0u ||
             (host->aid_result_confirm_stage == 1u && gp_board_confirm_allowed(host))))
        {
            /* Ordinary A edges exercise the same lock as a physical pad.
             * An explicit default-timing aid probes before and after it. */
            const SDL_Event confirm = {.key = {.type = SDL_EVENT_KEY_DOWN,
                                               .key = SDLK_RETURN,
                                               .scancode = SDL_SCANCODE_RETURN}};
            portable_gp_host_event(host, &confirm);
            SDL_Event released = confirm;
            released.type = SDL_EVENT_KEY_UP;
            portable_gp_host_event(host, &released);
            printf("GP result confirm: A accepted=%u ticks=%u short=%u\n", host->skip_results,
                   host->gp_failure_ready_ticks, host->configuration.fast_menus);
            ++host->aid_result_confirm_stage;
        }
    }
    if (host->pause_menu_active && host->focused)
    {
        gp_pause_menu_frame(host, now);
        if (host->error)
            return -1;
        if (host->quit)
            return 1;
    }
    /* The existing focus freeze can wait for live coin FX in regression
     * routes; it only stops ordinary host ticks and never writes race state. */
    const char *coin_freeze_env = getenv("MKSC_AID_COIN_FREEZE");
    const int coin_freeze = coin_freeze_env && strcmp(coin_freeze_env, "1") == 0;
    int freeze_ready = !coin_freeze;
    if (coin_freeze)
        for (unsigned i = 0u; i < PORTABLE_COIN_EFFECT_SLOTS; ++i)
        {
            const PortableCoinEffectSlot *coin = &host->snapshot.coin_effects.slots[i];
            if (coin->kind == PORTABLE_COIN_EFFECT_BOUNCE && coin->visible && coin->age >= 12u)
                freeze_ready = 1;
        }
    if (host->configuration.input_aid && !host->aid_freeze_done && !host->pause_menu_active &&
        host->session.source_frame_clock >= 200u && !host->snapshot.results_ready &&
        (freeze_ready || host->paused))
    {
        if (!host->paused)
        {
            gp_set_paused(host, 1);
            host->aid_freeze_frames = 0u;
            host->aid_coin_freeze_digest = portable_coin_effect_digest(&host->session.coin_effects);
        }
        else
        {
            if (coin_freeze && host->aid_coin_freeze_digest !=
                                   portable_coin_effect_digest(&host->session.coin_effects))
            {
                SDL_SetError("coin FX advanced while focus paused");
                return -1;
            }
            if (++host->aid_freeze_frames >= 4u)
            {
                host->aid_freeze_done = 1u;
                gp_set_paused(host, 0);
                if (coin_freeze)
                    printf("Coin FX pause: PASS immutable owner across focus freeze\n");
            }
        }
    }
    if (!host->focused || host->paused)
        host->history_ready = 0u;
    if (host->paused && !host->pause_menu_active)
    {
        const bool *keys = SDL_GetKeyboardState(NULL);
        if (portable_sdl_gamepad_frontend(host->controller) == 0u &&
            portable_sdl_gamepad_driving(host->controller) == 0u &&
            portable_sdl_keyboard_driving() == 0u && !keys[SDL_SCANCODE_P] &&
            !keys[SDL_SCANCODE_SPACE] && !keys[SDL_SCANCODE_RETURN])
            host->resume_armed = 1u;
    }
    return 0;
}

/* Called once per accepted race tick, after snapshot, audio and rumble. */
static int gp_tick_result_boards(PortableGpHost *host)
{
    if (host->snapshot.results_ready && !host->session.player.battle_enabled)
    {
        if (host->gp_failure_ready_ticks == 0u && !host->snapshot.source_result_times_ready)
        {
            host->error = 1;
            return 0;
        }
        if (!host->configuration.quick_run)
        {
            if (host->gp_failure_ready_ticks == 0u)
            {
                PortableQuickRunResults board;
                if (portable_quick_run_results_build(&host->session, &board) !=
                    PORTABLE_GP_RACE_RESULTS_OK)
                {
                    host->error = 1;
                    return 0;
                }
                for (unsigned rank = 0u; rank < 8u; ++rank)
                {
                    const PortableQuickRunResultRow *row = &board.rows[rank];
                    host->gp_time_rows[rank] = (PortableTimeBoardRow){
                        row->centiseconds,
                        row->racer_index,
                        row->driver_class,
                        (uint8_t)rank,
                        PORTABLE_TIME_BOARD_TIME,
                        (uint8_t)(row->racer_index == host->session.local_index)};
                    host->gp_time_points[row->racer_index] =
                        host->configuration.previous_cup
                            ? host->configuration.previous_cup->cumulative_points[row->racer_index]
                            : 0u;
                    host->gp_time_awards[row->racer_index] =
                        host->session.scoring_rules
                            ? host->session.scoring_rules->finish_points[rank]
                            : 0u;
                }
            }
            ++host->gp_failure_ready_ticks;
            if (host->configuration.fast_menus && host->skip_results &&
                host->configuration.recording == NULL)
            {
                /* Skip only presentation; the ordinary standings
                 * owner still awards points and advances the cup. */
                host->gp_result_flow.state = 4u;
                host->gp_result_visible = 0u;
                host->gp_time_board.state = 13u;
                host->gp_time_board.visible = 0u;
                host->gp_time_board.skip_transfer = 1u;
                if (host->mobile_results_aid_stage == 3u)
                {
                    host->mobile_results_aid_stage = 4u;
                    puts("GP mobile results: PASS");
                }
            }
            /* 1E7C8 case0 first runs the selected racer's 1E248
             * lap owner to stage4. Its 181-tick entry, slide and
             * total-clock540 hold precede the eight-racer board. */
            if (host->gp_result_flow.state < 4u)
            {
                if (host->gp_failure_ready_ticks == 1u)
                    memcpy(host->gp_result_times, host->snapshot.source_result_times_centiseconds,
                           sizeof host->gp_result_times);
                PortableGpResultsStatus status;
                if (host->gp_result_flow.state <= 1u)
                    status = portable_gp_results_flow_tick_entry(&host->gp_result_flow, 0u, 0u);
                else if (host->gp_result_flow.state == 2u)
                {
                    status = portable_gp_results_flow_tick_reveal(
                        &host->gp_result_flow, (uint8_t)host->snapshot.lap_count);
                    if (host->gp_result_flow.reveal_pending)
                    {
                        host->gp_result_display = host->gp_result_flow;
                        host->gp_result_visible = 1u;
                    }
                }
                else
                {
                    if (!gp_finish_portraits_tick(host))
                    {
                        host->error = 1;
                        return 0;
                    }
                    status = portable_gp_results_flow_tick_lap_hold(&host->gp_result_flow);
                    if (host->configuration.input_aid && !host->finish_owner_reported &&
                        host->gp_result_flow.total_ticks >= 539u)
                    {
                        host->finish_owner_reported = 1u;
                        printf(
                            "GP finish owner: course=%u selected_clock=%u board_clock=%u clock=%u total=%u portrait_ticks=%u rows=",
                            host->configuration.course_id, host->selected_finish_clock,
                            host->session.source_frame_clock - host->gp_failure_ready_ticks + 1u,
                            host->session.source_frame_clock, host->gp_result_flow.total_ticks,
                            host->finish_portrait_ticks);
                        for (unsigned rank = 0u; rank < 8u; ++rank)
                        {
                            const PortableGpFinishPortrait *row =
                                &host->finish_portraits.rows[rank];
                            const unsigned racer = host->session.roster.standings[rank];
                            printf("%s%u:%04x/%d/%u/%u", rank ? "," : "", racer,
                                   (unsigned)host->session.roster.racers[racer].source_flags,
                                   row->driver_class, row->progress, row->delay);
                        }
                        putchar('\n');
                    }
                }
                if (status != PORTABLE_GP_RESULTS_OK)
                {
                    host->error = 1;
                    return 0;
                }
            }
            else if (!host->configuration.test_seeded_standings &&
                     portable_gp_cup_session_finish_route(&host->session) ==
                         PORTABLE_GP_CUP_FINISH_RETRY_MENU)
            {
                /* 1E7C8 case1 skips2..26 if no local finisher qualifies.
                 * Retain the lap board and horizontal portrait row. */
                host->gp_result_visible = 1u;
                host->gp_time_board.state = 12u;
                host->gp_time_board.visible = 0u;
                if (!route_audio_begin_ceremony(
                        host->configuration.audio,
                        portable_gp_standings_music(host->snapshot.player_rank)))
                {
                    host->error = 1;
                    return 0;
                }
            }
            else
            {
                host->gp_result_visible = 0u;
                if (host->gp_time_board.ticks == 0u &&
                    !route_audio_begin_ceremony(
                        host->configuration.audio,
                        portable_gp_standings_music(host->snapshot.player_rank)))
                {
                    host->error = 1;
                    return 0;
                }
                if (portable_gp_time_board_tick(&host->gp_time_board, host->skip_results) !=
                    PORTABLE_GP_RESULTS_OK)
                {
                    host->error = 1;
                    return 0;
                }
                if (host->skip_results && host->configuration.input_aid)
                    puts("GP results: confirmed advance");
                if (host->skip_results && host->mobile_results_aid_stage == 3u)
                {
                    host->mobile_results_aid_stage = 4u;
                    puts("GP mobile results: PASS");
                }
            }
            host->skip_results = 0u;
        }
        else
        {
            if (host->gp_failure_ready_ticks == 0u)
                memcpy(host->gp_result_times, host->snapshot.source_result_times_centiseconds,
                       sizeof host->gp_result_times);
            if (host->gp_failure_ready_ticks < 973u)
                ++host->gp_failure_ready_ticks;
            /* A/Start advances the completed board after its source
             * lock; Short Transitions also permits early confirmation.
             * Recorded Quick Runs retain their original timing. */
            if (host->configuration.recording == NULL && host->skip_results &&
                host->gp_failure_ready_ticks < 973u)
            {
                host->gp_failure_ready_ticks = 973u;
                if (host->configuration.input_aid)
                    puts("GP results: confirmed advance");
            }
            if (host->gp_result_flow.state <= 1u)
            {
                if (portable_gp_results_flow_tick_entry(&host->gp_result_flow, 0u, 0u) !=
                    PORTABLE_GP_RESULTS_OK)
                {
                    host->error = 1;
                    return 0;
                }
            }
            else if (host->gp_result_flow.state == 2u)
            {
                if (portable_gp_results_flow_tick_reveal(&host->gp_result_flow,
                                                         (uint8_t)host->snapshot.lap_count) !=
                    PORTABLE_GP_RESULTS_OK)
                {
                    host->error = 1;
                    return 0;
                }
                if (host->gp_result_flow.reveal_pending)
                {
                    host->gp_result_display = host->gp_result_flow;
                    host->gp_result_visible = 1u;
                }
            }
            else
            {
                if (!gp_finish_portraits_tick(host))
                {
                    host->error = 1;
                    return 0;
                }
                if (host->configuration.input_aid && !host->finish_owner_reported &&
                    host->gp_failure_ready_ticks >= 539u)
                {
                    host->finish_owner_reported = 1u;
                    printf("Quick finish owner: course=%u clock=%u portrait_ticks=%u rows=",
                           host->configuration.course_id, host->session.source_frame_clock,
                           host->finish_portrait_ticks);
                    for (unsigned rank = 0u; rank < 8u; ++rank)
                    {
                        const PortableGpFinishPortrait *row = &host->finish_portraits.rows[rank];
                        const unsigned racer = host->session.roster.standings[rank];
                        printf("%s%u:%04x/%d/%u/%u", rank ? "," : "", racer,
                               (unsigned)host->session.roster.racers[racer].source_flags,
                               row->driver_class, row->progress, row->delay);
                    }
                    putchar('\n');
                }
            }
        }
    }
    return !host->error;
}

static int gp_frame_advance_ticks(PortableGpHost *host, uint64_t now)
{
    uint16_t tick_buttons[PORTABLE_FRAME_MAX_CATCH_UP];
    size_t tick_count = 0u;
    int dropped = 0;
    if (portable_frame_scheduler_update(
            &host->scheduler, now, host->focused && !host->paused,
            host->configuration.input_aid
                ? (host->session.player.battle_enabled
                       ? 0u
                       : portable_gp_race_smoke_control(
                             &host->session,
                             portable_race_session_total_ticks(&host->session.runtime.session)))
                : (portable_sdl_keyboard_driving() |
                   portable_sdl_gamepad_driving(host->controller)),
            tick_buttons, PORTABLE_FRAME_MAX_CATCH_UP, &tick_count,
            &dropped) != PORTABLE_FRAME_SCHEDULER_OK)
    {
        host->error = 1;
        return 0;
    }
    (void)dropped;
    size_t processed = 0u;
    for (size_t tick = 0u; tick < tick_count; ++tick)
    {
        if (host->entry.initialized && !host->entry.snapshot.done)
        {
            if (portable_gp_entry_tick(&host->entry, host->scene.sine, host->scene.sine_count) < 0)
            {
                host->error = 1;
                break;
            }
            if (host->session.sky_motion_rules != NULL)
            {
                const PortableSkyMotionInputs input = {.clock = host->entry.ticks,
                                                       .yaw = host->entry.snapshot.camera_yaw};
                if (portable_sky_motion_tick(&host->entry_sky, host->session.sky_motion_rules,
                                             &input, host->scene.sine, host->scene.sine_count) < 0)
                {
                    host->error = 1;
                    break;
                }
            }
            if (!route_audio_render(host->configuration.audio, 0, 0, NULL))
            {
                host->error = 1;
                break;
            }
            host->history_ready = 0u;
            ++processed;
            continue;
        }
        if (!portable_gp_race_presentation_build(&host->session, &host->snapshot,
                                                 &host->previous_state))
        {
            host->error = 1;
            break;
        }
        host->history_ready = 1u;
        if (host->configuration.input_aid && !host->configuration.playback)
        {
            const char *wall = getenv("MKSC_AID_WALL_CONTACT");
            tick_buttons[tick] =
                portable_recovered_wall_contact_aid(tick_buttons[tick],
                                                    wall && strcmp(wall, "left") == 0    ? -1
                                                    : wall && strcmp(wall, "right") == 0 ? 1
                                                                                         : 0,
                                                    &host->session.runtime.session);
            const char *water_rescue = getenv("MKSC_AID_WATER_RESCUE");
            tick_buttons[tick] = portable_recovered_water_rescue_aid(
                tick_buttons[tick], water_rescue != NULL && strcmp(water_rescue, "1") == 0,
                &host->session.player);
            const char *modern_drift=getenv("MKSC_AID_MODERN_DRIFT");
            tick_buttons[tick]=portable_recovered_modern_drift_aid(tick_buttons[tick],
                modern_drift!=NULL && strcmp(modern_drift,"1")==0,&host->session.player,
                host->session.player_dependencies.modern_drift_rules);
            const char *modern_air=getenv("MKSC_AID_MODERN_AIR");
            tick_buttons[tick]=portable_recovered_modern_air_aid(tick_buttons[tick],
                modern_air==NULL?0:strcmp(modern_air,"once")==0?2:strcmp(modern_air,"1")==0,&host->session.player,
                host->session.player_dependencies.modern_drift_rules);
        }
        if (!host->configuration.input_aid && !host->configuration.playback)
            tick_buttons[tick] |= portable_mobile_touch_take_driving_pressed();
        PortableGpInputRecording *recording = host->configuration.recording;
        if (host->configuration.quick_run && recording != NULL)
        {
            if (host->configuration.playback)
            {
                if (host->playback_tick >= recording->count)
                {
                    SDL_SetError("Quick Run replay input exhausted at tick %u",
                                 host->playback_tick);
                    host->error = 1;
                    break;
                }
                tick_buttons[tick] = recording->buttons[host->playback_tick++];
            }
            else if (!recording->overflow)
            {
                if (recording->count == PORTABLE_GP_REPLAY_MAX_TICKS)
                    recording->overflow = 1u;
                else
                    recording->buttons[recording->count++] = tick_buttons[tick];
            }
        }
#ifdef PORTABLE_ENABLE_TEST_HOOKS
        gp_trace_kinematics(host,host->host_ticks + (uint32_t)processed,"before");
#endif
        const PortableGpRaceSessionStatus tick_status =
            portable_gp_race_session_tick(&host->session, tick_buttons[tick], &host->disposition);
#ifdef PORTABLE_ENABLE_TEST_HOOKS
        gp_trace_kinematics(host,host->host_ticks + (uint32_t)processed,"after");
#endif
        if (tick_status != PORTABLE_GP_RACE_SESSION_OK)
        {
            SDL_SetError("GP simulation status=%u driver_status=%d roster_stage=%u",
                         (unsigned)tick_status, host->session.runtime.last_driver_status,
                         (unsigned)host->session.failed_roster_stage);
            host->error = 1;
            break;
        }

        const PortableGpRaceSessionStatus tick_snapshot_status =
            portable_gp_race_session_snapshot(&host->session, &host->snapshot);
        if (tick_snapshot_status != PORTABLE_GP_RACE_SESSION_OK)
        {
            SDL_SetError("GP snapshot status=%u", (unsigned)tick_snapshot_status);
            host->error = 1;
            break;
        }
        if (host->snapshot.phase == PORTABLE_RACE_PHASE_FINISHED &&
            host->selected_finish_clock == 0u)
            host->selected_finish_clock = host->session.source_frame_clock;
        if (!gp_hud_moments(host))
        {
            host->error = 1;
            break;
        }
        if (host->configuration.input_aid || host->configuration.quick_run)
            host->aid_digest =
                aid_tick_digest(host->aid_digest, &host->snapshot, host->session.source_rng_state,
                    host->configuration.course_id, host->host_ticks + (uint32_t)processed);
        const int result_audio = !host->configuration.quick_run && host->gp_result_flow.state >= 4u;
        if (!(result_audio ? route_audio_render(host->configuration.audio, 0, 0, NULL)
                           : route_audio_render_gp(host->configuration.audio, tick_buttons[tick],
                                                   &host->snapshot)))
        {
            SDL_SetError("GP race audio");
            host->error = 1;
            break;
        }
        host->display_buttons = tick_buttons[tick];
        if (host->configuration.rumble)
            portable_sdl_gamepad_race_rumble(
                host->controller, &host->rumble, &host->snapshot.engine_sound,
                &host->snapshot.contact_audio, &host->snapshot.boost_voice,
                host->snapshot.phase == PORTABLE_RACE_PHASE_RACING);

        if (!gp_tick_result_boards(host))
            break;
        ++processed;
        if (host->configuration.quick_run && host->gp_failure_ready_ticks >= 549u)
            break;
    }
    if (processed != 0u)
    {
        host->host_ticks += (uint32_t)processed;
        host->needs_present = 1;
    }
    return !host->error;
}

static int gp_capture_scene_effects(PortableGpHost *host, int render_ok)
{
    if (render_ok && host->session.player.hop.mode == 1u &&
        !portable_race_hop_capture(&host->frame, "gp", host->snapshot.player_height_q16))
        render_ok = 0;
    if (render_ok && host->snapshot.player_kart_contacts &&
        host->session.player.normal_collision_timer_38 > 1u)
    {
        if (!host->kart_contact_seen && host->configuration.input_aid)
            printf("GP kart contact: course=%u driver=%u hold=%u impulse=%d,%d coins=%u\n",
                   host->configuration.course_id, host->configuration.character_id,
                   host->session.player.normal_collision_timer_38,
                   host->session.player.pose.component_x, host->session.player.pose.component_y,
                   host->session.roster.racers[0].coin_count_178);
        host->kart_contact_seen = 1u;
        if (!portable_screen_capture(&host->frame, "gp-kart-contact"))
            render_ok = 0;
    }
    if (render_ok && host->session.player.source_hazard_kind == PORTABLE_DRIVER_HAZARD_VOID)
    {
        const char *sink = host->snapshot.fall_mode == 2u && host->snapshot.fall_frames_76 == 14u &&
                                   !host->snapshot.driver_hidden
                               ? "gp-sink-14"
                           : host->snapshot.fall_mode == 2u &&
                                   host->snapshot.fall_frames_76 == 15u &&
                                   host->snapshot.driver_hidden
                               ? "gp-sink-15"
                           : host->snapshot.fall_mode == 6u && !host->snapshot.fall_frames_76 &&
                                   !host->snapshot.driver_hidden
                               ? "gp-sink-return"
                               : NULL;
        if (sink != NULL && !portable_screen_capture(&host->frame, sink))
            render_ok = 0;
    }
    if (render_ok && host->snapshot.recovery_fx.visible)
    {
        const char *effect = (host->session.hazard_effect_72 & 15u) == 12u &&
                                     host->snapshot.player_palette_bank == 9u
                                 ? "gp-ice-frozen"
                             : host->snapshot.recovery_fx.thaw_age >= 2u ? "gp-ice-thaw"
                                                                         : NULL;
        if (effect != NULL && !portable_screen_capture(&host->frame, effect))
            render_ok = 0;
        for (unsigned i = 0u; i < PORTABLE_RECOVERY_DRIPS; ++i)
            if (host->snapshot.recovery_fx.drips[i].age >= 2u &&
                !portable_screen_capture(&host->frame, "gp-water-drip"))
                render_ok = 0;
    }
    if (render_ok)
        for (unsigned i = 0u; i < PORTABLE_IMPACT_EFFECT_SLOTS; ++i)
        {
            const PortableImpactEffectSlot *impact = &host->snapshot.impact_effects.slots[i];
            if (impact->active && impact->age >= 2u)
            {
                char name[24];
                (void)snprintf(name, sizeof name, "gp-impact-%u", impact->variant);
                if (!portable_screen_capture(&host->frame, name))
                    render_ok = 0;
            }
        }
    if (render_ok)
        for (unsigned i = 0u; i < PORTABLE_COIN_EFFECT_SLOTS; ++i)
        {
            const PortableCoinEffectSlot *coin = &host->snapshot.coin_effects.slots[i];
            if (!coin->visible || coin->age < 12u ||
                (coin->kind != PORTABLE_COIN_EFFECT_BOUNCE &&
                 coin->kind != PORTABLE_COIN_EFFECT_RISE))
                continue;
            const PortableRaceParticleAssetView *art =
                &host->views[0].coin_effect_art[coin->kind == PORTABLE_COIN_EFFECT_RISE ? 1u : 0u];
            unsigned row = 0u, span = 0u;
            while (row < art->frame_count && coin->age > span + art->frames[row].duration + 1u)
            {
                span += art->frames[row].duration + 1u;
                ++row;
            }
            if (row == art->frame_count || art->frames[row].size == 2u)
                continue;
            const char *name = coin->kind == PORTABLE_COIN_EFFECT_RISE ? "gp-coin-rise"
                               : coin->cause == 1u                     ? "gp-coin-hit"
                               : coin->cause == 3u                     ? "gp-coin-contact"
                               : coin->cause == 4u                     ? "gp-coin-crush"
                                                                       : "gp-coin-haunt";
            if (!portable_screen_capture(&host->frame, name))
                render_ok = 0;
        }
    if (render_ok && host->snapshot.surface_trails.visible)
    {
        int trail_drawn = 0;
        for (unsigned i = 0u; i < PORTABLE_SURFACE_TRAIL_SLOTS; ++i)
        {
            const PortableSurfaceTrailSlot *slot = &host->snapshot.surface_trails.slots[i];
            if (slot->age && (slot->age & 3u) && slot->kind >= 1u && slot->kind <= 5u)
            {
                trail_drawn = 1;
                char name[24];
                (void)snprintf(name, sizeof name, "gp-trail-kind%u", slot->kind);
                if (!portable_screen_capture(&host->frame, name))
                    render_ok = 0;
            }
        }
        if (trail_drawn && host->snapshot.surface_trails.extra_spray &&
            !portable_screen_capture(&host->frame, "gp-trail-spray"))
            render_ok = 0;
    }
    if (render_ok && host->views[0].entry_splash.frame_count != 0u &&
        host->snapshot.entry_splash.age >= 20u && host->snapshot.entry_splash.age <= 28u &&
        !portable_screen_capture(&host->frame, host->session.player.source_hazard_kind ==
                                                       PORTABLE_DRIVER_HAZARD_LAVA
                                                   ? "gp-lava-entry"
                                                   : "gp-water-entry"))
        render_ok = 0;
    if (render_ok && host->snapshot.player_affine.mode != 0u &&
        (host->snapshot.player_affine.inverse_x_q8 != 256u ||
         host->snapshot.player_affine.inverse_y_q8 != 256u))
    {
        const char *effect = host->snapshot.player_affine.mode == 1u   ? "gp-landing"
                             : host->snapshot.player_affine.mode == 4u ? "gp-unshrink"
                             : host->snapshot.player_affine.mode == 7u &&
                                     host->snapshot.player_affine.inverse_y_q8 != 384u
                                 ? "gp-crush-rebound"
                                 : NULL;
        if (effect != NULL && !portable_screen_capture(&host->frame, effect))
            render_ok = 0;
    }

    return render_ok;
}

static int gp_draw_race_overlays(PortableGpHost *host, int render_ok,
                                 PortableSoftwareFrame *centered)
{
    if (render_ok && (!host->entry.initialized || host->entry.snapshot.done) &&
        host->snapshot.lakitu_visible)
        render_ok =
            portable_frontend_image_draw_scaled(
                host->configuration.pack, portable_gp_lakitu_image(host->snapshot.lakitu_art_bank),
                centered, host->snapshot.lakitu_x, host->snapshot.lakitu_y, 0, 0,
                (int)host->frame.width, (int)host->frame.height,
                host->snapshot.lakitu_scale_q8) == PORTABLE_FRONTEND_PRESENTATION_OK;
    if (render_ok && (!host->entry.initialized || host->entry.snapshot.done) &&
        !host->snapshot.results_ready)
        render_ok = portable_gp_countdown_render(
            host->configuration.pack, host->snapshot.phase, host->race.rules.countdown_ticks,
            host->snapshot.countdown_remaining, host->snapshot.total_ticks, centered);
    if (render_ok && host->snapshot.results_ready && !host->session.player.battle_enabled)
    {
        render_ok = portable_gp_results_render_coins(
                        &host->views[0].gp_results,
                        (uint8_t)host->session.roster.racers[0].coin_count_178, &host->frame) &&
                    portable_gp_results_render_rank(&host->views[0].gp_results,
                                                    host->snapshot.player_rank, &host->frame);
    }

    if (render_ok && host->entry.initialized && !host->entry.snapshot.done)
    {
        /* Modern cards replace only the classic panel/veil. The source
         * camera, arrival and HUD owners retain their admitted ticks. */
        if (!host->configuration.modern_menus)
            render_ok = portable_gp_opening_render(&host->views[0].gp_opening, &host->entry.snapshot,
                                                   host->session.engine_class, &host->frame);
        if (render_ok) render_ok = gp_entry_hud(host);
        const char *capture = host->entry.snapshot.hud_progress >= 8u ? "gp-entry-hud"
                              : host->entry.ticks >= 159u             ? "gp-entry-arrival"
                              : host->entry.ticks >= 100u             ? "gp-entry-panel"
                              : host->entry.ticks >= 16u              ? "gp-entry-reveal"
                                                                      : NULL;
        if (render_ok && capture != NULL)
            render_ok = portable_screen_capture(&host->frame, capture);
    }
    PortableFrontendPresentationStatus menu_status = PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
    if (render_ok && host->pause_menu_active && host->configuration.modern_menus)
        menu_status = PORTABLE_FRONTEND_PRESENTATION_OK; /* Drawn over the race at presentation. */
    else if (render_ok && host->pause_menu_active)
    {
        const PortableMenuPaletteProfile profile =
            portable_gp_results_menu_profile(&host->views[0].gp_results);
        menu_status = portable_pause_menu_render_menu_palette(
            host->configuration.pack, &host->pause_menu, &profile, &host->frame);
        if (menu_status != PORTABLE_FRONTEND_PRESENTATION_OK &&
            menu_status != PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION)
            render_ok = 0;
    }
    const char *script_message = portable_script_host_message(host->configuration.scripts);
    if (render_ok && script_message != NULL && !host->pause_menu_active)
    {
        /* Mod notices: the ROM window font, up to three 26-glyph lines. */
        render_ok = portable_menu_notice(host->configuration.pack, centered, script_message);
        if (render_ok && !portable_screen_capture(&host->frame, "script-message"))
            render_ok = 0;
    }
    if (render_ok && host->paused && menu_status != PORTABLE_FRONTEND_PRESENTATION_OK)
    {
        /* Focus or pad loss freezes the race without the 2C06C list. */
        const PortableAssetPackView *pack = host->configuration.pack;
        const PortableFrontendPresentationStatus panel =
            portable_menu_panel(pack, centered, 20, 56, 200u, 48u);
        render_ok = panel == PORTABLE_FRONTEND_PRESENTATION_OK ||
                    panel == PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION;
        (void)portable_menu_text_centered(pack, centered, 120, 64, "PAUSED");
        (void)portable_menu_text_centered(pack, centered, 120, 80, "START / ENTER TO RESUME");
        if (render_ok && !portable_screen_capture(&host->frame, "race-freeze"))
            render_ok = 0;
    }

    return render_ok;
}

static int gp_draw_frame_diagnostics(PortableGpHost *host, int render_ok)
{
    if (render_ok && host->snapshot.snow_weather.enabled &&
        host->session.runtime.session.phase == PORTABLE_RACE_PHASE_RACING)
    {
        const char *name = host->session.source_frame_clock < 240u   ? "gp-snow-0"
                           : host->session.source_frame_clock < 360u ? "gp-snow-1"
                                                                     : "gp-snow-2";
        if (!portable_screen_capture(&host->frame, name))
            render_ok = 0;
    }
    if (render_ok && host->snapshot.finish_camera.snapshot.active)
    {
        const uint8_t frame = host->snapshot.player_art_pose.frame_override;
        if (frame >= 28u && frame <= 31u)
        {
            char capture[24];
            (void)snprintf(capture, sizeof capture, "gp-finish-%u", frame);
            if (!portable_screen_capture(&host->frame, capture))
                render_ok = 0;
        }
        if (host->snapshot.finish_camera.phase == 9u &&
            host->snapshot.finish_camera.orbit_yaw >= 8192 &&
            !portable_screen_capture(&host->frame, "gp-finish-orbit"))
            render_ok = 0;
    }
    const char *hud_moments = getenv("MKSC_CAPTURE_HUD");
    if (render_ok && hud_moments && strcmp(hud_moments, "1") == 0 && host->selected_finish_clock)
    {
        static const uint32_t ages[3] = {32u, 80u, 220u};
        static const char *const names[3] = {"gp-finish-age-32", "gp-finish-age-80",
                                             "gp-finish-age-220"};
        const uint32_t age = host->session.source_frame_clock - host->selected_finish_clock;
        for (unsigned i = 0u; i < 3u; ++i)
            if (age >= ages[i] && !(host->finish_capture_mask & (1u << i)))
            {
                render_ok = portable_screen_capture(&host->frame, names[i]);
                host->finish_capture_mask |= (uint8_t)(1u << i);
                printf("GP finish moment: age=%u lakitu=%u art=%u visible=%u x=%d y=%d scale=%u\n",
                       age, host->session.hazard_lakitu.phase, host->snapshot.lakitu_art_bank,
                       host->snapshot.lakitu_visible, host->snapshot.lakitu_x,
                       host->snapshot.lakitu_y, host->snapshot.lakitu_scale_q8);
                if (!render_ok)
                    break;
            }
    }
    if (render_ok && host->snapshot.player_art_pose.frame_override >= 24u &&
        host->snapshot.player_art_pose.frame_override <= 27u && !host->snapshot.player_revenge_bomb)
    {
        char capture[32];
        (void)snprintf(capture, sizeof capture, "gp-contact-%u",
                       host->snapshot.player_art_pose.frame_override);
        if (!portable_screen_capture(&host->frame, capture))
            render_ok = 0;
    }
    if (render_ok && host->configuration.input_display && !host->snapshot.results_ready &&
        !host->pause_menu_active)
    {
        int16_t stick_x, stick_y;
        portable_sdl_gamepad_stick(host->controller, &stick_x, &stick_y);
        portable_input_display_render_corner(&host->frame, host->display_buttons, stick_x, stick_y);
    }
    if (render_ok && getenv("MKSC_CAPTURE_AMBIENT") != NULL && host->snapshot.ambient.initialized &&
        host->session.ambient.post_serial >= 10u)
    {
        const uint32_t serial = host->session.ambient.post_serial;
        const char *name = serial < 30u   ? "gp-ambient-0"
                           : serial < 50u ? "gp-ambient-1"
                                          : "gp-ambient-2";
        if (!portable_screen_capture(&host->frame, name))
            render_ok = 0;
    }
    if (render_ok && host->snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
        host->snapshot.player_speed_q16 > 65536 && !host->snapshot.player_revenge_bomb &&
        host->snapshot.player_art_motion.wheel_palette_valid)
    {
        const uint8_t wheel = host->snapshot.player_art_motion.wheel_phase;
        const char *name = wheel == 0u   ? "gp-wheels-0"
                           : wheel == 1u ? "gp-wheels-1"
                                         : "gp-wheels-2";
        if (!portable_screen_capture(&host->frame, name))
            render_ok = 0;
    }
    if (render_ok && host->scene.driver_projection != NULL &&
        host->snapshot.phase == PORTABLE_RACE_PHASE_COUNTDOWN &&
        host->snapshot.countdown_remaining <= 100u &&
        !portable_screen_capture(&host->frame, "gp-grid"))
        render_ok = 0;
    if (render_ok && host->scene.driver_projection != NULL &&
        host->snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
        host->session.runtime.session.elapsed_ticks >= 300u &&
        !portable_screen_capture(&host->frame, "gp-distance"))
        render_ok = 0;
    if (render_ok && host->snapshot.sky_motion.initialized &&
        host->snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
        host->session.runtime.session.elapsed_ticks >= 300u)
    {
        char sky_capture[40];
        (void)snprintf(sky_capture, sizeof sky_capture, "gp-sky-%u", host->configuration.course_id);
        if (!portable_screen_capture(&host->frame, sky_capture))
            render_ok = 0;
    }
    if (render_ok && host->snapshot.sky_motion.split_vertical &&
        !portable_screen_capture(&host->frame, "gp-sky-night"))
        render_ok = 0;
    if (render_ok && !host->paused && host->snapshot.phase == PORTABLE_RACE_PHASE_RACING &&
        !portable_race_motion_capture(&host->frame, "gp", host->snapshot.total_ticks))
        render_ok = 0;

    return render_ok;
}

/* Compose and capture in source order before uploading the same frame. */
static int gp_frame_present(PortableGpHost *host, uint64_t now)
{
    const int smooth = host->configuration.interpolate && host->focused && !host->paused;
    if (smooth)
        host->needs_present =
            portable_present_due(now, host->configuration.render_fps, &host->last_present_ns);
    if (host->needs_present)
    {
        const PortableGpRaceSessionStatus snapshot_status =
            portable_gp_race_session_snapshot(&host->session, &host->snapshot);
        if (!gp_resize_surface(host))
        {
            host->error = 1;
            return 0;
        }
        PortableSoftwareFrame centered =
            portable_software_frame_native_view(&host->frame, PORTABLE_FRAME_CENTER);
        const int presentation_ok =
            snapshot_status == PORTABLE_GP_RACE_SESSION_OK &&
            portable_gp_race_presentation_build(&host->session, &host->snapshot, &host->state);
        if (host->visual_wall_mutation_count != host->session.player.wall_mutation_count)
        {
            /* Keep coin art separate while publishing native terrain changes. */
            for (size_t cell = 0; cell < host->scene.world_map.map_bytes; ++cell)
                if (host->storage->terrain_cells[cell] !=
                    host->storage->initial_terrain_cells[cell])
                    host->visual_world_map[cell] = host->storage->terrain_cells[cell];
            host->visual_wall_mutation_count = host->session.player.wall_mutation_count;
        }
        /* Source sub_8039B38 replaces a collected coin's BG2 tile
         * with 0xc1. The presentation map is separate from terrain. */
        for (size_t index = 0u; index < host->session.coin_count; ++index)
        {
            const PortableGpCoinState *coin = &host->session.coins[index];
            if (coin->active)
                continue;
            const int32_t x = coin->placement.position_x_q16 / (8 * 65536);
            const int32_t y = coin->placement.position_y_q16 / (8 * 65536);
            if (x >= 0 && y >= 0 && x < (int32_t)host->scene.world_map.width_tiles &&
                y < (int32_t)host->scene.world_map.height_tiles)
                host->visual_world_map[(size_t)y * host->scene.world_map.width_tiles + (size_t)x] =
                    UINT8_C(0xc1);
        }
        int render_ok = presentation_ok;
        if (render_ok && host->snapshot.results_ready && host->gp_result_flow.state >= 2u &&
            !host->gp_result_hud_ready)
        {
            render_ok = portable_race_scene_capture_lap_board_hud(&host->scene, &host->state,
                                                                  &host->gp_result_hud);
            host->gp_result_hud_ready = (uint8_t)render_ok;
        }
        if (render_ok)
        {
            host->render_state = host->state;
            if (smooth && host->history_ready)
                portable_race_interpolate(&host->previous_state, &host->state,
                                          portable_frame_scheduler_alpha(&host->scheduler),
                                          &host->render_state);
            if (host->gp_result_visible)
                host->render_state.result_board_visible = 1u;
            if (!host->configuration.quick_run && host->gp_result_flow.state >= 4u)
                host->render_state.suppress_native_hud = 1u;
            if (host->entry.initialized && !host->entry.snapshot.done)
                render_ok = gp_entry_pose(host);
            if (render_ok)
                render_ok = portable_race_scene_render(&host->scene, &host->render_state, &host->frame);
            if(render_ok && host->render_state.modern_drift_enabled)
                render_ok=portable_screen_capture_drift(&host->frame,"gp",&host->render_state.modern_drift);
            if(render_ok && host->render_state.modern_air_enabled)
                render_ok=portable_screen_capture_air(&host->frame,"gp",&host->render_state.modern_air);
        }
        const int modern_results=host->configuration.modern_menus &&
            (host->gp_result_visible||(!host->configuration.quick_run&&host->gp_time_board.visible));
        if(!modern_results)modern_menu_host_results_closed(&host->modern);
        if (render_ok && !modern_results && !host->configuration.quick_run && host->gp_time_board.visible)
        {
            PortableSoftwareFrame board =
                portable_software_frame_native_view(&host->frame, PORTABLE_FRAME_CENTER);
            render_ok = portable_gp_time_board_render(
                &host->views[0].gp_standings, host->gp_time_rows, host->gp_time_points,
                host->gp_time_awards, &host->gp_time_board, host->session.engine_class, &board);
            if (render_ok && host->gp_time_board.state == 8u)
                render_ok = portable_screen_capture(&host->frame, "gp-time-board");
            if (render_ok && host->gp_time_board.state == 10u)
                render_ok = portable_screen_capture(&host->frame, "gp-points-reveal");
        }
        if (render_ok && !modern_results && host->gp_result_visible)
        {
            render_ok =
                host->gp_result_hud_ready &&
                portable_gp_results_render_laps_with_hud(
                    &host->views[0].gp_results, &host->gp_result_display, host->gp_result_times,
                    (uint8_t)host->snapshot.lap_count, &host->gp_result_hud, &host->frame);
            if (render_ok && (host->gp_result_display.reveal_progress == 1u ||
                              host->gp_result_display.reveal_progress == 3u ||
                              host->gp_result_display.reveal_progress == 7u))
            {
                char name[24];
                (void)snprintf(name, sizeof name, "gp-lap-slide-%u",
                               host->gp_result_display.reveal_progress);
                if (!portable_screen_capture(&host->frame, name))
                    render_ok = 0;
            }
            if (render_ok && host->configuration.quick_run && host->gp_result_flow.state == 3u)
            {
                render_ok = portable_hud_render_quick_finish_portraits(
                    &host->views[0].gp_race_hud, &host->views[0].hud, &host->finish_portraits,
                    host->scene.gp_racer_portraits, &host->frame);
                if (render_ok && host->gp_failure_ready_ticks >= 539u)
                    render_ok = portable_screen_capture(&host->frame, "quick-finish-portraits");
            }
            if (render_ok && !host->configuration.quick_run && host->gp_result_flow.state == 3u)
            {
                render_ok = portable_hud_render_gp_finish_portraits(
                    &host->views[0].gp_race_hud, &host->views[0].hud, &host->finish_portraits,
                    host->scene.gp_racer_portraits, host->session.source_frame_clock, &host->frame);
                const char *capture = host->gp_result_flow.total_ticks >= 539u
                                          ? "gp-finish-complete"
                                      : host->finish_portrait_ticks >= 96u ? "gp-finish-settled"
                                      : host->finish_portrait_ticks >= 32u ? "gp-finish-slide"
                                                                           : NULL;
                if (render_ok && capture != NULL)
                    render_ok = portable_screen_capture(&host->frame, capture);
            }
            if (render_ok && host->gp_result_display.reveal_progress == 8u &&
                !portable_screen_capture(
                    &host->frame, host->snapshot.lap_count == 3u ? "lap-board" : "lap-board-long"))
                render_ok = 0;
        }
        render_ok = gp_capture_scene_effects(host, render_ok);
        render_ok = gp_draw_race_overlays(host, render_ok, &centered);
        render_ok = gp_draw_frame_diagnostics(host, render_ok);
        const char *capture = getenv("MKSC_RACE_FRAME");
        if (!host->race_frame_captured && host->session.source_frame_clock >= 300u &&
            capture != NULL && capture[0] != '\0')
        {
            if (!portable_software_frame_write_ppm(&host->frame, capture))
            {
                host->error = 1;
                return 0;
            }
            host->race_frame_captured = 1u;
        }
        portable_software_frame_copy_native(&host->frame, host->pixels);
        const int modern_pause = host->configuration.modern_menus && host->pause_menu_active;
        if (!modern_pause) modern_menu_host_pause_closed(&host->modern);
        const char *mode = host->configuration.battle ? "Battle"
                           : host->configuration.quick_run ? "Quick Run"
                                                           : "Grand Prix";
        uint32_t entry_elapsed = 0u;
        const int modern_entry = gp_modern_entry_active(host, &entry_elapsed);
        const int present_ok =
            render_ok &&
            (modern_pause ? modern_menu_host_present_pause(&host->modern, host->renderer, &host->frame,
                                                            host->configuration.fit_scaling, &host->pause_menu, mode,
                                                            host->configuration.battle ? "battle-pause-modern"
                                                            : host->configuration.quick_run ? "quick-pause-modern"
                                                                                            : "gp-pause-modern")
                          : modern_results ? gp_present_modern_results(host)
                          : modern_entry ? gp_present_modern_entry(host, entry_elapsed, mode)
                          : portable_software_frame_present(
                                host->renderer, host->render_texture != NULL ? host->render_texture : host->texture,
                                &host->frame, host->configuration.fit_scaling));
        if (!present_ok)
        {
            if (!render_ok)
                SDL_SetError("GP presentation snapshot=%u build=%d", (unsigned)snapshot_status,
                             presentation_ok);
            host->error = 1;
            return 0;
        }
        if(host->pause_aid.stage==2u && host->pause_menu_active &&
            host->pause_menu.open_ticks>=PORTABLE_PAUSE_MENU_OPEN_TICKS && !host->pause_aid.presented){
            if(modern_pause){
                if(host->modern.pause_frames>=PORTABLE_PAUSE_MENU_OPEN_TICKS){
                    PortableSoftwareFrame shown;
                    if(!portable_software_frame_init(&shown,host->modern.pixels,
                        (size_t)host->modern.width,(size_t)host->modern.height) ||
                        !portable_screen_capture(&shown,"gp-pause-modern")){host->error=1;return 0;}
                    host->pause_aid.presented=1u;
                }
            }else{
                if(!portable_screen_capture(&host->frame,"gp-pause-classic")){host->error=1;return 0;}
                host->pause_aid.presented=1u;
            }
        }
        host->needs_present = 0;
        ++host->aid_presents;
    }
    return !host->error;
}

/* Enter a result host only after this frame has finished presenting. */
static int gp_frame_begin_results(PortableGpHost *host)
{
    PortableGpRaceHostOutcome *outcome = &host->outcome;
    if (host->session.player.battle_enabled && host->snapshot.results_ready)
    {
        PortableBattleResults results;
        if (portable_battle_results_build(&host->session, &results) != PORTABLE_GP_RACE_RESULTS_OK)
        {
            SDL_SetError("Battle results course=%u phase=%02x", host->configuration.course_id,
                         host->session.roster.race_phase_7d4);
            host->error = 1;
            return 0;
        }
        host->results = portable_battle_results_begin(
            &host->ui_text, host->pixels, host->configuration.audio, host->renderer, host->texture,
            &host->views[0].gp_results, &host->views[0].gp_standings, &results,
            host->configuration.engine_class);
        if (host->results == NULL || !gp_results_video(host))
        {
            host->error = 1;
            return 0;
        }
        if (host->configuration.input_aid)
            printf("Battle ended: course=%u ticks=%u rank=%u balloons=%u/%u/%u/%u cpu_boosts=%u\n",
                   host->configuration.course_id, host->snapshot.total_ticks,
                   host->snapshot.player_rank, results.rows[0].balloons, results.rows[1].balloons,
                   results.rows[2].balloons, results.rows[3].balloons,
                   host->session.roster.cpu_mushroom_uses);
        return 1;
    }
    if (host->configuration.quick_run && host->snapshot.results_ready)
    {
        if (host->gp_failure_ready_ticks >= 549u)
        {
            PortableQuickRunResults results;
            if (portable_quick_run_results_build(&host->session, &results) !=
                PORTABLE_GP_RACE_RESULTS_OK)
            {
                SDL_SetError("Quick Run results course=%u mode=%04x phase=%02x",
                             host->configuration.course_id,
                             host->session.roster.race_mode_flags_7da,
                             host->session.roster.race_phase_7d4);
                host->error = 1;
                return 0;
            }
            PortableGpInputRecording *recording = host->configuration.recording;
            if (recording != NULL)
            {
                if (host->configuration.playback)
                {
                    if (host->playback_tick != recording->count ||
                        host->aid_digest != recording->digest)
                    {
                        SDL_SetError("Quick Run replay diverged: tick=%u/%u digest=%016llx/%016llx",
                                     host->playback_tick, recording->count,
                                     (unsigned long long)host->aid_digest,
                                     (unsigned long long)recording->digest);
                        host->error = 1;
                        return 0;
                    }
                    if (host->configuration.input_aid)
                        puts("Quick Run replay: PASS fixed-tick snapshot/RNG digest matches");
                }
                else
                {
                    recording->results = results;
                    recording->digest = host->aid_digest;
                    recording->complete = (uint8_t)!recording->overflow;
                }
            }
            host->results = portable_quick_results_begin(
                &host->ui_text, host->pixels, (uint8_t)(recording != NULL && recording->complete),
                host->configuration.audio, host->renderer, host->texture,
                &host->views[0].gp_results, &host->views[0].gp_standings, &results);
            if (host->results == NULL || !gp_results_video(host))
            {
                host->error = 1;
                return 0;
            }
            portable_gp_results_source_clock(host->results, host->session.source_frame_clock);
            portable_gp_results_set_race_background(host->results, gp_standings_background, host);
        }
        return 1;
    }
    const PortableGpCupFinishRoute finish_route =
        host->snapshot.results_ready ? (host->configuration.test_seeded_standings
                                            ? PORTABLE_GP_CUP_FINISH_STANDINGS
                                            : portable_gp_cup_session_finish_route(&host->session))
                                     : PORTABLE_GP_CUP_FINISH_NOT_READY;
    /* 1E7C8 cases3..11 finish before the existing12..25 owner. */
    if (host->snapshot.results_ready && host->gp_time_board.state >= 12u &&
        (finish_route == PORTABLE_GP_CUP_FINISH_STANDINGS ||
         finish_route == PORTABLE_GP_CUP_FINISH_RETRY_MENU))
    {
        const PortableGpCupFinishRoute route = finish_route;
        if (!host->snapshot.source_result_times_ready)
        {
            host->error = 1;
            return 0;
        }

        if (route == PORTABLE_GP_CUP_FINISH_RETRY_MENU)
        {
            host->results = portable_gp_retry_begin(host->configuration.pack, host->pixels,
                                                    host->session.retry_credits, host->renderer,
                                                    host->texture, host->configuration.audio);
            if (host->results == NULL || !gp_results_video(host))
            {
                host->error = 1;
                return 0;
            }
            portable_gp_results_set_race_background(host->results, gp_failure_background, host);
            portable_gp_results_failure_palette(host->results, &host->views[0].gp_results);
            portable_gp_results_source_clock(host->results, host->session.source_frame_clock);
            printf("GP failed result: rank=%u credits=%u lap-board=retained time-board=skipped\n",
                   host->snapshot.player_rank, host->session.retry_credits);
            return 1;
        }
        if (route != PORTABLE_GP_CUP_FINISH_STANDINGS)
        {
            host->error = 1;
            return 0;
        }
        PortableGpCupSession standings;
        const PortableGpCupSessionStatus standings_status = portable_gp_cup_session_begin_standings(
            &host->session, host->configuration.previous_cup, host->configuration.cup_index,
            host->configuration.course_in_cup, host->configuration.course_header_index,
            host->configuration.track_set, &standings);
        if (standings_status != PORTABLE_GP_CUP_SESSION_OK)
        {
            SDL_SetError(
                "GP standings status=%u set=%u cup=%u track=%u laps=%u/%u flags=%04x race_phase=%02x",
                (unsigned)standings_status, host->configuration.track_set,
                host->configuration.cup_index, host->configuration.course_in_cup,
                host->session.runtime.session.laps_completed,
                host->session.runtime.session.lap_count, host->session.roster.race_mode_flags_7da,
                host->session.roster.race_phase_7d4);
            host->error = 1;
            return 0;
        }
#ifdef PORTABLE_ENABLE_TEST_HOOKS
        if (host->configuration.test_seeded_standings)
        {
            const unsigned wanted = host->configuration.previous_cup->rows[0].standing_rank;
            if (standings.rows[0].standing_rank != wanted)
            {
                SDL_SetError("Seeded standings rank=%u expected=%u",
                             standings.rows[0].standing_rank, wanted);
                host->error = 1;
                return 0;
            }
            printf("GP seeded standings: actual-race-rank=%u cup-rank=%u\n",
                   host->snapshot.player_rank, standings.rows[0].standing_rank);
        }
#endif
        if (host->configuration.input_aid)
            printf("GP points: track=%u rank=%u award=%u total=%u\n", standings.course_in_cup,
                   standings.rows[0].finish_rank, standings.rows[0].points_awarded,
                   standings.cumulative_points[0]);
        host->results =
            portable_gp_standings_begin(&host->ui_text, &host->views[0].gp_standings, &standings,
                                        host->renderer, host->texture, host->configuration.audio);
        if (host->results == NULL || !gp_results_video(host))
        {
            SDL_SetError("GP standings presentation begin");
            host->error = 1;
            return 0;
        }
        host->session.finish_camera.phase = 15u;
        host->session.finish_camera.snapshot.active = 1u;
        portable_gp_results_set_race_background(host->results, gp_standings_background, host);
        if (host->configuration.fast_menus)
            portable_gp_results_allow_skip(host->results);
        if (host->gp_time_board.skip_transfer)
            portable_gp_results_skip_transfer(host->results);
        if (outcome != NULL)
        {
            outcome->cup_session = standings;
            outcome->cup_session_ready = 1u;
        }

        return 1;
    }
    return !host->error;
}

int portable_gp_host_frame(PortableGpHost *host)
{
    if (host == NULL || host->error)
        return -1;
    if (host->quit)
        return 1;
    const uint64_t now =
        host->configuration.input_aid
            ? (host->aid_now +=
               host->configuration.interpolate
                   ? UINT64_C(1000000000) /
                         (host->configuration.render_fps ? host->configuration.render_fps : 240u)
                   : UINT64_C(16742707))
            : frontend_now_ns(host->frequency);
    if (host->results != NULL)
        return gp_frame_results_active(host, now);
    if (!gp_mobile_results_aid(host))
    {
        host->error = 1;
        return -1;
    }
    const unsigned lifecycle = portable_mobile_lifecycle_take();
    if (lifecycle & PORTABLE_MOBILE_BACKGROUND) {
        portable_mobile_touch_clear();
        host->mobile_pause_cancel = 0u;host->mobile_results_continue=0u;
        SDL_ResetKeyboard();
        const SDL_Event lost = {.type = SDL_EVENT_WINDOW_FOCUS_LOST};
        if (host->focused) portable_gp_host_event(host, &lost);
        if (!host->snapshot.results_ready && !host->pause_menu_active) gp_open_pause_menu(host);
    }
    if (lifecycle & PORTABLE_MOBILE_FOREGROUND) {
        const SDL_Event gained = {.type = SDL_EVENT_WINDOW_FOCUS_GAINED};
        portable_gp_host_event(host, &gained);
    }
    if (host->mobile_results_aid_stage == 1u &&
        (lifecycle & (PORTABLE_MOBILE_BACKGROUND | PORTABLE_MOBILE_FOREGROUND)) ==
            (PORTABLE_MOBILE_BACKGROUND | PORTABLE_MOBILE_FOREGROUND))
        host->mobile_results_aid_stage = 2u;
    if (lifecycle & PORTABLE_MOBILE_TERMINATING) {
        portable_mobile_touch_clear();
        host->app_quit = 1; host->quit = 1;
        return 1;
    }
    if (host->mobile_results_continue && host->focused && !host->paused &&
        host->snapshot.results_ready && gp_board_confirm_allowed(host)) {
        host->skip_results=1u;host->mobile_results_continue=0u;
    }
    if (host->paused) (void)portable_mobile_touch_take_driving_pressed();
    if (!host->pause_menu_active) (void)portable_mobile_touch_take_pressed();
    const int prepared = gp_frame_prepare_input(host, now);
    if (prepared != 0)
        return prepared;
    if (!gp_frame_advance_ticks(host, now))
        return -1;
    if (!gp_frame_present(host, now))
        return -1;
    if (!gp_frame_begin_results(host))
        return -1;
    return host->error ? -1 : host->quit ? 1 : 0;
}

int run_gp_race_sdl_scene(const PortableAssetPackView *pack, uint32_t course_id,
                          uint32_t character_id, SDL_Window *host_window,
                          SDL_Renderer *host_renderer, SDL_Texture *host_texture,
                          const PortableGpCupSession *previous_cup, uint8_t course_in_cup,
                          uint8_t course_header_index, RouteAudioHost *audio,
                          PortableGpRaceHostOutcome *outcome)
{
    if (outcome != NULL)
        memset(outcome, 0, sizeof *outcome);
    PortableGpCupDefinition definition = {0};
    uint8_t cup_index = 0u;
    int found = 0;
    for (uint8_t cup = 0u; cup < PORTABLE_FRONTEND_COURSE_CUPS && !found; ++cup)
    {
        if (!portable_frontend_gp_cup(pack, cup, &definition))
            continue;
        for (uint8_t track = 0u; track < 4u; ++track)
            if (definition.course_ids[track] == course_id)
            {
                cup_index = definition.cup_index;
                course_in_cup = track;
                course_header_index = definition.source_headers[track];
                found = 1;
                break;
            }
    }
    if (!found)
    {
        fprintf(stderr, "GP course %u has no complete version-2 cup catalog\n", course_id);
        return 1;
    }
    PortableGpScoringRules scoring;
    if (!portable_recovered_gp_scoring(pack, &scoring))
        return 1;
    const PortableGpHostScene configuration = {
        pack,
        course_id,
        character_id,
        host_window,
        host_renderer,
        host_texture,
        previous_cup,
        course_in_cup,
        course_header_index,
        audio,
        previous_cup != NULL ? previous_cup->engine_class : 0u,
        cup_index,
        previous_cup != NULL ? previous_cup->retry_credits : scoring.initial_retry_credits,
        definition.track_set,
        0u,
        0u,
        0u,
        0u,
        {0},
        NULL,
        0u,
        0u,
        0u,
        1u,
        0u,
        240u,
        0u,
        NULL,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        {0}};
    PortableGpHost *host = portable_gp_host_begin(&configuration);
    if (host == NULL)
        return 1;
    for (;;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            portable_gp_host_event(host, &event);
        if (portable_gp_host_frame(host) != 0)
            break;
        SDL_Delay(1);
    }
    return portable_gp_host_end(host, outcome);
}

int run_gp_race_sdl(const PortableAssetPackView *pack, uint32_t course_id, uint32_t character_id)
{
    PortableGpRaceHostOutcome outcome = {0};
    return run_gp_race_sdl_scene(pack, course_id, character_id, NULL, NULL, NULL, NULL, 0u, 0u,
                                 NULL, &outcome);
}

#endif
