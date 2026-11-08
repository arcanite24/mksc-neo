#include "gp_race_presentation.h"

#include <string.h>

/* 49D78: the selected racer blinks on the accepted source clock. Other
 * racers vanish during the middle of +66, then blink on the timer itself.
 * The signed halfword comparison deliberately leaves negative clocks on the
 * parity path, rather than treating them as an unsigned middle interval. */
static int boo_hidden(int16_t timer, int selected, uint32_t source_clock)
{
    if (timer == 0)
        return 0;
    if (selected)
        return (source_clock & 1u) != 0u;
    if (timer > 32 && timer <= 343)
        return 1;
    return ((uint16_t)timer & 1u) != 0u;
}

int portable_gp_race_presentation_build(
    const PortableGpRaceSession *session,
    const PortableGpRaceSnapshot *snapshot,
    PortableRacePresentationState *state)
{
    if (session == NULL || snapshot == NULL || state == NULL)
        return 0;
    const int battle=session->rules.progress_kind==PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1;
    if (snapshot->lap_count==0u && !battle) return 0;

    memset(state, 0, sizeof *state);
    state->position_x_q16 = snapshot->player_x_q16;
    state->position_y_q16 = snapshot->player_y_q16;
    state->height_q16 = snapshot->player_height_q16;
    /* Publish the same live course owners used by the collision pass. The
     * renderer and interpolation only read this immutable per-tick copy. */
    state->course_props = snapshot->course_props;
    state->lava_bubbles = session->lava_bubbles;
    state->thwomps = session->thwomps;
    state->crabs = session->crabs;
    state->seagulls = session->seagulls;
    state->piranhas = session->piranhas;
    state->snowmen = session->snowmen;
    state->mice = snapshot->mice;
    state->thunderclouds = snapshot->thunderclouds;
    state->falling_stars = snapshot->falling_stars;
    state->penguins = session->penguins;
    state->boos = session->boos;
    state->cannonballs = session->cannonballs;
    memcpy(state->coin_pickups,snapshot->coin_pickups,sizeof state->coin_pickups);
    state->lava_smoke = snapshot->lava_smoke;
    state->recovery_fx=snapshot->recovery_fx;
    state->surface_trails=snapshot->surface_trails;
    state->coin_effects=snapshot->coin_effects;
    state->impact_effects=snapshot->impact_effects;
    state->entry_splash = snapshot->entry_splash;
    state->boost_particle_age = snapshot->boost_particle_age;
    state->modern_drift_enabled = snapshot->modern_drift_enabled;
    state->modern_drift = snapshot->modern_drift;
    state->modern_air_enabled = snapshot->modern_air_enabled;
    state->modern_air = snapshot->modern_air;
    state->boost_particle_birth_height_q16 = snapshot->boost_particle_birth_height_q16;
    state->heading = snapshot->player_heading;
    state->visual_yaw = snapshot->player_visual_yaw;
    state->palette_bank = snapshot->player_palette_bank;
    state->flattened=snapshot->player_flattened;
    state->art_motion=snapshot->player_art_motion;
    state->sky_motion=snapshot->sky_motion;
    state->ambient=snapshot->ambient;
    state->snow_weather=snapshot->snow_weather;
    state->ambient_rules=session->ambient_rules;
    state->driver_affine_x=snapshot->player_affine.inverse_x_q8;
    state->driver_affine_y=snapshot->player_affine.inverse_y_q8;
    state->lightning_scene_active = snapshot->lightning_scene_active;
    state->lightning_scene_white = snapshot->lightning_scene_white;
    state->camera_yaw = snapshot->camera_yaw;
    if(snapshot->finish_camera.snapshot.active){
        state->camera_position_override=1u;
        state->camera_position_x_q16=snapshot->finish_camera.snapshot.x_q16;
        state->camera_position_y_q16=snapshot->finish_camera.snapshot.y_q16;
        state->source_sky_parallax=1u;
    }
    state->character_view_angle=snapshot->player_view_angle;
    state->art_pose=snapshot->player_art_pose;
    state->art_pose_valid=1u;
    state->phase = snapshot->phase;
    /* GP owns its result board and retry/standings navigation. */
    state->laps_completed = snapshot->lap_hud.initialized?
        (uint16_t)(snapshot->lap_hud.displayed_lap-1u):snapshot->laps_completed;
    state->lap_count = snapshot->lap_count;
    state->countdown_remaining = snapshot->countdown_remaining;
    state->next_route_index = snapshot->player_route_index;
    state->total_ticks = snapshot->total_ticks;
    state->marker_clock=snapshot->marker_clock;
    state->marker_source_flags=snapshot->marker_source_flags;
    state->marker_class_flags=snapshot->marker_class_flags;
    state->recovery_phase = snapshot->recovery_phase;
    state->recovery_phase_ticks = snapshot->recovery_phase_ticks;
    state->opponent_count = portable_gp_race_roster_count(&session->roster)-1u;
    state->show_battle = (uint8_t)battle;
    state->balloons_remaining = snapshot->player_balloons_remaining;
    state->balloon_tick = snapshot->balloon_tick;
    state->balloon_heading = snapshot->player_balloon_heading;
    state->revenge_bomb = snapshot->player_revenge_bomb;
    state->bomb_walk_phase = snapshot->player_bomb_walk_phase;
    state->bomb_response = snapshot->player_bomb_response;
    state->bomb_speed_q16 = snapshot->player_speed_q16;
    state->show_gp_item = 1u;
    memcpy(state->boo_attacks,session->roster.boo_attacks,sizeof state->boo_attacks);
    /* Ghosting is a native presentation effect; collision immunity is +66. */
    state->suppress_player=(uint8_t)(boo_hidden(
        session->roster.racers[session->local_index].contact_status_66,1,session->source_frame_clock) ||
        snapshot->driver_hidden);
    state->gp_rank = snapshot->player_rank;
    state->gp_rank_hud=snapshot->rank_hud;
    state->gp_coin_hud=snapshot->coin_hud;
    state->gp_coins = (uint8_t)session->roster.racers[session->local_index].coin_count_178;
    state->source_clock_override = 1u;
    state->source_clock_centiseconds = snapshot->source_clock_centiseconds;
    if(snapshot->results_ready && snapshot->source_result_times_ready && snapshot->lap_count>0u){
        state->result_clock_mode=2u;
        state->result_lap_centiseconds=snapshot->source_result_times_centiseconds[snapshot->lap_count-1u];
    }
    for (size_t rank = 0u; rank < 4u; ++rank) {
        const uint8_t racer = session->roster.standings[rank];
        if (racer >= PORTABLE_GP_RACER_COUNT) return 0;
        state->gp_top_four_classes[rank] = session->roster.racers[racer].driver_class;
        state->gp_top_four_racers[rank] = racer;
    }
    state->gp_item = snapshot->item_hud;
    state->item_box_count = session->item_box_count;
    if (state->item_box_count > PORTABLE_RACE_SCENE_MAX_ITEM_BOXES)
        state->item_box_count = PORTABLE_RACE_SCENE_MAX_ITEM_BOXES;
    for (size_t index = 0u; index < state->item_box_count; ++index)
    {
        state->item_boxes[index].position_x_q16 =
            session->item_boxes[index].placement.position_x_q16;
        state->item_boxes[index].position_y_q16 =
            session->item_boxes[index].placement.position_y_q16;
        state->item_boxes[index].height_q16 = 0;
        state->item_boxes[index].active = session->item_boxes[index].active;
    }
    {
        if (portable_gp_race_roster_lightning_affine_scale(
                &session->roster, session->local_index, UINT16_C(0x0100),
                &state->character_affine_scale) != PORTABLE_GP_ROSTER_OK)
            return 0;
        for(size_t owner=0u;owner<PORTABLE_GP_GREEN_ACTOR_SLOTS;++owner){
            const PortableGpGreenShell *shell=portable_gp_green_actor(&session->roster,owner);
            state->green_shells[owner]=(PortableRaceItemPresentation){
                .x_q16=shell->position_x_q16,.y_q16=shell->position_y_q16,
                .z_q16=shell->position_z_q16,.animation_index=shell->display_index,
                .visible=(uint8_t)(shell->occupied && (shell->flags_34&1u))};
        }
        for(size_t owner=0u;owner<PORTABLE_GP_RED_ACTOR_SLOTS;++owner){
            const PortableGpRedShell *shell=portable_gp_red_actor(&session->roster,owner);
            state->red_shells[owner]=(PortableRaceItemPresentation){
                .x_q16=shell->position_x_q16,.y_q16=shell->position_y_q16,
                .z_q16=shell->position_z_q16,.animation_index=shell->display_index,.kind=shell->kind_10,
                .visible=(uint8_t)(shell->occupied && (shell->flags_34&1u))};
        }
        for(size_t owner=0u;owner<PORTABLE_GP_BANANA_ACTOR_SLOTS;++owner){
            const PortableGpBanana *banana=portable_gp_banana_actor(&session->roster,owner);
            state->bananas[owner]=(PortableRaceItemPresentation){
                .x_q16=banana->position_x_q16,.y_q16=banana->position_y_q16,
                .z_q16=banana->position_z_q16,
                .visible=(uint8_t)(banana->occupied && (banana->flags_34&1u))};
        }
        state->lightning_effect_frame =
            session->roster.lightning_effect.frame_index;
        state->lightning_effect_visible = (uint8_t)(
            session->roster.lightning_effect.occupied &&
            session->roster.lightning_effect.visible);
    }

    for (size_t index = 0u; index < state->opponent_count; ++index)
    {
        const PortableGpOpponentSnapshot *source = &snapshot->opponents[index];
        PortableRaceOpponentPresentation *opponent = &state->opponents[index];
        opponent->position_x_q16 = source->position_x_q16;
        opponent->position_y_q16 = source->position_y_q16;
        opponent->height_q16 = source->height_q16;
        opponent->marker_source_flags=source->marker_source_flags;
        opponent->marker_class_flags=source->driver_class;
        const size_t other = portable_gp_race_session_other_racer(session, index);
        opponent->visible = (uint8_t)(source->visible &&
            !boo_hidden(session->roster.racers[other].contact_status_66,0,
                        session->source_frame_clock) &&
            !source->driver_hidden);
        opponent->palette_bank = source->palette_bank;
        opponent->flattened=source->flattened;
        opponent->art_motion=source->art_motion;
        opponent->driver_affine_x=source->affine.inverse_x_q8;
        opponent->driver_affine_y=source->affine.inverse_y_q8;
        opponent->balloons_remaining = source->balloons_remaining;
        opponent->heading = source->balloon_heading;
        opponent->revenge_bomb = source->revenge_bomb;
        opponent->bomb_walk_phase = source->bomb_walk_phase;
        opponent->bomb_response = source->bomb_response;
        opponent->bomb_speed_q16 = source->speed_q16;
        if (portable_gp_race_roster_lightning_affine_scale(
                &session->roster, other, UINT16_C(0x0100),
                &opponent->character_affine_scale) != PORTABLE_GP_ROSTER_OK)
            return 0;
        const PortableCharacterViewInput view={.camera_yaw=snapshot->camera_yaw,
            .driver_angle=source->source_angle,.steering_response_q8=source->steering_response,
            .contact_mode_6e=source->contact_mode,.contact_phase_6f=source->contact_phase,
            .contact_frame_6c=source->contact_frame,.contact_flags_6d=source->contact_flags};
        opponent->art_pose=portable_character_view_pose(&view,session->view_rules);
        opponent->visual_yaw=portable_character_visual_yaw(&view,session->view_rules);
        opponent->art_pose_valid=1u;
        opponent->character_view_angle=opponent->art_pose.angle;
    }
    return 1;
}
