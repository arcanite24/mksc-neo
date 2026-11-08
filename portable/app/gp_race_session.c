#include "diagnostic.h"
#include "source_random.h"
#include "gp_race_session.h"
#include "surface_trail_driver.h"
#include "gp_coin_hud.h"
#include "spatial_audio_pitch.h"

#include <stddef.h>
#include <string.h>
#include "thwomp_motion.h"
#include "course_prop_motion.h"
#include "crab_motion.h"
#include "seagull_motion.h"
#include "piranha_motion.h"
#include "snowman_motion.h"
#include "mouse_motion.h"
#include "thundercloud_motion.h"
#include "falling_star_motion.h"
#include "penguin_motion.h"
#include "boo_motion.h"
#include "course_contacts.h"
#include "cannonball_motion.h"

#include "driver_angle_callbacks.h"
#include "driver_forward_acceleration.h"
#include "driver_input.h"
#include "driver_palette.h"
#include "character_view.h"
#include "driver_water_recovery.h"
#include "kart_motion.h"

/* A single-player tick runs every part in source order. With 2..4 locals,
 * each local's pass runs TICK_LOCAL and the shared world then runs once. */
enum
{
    TICK_LOCAL = 1u,
    TICK_WORLD = 2u,
    TICK_ALL = 3u
};

static int multi_local(const PortableGpRaceSession *session)
{
    return session->local_count > 1u;
}

static unsigned tick_parts(const PortableGpRaceSession *session)
{
    return multi_local(session) ? TICK_LOCAL : TICK_ALL;
}

enum
{
    PORTABLE_GP_GRID_COLUMNS_OFFSET = 20u,
    PORTABLE_GP_GRID_FIRST_ROW_OFFSET = 8u,
    PORTABLE_GP_GRID_ROW_SPACING = 24u,
    PORTABLE_GP_PLAYER_GRID_RANK = 7u
};

/* sub_8040E8C samples the previous rev value through sub_8040DD8 before
 * updating +0x92. sub_803AE70 then drains that same value during a held-A
 * launch; +0xb4 bit 0 selects the source's 0xc000 acceleration branch. */
static void tick_player_launch_revs(PortableGpRaceSession *session, uint16_t control_word)
{
    uint16_t flags = session->player.steering.response_flags;
    int32_t revs = (int16_t)session->launch_revs_92;
    if (session->runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN)
    {
        /* 40DD8: B2 bit 0 requests A9AC's start boost when A is held in the
         * 0x1c0..0x3bf window; above 0x5bf B4 bit 0 is the over-rev launch. */
        uint16_t *requests =
            &session->roster.racers[session->local_index].item_response_flags_b2;
        *requests &= UINT16_C(0xfffc);
        if (revs > 0x05bf)
            flags |= UINT16_C(1);
        else
        {
            flags &= UINT16_C(0xfffe);
            if (revs <= 0x03bf && revs >= 0x01c0 && (control_word & UINT16_C(0x8000)) != 0u)
                *requests |= UINT16_C(1);
        }
        portable_driver_engine_oscillate(&session->launch_revs_92, &session->launch_rev_mode_98,
                                         control_word, 0x20, 0x200, 0x100);
        revs = session->launch_revs_92;
    }
    else if (session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
             (flags & UINT16_C(1)) != 0u)
    {
        if (revs > 0x01ff)
        {
            revs -= (control_word & UINT16_C(0x8000)) != 0u ? 0x10 : 0x40;
            flags |= UINT16_C(0x20);
        }
        else
            flags &= UINT16_C(0xffde);
    }
    session->launch_revs_92 = (uint16_t)revs;
    /* The observed GP entry calls40FEC first at racing tick6, then every
     * eight ticks. Its source92 also controls launch acceleration. */
    if (session->engine_rev_profile.limit > 0 &&
        session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
        session->runtime.session.elapsed_ticks >= 6u &&
        ((session->runtime.session.elapsed_ticks - 6u) & 7u) == 0u)
    {
        PortableDriverEngineSoundInput input = portable_driver_engine_sound_input(&session->player);
        input.control_word = control_word;
        portable_driver_engine_revs_tick(&session->launch_revs_92, &session->launch_rev_mode_98,
                                         &session->engine_rev_profile, &input);
    }
    session->player.steering.response_flags = flags;
    session->player.forward_acceleration.state_flags = flags;
}

static void adopt_player_bomb_respawn(PortableGpRaceSession *session)
{
    PortableGpRacer *r = &session->roster.racers[session->local_index];
    if (!r->bomb_respawn_pending)
        return;
    PortableDriverPilotState *p = &session->player;
    p->source_hazard_kind = PORTABLE_DRIVER_HAZARD_BATTLE_BOMB;
    p->source_terrain22_active = 0u;
    p->motion.speed_q16 = r->speed_q16;
    p->motion.acceleration_q16 = r->acceleration_q16;
    p->steering.response_q8 = 0;
    p->forward_acceleration.response_q8 = 0;
    p->steering.base_angle = 0;
    p->steering.position_82 = 0u;
    p->steering.driver_mode = 2u;
    p->auto_acceleration.driver_mode = 2u;
    p->hop.mode = 9u;
    p->hop.height_q16 = r->world_height_q16;
    p->pose.position_z = r->world_height_q16;
    p->fall_frames_76 = r->fall_frames_76;
    p->steering.state_flags = r->source_flags;
    p->hop.state_flags = r->source_flags;
    p->recovery_contact_34 = r->recovery_contact_34;
    p->drift_charge.charge_counter = r->recovery_timer_9c;
    p->recovery_target_x_9e = (uint16_t)r->recovery_target_x_9e;
    p->recovery_target_y_a0 = (uint16_t)r->recovery_target_y_a0;
    p->recovery_target_angle_a2 = r->recovery_target_heading_a2;
    p->item_cue_flags_58 = r->hit_cue_flags_58;
    r->bomb_respawn_pending = 0u;
}

static uint16_t race_camera_yaw(const PortableGpRaceSession *session)
{
    return session->finish_camera.snapshot.active
               ? session->finish_camera.snapshot.yaw
               : portable_character_camera_yaw(&session->character_camera);
}

/* main_getRandom owns visual randomness separately from item/AI randomness.
 * Native race entry seeds this owner at zero for deterministic replay. */
static uint32_t main_visual_random(void *context)
{
    return portable_source_random((uint32_t *)context);
}
static PortableSnowWeatherInputs snow_weather_inputs(const PortableGpRaceSession *s, uint32_t clock)
{
    const int finish = s->finish_camera.snapshot.active;
    return (PortableSnowWeatherInputs){
        .camera_x_q16 = finish ? s->finish_camera.snapshot.x_q16 : s->player.pose.position_x,
        .camera_y_q16 = finish ? s->finish_camera.snapshot.y_q16 : s->player.pose.position_y,
        .yaw = race_camera_yaw(s),
        .parity = (uint8_t)(clock & 1u)};
}

static int coin_body(const PortableGpRaceSession *session, PortableCameraProjectionResult *body)
{
    return portable_selected_driver_body(
        &session->player, portable_character_camera_yaw(&session->character_camera),
        &session->finish_camera.snapshot,
        session->sky_motion.initialized ? (int8_t)(session->sky_motion.snapshot.road_line - 74) : 0,
        session->player_dependencies.sine_table, session->player_dependencies.sine_count, body);
}

static int tick_surface_trails(PortableGpRaceSession *session)
{
    if (session->surface_trail_rules == NULL)
        return 1;
    PortableSurfaceTrailInputs input;
    return portable_surface_trail_driver_inputs(
               &session->player, portable_character_camera_yaw(&session->character_camera),
               &session->finish_camera.snapshot, session->source_frame_clock,
               session->sky_motion.initialized
                   ? (int8_t)(session->sky_motion.snapshot.road_line - 74)
                   : 0,
               session->player_dependencies.sine_table, session->player_dependencies.sine_count,
               &input) &&
           portable_surface_trail_tick(&session->surface_trails, session->surface_trail_rules,
                                       &input, session->player_dependencies.sine_table,
                                       session->player_dependencies.sine_count) >= 0;
}

/* C4C0/D00C reads preceding projected coordinates before rival motion and
 * the post-driver camera pass. CPUs may receive requests but do not own it. */
static int request_contact_art_for(PortableGpRaceSession *session, size_t actor)
{
    if (session->runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN)
        return 1;
    uint8_t views[PORTABLE_GP_RACER_COUNT] = {0};
    const uint16_t yaw = race_camera_yaw(session);
    for (size_t i = 0u; i < portable_gp_race_roster_count(&session->roster); ++i)
    {
        const PortableGpRacer *racer = &session->roster.racers[i];
        const int local = i == session->local_index;
        const PortableCharacterViewInput input = {
            .camera_yaw = yaw,
            .selected = (uint8_t)local,
            .driver_angle =
                local ? session->player.steering.source_angle_56 : racer->source_heading,
            .source_flags = local ? session->player.steering.state_flags : racer->source_flags,
            .steering_response_q8 =
                local ? session->player.steering.response_q8 : (int16_t)racer->hit_counter_88,
            .speed_q16 = local ? session->player.motion.speed_q16 : racer->speed_q16,
            .position_82 = session->player.steering.position_82,
            .camera_adjustment = session->character_camera.current_adjustment,
            .contact_mode_6e = racer->contact_mode_6e,
            .contact_phase_6f = racer->contact_phase_6f,
            .contact_frame_6c = racer->contact_frame_6c,
            .contact_flags_6d = racer->contact_flags_6d};
        views[i] = portable_character_view_pose(&input, session->view_rules).angle;
    }
    return portable_gp_race_roster_resolve_view_contacts(
               &session->roster, actor, session->rules.route_count, views) == PORTABLE_GP_ROSTER_OK;
}

static void sync_player(PortableGpRaceSession *session)
{
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    /* The item actor owns10000, including its flight lock and contact clear. */
    session->player.steering.state_flags =
        (session->player.steering.state_flags & ~UINT32_C(0x10000)) |
        (player->source_flags & UINT32_C(0x10000));

    if (session->player.battle_enabled)
    {
        player->balloons_remaining = session->player.balloons_remaining;
        player->balloon_event = session->player.balloon_event;
        player->hit_state_flags_74 = session->player.recovery_flags_74;
    }
    player->kart_contact_state_5a = session->player.vector_effect.contact_word_5a;
    player->affine = session->player.affine;
    player->art_motion = session->player.art_motion;
    player->burn_ticks_72 = session->player.burn_ticks_72;
    player->palette_requests_194 = session->player.palette_requests_194;
    player->palette_bank = session->player.palette_bank;
    player->position_x_q16 = session->player.pose.position_x;
    player->position_y_q16 = session->player.pose.position_y;
    player->speed_q16 = session->player.motion.speed_q16;
    player->world_height_q16 = session->player.pose.position_z;
    player->fall_frames_76 = session->player.fall_frames_76;
    player->acceleration_q16 = session->player.motion.acceleration_q16;
    player->component_x = session->player.motion.component_x;
    player->component_y = session->player.motion.component_y;
    player->heading = (uint16_t)session->player.motion.angle;
    player->source_heading = session->player.steering.source_angle_56;
    player->release_angle_offset = session->player.steering.steering_offset;
    player->hit_spin_82 = (int16_t)session->player.steering.position_82;
    player->contact_heading_56 = session->player.steering.source_angle_56;
    player->control = session->player.input.control_word;
    /* The recovered Beach pilot remains the flag/mode owner after it leaves
     * material22. Normal9B38/A9AC clears800 on the turn following touchdown. */
    if (session->player.source_terrain22_active || session->player.source_hazard_enabled ||
        (session->roster.course_id == 25u && session->player.source_terrain22_enabled))
    {
        player->source_flags = session->player.steering.state_flags;
        player->mushroom_mode_84 = session->player.steering.driver_mode;
        player->lightning_state_78 = session->player.hop.mode;
        player->hit_state_flags_74 = session->player.recovery_flags_74;
    }
    const int new_source_wrap = player->route_wrap_91 != session->runtime.session.lap_raw;
    if (new_source_wrap)
        portable_gp_race_roster_apply_route_wrap(player, &session->rules,
                                                 session->runtime.session.lap_raw);
    /* The core B98C observer owns90/19B, including airborne rejection and
     * the carry Y resample. Publishing the raw cell again here bypasses that
     * gate and makes the next9828 rescue choose a different checkpoint. */
    if ((session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING || new_source_wrap) &&
        session->runtime.session.elapsed_ticks != 0u)
    {
        player->route_index = session->player.source_route_index_90;
        player->acceleration_mode_19b = session->player.source_speed_variant_19b;
        player->source_flags = (player->source_flags & ~UINT32_C(3)) |
                               (session->player.steering.state_flags & UINT32_C(3));
    }
    else if (session->runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN)
        player->route_index = session->runtime.session.checkpoint_raw & UINT8_C(0x7f);
    player->mushroom_mode_84 = session->player.steering.driver_mode;
    player->mushroom_timer_cc = session->player.steering.exit_timer;
    /* The pilot consumes only A9AC's boost bits (item and start); item/actor
     * writers retain ownership of the other source B2 requests. */
    player->item_response_flags_b2 =
        (uint16_t)((player->item_response_flags_b2 & UINT16_C(0x7ffe)) |
                   (session->player.item_requests_b2 & UINT16_C(0x8001)));
    player->mushroom_response_flags_b4 = session->player.steering.response_flags;
    player->hit_response_b6 = session->player.strong_spin_rate_b6;
    player->hit_state_7e = session->player.steering.response_status;
    /* +0x17d and +0xae are source-owned contested-progress fields. Keep the
     * roster initializer's captured values until their player writers are
     * recovered; the accepted opening standings use route wrap/index. */
    player->motion_active =
        (uint8_t)(session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
                  session->runtime.session.elapsed_ticks != 0u);

    /* The source uses +0x38 and +0x48 as part of the same kart-contact
     * response consumed by the driver tick.  Once that bounded response is
     * active, mirror its authoritative countdown/flags back into the GP
     * roster view instead of leaving the collision detector's entry values
     * frozen there. */
    if (session->player.normal_collision_timer_38 != 0u || player->banana_hit_timer_38 != 0u)
    {
        player->banana_hit_timer_38 = session->player.normal_collision_timer_38;
        player->source_flags = session->player.steering.state_flags;
    }
}

static int begin_player_kart_contact_response(PortableGpRaceSession *session)
{
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    PortableSourceNormalResponseResult release;

    /* C68B2 captured the first Peach Circuit player/rival-2 contact at the
     * sub_803E92C return.  The following source wrapper applies the existing
     * sub_803AD6C normal-response helper to that resultant and then holds the
     * pair while +0x38 counts down.  Reuse the core source adapter here; its
     * max-component magnitude and source angle produce 0x01f90000/0x2d03 for
     * the captured (505,-254) Bowser response. */
    session->player.normal_collision_source_from_native_state = 1u;
    session->player.normal_collision_source_use_b550_table = 1u;
    session->player.normal_collision_timer_38 = player->banana_hit_timer_38;
    session->player.vector_effect.contact_word_5a = player->kart_contact_state_5a;
    if (portable_driver_pilot_apply_normal_collision_source_angle(
            &session->player, player->component_x, player->component_y,
            (uint16_t)session->player.steering.steering_offset, player->source_flags,
            (uint16_t)session->player.steering.angle, (uint16_t)session->player.steering.base_angle,
            INT16_C(0x00a0), session->player.motion.speed_q16, 0u, 0u) != PORTABLE_DRIVER_PILOT_OK)
        return 0;
    session->player.motion.component_x = player->component_x;
    session->player.motion.component_y = player->component_y;
    session->player.pose.component_x = player->component_x;
    session->player.pose.component_y = player->component_y;
    if (session->player_dependencies.normal_response_table == NULL ||
        portable_source_normal_response_produce(
            UINT8_C(0x0b), (int16_t)session->player.normal_collision.angle_delta_80,
            session->player_dependencies.normal_response_table,
            &release) != PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK)
        return 0;
    session->player_contact_release_pending = 1u;
    return 1;
}

static int advance_player_kart_contact_response(PortableGpRaceSession *session,
                                                int32_t prior_acceleration_q16)
{
    PortableSourceNormalResponseResult response;
    int32_t delta = (int16_t)session->player.steering.base_angle;
    int32_t position = (int16_t)session->player.steering.position_82;

    /* 8E34's common prologue and status-11 branch run after the contact
     * hold. The frame-2132 source call clears +0xb4 bits and approaches
     * +0x82 by 0x400 before it steps +0x80. */
    if ((session->player.steering.response_flags & UINT16_C(1)) == 0u)
        session->player.steering.response_flags &= UINT16_C(0xffd3);

    /* At the captured frame 2140, 8E34 sees +bc below 1.0, clears +0x80,
     * and the following DAEC step changes status 11 to 14. The retained
     * +0xc0 still belongs to this frame's completed motion. */
    if ((uint32_t)session->player.motion.speed_q16 <= UINT32_C(0x00ffffff))
    {
        session->player.motion.acceleration_q16 = prior_acceleration_q16;
        session->player.steering.base_angle = 0;
        session->player.normal_collision.angle_delta_80 = 0u;
        session->player.normal_collision_response_delta_80 = 0u;
        session->player.steering.response_status = UINT16_C(0x000e);
        session->player.normal_collision.response_timer_7e = UINT8_C(0x0e);
        session->player.normal_collision.timer_value_84 = 0u;
        session->player_contact_response_active = 0u;
        session->player_contact_clear_status_pending = 1u;
        return 1;
    }
    if (position > 0)
    {
        position -= INT32_C(0x0400);
        if (position < 0)
            position = 0;
    }
    else if (position < 0)
    {
        position += INT32_C(0x0400);
        if (position > 0)
            position = 0;
    }
    session->player.steering.position_82 = (uint16_t)position;

    /* The status-11 response approaches +0x80 by 0x400 after heading
     * composition.  +0x7a therefore shows the prior value for one frame while
     * the packed response table selects the next +0xc0 from the new value. */
    if (delta > 0)
    {
        delta -= INT32_C(0x0400);
        if (delta < 0)
            delta = 0;
    }
    else if (delta < 0)
    {
        delta += INT32_C(0x0400);
        if (delta > 0)
            delta = 0;
    }
    session->player.steering.base_angle = (int16_t)delta;
    session->player.normal_collision.angle_delta_80 = (uint16_t)delta;
    session->player.normal_collision_response_delta_80 = (uint16_t)delta;
    if (portable_source_normal_response_produce(
            UINT8_C(0x0b), (int16_t)delta, session->player_dependencies.normal_response_table,
            &response) != PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK)
        return 0;
    session->player.normal_collision_response_next_status = response.next_status;
    if (response.output_written)
    {
        session->player.motion.acceleration_q16 = response.output_q16;
        return 1;
    }

    session->player.steering.response_status = response.next_status;
    session->player.normal_collision.response_timer_7e = response.next_status;
    session->player.normal_collision.timer_value_84 = 0u;
    session->player_contact_response_active = 0u;
    session->player_contact_clear_status_pending = 1u;
    return 1;
}

static void finish_local_pilot(PortableGpRaceSession *session)
{
    if (!session->player.finished_initialized)
        portable_driver_pilot_retire_selected(&session->player);
    session->player.steering.state_flags =
        (session->player.steering.state_flags & ~UINT32_C(0x38)) | UINT32_C(0x2034);
    session->roster.racers[session->local_index].source_flags =
        session->player.steering.state_flags;
    session->roster.racers[session->local_index].lap_marker_a4 |= UINT16_C(0x4000);
}

/* sub_804A878's mode-0x400 branch: once every racer but the last has
 * finished, the race ends; the last kart is stopped where it stands. */
static int advance_vs_finish(PortableGpRaceSession *session, unsigned parts)
{
    PortableGpRaceRoster *roster = &session->roster;
    if ((parts & TICK_LOCAL) && session->runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED)
    {
        if ((roster->racers[session->local_index].source_flags & UINT32_C(0x20)) == 0u)
            finish_local_pilot(session);
        /* B7B8 retires the driver inside the runtime's progress callback,
         * before this pass sees flags20. DAC4's completed clock is independent
         * of that flag and remains fixed while the live HUD clock advances. */
        const PortableGpSourceClock *clock = &session->source_result_clock;
        if (!session->vs_retired[session->local_index] && clock->lap_count != 0u &&
            clock->laps_recorded == clock->lap_count)
            session->vs_finish_centiseconds[session->local_index] = clock->finish_centiseconds;
    }
    if (!(parts & TICK_WORLD))
        return 1;
    const uint8_t phase = (uint8_t)(roster->race_phase_7d4 & UINT8_C(0x0f));
    if (phase == 0u)
    {
        size_t finished = 0u;
        for (size_t i = 0u; i < session->local_count; ++i)
            if (roster->racers[i].source_flags & UINT32_C(0x20))
                ++finished;
        if (finished + 1u >= session->local_count)
        {
            for (size_t i = 0u; i < session->local_count &&
                                i < sizeof session->vs_retired / sizeof session->vs_retired[0];
                 ++i)
                if ((roster->racers[i].source_flags & UINT32_C(0x20)) == 0u)
                    session->vs_retired[i] = 1u;
            roster->race_phase_7d4 = UINT8_C(0x83);
        }
    }
    else if (phase == 3u)
    {
        roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 4u);
        roster->finish_countdown = 0u;
        roster->finish_player_rank = (uint8_t)(roster->racers[0].rank_flags_ba >> 1u);
    }
    else if (phase == 4u)
    {
        roster->race_phase_7d4 = (uint8_t)((roster->race_phase_7d4 & UINT8_C(0xf0)) | 5u);
    }
    return 1;
}

static int advance_gp_finish(PortableGpRaceSession *session, unsigned parts)
{
    uint8_t phase;
    if ((session->roster.race_mode_flags_7da & UINT16_C(0x0f00)) == UINT16_C(0x0800))
    {
        if (session->runtime.session.phase != PORTABLE_RACE_PHASE_RACING &&
            session->runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED)
            return 1;
        if (parts & TICK_WORLD)
        {
            const PortableGpRosterStatus result =
                portable_battle_race_roster_tick_finish_transition(&session->roster);
            if (result != PORTABLE_GP_ROSTER_OK && result != PORTABLE_GP_ROSTER_ITEM_INELIGIBLE)
                return 0;
        }
        if ((session->roster.race_phase_7d4 & UINT8_C(0x0f)) >= 4u)
        {
            if (parts & TICK_LOCAL)
            {
                session->roster.battle_scene_state = 4u;
                session->player.battle_scene_state = 4u;
                session->runtime.session.phase = PORTABLE_RACE_PHASE_FINISHED;
                session->source_result_clock.enabled = 0u;
                session->player.steering.state_flags =
                    session->roster.racers[session->local_index].source_flags;
                session->player.motion.speed_q16 = 0;
            }
            if (!(parts & TICK_WORLD))
                return 1;
            const PortableGpRosterStatus banner = portable_battle_race_roster_tick_finish_banner(
                &session->roster, session->player_dependencies.sine_table,
                session->player_dependencies.sine_count);
            return banner == PORTABLE_GP_ROSTER_OK || banner == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
        }
        return 1;
    }
    if (multi_local(session))
        return advance_vs_finish(session, parts);
    if (session->runtime.session.phase != PORTABLE_RACE_PHASE_FINISHED)
        return 1;
    phase = (uint8_t)(session->roster.race_phase_7d4 & UINT8_C(0x0f));
    if (phase == 0u)
    {
        /* sub_803B7B8's GP finish branch clears +0x7e/+0x80/+0x82 and
         * +0x84, marks +0xa4 bit 14, and retires the final lap cue in
         * +0x48. Direct original driver memory at frames 8718/8719 moves
         * 11/0x05fe/11/0x800c to 0/0/0/0xa034. */
        finish_local_pilot(session);
        /* The retained first-place Peach GP finish is mode 0x0100 at
         * source frame 5657. Mode 0x0200 would finish every rival here. */
        session->roster.race_mode_flags_7da =
            (uint16_t)(UINT16_C(0x0100) | (session->roster.race_mode_flags_7da & UINT16_C(0x10)));
        session->roster.race_phase_7d4 = UINT8_C(0x83);
        phase = 3u;
    }
    if (phase == 3u || phase == 4u)
        return portable_gp_race_roster_tick_finish_transition(&session->roster) ==
               PORTABLE_GP_ROSTER_OK;
    return phase == 5u;
}

static int roster_call_succeeded(PortableGpRosterStatus status)
{
    return status == PORTABLE_GP_ROSTER_OK || status == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
}

static PortableGpRaceSessionStatus apply_coin_loss_at(PortableGpRaceSession *session,
                                                      PortableCoinEffectCause cause, uint32_t clock)
{
    if (session == NULL || cause < PORTABLE_COIN_EFFECT_CAUSE_NONE ||
        cause > PORTABLE_COIN_EFFECT_CAUSE_HAUNT)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    static const uint8_t amounts[6] = {0u, 4u, 2u, 1u, 8u, 1u};
    const unsigned requested = amounts[cause];
    /* D4E4's negative scene override precedes the saved coins policy. */
    if (requested == 0u ||
        (session->source_scene_row_c88 >= 0 && session->quick_run_options.coins_off))
    {
        session->recovery_coin_loss = 0u;
        return PORTABLE_GP_RACE_SESSION_OK;
    }
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    const uint32_t flags = session->player.steering.state_flags;
    if ((flags & UINT32_C(0x20)) != 0u || (flags & UINT32_C(0x8000)) == 0u)
    {
        session->recovery_coin_loss = 0u;
        return PORTABLE_GP_RACE_SESSION_OK;
    }
    if (session->source_scene_row_c88 < 0)
    {
        const uint32_t scene = (uint32_t)session->source_scene_row_c88;
        if ((scene & UINT32_C(0xff01)) == 0u)
        {
            session->recovery_coin_loss = 0u;
            return PORTABLE_GP_RACE_SESSION_OK;
        }
    }
    else if ((session->roster.race_mode_flags_7da & UINT16_C(0xf00)) == UINT16_C(0x800))
    {
        /* D4E4 leaves the loss gate clear in Battle (mode 0x0800).
         * Recovery and bomb contacts still invoke it, even with zero coins. */
        session->recovery_coin_loss = 0u;
        return PORTABLE_GP_RACE_SESSION_OK;
    }
    else if (session->roster.race_mode_flags_7da != UINT16_C(0x100) &&
             session->roster.race_mode_flags_7da != UINT16_C(0x110) &&
             session->roster.race_mode_flags_7da != UINT16_C(0x400))
    {
        /* Ghost/other mode settings read save halfwords in D4E4. Their
         * producers are outside this normal-GP card, not assumed enabled. */
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    }
    const uint16_t loss =
        player->coin_count_178 < requested ? player->coin_count_178 : (uint16_t)requested;
    player->coin_count_178 = (uint16_t)(player->coin_count_178 - loss);
    session->recovery_coin_loss = (uint8_t)loss;
    if (loss && session->coin_effect_rules)
    {
        const PortableCoinEffectRequest request = {.cause = cause,
                                                   .loss = (uint8_t)loss,
                                                   .remaining = (uint8_t)player->coin_count_178,
                                                   .clock = clock,
                                                   .x = session->coin_birth_x,
                                                   .y = session->coin_birth_y,
                                                   .inverse = session->coin_birth_inverse};
        if (!portable_coin_effect_request(&session->coin_effects, session->coin_effect_rules,
                                          &request))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    /* 49850 shares the player's count before the next driver pass. Current
     * rivals already moved: retain their old coins on this actor turn. */
    session->rival_coin_count_handoff = (uint8_t)player->coin_count_178;
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus portable_gp_race_session_apply_coin_loss(PortableGpRaceSession *session,
                                                                     PortableCoinEffectCause cause)
{
    return apply_coin_loss_at(session, cause, session ? session->source_frame_clock : 0u);
}

static int queue_race_audio(PortableGpRaceSession *session, uint16_t song_id, uint8_t player,
                            uint16_t volume, int16_t pitch)
{
    PortableGpRaceAudioEvent *event;
    if (session->audio_event_count >= PORTABLE_GP_RACE_AUDIO_EVENT_CAPACITY)
        return 0;
    event = &session->audio_events[session->audio_event_count++];
    *event = (PortableGpRaceAudioEvent){.song_id = song_id,
                                        .player = player,
                                        .volume = volume,
                                        .pitch = pitch,
                                        .operation = PORTABLE_GP_AUDIO_SONG};
    return 1;
}

/* Source B7D0/B988 callbacks share one selected-local owner. Producers in
 * the roster have already subtracted coins: consume their cause without a
 * second gameplay mutation. */
int portable_gp_race_session_tick_dropped_coins(PortableGpRaceSession *session)
{
    if (!session)
        return 0;
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    if (player->coin_loss_event_overflow)
        return 0;
    for (uint8_t i = 0u; i < player->coin_loss_event_count; ++i)
    {
        if (!session->coin_effect_rules)
            continue;
        const PortableCoinEffectRequest request = {
            .cause = (PortableCoinEffectCause)player->coin_loss_events[i].cause,
            .loss = player->coin_loss_events[i].loss,
            .remaining = (uint8_t)player->coin_loss_events[i].remaining,
            .clock = player->coin_loss_events[i].clock,
            .x = session->coin_birth_x,
            .y = session->coin_birth_y,
            .inverse = session->coin_birth_inverse};
        if (!portable_coin_effect_request(&session->coin_effects, session->coin_effect_rules,
                                          &request))
            return 0;
    }
    player->coin_loss_event_count = 0u;
    if (!session->coin_effect_rules)
        return 1;
    PortableCoinEffectAudioEvents audio;
    if (!portable_coin_effect_tick(&session->coin_effects, session->coin_effect_rules,
                                   session->coin_birth_inverse, &audio))
        return 0;
    for (uint8_t i = 0u; i < audio.count; ++i)
    {
        if (!queue_race_audio(session, audio.events[i].song_id, 7u, audio.events[i].volume, 0))
            return 0;
        session->audio_events[session->audio_event_count - 1u].unless_busy = 1u;
    }
    return 1;
}

/* F450: 167 at volume 192+speed[25:20] and pitch speed[25:16]-512. */
static int queue_player_bump(PortableGpRaceSession *session)
{
    const int32_t speed = session->roster.player_bump_speed_q16;
    const uint32_t magnitude = speed < 0 ? (uint32_t)-(int64_t)speed : (uint32_t)speed;
    if (!session->roster.player_bump_pending)
        return 1;
    session->roster.player_bump_pending = 0u;
    if ((session->roster.racers[session->local_index].source_flags & UINT32_C(0x20)) != 0u)
        return 1;
    return queue_race_audio(session, 167u, 6u, (uint16_t)(192u + ((magnitude >> 20) & 63u)),
                            (int16_t)((int32_t)((magnitude >> 16) & 0x3ffu) - 512));
}

static uint8_t voice_small(const PortableGpRacer *racer)
{
    return (uint8_t)((racer->hit_cue_flags_58 & UINT16_C(0x10)) == 0u);
}

/* Presentation-only: a saturated frame drops a voice instead of failing. */
static int queue_voice(PortableGpRaceSession *session, uint16_t song, uint8_t player,
                       uint16_t volume)
{
    if (session->audio_event_count >= PORTABLE_GP_RACE_AUDIO_EVENT_CAPACITY)
        return 1;
    return queue_race_audio(session, song, player, volume, 0);
}

/* E2B4/40D8C/804E144 voices. ROM tables ECBC8 (ouch: local normal/small,
 * remote normal/small), ECC88 (local taunt) and ECC68 (overtake laugh). */
static int tick_race_voices(PortableGpRaceSession *session, unsigned parts)
{
    PortableGpRaceRoster *roster = &session->roster;
    const PortableGpRacer *local = &roster->racers[session->local_index];
    const int muted = (local->source_flags & UINT32_C(0x20)) != 0u;
    uint8_t kept = 0u;
    for (uint8_t i = 0u; (parts & TICK_WORLD) && i < roster->pending_voice_count; ++i)
    {
        PortableGpPendingVoice voice = roster->pending_voices[i];
        if (voice.ticks > 1u)
        {
            --voice.ticks;
            roster->pending_voices[kept++] = voice;
            continue;
        }
        if (voice.racer >= 8u)
            continue;
        if (voice.taunt == 2u)
        {
            if (!muted &&
                (roster->hazard_sound_rules == NULL ||
                 !queue_voice(session,
                              roster->hazard_sound_rules
                                  ->taunt[portable_gp_racer_voice_class(local)][voice_small(local)],
                              2u, 256u)))
                return 0;
            continue;
        }
        const PortableGpRacer *victim = &roster->racers[voice.racer];
        if (!muted)
        {
            const int remote = !victim->is_player;
            uint16_t volume = 256u;
            if (remote)
            {
                volume =
                    portable_gp_item_spatial_volume(victim->position_x_q16, victim->position_y_q16,
                                                    local->position_x_q16, local->position_y_q16);
                if (volume < 64u)
                    volume = 64u;
            }
            if (roster->hazard_sound_rules == NULL ||
                !queue_voice(
                    session,
                    roster->hazard_sound_rules->impact_voices[portable_gp_racer_voice_class(victim)]
                                                             [remote][voice_small(victim)],
                    remote ? 4u : 2u, volume))
                return 0;
        }
        if (voice.taunt == 1u)
        {
            voice.taunt = 2u;
            voice.ticks = 60u;
            roster->pending_voices[kept++] = voice;
        }
    }
    if (parts & TICK_WORLD)
        roster->pending_voice_count = kept;
    if (!(parts & TICK_LOCAL))
        return 1;
    /* 804E144: GP/Quick only; when the local rank drops, the kart now one
     * place ahead laughs. At most one laugh per frame. */
    const uint16_t mode = (uint16_t)(roster->race_mode_flags_7da & 0xf00u);
    const uint8_t rank = (uint8_t)(local->rank_flags_ba >> 1u);
    if (!session->voice_rank_ready)
    {
        session->voice_rank_ready = 1u;
        session->voice_previous_rank = rank;
    }
    if ((mode == 0x100u || mode == 0x400u) && rank != session->voice_previous_rank)
    {
        if (rank > session->voice_previous_rank && !muted && rank >= 1u && rank <= 8u &&
            roster->standings[rank - 1u] < 8u)
        {
            const PortableGpRacer *passer = &roster->racers[roster->standings[rank - 1u]];
            if (roster->hazard_sound_rules == NULL ||
                !queue_voice(session,
                             roster->hazard_sound_rules->laugh[portable_gp_racer_voice_class(
                                 passer)][voice_small(passer)],
                             4u, 256u))
                return 0;
        }
    }
    session->voice_previous_rank = rank;
    return 1;
}

static int queue_race_music(PortableGpRaceSession *session, uint8_t effect,
                            PortableGpRaceAudioOperation operation)
{
    if (session->audio_event_count >= PORTABLE_GP_RACE_AUDIO_EVENT_CAPACITY)
        return 0;
    session->audio_events[session->audio_event_count++] =
        (PortableGpRaceAudioEvent){.operation = operation, .music_effect = effect};
    return 1;
}

/* Source99A4/9DAC reset simulation fields; the audio controller consumes
 * the same immutable hop events as Time Trial. */
static void tick_player_recovery_reset(PortableGpRaceSession *session)
{
    if (!session->player.source_terrain22_enabled)
        return;
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    if (session->player.hop.retrieval_started)
    {
        session->launch_revs_92 = 0u;
        player->lightning_timer_68 = 0u;
        player->hit_cue_flags_58 |= UINT16_C(0x10);
    }
    if (session->player.steering.driver_mode == 4u)
        session->launch_revs_92 = 0u;
}

/* collision.c::collide uses the sum of both source actor sizes in quarter
 * world units: abs(delta) <= (size_a + size_b) << 14.  sub_803C344 gives
 * every kart a 10,10,10 box; sub_80472C4 gives red shells 9,9,9 and
 * sub_8048054 gives bananas 4,4,4. Released shells reset each size to8.
 * World height participates in the same quarter-unit AABB. */
static int item_overlaps_racer(int32_t item_x, int32_t item_y, int32_t item_z, uint8_t item_size,
                               const PortableGpRacer *racer)
{
    const int64_t extent = (int64_t)(item_size + 10u) << 14u;
    int64_t dx = (int64_t)item_x - racer->position_x_q16;
    int64_t dy = (int64_t)item_y - racer->position_y_q16;
    int64_t dz = (int64_t)item_z - racer->world_height_q16;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    if (dz < 0)
        dz = -dz;
    return racer->contact_status_66 == 0 && racer->banana_hit_timer_38 <= 1u && dx <= extent &&
           dy <= extent && dz <= extent;
}

static int collect_red_shell_audio(PortableGpRaceSession *session, size_t owner_index,
                                   size_t victim_index)
{
    PortableGpRedShellAudioEvent event;
    PortableGpRedShellStatus actor_status;
    PortableGpRosterStatus status;
    actor_status = portable_gp_red_shell_take_contact_audio(
        portable_gp_red_actor_mutable(&session->roster, owner_index), 0u, UINT16_C(0x0100), &event);
    if (actor_status == PORTABLE_GP_RED_SHELL_OK &&
        !queue_race_audio(session, event.song_id, event.player, event.volume, 0))
        return 0;
    if (actor_status != PORTABLE_GP_RED_SHELL_OK &&
        actor_status != PORTABLE_GP_RED_SHELL_INELIGIBLE)
        return 0;
    status = portable_gp_race_roster_take_red_shell_hit_audio(&session->roster, victim_index, 0u,
                                                              UINT16_C(0x0100), &event);
    if (status == PORTABLE_GP_ROSTER_OK &&
        !queue_race_audio(session, event.song_id, event.player, event.volume, 0))
        return 0;
    return status == PORTABLE_GP_ROSTER_OK || status == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
}

static int collect_green_shell_audio(PortableGpRaceSession *session, size_t owner_index,
                                     size_t victim_index)
{
    PortableGpGreenShellAudioEvent actor_event;
    PortableGpRedShellAudioEvent victim_event;
    PortableGpGreenShellStatus actor_status;
    PortableGpRosterStatus status;
    actor_status = portable_gp_green_shell_take_contact_audio(
        portable_gp_green_actor_mutable(&session->roster, owner_index), 0u, UINT16_C(0x0100),
        &actor_event);
    if (actor_status == PORTABLE_GP_GREEN_SHELL_OK &&
        !queue_race_audio(session, actor_event.song_id, actor_event.player, actor_event.volume, 0))
        return 0;
    if (actor_status != PORTABLE_GP_GREEN_SHELL_OK &&
        actor_status != PORTABLE_GP_GREEN_SHELL_INELIGIBLE)
        return 0;
    status = portable_gp_race_roster_take_red_shell_hit_audio(&session->roster, victim_index, 0u,
                                                              UINT16_C(0x0100), &victim_event);
    if (status == PORTABLE_GP_ROSTER_OK &&
        !queue_race_audio(session, victim_event.song_id, victim_event.player, victim_event.volume,
                          0))
        return 0;
    return status == PORTABLE_GP_ROSTER_OK || status == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
}

static int collect_banana_audio(PortableGpRaceSession *session, size_t owner_index,
                                size_t victim_index)
{
    PortableGpBananaAudioEvent event;
    PortableGpBananaStatus actor_status;
    PortableGpRosterStatus status;
    actor_status = portable_gp_banana_take_contact_audio(
        portable_gp_banana_actor_mutable(&session->roster, owner_index), 0u, UINT16_C(0x0100),
        &event);
    if (actor_status == PORTABLE_GP_BANANA_OK &&
        !queue_race_audio(session, event.song_id, event.player, event.volume, event.pitch))
        return 0;
    if (actor_status != PORTABLE_GP_BANANA_OK && actor_status != PORTABLE_GP_BANANA_INELIGIBLE)
        return 0;
    status = portable_gp_race_roster_take_banana_reaction_audio(&session->roster, victim_index, 0u,
                                                                &event);
    if (status == PORTABLE_GP_ROSTER_OK &&
        !queue_race_audio(session, event.song_id, event.player, event.volume, event.pitch))
        return 0;
    if (status != PORTABLE_GP_ROSTER_OK && status != PORTABLE_GP_ROSTER_ITEM_INELIGIBLE)
        return 0;
    status = portable_gp_race_roster_take_banana_special_entry_audio(&session->roster, victim_index,
                                                                     0u, &event);
    if (status == PORTABLE_GP_ROSTER_OK &&
        !queue_race_audio(session, event.song_id, event.player, event.volume, event.pitch))
        return 0;
    if (status != PORTABLE_GP_ROSTER_OK && status != PORTABLE_GP_ROSTER_ITEM_INELIGIBLE)
        return 0;
    status = portable_gp_race_roster_take_banana_special_class_audio(&session->roster, victim_index,
                                                                     0u, &event);
    if (status == PORTABLE_GP_ROSTER_OK &&
        !queue_race_audio(session, event.song_id, event.player, event.volume, event.pitch))
        return 0;
    return status == PORTABLE_GP_ROSTER_OK || status == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
}

static void adopt_player_banana_hit(PortableGpRaceSession *session)
{
    const PortableGpRacer *r = &session->roster.racers[session->local_index];
    session->player.normal_collision_source_from_native_state = 1u;
    session->player.normal_collision_source_use_b550_table = 1u;
    session->player.normal_collision_timer_38 = r->banana_hit_timer_38;
    session->player.vector_effect.contact_word_5a = r->kart_contact_state_5a;
    session->player.steering.state_flags = r->source_flags;
    session->player.steering.response_flags = r->mushroom_response_flags_b4;
    session->player.forward_acceleration.state_flags = r->mushroom_response_flags_b4;
    session->player.hop.animation_flags = r->mushroom_response_flags_b4;
}

static int adopt_player_strong_hit(PortableGpRaceSession *session)
{
    const PortableGpRacer *victim = &session->roster.racers[session->local_index];
    session->player.normal_collision_source_from_native_state = 1u;
    session->player.normal_collision_source_use_b550_table = 1u;
    session->player.normal_collision_timer_38 = victim->banana_hit_timer_38;
    session->player.vector_effect.contact_word_5a = victim->kart_contact_state_5a;
    session->player.steering.state_flags = victim->source_flags;
    session->player.motion.component_x = session->player.pose.component_x = victim->component_x;
    session->player.motion.component_y = session->player.pose.component_y = victim->component_y;
    session->player.steering.response_flags = victim->mushroom_response_flags_b4;
    session->player.forward_acceleration.state_flags = victim->mushroom_response_flags_b4;
    session->player.hop.animation_flags = victim->mushroom_response_flags_b4;
    session->player.strong_spin_rate_b6 = victim->hit_response_b6;
    session->player.recovery_flags_74 = victim->hit_state_flags_74;
    /* Strong damage owns A9AC/8E34, instead of the normal
     * push release. Ground-entry9B38 consumes E68C's pending
     * impulse through AE18; airborne entry retains it. */
    session->player_contact_release_pending = 0u;
    session->player_contact_response_active = 0u;
    if (victim->kart_contact_state_5a != 7u && (victim->source_flags & UINT32_C(0x20)) == 0u &&
        !queue_race_audio(session, 162u, 5u, 256u, 0))
        return 0;
    return 1;
}

enum
{
    LOCAL_HIT_STRONG = 1u,
    LOCAL_HIT_BANANA = 2u,
    LOCAL_HIT_STRONG_QUIET = 3u,
    LOCAL_HIT_PUSH = 4u
};

/* The pilot in the session fields adopts an item hit written to its racer. */
static int adopt_local_hit(PortableGpRaceSession *session, uint8_t kind)
{
    const PortableGpRacer *racer = &session->roster.racers[session->local_index];
    if (kind == LOCAL_HIT_PUSH)
    {
        if (!begin_player_kart_contact_response(session))
            return 0;
        session->rival_coin_count_handoff = (uint8_t)racer->coin_count_178;
        return portable_gp_driving_quality_record_event(
                   &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_1A,
                   session->roster.race_mode_flags_7da,
                   racer->source_flags) == PORTABLE_GP_DRIVING_QUALITY_OK;
    }
    if (kind == LOCAL_HIT_BANANA)
    {
        adopt_player_banana_hit(session);
        return 1;
    }
    if (!adopt_player_strong_hit(session))
        return 0;
    if (kind == LOCAL_HIT_STRONG)
    {
        if (portable_gp_driving_quality_record_event(
                &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_1A,
                session->roster.race_mode_flags_7da,
                racer->source_flags) != PORTABLE_GP_DRIVING_QUALITY_OK)
            return 0;
        session->rival_coin_count_handoff = (uint8_t)racer->coin_count_178;
    }
    return 1;
}

/* Another local's pilot is parked; it adopts the hit on its own pass. */
static int item_hit_local(PortableGpRaceSession *session, size_t victim, uint8_t kind)
{
    if (victim == session->local_index)
        return adopt_local_hit(session, kind);
    if (victim >= PORTABLE_GP_MAX_LOCALS)
        return 0;
    session->local_hit_pending[victim] = kind;
    return 1;
}

static int tick_item_contacts_and_responses(PortableGpRaceSession *session)
{
    const int32_t *sine = session->runtime.recovered_dependencies->sine_table;
    const size_t sine_count = session->runtime.recovered_dependencies->sine_count;
    PortableGpRosterStatus status;

    /* Driver response dispatch consumes collision flags on the following
     * authoritative frame.  Advance old responses before detecting this
     * frame's new contacts. */
    for (size_t victim = 1u; victim < portable_gp_race_roster_count(&session->roster); ++victim)
    {
        /* Bound Beach CPU contacts feed the authoritative C4C0 pending-hit
         * motion pass. The legacy item loop must not also apply its spin/
         * B6 decay (or the old shell cap) before that one driver update. */
        if (session->roster.racers[victim].is_player ||
            (session->roster.racers[victim].mushroom_response_flags_b4 & UINT16_C(0x0200)) != 0u)
            continue;
        status = portable_gp_race_roster_tick_banana_hit_response(&session->roster, victim);
        if (!roster_call_succeeded(status))
            return 0;
    }
    for (size_t index = 0u; index < PORTABLE_GP_GREEN_ACTOR_SLOTS; ++index)
        if (portable_gp_green_actor(&session->roster, index)->occupied &&
            portable_gp_green_actor(&session->roster, index)->phase ==
                PORTABLE_GP_GREEN_SHELL_DESTROYING)
        {
            status = portable_gp_race_roster_tick_green_shell_destroying(&session->roster, index);
            if (!roster_call_succeeded(status))
                return 0;
        }
    for (size_t index = 0u; index < PORTABLE_GP_BANANA_ACTOR_SLOTS; ++index)
        if (portable_gp_banana_actor(&session->roster, index)->occupied &&
            portable_gp_banana_actor(&session->roster, index)->phase ==
                PORTABLE_GP_BANANA_DESTROYING)
        {
            status = portable_gp_race_roster_tick_banana_destroying(&session->roster, index);
            if (!roster_call_succeeded(status))
                return 0;
        }

    /* collision.c suppresses actors whose field 0x38 pair OR is greater than
     * one. Released shells start at eight and therefore cannot hit a kart
     * until their source countdown reaches one. */
    uint8_t green_actors[PORTABLE_GP_GREEN_ACTOR_SLOTS];
    const unsigned green_count = portable_gp_green_actor_order(&session->roster, green_actors);
    for (unsigned turn = 0u; turn < green_count; ++turn)
    {
        const size_t index = green_actors[turn];
        PortableGpGreenShell *shell = portable_gp_green_actor_mutable(&session->roster, index);
        if (!(shell->flags_34 & 2u))
            continue;
        if ((shell->phase == PORTABLE_GP_GREEN_SHELL_RELEASED ||
             shell->phase == PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING ||
             shell->phase == PORTABLE_GP_GREEN_SHELL_ACTIVE ||
             shell->phase == PORTABLE_GP_GREEN_SHELL_ORBITING) &&
            shell->lifetime_38 <= 1u)
        {
            for (size_t victim = 0u; victim < portable_gp_race_roster_count(&session->roster);
                 ++victim)
            {
                if (!item_overlaps_racer(shell->position_x_q16, shell->position_y_q16,
                                         shell->position_z_q16,
                                         (shell->phase == PORTABLE_GP_GREEN_SHELL_ACTIVE ||
                                          shell->phase == PORTABLE_GP_GREEN_SHELL_ORBITING)
                                             ? 9u
                                             : 8u,
                                         &session->roster.racers[victim]))
                    continue;
                status = portable_gp_race_roster_resolve_green_shell_kart_hit(
                    &session->roster, index, victim, 0u, 0u, sine, sine_count);
                if (status != PORTABLE_GP_ROSTER_OK)
                    return 0;
                if (session->roster.racers[victim].is_player &&
                    session->roster.racers[victim].red_shell_hit_audio_pending == 1u &&
                    !item_hit_local(session, victim, LOCAL_HIT_STRONG))
                    return 0;
                if (!collect_green_shell_audio(session, index, victim))
                    return 0;
                if ((shell->flags_34 & 2u) == 0u)
                    break;
            }
        }
    }
    uint8_t red_actors[PORTABLE_GP_RED_ACTOR_SLOTS];
    const unsigned red_count = portable_gp_red_actor_order(&session->roster, red_actors);
    for (unsigned turn = 0u; turn < red_count; ++turn)
    {
        const size_t index = red_actors[turn];
        PortableGpRedShell *shell = portable_gp_red_actor_mutable(&session->roster, index);
        if (!(shell->flags_34 & 2u))
            continue;
        if ((shell->collision_active || (shell->phase == PORTABLE_GP_RED_SHELL_ACTIVE ||
                                         shell->phase == PORTABLE_GP_RED_SHELL_ORBITING)) &&
            shell->lifetime_38 <= 1u)
        {
            for (size_t victim = 0u; victim < portable_gp_race_roster_count(&session->roster);
                 ++victim)
            {
                if (!item_overlaps_racer(shell->position_x_q16, shell->position_y_q16,
                                         shell->position_z_q16,
                                         (shell->phase <= PORTABLE_GP_RED_SHELL_ACTIVE ||
                                          shell->phase == PORTABLE_GP_RED_SHELL_ORBITING)
                                             ? 9u
                                             : 8u,
                                         &session->roster.racers[victim]))
                    continue;
                status = portable_gp_race_roster_resolve_red_shell_kart_hit(
                    &session->roster, index, victim, 0u, 0u, sine, sine_count);
                if (status != PORTABLE_GP_ROSTER_OK)
                    return 0;
                if (session->roster.racers[victim].is_player &&
                    session->roster.racers[victim].red_shell_hit_audio_pending == 1u &&
                    !item_hit_local(session, victim, LOCAL_HIT_STRONG))
                    return 0;
                if (!collect_red_shell_audio(session, index, victim))
                    return 0;
                if (!shell->collision_active || shell->lifetime_38 > 1u)
                    break;
            }
        }
    }
    uint8_t banana_actors[PORTABLE_GP_BANANA_ACTOR_SLOTS];
    const unsigned banana_count = portable_gp_banana_actor_order(&session->roster, banana_actors);
    for (unsigned turn = 0u; turn < banana_count; ++turn)
    {
        const size_t index = banana_actors[turn];
        PortableGpBanana *banana = portable_gp_banana_actor_mutable(&session->roster, index);
        if ((banana->flags_34 & 2u) && banana->collision_guard_38 <= 1u &&
            (banana->phase == PORTABLE_GP_BANANA_HELD ||
             ((banana->phase == PORTABLE_GP_BANANA_DROPPED ||
               banana->phase == PORTABLE_GP_BANANA_THROWN) &&
              banana->collision_active)))
        {
            for (size_t victim = 0u; victim < portable_gp_race_roster_count(&session->roster);
                 ++victim)
            {
                if (!item_overlaps_racer(banana->position_x_q16, banana->position_y_q16,
                                         banana->position_z_q16, 4u,
                                         &session->roster.racers[victim]))
                    continue;
                status = portable_gp_race_roster_resolve_banana_kart_hit_with_context(
                    &session->roster, index, victim, session->source_frame_clock, UINT16_C(0x0100));
                if (status != PORTABLE_GP_ROSTER_OK)
                    return 0;
                if (session->roster.racers[victim].is_player &&
                    session->roster.racers[victim].banana_hit_timer_38 == 3u &&
                    !item_hit_local(
                        session, victim,
                        (session->roster.racers[victim].mushroom_response_flags_b4 & 0x200u)
                            ? LOCAL_HIT_STRONG_QUIET
                            : LOCAL_HIT_BANANA))
                    return 0;
                if (!collect_banana_audio(session, index, victim))
                    return 0;
                if (banana->phase == PORTABLE_GP_BANANA_DESTROYING)
                    break;
            }
        }
    }
    return 1;
}

/* F450 establishes a five-unit overlap. One protected kart damages the
 * victim; two protected karts take F0E8 and the local normal-push adapter. */
static int tick_player_protection(PortableGpRaceSession *session, uint8_t accelerate_pressed)
{
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    /* 7A2C truncates immunity on water/lava recovery entry, before 7B30. */
    if (session->player.hop.mode >= 2u && session->player.hop.mode <= 5u &&
        player->lightning_status_6a > 4)
    {
        player->lightning_status_6a = 4;
        session->player.steering.response_flags &= UINT16_C(0x4000);
    }
    player->affine = session->player.affine;
    const uint16_t shrink = player->lightning_timer_68;
    if (portable_gp_race_roster_tick_lightning_timer(&session->roster, session->local_index) !=
        PORTABLE_GP_ROSTER_OK)
        return 0;
    if (shrink == 1u && (player->source_flags & UINT32_C(0x20)) == 0u &&
        !queue_race_audio(session, 133u, 6u, 256u, 0))
        return 0; /* gSongTable player6 */
    session->player.lightning_timer_68 = (int16_t)player->lightning_timer_68;
    session->player.affine = player->affine;

    const int16_t previous = player->lightning_status_6a;
    if (portable_gp_race_roster_tick_protection(&session->roster, session->local_index) !=
        PORTABLE_GP_ROSTER_OK)
        return 0;
    if (portable_gp_race_roster_tick_boo_timers(&session->roster, session->local_index,
                                                accelerate_pressed) != PORTABLE_GP_ROSTER_OK)
        return 0;
    session->player.item_lock_ac = player->item_lock_ac;
    session->player.contact_guard_66 = (uint16_t)player->contact_status_66;
    session->player.invulnerability_timer_6a = (uint16_t)player->lightning_status_6a;
    if (previous != 0 && player->lightning_status_6a == 0)
    {
        session->player.steering.response_flags &= UINT16_C(0x4000);
        session->player.forward_acceleration.state_flags &= UINT16_C(0x4000);
        session->player.steering.driver_mode = session->player.hop.mode;
    }
    return 1;
}

static int use_player_item(PortableGpRaceSession *session)
{
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    const int32_t *sine = session->runtime.recovered_dependencies->sine_table;
    const size_t sine_count = session->runtime.recovered_dependencies->sine_count;
    PortableGpRosterStatus status = PORTABLE_GP_ROSTER_ITEM_INELIGIBLE;
    uint8_t quality_event = 0u;
    /* A script sees a fresh activation from the slot, not a press that fires
     * an already orbiting or attached triple. Zero cancels and keeps it. */
    if (session->script_hooks != NULL && session->script_hooks->item_use != NULL &&
        player->item_inventory.held_item_17a != 0u && !player->item_inventory.active &&
        player->shell_item_18c == 0u && player->item_use_state_17c == 0u)
    {
        const uint16_t chosen =
            session->script_hooks->item_use(session->script_hooks->context, session->local_index,
                                            player->item_inventory.held_item_17a);
        if (chosen == 0u)
            return 1;
        if (chosen != player->item_inventory.held_item_17a)
        {
            player->item_inventory.held_item_17a = chosen;
            session->roster.item_hud.current = chosen;
            session->roster.item_hud.displayed_item = chosen;
        }
    }
    const uint16_t used_item = player->item_inventory.held_item_17a;

    switch (player->item_inventory.held_item_17a)
    {
    case PORTABLE_GP_GREEN_SHELL_ITEM:
    case PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM:
        status = portable_gp_race_roster_use_green_shell(&session->roster, session->local_index,
                                                         sine, sine_count);
        break;
    case PORTABLE_GP_RED_SHELL_ITEM:
    case PORTABLE_GP_TRIPLE_RED_SHELL_ITEM:
    case PORTABLE_GP_SPINY_SHELL_ITEM:
        status = portable_gp_race_roster_use_red_shell(&session->roster, session->local_index, sine,
                                                       sine_count);
        break;
    case PORTABLE_GP_BANANA_ITEM:
        status = portable_gp_race_roster_use_banana(&session->roster, session->local_index, sine,
                                                    sine_count);
        break;
    case UINT16_C(0x1006):
    case UINT16_C(0x2006):
        status = portable_gp_race_roster_use_reserved_item(&session->roster, session->local_index);
        break;
    case UINT16_C(0x1007):
    case UINT16_C(0x1008):
    case UINT16_C(0x1009):
    case UINT16_C(0x100e):
    case UINT16_C(0x100f):
        status = portable_gp_race_roster_use_mushroom(&session->roster, session->local_index);
        break;
    case UINT16_C(0x100a):
        status = portable_gp_race_roster_use_star(&session->roster, session->local_index);
        if (status == PORTABLE_GP_ROSTER_OK)
            session->hazard_effect_72 = 0u;
        quality_event = PORTABLE_GP_DRIVING_EVENT_15;
        break;
    case UINT16_C(0x100b):
        status = portable_gp_race_roster_use_boo(&session->roster, session->local_index);
        if (status == PORTABLE_GP_ROSTER_OK)
        {
            session->player.contact_guard_66 = (uint16_t)player->contact_status_66;
            if (!queue_race_audio(session, 129u, 6u, 256u, 0))
                return 0;
        }
        break;
    case UINT16_C(0x100c):
        status = portable_gp_race_roster_use_lightning(&session->roster, session->local_index);
        if (status == PORTABLE_GP_ROSTER_OK)
        {
            PortableGpLightningAudioEvent event;
            const PortableGpRosterStatus audio =
                portable_gp_race_roster_take_lightning_audio(&session->roster, &event);
            if (audio == PORTABLE_GP_ROSTER_OK &&
                !queue_race_audio(session, event.song_id, event.player, UINT16_C(0x100), 0))
                return 0;
            if (audio != PORTABLE_GP_ROSTER_OK && audio != PORTABLE_GP_ROSTER_ITEM_INELIGIBLE)
                return 0;
        }
        quality_event = PORTABLE_GP_DRIVING_EVENT_16;
        break;
    default:
        return 1;
    }
    if (status == PORTABLE_GP_ROSTER_OK && used_item != 0x100bu)
        portable_gp_item_hud_consume(&session->roster.item_hud);
    if (!roster_call_succeeded(status))
        return 0;
    if (status == PORTABLE_GP_ROSTER_OK && quality_event != 0u &&
        portable_gp_driving_quality_record_event(
            &session->driving_quality, (PortableGpDrivingQualityEvent)quality_event,
            session->roster.race_mode_flags_7da,
            player->source_flags) != PORTABLE_GP_DRIVING_QUALITY_OK)
        return 0;
    return 1;
}

static int tick_race_items(PortableGpRaceSession *session, uint8_t item_pressed, unsigned parts)
{
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    const int32_t *sine = session->runtime.recovered_dependencies->sine_table;
    const size_t sine_count = session->runtime.recovered_dependencies->sine_count;
    PortableGpRosterStatus status;

    if (item_pressed && (parts & TICK_LOCAL))
    {
        player->control |= UINT16_C(0x0080);
        if (!use_player_item(session))
            return 0;
    }
    if (!(parts & TICK_WORLD))
        return 1;

    const PortableDriverPilotDependencies *item_deps = session->runtime.recovered_dependencies;
    const PortableGpGreenShellWorld green_world = {item_deps->track_material, item_deps->raster,
                                                   item_deps->terrain,
                                                   item_deps->terrain_effect_tables};
    uint8_t green_actors[PORTABLE_GP_GREEN_ACTOR_SLOTS];
    const unsigned green_count = portable_gp_green_actor_order(&session->roster, green_actors);
    for (unsigned turn = 0u; turn < green_count; ++turn)
    {
        const size_t index = green_actors[turn];
        PortableGpGreenShell *shell = portable_gp_green_actor_mutable(&session->roster, index);
        switch (shell->phase)
        {
        case PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING:
        case PORTABLE_GP_GREEN_SHELL_ORBITING:
            status = portable_gp_race_roster_tick_green_shell_orbit(&session->roster, index, sine,
                                                                    sine_count);
            break;
        case PORTABLE_GP_GREEN_SHELL_ATTACHED:
            status = portable_gp_race_roster_tick_green_shell_attached(&session->roster, index,
                                                                       sine, sine_count);
            break;
        case PORTABLE_GP_GREEN_SHELL_ACTIVE:
            status = portable_gp_race_roster_tick_green_shell_held_active(&session->roster, index,
                                                                          sine, sine_count);
            break;
        case PORTABLE_GP_GREEN_SHELL_RELEASED:
        case PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING:
            status = portable_gp_race_roster_tick_green_shell_released(&session->roster, index,
                                                                       &green_world);
            break;
        default:
            continue;
        }
        if (!roster_call_succeeded(status))
            return 0;
        PortableGpGreenShellAudioEvent event;
        const uint16_t volume =
            portable_gp_item_spatial_volume(shell->position_x_q16, shell->position_y_q16,
                                            player->position_x_q16, player->position_y_q16);
        if (shell->orbit_audio_pending)
        {
            shell->orbit_audio_pending = 0u;
            if (session->roster.racers[portable_gp_green_actor_owner(&session->roster, index)]
                    .is_player &&
                !(player->source_flags & 0x20u) && !queue_race_audio(session, 122u, 4u, 256u, 0))
                return 0;
        }
        if (shell->launch_audio_pending)
        {
            shell->launch_audio_pending = 0u;
            if (!(player->source_flags & 0x20u) &&
                !queue_race_audio(session, 123u, 4u, volume < 64u ? 64u : volume, 0))
                return 0;
        }
        if (portable_gp_green_shell_take_contact_audio(
                shell, (uint8_t)((player->source_flags & 0x20u) != 0u), volume, &event) ==
                PORTABLE_GP_GREEN_SHELL_OK &&
            !queue_race_audio(session, event.song_id, event.player, event.volume, 0))
            return 0;
    }

    const PortableGpRedShellWorld red_world = {item_deps->track_material,
                                               item_deps->raster,
                                               item_deps->terrain,
                                               item_deps->terrain_effect_tables,
                                               sine,
                                               sine_count,
                                               session->engine_class,
                                               0u,
                                               0u};
    const PortableGpRedShellRoute red_route = {
        session->rules.gp_route_rows, session->rules.route_count,
        (session->rules.gp_sentinel_target_mask & 2u) ? &session->rules.gp_sentinel_targets[1]
                                                      : NULL,
        item_deps->source_track_angle_lut, item_deps->source_track_angle_lut_count};
    uint8_t red_actors[PORTABLE_GP_RED_ACTOR_SLOTS];
    const unsigned red_count = portable_gp_red_actor_order(&session->roster, red_actors);
    for (unsigned turn = 0u; turn < red_count; ++turn)
    {
        const size_t index = red_actors[turn];
        PortableGpRedShell *shell = portable_gp_red_actor_mutable(&session->roster, index);
        status = portable_gp_race_roster_tick_red_shell_actor(&session->roster, index, &red_world,
                                                              &red_route);
        if (!roster_call_succeeded(status))
        {
            portable_diagnostic_printf("course=%u mode=Mario GP red-shell owner=%zu state=%u floor=%u unsupported\n",
                    session->roster.course_id, portable_gp_red_actor_owner(&session->roster, index),
                    shell->phase, shell->floor_36);
            return 0;
        }
        const uint8_t suppressed = (uint8_t)((player->source_flags & 0x20u) != 0u);
        const uint16_t volume =
            portable_gp_item_spatial_volume(shell->position_x_q16, shell->position_y_q16,
                                            player->position_x_q16, player->position_y_q16);
        if (shell->orbit_audio_pending)
        {
            shell->orbit_audio_pending = 0u;
            if (!suppressed &&
                session->roster.racers[portable_gp_red_actor_owner(&session->roster, index)]
                    .is_player &&
                !queue_race_audio(session, 122u, 4u, 256u, 0))
                return 0;
        }
        if (shell->motion_audio_pending)
        {
            const uint16_t song = shell->motion_audio_pending;
            shell->motion_audio_pending = 0u;
            if (!suppressed && !queue_race_audio(session, song, song == 134u ? 6u : 4u,
                                                 volume < 64u ? 64u : volume, 0))
                return 0;
        }
        if (shell->flight_audio_pending)
        {
            shell->flight_audio_pending = 0u;
            const PortableSpatialAudioCamera camera = {
                .x_q16 = player->position_x_q16,
                .y_q16 = player->position_y_q16,
                .heading = session->player.steering.angle,
                .speed_q16 = session->player.motion.speed_q16,
                .racing = (uint8_t)(session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING),
                .finished = suppressed,
                .fix_heading_wrap = session->fix_doppler_wrap};
            const PortableSpatialAudioActor actor = {.x_q16 = shell->flight_audio_x_q16,
                                                     .y_q16 = shell->flight_audio_y_q16,
                                                     .heading = shell->flight_audio_heading,
                                                     .type = shell->kind_10,
                                                     .speed =
                                                         (int32_t)shell->flight_audio_speed * 16};
            int16_t pitch;
            if (!portable_spatial_audio_pitch(&camera, &actor, item_deps->spatial_pitch_table,
                                              item_deps->spatial_pitch_count, &pitch))
            {
                portable_diagnostic_printf("course=%u mode=Mario GP Spiny audio unsupported\n",
                        session->roster.course_id);
                return 0;
            }
            const uint16_t flight_volume = portable_gp_item_spatial_volume(
                actor.x_q16, actor.y_q16, camera.x_q16, camera.y_q16);
            if (!suppressed && !queue_race_audio(session, 128u, 4u, flight_volume, pitch))
                return 0;
        }
        while (shell->trap_audio_pending)
        {
            --shell->trap_audio_pending;
            if (!suppressed && !queue_race_audio(session, 127u, 7u, volume < 64u ? 64u : volume, 0))
                return 0;
        }
        PortableGpRedShellAudioEvent event;
        if (portable_gp_red_shell_take_contact_audio(shell, suppressed, volume, &event) ==
                PORTABLE_GP_RED_SHELL_OK &&
            !queue_race_audio(session, event.song_id, event.player, event.volume, 0))
            return 0;
    }

    const PortableDriverPilotDependencies *deps = session->runtime.recovered_dependencies;
    const PortableGpBananaWorld world = {.material = deps->track_material,
                                         .raster = deps->raster,
                                         .terrain = deps->terrain,
                                         .route = session->rules.route,
                                         .route_rows = session->rules.gp_route_rows,
                                         .route_row_count = session->rules.gp_route_row_count,
                                         .checkpoint_count = session->rules.route_count};
    uint8_t banana_actors[PORTABLE_GP_BANANA_ACTOR_SLOTS];
    const unsigned banana_count = portable_gp_banana_actor_order(&session->roster, banana_actors);
    for (unsigned turn = 0u; turn < banana_count; ++turn)
    {
        const size_t index = banana_actors[turn];
        PortableGpBanana *banana = portable_gp_banana_actor_mutable(&session->roster, index);
        const PortableGpRacer *banana_owner =
            &session->roster.racers[portable_gp_banana_actor_owner(&session->roster, index)];
        if (banana->phase != PORTABLE_GP_BANANA_HELD && banana->phase != PORTABLE_GP_BANANA_THROWN)
            continue;
        status =
            portable_gp_race_roster_tick_banana(&session->roster, index, &world, sine, sine_count);
        if (!roster_call_succeeded(status))
            return 0;
        PortableGpBananaAudioEvent event;
        if (portable_gp_banana_take_motion_audio(
                banana, (uint8_t)((banana_owner->source_flags & 0x20u) != 0u),
                player->position_x_q16, player->position_y_q16, &event) == PORTABLE_GP_BANANA_OK &&
            !queue_race_audio(session, event.song_id, event.player, event.volume, 0))
            return 0;
    }

    for (size_t owner = 0u; owner < portable_gp_race_roster_count(&session->roster); ++owner)
        if (portable_gp_race_roster_tick_mushroom_actors(&session->roster, owner) !=
            PORTABLE_GP_ROSTER_OK)
            return 0;

    if (session->roster.lightning_effect.occupied &&
        portable_gp_race_roster_tick_lightning_effect(&session->roster) != PORTABLE_GP_ROSTER_OK)
        return 0;
    return 1;
}

static int32_t player_item_strength_threshold(const PortableRaceRulesView *rules)
{
    return rules->item_strength_threshold;
}

/* 49A54 counts only ordinary shell actor types; mushrooms, bananas and
 * Spiny do not consume C83. D9D4's C84 sums held inventory strength. */
static int32_t battle_item_strength_threshold(const PortableGpRaceRoster *roster)
{
    int32_t threshold = 8;
    for (size_t i = 0u; i < PORTABLE_GP_GREEN_ACTOR_SLOTS; ++i)
    {
        const PortableGpGreenShell *shell = portable_gp_green_actor(roster, i);
        if (shell->occupied && (shell->kind_10 == 0x8001u || shell->kind_10 == 0x8401u))
            --threshold;
    }
    for (size_t i = 0u; i < PORTABLE_GP_RED_ACTOR_SLOTS; ++i)
    {
        const PortableGpRedShell *shell = portable_gp_red_actor(roster, i);
        if (shell->occupied && (shell->kind_10 == 0x8002u || shell->kind_10 == 0x8402u))
            --threshold;
    }
    for (size_t i = 0u; i < portable_gp_race_roster_count(roster); ++i)
        threshold -= portable_gp_item_strength(roster->racers[i].item_inventory.held_item_17a);
    return threshold;
}

static int player_roulette_selection(PortableGpRaceSession *session, uint8_t stop_pressed,
                                     uint16_t *selected)
{
    PortableGpItemInventory *inventory =
        &session->roster.racers[session->local_index].item_inventory;
    uint16_t selected_item;
    selected_item = PORTABLE_GP_ITEM_FALLBACK;
    if (inventory->active && inventory->player_owned && inventory->delay_74 <= 1 &&
        (inventory->timeline_index_78 >= PORTABLE_GP_ITEM_TIMELINE_COUNT ||
         (stop_pressed && inventory->timeline_index_78 > 2)))
    {
        const size_t leader_index = session->roster.standings[0];
        const int32_t threshold = player_item_strength_threshold(&session->rules);
        if (leader_index >= PORTABLE_GP_RACER_COUNT ||
            (threshold == 0 && session->battle_item_rules == NULL))
            return 0;
        if ((session->roster.race_mode_flags_7da & UINT16_C(0x0f00)) == UINT16_C(0x0800))
        {
            if (session->battle_item_rules == NULL)
                return 0;
            selected_item = portable_gp_item_select_battle(
                session->battle_item_rules,
                session->roster.racers[session->local_index].balloons_remaining,
                battle_item_strength_threshold(&session->roster), &session->source_rng_state);
        }
        else if (leader_index == session->local_index)
            selected_item = portable_gp_item_select_player(session->player_item_rules,
                                                           session->roster.roulette_rules, 1u, 0u,
                                                           threshold, &session->source_rng_state);
        else
        {
            const PortableGpRacer *leader = &session->roster.racers[leader_index];
            const PortableGpRacer *player = &session->roster.racers[session->local_index];
            const uint32_t route_count = (uint16_t)session->rules.route_count;
            const uint32_t leader_progress =
                route_count * leader->route_wrap_91 + leader->route_index;
            const uint32_t player_progress =
                route_count * player->route_wrap_91 + player->route_index;
            selected_item = portable_gp_item_select_player(
                session->player_item_rules, session->roster.roulette_rules, 0u,
                leader_progress - player_progress, threshold, &session->source_rng_state);
        }
    }
    *selected = selected_item;
    return 1;
}

static int tick_player_roulette(PortableGpRaceSession *session, uint8_t stop_pressed)
{
    if (!session->roster.racers[session->local_index].item_inventory.active)
        return 1;
    uint16_t selected_item;
    if (!player_roulette_selection(session, stop_pressed, &selected_item))
        return 0;
    const PortableGpRosterStatus status = portable_gp_race_roster_tick_item(
        &session->roster, session->local_index, stop_pressed, selected_item);
    return status == PORTABLE_GP_ROSTER_OK;
}

static int item_box_overlaps_position(const PortableGpItemBoxState *box, int32_t position_x_q16,
                                      int32_t position_y_q16)
{
    /* collision.c::collide combines the box's source size 0x1e with the
     * kart's size 10 in quarter-world units.  (30 + 10) << 14 gives the
     * observed 10-pixel contact bound on the natural Peach Yoshi route. */
    const int64_t extent = (int64_t)(box->placement.collision_half_extent + 10u) << 14u;
    int64_t delta_x = (int64_t)position_x_q16 - box->placement.position_x_q16;
    int64_t delta_y = (int64_t)position_y_q16 - box->placement.position_y_q16;
    if (delta_x < 0)
        delta_x = -delta_x;
    if (delta_y < 0)
        delta_y = -delta_y;
    return delta_x <= extent && delta_y <= extent;
}

static int tick_opponent_roulette(PortableGpRaceSession *session)
{
    const uint8_t battle =
        (uint8_t)((session->roster.race_mode_flags_7da & UINT16_C(0x0f00)) == UINT16_C(0x0800));
    if (battle && session->battle_item_rules == NULL)
        return 0;
    if (!battle && session->cpu_item_rules == NULL)
    {
        for (size_t i = 1u; i < portable_gp_race_roster_count(&session->roster); ++i)
            if (session->roster.racers[i].item_inventory.active)
                return 0;
        return 1;
    }
    for (size_t index = 1u; index < portable_gp_race_roster_count(&session->roster); ++index)
    {
        PortableGpRacer *racer = &session->roster.racers[index];
        if (racer->is_player || !racer->item_inventory.active)
            continue;
        if (battle)
        {
            /* Battle CPUs are a native extension. Use the same balloon odds
             * as humans, and sample only on the 30-turn callback's settlement. */
            const uint16_t selected =
                racer->item_inventory.roulette_index_70 == 0
                    ? portable_gp_item_select_battle(
                          session->battle_item_rules, racer->balloons_remaining,
                          battle_item_strength_threshold(&session->roster),
                          &session->source_rng_state)
                    : 0u;
            if (portable_gp_race_roster_tick_item(&session->roster, index, 0u, selected) !=
                PORTABLE_GP_ROSTER_OK)
                return 0;
            continue;
        }
        const uint8_t variant = racer->route_row_191 > 1u ? 2u : racer->route_row_191;
        uint16_t selected =
            session->cpu_item_rules
                ->item[variant][racer->driver_class & 7u][(racer->rank_flags_ba >> 1u) & 7u];
        int32_t cutoff = 7 - (int32_t)portable_gp_item_pool_count(&session->roster);
        for (unsigned owner = 0u; owner < 8u; ++owner)
        {
            const PortableGpRacer *other = &session->roster.racers[owner];
            cutoff -= portable_gp_item_strength(other->item_inventory.held_item_17a);
        }
        if (portable_gp_item_strength(selected) > cutoff)
            selected = 0u;
        if (portable_gp_race_roster_tick_item(&session->roster, index, 0u, selected) !=
            PORTABLE_GP_ROSTER_OK)
            return 0;
    }
    return 1;
}

static int tick_item_boxes(PortableGpRaceSession *session, uint8_t player_available,
                           int32_t player_contact_x_q16, int32_t player_contact_y_q16)
{
    const uint8_t battle =
        (uint8_t)((session->roster.race_mode_flags_7da & UINT16_C(0x0f00)) == UINT16_C(0x0800));
    if (battle && session->battle_item_rules == NULL)
        return 0;
    for (size_t index = 0u; index < session->item_box_count; ++index)
    {
        PortableGpItemBoxState *box = &session->item_boxes[index];
        if (!box->active)
        {
            if (box->cooldown_ticks != 0u)
                --box->cooldown_ticks;
            if (box->cooldown_ticks == 0u)
                box->active = 1u;
            continue;
        }
        for (size_t racer_index = 0u; racer_index < portable_gp_race_roster_count(&session->roster);
             ++racer_index)
        {
            PortableGpRacer *racer = &session->roster.racers[racer_index];
            /* Other locals take their pickup at their tick-entry position;
             * their own pass then runs the roulette and HUD. */
            const int local = racer_index == session->local_index;
            const int32_t x = local ? player_contact_x_q16 : racer->position_x_q16;
            const int32_t y = local ? player_contact_y_q16 : racer->position_y_q16;
            if (local && !player_available)
                continue;
            if (!battle && !racer->is_player && session->cpu_item_rules == NULL)
                continue;
            /* Finished racers retain motion, but source driver +0x48 bit
             * 0x20 makes them ineligible for another item-box pickup. */
            if ((racer->source_flags & UINT32_C(0x20)) != 0u)
                continue;
            if (!item_box_overlaps_position(box, x, y))
                continue;
            if (racer->is_player && session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
                !queue_race_audio(session, 121u, 6u, 256u, 0))
                return 0;
            if (racer->item_inventory.held_item_17a == PORTABLE_GP_ITEM_NONE)
            {
                /* The stream's source_parameter is the actor checkpoint ID,
                 * not its roulette seed. sub_8043860 initializes +0x4c to
                 * zero; sub_8044384 reads that field for the first preview. */
                if (portable_gp_race_roster_begin_item_box(&session->roster, racer_index,
                                                           box->placement.kind,
                                                           0u) != PORTABLE_GP_ROSTER_OK)
                    return 0;
                if (racer->is_player && !queue_race_audio(session, 103u, 7u, 256u, 0))
                    return 0;
                /* The player roulette actor is updated later on the same
                 * source frame: frame 1788 already has +0x74 == 1 after its
                 * constructor installed 2. Opponent delay is separate. */
                if (local && portable_gp_race_roster_tick_item(&session->roster, racer_index, 0u,
                                                               PORTABLE_GP_ITEM_FALLBACK) !=
                                 PORTABLE_GP_ROSTER_OK)
                    return 0;
            }
            else if (local && portable_gp_driving_quality_record_event(
                                  &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_24,
                                  session->roster.race_mode_flags_7da,
                                  racer->source_flags) != PORTABLE_GP_DRIVING_QUALITY_OK)
                return 0;
            box->active = 0u;
            box->cooldown_ticks = battle ? (uint16_t)(PORTABLE_GP_ITEM_BOX_RESPAWN_TICKS - 2u +
                                                      session->battle_item_rules->box_hidden_ticks)
                                         : PORTABLE_GP_ITEM_BOX_RESPAWN_TICKS;
            ++session->item_box_contacts;
            if (!racer->is_player)
                ++session->opponent_item_box_contacts;
            break;
        }
        if (!box->active)
            break;
    }
    return 1;
}

/* CPU B2B4/9B38 uses each course's material map and shared jump rules. */
/* Native Battle navigation chooses intent only. BB48 steering and BD24's
 * acceleration rows still own motion, and terrain/contact recovery wins. */
static int tick_battle_navigation(PortableGpRaceSession *session)
{
    if (!session->player.battle_enabled || session->battle_item_rules == NULL ||
        session->battle_item_rules->ai.cruise_speed == 0u)
        return 1;
    const PortableBattleAiRules *rules = &session->battle_item_rules->ai;
    const PortableDriverPilotDependencies *deps = &session->player_dependencies;
    for (size_t owner = 1u; owner < portable_gp_race_roster_count(&session->roster); ++owner)
    {
        PortableGpRacer *r = &session->roster.racers[owner];
        if (r->is_player || r->lightning_state_78 > 1u || (r->source_flags & 0x20u) != 0u)
            continue;
        const PortableBattleNavPoint start = {r->position_x_q16 / 65536, r->position_y_q16 / 65536};
        if (deps->terrain == NULL || start.x < 0 || start.y < 0 ||
            start.x >= (int32_t)deps->terrain->width * 8 ||
            start.y >= (int32_t)deps->terrain->height * 8)
            continue; /* Source terrain recovery owns out-of-bounds entries. */
        if (!r->battle_navigation_valid || (session->source_frame_clock & 7u) == owner)
        {
            PortableBattleNavPoint goals[PORTABLE_BATTLE_NAV_GOALS];
            size_t count = 0u;
            if (!r->elimination.revenge_bomb && r->item_inventory.held_item_17a == 0u &&
                r->shell_item_18c == 0u && r->item_use_state_17c == 0u && !r->item_inventory.active)
            {
                for (size_t i = 0u; i < session->item_box_count; ++i)
                {
                    const PortableGpItemBoxState *box = &session->item_boxes[i];
                    if (!(box->active))
                        continue;
                    if (count >= PORTABLE_BATTLE_NAV_GOALS)
                        return 0;
                    goals[count++] =
                        (PortableBattleNavPoint){box->placement.position_x_q16 / 65536,
                                                 box->placement.position_y_q16 / 65536};
                }
            }
            PortableBattleNavRoute route = {0};
            if (count != 0u && !portable_battle_navigation_route(
                                   &session->battle_navigation, deps->terrain, deps->track_material,
                                   rules, start, goals, count, &route))
                return 0;
            if (!route.reachable)
            {
                count = 0u;
                for (size_t i = 0u; i < portable_gp_race_roster_count(&session->roster); ++i)
                {
                    const PortableGpRacer *other = &session->roster.racers[i];
                    if (i == owner || other->balloons_remaining == 0u ||
                        other->lightning_state_78 > 1u)
                        continue;
                    goals[count++] = (PortableBattleNavPoint){other->position_x_q16 / 65536,
                                                              other->position_y_q16 / 65536};
                }
                if (count != 0u && !portable_battle_navigation_route(
                                       &session->battle_navigation, deps->terrain,
                                       deps->track_material, rules, start, goals, count, &route))
                    return 0;
            }
            r->battle_navigation_valid = 1u;
            r->battle_waypoint_x = (int16_t)(route.reachable ? route.waypoint.x : start.x);
            r->battle_waypoint_y = (int16_t)(route.reachable ? route.waypoint.y : start.y);
        }
        uint16_t angle;
        if (portable_driver_vector_effect_source_angle((int16_t)(r->battle_waypoint_x - start.x),
                                                       (int16_t)(r->battle_waypoint_y - start.y),
                                                       &angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
            return 0;
        const uint16_t difference = (uint16_t)(angle - r->heading);
        const unsigned turn = difference < 0x8000u ? difference : 0x10000u - difference;
        r->battle_speed_target = (uint16_t)(turn >= rules->stop_angle   ? 0u
                                            : turn >= rules->turn_angle ? rules->turn_speed
                                                                        : rules->cruise_speed);
        if (r->battle_waypoint_x == start.x && r->battle_waypoint_y == start.y)
            r->battle_speed_target = 0u;
    }
    return 1;
}

static int adopt_local_lightning(PortableGpRaceSession *session)
{
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    player->lightning_hit_pending = 0u;
    session->player.steering.response_flags = player->mushroom_response_flags_b4;
    session->player.forward_acceleration.state_flags = player->mushroom_response_flags_b4;
    session->player.hop.animation_flags = player->mushroom_response_flags_b4;
    session->player.strong_spin_rate_b6 = player->hit_response_b6;
    session->player.recovery_flags_74 = player->hit_state_flags_74;
    session->player.motion.speed_q16 = player->speed_q16;
    return portable_gp_driving_quality_record_event(
               &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_0A,
               session->roster.race_mode_flags_7da,
               player->source_flags) == PORTABLE_GP_DRIVING_QUALITY_OK;
}

static PortableGpRosterStatus request_cpu_contact_art(void *context, PortableGpRaceRoster *roster,
                                                      size_t actor)
{
    PortableGpRaceSession *session = context;
    if (roster != &session->roster)
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    return request_contact_art_for(context, actor) ? PORTABLE_GP_ROSTER_OK
                                                   : PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
}

static PortableGpRosterStatus tick_rivals(PortableGpRaceSession *session)
{
    const PortableDriverPilotDependencies *deps = session->runtime.recovered_dependencies;
    const int terrain = deps->terrain != NULL && deps->track_material != NULL;
    PortableRecoveredCoin coins[PORTABLE_RECOVERED_COIN_CAPACITY];
    size_t active_coins = 0u;
    if (terrain)
        for (size_t coin = 0u; coin < session->coin_count; ++coin)
            if (session->coins[coin].active)
                coins[active_coins++] = session->coins[coin].placement;
    session->roster.cpu_items_enabled = (uint8_t)!session->quick_run_options.items_off;
    session->roster.battle_combat = session->battle_item_rules != NULL
                                        ? session->battle_item_rules->combat
                                        : (PortableBattleCombatRules){0};
    session->roster.cpu_banana_release_valid =
        session->cpu_item_rules != NULL && session->cpu_item_rules->banana_release_valid;
    if (session->roster.cpu_banana_release_valid)
        memcpy(session->roster.cpu_banana_release, session->cpu_item_rules->banana_release, 8u);
    session->roster.cpu_green_release_valid =
        session->cpu_item_rules != NULL && session->cpu_item_rules->green_release_valid;
    if (session->roster.cpu_green_release_valid)
        memcpy(session->roster.cpu_green_release, session->cpu_item_rules->green_release, 8u);
    session->roster.cpu_red_release_valid =
        session->cpu_item_rules != NULL && session->cpu_item_rules->red_release_valid;
    if (session->roster.cpu_red_release_valid)
        memcpy(session->roster.cpu_red_release, session->cpu_item_rules->red_release, 8u);
    session->roster.cpu_item_engine = session->engine_class;
    session->roster.cpu_item_clock = session->source_frame_clock;
    uint16_t prior_progress[PORTABLE_GP_OPPONENT_COUNT];
    for (unsigned i = 0u; i < PORTABLE_GP_OPPONENT_COUNT; ++i)
        prior_progress[i] = session->roster.racers[i + 1u].max_progress_c6;
    if (!tick_battle_navigation(session))
        return PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED;
    const uint32_t hits = session->roster.lightning_effect_events;
    session->roster.piranhas = &session->piranhas;
    const PortableGpRosterStatus result = portable_gp_race_roster_tick_initial_all_terrain(
        &session->roster, &session->rules, deps->raster, deps->sine_table, deps->sine_count,
        terrain ? deps->terrain : NULL, terrain ? deps->track_material : NULL,
        terrain ? coins : NULL, active_coins, deps->ramp_speed_limit_q16,
        deps->source_track_angle_lut, deps->source_track_angle_lut_count, &session->course_props,
        request_cpu_contact_art, session);
    session->roster.piranhas = NULL; /* borrowed for this tick only */
    if (result != PORTABLE_GP_ROSTER_OK)
        return result;
    for (unsigned i = 0u; i < PORTABLE_GP_OPPONENT_COUNT; ++i)
    {
        const PortableGpRacer *r = &session->roster.racers[i + 1u];
        const unsigned wrap = r->max_progress_c6 >> 8u;
        /* B7B8 calls DCF0 at the penultimate/finish markers, and at earlier
         * lap markers for bit8000. ROM DCF0 updates every racer's timestamp;
         * the source C transcription incorrectly makes that player-only. */
        if (r->max_progress_c6 > prior_progress[i] && wrap >= 0x81u && wrap <= 0x85u &&
            (wrap + 1u >= 0x80u + session->rules.lap_count ||
             (r->source_flags & UINT32_C(0x8000)) != 0u))
        {
            const uint32_t now = session->source_result_clock.centiseconds;
            const uint32_t elapsed = now - session->rival_lap_timestamp[i];
            session->rival_lap_centiseconds[i][wrap - 0x81u] =
                elapsed > UINT32_C(0xea5f) ? UINT16_C(0xea5f) : (uint16_t)elapsed;
            session->rival_lap_timestamp[i] = now;
        }
    }

    /* Locals adopt their own Lightning hits on their passes. */
    if (!multi_local(session) && session->roster.lightning_effect_events != hits &&
        !adopt_local_lightning(session))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    PortableGpLightningAudioEvent event;
    const PortableGpRosterStatus audio =
        portable_gp_race_roster_take_lightning_audio(&session->roster, &event);
    if (audio == PORTABLE_GP_ROSTER_OK &&
        !queue_race_audio(session, event.song_id, event.player, 256u, 0))
        return PORTABLE_GP_ROSTER_INVALID_ARGUMENT;
    return audio == PORTABLE_GP_ROSTER_OK || audio == PORTABLE_GP_ROSTER_ITEM_INELIGIBLE
               ? PORTABLE_GP_ROSTER_OK
               : audio;
}

/* 32798 writes an 8-unit tile in the owned course terrain. Rendering keeps
 * its own map, but simulation must see C0 before pickup and C1 afterward. */
static int coin_terrain_cell(const PortableDriverPilotDependencies *deps,
                             const PortableRecoveredCoin *coin, size_t *cell)
{
    const PortableTerrainView *terrain = deps->terrain;
    if (!terrain || !terrain->cells || terrain->row_stride < terrain->width ||
        coin->position_x_q16 < 0 || coin->position_y_q16 < 0)
        return 0;
    const size_t x = (uint32_t)coin->position_x_q16 >> 19u;
    const size_t y = (uint32_t)coin->position_y_q16 >> 19u;
    if (x >= terrain->width || y >= terrain->height || y > (SIZE_MAX - x) / terrain->row_stride)
        return 0;
    *cell = y * terrain->row_stride + x;
    return !deps->owned_terrain_cells ||
           (terrain->cells == deps->owned_terrain_cells && *cell < deps->owned_terrain_capacity);
}

static int tick_player_coins(PortableGpRaceSession *session, uint8_t ground_mode_at_entry)
{
    /* 9B38's mode0/C0 branch rejects CPU flag2000 and finished flag20.
     * A landing still used the airborne branch on this turn. At99 the coin
     * remains available: the source clamps the count without writing C1. */
    if (ground_mode_at_entry != 0u || session->player.hop.mode != 0u ||
        session->player.pose.position_z > INT32_C(0x40000) ||
        (session->player.steering.state_flags & UINT32_C(0x2020)) != 0u ||
        session->roster.racers[session->local_index].coin_count_178 >= 99u)
        return 1;
    const uint32_t tile_x = session->player.pose.position_x < 0
                                ? UINT32_MAX
                                : (uint32_t)session->player.pose.position_x >> 19u;
    const uint32_t tile_y = session->player.pose.position_y < 0
                                ? UINT32_MAX
                                : (uint32_t)session->player.pose.position_y >> 19u;

    for (size_t index = 0u; index < session->coin_count; ++index)
    {
        PortableGpCoinState *coin = &session->coins[index];
        /* Source sub_8032798 installs +0x90 coins in 8-unit track cells. */
        if (!coin->active || tile_x != ((uint32_t)coin->placement.position_x_q16 >> 19u) ||
            tile_y != ((uint32_t)coin->placement.position_y_q16 >> 19u))
            continue;
        size_t cell;
        if (!coin_terrain_cell(&session->player_dependencies, &coin->placement, &cell))
            return 0;
        uint8_t *cells = session->player_dependencies.owned_terrain_cells;
        if (cells && cells[cell] != UINT8_C(0xc0))
            continue;
        if (portable_gp_race_roster_collect_coin(&session->roster, session->local_index) !=
            PORTABLE_GP_ROSTER_OK)
            return 0;
        if (cells)
            cells[cell] = UINT8_C(0xc1);
        /* Repeated placement records install one C0 cell, not extra coins. */
        for (size_t other = index; other < session->coin_count; ++other)
            if (((uint32_t)session->coins[other].placement.position_x_q16 >> 19u) == tile_x &&
                ((uint32_t)session->coins[other].placement.position_y_q16 >> 19u) == tile_y)
                session->coins[other].active = 0u;
        unsigned slot = 0u;
        while (slot < PORTABLE_COIN_PICKUP_EFFECT_CAPACITY && session->coin_pickups[slot].age)
            ++slot;
        if (slot == PORTABLE_COIN_PICKUP_EFFECT_CAPACITY)
            return 0;
        portable_coin_pickup_effect_start(&session->coin_pickups[slot], 120,
                                          (int16_t)(140 - session->player.pose.position_z / 65536),
                                          session->source_frame_clock);
        if (!queue_race_audio(session, 101u, 7u, 256u, 0))
            return 0;
        break; /* one map cell is dispatched per driver turn */
    }
    return 1;
}

static void clear_player_components(PortableGpRaceSession *session)
{
    session->player.motion.component_x = 0;
    session->player.motion.component_y = 0;
    session->player.pose.component_x = 0;
    session->player.pose.component_y = 0;
}

static int begin_player_water_recovery(PortableGpRaceSession *session)
{
    PortableRaceSession *race = &session->runtime.session;
    const size_t target_index =
        (race->start_route_index + race->route_count - 1u) % race->route_count;
    const PortableRacePoint target = race->route[target_index];

    if (race->compatibility.course_id != 25u || race->phase != PORTABLE_RACE_PHASE_RACING ||
        session->water_recovery.phase != PORTABLE_WATER_RECOVERY_NONE ||
        !portable_water_recovery_begin(&session->water_recovery, session->player.pose.position_x,
                                       session->player.pose.position_y, target.x * INT32_C(65536),
                                       target.y * INT32_C(65536), race->start_heading))
        return 0;
    /* Preserve the qualified Shy source checkpoint rewind shared by Time
       Trial and GP. The GP grid changes start_route_index, not this rule. */
    if (race->progress_kind == PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1 &&
        race->laps_completed == 0u && race->checkpoint_raw == 0u && race->lap_raw == UINT8_C(0x80))
    {
        race->checkpoint_raw = race->start_checkpoint_raw;
        race->checkpoint_next_index = 0u;
        race->lap_raw = UINT8_C(0x7f);
        race->max_progress_raw = (uint16_t)((UINT16_C(0x7f) << 8) | race->start_checkpoint_raw);
        race->finish_armed = 0u;
    }
    race->next_route_index = race->start_route_index;
    race->rescue_x_q16 = target.x * INT32_C(65536);
    race->rescue_y_q16 = target.y * INT32_C(65536);
    race->rescue_heading = race->start_heading;
    ++race->rescue_count;
    session->player.steering.driver_mode = 0u;
    session->player.steering.exit_timer = 0u;
    session->player.steering.response_flags &= UINT16_C(0xff3f);
    session->player.forward_acceleration.state_flags &= UINT16_C(0xff3f);
    session->player.hop.animation_flags &= UINT16_C(0xff3f);
    session->previous_semantic_buttons = 0u;
    clear_player_components(session);
    return 1;
}

static PortableGpRaceSessionStatus
tick_player_water_recovery(PortableGpRaceSession *session, PortableRaceTickDisposition *disposition)
{
    PortableRaceMotionObservation motion = {0};

    *disposition = portable_race_session_begin_tick(&session->runtime.session);
    if (*disposition != PORTABLE_RACE_TICK_ADVANCE_MOTION)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    motion.old_x_q16 = session->player.pose.position_x;
    motion.old_y_q16 = session->player.pose.position_y;
    if (!tick_player_protection(session, 0u))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    if (!portable_driver_water_recovery_tick(&session->water_recovery, &session->player,
                                             &session->player_dependencies))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    motion.new_x_q16 = session->player.pose.position_x;
    motion.new_y_q16 = session->player.pose.position_y;
    motion.track_cell = 0u;
    motion.track_cell_valid = 0u;
    motion.drivable = 0u;
    if (!portable_race_session_commit_motion(&session->runtime.session, &motion))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    session->runtime.water_entry = 0u;
    session->runtime.boundary_exit = 0u;
    sync_player(session);
    if (!request_contact_art_for(session, session->local_index))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    const int single = !multi_local(session);
    if (!tick_player_roulette(session, 0u) || !tick_race_items(session, 0u, tick_parts(session)) ||
        (single &&
         (!tick_opponent_roulette(session) || !tick_item_contacts_and_responses(session) ||
          !tick_item_boxes(session, 0u, 0, 0))))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    /* Recovery carries the player outside ordinary kart/item-box collision,
       while the other seven racers continue their authoritative GP ticks. */
    if (single && tick_rivals(session) != PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    session->player.steering.state_flags |=
        session->roster.racers[session->local_index].source_flags & UINT32_C(0x000c);
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus
portable_gp_race_session_begin_player_item_box(PortableGpRaceSession *session,
                                               uint16_t roulette_seed)
{
    if (session == NULL)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    return portable_gp_race_roster_begin_item_box(&session->roster, session->local_index,
                                                  PORTABLE_GP_ITEM_BOX_KIND,
                                                  roulette_seed) == PORTABLE_GP_ROSTER_OK
               ? PORTABLE_GP_RACE_SESSION_OK
               : PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
}

PortableGpRaceSessionStatus
portable_gp_race_session_tick_player_item(PortableGpRaceSession *session, uint8_t stop_pressed,
                                          uint16_t selected_item)
{
    if (session == NULL)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    return portable_gp_race_roster_tick_item(&session->roster, session->local_index, stop_pressed,
                                             selected_item) == PORTABLE_GP_ROSTER_OK
               ? PORTABLE_GP_RACE_SESSION_OK
               : PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
}

static PortableAmbientInputs ambient_inputs(const PortableGpRaceSession *session, uint32_t clock,
                                            const PortableSkyMotion *before)
{
    const PortableGpRacer *leader = &session->roster.racers[session->roster.standings[0]];
    return (PortableAmbientInputs){.clock = clock,
                                   .selected_flags = session->player.steering.state_flags,
                                   .selected_speed_q16 = session->player.motion.speed_q16,
                                   .selected_base_speed_q16 = session->player.input.base_speed_q16,
                                   .selected_progress = session->runtime.session.max_progress_raw,
                                   .leader_progress = leader->max_progress_c6,
                                   .leader_route_wrap = leader->route_wrap_91,
                                   .race_phase_7d4 = session->roster.race_phase_7d4,
                                   .scene_mode_80d = session->roster.lightning_state_80d,
                                   .sky_phase_before = before->phase,
                                   .sky_phase_after = session->sky_motion.phase,
                                   .sky_elapsed_before = before->elapsed,
                                   .sky_counter_before = before->transition_counter,
                                   .source_gate =
                                       (uint8_t)((session->roster.race_phase_7d4 & 0x40u) != 0u)};
}

static int tick_ambient(PortableGpRaceSession *session, uint8_t stage, uint32_t clock,
                        const PortableSkyMotion *before)
{
    const PortableAmbientInputs inputs = ambient_inputs(session, clock, before);
    return portable_ambient_tick(&session->ambient, session->ambient_rules, stage, &inputs,
                                 session->player_dependencies.sine_table,
                                 session->player_dependencies.sine_count) >= 0;
}

/* A4D8/31E14 resolves the skyline from entry poses before contact and
 * driver motion. Each local owns its camera and persistent motion controller. */
static int tick_sky_motion(PortableGpRaceSession *session, uint32_t clock, int initialize)
{
    const PortableSkyMotion before = session->sky_motion;
    const unsigned leader = session->roster.standings[0];
    if (leader >= PORTABLE_GP_RACER_COUNT)
        return 0;
    const PortableGpRacer *front = &session->roster.racers[leader];
    const PortableSkyMotionInputs inputs = {
        .clock = clock,
        .yaw = race_camera_yaw(session),
        .vertical_offset =
            session->finish_camera.snapshot.active
                ? session->finish_camera.snapshot.vertical_offset
                : portable_sky_motion_camera_offset(session->player.pose.position_z,
                                                    session->player.source_material_85,
                                                    session->player.art_motion.bob_phase),
        .vertical_offset_q16 = session->finish_camera.snapshot.active
            ? (int32_t)session->finish_camera.snapshot.vertical_offset * 65536
            : portable_sky_motion_camera_offset_q16(session->player.pose.position_z,
                session->player.source_material_85, session->player.art_motion.bob_phase),
        .precise_vertical_offset = 1u,
        .selected_progress = session->runtime.session.max_progress_raw,
        .leader_progress = front->max_progress_c6,
        .leader_route_index = front->route_index,
        .leader_route_wrap = front->route_wrap_91,
        .scene_flags_7fe = session->roster.race_mode_flags_7da,
        .source_gate = (uint8_t)((session->roster.race_phase_7d4 & 0x40u) != 0u),
        .lightning_phase =
            (uint8_t)(session->roster.lightning_state_80d == 2u &&
                              session->roster.lightning_duration_c82 >
                                  portable_item_status_flash_end(session->roster.item_status_rules)
                          ? 2u
                          : 0u)};
    if ((initialize ? portable_sky_motion_init : portable_sky_motion_tick)(
            &session->sky_motion, session->sky_motion_rules, &inputs,
            session->player_dependencies.sine_table, session->player_dependencies.sine_count) < 0)
        return 0;
    return initialize || session->local_index != 0u ||
           tick_ambient(session, PORTABLE_AMBIENT_PRE_PROFILE, clock, &before);
}

static PortableGpRaceSessionStatus init_engine_grid_order(
    PortableGpRaceSession *session, const PortableRecoveredRaceView *race,
    uint8_t selected_driver_class, const uint8_t rank_by_racer[8], uint8_t engine_class,
    int preserve_locals)
{
    static const uint8_t first_race_ranks[8] = {7u, 6u, 5u, 4u, 3u, 2u, 1u, 0u};
    static const uint8_t arena_ranks[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    uint8_t start_checkpoint_raw;
    if (session == NULL || race == NULL)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    const uint8_t battle =
        (uint8_t)(race->rules.progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1);
    const uint8_t *input_ranks = rank_by_racer != NULL ? rank_by_racer
                           : battle              ? arena_ranks
                                                 : first_race_ranks;
    if (battle && (race->rules.lap_count != 0u || race->rules.finish_kind != PORTABLE_FINISH_NONE ||
                   race->gp_grid == NULL || race->battle_items == NULL))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (race->rules.compatibility.course_id == 0u ||
        selected_driver_class >= PORTABLE_GP_RACER_COUNT ||
        race->rules.compatibility.character_class != selected_driver_class)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if ((race->gp_grid == NULL) != (race->gp_grid_count == 0u) ||
        (race->gp_grid != NULL && race->gp_grid_count != (battle ? 4u : 8u)) ||
        (rank_by_racer != NULL && race->gp_grid == NULL) || input_ranks[0] >= PORTABLE_GP_RACER_COUNT)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;

    if (!portable_mushroom_rules_valid(race->dependencies.mushroom_rules) ||
        !portable_item_status_rules_valid(race->dependencies.item_status_rules) ||
        !portable_boo_rules_valid(race->dependencies.boo_rules) ||
        !portable_green_shell_rules_valid(race->dependencies.green_shell_rules) ||
        !portable_red_shell_rules_valid(race->dependencies.red_shell_rules) ||
        !portable_banana_rules_valid(race->dependencies.banana_rules) ||
        !portable_shell_orbit_rules_valid(race->dependencies.shell_orbit_rules))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (engine_class > 2u || race->gp_engines == NULL || race->gp_engine_count != 3u ||
        race->dependencies.forward_target_response_table == NULL ||
        race->dependencies.forward_target_response_count != 8u)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    const PortableGpEngineProfile *profile = race->gp_engines != NULL && race->gp_engine_count == 3u
                                                 ? &race->gp_engines[engine_class]
                                                 : NULL;
    /* A caller may supply its existing roster order as the new grid. */
    uint8_t ranks[PORTABLE_GP_RACER_COUNT];
    memcpy(ranks, input_ranks, sizeof ranks);
    /* Secondary local pilots park in the trailing owner array while the
     * shared constructor initializes the session pilot in the same destination. */
    memset(session, 0, preserve_locals ? offsetof(PortableGpRaceSession, locals) : sizeof *session);
    session->engine_class = engine_class;
    /* The rev profile belongs to imported engine data. Simulation-only legacy
     * packs omit that owner rather than carrying extracted table defaults. */
    if (race->engine_sound_rules != NULL)
        session->engine_rev_profile = race->engine_sound_rules->profiles[engine_class];
    session->retry_credits = race->gp_scoring != NULL ? race->gp_scoring->initial_retry_credits : 3u;
    if (session->retry_credits > 3u)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (race->gp_cpu_items != NULL && race->gp_cpu_item_engine_count == 3u)
        session->cpu_item_rules = &race->gp_cpu_items[engine_class];
    session->scoring_rules = race->gp_scoring;
    session->player_item_rules = race->gp_player_items;
    session->battle_item_rules = race->battle_items;
    session->rules = race->rules;
    if (profile != NULL)
        session->rules.cpu_tuning = &profile->cpu;
    if (race->gp_grid != NULL)
    {
        const PortableGpGridRow *row = &race->gp_grid[ranks[0]];
        if (row->heading > UINT16_MAX)
            return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
        session->rules.start_x_q16 = row->x_q16;
        session->rules.start_y_q16 = row->y_q16;
        session->rules.start_heading = (uint16_t)row->heading;
    }
    else
    {
        session->rules.start_x_q16 =
            (int32_t)((uint32_t)race->rules.start_x_q16 + (PORTABLE_GP_GRID_COLUMNS_OFFSET << 16u));
        session->rules.start_y_q16 =
            (int32_t)((uint32_t)race->rules.start_y_q16 +
                      ((PORTABLE_GP_GRID_FIRST_ROW_OFFSET +
                        PORTABLE_GP_GRID_ROW_SPACING * PORTABLE_GP_PLAYER_GRID_RANK)
                       << 16u));
    }
    if (race->dependencies.raster == NULL ||
        portable_track_raster_query16(race->dependencies.raster, session->rules.start_x_q16 >> 16u,
                                      session->rules.start_y_q16 >> 16u,
                                      &start_checkpoint_raw) != PORTABLE_TRACK_RASTER_OK)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    session->rules.start_route_index = (uint8_t)(start_checkpoint_raw & UINT8_C(0x7f));
    session->rules.start_checkpoint_raw = start_checkpoint_raw;
    if (!battle)
    {
        session->rules.progress_kind = PORTABLE_RACE_PROGRESS_SOURCE_RASTER_V1;
        session->rules.finish_kind = PORTABLE_FINISH_SOURCE_Y_CROSSING;
    }

    if (!portable_course_props_init_with_random(&session->course_props, race->course_props,
                                                race->course_prop_count, main_visual_random,
                                                &session->main_visual_rng_state))
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (!portable_lava_bubble_course_init(&session->lava_bubbles, race->lava_bubbles,
                                          race->lava_bubble_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_thwomp_course_init(&session->thwomps, race->thwomps, race->thwomp_count))
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    session->player = race->initial_driver;
    session->player.impact_selected = 1u;
    session->player.impact_x = 120;
    session->player.impact_y = 140;
    session->player.impact_depth = 256;
    session->sky_motion_rules = race->sky_motion_rules;
    session->view_rules = race->view_rules;
    session->finish_rules = race->finish_rules;
    session->weather_profile = race->weather_profile;
    session->recovery_fx_rules = race->recovery_fx_rules;
    session->coin_effect_rules = race->coin_effect_rules;
    session->coin_birth_x = 120;
    session->coin_birth_y = 140;
    session->coin_birth_inverse = 256;
    session->surface_trail_rules = race->surface_trail_rules;
    session->icy_recovery = (uint8_t)(race->icy_recovery && race->recovery_fx_rules != NULL);
    session->ambient_rules = race->ambient_rules;
    if (race->ambient_seed != NULL)
        session->ambient_seed = *race->ambient_seed;
    if (portable_ambient_init(&session->ambient, session->ambient_rules, &session->ambient_seed) < 0)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    session->recovery_bob_offsets = race->recovery_bob_offsets;
    session->finish_lakitu_rules = race->finish_lakitu_rules;
    if (session->recovery_bob_offsets != NULL)
    {
        session->player.source_hazard_enabled = 1u;
        session->player.source_terrain22_enabled = 1u;
    }
    /* C1BC/BF34 initialize contact34 through independent bit2/bit1 writers.
     * Recovery A800 later ORs these bits rather than installing a capture. */
    session->player.recovery_contact_34 |= UINT16_C(3);
    session->source_scene_row_c88 = 0; /* 48C74's source scene reset */
    /* The fresh GP grid's global frame counter is six ticks ahead of its
     * native replay. The ready-grid handoff below supplies its distinct
     * phase. sub_803DA54 uses this counter to select a racer. */
    session->source_frame_clock = 6u;
    if (!portable_gp_source_clock_init(&session->source_result_clock, (uint8_t)session->rules.lap_count))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    session->player.pose.position_x = session->rules.start_x_q16;
    session->player.pose.position_y = session->rules.start_y_q16;
    session->player.motion.angle = (int16_t)session->rules.start_heading;
    session->player.motion.speed_q16 = 0;
    session->player.motion.acceleration_q16 = 0;
    session->player.motion.component_x = 0;
    session->player.motion.component_y = 0;
    /* The source player begins GP with +0x48 bit 0x8000 set. B550 keeps
     * this bit while adding the temporary +0x4000 boundary hold. Source
     * +0xb4 is a separate animation/response field and starts at zero. */
    session->player.steering.state_flags |= UINT32_C(0x8000);
    if (!portable_crab_course_init(&session->crabs, race->crabs, race->crab_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_seagull_course_init(&session->seagulls, race->seagulls, race->seagull_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_cannonball_course_init(&session->cannonballs, race->cannonballs,
                                         race->cannonball_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_piranha_course_init(&session->piranhas, race->piranhas, race->piranha_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_mouse_course_init(&session->mice, race->mice, race->mouse_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_thundercloud_course_init(&session->thunderclouds, race->thunderclouds,
                                           race->thundercloud_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_falling_star_course_init(&session->falling_stars, race->falling_stars,
                                           race->falling_star_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_snowman_course_init(&session->snowmen, race->snowmen, race->snowman_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_penguin_course_init(&session->penguins, race->penguins, race->penguin_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!portable_boo_course_init(&session->boos, race->boos, race->boo_count))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    session->player.source_update_order = 1u;
    session->player.battle_enabled = (uint8_t)battle;
    session->player.battle_scene_state = battle ? 3u : 0u;
    session->player.preserve_source_base_speed = 1u;
    session->player.input.base_speed_q16 = profile->player_base_speed_q16;
    session->player.allow_track_boundary = 1u;

    session->player_dependencies = race->dependencies;
    if (race->gp_forward_local_table == NULL || race->gp_forward_local_count < 16u)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    session->player_dependencies.forward_local_table =
        engine_class == 2u ? race->dependencies.forward_local_table : race->gp_forward_local_table;
    session->player_dependencies.forward_local_count = race->gp_forward_local_count;
    session->player_dependencies.forward_limit_thresholds = profile->player_forward_limits;
    session->player_dependencies.forward_limit_threshold_count = 16u;
    if (profile != NULL)
    {
        session->player_dependencies.source_angle_target_table = profile->angle_targets;
        session->player_dependencies.source_angle_target_table_count = sizeof profile->angle_targets;
    }
    session->player_dependencies.forward_target_response_table =
        race->dependencies.forward_target_response_table;
    session->player_dependencies.forward_target_response_count = 8u;

    if (portable_race_runtime_init_recovered(&session->runtime, &session->rules, &session->player,
                                             &session->player_dependencies) != PORTABLE_RACE_RUNTIME_OK)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    portable_gp_driving_quality_init(&session->driving_quality);
    portable_gp_driving_quality_begin_race(&session->driving_quality);
    /* Roster grid rows are relative to the recovered course start, while the
     * runtime player start above is the selected rank-7 row. */
    {
        PortableRaceRulesView grid_rules = race->rules;
        grid_rules.cpu_tuning = session->rules.cpu_tuning;
        grid_rules.start_route_index = session->rules.start_route_index;
        if (portable_gp_race_roster_init_for_rules(
                &session->roster, &grid_rules, selected_driver_class) != PORTABLE_GP_ROSTER_OK ||
            (race->gp_grid != NULL &&
             portable_gp_race_roster_apply_course_grid(&session->roster, race->gp_grid, ranks) !=
                 PORTABLE_GP_ROSTER_OK))
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        /* Normal GP sub_803BF34 (0803C0E8) initializes +0x178 from
         * (rank_flags_ba >> 2) + 2. sub_8049850 shares the player's
         * count with every rival before driver updates. Rank seven
         * starts with five; a first-place session-course grid starts at two. */
        session->roster.hazard_sound_rules = race->hazard_sound_rules;
        session->roster.roulette_rules = race->roulette_rules;
        for (unsigned i = 0u; i < 8u; ++i)
            session->roster.racers[i].item_inventory.rules = race->roulette_rules;
        session->roster.item_status_rules = race->dependencies.item_status_rules;
        session->roster.boo_rules = race->dependencies.boo_rules;
        const uint16_t starting_coins =
            battle ? 0u : (uint16_t)((session->roster.racers[0].rank_flags_ba >> 2u) + 2u);
        for (size_t racer = 0u; racer < portable_gp_race_roster_count(&session->roster); ++racer)
        {
            session->roster.racers[racer].coin_count_178 = starting_coins;
            session->roster.racers[racer].affine_rules = race->dependencies.affine_rules;
            session->roster.racers[racer].art_motion_rules = race->dependencies.art_motion_rules;
            session->roster.racers[racer].mushroom_rules = race->dependencies.mushroom_rules;
            session->roster.racers[racer].green_shell_rules = race->dependencies.green_shell_rules;
            session->roster.racers[racer].shell_orbit_rules = race->dependencies.shell_orbit_rules;
            session->roster.racers[racer].red_shell_rules = race->dependencies.red_shell_rules;
            session->roster.racers[racer].banana_rules = race->dependencies.banana_rules;
        }
        session->roster.racers[0].voice_override = race->voice_override;
        if (portable_gp_race_roster_decide_initial(
                &session->roster, &session->rules, race->dependencies.raster) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    }
    /* Source normal GP racing is exactly mode 0x0100.  The finish controller
     * later replaces it with the observed 0x2200 transition flags. */
    session->roster.race_mode_flags_7da = battle ? UINT16_C(0x0800) : UINT16_C(0x0100);
    if (battle)
    {
        session->roster.race_phase_7d4 = 3u;
        session->roster.battle_scene_state = 3u;
        for (size_t i = 0u; i < portable_gp_race_roster_count(&session->roster); ++i)
        {
            PortableGpRacer *r = &session->roster.racers[i];
            r->arena_start_x = (int16_t)(r->position_x_q16 / 65536);
            r->arena_start_y = (int16_t)(r->position_y_q16 / 65536);
            r->arena_start_heading = r->heading;
        }
    }
    session->roster.shell_orbit_delay_valid =
        session->cpu_item_rules != NULL && session->cpu_item_rules->shell_orbit_delay_valid;
    if (session->roster.shell_orbit_delay_valid)
        memcpy(session->roster.shell_orbit_delay, session->cpu_item_rules->shell_orbit_delay, 6u);

    if (race->coin_count > PORTABLE_RECOVERED_COIN_CAPACITY ||
        (race->coin_count != 0u && race->coins == NULL))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    session->coin_count = battle ? 0u : race->coin_count;
    for (size_t index = 0u; index < session->coin_count; ++index)
    {
        session->coins[index].placement = race->coins[index];
        session->coins[index].active = 1u;
        size_t cell;
        if (!coin_terrain_cell(&session->player_dependencies, &race->coins[index], &cell))
            return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    }

    if (race->course_object_count > PORTABLE_RECOVERED_COURSE_OBJECT_CAPACITY ||
        (race->course_object_count != 0u && race->course_objects == NULL))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    session->item_box_count = race->course_object_count;
    for (size_t index = 0u; index < session->item_box_count; ++index)
    {
        if (race->course_objects[index].kind != PORTABLE_GP_ITEM_BOX_KIND ||
            race->course_objects[index].collision_half_extent != 0x1eu)
            return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
        session->item_boxes[index].placement = race->course_objects[index];
        session->item_boxes[index].active = 1u;
    }

    if (session->player_dependencies.owned_terrain_cells)
        for (size_t index = 0u; index < session->coin_count; ++index)
        {
            size_t cell;
            if (!coin_terrain_cell(&session->player_dependencies, &session->coins[index].placement, &cell))
                return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
            session->player_dependencies.owned_terrain_cells[cell] = UINT8_C(0xc0);
        }
    session->player.balloons_remaining = session->roster.racers[0].balloons_remaining;
    session->player.recovery_flags_74 = session->roster.racers[0].hit_state_flags_74;
    sync_player(session);
    portable_lap_hud_init(&session->lap_hud);
    session->rival_coin_count_handoff = (uint8_t)session->roster.racers[0].coin_count_178;
    portable_character_camera_init(&session->character_camera, session->player.steering.steering_offset);
    if (portable_snow_weather_init(
            &session->snow_weather, session->weather_profile ? &session->weather_profile->rules : NULL,
            session->player.pose.position_x, session->player.pose.position_y, race_camera_yaw(session),
            (session->roster.race_mode_flags_7da & 1u) == 0u) < 0)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    if (!tick_sky_motion(session, session->source_frame_clock, 1))
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    session->runtime.recovered_driver = &session->player;
    session->runtime.recovered_dependencies = &session->player_dependencies;
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus portable_gp_race_session_init_engine_grid_order(
    PortableGpRaceSession *session, const PortableRecoveredRaceView *race,
    uint8_t selected_driver_class, const uint8_t rank_by_racer[8], uint8_t engine_class)
{
    return init_engine_grid_order(session, race, selected_driver_class, rank_by_racer,
                                  engine_class, 0);
}

PortableGpRaceSessionStatus portable_gp_race_session_init_quick_run(
    PortableGpRaceSession *session, const PortableRecoveredRaceView *race,
    uint8_t selected_driver_class, uint8_t engine_class, PortableQuickRunOptions options)
{
    if (session == NULL || race == NULL || options.five_laps > 1u || options.items_off > 1u ||
        options.coins_off > 1u)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (race->rules.progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    PortableRecoveredRaceView selected = *race;
    selected.rules.lap_count = options.five_laps ? 5u : 3u;
    if (options.items_off)
    {
        selected.course_objects = NULL;
        selected.course_object_count = 0u;
    }
    if (options.coins_off)
    {
        selected.coins = NULL;
        selected.coin_count = 0u;
    }
    const PortableGpRaceSessionStatus status = portable_gp_race_session_init_engine_grid_order(
        session, &selected, selected_driver_class, NULL, engine_class);
    if (status != PORTABLE_GP_RACE_SESSION_OK)
        return status;
    session->quick_run_options = options;
    session->roster.race_mode_flags_7da = UINT16_C(0x0110);
    if (options.coins_off)
    {
        for (unsigned racer = 0u; racer < PORTABLE_GP_RACER_COUNT; ++racer)
            session->roster.racers[racer].coin_count_178 = 0u;
        session->rival_coin_count_handoff = 0u;
    }
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus portable_gp_race_session_init_grid_order(
    PortableGpRaceSession *session, const PortableRecoveredRaceView *race,
    uint8_t selected_driver_class, const uint8_t rank_by_racer[8])
{
    return portable_gp_race_session_init_engine_grid_order(session, race, selected_driver_class,
                                                           rank_by_racer, 0u);
}

PortableGpRaceSessionStatus portable_gp_race_session_init(PortableGpRaceSession *session,
                                                          const PortableRecoveredRaceView *race,
                                                          uint8_t selected_driver_class)
{
    return portable_gp_race_session_init_grid_order(session, race, selected_driver_class, NULL);
}

PortableGpRaceSessionStatus
portable_gp_race_session_handoff_launch_rng(PortableGpRaceSession *session, uint32_t elapsed_frames)
{
    if (session == NULL)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    for (uint32_t frame = 0u; frame < elapsed_frames; ++frame)
        (void)portable_gp_item_random(&session->source_rng_state);
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus
portable_gp_race_session_handoff_launch_inputs(PortableGpRaceSession *session,
                                               const uint16_t *semantic_inputs, size_t count)
{
    if (session == NULL || semantic_inputs == NULL || count != PORTABLE_GP_LAUNCH_PRELUDE_TICKS)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (session->runtime.session.phase != PORTABLE_RACE_PHASE_COUNTDOWN ||
        session->runtime.session.countdown_remaining != session->runtime.session.countdown_ticks ||
        session->runtime.session.elapsed_ticks != 0u || session->source_result_clock.enabled ||
        session->launch_prelude_applied || session->launch_revs_92 != 0u ||
        session->launch_rev_mode_98 != 0u)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    for (size_t index = 0u; index < count; ++index)
    {
        tick_player_launch_revs(
            session, portable_driver_control_word_from_semantic_buttons(semantic_inputs[index]));
        if (!tick_surface_trails(session))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    session->launch_prelude_applied = 1u;
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus
portable_gp_race_session_handoff_clock_boundary(PortableGpRaceSession *session,
                                                PortableGpClockEnableBoundary boundary)
{
    if (session == NULL || (boundary != PORTABLE_GP_CLOCK_ENABLE_ON_FIRST_RACING_TICK &&
                            boundary != PORTABLE_GP_CLOCK_ENABLE_BEFORE_FINAL_COUNTDOWN))
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (session->runtime.session.phase != PORTABLE_RACE_PHASE_COUNTDOWN ||
        session->runtime.session.countdown_remaining != session->runtime.session.countdown_ticks ||
        session->runtime.session.countdown_ticks < 2u || session->source_result_clock.enabled)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    session->source_clock_enable_boundary = (uint8_t)boundary;
    if (boundary == PORTABLE_GP_CLOCK_ENABLE_BEFORE_FINAL_COUNTDOWN)
    {
        /* Ready-grid IWRAM at source counts 201-204 has global frame
         * counter +0x10 low bits equal to native ticks 178-181. The fresh
         * grid is six ticks ahead instead. */
        session->source_frame_clock = 0u;
    }
    return PORTABLE_GP_RACE_SESSION_OK;
}

/* 58AB0/58DE8: a teepee's Shy Guy rides one roster kart and holds +AC. */
static int tick_teepees(PortableGpRaceSession *session)
{
    PortableTeepeeKart karts[PORTABLE_GP_RACER_COUNT];
    const size_t count = portable_gp_race_roster_count(&session->roster);
    for (size_t i = 0u; i < count; ++i)
    {
        const PortableGpRacer *r = &session->roster.racers[i];
        karts[i] = (PortableTeepeeKart){.x_q16 = r->position_x_q16,
                                        .y_q16 = r->position_y_q16,
                                        .z_q16 = r->world_height_q16,
                                        .speed_q16 = r->speed_q16,
                                        .component_x = r->component_x,
                                        .component_y = r->component_y,
                                        .protection = r->lightning_status_6a,
                                        .heading = r->heading,
                                        .lock = r->item_lock_ac,
                                        .is_player = r->is_player,
                                        .muted = (uint8_t)((r->source_flags & 0x20u) != 0u)};
    }
    const int cues = portable_course_props_tick_teepees(
        &session->course_props, karts, count, session->player_dependencies.sine_table,
        session->player_dependencies.sine_count, session->source_frame_clock);
    if (cues < 0)
        return 0;
    for (size_t i = 0u; i < count; ++i)
        session->roster.racers[i].item_lock_ac = karts[i].lock;
    session->player.item_lock_ac = session->roster.racers[session->local_index].item_lock_ac;
    if (cues)
        session->player.contact_audio =
            (PortableDriverContactAudio){.serial = session->player.contact_audio.serial + 1u,
                                         .song_id = 372u,
                                         .volume = 256u,
                                         .restart = 1u};
    return 1;
}

static int advance_course_spatial(PortableGpRaceSession *session)
{
    if (!portable_course_prop_motion_tick(
            &session->course_props, session->player_dependencies.sine_table,
            session->player_dependencies.sine_count, session->rules.compatibility.course_id,
            "Mario GP",
            &(PortableCoursePropListener){.x_q16 = session->player.pose.position_x,
                                          .y_q16 = session->player.pose.position_y,
                                          .flags = session->player.steering.state_flags,
                                          .sounds = &session->prop_sounds}) ||
        !tick_teepees(session))
        return 0;
    if (!portable_lava_bubble_course_advance(&session->lava_bubbles,
                                             session->player_dependencies.sine_table))
        return 0;
    if (!portable_crab_motion_tick(&session->crabs, session->player_dependencies.sine_table,
                                   session->rules.compatibility.course_id, "Mario GP"))
        return 0;
    if (!portable_seagull_motion_tick(&session->seagulls, session->player_dependencies.sine_table,
                                      session->rules.compatibility.course_id, "Mario GP"))
        return 0;
    if (!portable_piranha_motion_tick(&session->piranhas, session->player.steering.state_flags,
                                      session->rules.compatibility.course_id, "Mario GP",
                                      &session->piranha_sounds))
        return 0;
    if (!portable_mouse_motion_tick(&session->mice, session->player_dependencies.sine_table,
                                    session->player_dependencies.sine_count,
                                    session->rules.compatibility.course_id, "Mario GP"))
        return 0;
    if (!portable_thundercloud_motion_tick(&session->thunderclouds,
                                           session->rules.compatibility.course_id, "Mario GP"))
        return 0;
    if (!portable_falling_star_motion_tick(&session->falling_stars,
                                           session->rules.compatibility.course_id, "Mario GP"))
        return 0;
    if (!portable_snowman_motion_tick(&session->snowmen, session->rules.compatibility.course_id,
                                      "Mario GP"))
        return 0;
    if (!portable_penguin_motion_tick(&session->penguins, session->player_dependencies.sine_table,
                                      session->player_dependencies.sine_count,
                                      session->rules.compatibility.course_id, "Mario GP"))
        return 0;
    if (!portable_boo_motion_tick(&session->boos, session->player_dependencies.sine_table,
                                  session->player_dependencies.sine_count,
                                  session->rules.compatibility.course_id, "Mario GP"))
        return 0;
    const PortableCannonballListener cannonball_listener = {
        .x_q16 = session->player.pose.position_x,
        .y_q16 = session->player.pose.position_y,
        .heading = (uint16_t)session->player.steering.steering_offset,
        .driver_flags = session->player.steering.state_flags,
    };
    if (!portable_cannonball_motion_tick(
            &session->cannonballs, session->rules.compatibility.course_id, "Mario GP",
            &cannonball_listener, session->player_dependencies.sine_table,
            session->player_dependencies.sine_count, &session->cannonball_sounds))
        return 0;
    if (!portable_thwomp_motion_tick(&session->thwomps, session->rules.compatibility.course_id,
                                     "Mario GP", session->player.pose.position_x,
                                     session->player.pose.position_y,
                                     session->player.steering.state_flags, &session->thwomp_sounds))
        return 0;
    return 1;
}

static int collide_course_actors(PortableGpRaceSession *session)
{
    session->player.lightning_timer_68 =
        (int16_t)session->roster.racers[session->local_index].lightning_timer_68;
    if (session->runtime.session.phase != PORTABLE_RACE_PHASE_RACING)
        return 1;
    session->player.source_route_index_90 =
        session->roster.racers[session->local_index].route_index;
    PortableCourseContacts worlds[PORTABLE_GP_RACER_COUNT] = {
        {.driver = &session->player,
         .dependencies = &session->player_dependencies,
         .course_props = &session->course_props,
         .lava_bubbles = &session->lava_bubbles,
         .thwomps = &session->thwomps,
         .crabs = &session->crabs,
         .seagulls = &session->seagulls,
         .cannonballs = &session->cannonballs,
         .piranhas = &session->piranhas,
         .snowmen = &session->snowmen,
         .mice = &session->mice,
         .thunderclouds = &session->thunderclouds,
         .falling_stars = &session->falling_stars,
         .penguins = &session->penguins,
         .boos = &session->boos,
         .piranha_sounds = &session->piranha_sounds,
         .impact_sounds = &session->impact_sounds,
         .driver_class =
             portable_gp_racer_voice_class(&session->roster.racers[session->local_index]),
         .listener_flags = session->player.steering.state_flags,
         .listener_x = session->player.pose.position_x,
         .listener_y = session->player.pose.position_y,
         .scene_clock = session->source_frame_clock + 1u,
         .course_id = session->rules.compatibility.course_id,
         .checkpoint_count = (uint16_t)session->rules.route_count,
         .mode_name = "GP/Quick Run/Battle",
         .is_player = 1u,
         .kart_index = (uint8_t)session->local_index,
         .collision_guard = session->roster.racers[session->local_index].banana_hit_timer_38}};
    PortableDriverPilotState cpu_drivers[PORTABLE_GP_OPPONENT_COUNT] = {0};
    /* Locals take their own course-actor pass; VS and Battle have no CPUs. */
    const size_t world_count =
        multi_local(session) ? 1u : portable_gp_race_roster_count(&session->roster);
    for (size_t index = 1u; index < world_count; ++index)
    {
        PortableGpRacer *racer = &session->roster.racers[index];
        PortableDriverPilotState *driver = &cpu_drivers[index - 1u];
        driver->pose.position_x = racer->position_x_q16;
        driver->pose.position_y = racer->position_y_q16;
        driver->pose.position_z = racer->world_height_q16;
        driver->fall_frames_76 = racer->fall_frames_76;
        driver->motion.speed_q16 = racer->speed_q16;
        driver->motion.component_x = racer->component_x;
        driver->motion.component_y = racer->component_y;
        driver->steering.angle = (int16_t)racer->source_heading;
        driver->steering.state_flags = racer->source_flags;
        driver->steering.response_flags = racer->mushroom_response_flags_b4;
        driver->steering.response_q8 = (int16_t)racer->hit_counter_88;
        driver->steering.control_word = driver->input.control_word = racer->control;
        driver->forward_acceleration.state_flags = racer->mushroom_response_flags_b4;
        driver->hop.mode = racer->lightning_state_78;
        driver->contact_guard_66 = (uint16_t)racer->contact_status_66;
        driver->invulnerability_timer_6a = (uint16_t)racer->lightning_status_6a;
        driver->lightning_timer_68 = (int16_t)racer->lightning_timer_68;
        driver->normal_collision_timer_38 = racer->banana_hit_timer_38;
        driver->vector_effect.contact_word_5a = racer->kart_contact_state_5a;
        driver->source_route_index_90 = racer->route_index;
        driver->contact_anchor_x_ee = racer->contact_center_x;
        driver->contact_anchor_y_f0 = racer->contact_center_y;
        driver->recovery_contact_34 = racer->recovery_contact_34;
        driver->recovery_flags_74 = racer->hit_state_flags_74;
        driver->strong_spin_rate_b6 = racer->hit_response_b6;
        driver->flattened_64 = racer->flattened_64;
        driver->affine = racer->affine;
        driver->palette_requests_194 = racer->palette_requests_194;
        driver->burn_ticks_72 = racer->burn_ticks_72;
        driver->item_cue_flags_58 = racer->hit_cue_flags_58;
        worlds[index] = worlds[0];
        worlds[index].driver = driver;
        worlds[index].driver_class = portable_gp_racer_voice_class(racer);
        worlds[index].is_player = 0u;
        worlds[index].player_rank_ahead = 0u;
        worlds[index].kart_index = (uint8_t)index;
        worlds[index].collision_guard = 0u;
        for (size_t rank = 0u; rank < portable_gp_race_roster_count(&session->roster); ++rank)
        {
            const uint8_t ranked = session->roster.standings[rank];
            if (ranked == index)
                break;
            if (ranked >= portable_gp_race_roster_count(&session->roster))
                return 0;
            if (session->roster.racers[ranked].source_flags & UINT32_C(0x8000))
                worlds[index].player_rank_ahead = 1u;
        }
    }
    if (!portable_course_contacts_collide_many(worlds, world_count))
        return 0;
    if (worlds[0].events.burns && !(session->player.steering.state_flags & 0x20u))
        portable_lava_smoke_start(&session->lava_smoke);
    for (size_t index = 0u; index < world_count; ++index)
        for (uint8_t n = 0u; n < worlds[index].events.boo_haunts; ++n)
            if (portable_gp_race_roster_haunt(&session->roster,
                                              index == 0u ? session->local_index : index) !=
                PORTABLE_GP_ROSTER_OK)
                return 0;
    session->course_contact_events += worlds[0].events.contacts;
    if (worlds[0].events.contacts)
    {
        session->roster.racers[session->local_index].lightning_timer_68 =
            (uint16_t)session->player.lightning_timer_68;
        session->player.normal_collision_source_from_native_state = 1u;
        session->player.normal_collision_source_use_b550_table = 1u;
        session->roster.racers[session->local_index].hit_state_flags_74 =
            session->player.recovery_flags_74;
        sync_player(session);
    }
    for (uint8_t i = 0u; i < worlds[0].events.coin_loss_count; ++i)
        if (apply_coin_loss_at(
                session, (PortableCoinEffectCause)worlds[0].events.coin_loss_causes[i],
                session->source_frame_clock + ((tick_parts(session) & TICK_WORLD) != 0u)) !=
            PORTABLE_GP_RACE_SESSION_OK)
            return 0;
    if (worlds[0].events.strong_hits &&
        portable_gp_driving_quality_record_event(
            &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_0A,
            session->roster.race_mode_flags_7da,
            session->player.steering.state_flags) != PORTABLE_GP_DRIVING_QUALITY_OK)
        return 0;
    for (size_t index = 1u; index < world_count; ++index)
    {
        PortableGpRacer *racer = &session->roster.racers[index];
        const PortableDriverPilotState *driver = &cpu_drivers[index - 1u];
        session->cpu_course_contact_events += worlds[index].events.contacts;
        racer->palette_requests_194 = driver->palette_requests_194;
        racer->burn_ticks_72 = driver->burn_ticks_72;
        if (!worlds[index].events.contacts)
            continue;
        racer->component_x = driver->motion.component_x;
        racer->component_y = driver->motion.component_y;
        racer->source_flags = driver->steering.state_flags;
        racer->mushroom_response_flags_b4 = driver->forward_acceleration.state_flags;
        racer->hit_counter_88 = (uint16_t)driver->steering.response_q8;
        racer->control = driver->input.control_word;
        racer->banana_hit_timer_38 = driver->normal_collision_timer_38;
        racer->kart_contact_state_5a = driver->vector_effect.contact_word_5a;
        racer->hit_state_flags_74 = driver->recovery_flags_74;
        racer->hit_response_b6 = driver->strong_spin_rate_b6;
        racer->flattened_64 = driver->flattened_64;
        racer->affine = driver->affine;
        racer->lightning_timer_68 = (uint16_t)driver->lightning_timer_68;
        if (driver->normal_collision_timer_38 > 1u)
            racer->course_contact_hold = 1u;
    }
    return 1;
}

static PortableGpRaceSessionStatus
complete_item_art_tick(PortableGpRaceSession *session, unsigned parts,
                       const PortableSnowWeatherInputs *weather_input)
{
    const unsigned marker_phase = session->roster.race_phase_7d4 & 15u;
    if ((parts & TICK_WORLD) && !(session->roster.race_phase_7d4 & 0x40u) && marker_phase >= 1u &&
        marker_phase <= 4u)
        session->marker_display_clock = session->marker_clock++;
    /* 49D78/D328 follows the complete driver pass. C444's last balloon
     * starts B020, and this same tick consumes the first AFA4 duration. */
    if (session->player.battle_enabled && (parts & TICK_WORLD))
    {
        for (size_t i = 0u; i < portable_gp_race_roster_count(&session->roster); ++i)
        {
            PortableGpRacer *racer = &session->roster.racers[i];
            if (session->battle_item_rules == NULL ||
                !portable_battle_elimination_tick(&racer->elimination, racer->balloon_event,
                                                  session->battle_item_rules->elimination_ticks))
                return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
            if (racer->elimination.revenge_bomb)
            {
                const uint8_t cue =
                    portable_battle_bomb_walk_tick(&racer->contact_flags_6d, racer->speed_q16);
                racer->bomb_walk_audio = cue;
                if (i == session->local_index)
                    session->player_bomb_walk_audio = cue;
            }
            if (racer->bomb_audio_pending)
            {
                racer->bomb_audio_pending = 0u;
                /* E7F0 starts390 on the source song's player7. */
                if (!queue_race_audio(session, 390u, 7u, racer->bomb_audio_volume, 0))
                    return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
            }
        }
    }
    /* AD00 runs after item constructors: a repeated strike restarts at black. */
    if ((parts & TICK_WORLD) && session->roster.lightning_state_80d == 2u &&
        session->roster.lightning_duration_c82 ==
            portable_item_status_flash_end(session->roster.item_status_rules))
        session->roster.lightning_state_80d = 0u;
    if ((parts & TICK_LOCAL) && session->recovery_bob_offsets != NULL)
    {
        uint16_t consumed_requests = 0u;
        if (((session->hazard_effect_72 & 15u) == 13u ||
             (session->hazard_effect_72 & 15u) == 11u) &&
            (session->player.hop.animation_flags & UINT16_C(0x8000)))
            session->hazard_effect_72 &= UINT16_C(0x100);
        if (session->hazard_lakitu.phase == PORTABLE_GP_LAKITU_FINISH &&
            portable_gp_finish_lakitu_tick(&session->hazard_lakitu, session->finish_lakitu_rules,
                                           session->recovery_bob_offsets,
                                           session->player_dependencies.sine_table) != 1)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        /* B64 follows C4C0; requests raised this turn wait until 51014. */
        if (portable_gp_recovery_lakitu_tick(
                &session->hazard_lakitu, &consumed_requests, session->player.steering.state_flags,
                session->prior_projected_y, &session->player.pose.position_z,
                &session->hazard_effect_72, session->recovery_bob_offsets,
                session->player_dependencies.sine_table) != 1)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        session->player.hop.height_q16 = session->player.pose.position_z;
        if (session->hazard_lakitu.retrieval_effect_started && session->recovery_fx_rules &&
            !portable_recovery_fx_request(&session->recovery_fx, session->recovery_fx_rules,
                                          &session->hazard_effect_72,
                                          (uint8_t)(session->hazard_effect_72 & 15u),
                                          !(session->player.steering.state_flags & 0x20u)))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        session->player.hop.recovery_requests &= (uint16_t)~session->hazard_lakitu.cleared_requests;
        if (portable_gp_race_session_apply_coin_loss(
                session, session->hazard_lakitu.coin_loss_requested) != PORTABLE_GP_RACE_SESSION_OK)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    if (parts & TICK_WORLD)
    {
        uint8_t boo_phases[PORTABLE_BOO_ATTACK_CAPACITY];
        for (unsigned i = 0u; i < PORTABLE_BOO_ATTACK_CAPACITY; ++i)
            boo_phases[i] = session->roster.boo_attacks[i].phase;
        if (portable_gp_race_roster_tick_boo_attacks(
                &session->roster, session->player_dependencies.sine_table,
                session->player_dependencies.sine_count) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        for (unsigned i = 0u; i < PORTABLE_BOO_ATTACK_CAPACITY; ++i)
        {
            const PortableBooAttack *actor = &session->roster.boo_attacks[i];
            if (actor->target != 0u)
                continue;
            const int effect = portable_boo_attack_music_transition(boo_phases[i], actor->phase);
            if (effect != 0)
            {
                if (!queue_race_music(session, 2u,
                                      effect > 0 ? PORTABLE_GP_AUDIO_MUSIC_REQUEST
                                                 : PORTABLE_GP_AUDIO_MUSIC_RELEASE))
                    return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
            }
        }
        if (!advance_course_spatial(session))
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    }
    if ((parts & TICK_WORLD) &&
        portable_snow_weather_tick(
            &session->snow_weather,
            session->weather_profile ? &session->weather_profile->rules : NULL, weather_input,
            session->player_dependencies.sine_table, session->player_dependencies.sine_count,
            main_visual_random, &session->main_visual_rng_state) < 0)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if ((parts & TICK_LOCAL) &&
        (!session->finish_camera.phase || session->finish_camera.phase == 8u))
    {
        const PortableCharacterCameraInput camera_input = {
            .driver_heading_7c = session->player.steering.steering_offset,
            .response_status_7e = session->player.steering.response_status,
            .steering_position_82 = session->player.steering.position_82,
            .control_word_96 = session->player.steering.control_word};
        if (!portable_character_camera_tick(&session->character_camera, &camera_input))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    if ((parts & TICK_LOCAL) && session->finish_rules != NULL && !session->player.battle_enabled)
    {
        if (session->runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED &&
            !session->finish_camera.phase)
            portable_finish_camera_start(&session->finish_camera);
        const PortableFinishCameraInput input = {
            .driver_x_q16 = session->player.pose.position_x,
            .driver_y_q16 = session->player.pose.position_y,
            .height_q16 = session->player.pose.position_z,
            .driver_angle_56 = session->player.steering.source_angle_56,
            .normal_x_q16 = session->player.pose.position_x,
            .normal_y_q16 = session->player.pose.position_y,
            .normal_yaw = portable_character_camera_yaw(&session->character_camera),
            .mode_flags_7da = session->roster.race_mode_flags_7da,
            .driver_mode_78 = session->player.hop.mode,
            .rank = (uint8_t)(session->roster.racers[session->local_index].rank_flags_ba >> 1u),
            .material_85 = session->player.source_material_85,
            .bob_phase = session->player.art_motion.bob_phase};
        if (portable_finish_camera_tick(&session->finish_camera, &session->finish_rules->camera,
                                        &input, session->player_dependencies.sine_table,
                                        session->player_dependencies.sine_count) < 0)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        PortableGpRacer *racer = &session->roster.racers[session->local_index];
        if (session->finish_camera.snapshot.request_celebration &&
            portable_finish_animation_request(
                &racer->contact_mode_6e, &racer->contact_phase_6f, &racer->contact_flags_6d,
                &session->finish_rules->schedules[racer->driver_class]) < 0)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        if (session->finish_camera.snapshot.cancel_effects)
        {
            racer->contact_mode_6e = racer->contact_phase_6f = 0u;
            session->player.affine.mode = session->player.affine.row =
                session->player.affine.remaining = session->player.affine.pending_mode = 0u;
            portable_recovery_fx_cancel(&session->hazard_effect_72,
                                        &session->player.palette_requests_194);
            racer->effect_state_72 = session->hazard_effect_72;
            session->player.burn_ticks_72 = racer->burn_ticks_72 = 0u;
        }
    }
    if ((parts & TICK_WORLD) && !tick_ambient(session, PORTABLE_AMBIENT_POST_DRIVER,
                                              session->source_frame_clock, &session->sky_motion))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    for (size_t i = 0u; i < portable_gp_race_roster_count(&session->roster); ++i)
    {
        const int owns_local = (parts & TICK_LOCAL) && i == session->local_index;
        const int owns_cpu =
            (parts & TICK_WORLD) && !multi_local(session) && i != session->local_index;
        if (!owns_local && !owns_cpu)
            continue;
        PortableGpRacer *racer = &session->roster.racers[i];
        const uint16_t angle =
            owns_local ? session->player.steering.source_angle_56 : racer->source_heading;
        const PortableCharacterViewInput view_input = {
            .camera_yaw = race_camera_yaw(session),
            .driver_angle = angle,
            .selected = (uint8_t)owns_local,
            .source_flags = owns_local ? session->player.steering.state_flags : racer->source_flags,
            .steering_response_q8 =
                owns_local ? session->player.steering.response_q8 : (int16_t)racer->hit_counter_88,
            .speed_q16 = owns_local ? session->player.motion.speed_q16 : racer->speed_q16,
            .position_82 = session->player.steering.position_82,
            .camera_adjustment = session->character_camera.current_adjustment,
            .contact_mode_6e = racer->contact_mode_6e,
            .contact_phase_6f = racer->contact_phase_6f,
            .contact_frame_6c = racer->contact_frame_6c,
            .contact_flags_6d = racer->contact_flags_6d};
        const uint8_t view = portable_character_view_pose(&view_input, session->view_rules).angle;
        if ((racer->contact_mode_6e & 15u) == 2u)
        {
            if (session->finish_rules == NULL || racer->driver_class >= 8u ||
                portable_finish_animation_tick(
                    &racer->contact_mode_6e, &racer->contact_phase_6f, &racer->contact_frame_6c,
                    &session->finish_rules->schedules[racer->driver_class], view,
                    racer->contact_height_24) < 0)
                return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        }
        else if (portable_gp_race_roster_tick_contact_animation(
                     racer, view, session->roster.roster_rules) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    }
    if (parts & TICK_LOCAL)
    {
        PortableCameraState camera;
        PortableCameraProjection projection;
        const uint16_t yaw = race_camera_yaw(session);
        const unsigned slot = ((int16_t)yaw / 64) & 1023u;
        portable_camera_state_update(
            &camera,
            session->finish_camera.snapshot.active ? session->finish_camera.snapshot.x_q16
                                                   : session->player.pose.position_x,
            session->finish_camera.snapshot.active ? session->finish_camera.snapshot.y_q16
                                                   : session->player.pose.position_y,
            yaw);
        if (portable_camera_projection_build(
                &camera, session->player_dependencies.sine_table[slot],
                session->player_dependencies.sine_table[(256u - slot) & 1023u], 31414, 9319,
                &projection))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        for (size_t i = 0u; i < portable_gp_race_roster_count(&session->roster); ++i)
        {
            if (multi_local(session) && i != session->local_index)
                continue;
            PortableGpRacer *racer = &session->roster.racers[i];
            PortableCameraProjectionResult ground;
            const int result = portable_camera_projection_project(
                &camera, &projection, racer->position_x_q16, racer->position_y_q16, 0, &ground);
            if (result == PORTABLE_CAMERA_PROJECTION_OK)
            {
                /* Actor+1e is ground Y; Actor+20 is body X. */
                racer->contact_axis_1e = ground.y;
                racer->contact_axis_20 = ground.x;
                racer->contact_height_24 = (uint16_t)ground.distance;
            }
            else if (result == PORTABLE_CAMERA_PROJECTION_BEHIND_CAMERA ||
                     result == PORTABLE_CAMERA_PROJECTION_OUT_OF_RANGE)
            {
                racer->contact_height_24 = UINT16_MAX;
            }
            else
                return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        }
    }
    if ((parts & TICK_LOCAL) && !multi_local(session) &&
        session->player_dependencies.impact_effect_rules &&
        !portable_impact_effect_tick(&session->player.impact_effects,
                                     session->player_dependencies.impact_effect_rules))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if ((parts & TICK_LOCAL) && !multi_local(session) &&
        !portable_gp_race_session_tick_dropped_coins(session))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    /* C84C consumes one-shot palette requests once in simulation. Protection
     * and flash priority remain snapshot-readable for all participants. */
    if (parts & TICK_LOCAL)
    {
        const int affine_cue = portable_driver_affine_tick(
            &session->player.affine, session->player_dependencies.affine_rules);
        if (affine_cue < 0)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        if (affine_cue && !(session->player.steering.state_flags & 0x20u) &&
            !queue_race_audio(session, 133u, 6u, 256u, 0))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        session->roster.racers[session->local_index].affine = session->player.affine;
        if (session->player.burn_ticks_72 == PORTABLE_DRIVER_BURN_TICKS)
        {
            portable_recovery_fx_cancel(&session->hazard_effect_72,
                                        &session->player.palette_requests_194);
            session->hazard_effect_72 = 3u;
        }
        session->recovery_fx_events = 0u;
        if (session->recovery_fx_rules)
        {
            const int events = portable_recovery_fx_tick(
                &session->recovery_fx, session->recovery_fx_rules, &session->hazard_effect_72,
                session->player.hop.animation_flags, &session->player.palette_requests_194,
                session->source_frame_clock, portable_driver_effects_visible(&session->player),
                !(session->player.steering.state_flags & 0x20u));
            if (events < 0)
                return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
            session->recovery_fx_events = (uint8_t)events;
            if (events & PORTABLE_RECOVERY_FX_THAW_START)
                session->player.art_motion.wheel_phase = 2u;
        }
        portable_driver_palette_burn(&session->player.burn_ticks_72,
                                     &session->player.palette_requests_194);
        if (!session->player.burn_ticks_72 && (session->hazard_effect_72 & 15u) == 3u)
            portable_recovery_fx_cancel(&session->hazard_effect_72,
                                        &session->player.palette_requests_194);
        (void)portable_driver_art_motion_step(
            &session->player.art_motion, session->player_dependencies.art_motion_rules,
            session->player.motion.speed_q16, session->player.steering.state_flags,
            session->player.forward_acceleration.state_flags, session->player.source_material_85,
            session->player.palette_requests_194, session->source_frame_clock,
            session->roster.racers[session->local_index].driver_class,
            session->player.steering.response_status, 1, 0, session->roster.race_phase_7d4,
            session->roster.racers[session->local_index].elimination.revenge_bomb);
        session->roster.racers[session->local_index].art_motion = session->player.art_motion;
        if (portable_lava_smoke_tick_eligible(&session->lava_smoke, session->source_frame_clock,
                                              portable_driver_effects_visible(&session->player)) &&
            session->piranha_sounds.count < PORTABLE_PIRANHA_CAPACITY)
            session->piranha_sounds.songs[session->piranha_sounds.count++] = 183u;
        session->player.palette_bank = portable_driver_palette_select(
            &session->player.palette_requests_194,
            session->roster.racers[session->local_index].lightning_status_6a,
            (int16_t)session->roster.racers[session->local_index].lightning_timer_68,
            session->player.source_material_85, 0u, session->source_frame_clock);
        PortableGpRacer *local_palette = &session->roster.racers[session->local_index];
        local_palette->burn_ticks_72 = session->player.burn_ticks_72;
        local_palette->palette_requests_194 = session->player.palette_requests_194;
        local_palette->palette_bank = session->player.palette_bank;
        if (!tick_surface_trails(session))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    if (!(parts & TICK_WORLD))
        return PORTABLE_GP_RACE_SESSION_OK;
    for (size_t i = 1u; i < portable_gp_race_roster_count(&session->roster); ++i)
    {
        PortableGpRacer *racer = &session->roster.racers[i];
        if (racer->is_player)
            continue;
        if (portable_driver_affine_tick(&racer->affine, racer->affine_rules) < 0)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        portable_driver_palette_burn(&racer->burn_ticks_72, &racer->palette_requests_194);
        (void)portable_driver_art_motion_step(
            &racer->art_motion, racer->art_motion_rules, racer->speed_q16, racer->source_flags,
            racer->mushroom_response_flags_b4, racer->source_material_85,
            racer->palette_requests_194, session->source_frame_clock, racer->driver_class,
            racer->hit_state_7e, 0, 0, session->roster.race_phase_7d4,
            racer->elimination.revenge_bomb);
        racer->palette_bank = portable_driver_palette_select(
            &racer->palette_requests_194, racer->lightning_status_6a,
            (int16_t)racer->lightning_timer_68, racer->source_material_85, 0u,
            session->source_frame_clock);
    }
    return portable_gp_item_art_clock_tick(&session->item_art_clock, session->roster.race_phase_7d4)
               ? PORTABLE_GP_RACE_SESSION_OK
               : PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
}

static void clear_tick_requests(PortableGpRaceSession *session)
{
    session->audio_event_count = 0u;
    ++session->impact_sounds.serial;
    session->impact_sounds.count = 0u;
    ++session->piranha_sounds.serial;
    session->piranha_sounds.count = 0u;
    for (size_t i = 0u; i < PORTABLE_GP_RACER_COUNT; ++i)
        session->roster.racers[i].bomb_walk_audio = 0u;
}

/* Hits raised during the shared world pass reach a local's pilot here. */
static int adopt_pending_local_hits(PortableGpRaceSession *session)
{
    const size_t local = session->local_index;
    if (session->roster.racers[local].lightning_hit_pending && !adopt_local_lightning(session))
        return 0;
    const uint8_t kind = session->local_hit_pending[local];
    session->local_hit_pending[local] = 0u;
    return kind == 0u || adopt_local_hit(session, kind);
}

static int collide_racer_karts(PortableGpRaceSession *session, uint32_t clock)
{
    if (session->runtime.session.phase == PORTABLE_RACE_PHASE_COUNTDOWN)
        return 1;
    const uint8_t saved = session->local_index;
    const uint8_t locals = multi_local(session) ? session->local_count : 1u;
    if (locals > PORTABLE_GP_MAX_LOCALS)
        return 0;
    for (uint8_t k = 0u; k < locals; ++k)
    {
        if (portable_gp_race_session_select_local(session, k) != PORTABLE_GP_RACE_SESSION_OK)
            return 0;
        PortableGpRacer *r = &session->roster.racers[k];
        r->position_x_q16 = session->player.pose.position_x;
        r->position_y_q16 = session->player.pose.position_y;
        r->world_height_q16 = session->player.pose.position_z;
        /* Item actors retain ownership of the held/flight lock across the
         * pre-driver contact pass, just as in sync_player. */
        r->source_flags = (session->player.steering.state_flags & ~UINT32_C(0x10000)) |
                          (r->source_flags & UINT32_C(0x10000));
        r->component_x = session->player.motion.component_x;
        r->component_y = session->player.motion.component_y;
        r->heading = (uint16_t)session->player.motion.angle;
        r->source_heading = session->player.steering.source_angle_56;
        r->banana_hit_timer_38 = session->player.normal_collision_timer_38;
        r->contact_status_60 = session->player.vector_effect_source_state;
        r->contact_status_66 = (int16_t)session->player.contact_guard_66;
        r->lightning_state_78 = session->player.hop.mode;
    }
    if (portable_gp_race_session_select_local(session, saved) != PORTABLE_GP_RACE_SESSION_OK)
        return 0;
    PortableGpKartContactReport report;
    if (portable_gp_race_roster_collide_karts(
            &session->roster, (uint16_t)session->rules.route_count, clock,
            session->player_dependencies.sine_table, session->player_dependencies.sine_count,
            &report) != PORTABLE_GP_ROSTER_OK)
        return 0;
    for (uint8_t k = 0u; k < locals; ++k)
    {
        if (report.response[k])
            session->local_hit_pending[k] = report.response[k] == PORTABLE_GP_KART_CONTACT_PUSH
                                                ? LOCAL_HIT_PUSH
                                                : LOCAL_HIT_STRONG;
        if (report.bump[k])
        {
            if (portable_gp_race_session_select_local(session, k) != PORTABLE_GP_RACE_SESSION_OK)
                return 0;
            session->roster.player_bump_pending = 1u;
            session->roster.player_bump_speed_q16 = report.bump_speed_q16[k];
        }
    }
    return portable_gp_race_session_select_local(session, saved) == PORTABLE_GP_RACE_SESSION_OK;
}

static int hud_classes(const PortableGpRaceSession *s, uint8_t classes[4])
{
    for (unsigned i = 0u; i < 4u; ++i)
    {
        const unsigned racer = s->roster.standings[i];
        if (racer >= PORTABLE_GP_RACER_COUNT)
            return 0;
        classes[i] = s->roster.racers[racer].driver_class;
    }
    return 1;
}

static int tick_owned_hud(PortableGpRaceSession *s)
{
    const PortableGpRaceHudAssetView *r = s->hud_rules;
    if (!r || !s->rank_hud.initialized || !s->coin_hud.initialized)
        return 1;
    uint8_t classes[4];
    const PortableGpRacer *player = &s->roster.racers[s->local_index];
    if (!hud_classes(s, classes))
        return 0;
    portable_gp_rank_hud_separator_step(&s->rank_hud, s->source_frame_clock,
                                        r->rank_separator_colors);
    /* DF08/E144 and DA80 run before this driver's motion. Their pending
     * map/palette writes become the following accepted snapshot's display. */
    return portable_gp_rank_hud_step(
               &s->rank_hud, r->rank_maps, r->rank_transition_maps, r->kind_pages, r->rank_colors,
               r->rank_flash_colors, (uint8_t)(player->rank_flags_ba >> 1u), classes,
               s->source_result_clock.centiseconds, s->source_result_clock.enabled ? 4u : 0u) &&
           portable_gp_coin_hud_step(&s->coin_hud, s->source_frame_clock, player->coin_count_178,
                                     s->player.steering.state_flags, s->roster.race_phase_7d4,
                                     r->coin_hud_colors);
}

/* Turn-local observations and deferred contact work are shared by the player
 * and world phases, without adding state to snapshots or saved races. */
typedef struct
{
    uint16_t semantic_buttons;
    int32_t contact_acceleration_q16;
    int32_t boundary_acceleration_q16;
    uint32_t charged_exit_count;
    uint8_t start_boosts;
    uint8_t player_contact_response;
    uint8_t boundary_hold;
    uint8_t item_pressed;
    uint8_t accelerate_pressed;
    uint8_t roulette_was_active;
    uint8_t launch_racing_tick;
    uint8_t player_coins_before_motion;
    uint8_t ground_mode_before_motion;
    uint16_t laps_before_motion;
    uint16_t source_control;
    uint16_t steering_status_before_motion;
    uint16_t drift_angle_before_motion;
    int32_t x_before_motion;
    int32_t y_before_motion;
    unsigned parts;
    int single;
    PortableSnowWeatherInputs weather_input;
    int32_t entry_splash_height;
    int completed;
} GpRaceTick;

static PortableGpRaceSessionStatus prepare_race_world(PortableGpRaceSession *session,
                                                      PortableRaceTickDisposition *disposition,
                                                      GpRaceTick *tick)
{
    if (session == NULL || disposition == NULL)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    session->player.impact_event = 0u;
    tick->parts = tick_parts(session);
    session->roster.coin_effect_clock =
        session->source_frame_clock + ((tick->parts & TICK_WORLD) != 0u);
    if (session->coin_effect_rules || session->player_dependencies.impact_effect_rules)
    {
        PortableCameraProjectionResult body;
        if (coin_body(session, &body))
        {
            session->coin_birth_x = body.x;
            session->coin_birth_y = body.y;
            session->coin_birth_inverse = (int16_t)body.distance;
            session->player.impact_x = body.x;
            session->player.impact_y = body.y;
            session->player.impact_depth = (int16_t)body.distance;
        }
        else if (!(session->player.steering.state_flags & UINT32_C(0x20)))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    tick->weather_input = snow_weather_inputs(session, session->source_frame_clock +
                                                           ((tick->parts & TICK_WORLD) != 0u));
    if (!tick_sky_motion(session, session->source_frame_clock + ((tick->parts & TICK_WORLD) != 0u),
                         0))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    portable_entry_splash_tick(&session->entry_splash);
    tick->single = (tick->parts & TICK_WORLD) != 0u;
    /* Collision voices are requests for this tick. Clearing after978C would
     * erase them before the audio host observes the immutable snapshot. */
    if (tick->single)
    {
        clear_tick_requests(session);
        if (!collide_racer_karts(session, session->source_frame_clock + 1u))
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    }
    session->player_bomb_walk_audio = 0u;
    /* A4D8/D4B4 precedes978C: later pickup may replace the HUD song. */
    const uint16_t hud_song = portable_gp_item_hud_tick(
        &session->roster.item_hud,
        (uint8_t)((session->roster.racers[session->local_index].source_flags & 0x20u) != 0u));
    if (hud_song && !queue_race_audio(session, hud_song, 7u, 256u, 0))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    adopt_player_bomb_respawn(session);
    if (!adopt_pending_local_hits(session))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    session->player.steering.state_flags =
        (session->player.steering.state_flags & ~UINT32_C(0x10000)) |
        (session->roster.racers[session->local_index].source_flags & UINT32_C(0x10000));
    if (!collide_course_actors(session))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    if (tick->single)
        ++session->source_frame_clock;
    for (unsigned i = 0u; i < PORTABLE_COIN_PICKUP_EFFECT_CAPACITY; ++i)
        portable_coin_pickup_effect_tick(&session->coin_pickups[i]);
    /* play.c runs484E0 after978C and before49850's driver turns. A later
     * lightning constructor resets the full360-tick scene clock. */
    if (tick->single && session->roster.lightning_duration_c82)
        --session->roster.lightning_duration_c82;
    /* 49850 updates drivers before passive course actors. Successful paths
     * advance those actors once in complete_item_art_tick below. */
    /* sub_804D8F8 runs before the frame's GP driver update. Its start flag
     * is set later in that first racing frame, so tick one stays at zero. */
    if (!portable_gp_source_clock_tick(&session->source_result_clock, session->hud_clock_steps,
                                       session->hud_clock_step_count))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    /* text08007138.c calls sub_8043840 once after main_frameProc on every
     * active game frame. Item selection later in this tick consumes the next
     * sample from the same DAT_03002344 state. */
    if (tick->single)
        (void)portable_gp_item_random(&session->source_rng_state);
    tick->steering_status_before_motion = session->player.steering.response_status;
    tick->drift_angle_before_motion = session->player.steering.drift_angle;
    tick->player_coins_before_motion =
        (uint8_t)session->roster.racers[session->local_index].coin_count_178;
    tick->ground_mode_before_motion = session->player.hop.mode;
    tick->x_before_motion = session->player.pose.position_x;
    tick->y_before_motion = session->player.pose.position_y;
    /* The native player update is one source frame ahead of the rival update.
     * Original frame 5456 has player coin 6 but all rivals still at 5; they
     * receive 6 on frame 5457. Retain the count seen at the previous tick's
     * entry so the rivals consume it on their matching source frame. */
    /* A pickup reaches rivals on their following update. Contact loss is
     * copied to all eight racer objects on the contact frame below. */
    if (tick->single)
    {
        if (session->roster.racers[session->local_index].coin_count_178 >
            session->rival_coin_count_handoff)
            session->rival_coin_count_handoff =
                (uint8_t)session->roster.racers[session->local_index].coin_count_178;
        for (size_t index = 1u; index < portable_gp_race_roster_count(&session->roster); ++index)
            session->roster.racers[index].coin_count_178 = session->rival_coin_count_handoff;
    }
    /* The original lap cue is a one-frame presentation event: direct driver
     * memory after frames 4033/4034 is 0x0100/0, and after 6186/6187 is
     * 0x0080/0. Retire the previous event before this frame can raise one. */
    session->roster.racers[session->local_index].lap_marker_a4 &= UINT16_C(0xfe7f);
    session->recovery_coin_loss = 0u;
    portable_lap_hud_step(&session->lap_hud, session->runtime.session.max_progress_raw,
                          session->player.steering.state_flags);
    if (!tick_owned_hud(session))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    /* DA80 precedes driver movement and starts song194 on source player10.
     * Only the qualified normal GP profile owns this warning here. */
    if (session->source_coin_hud_enabled && session->source_scene_row_c88 >= 0 &&
        session->roster.race_mode_flags_7da == UINT16_C(0x100) &&
        portable_gp_coin_hud_warning(session->source_frame_clock,
                                     session->roster.racers[session->local_index].coin_count_178,
                                     session->player.steering.state_flags,
                                     session->roster.race_phase_7d4) &&
        !queue_race_audio(session, 194u, 10u, 256u, 0))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if (session->rules.progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1 &&
        session->runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED)
    {
        *disposition = PORTABLE_RACE_TICK_FINISHED;
        tick->completed = 1;
        return advance_gp_finish(session, tick->parts)
                   ? complete_item_art_tick(session, tick->parts, &tick->weather_input)
                   : PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    }
    if (session->water_recovery.phase != PORTABLE_WATER_RECOVERY_NONE)
    {
        session->previous_semantic_buttons = 0u;
        const PortableGpRaceSessionStatus recovery_status =
            tick_player_water_recovery(session, disposition);
        tick->completed = 1;
        return recovery_status == PORTABLE_GP_RACE_SESSION_OK
                   ? complete_item_art_tick(session, tick->parts, &tick->weather_input)
                   : recovery_status;
    }
    return PORTABLE_GP_RACE_SESSION_OK;
}

static PortableGpRaceSessionStatus prepare_player_controls(PortableGpRaceSession *session,
                                                           GpRaceTick *tick)
{
    /* GP owns the finished kart's throttle while presenting the result.
     * The retained original no-input continuation keeps driver +0x96 at
     * 0x8000 for all 549 frames through the retry-menu entry. */
    tick->source_control =
        session->runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED
            ? UINT16_C(0x8000)
            : portable_driver_control_word_from_semantic_buttons(tick->semantic_buttons);
    tick->launch_racing_tick =
        (uint8_t)(session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING);
    if (!tick->launch_racing_tick)
        tick_player_launch_revs(session, tick->source_control);
    tick->item_pressed =
        (uint8_t)((tick->semantic_buttons & PORTABLE_INPUT_ITEM) != 0u &&
                  (session->previous_semantic_buttons & PORTABLE_INPUT_ITEM) == 0u);
    tick->accelerate_pressed =
        (uint8_t)((tick->semantic_buttons & PORTABLE_INPUT_ACCELERATE) != 0u &&
                  (session->previous_semantic_buttons & PORTABLE_INPUT_ACCELERATE) == 0u);
    /* Driver +96 bit4 is the R rising edge, not a fresh accelerator press. */
    if ((tick->semantic_buttons & PORTABLE_INPUT_HOP) != 0u &&
        (session->previous_semantic_buttons & PORTABLE_INPUT_HOP) == 0u)
        tick->source_control |= UINT16_C(0x0004);
    session->previous_semantic_buttons = tick->semantic_buttons;
    tick->roulette_was_active = session->roster.racers[session->local_index].item_inventory.active;
    tick->player_contact_response = (uint8_t)(session->player.normal_collision_timer_38 != 0u);
    if (tick->player_contact_response)
        tick->source_control = session->player.input.control_word;
    if (!tick->player_contact_response)
        session->roster.racers[session->local_index].source_flags =
            session->player.steering.state_flags;
    tick->boundary_hold = (uint8_t)(session->player.normal_collision_timer_62 != 0u);
    tick->charged_exit_count = session->player.charged_exit_count;
    tick->start_boosts = session->player.start_boosts;
    tick->contact_acceleration_q16 = session->player.motion.acceleration_q16;
    tick->boundary_acceleration_q16 = session->player.motion.acceleration_q16;
    if (!queue_player_bump(session))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if (session->runtime.session.phase != PORTABLE_RACE_PHASE_COUNTDOWN &&
        !tick_player_protection(session, tick->accelerate_pressed))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    portable_gp_race_roster_cache_contact_center(&session->roster.racers[session->local_index]);
    /* The continuous GP source trace reaches sub_803AF10 when the next
     * running-race tick is 4 mod 8. Keep this GP entry phase out of the
     * generic driver runtime. */
    session->player.source_route_check_due =
        (uint8_t)(((session->runtime.session.elapsed_ticks + 1u) & UINT32_C(7)) == UINT32_C(4));
    /* GP +0x178 is one shared coin count, read by 7FD8 when it derives
     * +0xa8. The roster owns pickups and contact losses; pass its current
     * value to the player pilot before the source-order driver update. */
    session->player.input.speed_parameter =
        session->roster.racers[session->local_index].coin_count_178;
    session->player.source_route_index_90 =
        session->roster.racers[session->local_index].route_index;
    /* Hit/item writers and the player pilot address the same source74.
     * Carry roster changes into C4C0, then sync_player publishes its writes. */
    session->player.recovery_flags_74 =
        session->roster.racers[session->local_index].hit_state_flags_74;
    if (session->player.battle_enabled)
    {
        session->player.balloons_remaining =
            session->roster.racers[session->local_index].balloons_remaining;
        session->player.battle_scene_state = session->roster.battle_scene_state;
    }
    session->runtime.source_checkpoint_mode_row = session->roster.ai_mode_row_c64;
    session->prior_projected_y = (int16_t)(140 - session->player.pose.position_z / 65536);
    const int finish_lakitu_requested =
        session->finish_lakitu_rules != NULL &&
        (session->roster.racers[session->local_index].lap_marker_a4 & UINT16_C(0x4000)) != 0u;
    if (session->recovery_bob_offsets != NULL &&
        portable_gp_finish_lakitu_consume(
            &session->hazard_lakitu, &session->roster.racers[session->local_index].lap_marker_a4,
            session->finish_lakitu_rules) != 1)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if (finish_lakitu_requested)
        session->player.hop.recovery_requests &= UINT16_C(0xfb8f);
    if (session->recovery_bob_offsets != NULL &&
        portable_gp_recovery_lakitu_consume(&session->hazard_lakitu,
                                            &session->player.hop.recovery_requests,
                                            session->player.steering.state_flags) != 1)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    /* The prior 7FD8 item constructor queues B2 after the response pass.
     * Feed it into the next C4C0/A9AC turn, just like Time Trial and CPUs. */
    session->player.item_cue_flags_58 =
        session->roster.racers[session->local_index].hit_cue_flags_58;
    session->player.item_requests_b2 =
        session->roster.racers[session->local_index].item_response_flags_b2 & UINT16_C(0x8001);
    tick->laps_before_motion = session->runtime.session.laps_completed;
    session->runtime.course_props = &session->course_props;
    session->runtime.piranhas = &session->piranhas;
    tick->entry_splash_height = session->player.pose.position_z;
    return PORTABLE_GP_RACE_SESSION_OK;
}

static PortableGpRaceSessionStatus advance_player_runtime(PortableGpRaceSession *session,
                                                          PortableRaceTickDisposition *disposition,
                                                          GpRaceTick *tick)
{
    PortableRaceRuntimeStatus runtime_status;
    session->runtime.finished_inputs.coin_count =
        session->roster.racers[session->local_index].coin_count_178;
    runtime_status =
        portable_race_runtime_tick(&session->runtime, tick->source_control, disposition);
    if (runtime_status != PORTABLE_RACE_RUNTIME_OK)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if (session->recovery_bob_offsets != NULL && session->player.hop.recovery_started)
    {
        if (session->player.source_hazard_kind == PORTABLE_DRIVER_HAZARD_LAVA)
        {
            portable_entry_splash_start(&session->entry_splash, tick->entry_splash_height);
            session->hazard_effect_72 = 3u;
            session->player.burn_ticks_72 = PORTABLE_DRIVER_BURN_TICKS;
            if (!(session->player.steering.state_flags & UINT32_C(0x20)))
                portable_lava_smoke_start(&session->lava_smoke);
        }
        else if (session->player.source_terrain22_active)
        {
            portable_entry_splash_start(&session->entry_splash, tick->entry_splash_height);
            if (session->recovery_fx_rules)
            {
                if (!portable_recovery_fx_request(&session->recovery_fx, session->recovery_fx_rules,
                                                  &session->hazard_effect_72,
                                                  session->icy_recovery ? 11u : 13u,
                                                  !(session->player.steering.state_flags & 0x20u)))
                    return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
            }
            else
                session->hazard_effect_72 = 13u;
            session->player.burn_ticks_72 = 0u;
            session->player.palette_requests_194 &= UINT16_C(0x97ff);
        }
        ++session->runtime.session.rescue_count;
    }
    if (*disposition == PORTABLE_RACE_TICK_COUNTDOWN &&
        session->source_clock_enable_boundary == PORTABLE_GP_CLOCK_ENABLE_BEFORE_FINAL_COUNTDOWN &&
        session->runtime.session.countdown_remaining == 1u &&
        !portable_gp_source_clock_enable(&session->source_result_clock))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if (*disposition == PORTABLE_RACE_TICK_ADVANCE_MOTION &&
        session->runtime.session.elapsed_ticks == 1u && !session->source_result_clock.enabled &&
        !portable_gp_source_clock_enable(&session->source_result_clock))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if (session->runtime.session.laps_completed > tick->laps_before_motion &&
        !portable_gp_source_clock_record_lap(&session->source_result_clock,
                                             (uint8_t)tick->laps_before_motion))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    if (tick->launch_racing_tick)
        tick_player_launch_revs(session, tick->source_control);
    if (*disposition == PORTABLE_RACE_TICK_FINISHED)
    {
        tick->completed = 1;
        /* Battle's phase4 freezes all four karts for its180-tick transition.
         * GP's post-lap route/heading/acceleration path has no arena meaning. */
        if (session->player.battle_enabled)
            return advance_gp_finish(session, tick->parts)
                       ? complete_item_art_tick(session, tick->parts, &tick->weather_input)
                       : PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        /* Runtime has already advanced the shared autonomous driver owner. */
        sync_player(session);
        /* sub_803DA54 selects its racer from the original global frame
         * counter. The ready and fresh grids have different counter phase;
         * direct IWRAM captures pair it with each native launch above. */
        if ((session->source_frame_clock & UINT32_C(7)) == 0u &&
            (session->player.steering.state_flags & UINT32_C(0x20)) != 0u)
            session->player.source_steering_selector_9a = 3u;
        if (!request_contact_art_for(session, session->local_index))
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        /* The source continues updating rivals while the result scene is
         * visible, so their minimap markers must keep following live poses. */
        if (tick->single && tick_rivals(session) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        return advance_gp_finish(session, tick->parts)
                   ? complete_item_art_tick(session, tick->parts, &tick->weather_input)
                   : PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    }
    if (*disposition != PORTABLE_RACE_TICK_ADVANCE_MOTION)
    {
        tick->completed = 1;
        if (!request_contact_art_for(session, session->local_index))
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        return complete_item_art_tick(session, tick->parts, &tick->weather_input);
    }
    return PORTABLE_GP_RACE_SESSION_OK;
}

static PortableGpRaceSessionStatus settle_player_motion(PortableGpRaceSession *session,
                                                        GpRaceTick *tick)
{
    /* Coin objects occupy map cells. The source tests the updated driver
     * position, then records the new +0x178-derived +0xa8 in this frame.
     * Frame 7005 first enters (44,118), while frame 7004 is still (45,118). */
    if (session->runtime.session.phase == PORTABLE_RACE_PHASE_RACING &&
        session->runtime.session.elapsed_ticks != 0u)
    {
        if (!tick_player_coins(session, tick->ground_mode_before_motion))
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        session->player.input.speed_parameter =
            (uint8_t)session->roster.racers[session->local_index].coin_count_178;
        if (portable_driver_input_tick(&session->player.input, tick->source_control) !=
            PORTABLE_DRIVER_INPUT_OK)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        /* A pickup can move the source mode-0 target across the current
         * speed before +0xc0 is written. Recompute only that upward crossing;
         * the finish helper's negative branch is not the racing slowdown. */
        if (session->roster.racers[session->local_index].coin_count_178 !=
                tick->player_coins_before_motion &&
            session->player.steering.driver_mode == 0u &&
            session->player.motion.acceleration_q16 < 0 &&
            session->player.input.speed_target_q16 >= session->player.motion.speed_q16)
        {
            const uint32_t speed = (uint32_t)(session->player.motion.speed_q16 >> 16);
            const uint32_t bin = (speed > 1023u ? 1023u : speed) >> 6u;
            const uint32_t index =
                (uint32_t)session->player.auto_acceleration.character_class * 16u + bin;
            if (session->player_dependencies.source_angle_target_table == NULL ||
                index >= session->player_dependencies.source_angle_target_table_count)
                return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
            session->player.motion.acceleration_q16 =
                (int32_t)(session->player_dependencies.source_angle_target_table[index]) << 12u;
            /* The pickup precedes 7B30's 8298/8710 dispatch in the source.
             * Native motion has already dispatched against the old target,
             * so an upward crossing must also run 8640's speed-indexed
             * response step. Source frame 2870: 8AFC gives 0xf5d0 and
             * 8640 changes it to 0xf5d5 before frame 2871 composes heading. */
            if ((tick->source_control & UINT16_C(0xc000)) == UINT16_C(0x8000) &&
                session->player.steering.response_status <= 4u &&
                session->player.motion.speed_q16 < session->player.input.speed_target_q16)
            {
                PortableDriverForwardAccelerationState response =
                    session->player.forward_acceleration;
                response.speed_q16 = session->player.motion.speed_q16;
                response.state_flags = session->player.steering.response_flags;
                if (portable_driver_forward_acceleration_tick(
                        &response, session->player_dependencies.forward_response_curve,
                        session->player_dependencies.forward_response_count,
                        session->player_dependencies.forward_local_table,
                        session->player_dependencies.forward_local_count) !=
                    PORTABLE_DRIVER_FORWARD_ACCELERATION_OK)
                    return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
                session->player.steering.response_q8 = response.response_q8;
                session->player.forward_acceleration.response_q8 = response.response_q8;
            }
            /* 8E34 case 0 also sees the post-pickup target. If the old
             * target armed status 1 during the native pilot, undo only that
             * premature arm. Source frame 1905 remains status 0; frame
             * 1906 arms status 1 and frame 1907 begins its turn step. */
            if (tick->steering_status_before_motion == 0u &&
                session->player.steering.response_status == 1u)
            {
                session->player.steering.response_status = 0u;
                session->player.steering.drift_angle = tick->drift_angle_before_motion;
            }
        }
    }

    /* C4C0 runs 9B38 before draining AC. Pickup and its speed target
     * observe the pre-drain count; each64th AC then drops one coin before
     * decrement, without recomputing that already-published speed target. */
    if ((int16_t)session->player.item_lock_ac != 0)
    {
        if (session->player.invulnerability_timer_6a)
            session->player.item_lock_ac = 0u;
        else
        {
            if (!(session->player.item_lock_ac & 63u) &&
                portable_gp_race_session_apply_coin_loss(
                    session, PORTABLE_COIN_EFFECT_CAUSE_HAUNT) != PORTABLE_GP_RACE_SESSION_OK)
                return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
            --session->player.item_lock_ac;
        }
        session->roster.racers[session->local_index].item_lock_ac = session->player.item_lock_ac;
    }
    /* During B550's +0x62 hold, C4C0 skips sub_8038200 and retains +0xc0.
     * The frame-2093 source object still has 0x00080000 after the rebound. */
    if (tick->boundary_hold)
        session->player.motion.acceleration_q16 = tick->boundary_acceleration_q16;

    if (session->player.impact_event &&
        portable_gp_driving_quality_record_event(
            &session->driving_quality,
            session->player.impact_event == 1u ? PORTABLE_GP_DRIVING_EVENT_18
                                               : PORTABLE_GP_DRIVING_EVENT_19,
            session->roster.race_mode_flags_7da,
            session->player.steering.state_flags) != PORTABLE_GP_DRIVING_QUALITY_OK)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;

    if (session->runtime.water_entry && !begin_player_water_recovery(session))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;

    if (session->player.hop.recovery_touchdown &&
        portable_gp_driving_quality_record_event(
            &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_17,
            session->roster.race_mode_flags_7da,
            session->player.steering.state_flags) != PORTABLE_GP_DRIVING_QUALITY_OK)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;

    /* A9AC's start boost calls 349C for the local GP player. */
    if (session->player.start_boosts != tick->start_boosts &&
        portable_gp_driving_quality_record_event(
            &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_1B,
            session->roster.race_mode_flags_7da,
            session->roster.racers[session->local_index].source_flags) !=
            PORTABLE_GP_DRIVING_QUALITY_OK)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;

    if (session->player.charged_exit_count != tick->charged_exit_count &&
        portable_gp_driving_quality_record_event(
            &session->driving_quality, PORTABLE_GP_DRIVING_EVENT_1C,
            session->roster.race_mode_flags_7da,
            session->roster.racers[session->local_index].source_flags) !=
            PORTABLE_GP_DRIVING_QUALITY_OK)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;

    if (portable_gp_driving_quality_tick(
            &session->driving_quality, session->roster.race_mode_flags_7da,
            session->roster.racers[session->local_index].source_flags,
            session->player.input.control_word,
            session->player.source_material_valid != 0u ? session->player.source_material_85
                                                        : 0u) != PORTABLE_GP_DRIVING_QUALITY_OK)
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;

    /* +0x4000 bypasses the source motion writer while +0x38 is active.  The
     * generic pilot still evaluates the held-A acceleration path so it can be
     * resumed later; keep the source driver's current +0xc0 unchanged during
     * the bounded kart-contact response. */
    if (tick->player_contact_response)
        session->player.motion.acceleration_q16 = tick->contact_acceleration_q16;
    else if (session->player_contact_release_pending)
    {
        /* The first tick after +0x38 reaches zero integrates the retained
         * collision acceleration, then sub_803DAEC installs the packed normal
         * response output for the following motion tick. */
        session->player_contact_release_pending = 0u;
        session->player_contact_response_active = 1u;
        if (!advance_player_kart_contact_response(session, tick->contact_acceleration_q16))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    else if (session->player_contact_response_active)
    {
        if (!advance_player_kart_contact_response(session, tick->contact_acceleration_q16))
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    else if (session->player_contact_clear_status_pending)
    {
        session->player.steering.response_status = 0u;
        session->player.normal_collision.response_timer_7e = 0u;
        session->player.normal_collision_source_from_native_state = 0u;
        session->player.normal_collision_source_use_b550_table = 0u;
        session->player_contact_clear_status_pending = 0u;
    }
    return PORTABLE_GP_RACE_SESSION_OK;
}

static PortableGpRaceSessionStatus advance_items_and_rivals(PortableGpRaceSession *session,
                                                            GpRaceTick *tick)
{
    sync_player(session);
    if (!request_contact_art_for(session, session->local_index))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    tick_player_recovery_reset(session);
    session->failed_roster_stage = 1u;
    if (!tick_player_roulette(session, tick->item_pressed) ||
        !tick_race_items(session, (uint8_t)(tick->item_pressed && !tick->roulette_was_active),
                         tick->parts) ||
        (tick->single && !tick_opponent_roulette(session)))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    session->failed_roster_stage = 2u;
    if (tick->single && !tick_item_contacts_and_responses(session))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    adopt_player_bomb_respawn(session);
    session->failed_roster_stage = 3u;
    /* play.c calls sub_804AEFC/col_collide before sub_804A878 advances
     * drivers. The player's item-box contact therefore uses the pose saved
     * at this frame's entry, while rival poses have not yet advanced here. */
    if (tick->single && !tick_item_boxes(session, 1u, tick->x_before_motion, tick->y_before_motion))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    session->failed_roster_stage = 4u;
    session->roster.cpu_item_clock = session->source_frame_clock;
    session->failed_roster_stage = 5u;
    /* The retained ready-grid state advances its first rival frame with
     * racing tick one; the earlier fresh-grid state defers that pass. The
     * same source launch boundary also selects the GP clock phase above. */
    if (tick->single &&
        (session->runtime.session.elapsed_ticks != 1u ||
         session->source_clock_enable_boundary ==
             PORTABLE_GP_CLOCK_ENABLE_BEFORE_FINAL_COUNTDOWN) &&
        tick_rivals(session) != PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    session->player.steering.state_flags |=
        session->roster.racers[session->local_index].source_flags & UINT32_C(0x000c);
    session->failed_roster_stage = 6u;
    if (!advance_gp_finish(session, tick->parts))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    if (!tick_race_voices(session, tick->parts))
        return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    return complete_item_art_tick(session, tick->parts, &tick->weather_input);
}

static PortableGpRaceSessionStatus tick_session(PortableGpRaceSession *session,
                                                uint16_t semantic_buttons,
                                                PortableRaceTickDisposition *disposition)
{
    GpRaceTick tick = {.semantic_buttons = semantic_buttons};
    PortableGpRaceSessionStatus status;

    /* World producers precede the player; entry-pose box contacts and rival
     * updates remain after player publication. Recovery/countdown/finish own
     * their early completion paths and must not enter the later phases. */
    status = prepare_race_world(session, disposition, &tick);
    if (status != PORTABLE_GP_RACE_SESSION_OK || tick.completed)
        return status;
    status = prepare_player_controls(session, &tick);
    if (status != PORTABLE_GP_RACE_SESSION_OK)
        return status;
    status = advance_player_runtime(session, disposition, &tick);
    if (status != PORTABLE_GP_RACE_SESSION_OK || tick.completed)
        return status;
    status = settle_player_motion(session, &tick);
    if (status != PORTABLE_GP_RACE_SESSION_OK)
        return status;
    status = advance_items_and_rivals(session, &tick);
    if (status != PORTABLE_GP_RACE_SESSION_OK)
        return status;
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus portable_gp_race_session_tick(PortableGpRaceSession *session,
                                                          uint16_t semantic_buttons,
                                                          PortableRaceTickDisposition *disposition)
{
    if (session != NULL && multi_local(session))
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (session != NULL)
        session->roster.script_hooks = session->script_hooks;
    const PortableGpRaceSessionStatus status = tick_session(session, semantic_buttons, disposition);
    if (status == PORTABLE_GP_RACE_SESSION_OK && session->script_hooks != NULL &&
        session->script_hooks->after_tick != NULL)
        session->script_hooks->after_tick(session->script_hooks->context);
    return status;
}

PortableGpRaceSessionStatus
portable_gp_race_session_grant_player_item(PortableGpRaceSession *session, uint16_t item)
{
    if (session == NULL || item < PORTABLE_GP_GREEN_SHELL_ITEM || item > UINT16_C(0x100c))
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    PortableGpRacer *player = &session->roster.racers[session->local_index];
    if (player->item_inventory.held_item_17a != 0u || player->item_inventory.active ||
        player->shell_item_18c != 0u || player->item_use_state_17c != 0u ||
        (player->source_flags & UINT32_C(0x20)) != 0u ||
        session->runtime.session.phase == PORTABLE_RACE_PHASE_FINISHED)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    player->item_inventory.held_item_17a = item;
    portable_gp_item_hud_transition(&session->roster.item_hud, 0u, item);
    return PORTABLE_GP_RACE_SESSION_OK;
}

/* The session's player fields, and the roster's local HUD and bump cue,
 * belong to whichever local is selected; the others wait in locals[]. */
#define LOCAL_SESSION_FIELDS(X)                                                                    \
    X(player_dependencies)                                                                         \
    X(player) X(runtime) X(sky_motion) X(character_camera) X(finish_camera) X(driving_quality)     \
        X(water_recovery) X(hazard_lakitu) X(hazard_effect_72) X(prior_projected_y)                \
            X(coin_birth_x) X(coin_birth_y) X(coin_birth_inverse) X(coin_pickups) X(lava_smoke)    \
                X(recovery_fx) X(surface_trails) X(coin_effects) X(recovery_fx_events)             \
                    X(entry_splash) X(source_result_clock) X(rank_hud) X(coin_hud) X(lap_hud)      \
                        X(launch_revs_92) X(launch_rev_mode_98) X(launch_prelude_applied)          \
                            X(player_bomb_walk_audio) X(recovery_coin_loss)                        \
                                X(previous_semantic_buttons) X(player_contact_release_pending)     \
                                    X(player_contact_response_active)                              \
                                        X(player_contact_clear_status_pending) X(voice_rank_ready) \
                                            X(voice_previous_rank)
#define LOCAL_ROSTER_FIELDS(X) X(item_hud) X(player_bump_pending) X(player_bump_speed_q16)

static void exchange_bytes(void *first, void *second, size_t size, int swap)
{
    unsigned char *a = first, *b = second;
    for (size_t i = 0u; i < size; ++i)
    {
        const unsigned char t = a[i];
        a[i] = b[i];
        if (swap)
            b[i] = t;
    }
}

/* swap=0 copies the session's local into *local (used at init). */
static void exchange_local(PortableGpRaceSession *session, PortableGpLocalState *local, int swap)
{
#define LOCAL_SESSION_FIELD(f)                                                                     \
    (void)sizeof(char[sizeof session->f == sizeof local->f ? 1 : -1]);                             \
    exchange_bytes(&local->f, &session->f, sizeof local->f, swap);
#define LOCAL_ROSTER_FIELD(f)                                                                      \
    (void)sizeof(char[sizeof session->roster.f == sizeof local->f ? 1 : -1]);                      \
    exchange_bytes(&local->f, &session->roster.f, sizeof local->f, swap);
    LOCAL_SESSION_FIELDS(LOCAL_SESSION_FIELD)
    LOCAL_ROSTER_FIELDS(LOCAL_ROSTER_FIELD)
#undef LOCAL_SESSION_FIELD
#undef LOCAL_ROSTER_FIELD
}

PortableGpRaceSessionStatus portable_gp_race_session_select_local(PortableGpRaceSession *session,
                                                                  uint8_t local)
{
    if (session == NULL || local >= (multi_local(session) ? session->local_count : 1u))
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (session->local_index == local)
        return PORTABLE_GP_RACE_SESSION_OK;
    if (session->local_index != 0u)
        exchange_local(session, &session->locals[session->local_index - 1u], 1);
    if (local != 0u)
        exchange_local(session, &session->locals[local - 1u], 1);
    session->local_index = local;
    session->roster.local_racer = local;
    return PORTABLE_GP_RACE_SESSION_OK;
}

static PortableGpRaceSessionStatus init_local(PortableGpRaceSession *session,
                                              const PortableRecoveredRaceView *race,
                                              uint8_t driver_class, uint8_t grid_row,
                                              uint8_t engine_class, PortableQuickRunOptions options,
                                              int preserve_locals)
{
    uint8_t ranks[PORTABLE_GP_RACER_COUNT] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    ranks[0] = grid_row;
    ranks[grid_row] = 0u;
    PortableRecoveredRaceView selected = *race;
    if (race->rules.progress_kind != PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1)
    {
        selected.rules.lap_count = options.five_laps ? 5u : 3u;
        if (options.items_off)
        {
            selected.course_objects = NULL;
            selected.course_object_count = 0u;
        }
        if (options.coins_off)
        {
            selected.coins = NULL;
            selected.coin_count = 0u;
        }
    }
    return init_engine_grid_order(session, &selected, driver_class, ranks, engine_class,
                                   preserve_locals);
}

PortableGpRaceSessionStatus
portable_gp_race_session_init_locals(PortableGpRaceSession *session,
                                     const PortableRecoveredRaceView *const races[],
                                     const uint8_t driver_classes[], uint8_t local_count,
                                     uint8_t engine_class, PortableQuickRunOptions options)
{
    if (session == NULL || races == NULL || driver_classes == NULL || local_count < 2u ||
        local_count > PORTABLE_GP_MAX_LOCALS || options.five_laps > 1u || options.items_off > 1u ||
        options.coins_off > 1u || options.mushroom_start > 1u)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    for (uint8_t k = 0u; k < local_count; ++k)
        if (races[k] == NULL ||
            races[k]->rules.compatibility.course_id != races[0]->rules.compatibility.course_id ||
            races[k]->rules.progress_kind != races[0]->rules.progress_kind)
            return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    const int battle = races[0]->rules.progress_kind == PORTABLE_RACE_PROGRESS_ARENA_RASTER_V1;
    for (uint8_t k = (uint8_t)(local_count - 1u); k != 0u; --k)
    {
        const PortableGpRaceSessionStatus status =
            init_local(session, races[k], driver_classes[k], k, engine_class, options,
                       k != local_count - 1u);
        if (status != PORTABLE_GP_RACE_SESSION_OK)
            return status;
        exchange_local(session, &session->locals[k - 1u], 0);
    }
    const PortableGpRaceSessionStatus status =
        init_local(session, races[0], driver_classes[0], 0u, engine_class, options, 1);
    if (status != PORTABLE_GP_RACE_SESSION_OK)
        return status;
    if (portable_gp_race_roster_set_locals(&session->roster, driver_classes, local_count) !=
        PORTABLE_GP_ROSTER_OK)
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    session->local_count = local_count;
    session->quick_run_options = battle ? (PortableQuickRunOptions){0} : options;
    session->roster.race_mode_flags_7da = battle ? UINT16_C(0x0800) : UINT16_C(0x0400);
    session->roster.cpu_items_enabled = 0u;
    for (uint8_t k = 0u; k < local_count; ++k)
    {
        PortableGpRacer *racer = &session->roster.racers[k];
        racer->voice_override = races[k]->voice_override;
        racer->coin_count_178 =
            battle || options.coins_off ? 0u : (uint16_t)((racer->rank_flags_ba >> 2u) + 2u);
        if (portable_gp_race_session_select_local(session, k) != PORTABLE_GP_RACE_SESSION_OK)
            return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
        session->player.balloons_remaining = racer->balloons_remaining;
        session->player.recovery_flags_74 = racer->hit_state_flags_74;
        sync_player(session);
        /* sub_8048D44: VS mushroom start hands every racer item 0x1008. */
        if (!battle && options.mushroom_start &&
            portable_gp_race_session_grant_player_item(session, UINT16_C(0x1008)) !=
                PORTABLE_GP_RACE_SESSION_OK)
            return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    }
    return portable_gp_race_session_select_local(session, 0u);
}

/* The shared world after every local's pass: item actors and their hits,
 * boxes, standings and the VS/Battle finish. Local 0 is selected. */
static PortableGpRaceSessionStatus tick_world(PortableGpRaceSession *session,
                                              const PortableSnowWeatherInputs *weather_input)
{
    const PortableGpRacer *first = &session->roster.racers[0];
    if (!tick_race_items(session, 0u, TICK_WORLD) || !tick_item_contacts_and_responses(session))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    if (session->runtime.session.phase != PORTABLE_RACE_PHASE_COUNTDOWN)
    {
        if (!tick_item_boxes(session, 1u, first->position_x_q16, first->position_y_q16))
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
        session->roster.cpu_item_clock = session->source_frame_clock;
        if (tick_rivals(session) != PORTABLE_GP_ROSTER_OK)
            return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    }
    if (!advance_gp_finish(session, TICK_WORLD) || !tick_race_voices(session, TICK_WORLD))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    return complete_item_art_tick(session, TICK_WORLD, weather_input);
}

PortableGpRaceSessionStatus
portable_gp_race_session_tick_locals(PortableGpRaceSession *session, const uint16_t buttons[],
                                     PortableRaceTickDisposition *disposition)
{
    if (session == NULL || buttons == NULL || disposition == NULL)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    if (!multi_local(session))
        return portable_gp_race_session_tick(session, buttons[0], disposition);
    if (session->local_index != 0u)
        return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
    const PortableSnowWeatherInputs weather_input =
        snow_weather_inputs(session, session->source_frame_clock + 1u);
    session->roster.script_hooks = session->script_hooks;
    clear_tick_requests(session);
    ++session->source_frame_clock;
    if (session->roster.lightning_duration_c82)
        --session->roster.lightning_duration_c82;
    (void)portable_gp_item_random(&session->source_rng_state);
    if (!collide_racer_karts(session, session->source_frame_clock))
        return PORTABLE_GP_RACE_SESSION_ROSTER_ERROR;
    PortableRaceTickDisposition first = PORTABLE_RACE_TICK_INVALID;
    for (uint8_t k = 0u; k < session->local_count; ++k)
    {
        PortableRaceTickDisposition local = PORTABLE_RACE_TICK_INVALID;
        PortableGpRaceSessionStatus status = portable_gp_race_session_select_local(session, k);
        if (status == PORTABLE_GP_RACE_SESSION_OK)
            status = tick_session(session, session->vs_retired[k] ? 0u : buttons[k], &local);
        if (status == PORTABLE_GP_RACE_SESSION_OK && session->vs_retired[k])
            session->player.motion.speed_q16 = 0;
        if (k == 0u)
            first = local;
        if (status != PORTABLE_GP_RACE_SESSION_OK)
        {
            (void)portable_gp_race_session_select_local(session, 0u);
            return status;
        }
    }
    const PortableGpRaceSessionStatus status = portable_gp_race_session_select_local(session, 0u);
    if (status != PORTABLE_GP_RACE_SESSION_OK)
        return status;
    *disposition = first;
    const PortableGpRaceSessionStatus world_status = tick_world(session, &weather_input);
    if (world_status == PORTABLE_GP_RACE_SESSION_OK)
    {
        /* The source global FX pass follows all local drivers and actors.
         * Each local keeps its own pool and cached projection, advanced once. */
        for (uint8_t k = 0u; k < session->local_count; ++k)
        {
            if (portable_gp_race_session_select_local(session, k) != PORTABLE_GP_RACE_SESSION_OK ||
                !portable_gp_race_session_tick_dropped_coins(session) ||
                (session->player_dependencies.impact_effect_rules &&
                 !portable_impact_effect_tick(&session->player.impact_effects,
                                              session->player_dependencies.impact_effect_rules)))
            {
                (void)portable_gp_race_session_select_local(session, 0u);
                return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
            }
        }
        if (portable_gp_race_session_select_local(session, 0u) != PORTABLE_GP_RACE_SESSION_OK)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
    }
    if (world_status == PORTABLE_GP_RACE_SESSION_OK && session->script_hooks != NULL &&
        session->script_hooks->after_tick != NULL)
        session->script_hooks->after_tick(session->script_hooks->context);
    return world_status;
}

int portable_gp_race_session_locals_finished(const PortableGpRaceSession *session)
{
    return session != NULL && multi_local(session) &&
           (session->roster.race_phase_7d4 & UINT8_C(0x0f)) == 5u;
}

PortableGpRaceSessionStatus portable_gp_race_session_snapshot(const PortableGpRaceSession *session,
                                                              PortableGpRaceSnapshot *snapshot)
{
    if (session == NULL || snapshot == NULL ||
        session->runtime.recovered_driver != &session->player)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->player_x_q16 = session->player.pose.position_x;
    snapshot->player_y_q16 = session->player.pose.position_y;
    snapshot->player_height_q16 = session->player.pose.position_z;
    snapshot->player_speed_q16 = session->player.motion.speed_q16;
    snapshot->engine_sound = portable_driver_engine_sound_input(&session->player);
    snapshot->engine_sound.ice_thaw_started =
        (uint8_t)((session->recovery_fx_events & PORTABLE_RECOVERY_FX_THAW_START) != 0u);
    if (session->recovery_fx_rules)
        snapshot->engine_sound.water_rescue_arrived =
            (uint8_t)(session->player.source_terrain22_active &&
                      session->hazard_lakitu.retrieval_effect_started);
    if (session->roster.racers[session->local_index].elimination.revenge_bomb)
    {
        snapshot->engine_sound.character_flags |= 8u;
        snapshot->engine_sound.bomb_walk_audio = session->player_bomb_walk_audio;
    }
    snapshot->engine_revs = session->launch_revs_92;
    snapshot->race_audio_mode = session->roster.race_mode_flags_7da;
    snapshot->engine_revs_owned = 1u;
    snapshot->engine_scene_stop = (uint8_t)((session->roster.race_phase_7d4 & 0x40u) != 0u);
    snapshot->course_props = session->course_props;
    snapshot->mice = session->mice;
    snapshot->thunderclouds = session->thunderclouds;
    snapshot->falling_stars = session->falling_stars;
    snapshot->contact_audio = session->player.contact_audio;
    snapshot->boost_voice = session->player.boost_voice;
    memcpy(snapshot->coin_pickups, session->coin_pickups, sizeof snapshot->coin_pickups);
    snapshot->lava_smoke = session->lava_smoke;
    snapshot->recovery_fx = session->recovery_fx;
    snapshot->surface_trails = session->surface_trails;
    snapshot->coin_effects = session->coin_effects;
    snapshot->impact_effects = session->player.impact_effects;
    snapshot->player_kart_contacts =
        session->roster.racers[session->local_index].kart_contact_events;
    snapshot->recovery_effect_72 = session->hazard_effect_72;
    snapshot->recovery_fx_events = session->recovery_fx_events;
    snapshot->entry_splash = session->entry_splash;
    snapshot->boost_particle_age = session->player.boost_particle_age;
    snapshot->modern_drift_enabled = (uint8_t)(session->player_dependencies.modern_drift_rules != NULL);
    snapshot->modern_drift = session->player.modern_drift;
    snapshot->modern_air_enabled = (uint8_t)(snapshot->modern_drift_enabled &&
        session->player_dependencies.modern_drift_rules->air.launch_mask != 0u);
    snapshot->modern_air = session->player.modern_air;
    snapshot->modern_drift_release_serial = snapshot->modern_drift_enabled ? session->player.charged_audio_cue_count : 0u;
    snapshot->boost_particle_birth_height_q16 = session->player.boost_particle_birth_height_q16;
    snapshot->cannonball_sounds = session->cannonball_sounds;
    snapshot->piranha_sounds = session->piranha_sounds;
    snapshot->thwomp_sounds = session->thwomp_sounds;
    snapshot->prop_sounds = session->prop_sounds;
    snapshot->impact_sounds = session->impact_sounds;
    snapshot->item_hud = session->roster.item_hud.displayed_item;
    snapshot->rank_hud = session->rank_hud;
    snapshot->coin_hud = session->coin_hud;
    snapshot->lap_hud = session->lap_hud;
    snapshot->player_flattened = session->player.flattened_64;
    snapshot->player_affine = session->player.affine;
    snapshot->player_art_motion = session->player.art_motion;
    snapshot->sky_motion = session->sky_motion.snapshot;
    snapshot->ambient = session->ambient.snapshot;
    snapshot->ambient_digest = portable_ambient_digest(&session->ambient);
    snapshot->snow_weather = session->snow_weather.snapshot;
    snapshot->snow_weather_digest = portable_snow_weather_digest(&session->snow_weather);
    snapshot->main_visual_rng_state = session->main_visual_rng_state;
    snapshot->player_heading = (uint16_t)session->player.motion.angle;
    snapshot->camera_yaw = race_camera_yaw(session);
    snapshot->finish_camera = session->finish_camera;
    snapshot->finished_target_angle_ca = session->player.finished_target_angle_ca;
    snapshot->finished_initialized = session->player.finished_initialized;
    snapshot->finished_route_row_191 = session->player.finished_route_row_191;
    const PortableGpRacer *view_racer = &session->roster.racers[session->local_index];
    const PortableCharacterViewInput view = {
        .selected = 1u,
        .source_flags = session->player.steering.state_flags,
        .contact_mode_6e = view_racer->contact_mode_6e,
        .contact_phase_6f = view_racer->contact_phase_6f,
        .contact_frame_6c = view_racer->contact_frame_6c,
        .contact_flags_6d = view_racer->contact_flags_6d,
        .camera_adjustment = session->character_camera.current_adjustment,
        .camera_yaw = snapshot->camera_yaw,
        .driver_angle = session->player.steering.source_angle_56,
        .steering_response_q8 = session->player.steering.response_q8,
        .speed_q16 = snapshot->player_speed_q16,
        .position_82 = session->player.steering.position_82};
    snapshot->player_art_pose = portable_character_view_pose(&view, session->view_rules);
    snapshot->player_visual_yaw = portable_character_visual_yaw(&view, session->view_rules);
    snapshot->player_view_angle = snapshot->player_art_pose.angle;
    snapshot->player_star_uses = session->roster.player_star_uses;
    snapshot->lightning_uses = session->roster.lightning_events;
    snapshot->lightning_scene_active =
        (uint8_t)(session->roster.lightning_state_80d == 2u &&
                  session->roster.lightning_duration_c82 >
                      portable_item_status_flash_end(session->roster.item_status_rules));
    snapshot->lightning_scene_white =
        (uint8_t)(snapshot->lightning_scene_active &&
                  ((session->roster.item_status_rules->lightning_ticks -
                    session->roster.lightning_duration_c82) &
                   4u) != 0u);
    snapshot->lightning_music_active = (uint8_t)(session->roster.lightning_duration_c82 != 0u);
    snapshot->music_finished =
        (uint8_t)((session->roster.racers[session->local_index].source_flags & 0x20u) != 0u);
    snapshot->music_final_lap =
        (uint8_t)((session->roster.racers[session->local_index].source_flags & 8u) != 0u);
    snapshot->player_boo_active =
        (uint8_t)(session->roster.racers[session->local_index].contact_status_66 != 0);
    snapshot->player_star_active =
        (uint8_t)(session->roster.racers[session->local_index].lightning_status_6a != 0);
    uint16_t palette_requests = 0u;
    snapshot->player_palette_bank = portable_driver_palette_select(
        &palette_requests, session->roster.racers[session->local_index].lightning_status_6a,
        (int16_t)session->roster.racers[session->local_index].lightning_timer_68, 0u, 0u,
        session->source_frame_clock);
    if (snapshot->player_palette_bank == 0u && session->player.palette_bank >= 5u)
        snapshot->player_palette_bank = session->player.palette_bank;
    snapshot->phase = session->runtime.session.phase;
    snapshot->laps_completed = session->runtime.session.laps_completed;
    snapshot->lap_count = session->runtime.session.lap_count;
    snapshot->countdown_remaining = session->runtime.session.countdown_remaining;
    snapshot->player_route_index = session->roster.racers[session->local_index].route_index;
    snapshot->balloon_tick = session->source_frame_clock;
    snapshot->player_revenge_bomb =
        session->roster.racers[session->local_index].elimination.revenge_bomb;
    snapshot->player_bomb_walk_phase =
        session->roster.racers[session->local_index].contact_flags_6d >> 1u;
    snapshot->player_bomb_response = session->roster.racers[session->local_index].hit_counter_88;
    snapshot->player_balloon_heading =
        session->roster.racers[session->local_index].contact_heading_56;
    snapshot->player_balloons_remaining =
        session->roster.racers[session->local_index].balloons_remaining;
    snapshot->player_rank =
        (uint8_t)(session->roster.racers[session->local_index].rank_flags_ba >> 1u);
    snapshot->finish_transition_phase = (uint8_t)(session->roster.race_phase_7d4 & UINT8_C(0x0f));
    snapshot->results_ready = (uint8_t)(snapshot->phase == PORTABLE_RACE_PHASE_FINISHED &&
                                        snapshot->finish_transition_phase == 5u);
    /* A GP race starts on the grid, so its HUD clock begins with the first
     * racing frame. The shared Time Trial helper deliberately returns zero
     * until the flying-start line crossing and is not the GP clock. */
    snapshot->total_ticks = session->runtime.session.elapsed_ticks;
    snapshot->marker_clock = session->marker_display_clock;
    snapshot->marker_source_flags = session->player.steering.state_flags;
    snapshot->marker_class_flags = session->roster.racers[session->local_index].driver_class;
    snapshot->source_clock_centiseconds = session->source_result_clock.centiseconds;
    snapshot->source_clock_phase = session->source_result_clock.phase;
    memcpy(snapshot->source_result_times_centiseconds,
           session->source_result_clock.lap_centiseconds,
           sizeof session->source_result_clock.lap_centiseconds);
    snapshot->source_result_times_ready = (uint8_t)portable_gp_source_clock_times(
        &session->source_result_clock, snapshot->source_result_times_centiseconds);
    snapshot->rescue_count = session->runtime.session.rescue_count;
    snapshot->recovery_phase = session->water_recovery.phase;
    snapshot->recovery_elapsed_ticks = session->water_recovery.elapsed_ticks;
    snapshot->recovery_phase_ticks = session->water_recovery.phase_ticks;
    if (session->recovery_bob_offsets != NULL)
    {
        snapshot->recovery_phase =
            session->player.source_terrain22_active && session->player.hop.mode == 4u
                ? PORTABLE_WATER_RECOVERY_SINK
                : PORTABLE_WATER_RECOVERY_NONE;
        snapshot->recovery_phase_ticks =
            snapshot->recovery_phase == PORTABLE_WATER_RECOVERY_SINK
                ? (uint16_t)(144u - session->player.drift_charge.charge_counter)
                : 0u;
    }
    snapshot->lakitu_state = portable_gp_lakitu_source_state(&session->hazard_lakitu);
    snapshot->lakitu_scale_q8 = portable_gp_lakitu_scale(&session->hazard_lakitu);
    snapshot->lakitu_x = session->hazard_lakitu.x;
    snapshot->lakitu_y = session->hazard_lakitu.y;
    snapshot->lakitu_visible = (uint8_t)(session->hazard_lakitu.visible & 1u);
    snapshot->lakitu_art_bank = session->hazard_lakitu.art_bank;
    snapshot->fall_mode = session->player.source_hazard_kind ? session->player.hop.mode : 0u;
    snapshot->fall_timer =
        session->player.source_hazard_kind ? session->player.drift_charge.charge_counter : 0u;
    snapshot->fall_frames_76 = session->player.fall_frames_76;
    snapshot->driver_hidden = (uint8_t)!portable_driver_effects_visible(&session->player);
    snapshot->audio_event_count = session->audio_event_count;
    memcpy(snapshot->audio_events, session->audio_events, sizeof snapshot->audio_events);
    memcpy(snapshot->lap_ticks, session->runtime.session.lap_ticks, sizeof snapshot->lap_ticks);
    for (size_t index = 0u; index < PORTABLE_GP_OPPONENT_COUNT; ++index)
    {
        const size_t other = portable_gp_race_session_other_racer(session, index);
        const PortableGpRacer *racer = &session->roster.racers[other];
        PortableGpOpponentSnapshot *opponent = &snapshot->opponents[index];
        opponent->fall_audio_serial = racer->fall_audio_serial;
        opponent->fall_audio_variant = racer->fall_audio_variant;
        opponent->revenge_bomb = racer->elimination.revenge_bomb;
        opponent->bomb_walk_phase = racer->contact_flags_6d >> 1u;
        opponent->bomb_response = racer->hit_counter_88;
        opponent->speed_q16 = racer->speed_q16;
        opponent->balloons_remaining = racer->balloons_remaining;
        opponent->balloon_heading = racer->contact_heading_56;
        opponent->position_x_q16 = racer->position_x_q16;
        opponent->position_y_q16 = racer->position_y_q16;
        /* +24 is projected contact depth, not world Z. Publish the jump
         * consumer's +8 so ordinary GP rendering observes actual airtime. */
        opponent->height_q16 = racer->world_height_q16;
        opponent->heading = racer->heading;
        opponent->source_angle = racer->source_heading;
        opponent->steering_response = (int16_t)racer->hit_counter_88;
        opponent->contact_mode = racer->contact_mode_6e;
        opponent->contact_phase = racer->contact_phase_6f;
        opponent->contact_frame = racer->contact_frame_6c;
        opponent->contact_flags = racer->contact_flags_6d;
        opponent->driver_class = racer->driver_class;
        opponent->marker_source_flags = racer->source_flags;
        opponent->voice_class = portable_gp_racer_voice_class(racer);
        opponent->star_active = (uint8_t)(racer->lightning_status_6a != 0);
        palette_requests = 0u;
        opponent->palette_bank = portable_driver_palette_select(
            &palette_requests, racer->lightning_status_6a, (int16_t)racer->lightning_timer_68, 0u,
            0u, session->source_frame_clock);
        if (opponent->palette_bank == 0u && racer->palette_bank >= 5u)
            opponent->palette_bank = racer->palette_bank;
        opponent->flattened = racer->flattened_64;
        opponent->affine = racer->affine;
        opponent->art_motion = racer->art_motion;
        opponent->visible = (uint8_t)(other < portable_gp_race_roster_count(&session->roster));
        opponent->fall_frames_76 = racer->fall_frames_76;
        opponent->driver_hidden =
            (uint8_t)portable_driver_hidden(racer->lightning_state_78, racer->world_height_q16,
                                            racer->fall_frames_76, racer->recovery_timer_9c);
        opponent->engine_sound = (PortableDriverEngineSoundInput){
            .state_flags = racer->source_flags,
            .height_q16 = racer->world_height_q16,
            .speed_q16 = racer->speed_q16,
            .driver_mode = racer->mushroom_mode_84,
            .lightning_timer = (int16_t)racer->lightning_timer_68,
            .character_flags = (uint8_t)(racer->elimination.revenge_bomb ? 8u : 0u),
            .bomb_walk_audio = racer->bomb_walk_audio,
        };
        if (opponent->visible)
        {
            const PortableSpatialAudioCamera camera = {
                .x_q16 = snapshot->player_x_q16,
                .y_q16 = snapshot->player_y_q16,
                .speed_q16 = snapshot->player_speed_q16,
                .heading = snapshot->player_heading,
                .racing = (uint8_t)(snapshot->phase == PORTABLE_RACE_PHASE_RACING),
                .finished = (uint8_t)((snapshot->engine_sound.state_flags & 0x20u) != 0u),
                .fix_heading_wrap = session->fix_doppler_wrap};
            const PortableSpatialAudioActor actor = {.x_q16 = racer->position_x_q16,
                                                     .y_q16 = racer->position_y_q16,
                                                     .speed = racer->speed_q16,
                                                     .heading = racer->heading,
                                                     .type = 0x2000u};
            if (!portable_spatial_audio_pitch(
                    &camera, &actor, session->player_dependencies.spatial_pitch_table,
                    session->player_dependencies.spatial_pitch_count, &opponent->engine_doppler))
                return PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT;
        }
    }
    return PORTABLE_GP_RACE_SESSION_OK;
}

PortableGpRaceSessionStatus portable_gp_race_session_configure_hud(
    PortableGpRaceSession *s, const PortableGpRaceHudAssetView *r, const PortableHudAssetView *hud)
{
    if (!s || !hud || !hud->clock_steps || hud->clock_step_count != PORTABLE_HUD_CLOCK_STEP_COUNT)
        return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
    s->hud_clock_steps = hud->clock_steps;
    s->hud_clock_step_count = hud->clock_step_count;
    const uint8_t saved = s->local_index;
    s->hud_rules = r;
    const uint8_t count = multi_local(s) ? s->local_count : 1u;
    for (uint8_t local = 0u; local < count; ++local)
    {
        if (portable_gp_race_session_select_local(s, local) != PORTABLE_GP_RACE_SESSION_OK)
            return PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR;
        memset(&s->rank_hud, 0, sizeof s->rank_hud);
        memset(&s->coin_hud, 0, sizeof s->coin_hud);
        if (r && r->rank_transition_maps && r->rank_flash_colors && r->coin_hud_colors)
        {
            uint8_t classes[4];
            const PortableGpRacer *player = &s->roster.racers[local];
            if (!hud_classes(s, classes) ||
                !portable_gp_rank_hud_init(&s->rank_hud, r->rank_maps, r->kind_pages,
                                           r->rank_colors, (uint8_t)(player->rank_flags_ba >> 1u),
                                           classes) ||
                !portable_gp_coin_hud_init(&s->coin_hud, s->source_frame_clock,
                                           player->coin_count_178, r->coin_hud_colors))
            {
                (void)portable_gp_race_session_select_local(s, saved);
                return PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT;
            }
            if (r->rank_separator_colors)
            {
                for (unsigned i = 0u; i < 2u; ++i)
                {
                    s->rank_hud.pending_separator_colors[i] =
                        (uint16_t)r->rank_separator_colors[4u + i * 2u] |
                        (uint16_t)((uint16_t)r->rank_separator_colors[5u + i * 2u] << 8u);
                    s->rank_hud.displayed_separator_colors[i] =
                        s->rank_hud.pending_separator_colors[i];
                }
            }
        }
    }
    return portable_gp_race_session_select_local(s, saved);
}
