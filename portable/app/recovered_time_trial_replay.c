#include "recovered_time_trial_replay.h"

#include <ctype.h>
#include <inttypes.h>
#include "../core/text_number.h"
#include <string.h>

enum { REPLAY_STOCK_FIELD_COUNT = 198, REPLAY_MODERN_FIELD_COUNT = 204, REPLAY_STATE_FIELD_COUNT = 210 };

static int modern_replay(const PortableRecoveredTimeTrialSession *session)
{
    return session->runtime.recovered_dependencies != NULL &&
        session->runtime.recovered_dependencies->modern_drift_rules != NULL;
}

static int air_replay(const PortableRecoveredTimeTrialSession *session)
{
    return modern_replay(session) &&
        session->runtime.recovered_dependencies->modern_drift_rules->air.launch_mask != 0u;
}

static size_t replay_field_count(const PortableRecoveredTimeTrialSession *session)
{
    return air_replay(session) ? REPLAY_STATE_FIELD_COUNT :
        modern_replay(session) ? REPLAY_MODERN_FIELD_COUNT : REPLAY_STOCK_FIELD_COUNT;
}

static uint32_t replay_version(const PortableRecoveredTimeTrialSession *session)
{
    return air_replay(session) ? PORTABLE_RECOVERED_REPLAY_AIR_VERSION :
        modern_replay(session) ? PORTABLE_RECOVERED_REPLAY_MODERN_VERSION :
        PORTABLE_RECOVERED_REPLAY_VERSION;
}

static const char *const state_field_names[REPLAY_STATE_FIELD_COUNT] = {
    "phase", "position_x_q16", "position_y_q16", "position_z_q16",
    "pose_component_x", "pose_component_y", "track_cell",
    "track_cell_valid", "speed_q16", "acceleration_q16", "heading",
    "motion_component_x", "motion_component_y", "input_control_word",
    "input_speed_target_q16", "steering_control_word", "steering_angle",
    "steering_offset", "steering_base_angle", "steering_state_flags",
    "steering_response_q8", "steering_response_flags",
    "steering_response_status", "steering_response_slot",
    "steering_response_selector_flags", "steering_response_selector_index",
    "steering_response_row",
    "steering_spin_delay", "steering_drift_angle", "steering_exit_timer", "steering_driver_mode",
    "mushrooms_remaining", "item_requests_b2", "item_cue_flags_58", "boost_particle_age", "boost_particle_birth_height_q16", "previous_item_control",
    "hop_velocity_q8",
    "hop_mode", "drift_charge", "charged_exit_active",
    "charged_exit_count", "charged_audio_cue_count",
    "charged_particle_count", "charged_particle_age", "auto_speed_q16",
    "auto_acceleration_q16",
    "brake_speed_q16", "brake_acceleration_q16", "forward_speed_q16",
    "forward_acceleration_q16", "forward_response_q8", "laps_completed",
    "lap_hud_pending", "lap_hud_displayed", "lap_hud_initialized",
    "countdown_remaining", "next_route_index", "elapsed_ticks",
    "total_ticks", "current_lap_ticks", "rescue_count", "paused",
    "last_runtime_status", "last_driver_status", "last_material_status",
    "boundary_exit", "water_entry", "last_material_flags",
    "last_material_flags_valid", "water_active",
    "recovery_height", "recovery_timer", "finish_armed",
    "hazard_kind", "recovery_requests", "hazard_effect_72", "lakitu_x",
    "lakitu_y", "lakitu_visible", "lakitu_wait", "lakitu_counter",
    "lakitu_art_bank", "lakitu_flags", "lakitu_phase", "lakitu_callback",
    "lakitu_script", "lakitu_drop_started", "lakitu_exit_velocity",
    "lakitu_retrieval_effect_started", "lakitu_finish_turn", "lakitu_inverse_q8", "contact_word", "contact_direction",
    "contact_scheduler", "contact_cell", "contact_cell_valid",
    "contact_timer_38", "contact_timer_62", "contact_guard_66", "lightning_timer", "invulnerability_timer", "flattened_64",
    "wall_mutation_count", "wall_mutation_digest", "wall_mutation_cell", "strong_spin_rate", "scene_clock", "recovery_flags_74", "contact_anchor_x", "contact_anchor_y", "palette_requests", "palette_bank",
    "checkpoint_raw", "lap_raw", "max_progress_raw", "checkpoint_next_index", "lap_started", "lakitu_warning_turn", "lakitu_cleared_requests",
    "affine_x", "affine_y", "affine_mode", "affine_row", "affine_remaining", "affine_pending",
    "wheel_countdown", "wheel_phase", "bob_phase", "wheel_bank", "wheel_valid", "art_initialized", "cell_bob",
    "sky_initialized", "sky_phase", "sky_prior_vertical_offset", "sky_elapsed", "sky_transition_counter", "sky_horizon_y", "sky_transition_clock", "sky_raw_y0", "sky_raw_y1", "sky_raw_y2",
    "sky_view_scroll_x_0", "sky_view_scroll_x_1", "sky_view_scroll_x_2", "sky_view_scroll_y_0", "sky_view_scroll_y_1", "sky_view_scroll_y_2", "sky_view_layer_mask", "sky_view_blend_mode", "sky_view_blend_a", "sky_view_blend_b", "sky_view_split_y", "sky_view_split_layer", "sky_view_split_scroll_x", "sky_view_layer_source_0", "sky_view_layer_source_1", "sky_view_layer_source_2", "sky_view_initialized", "sky_road_line", "sky_road_mode", "sky_split_scroll_y", "sky_split_vertical", "ambient_digest", "camera_current_yaw", "camera_adjustment", "camera_step", "finish_camera_phase", "finish_camera_orbit", "finish_camera_hold", "finish_camera_x", "finish_camera_y", "finish_camera_yaw", "finish_camera_vertical", "finish_camera_active", "finish_camera_celebration", "finish_camera_cancel", "snow_weather_digest_lo", "snow_weather_digest_hi", "main_visual_rng_state", "finished_target_angle_ca", "finished_initialized", "finished_route_row_191", "source_steering_selector_9a", "source_speed_variant_19b", "source_speed_offset_19d", "lava_smoke_digest_lo", "lava_smoke_digest_hi", "fall_frames_76", "driver_hidden", "recovery_fx_digest_lo", "recovery_fx_digest_hi", "recovery_fx_events", "surface_trail_digest_lo", "surface_trail_digest_hi", "impact_effect_digest_lo", "impact_effect_digest_hi",
    "modern_charge_ticks", "modern_boost_ticks", "modern_charge_tier",
    "modern_boost_tier", "modern_previous_held", "modern_last_applied_tier",
    "air_ticks", "air_boost_ticks", "air_armed", "air_tricked",
    "air_previous_hop", "air_last_applied"
};

static void state_values(const PortableRecoveredTimeTrialSession *session,
                         int64_t values[REPLAY_STATE_FIELD_COUNT])
{
    const PortableDriverPilotState *driver = &session->driver;
    const PortableRaceSession *race = &session->runtime.session;
    size_t index = 0;

#define VALUE(field) values[index++] = (int64_t)(field)
    VALUE(race->phase);
    VALUE(driver->pose.position_x);
    VALUE(driver->pose.position_y);
    VALUE(driver->pose.position_z);
    VALUE(driver->pose.component_x);
    VALUE(driver->pose.component_y);
    VALUE(driver->pose.track_cell);
    VALUE(driver->pose.track_cell_valid);
    VALUE(driver->motion.speed_q16);
    VALUE(driver->motion.acceleration_q16);
    VALUE(driver->motion.angle);
    VALUE(driver->motion.component_x);
    VALUE(driver->motion.component_y);
    VALUE(driver->input.control_word);
    VALUE(driver->input.speed_target_q16);
    VALUE(driver->steering.control_word);
    VALUE(driver->steering.angle);
    VALUE(driver->steering.steering_offset);
    VALUE(driver->steering.base_angle);
    VALUE(driver->steering.state_flags);
    VALUE(driver->steering.response_q8);
    VALUE(driver->steering.response_flags);
    VALUE(driver->steering.response_status);
    VALUE(driver->steering.response_slot);
    VALUE(driver->steering.response_selector_flags);
    VALUE(driver->steering.response_selector_index);
    VALUE(driver->steering.response_row);
    VALUE(driver->steering.spin_delay);
    VALUE(driver->steering.drift_angle);
    VALUE(driver->steering.exit_timer);
    VALUE(driver->steering.driver_mode);
    VALUE(session->mushrooms_remaining);
    VALUE(driver->item_requests_b2);
    VALUE(driver->item_cue_flags_58);
    VALUE(driver->boost_particle_age);
    VALUE(driver->boost_particle_birth_height_q16);
    VALUE(session->previous_item_control);
    VALUE(driver->hop.velocity_q8);
    VALUE(driver->hop.mode);
    VALUE(driver->drift_charge.charge_counter);
    VALUE(driver->charged_exit_active);
    VALUE(driver->charged_exit_count);
    VALUE(driver->charged_audio_cue_count);
    VALUE(driver->charged_particle_count);
    VALUE(driver->charged_particle_age);
    VALUE(driver->auto_acceleration.speed_q16);
    VALUE(driver->auto_acceleration.acceleration_q16);
    VALUE(driver->brake.speed_q16);
    VALUE(driver->brake.acceleration_q16);
    VALUE(driver->forward_acceleration.speed_q16);
    VALUE(driver->forward_acceleration.acceleration_q16);
    VALUE(driver->forward_acceleration.response_q8);
    VALUE(race->laps_completed);
    VALUE(session->lap_hud.pending_lap);
    VALUE(session->lap_hud.displayed_lap);
    VALUE(session->lap_hud.initialized);
    VALUE(race->countdown_remaining);
    VALUE(race->next_route_index);
    VALUE(race->elapsed_ticks);
    VALUE(portable_race_session_total_ticks(race));
    VALUE(portable_race_session_current_lap_ticks(race));
    VALUE(race->rescue_count);
    VALUE(session->paused);
    VALUE(session->last_runtime_status);
    VALUE(session->runtime.last_driver_status);
    VALUE(session->runtime.last_material_status);
    VALUE(session->runtime.boundary_exit);
    VALUE(session->runtime.water_entry);
    VALUE(session->runtime.last_material_flags);
    VALUE(session->runtime.last_material_flags_valid);
    VALUE(driver->source_terrain22_active);
    VALUE(driver->hop.height_q16);
    VALUE(driver->drift_charge.charge_counter);
    VALUE(race->finish_armed);
    VALUE(driver->source_hazard_kind);
    VALUE(driver->hop.recovery_requests);
    VALUE(session->hazard_effect_72);
    VALUE(session->hazard_lakitu.x);
    VALUE(session->hazard_lakitu.y);
    VALUE(session->hazard_lakitu.visible);
    VALUE(session->hazard_lakitu.wait);
    VALUE(session->hazard_lakitu.counter);
    VALUE(session->hazard_lakitu.art_bank);
    VALUE(session->hazard_lakitu.flags);
    VALUE(session->hazard_lakitu.phase);
    VALUE(session->hazard_lakitu.callback);
    VALUE(session->hazard_lakitu.script);
    VALUE(session->hazard_lakitu.drop_started);
    VALUE(session->hazard_lakitu.exit_velocity_q16);
    VALUE(session->hazard_lakitu.retrieval_effect_started);
    VALUE(session->hazard_lakitu.finish_turn);
    VALUE(session->hazard_lakitu.inverse_q8);
    VALUE(driver->vector_effect.contact_word_5a);
    VALUE(driver->vector_effect_source_direction);
    VALUE(driver->vector_effect_source_state);
    VALUE(driver->vector_effect_previous_cell_key);
    VALUE(driver->vector_effect_previous_cell_key_valid);
    VALUE(driver->normal_collision_timer_38);
    VALUE(driver->normal_collision_timer_62);
    VALUE(driver->contact_guard_66);
    VALUE(driver->lightning_timer_68);
    VALUE(driver->invulnerability_timer_6a);
    VALUE(driver->flattened_64);
    VALUE(driver->wall_mutation_count);
    VALUE(driver->wall_mutation_digest);
    VALUE(driver->wall_mutation_cell);
    VALUE(driver->strong_spin_rate_b6);
    VALUE(session->scene_clock);
    VALUE(driver->recovery_flags_74);
    VALUE(driver->contact_anchor_x_ee);
    VALUE(driver->contact_anchor_y_f0);
    VALUE(driver->palette_requests_194);
    VALUE(driver->palette_bank);
    VALUE(race->checkpoint_raw);
    VALUE(race->lap_raw);
    VALUE(race->max_progress_raw);
    VALUE(race->checkpoint_next_index);
    VALUE(race->lap_started);
    VALUE(session->hazard_lakitu.warning_turn);
    VALUE(session->hazard_lakitu.cleared_requests);
    VALUE(driver->affine.inverse_x_q8);VALUE(driver->affine.inverse_y_q8);
    VALUE(driver->affine.mode);VALUE(driver->affine.row);
    VALUE(driver->affine.remaining);VALUE(driver->affine.pending_mode);
    VALUE(driver->art_motion.countdown);VALUE(driver->art_motion.wheel_phase);
    VALUE(driver->art_motion.bob_phase);VALUE(driver->art_motion.wheel_palette_bank);
    VALUE(driver->art_motion.wheel_palette_valid);VALUE(driver->art_motion.initialized);
    VALUE(driver->art_motion.cached_bob_offset);
    VALUE(session->sky_motion.initialized);
    VALUE(session->sky_motion.phase);
    VALUE(session->sky_motion.prior_vertical_offset);
    VALUE(session->sky_motion.elapsed);
    VALUE(session->sky_motion.transition_counter);
    VALUE(session->sky_motion.horizon_y);
    VALUE(session->sky_motion.transition_clock);
    VALUE(session->sky_motion.raw_scroll_y[0]);VALUE(session->sky_motion.raw_scroll_y[1]);VALUE(session->sky_motion.raw_scroll_y[2]);
    VALUE(session->sky_motion.snapshot.scroll_x[0]);
    VALUE(session->sky_motion.snapshot.scroll_x[1]);
    VALUE(session->sky_motion.snapshot.scroll_x[2]);
    VALUE(session->sky_motion.snapshot.scroll_y[0]);
    VALUE(session->sky_motion.snapshot.scroll_y[1]);
    VALUE(session->sky_motion.snapshot.scroll_y[2]);
    VALUE(session->sky_motion.snapshot.layer_mask);
    VALUE(session->sky_motion.snapshot.blend_mode);
    VALUE(session->sky_motion.snapshot.blend_a);
    VALUE(session->sky_motion.snapshot.blend_b);
    VALUE(session->sky_motion.snapshot.split_y);
    VALUE(session->sky_motion.snapshot.split_layer);
    VALUE(session->sky_motion.snapshot.split_scroll_x);
    VALUE(session->sky_motion.snapshot.layer_source[0]);
    VALUE(session->sky_motion.snapshot.layer_source[1]);
    VALUE(session->sky_motion.snapshot.layer_source[2]);
    VALUE(session->sky_motion.snapshot.initialized);VALUE(session->sky_motion.snapshot.road_line);VALUE(session->sky_motion.snapshot.road_mode);VALUE(session->sky_motion.snapshot.split_scroll_y);VALUE(session->sky_motion.snapshot.split_vertical);
    VALUE(portable_ambient_digest(&session->ambient));
    VALUE(session->character_camera.current_yaw);
    VALUE(session->character_camera.current_adjustment);
    VALUE(session->character_camera.adjustment_step);
    VALUE(session->finish_camera.phase);VALUE(session->finish_camera.orbit_yaw);VALUE(session->finish_camera.hold_ticks);
    VALUE(session->finish_camera.snapshot.x_q16);VALUE(session->finish_camera.snapshot.y_q16);
    VALUE(session->finish_camera.snapshot.yaw);VALUE(session->finish_camera.snapshot.vertical_offset);
    VALUE(session->finish_camera.snapshot.active);VALUE(session->finish_camera.snapshot.request_celebration);
    VALUE(session->finish_camera.snapshot.cancel_effects);
    const uint64_t weather_digest=portable_snow_weather_digest(&session->snow_weather);
    VALUE((uint32_t)weather_digest);VALUE((uint32_t)(weather_digest>>32u));
    VALUE(session->main_visual_rng_state);
    VALUE(driver->finished_target_angle_ca);VALUE(driver->finished_initialized);VALUE(driver->finished_route_row_191);
    VALUE(driver->source_steering_selector_9a);VALUE(driver->source_speed_variant_19b);VALUE(driver->source_speed_offset_19d);
    const uint64_t smoke_digest=portable_lava_smoke_digest(&session->lava_smoke);
    VALUE((uint32_t)smoke_digest);VALUE((uint32_t)(smoke_digest>>32u));
    VALUE(driver->fall_frames_76);VALUE(!portable_driver_effects_visible(driver));
    const uint64_t recovery_digest=portable_recovery_fx_digest(&session->recovery_fx);
    VALUE((uint32_t)recovery_digest);VALUE((uint32_t)(recovery_digest>>32u));VALUE(session->recovery_fx_events);
    const uint64_t trail_digest=portable_surface_trail_digest(&session->surface_trails);
    VALUE((uint32_t)trail_digest);VALUE((uint32_t)(trail_digest>>32u));
    const uint64_t impact_digest=portable_impact_effect_digest(&driver->impact_effects);
    VALUE((uint32_t)impact_digest);VALUE((uint32_t)(impact_digest>>32u));
    if (modern_replay(session))
    {
        VALUE(driver->modern_drift.charge_ticks);
        VALUE(driver->modern_drift.boost_ticks);
        VALUE(driver->modern_drift.charge_tier);
        VALUE(driver->modern_drift.boost_tier);
        VALUE(driver->modern_drift.previous_held);
        VALUE(driver->modern_drift.last_applied_tier);
    }
    if (air_replay(session))
    {
        VALUE(driver->modern_air.air_ticks);
        VALUE(driver->modern_air.boost_ticks);
        VALUE(driver->modern_air.armed);
        VALUE(driver->modern_air.tricked);
        VALUE(driver->modern_air.previous_hop);
        VALUE(driver->modern_air.last_applied);
    }
#undef VALUE
}

static uint32_t hash_byte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * UINT32_C(16777619);
}

static uint32_t hash_u64(uint32_t hash, uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
        hash = hash_byte(hash, (uint8_t)(value >> shift));
    return hash;
}

uint32_t portable_recovered_replay_state_checksum(
    const PortableRecoveredTimeTrialSession *session)
{
    int64_t values[REPLAY_STATE_FIELD_COUNT];
    uint32_t hash = UINT32_C(2166136261);

    if (session == NULL)
        return 0;
    state_values(session, values);
    for (size_t index = 0; index < replay_field_count(session); ++index)
        hash = hash_u64(hash, (uint64_t)values[index]);
    for (size_t lap = 0; lap < (session->runtime.session.lap_count > 3u ? session->runtime.session.lap_count : 3u); ++lap)
        hash = hash_u64(hash, session->runtime.session.lap_ticks[lap]);
    if(session->lava_bubbles.count!=0u){
        hash=hash_u64(hash,session->lava_bubbles.count);
        for(size_t i=0;i<session->lava_bubbles.count;++i){
            const PortableLavaBubble *a=&session->lava_bubbles.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->velocity_x_q16,a->velocity_y_q16,a->velocity_z_q16,
                a->timer,a->period,a->phase,a->flags,a->art,a->checkpoint,a->placement_order};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->thwomps.count!=0u){
        hash=hash_u64(hash,session->thwomps.count);
        for(size_t i=0;i<session->thwomps.count;++i){
            const PortableThwomp *a=&session->thwomps.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->velocity_x_q16,a->velocity_y_q16,a->velocity_z_q16,
                a->timer,a->variant,a->state,a->flags,a->checkpoint,a->placement_order};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->crabs.count!=0u){
        hash=hash_u64(hash,session->crabs.count);
        for(size_t i=0;i<session->crabs.count;++i){
            const PortableCrab *a=&session->crabs.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->velocity_x_q16,a->velocity_y_q16,a->timer,a->variant,
                a->checkpoint,a->placement_order,a->flags,a->art,
                a->animation_frame,a->animation_timer,a->flip_y};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->seagulls.count!=0u){
        hash=hash_u64(hash,session->seagulls.count);
        for(size_t i=0;i<session->seagulls.count;++i){
            const PortableSeagull *a=&session->seagulls.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->velocity_z_q16,a->state,a->timer,a->phase,a->checkpoint,
                a->placement_order,a->flags,a->art,a->affine_x_q8,a->affine_y_q8,a->flip_y};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->piranhas.count!=0u){
        hash=hash_u64(hash,session->piranhas.count);
        for(size_t i=0;i<session->piranhas.count;++i){
            const PortablePiranha *a=&session->piranhas.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->velocity_z_q16,a->state,a->timer,a->phase,a->checkpoint,
                a->placement_order,a->flags,a->art,a->affine_x_q8,a->affine_y_q8,a->flip_y,a->struck_player};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->falling_stars.count!=0u){
        hash=hash_u64(hash,session->falling_stars.count);
        for(size_t i=0;i<session->falling_stars.count;++i){
            const PortableFallingStar *a=&session->falling_stars.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,a->initial_x_q16,a->offset_q16,
                a->velocity_x_q16,a->velocity_z_q16,a->timer,a->delay,a->affine_q8,a->angle,
                a->state,a->variant,a->checkpoint,a->placement_order,a->flags,a->art};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->thunderclouds.count!=0u){
        hash=hash_u64(hash,session->thunderclouds.count);
        hash=hash_u64(hash,session->thunderclouds.palette_phase[0]);
        hash=hash_u64(hash,session->thunderclouds.palette_phase[1]);
        for(size_t i=0;i<session->thunderclouds.count;++i){
            const PortableThundercloud *a=&session->thunderclouds.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,a->state,a->timer,
                a->palette_timer,a->variant,a->checkpoint,a->placement_order,a->flags,a->art};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->mice.count!=0u){
        hash=hash_u64(hash,session->mice.count);
        for(size_t i=0;i<session->mice.count;++i){
            const PortableMouse *a=&session->mice.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,a->velocity_x_q16,
                a->velocity_y_q16,a->velocity_z_q16,a->facing_x_q16,a->facing_y_q16,
                a->timer,a->affine_y_q8,a->variant,a->checkpoint,a->placement_order,
                a->flags,a->art,a->walk_phase,a->walk_ticks,a->hop_phase,a->turn_flip};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->snowmen.count!=0u){
        hash=hash_u64(hash,session->snowmen.count);
        for(size_t i=0;i<session->snowmen.count;++i){
            const PortableSnowman *a=&session->snowmen.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->state,a->timer,a->checkpoint,
                a->placement_order,a->flags,a->art};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->penguins.count!=0u){
        hash=hash_u64(hash,session->penguins.count);
        for(size_t i=0;i<session->penguins.count;++i){
            const PortablePenguin *a=&session->penguins.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->timer,a->checkpoint,
                a->placement_order,a->flags,a->art,a->affine_y_q8};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->course_props.count!=0u){
        hash=hash_u64(hash,session->course_props.count);
        for(size_t i=0;i<session->course_props.count;++i){
            const PortableCourseProp *a=&session->course_props.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                (int32_t)a->source_code,a->timer,a->state,a->flags,
                a->affine_x_q8,a->affine_y_q8,a->art_index,a->checkpoint,a->placement_order,a->contact_phase,
                a->mechakoopa.velocity_x_q16,a->mechakoopa.cycle,a->mechakoopa.heading,
                a->mechakoopa.animation_ticks,a->mechakoopa.animation_phase,a->mechakoopa.has_facing,
                a->kamek.velocity_x_q16,a->kamek.velocity_z_q16,a->kamek.phase,a->kamek.heading,a->kamek.has_facing,
                a->pier_boo.velocity_z_q16,a->pier_boo.phase,
                a->teepee.velocity_x_q16,a->teepee.velocity_y_q16,a->teepee.rider_x_q16,
                a->teepee.rider_y_q16,a->teepee.rider_z_q16,a->teepee.rider_vx_q16,a->teepee.rider_vy_q16,
                a->teepee.rider_vz_q16,a->teepee.rider_spin,a->teepee.rider_spin_rate,a->teepee.rider_gravity,
                a->teepee.rider_heading,a->teepee.counter,a->teepee.frame,a->teepee.kart,a->teepee.rider_state,
                a->lake_rock.home_x_q16,a->lake_rock.home_y_q16,a->lake_rock.velocity_z_q16,
                a->lake_rock.timer,a->lake_rock.delay,a->lake_rock.frame};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    if(session->boos.count!=0u){
        hash=hash_u64(hash,session->boos.count);
        for(size_t i=0;i<session->boos.count;++i){
            const PortableBoo *a=&session->boos.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->base_height_q16,a->velocity_z_q16,portable_boo_signed(a->phase),
                a->heading,portable_boo_signed(a->state),a->checkpoint,
                a->placement_order,a->flags,a->affine_y_q8};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    hash=hash_u64(hash,session->driver.item_lock_ac);hash=hash_u64(hash,session->driver.burn_ticks_72);
    for(size_t i=0;i<PORTABLE_BOO_ATTACK_CAPACITY;++i){
        const PortableBooAttack *a=&session->haunts[i];
        if(!a->occupied)continue;
        const int32_t fields[]={(int32_t)i,a->x_q16,a->y_q16,a->z_q16,a->vx_q16,a->vy_q16,a->vz_q16,
            a->angle,a->bob_angle,a->timer,a->radius,a->affine_y_q8,a->phase,a->visible,a->frame};
        for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
            hash=hash_u64(hash,(uint32_t)fields[j]);
    }
    if(session->cannonballs.count!=0u){
        hash=hash_u64(hash,session->cannonballs.count);
        for(size_t i=0;i<session->cannonballs.count;++i){
            const PortableCannonball *a=&session->cannonballs.actors[i];
            const int32_t fields[]={a->x_q16,a->y_q16,a->height_q16,
                a->velocity_z_q16,a->state,a->timer,a->checkpoint,
                a->placement_order,a->flags,a->art,a->affine_x_q8,a->affine_y_q8,a->size};
            for(size_t j=0;j<sizeof fields/sizeof fields[0];++j)
                hash=hash_u64(hash,(uint32_t)fields[j]);
        }
    }
    return hash;
}

static int compatibility_equal(const PortableRaceCompatibilityId *left,
                               const PortableRaceCompatibilityId *right)
{
    return left->course_id == right->course_id &&
        left->course_revision == right->course_revision &&
        left->rules_id == right->rules_id &&
        left->rules_revision == right->rules_revision &&
        left->handling_profile_id == right->handling_profile_id &&
        left->handling_revision == right->handling_revision &&
        left->character_class == right->character_class &&
        left->state_schema_version == right->state_schema_version &&
        memcmp(left->deterministic_digest, right->deterministic_digest,
               sizeof left->deterministic_digest) == 0;
}

int portable_recovered_replay_write_header(
    PortableReplayWriter *output, const PortableRecoveredTimeTrialSession *session)
{
    const PortableRaceCompatibilityId *id;
    int failed;

    if (output == NULL || session == NULL)
        return 0;
    id = &session->runtime.session.compatibility;
    failed = portable_replay_printf(output,
        "MKSC-RECOVERED-REPLAY %u %08" PRIx32
        " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32
        " %" PRIu32 " %" PRIu32 " %" PRIu32 " %" PRIu32 " ",
        replay_version(session),
        portable_recovered_replay_state_checksum(session), id->course_id,
        id->course_revision, id->rules_id, id->rules_revision,
        id->handling_profile_id, id->handling_revision, id->character_class,
        id->state_schema_version) < 0;
    for (size_t index = 0;
         !failed && index < sizeof id->deterministic_digest; ++index)
        failed = portable_replay_printf(output, "%02x", id->deterministic_digest[index]) < 0;
    return !failed && portable_replay_putc('\n', output) != -1;
}

int portable_recovered_replay_write_action(
    PortableReplayWriter *output, PortableRecoveredReplayAction action, int value)
{
    if (output == NULL)
        return 0;
    if (action == PORTABLE_RECOVERED_REPLAY_PAUSE)
        return portable_replay_printf(output, "A pause %u\n", value != 0) > 0;
    if (action == PORTABLE_RECOVERED_REPLAY_RESTART)
        return portable_replay_puts("A restart\n", output) >= 0;
    if (action == PORTABLE_RECOVERED_REPLAY_RESCUE)
        return portable_replay_puts("A rescue\n", output) >= 0;
    return 0;
}

int portable_recovered_replay_write_tick(
    PortableReplayWriter *output, uint32_t tick, uint16_t semantic_buttons,
    PortableRaceTickDisposition disposition,
    const PortableRecoveredTimeTrialSession *session)
{
    int64_t values[REPLAY_STATE_FIELD_COUNT];

    if (output == NULL || session == NULL)
        return 0;
    state_values(session, values);
    if (portable_replay_printf(output, "T %" PRIu32 " %04" PRIx16 " %u %08" PRIx32,
                tick, semantic_buttons, (unsigned)disposition,
                portable_recovered_replay_state_checksum(session)) < 0)
        return 0;
    for (size_t index = 0; index < replay_field_count(session); ++index)
        if (portable_replay_printf(output, " %" PRId64, values[index]) < 0)
            return 0;
    for (size_t lap = 0; lap < (session->runtime.session.lap_count > 3u ? session->runtime.session.lap_count : 3u); ++lap)
        if (portable_replay_printf(output, " %" PRIu32,
                    session->runtime.session.lap_ticks[lap]) < 0)
            return 0;
    return portable_replay_putc('\n', output) != -1;
}

static int only_space(const char *text)
{
    while (*text != '\0')
    {
        if (!isspace((unsigned char)*text))
            return 0;
        ++text;
    }
    return 1;
}

static int hex_nibble(char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return -1;
}

static int parse_digest(const char *text, uint8_t digest[32])
{
    if (strlen(text) != 64u)
        return 0;
    for (size_t index = 0; index < 32u; ++index)
    {
        const int high = hex_nibble(text[index * 2u]);
        const int low = hex_nibble(text[index * 2u + 1u]);
        if (high < 0 || low < 0)
            return 0;
        digest[index] = (uint8_t)(high * 16 + low);
    }
    return 1;
}

static int parse_integer(const char **cursor, const char *end, unsigned base, int64_t *value)
{
    return portable_text_i64(cursor, end, base, INT64_MIN, INT64_MAX, value);
}

static int parse_header_u32(const char **cursor, const char *end, unsigned base, uint32_t *value)
{
    uint64_t parsed;
    if (!portable_text_u64(cursor, end, base, UINT32_MAX, &parsed)) return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_header(const char *line, uint32_t *version, uint32_t *checksum,
                        PortableRaceCompatibilityId *expected)
{
    const char prefix[] = "MKSC-RECOVERED-REPLAY";
    if (strncmp(line, prefix, sizeof prefix - 1u) != 0) return 0;
    const char *cursor = line + sizeof prefix - 1u;
    const char *end = line + strlen(line);
    if (!parse_header_u32(&cursor, end, 10u, version) ||
        !parse_header_u32(&cursor, end, 16u, checksum)) return 0;
    uint32_t *fields[] = {&expected->course_id, &expected->course_revision,
        &expected->rules_id, &expected->rules_revision, &expected->handling_profile_id,
        &expected->handling_revision, &expected->character_class, &expected->state_schema_version};
    for (size_t i = 0u; i < sizeof fields / sizeof fields[0]; ++i)
        if (!parse_header_u32(&cursor, end, 10u, fields[i])) return 0;
    while (cursor < end && isspace((unsigned char)*cursor)) ++cursor;
    char digest[65];
    size_t count = 0u;
    while (cursor < end && !isspace((unsigned char)*cursor) && count < 64u)
        digest[count++] = *cursor++;
    digest[count] = '\0';
    return (*version == PORTABLE_RECOVERED_REPLAY_VERSION ||
            *version == PORTABLE_RECOVERED_REPLAY_MODERN_VERSION ||
            *version == PORTABLE_RECOVERED_REPLAY_AIR_VERSION) &&
        parse_digest(digest, expected->deterministic_digest) && only_space(cursor);
}

int portable_recovered_replay_inspect_header(PortableReplayReader *input,
    PortableRecoveredReplayHeader *out)
{
    char line[4096];
    PortableRecoveredReplayHeader header = {0};
    if(input == NULL || out == NULL ||
        portable_replay_read_line(line,sizeof line,input) == NULL ||
        !parse_header(line,&header.version,&header.initial_checksum,&header.compatibility))
        return 0;
    *out = header;
    return 1;
}

static int divergence(PortableReplayWriter *diagnostics, uint32_t tick, const char *field,
                      int64_t expected, int64_t actual)
{
    if (diagnostics != NULL)
        portable_replay_printf(diagnostics,
                "recovered replay divergence tick %" PRIu32
                " field %s expected %" PRId64 " actual %" PRId64 "\n",
                tick, field, expected, actual);
    return 0;
}

int portable_recovered_replay_run(
    PortableReplayReader *input, PortableRecoveredTimeTrialSession *session,
    PortableReplayWriter *diagnostics, size_t *out_tick_count)
{
    char line[4096];
    PortableRecoveredReplayHeader header;
    size_t ticks = 0;

    if (out_tick_count != NULL)
        *out_tick_count = 0;
    if (input == NULL || session == NULL ||
        !portable_recovered_replay_inspect_header(input,&header))
    {
        if (diagnostics != NULL)
            portable_replay_puts("recovered replay invalid header\n", diagnostics);
        return 0;
    }
    if (header.version != replay_version(session) ||
        !compatibility_equal(&header.compatibility,
            &session->runtime.session.compatibility))
    {
        if (diagnostics != NULL)
            portable_replay_puts("recovered replay compatibility mismatch\n", diagnostics);
        return 0;
    }
    if (header.initial_checksum != portable_recovered_replay_state_checksum(session))
        return divergence(diagnostics, 0, "initial_state_checksum",
                          header.initial_checksum,
                          portable_recovered_replay_state_checksum(session));

    while (portable_replay_read_line(line, sizeof line, input) != NULL)
    {
        if (strncmp(line, "A pause ", 8u) == 0)
        {
            uint64_t paused;
            const char *cursor = line + 8u;
            const char *end = line + strlen(line);
            if (!portable_text_u64(&cursor, end, 10u, UINT32_MAX, &paused) ||
                !only_space(cursor) || paused > 1u ||
                portable_recovered_time_trial_set_paused(session, (int)paused) !=
                    PORTABLE_RECOVERED_TIME_TRIAL_OK)
                return 0;
            continue;
        }
        if (strcmp(line, "A restart\n") == 0 || strcmp(line, "A restart") == 0)
        {
            if (portable_recovered_time_trial_restart(session) !=
                PORTABLE_RECOVERED_TIME_TRIAL_OK)
                return 0;
            continue;
        }
        if (strcmp(line, "A rescue\n") == 0 || strcmp(line, "A rescue") == 0)
        {
            if (portable_recovered_time_trial_rescue(session) !=
                PORTABLE_RECOVERED_TIME_TRIAL_OK)
                return 0;
            continue;
        }
        if (line[0] == 'T' && isspace((unsigned char)line[1]))
        {
            int64_t parsed_tick = 0, parsed_buttons = 0, parsed_disposition = 0;
            int64_t parsed_checksum = 0;
            int64_t expected_values[REPLAY_STATE_FIELD_COUNT];
            int64_t actual_values[REPLAY_STATE_FIELD_COUNT];
            uint32_t expected_laps[PORTABLE_RACE_MAX_LAPS];
            PortableRaceTickDisposition disposition;
            const char *cursor = line + 1;
            const char *end = line + strlen(line);

            if (!parse_integer(&cursor, end, 10u, &parsed_tick) ||
                !parse_integer(&cursor, end, 16u, &parsed_buttons) ||
                !parse_integer(&cursor, end, 10u, &parsed_disposition) ||
                !parse_integer(&cursor, end, 16u, &parsed_checksum) ||
                parsed_tick < 0 || parsed_tick != (int64_t)ticks ||
                parsed_buttons < 0 || parsed_buttons > UINT16_MAX ||
                parsed_checksum < 0 || parsed_checksum > UINT32_MAX ||
                parsed_buttons & ~(PORTABLE_INPUT_LEFT | PORTABLE_INPUT_RIGHT |
                    PORTABLE_INPUT_UP | PORTABLE_INPUT_DOWN |
                    PORTABLE_INPUT_ACCELERATE | PORTABLE_INPUT_BRAKE |
                    PORTABLE_INPUT_HOP | PORTABLE_INPUT_ITEM))
                return divergence(diagnostics, (uint32_t)ticks, "tick",
                                  ticks, parsed_tick);
            for (size_t index = 0; index < replay_field_count(session); ++index)
                if (!parse_integer(&cursor, end, 10u, &expected_values[index]))
                    return 0;
            for (size_t lap = 0; lap < (session->runtime.session.lap_count > 3u ? session->runtime.session.lap_count : 3u); ++lap)
            {
                int64_t value;
                if (!parse_integer(&cursor, end, 10u, &value) || value < 0 ||
                    value > UINT32_MAX)
                    return 0;
                expected_laps[lap] = (uint32_t)value;
            }
            if (!only_space(cursor) ||
                portable_recovered_time_trial_tick(
                    session, (uint16_t)parsed_buttons, &disposition) !=
                    PORTABLE_RECOVERED_TIME_TRIAL_OK)
                return 0;
            if ((int64_t)disposition != parsed_disposition)
                return divergence(diagnostics, (uint32_t)ticks,
                                  "disposition", parsed_disposition,
                                  disposition);
            state_values(session, actual_values);
            for (size_t index = 0; index < replay_field_count(session); ++index)
                if (expected_values[index] != actual_values[index])
                    return divergence(diagnostics, (uint32_t)ticks,
                        state_field_names[index], expected_values[index],
                        actual_values[index]);
            for (size_t lap = 0; lap < (session->runtime.session.lap_count > 3u ? session->runtime.session.lap_count : 3u); ++lap)
                if (expected_laps[lap] !=
                    session->runtime.session.lap_ticks[lap])
                    return divergence(diagnostics, (uint32_t)ticks,
                        "lap_ticks", expected_laps[lap],
                        session->runtime.session.lap_ticks[lap]);
            if ((uint32_t)parsed_checksum !=
                portable_recovered_replay_state_checksum(session))
                return divergence(diagnostics, (uint32_t)ticks, "checksum",
                    parsed_checksum,
                    portable_recovered_replay_state_checksum(session));
            ++ticks;
            continue;
        }
        if (diagnostics != NULL)
            portable_replay_printf(diagnostics, "recovered replay invalid record %zu\n", ticks);
        return 0;
    }
    if (input->failed)
        return 0;
    if (out_tick_count != NULL)
        *out_tick_count = ticks;
    if (diagnostics != NULL)
        portable_replay_printf(diagnostics, "recovered replay: PASS %zu tick(s)\n", ticks);
    return 1;
}
