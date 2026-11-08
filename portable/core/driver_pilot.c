#include "driver_pilot.h"
#include "course_prop_contact.h"
#include "driver_angle_composition.h"

#include <stdint.h>

static int sample_source_progress(PortableDriverPilotState *state,
                                  const PortableDriverPilotDependencies *dependencies)
{
    state->source_progress.checkpoint_mode = 0u;
    state->source_progress.new_x_q16 = state->pose.position_x;
    state->source_progress.new_y_q16 = state->pose.position_y;
    state->source_progress.track_cell = state->pose.track_cell;
    state->source_progress.track_cell_valid = state->pose.track_cell_valid;
    state->source_progress.drivable = state->pose.track_cell_valid;
    if (dependencies->observe_progress != NULL &&
        !dependencies->observe_progress(dependencies->progress_context, &state->source_progress))
        return 0;
    if (state->source_update_order)
    {
        /* B98C clears +48 bits0/1 before the hop and B550 consumers. Both
         * outside-route sentinels and the airborne low-half mode gate set2. */
        const PortableRaceMotionObservation *progress = &state->source_progress;
        state->steering.state_flags &= ~UINT32_C(3);
        if (!progress->track_cell_valid)
            state->steering.state_flags |= UINT32_C(1);
        else if ((progress->track_cell & UINT8_C(0x7f)) == UINT8_C(0x7f) ||
                 ((progress->track_cell & UINT8_C(0x80)) == 0u &&
                  (progress->checkpoint_mode & UINT8_C(0x80)) != 0u &&
                  progress->height_q16 > INT32_C(0x40000)))
            state->steering.state_flags |= UINT32_C(2);
    }
    return 1;
}

void portable_driver_pilot_request_contact_audio(PortableDriverPilotState *state, uint16_t song,
                                                 uint8_t restart)
{
    if (state == NULL || song == 0u || (state->steering.state_flags & UINT32_C(0x20)) != 0u)
        return;
    /* Thumb RSBS wraps at INT32_MIN; both shifts operate on those bits. */
    const uint32_t raw_speed = (uint32_t)state->motion.speed_q16;
    const uint32_t magnitude = state->motion.speed_q16 < 0 ? UINT32_C(0) - raw_speed : raw_speed;
    state->contact_audio = (PortableDriverContactAudio){
        .serial = state->contact_audio.serial + 1u,
        .song_id = song,
        .volume = (uint16_t)(192u + ((magnitude >> 20u) & 63u)),
        .pitch = (int16_t)((int32_t)((magnitude << 6u) >> 22u) - 512),
        .restart = restart,
    };
}

/* A9AC and A2B4 share ECC48 voices and song125. Drift cancellation
 * stops only song163, preserving another sound on the same player. */
static void queue_boost_audio(PortableDriverPilotState *state,
                              const PortableDriverPilotDependencies *dependencies,
                              uint16_t prior_flags)
{
    if (state->steering.state_flags & UINT32_C(0x20))
        return;
    const unsigned variant = (state->item_cue_flags_58 & UINT16_C(0x10)) ? 0u : 1u;
    const uint16_t voice = dependencies->mushroom_voice_songs[variant];
    if (voice != 0u)
        state->boost_voice = (PortableDriverContactAudio){
            .serial = state->boost_voice.serial + 1u,
            .song_id = voice,
            .volume = 256u,
            .restart = 1u,
        };
    state->contact_audio = (PortableDriverContactAudio){
        .serial = state->contact_audio.serial + 1u,
        .song_id = 125u,
        .stop_song = (prior_flags & 1u) ? 163u : 0u,
        .volume = 256u,
        .restart = 1u,
    };
}

/* C4C0/9B38 consumes the preceding 7FD8 item constructor before the
 * material response and next 8298 acceleration. Airborne/damage states retain
 * B2; inventory belongs to the constructor, not to this response. */
static int consume_mushroom_request(PortableDriverPilotState *state, uint8_t ground_mode,
                                    const PortableDriverPilotDependencies *dependencies)
{
    const uint16_t pending = state->item_requests_b2;
    const uint16_t prior_flags = state->steering.response_flags;
    if (portable_driver_mushroom_consume(
            ground_mode, &state->item_requests_b2, &state->steering.driver_mode,
            &state->steering.exit_timer, &state->steering.response_flags,
            &state->steering.response_status, &state->steering.base_angle,
            dependencies->mushroom_rules) != PORTABLE_DRIVER_MUSHROOM_OK)
        return 0;
    if ((pending & UINT16_C(0x8000)) && !(state->item_requests_b2 & UINT16_C(0x8000)))
    {
        state->auto_acceleration.driver_mode = state->steering.driver_mode;
        state->forward_acceleration.state_flags = state->steering.response_flags;
        state->hop.animation_flags = state->steering.response_flags;
        state->boost_particle_age = 1u;
        state->boost_particle_birth_height_q16 = state->pose.position_z;
        queue_boost_audio(state, dependencies, prior_flags);
        return 1;
    }
    const uint16_t start_flags = state->steering.response_flags;
    uint8_t started = 0u;
    if (portable_driver_start_boost_consume(
            ground_mode, &state->item_requests_b2, &state->steering.driver_mode,
            &state->steering.exit_timer, &state->steering.response_flags,
            &started) != PORTABLE_DRIVER_MUSHROOM_OK)
        return 0;
    if (started)
    {
        /* Same BD34 burst and boost cue as a mushroom (A9AC .L0803AB44). */
        state->auto_acceleration.driver_mode = state->steering.driver_mode;
        state->forward_acceleration.state_flags = state->steering.response_flags;
        state->hop.animation_flags = state->steering.response_flags;
        state->boost_particle_age = 1u;
        state->boost_particle_birth_height_q16 = state->pose.position_z;
        ++state->start_boosts;
        queue_boost_audio(state, dependencies, start_flags);
    }
    return 1;
}

/* ROM E760 negates each signed component, then bounds its magnitude to
 * 128..1024. FD7C halves the result toward zero. Cached C loses the middle
 * negative-input branch; these limits follow the Thumb instructions. */
static int16_t thwomp_rebound_component(int16_t component)
{
    int32_t reflected = -(int32_t)component;
    if (reflected >= 0)
    {
        if (reflected <= 128)
            reflected = 128;
        else if (reflected > 1023)
            reflected = 1024;
    }
    else
    {
        if (component <= 128)
            reflected = -128;
        else if (component > 1023)
            reflected = -1024;
    }
    return (int16_t)(reflected / 2);
}

void portable_driver_pilot_thwomp_hit(PortableDriverPilotState *state, int32_t height_q16,
                                      int32_t velocity_z_q16)
{
    if (state == NULL)
        return;
    if (height_q16 <= 65536 || velocity_z_q16 >= 0)
    {
        state->motion.component_x = thwomp_rebound_component(state->motion.component_x);
        state->motion.component_y = thwomp_rebound_component(state->motion.component_y);
    }
    else
    {
        state->motion.component_x = 0;
        state->motion.component_y = 0;
        state->steering.response_q8 = 0;
        state->forward_acceleration.response_q8 = 0;
        state->forward_acceleration.state_flags &= UINT16_C(0xffd3);
        state->steering.response_flags = state->forward_acceleration.state_flags;
        state->hop.animation_flags = state->forward_acceleration.state_flags;
        state->normal_collision_timer_38 = UINT16_C(0x7000);
        state->flattened_64 = 1u;
        state->affine.pending_mode = 5u;
        state->steering.control_word = 0u;
        state->input.control_word = 0u;
    }
    state->pose.component_x = state->motion.component_x;
    state->pose.component_y = state->motion.component_y;
}

static int native_recovery_active(const PortableDriverPilotState *state)
{
    return state->source_terrain22_active || state->source_hazard_kind;
}

static PortableDriverCaptureState capture_state(const PortableDriverPilotState *state)
{
    return (PortableDriverCaptureState){
        .x_q16 = state->pose.position_x,
        .y_q16 = state->pose.position_y,
        .height_q16 = state->hop.height_q16,
        .state_flags = state->steering.state_flags,
        .component_x = state->motion.component_x,
        .component_y = state->motion.component_y,
        .velocity_q8 = state->hop.velocity_q8,
        .timer_62 = state->normal_collision_timer_62,
        .mode = state->hop.mode,
        .response_status_7e = state->steering.response_status,
        .hit_angle_80 = (uint16_t)state->steering.base_angle,
        .spin_82 = state->steering.position_82,
        .driver_mode_84 = state->steering.driver_mode,
        .response_88 = (uint16_t)state->steering.response_q8,
        .timer_9c = state->drift_charge.charge_counter,
        .animation_b4 = state->forward_acceleration.state_flags,
        .spin_rate_b6 = state->strong_spin_rate_b6,
        .speed_q16 = state->motion.speed_q16,
        .acceleration_q16 = state->motion.acceleration_q16,
    };
}

/* 9B38 material 0x2A: a qualifying actor beside the kart starts mode 8. */
static int capture_begin(const PortableDriverPilotState *state,
                         const PortableDriverPilotDependencies *dependencies,
                         PortableDriverCaptureState *out)
{
    PortableDriverCaptureActor actors[PORTABLE_COURSE_CAPTURE_ACTOR_CAPACITY];
    const size_t count =
        portable_course_capture_actors(dependencies->piranhas, dependencies->course_props, actors,
                                       PORTABLE_COURSE_CAPTURE_ACTOR_CAPACITY);
    *out = capture_state(state);
    /* 9B38 keeps only +0xb4 bit 0x4000 on any 0x20..0x3f ground material. */
    out->animation_b4 &= UINT16_C(0x4000);
    return portable_driver_capture_begin(
               out, state->item_cue_flags_58, (int16_t)state->invulnerability_timer_6a,
               (int16_t)state->contact_guard_66, actors, count) == PORTABLE_DRIVER_HOP_OK &&
           out->captured;
}

static int capture_step(PortableDriverPilotState *state, uint8_t mode_at_entry,
                        const PortableDriverPilotDependencies *dependencies)
{
    PortableDriverCaptureState capture;
    if (mode_at_entry == 0u)
    {
        if (!capture_begin(state, dependencies, &capture))
            return 0;
        /* 8040A44 plays 162 for the local kart unless it is still sounding. */
        if (!(capture.state_flags & UINT32_C(0x20)))
            state->contact_audio =
                (PortableDriverContactAudio){.serial = state->contact_audio.serial + 1u,
                                             .song_id = PORTABLE_DRIVER_CAPTURE_SWALLOW_SONG,
                                             .volume = 256u};
    }
    else
    {
        capture = capture_state(state);
        if (portable_driver_capture_tick(&capture) != PORTABLE_DRIVER_HOP_OK)
            return 0;
    }
    state->hop.height_q16 = capture.height_q16;
    state->hop.state_flags = capture.state_flags;
    state->hop.velocity_q8 = capture.velocity_q8;
    state->hop.mode = capture.mode;
    state->hop.animation_flags = capture.animation_b4;
    state->motion.component_x = capture.component_x;
    state->motion.component_y = capture.component_y;
    state->normal_collision_timer_62 = capture.timer_62;
    state->steering.response_status = capture.response_status_7e;
    state->steering.base_angle = (int16_t)capture.hit_angle_80;
    state->steering.position_82 = capture.spin_82;
    state->steering.driver_mode = capture.driver_mode_84;
    state->auto_acceleration.driver_mode = capture.driver_mode_84;
    state->steering.response_q8 = (int16_t)capture.response_88;
    state->forward_acceleration.response_q8 = (int16_t)capture.response_88;
    state->drift_charge.charge_counter = capture.timer_9c;
    state->strong_spin_rate_b6 = capture.spin_rate_b6;
    state->motion.speed_q16 = capture.speed_q16;
    state->motion.acceleration_q16 = capture.acceleration_q16;
    if (capture.launched)
    {
        uint16_t heading;
        if (!portable_driver_course_heading(
                state->pose.position_x, state->pose.position_y, dependencies->source_track_route,
                dependencies->source_track_route_count, dependencies->raster,
                dependencies->source_track_angle_lut, dependencies->source_track_angle_lut_count,
                &heading))
            return 0;
        state->steering.steering_offset = (int16_t)heading;
        state->steering.angle = (int16_t)heading;
        state->steering.source_angle_56 = heading;
        state->motion.angle = (int16_t)heading;
        state->normal_collision_base_heading_7c = heading;
        state->normal_collision_response_angle_7a = heading;
        if (!(capture.state_flags & UINT32_C(0x20)))
            state->contact_audio =
                (PortableDriverContactAudio){.serial = state->contact_audio.serial + 1u,
                                             .song_id = PORTABLE_DRIVER_CAPTURE_LAUNCH_SONG,
                                             .volume = 256u,
                                             .restart = 1u};
    }
    return 1;
}

static int source_recovery_stops_motion(const PortableDriverPilotState *state)
{
    const uint8_t mode = state->steering.driver_mode;
    return native_recovery_active(state) &&
           (mode == 2u || mode == 3u || mode == 5u || mode == 6u || mode == 7u);
}

static void sync_acceleration_inputs(PortableDriverPilotState *state, uint16_t control_word,
                                     uint16_t steering_control_word)
{
    state->auto_acceleration.speed_q16 = state->motion.speed_q16;
    state->auto_acceleration.acceleration_q16 = state->motion.acceleration_q16;
    state->auto_acceleration.control_word = control_word;
    state->brake.speed_q16 = state->motion.speed_q16;
    state->brake.acceleration_q16 = state->motion.acceleration_q16;
    state->brake.control_word = control_word;
    state->forward_acceleration.speed_q16 = state->motion.speed_q16;
    state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
    state->steering.speed_q16 = state->motion.speed_q16;
    state->steering.response_q8 = state->forward_acceleration.response_q8;
    state->steering.control_word = steering_control_word;
    state->steering.response_flags = state->forward_acceleration.state_flags;
}

static void sync_motion_inputs(PortableDriverPilotState *state)
{
    state->motion.speed_q16 = state->auto_acceleration.speed_q16;
    state->motion.acceleration_q16 = state->auto_acceleration.acceleration_q16;
}

static void apply_source_normal_speed_floor(PortableDriverPilotState *state)
{
    int32_t next_speed;

    /* 8298's mode1 branch returns before the ordinary stop cleanup. */
    if (state->steering.driver_mode == 1u)
        return;

    /* sub_8038298: after sub_8038710/acceleration dispatch, the source adds
     * +0xbc and +0xc0 with 32-bit wrap semantics. A non-positive result
     * clears both fields. The motion vectors were produced earlier by
     * sub_8038200 and are deliberately left intact for this tick. */
    next_speed =
        (int32_t)((uint32_t)state->motion.speed_q16 + (uint32_t)state->motion.acceleration_q16);
    if (next_speed > 0)
        return;

    state->motion.speed_q16 = 0;
    state->motion.acceleration_q16 = 0;
    state->auto_acceleration.speed_q16 = 0;
    state->auto_acceleration.acceleration_q16 = 0;
    state->brake.speed_q16 = 0;
    state->brake.acceleration_q16 = 0;
    state->forward_acceleration.speed_q16 = 0;
    state->forward_acceleration.acceleration_q16 = 0;
    state->steering.speed_q16 = 0;
}

static int compute_control_acceleration(PortableDriverPilotState *state, uint16_t control_word,
                                        const PortableDriverPilotDependencies *dependencies)
{
    int result;

    /* 8E34, 8298 and 8710 share the source driver mode byte at +0x84.
     * A steering response can clear a boost after the earlier mode update;
     * acceleration must consume that current mode, not its stale copy. */
    if (state->source_update_order)
        state->auto_acceleration.driver_mode = state->steering.driver_mode;

    if (state->source_update_order && state->steering.driver_mode == 10u)
    {
        /* 8298 mode10 writes FFC8 to C0's high half after 8E34. */
        const int32_t acceleration =
            -INT32_C(0x00380000) +
            (int32_t)((uint32_t)state->motion.acceleration_q16 & UINT32_C(0xffff));
        state->auto_acceleration.acceleration_q16 = acceleration;
        state->forward_acceleration.acceleration_q16 = acceleration;
        return PORTABLE_DRIVER_PILOT_OK;
    }
    if (source_recovery_stops_motion(state))
    {
        /* 8298/384D8 modes 2/3/5/6/7 own BC, C0, 88, 7E, 80 and 82.
         * Leave the components produced by 8200 intact until the next tick. */
        state->motion.speed_q16 = 0;
        state->motion.acceleration_q16 = 0;
        state->auto_acceleration.speed_q16 = 0;
        state->auto_acceleration.acceleration_q16 = 0;
        state->brake.speed_q16 = 0;
        state->brake.acceleration_q16 = 0;
        state->forward_acceleration.speed_q16 = 0;
        state->forward_acceleration.acceleration_q16 = 0;
        state->forward_acceleration.response_q8 = 0;
        state->steering.speed_q16 = 0;
        state->steering.response_q8 = 0;
        state->steering.response_status = 0u;
        state->steering.base_angle = 0;
        state->steering.position_82 = 0u;
        state->normal_collision.response_timer_7e = 0u;
        state->normal_collision.angle_delta_80 = 0u;
        state->normal_collision_response_output_pending = 0u;
        return PORTABLE_DRIVER_PILOT_OK;
    }

    if (state->source_terrain22_active && (state->hop.mode == 1u || state->hop.mode == 4u))
    {
        /* 8298 strips C0's high half in mode1. Mode4 dispatch384A4
         * supplies +1 only with A held below7BFFFF, otherwise -1. */
        const uint16_t low = (uint16_t)state->motion.acceleration_q16;
        int32_t high = 0;
        if (state->hop.mode == 4u)
            high = (control_word & UINT16_C(0x8000)) != 0u &&
                           state->motion.speed_q16 <= INT32_C(0x007bffff)
                       ? 1
                       : -1;
        state->auto_acceleration.acceleration_q16 = high * 65536 + low;
        state->forward_acceleration.acceleration_q16 = state->auto_acceleration.acceleration_q16;
        return PORTABLE_DRIVER_PILOT_OK;
    }

    if (state->source_update_order && state->steering.response_status == 15u)
    {
        /* 8710 case15 replaces all of C0 and oscillates about128 units. */
        const int32_t acceleration = state->motion.speed_q16 <= INT32_C(0x00800000)
                                         ? INT32_C(0x00080000)
                                         : -INT32_C(0x00080000);
        state->steering.response_flags |= UINT16_C(8);
        state->forward_acceleration.state_flags |= UINT16_C(8);
        state->hop.animation_flags |= UINT16_C(8);
        state->auto_acceleration.acceleration_q16 = acceleration;
        state->forward_acceleration.acceleration_q16 = acceleration;
        return PORTABLE_DRIVER_PILOT_OK;
    }

    if (state->source_update_order && state->steering.response_status == 14u)
    {
        /* 8710 case14 calls8640 directly after8E34/8AFC. The terrain
         * limit and speed-target writers belong to ordinary statuses. */
        result = portable_driver_forward_acceleration_tick(
            &state->forward_acceleration, dependencies->forward_response_curve,
            dependencies->forward_response_count, dependencies->forward_local_table,
            dependencies->forward_local_count);
        if (result != PORTABLE_DRIVER_FORWARD_ACCELERATION_OK)
            return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
        state->auto_acceleration.acceleration_q16 = state->forward_acceleration.acceleration_q16;
        return PORTABLE_DRIVER_PILOT_OK;
    }

    if ((control_word & UINT16_C(0x4000)) != 0)
    {
        result = portable_driver_brake_acceleration_tick(
            &state->brake, dependencies->brake_table, dependencies->brake_table_count,
            dependencies->brake_accelerate_table, dependencies->brake_accelerate_table_count);
        if (result != PORTABLE_DRIVER_BRAKE_OK)
            return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
        state->auto_acceleration.acceleration_q16 = state->brake.acceleration_q16;
    }
    else if ((control_word & UINT16_C(0x8000)) != 0)
    {
        if (dependencies->forward_limit_thresholds != NULL)
        {
            if (state->auto_acceleration.driver_mode > 4 ||
                state->forward_limit_selector >= dependencies->forward_limit_threshold_count)
                return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
            result = portable_driver_forward_limit(
                dependencies->forward_limit_thresholds[state->forward_limit_selector],
                state->forward_acceleration.speed_q16,
                &state->forward_acceleration.acceleration_q16, dependencies->brake_table,
                dependencies->brake_table_count);
            if (result == PORTABLE_DRIVER_FORWARD_LIMIT_INVALID)
                return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
            if (result == PORTABLE_DRIVER_FORWARD_LIMIT_HANDLED)
            {
                state->auto_acceleration.acceleration_q16 =
                    state->forward_acceleration.acceleration_q16;
                return PORTABLE_DRIVER_PILOT_OK;
            }
        }
        /* sub_8038710's ordinary GP A branch calls sub_803DB4C once
         * +0xbc reaches the +0xa8 target. Its signed ROM row replaces the
         * high half of +0xc0 and retains the low half. */
        const int32_t speed = state->forward_acceleration.speed_q16;
        int target_reached =
            state->input.speed_target_q16 > 0 && speed >= state->input.speed_target_q16;
        if (state->lightning_timer_68 < 0)
            target_reached =
                speed > (int32_t)((uint32_t)state->input.speed_target_q16 - UINT32_C(0x800000));
        else if (state->lightning_timer_68 != 0 || state->item_lock_ac != 0u)
        {
            if (dependencies->source_angle_target_table == NULL ||
                dependencies->source_angle_target_table_count <= 0x8fu)
                return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
            target_reached |=
                speed >= (int32_t)dependencies->source_angle_target_table[0x8fu] * 1048576;
        }
        if (dependencies->forward_target_response_table != NULL && target_reached)
        {
            /* DB4C uses signed ASR then masks the index, including when
             * lightning calls it below the ordinary A8 speed target. */
            int64_t difference =
                (int32_t)((uint32_t)speed - (uint32_t)state->input.speed_target_q16);
            int64_t delta = difference >= 0 ? difference / 65536 : -((-difference + 65535) / 65536);
            if (delta > 0x1ff)
                delta = 0x1ff;
            const uint32_t index =
                ((uint32_t)(delta >= 0 ? delta / 64 : -((-delta + 63) / 64))) & 7u;
            int16_t sample;
            uint16_t low;
            if (dependencies->forward_target_response_count < 8u)
                return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
            sample = dependencies->forward_target_response_table[index];
            low = (uint16_t)state->forward_acceleration.acceleration_q16;
            state->forward_acceleration.acceleration_q16 = (int32_t)((int64_t)sample * 65536 + low);
            state->auto_acceleration.acceleration_q16 =
                state->forward_acceleration.acceleration_q16;
            return PORTABLE_DRIVER_PILOT_OK;
        }
        result = portable_driver_forward_acceleration_tick(
            &state->forward_acceleration, dependencies->forward_response_curve,
            dependencies->forward_response_count, dependencies->forward_local_table,
            dependencies->forward_local_count);
        if (result != PORTABLE_DRIVER_FORWARD_ACCELERATION_OK)
            return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
        state->auto_acceleration.acceleration_q16 = state->forward_acceleration.acceleration_q16;
    }
    else
    {
        result = portable_driver_acceleration_tick(&state->auto_acceleration,
                                                   dependencies->auto_acceleration_table,
                                                   dependencies->auto_acceleration_count);
        if (result != PORTABLE_DRIVER_ACCELERATION_OK)
            return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
    }

    return PORTABLE_DRIVER_PILOT_OK;
}

static int compute_source_mode_acceleration(PortableDriverPilotState *state,
                                            const PortableDriverPilotDependencies *dependencies)
{
    /* 8298 dispatches driver mode before 8710's response status. Defer the
     * recovery reset to the ordinary post-response acceleration phase. */
    if (source_recovery_stops_motion(state) ||
        (state->source_update_order && state->steering.driver_mode == 10u))
        return 0;
    if (state->source_update_order && state->steering.driver_mode == 1u)
    {
        /* 8298/383B8: airborne surface jumps keep C0's low half and skip
         * all ground throttle, brake and response acceleration writers. */
        state->motion.acceleration_q16 = (uint16_t)state->motion.acceleration_q16;
        state->auto_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->brake.acceleration_q16 = state->motion.acceleration_q16;
        return 1;
    }
    if (state->source_update_order && state->steering.driver_mode == 12u)
    {
        if (portable_driver_charged_boost_tick(
                &state->steering.driver_mode, &state->steering.exit_timer,
                &state->steering.response_flags, state->input.speed_target_q16,
                &state->motion.speed_q16,
                &state->motion.acceleration_q16) != PORTABLE_DRIVER_MUSHROOM_OK)
            return -1;
        state->auto_acceleration.driver_mode = state->steering.driver_mode;
        state->auto_acceleration.speed_q16 = state->motion.speed_q16;
        state->brake.speed_q16 = state->motion.speed_q16;
        state->forward_acceleration.speed_q16 = state->motion.speed_q16;
        state->auto_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->brake.acceleration_q16 = state->motion.acceleration_q16;
        state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        return 1;
    }
    if (state->source_update_order &&
        (state->steering.response_status >= 5u && state->steering.response_status <= 8u))
    {
        /* 8710 cases5/6 decelerate by8, cases7/8 by16, retaining
         * the fractional half,
         * but clear all acceleration while the B8 wobble is active. */
        state->motion.acceleration_q16 =
            state->steering.spin_delay != 0u
                ? 0
                : (int32_t)(((uint32_t)state->motion.acceleration_q16 & UINT32_C(0xffff)) |
                            (state->steering.response_status <= 6u ? UINT32_C(0xfff80000)
                                                                   : UINT32_C(0xfff00000)));
        state->auto_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->brake.acceleration_q16 = state->motion.acceleration_q16;
        return 1;
    }
    if (state->source_update_order &&
        (state->steering.driver_mode == 11u ||
         state->steering.response_status == UINT16_C(0x000b)) &&
        dependencies->normal_response_table != NULL)
    {
        PortableSourceNormalResponseResult response;

        if (portable_source_normal_response_produce(
                state->steering.response_status, state->steering.base_angle,
                dependencies->normal_response_table,
                &response) != PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK)
            return -1;
        if (response.output_written)
            state->motion.acceleration_q16 = response.output_q16;
        else
        {
            state->steering.response_status = response.next_status;
        }
        /* ROM DAEC branches directly to its return after writing C0.
         * Only zero angle or a non-11 status clears mode84. */
        if (response.timer_cleared)
        {
            state->normal_collision.timer_value_84 = 0u;
            state->steering.driver_mode = 0u;
            state->auto_acceleration.driver_mode = 0u;
        }
        state->auto_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->brake.acceleration_q16 = state->motion.acceleration_q16;
        return 1;
    }
    if (state->steering.driver_mode != 8u)
        return 0;
    if (portable_driver_mushroom_tick(&state->steering.driver_mode, &state->steering.exit_timer,
                                      &state->forward_acceleration.state_flags,
                                      &state->motion.speed_q16, &state->motion.acceleration_q16,
                                      dependencies->mushroom_rules) != PORTABLE_DRIVER_MUSHROOM_OK)
        return -1;
    state->auto_acceleration.driver_mode = state->steering.driver_mode;
    state->auto_acceleration.speed_q16 = state->motion.speed_q16;
    state->auto_acceleration.acceleration_q16 = state->motion.acceleration_q16;
    state->brake.speed_q16 = state->motion.speed_q16;
    state->brake.acceleration_q16 = state->motion.acceleration_q16;
    state->forward_acceleration.speed_q16 = state->motion.speed_q16;
    state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
    state->steering.speed_q16 = state->motion.speed_q16;
    state->steering.response_flags = state->forward_acceleration.state_flags;
    return 1;
}

static uint16_t source_cell_key(int32_t world_x, int32_t world_y)
{
    return (uint16_t)(((uint32_t)world_y >> 3 << 8) | ((uint32_t)world_x >> 3));
}

static void sync_normal_collision_response_state(PortableDriverPilotState *state)
{
    state->normal_collision_response_angle_7a = state->normal_collision.response_angle_7a;
    state->normal_collision_base_heading_7c = state->normal_collision.base_heading_7c;
    state->normal_collision_response_delta_80 = state->normal_collision.angle_delta_80;
    if (state->source_update_order && state->normal_collision_source_from_native_state &&
        state->normal_collision_source_use_b550_table)
    {
        /* AD6C writes +0xbc and +0x80 in the same driver read by later
         * motion/steering. For nonzero vectors it does NOT write +0x7a:
         * angle_history_7a retains that field; response_angle_7a is a
         * derived diagnostic angle, not an authoritative field write. */
        state->motion.speed_q16 = state->normal_collision.magnitude_state_bc;
        state->steering.angle = (int16_t)state->normal_collision.angle_history_7a;
        state->steering.steering_offset = (int16_t)state->normal_collision.base_heading_7c;
        state->steering.base_angle = (int16_t)state->normal_collision.angle_delta_80;
        state->steering.response_status = state->normal_collision.response_timer_7e;
        /* AD6C writes mode11 only for a nonzero, forward impact. */
        if (state->normal_collision.response_applied)
        {
            state->steering.driver_mode = state->normal_collision.timer_value_84;
            state->auto_acceleration.driver_mode = state->steering.driver_mode;
        }
        state->motion.angle = state->steering.angle;
    }
}

static void sync_normal_collision_source_from_native_state(PortableDriverPilotState *state)
{
    /* These assignments follow the accepted source field-owner map:
     * +0x4e/+0x50 <- motion components, +0x48 <- steering.state_flags,
     * +0x7a <- steering.angle, +0x7c <- steering.steering_offset,
     * +0x80 <- steering.base_angle, +0x92/+0x84/+0x7e <- the prior native
     * normal-response result, and +0xbc <- motion.speed_q16. */
    state->normal_collision_source.resultant_x = state->motion.component_x;
    state->normal_collision_source.resultant_y = state->motion.component_y;
    state->normal_collision_source.current_angle = (uint16_t)state->steering.steering_offset;
    state->normal_collision_source.source_flags = state->steering.state_flags;
    state->normal_collision_source.prior_angle_history_7a = (uint16_t)state->steering.angle;
    state->normal_collision_source.prior_angle_delta_80 = (uint16_t)state->steering.base_angle;
    state->normal_collision_source.prior_resistance_92 = state->normal_collision.resistance_92;
    state->normal_collision_source.prior_magnitude_state_bc = state->motion.speed_q16;
    state->normal_collision_source.prior_timer_value_84 = state->normal_collision.timer_value_84;
    state->normal_collision_source.prior_response_timer_7e =
        state->normal_collision.response_timer_7e;
}

static int apply_steering_response(PortableDriverPilotState *state,
                                   const PortableDriverPilotDependencies *dependencies)
{
    int result;
    const int16_t *response_data;
    size_t response_count;

    if (dependencies->steering_response_data == NULL)
        return PORTABLE_DRIVER_PILOT_OK;

    state->steering.response_q8 = state->forward_acceleration.response_q8;
    if (state->source_update_order)
        /* 8E34/DB84/DBD0 already wrote +b4 in this driver. 8AFC observes
         * those same flags; the acceleration adapter is the older copy. */
        state->forward_acceleration.state_flags = state->steering.response_flags;
    else
        state->steering.response_flags = state->forward_acceleration.state_flags;
    response_data = dependencies->steering_response_data;
    response_count = dependencies->steering_response_data_count;
    if (response_count >= 16u)
    {
        if (state->steering.response_slot >= 4u)
            return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
        response_data += (size_t)state->steering.response_slot * 4u;
        response_count = 4u;
    }
    else if (state->steering.response_slot != 0u)
        return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
    result =
        portable_driver_steering_response_tick(&state->steering, response_data, response_count);
    if (result != PORTABLE_DRIVER_STEERING_OK)
        return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
    state->forward_acceleration.response_q8 = state->steering.response_q8;
    return PORTABLE_DRIVER_PILOT_OK;
}

static int
apply_normal_collision_response_table(PortableDriverPilotState *state,
                                      const PortableDriverPilotDependencies *dependencies)
{
    PortableSourceNormalResponseResult response_output;

    /* sub_803DAEC writes the next +0xc0 only after the response handler. It
     * is meaningful to the native source-order adapter because the following
     * sub_8038200 tick consumes that stored acceleration. Keep the default
     * same-tick adapter unchanged until its source writer is independently
     * owned. */
    if (!state->source_update_order || dependencies->normal_response_table == NULL ||
        !state->normal_collision.response_applied)
        return PORTABLE_DRIVER_PILOT_OK;

    if (portable_source_normal_response_produce(
            UINT8_C(0x0b), (int16_t)state->normal_collision.angle_delta_80,
            dependencies->normal_response_table,
            &response_output) != PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK)
        return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;

    state->normal_collision_response_next_status = response_output.next_status;
    if (response_output.output_written)
    {
        state->normal_collision_response_output_c0 = response_output.output_q16;
        state->normal_collision_response_output_written = 1;
        state->normal_collision_response_output_pending = 1;
    }
    return PORTABLE_DRIVER_PILOT_OK;
}

static int apply_pending_vector_effect(PortableDriverPilotState *state,
                                       const PortableDriverPilotDependencies *dependencies)
{
    int result;
    PortableDriverMaterialState material = {0};
    const PortableDriverMaterialState *material_arg = NULL;
    int16_t component_x = state->motion.component_x;
    int16_t component_y = state->motion.component_y;

    if (state->vector_effect_alternate_state_branch)
    {
        result = portable_driver_vector_effect_apply_alternate(
            state->source_update_order ? state->steering.source_angle_56
                                       : state->vector_effect_source_direction,
            state->vector_effect_mode, dependencies->vector_alternate_x_components,
            dependencies->vector_alternate_x_count, dependencies->vector_alternate_y_components,
            dependencies->vector_alternate_y_count, &component_x, &component_y,
            &state->vector_effect);
    }
    else
    {
        if ((state->vector_effect_mode & 1) != 0)
        {
            if (dependencies->track_material == NULL || dependencies->terrain == NULL)
                return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
            result = portable_driver_material_sample(
                dependencies->track_material, dependencies->raster, dependencies->terrain,
                state->pose.position_x, state->pose.position_y, &material);
            if (result != PORTABLE_DRIVER_MATERIAL_OK)
                return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
            material_arg = &material;
        }
        result = portable_driver_vector_effect_apply(
            material_arg, dependencies->track_material, dependencies->terrain,
            dependencies->terrain_effect_tables, state->vector_effect_mode, &component_x,
            &component_y, &state->vector_effect);
    }
    if (result != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
    state->motion.component_x = component_x;
    state->motion.component_y = component_y;
    if (state->source_update_order)
    {
        state->vector_effect_source_direction = state->vector_effect_alternate_state_branch
                                                    ? 0u
                                                    : state->vector_effect.source_direction_index;
        /* B018 writes the contact word +0x5a, not response +0x7e.
         * The typed vector result retains that separate contact word. */
    }
    state->vector_effect_pending = 0;
    ++state->vector_effect_count;
    return PORTABLE_DRIVER_PILOT_OK;
}

int portable_driver_effects_visible(const PortableDriverPilotState *state)
{
    return state != NULL &&
           !portable_driver_hidden(state->hop.mode, state->pose.position_z, state->fall_frames_76,
                                   state->drift_charge.charge_counter);
}

/* B7B8 retires the selected kart without overwriting96,84,88 or CA. */
void portable_driver_pilot_retire_selected(PortableDriverPilotState *state)
{
    if (!state)
        return;
    state->steering.state_flags =
        (state->steering.state_flags & ~UINT32_C(0x38)) | UINT32_C(0x2038);
    state->steering.position_82 = 0u;
    state->steering.base_angle = 0;
    state->steering.response_status = 0u;
    state->finished_initialized = 1u;
    state->finished_route_row_191 = (uint8_t)((state->steering.state_flags & 0x8000u) != 0u);
}

/* BC84: outside raster retains90 and uses row0; ordinary finished player
 * cells select the imported alternate row through DCA4. */
static int finished_target(PortableDriverPilotState *state,
                           const PortableDriverPilotDependencies *d,
                           const PortableDriverFinishedInputs *in, int32_t x, int32_t y,
                           uint16_t *angle)
{
    uint8_t cell = 0;
    size_t index;
    const int status = portable_track_raster_query16(d->raster, x / 65536, y / 65536, &cell);
    if (status != PORTABLE_TRACK_RASTER_OK && status != PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS)
        return 0;
    const int outside = (state->steering.state_flags & 3u) != 0u ||
                        status == PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS || (cell & 127u) == 127u;
    index = outside ? state->source_route_index_90 : (cell & 127u);
    const PortableRacePoint *route = d->source_track_route;
    if (!outside && (state->steering.state_flags & 0x8020u) == 0x8020u && in->alternate_route &&
        in->alternate_route_count >= d->source_track_route_count)
        route = in->alternate_route;
    if (!route || index >= d->source_track_route_count)
        return 0;
    if (outside)
    {
        if (portable_driver_vector_effect_source_angle((int16_t)(route[index].x - x / 65536),
                                                       (int16_t)(route[index].y - y / 65536),
                                                       angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
            return 0;
    }
    else if (portable_driver_track_direction_source_angle(
                 x, y, route[index].x, route[index].y, d->source_track_angle_lut,
                 d->source_track_angle_lut_count, angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return 0;
    *angle &= UINT16_C(0xff00);
    return 1;
}
static int finished_acceleration(PortableDriverPilotState *state,
                                 const PortableDriverPilotDependencies *d,
                                 const PortableDriverFinishedInputs *in)
{
    const uint8_t mode = state->steering.driver_mode;
    if (mode == 0u)
    {
        const uint8_t *table = d->source_angle_target_table;
        const unsigned cls = state->auto_acceleration.character_class;
        const size_t slot = 0x80u + (state->source_speed_variant_19b & 3u) +
                            (state->source_steering_selector_9a & 15u) * 4u + cls * 20u;
        if (!table || cls >= 8u || d->source_angle_target_table_count < 0x1a0u ||
            slot >= d->source_angle_target_table_count)
            return 0;
        const int32_t target = (int32_t)table[slot] * 16 + state->source_speed_offset_19d +
                               (int32_t)(in->coin_count > 16u ? 16u : in->coin_count) * 2;
        const int32_t speed = state->motion.speed_q16 / 65536;
        const int32_t gap = target - speed;
        if (gap >= 0)
        {
            /* Comparison above truncatesBC, but the independent source
             * lookup uses ASR16 then ASR6&15 and only an upper clamp. */
            int32_t raw = state->motion.speed_q16 >= 0
                              ? state->motion.speed_q16 / 65536
                              : (int32_t)(-((-(int64_t)state->motion.speed_q16 + 65535) / 65536));
            if (raw > 1023)
                raw = 1023;
            const int32_t shifted = raw >= 0 ? raw / 64 : (int32_t)(-((-(int64_t)raw + 63) / 64));
            const unsigned bin = (uint32_t)shifted & 15u;
            state->motion.acceleration_q16 = (int32_t)table[cls * 16u + bin] * 4096;
        }
        else
        {
            const unsigned bin = (unsigned)(-gap > 511 ? 511 : -gap) / 64u;
            if (!d->source_auto_deceleration_table || d->source_auto_deceleration_count < 8u)
                return 0;
            const uint32_t bits =
                ((uint32_t)(uint16_t)d->source_auto_deceleration_table[bin] << 16u) |
                (uint16_t)state->motion.acceleration_q16;
            state->motion.acceleration_q16 =
                bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - INT64_C(0x100000000));
        }
    }
    else if (mode == 11u)
    {
        /* BD24's autonomous spin settles82 independently of player8E34. */
        if (!(state->steering.state_flags & 0x4000u))
        {
            int32_t position = state->motion.speed_q16 <= INT32_C(0x00ffffff)
                                   ? 0
                                   : (int16_t)state->steering.position_82;
            const int32_t before = position;
            if (position < 0)
                position += 1024;
            else if (position > 0)
                position -= 1024;
            state->steering.position_82 = (uint16_t)position;
            if (!position || (before < 0 && position >= 0) || (before > 0 && position < 0))
            {
                state->steering.driver_mode = state->auto_acceleration.driver_mode = 0u;
            }
            else
            {
                unsigned magnitude = (unsigned)(position < 0 ? -position : position);
                if (magnitude > 0x3fffu)
                    magnitude = 0x3f00u;
                const unsigned bin = (magnitude >> 11u) & 7u;
                if (!d->normal_response_table || d->normal_response_table->count < 8u)
                    return 0;
                const uint32_t bits = (uint32_t)d->normal_response_table->entries[bin] << 16u;
                state->motion.acceleration_q16 =
                    bits <= INT32_MAX ? (int32_t)bits
                                      : (int32_t)((int64_t)bits - INT64_C(0x100000000));
            }
        }
    }
    else
    {
        /* BD24 recovery/boost branches reuse the existing8298 owner. */
        int result = compute_source_mode_acceleration(state, d);
        if (result < 0)
            return 0;
        if (result == 0 &&
            compute_control_acceleration(state, UINT16_C(0x8000), d) != PORTABLE_DRIVER_PILOT_OK)
            return 0;
        state->motion.acceleration_q16 = state->auto_acceleration.acceleration_q16;
        if (mode != 1u)
            apply_source_normal_speed_floor(state);
    }
    state->auto_acceleration.speed_q16 = state->forward_acceleration.speed_q16 =
        state->motion.speed_q16;
    state->auto_acceleration.acceleration_q16 = state->forward_acceleration.acceleration_q16 =
        state->brake.acceleration_q16 = state->motion.acceleration_q16;
    state->steering.speed_q16 = state->motion.speed_q16;
    return 1;
}
static int finished_resolve(PortableDriverPilotState *state,
                            const PortableDriverPilotDependencies *d,
                            const PortableDriverFinishedInputs *in)
{
    state->finished_initialized = 1u;
    state->finished_route_row_191 = (uint8_t)((state->steering.state_flags & 0x8000u) != 0u);
    PortableDriverAngleCompositionInput angles = {
        .current_angle_56 = state->steering.source_angle_56,
        .target_angle_7a = (uint16_t)state->steering.angle,
        .source_flags_b4 = state->steering.response_flags,
        .limit_b6 = state->strong_spin_rate_b6,
        .position_82 = state->steering.position_82,
        .angle_delta_80 = (uint16_t)state->steering.base_angle,
        .timer_84 = state->steering.driver_mode,
        .response_status_7e = state->steering.response_status,
        .target_angle_ca = state->finished_target_angle_ca,
        .table_nibble_9a = state->source_steering_selector_9a,
        .target_table = d->source_angle_target_table,
        .target_table_count = d->source_angle_target_table_count};
    PortableDriverAngleCompositionResult out;
    if (portable_driver_angle_compose(&angles, &out) != PORTABLE_DRIVER_ANGLE_COMPOSITION_OK)
        return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
    state->steering.source_angle_56 = out.next_current_angle_56;
    state->steering.angle = (int16_t)out.next_target_angle_7a;
    state->steering.response_flags = state->forward_acceleration.state_flags = out.next_flags_b4;
    state->strong_spin_rate_b6 = out.next_limit_b6;
    state->steering.position_82 = out.next_position_82;
    state->steering.base_angle = (int16_t)out.next_angle_delta_80;
    state->steering.driver_mode = state->auto_acceleration.driver_mode = out.next_timer_84;
    state->steering.response_status = out.next_response_status_7e;
    state->motion.angle = state->steering.angle;
    if (!(state->steering.state_flags & 0x4000u))
    {
        PortableKartMotionState vector = state->motion;
        if (portable_kart_motion_tick(&vector, d->sine_table, d->sine_count) !=
            PORTABLE_KART_MOTION_OK)
            return PORTABLE_DRIVER_PILOT_MOTION_ERROR;
        state->motion.speed_q16 = vector.speed_q16;
        state->pose.component_x = state->motion.component_x = vector.component_x;
        state->pose.component_y = state->motion.component_y = vector.component_y;
    }
    if (!finished_target(state, d, in, state->pose.position_x, state->pose.position_y,
                         &state->finished_target_angle_ca))
        return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
    /* BD24 writes A after the CA direction lookup, unlike B7B8 retirement. */
    state->steering.control_word = state->input.control_word = UINT16_C(0x8000);
    state->auto_acceleration.control_word = state->brake.control_word = UINT16_C(0x8000);
    if (!finished_acceleration(state, d, in))
        return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
    if ((int16_t)state->item_lock_ac != 0)
        state->item_lock_ac =
            state->invulnerability_timer_6a ? 0u : (uint16_t)(state->item_lock_ac - 1u);
    return PORTABLE_DRIVER_PILOT_OK;
}
static int driver_pilot_tick(PortableDriverPilotState *state, uint16_t source_control_word,
                             const PortableDriverPilotDependencies *dependencies,
                             const PortableDriverFinishedInputs *finished);
int portable_driver_pilot_finished_tick(PortableDriverPilotState *state,
                                        const PortableDriverPilotDependencies *d,
                                        const PortableDriverFinishedInputs *in)
{
    if (!state || !d || !in || !state->source_update_order ||
        !(state->steering.state_flags & 0x2000u))
        return PORTABLE_DRIVER_PILOT_INVALID_ARGUMENT;
    return driver_pilot_tick(state, state->steering.control_word, d, in);
}
int portable_driver_pilot_tick_with_finished(PortableDriverPilotState *state,
                                             uint16_t source_control_word,
                                             const PortableDriverPilotDependencies *d,
                                             const PortableDriverFinishedInputs *in)
{
    return driver_pilot_tick(state, source_control_word, d, in);
}
int portable_driver_pilot_tick(PortableDriverPilotState *state, uint16_t source_control_word,
                               const PortableDriverPilotDependencies *dependencies)
{
    return driver_pilot_tick(state, source_control_word, dependencies, NULL);
}

/* Ephemeral phase state: shared only within one driver turn, never serialized.
 * Entry pose/height and deferred responses must survive the ordered stages. */
typedef struct
{
    uint16_t source_control_word;
    const PortableDriverFinishedInputs *finished_options;
    const PortableDriverFinishedInputs *finished;
    uint16_t control_word;
    int skip_motion_this_tick;
    int source_status15_processed;
    int source_b550_stage;
    int b550_flags_finished;
    int defer_source_response_acceleration;
    int source_control_acceleration_due;
    uint16_t status_before_steering;
    int material_sampled;
    int material_result;
    PortableDriverMaterialState material;
    int16_t source_contact_anchor_x;
    int16_t source_contact_anchor_y;
    uint8_t hop_mode_at_entry;
    int32_t contact_height_at_entry;
    int finished_resolved;
} DriverPilotTick;

static int pilot_begin_tick(PortableDriverPilotState *state,
                            const PortableDriverPilotDependencies *dependencies,
                            DriverPilotTick *tick)
{
    int result;
    if (state != NULL && state->charged_particle_age != 0u)
    {
        ++state->charged_particle_age;
        if (state->charged_particle_age > 21u)
            state->charged_particle_age = 0u;
    }
    if (state != NULL && state->boost_particle_age != 0u)
    {
        ++state->boost_particle_age;
        if (state->boost_particle_age > 51u)
            state->boost_particle_age = 0u;
    }
    if (state == NULL || dependencies == NULL || dependencies->raster == NULL ||
        dependencies->response_rules == NULL)
        return PORTABLE_DRIVER_PILOT_INVALID_ARGUMENT;
    state->balloon_event = PORTABLE_BATTLE_BALLOON_NONE;
    state->impact_event = 0u;
    tick->hop_mode_at_entry = state->hop.mode;
    tick->contact_height_at_entry = state->pose.position_z;
    state->hop.recovery_touchdown = 0u;
    state->hazard_audio_kind = 0u;
    state->hop.protected_hop_started = 0u;
    state->hop.surface_jump_started = state->hop.landed = 0u;

    tick->source_b550_stage = state->source_update_order &&
                              state->normal_collision_source_from_native_state &&
                              state->normal_collision_source_use_b550_table;

    /* sub_803C4C0 stores +0xee/+0xf0 from the pre-integration pose. */
    if (state->source_update_order)
    {
        tick->source_contact_anchor_x = (int16_t)(state->pose.position_x / INT32_C(0x10000));
        tick->source_contact_anchor_y = (int16_t)(state->pose.position_y / INT32_C(0x10000));
    }

    state->contact_anchor_x_ee = tick->source_contact_anchor_x;
    state->contact_anchor_y_f0 = tick->source_contact_anchor_y;

    state->source_progress = (PortableRaceMotionObservation){
        .old_x_q16 = state->pose.position_x,
        .old_y_q16 = state->pose.position_y,
        .height_q16 = tick->contact_height_at_entry,
        .hop_mode = tick->hop_mode_at_entry,
    };

    /* sub_803C4C0 applies the components produced by the previous frame
     * before it updates acceleration, speed or heading for this frame. */
    result = portable_driver_pose_tick(dependencies->raster, &state->pose);
    if (result != PORTABLE_DRIVER_POSE_OK &&
        !(result == PORTABLE_DRIVER_POSE_TRACK_OUT_OF_BOUNDS && state->allow_track_boundary != 0))
        return PORTABLE_DRIVER_PILOT_POSE_ERROR;

    if (!sample_source_progress(state, dependencies))
        return PORTABLE_DRIVER_PILOT_PROGRESS_ERROR;
    if (tick->finished_options && (state->steering.state_flags & UINT32_C(0x2000)))
    {
        tick->finished = tick->finished_options;
        tick->source_control_word = state->steering.control_word;
        /* Runtime observers own B98C acceptance. Standalone callers can
         * resolve the same checkpoint without creating a race/timer owner. */
        if (!dependencies->observe_progress)
        {
            const PortableRaceMotionObservation *p = &state->source_progress;
            const size_t index = p->track_cell & 127u;
            state->source_speed_variant_19b = 0u;
            if (p->track_cell_valid && index != 127u)
            {
                if (!(p->track_cell & 128u) && tick->finished->ai_modes)
                {
                    const size_t slot = (size_t)tick->finished->ai_mode_row *
                                            dependencies->source_track_route_count +
                                        index;
                    if (slot >= tick->finished->ai_mode_count)
                        return PORTABLE_DRIVER_PILOT_PROGRESS_ERROR;
                    state->source_speed_variant_19b = tick->finished->ai_modes[slot];
                }
                state->source_progress.checkpoint_mode = state->source_speed_variant_19b;
                if (!(p->track_cell & 128u) && (state->source_speed_variant_19b & 128u) &&
                    p->height_q16 > 0x40000)
                    state->steering.state_flags |= 2u;
                else
                    state->source_route_index_90 = (uint8_t)index;
            }
        }
    }

    if (state->source_update_order)
    {
        const PortableImpactEffectInputs impact = {.state_flags = state->steering.state_flags,
                                                   .speed_q16 = state->motion.speed_q16,
                                                   .component_x = state->motion.component_x,
                                                   .component_y = state->motion.component_y,
                                                   .axis = state->vector_effect_source_direction,
                                                   .x = state->impact_x,
                                                   .y = state->impact_y,
                                                   .depth = state->impact_depth,
                                                   .selected = state->impact_selected};
        const int event = portable_impact_effect_request(&state->impact_effects,
                                                         dependencies->impact_effect_rules, &impact,
                                                         &state->vector_effect.contact_word_5a);
        if (event < 0)
            return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
        state->impact_event = (uint8_t)event;
    }

    if (dependencies->track_material != NULL && dependencies->terrain != NULL)
    {
        tick->material_result = portable_driver_material_sample(
            dependencies->track_material, dependencies->raster, dependencies->terrain,
            state->pose.position_x, state->pose.position_y, &tick->material);
        tick->material_sampled = 1;
        if (state->source_update_order && state->pose.position_z > INT32_C(0x40000))
        {
            /* B2B4 bypasses the terrain material lookup above four units.
             * This height precedes the later Lakitu callback. Raster/route
             * producers still consume the independent horizontal pose. */
            tick->material.material_flags = UINT8_C(0x40);
            tick->material.material_flags_valid = 1u;
            tick->material.material_effect_candidate = 0u;
        }
        if (tick->material_result == PORTABLE_DRIVER_MATERIAL_OK)
        {
            state->source_material_85 = tick->material.material_flags;
            state->source_material_valid = 1u;
        }
        /* sub_803B2B4 writes sub_8032990's material byte to +0x85.
         * Ground sub_8039B38/A578 derives +0x86 from bits 1..4 only
         * for 0x40..0x7f. Its signed-byte gate preserves the prior terrain
         * response on wall/object contact; airborne states retain it too. */
        if (tick->material_result == PORTABLE_DRIVER_MATERIAL_OK && tick->hop_mode_at_entry == 0u &&
            tick->material.material_flags >= UINT8_C(0x40) &&
            tick->material.material_flags < UINT8_C(0x80))
            state->forward_limit_selector =
                (uint8_t)((tick->material.material_flags >> 1) & UINT8_C(0x0f));
    }

    state->hop.height_q16 = state->pose.position_z;
    state->hop.state_flags = state->steering.state_flags;
    state->hop.animation_flags = state->forward_acceleration.state_flags;
    state->source_terrain22_active =
        (uint8_t)(state->source_update_order && state->source_terrain22_enabled &&
                  ((tick->material_result == PORTABLE_DRIVER_MATERIAL_OK &&
                    tick->material.material_flags == UINT8_C(0x22)) ||
                   (state->source_terrain22_active &&
                    (tick->hop_mode_at_entry == 3u || tick->hop_mode_at_entry == 4u ||
                     tick->hop_mode_at_entry == 6u || tick->hop_mode_at_entry == 7u))));
    /* Ground-only material dispatch; retain the owner throughout carry. */
    if (!state->source_update_order ||
        (!state->source_hazard_enabled &&
         state->source_hazard_kind != PORTABLE_DRIVER_HAZARD_BATTLE_BOMB))
        state->source_hazard_kind = PORTABLE_DRIVER_HAZARD_NONE;
    else if (tick->hop_mode_at_entry == 0u)
    {
        state->source_hazard_kind = PORTABLE_DRIVER_HAZARD_NONE;
        if (tick->material_result == PORTABLE_DRIVER_MATERIAL_OK ||
            (state->allow_track_boundary &&
             tick->material_result == PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS))
        {
            if (tick->material.material_flags == UINT8_C(0x26))
                state->source_hazard_kind = PORTABLE_DRIVER_HAZARD_BOUNDS;
            else if (tick->material.material_flags == UINT8_C(0x24))
                state->source_hazard_kind = PORTABLE_DRIVER_HAZARD_LAVA;
            else if (tick->material.material_flags == UINT8_C(0x20) ||
                     (tick->material.material_flags == UINT8_C(0x28) &&
                      !(state->steering.state_flags & UINT32_C(0x4000))))
                state->source_hazard_kind = PORTABLE_DRIVER_HAZARD_VOID;
            else if (tick->material.material_flags == PORTABLE_DRIVER_CAPTURE_MATERIAL)
            {
                PortableDriverCaptureState capture;
                if (capture_begin(state, dependencies, &capture))
                    state->source_hazard_kind = PORTABLE_DRIVER_HAZARD_CAPTURE;
            }
        }
    }
    else if (state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_CAPTURE &&
             tick->hop_mode_at_entry != PORTABLE_DRIVER_CAPTURE_MODE)
        state->source_hazard_kind = PORTABLE_DRIVER_HAZARD_NONE;
    if (!(state->source_hazard_kind ||
          (state->source_terrain22_active &&
           (tick->hop_mode_at_entry == 3u || tick->hop_mode_at_entry == 4u ||
            tick->hop_mode_at_entry == 6u || tick->hop_mode_at_entry == 7u))) &&
        portable_driver_drift_charge_pre_tick(&state->drift_charge, state->steering.control_word,
                                              state->hop.mode) != PORTABLE_DRIVER_HOP_OK)
        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
    if (!(state->source_hazard_kind ||
          (state->source_terrain22_active &&
           (tick->hop_mode_at_entry == 3u || tick->hop_mode_at_entry == 4u ||
            tick->hop_mode_at_entry == 6u || tick->hop_mode_at_entry == 7u))) &&
        portable_driver_hop_tick(&state->hop, state->source_update_order
                                                  ? state->steering.control_word
                                                  : tick->source_control_word) !=
            PORTABLE_DRIVER_HOP_OK)
        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
    if (state->source_update_order && state->hop.landed)
        (void)portable_driver_affine_request(&state->affine, dependencies->affine_rules, 1u);
    if (state->source_update_order && state->hop.landed && state->steering.driver_mode != 8u)
    {
        state->steering.driver_mode = 0u;
        state->auto_acceleration.driver_mode = 0u;
    }
    state->pose.position_z = state->hop.height_q16;
    state->steering.state_flags = state->hop.state_flags;
    state->forward_acceleration.state_flags = state->hop.animation_flags;
    state->steering.response_flags = state->hop.animation_flags;
    state->steering.response_selector_index = state->forward_limit_selector;
    state->steering.response_selector_flags = state->auto_acceleration.character_class;

    if (portable_driver_response_row_select(&state->steering, tick->source_control_word,
                                            dependencies->response_rules) !=
        PORTABLE_DRIVER_STEERING_OK)
        return PORTABLE_DRIVER_PILOT_STEERING_ERROR;

    if ((state->steering.response_flags & UINT16_C(0x0040)) != 0u)
        state->charged_exit_active = 1u;
    return PORTABLE_DRIVER_PILOT_OK;
}

static int pilot_schedule_terrain_contacts(PortableDriverPilotState *state,
                                           const PortableDriverPilotDependencies *dependencies,
                                           DriverPilotTick *tick)
{
    int result;
    /* sub_803C4C0 -> sub_803B550 -> sub_803B2B4 schedules the kart-owned
     * vector call before the rest of the driver update. The source +0x60 word
     * selects a post-trigger branch; bit 0x4000 does not suppress B2B4. */
    if (dependencies->track_material != NULL && dependencies->terrain != NULL &&
        dependencies->terrain_effect_tables != NULL)
    {
        if (!tick->material_sampled)
            tick->material_result = portable_driver_material_sample(
                dependencies->track_material, dependencies->raster, dependencies->terrain,
                state->pose.position_x, state->pose.position_y, &tick->material);

        if (tick->material_result == PORTABLE_DRIVER_MATERIAL_OK)
        {
            const uint16_t current_cell_key =
                source_cell_key(tick->material.world_x, tick->material.world_y);
            /* B2B4 records the source material byte, then B550 calls
             * sub_8040810 for an occupied 0x90 object tile. Its
             * sub_8040570 resultant is written before the B550 timer and
             * the following sub_8039B38 response. */
            if (state->source_update_order && dependencies->sine_table != NULL)
            {
                const PortableCoursePropContactInput input = {
                    .world_x = tick->material.world_x,
                    .world_y = tick->material.world_y,
                    .height_q16 = tick->contact_height_at_entry,
                    .anchor_x = tick->source_contact_anchor_x,
                    .anchor_y = tick->source_contact_anchor_y,
                    .component_x = state->motion.component_x,
                    .component_y = state->motion.component_y,
                    .heading = (uint16_t)state->steering.angle,
                    .timer = state->normal_collision_timer_38,
                    .guard = state->contact_guard_66,
                    .protection = state->invulnerability_timer_6a,
                    .material = tick->material.material_flags,
                    .hop_mode = tick->hop_mode_at_entry};
                PortableCoursePropContactResult contact;
                const PortableCoursePropContactKind kind = portable_course_prop_tile_contact(
                    dependencies->course_props, &input, dependencies->sine_table,
                    dependencies->sine_count, &contact);
                if (kind == PORTABLE_COURSE_PROP_CONTACT_ERROR)
                    return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
                if (kind == PORTABLE_COURSE_PROP_CONTACT_DAMAGE &&
                    !(state->steering.state_flags & UINT32_C(0x20)))
                    state->contact_audio =
                        (PortableDriverContactAudio){.serial = state->contact_audio.serial + 1u,
                                                     .song_id = 160u,
                                                     .volume = 256u,
                                                     .restart = 1u};
                if (kind == PORTABLE_COURSE_PROP_CONTACT_BOUNCE)
                {
                    if (contact.callback_song)
                        portable_driver_pilot_request_contact_audio(state, contact.callback_song,
                                                                    1u);
                    else if (contact.owner_found && contact.source_code != UINT32_C(0x860000) &&
                             (contact.source_code < 0xa0004u || contact.source_code > 0xa0006u))
                        portable_driver_pilot_request_contact_audio(state, 170u, 1u);
                    state->vector_effect.contact_word_5a = UINT16_C(0x8007);
                    state->motion.component_x = contact.vector.resultant_x;
                    state->motion.component_y = contact.vector.resultant_y;
                    state->normal_collision_source_from_native_state = 1u;
                    state->normal_collision_source_use_b550_table = 1u;
                    tick->source_b550_stage = 1;
                    state->steering.state_flags |= UINT32_C(0x1000);
                    state->normal_collision_timer_38 = 8u;
                    state->normal_collision_source_valid = 1u;
                    state->normal_collision_source_use_motion = 1u;
                    state->normal_collision_pending = 1u;
                }
            }
            uint8_t wall_reflect = 1u;
            if (state->source_update_order && tick->contact_height_at_entry <= 0x40000 &&
                tick->material.material_flags >= 0x82u && tick->material.material_flags <= 0x8fu)
            {
                size_t changed_cell;
                if (portable_driver_wall_material_contact(
                        &tick->material, dependencies->terrain, dependencies->owned_terrain_cells,
                        dependencies->owned_terrain_capacity, dependencies->wall_replacement_tile,
                        state->invulnerability_timer_6a, &wall_reflect,
                        &changed_cell) != PORTABLE_DRIVER_MATERIAL_OK)
                    return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
                /* B110 starts 0x7c even when +6A suppresses reflection. */
                if (tick->material.material_flags == 0x84u)
                    portable_driver_pilot_request_contact_audio(state, 124u, 1u);
                ++state->wall_mutation_count;
                state->wall_mutation_cell = (uint32_t)changed_cell;
                /* Ordered deterministic mutation history belongs to replay state. */
                state->wall_mutation_digest =
                    (state->wall_mutation_digest ^ (uint32_t)changed_cell) * UINT32_C(16777619);
                state->wall_mutation_digest =
                    (state->wall_mutation_digest ^ dependencies->wall_replacement_tile) *
                    UINT32_C(16777619);
            }
            const int trigger_result = portable_driver_vector_effect_source_trigger(
                wall_reflect ? tick->material.material_flags : 0x40u,
                state->vector_effect_previous_cell_key, current_cell_key,
                state->vector_effect_previous_cell_key_valid, tick->contact_height_at_entry,
                state->vector_effect_source_state, &state->vector_trigger);
            PortableDriverVectorScheduleResult schedule;
            const uint8_t sub_b2b4_result =
                state->source_update_order
                    ? state->vector_trigger.contact
                    : (uint8_t)(trigger_result == PORTABLE_DRIVER_VECTOR_TRIGGER_OK &&
                                state->vector_trigger.candidate);

            if (portable_driver_vector_effect_source_schedule_normal_collision(
                    state->vector_effect_source_state, sub_b2b4_result, &schedule) !=
                PORTABLE_DRIVER_VECTOR_SCHEDULER_OK)
                return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
            state->vector_effect_source_state = schedule.next_source_state;

            if (state->source_update_order && state->vector_trigger.contact)
            {
                /* B110 already issued the same cue before this busy check.
                 * The damaged-spin branch returns before B2B4's audio block. */
                if (tick->material.material_flags != 0x84u &&
                    !((state->steering.state_flags & UINT32_C(0x8000)) != 0u &&
                      (state->steering.response_status == 5u ||
                       state->steering.response_status == 6u)))
                    portable_driver_pilot_request_contact_audio(
                        state, dependencies->wall_contact_song, 0u);
                /* B2B4 primes the hold and cancels charged drift for every
                 * wall sample, including continued contact in the same cell. */
                state->normal_collision_timer_62 = 8u;
                state->forward_acceleration.state_flags &= UINT16_C(0xffbf);
                state->steering.response_flags &= UINT16_C(0xffbf);
                state->charged_exit_active = 0u;
                state->normal_collision_source_valid = 1u;
                state->normal_collision_source_from_native_state = 1u;
                state->normal_collision_source_use_b550_table = 1u;
                state->normal_collision_source_use_motion = 1u;
            }
            if ((trigger_result == PORTABLE_DRIVER_VECTOR_TRIGGER_OK &&
                 state->vector_trigger.candidate) ||
                (state->source_update_order && state->vector_trigger.contact &&
                 state->vector_trigger.alternate_state_branch))
            {
                ++state->vector_trigger_count;
                if (state->source_update_order && dependencies->normal_resultant_x_table != NULL &&
                    dependencies->normal_resultant_y_table != NULL)
                {
                    /* B2B4 primes +0x62 before B550 arms the boundary
                     * response; subsequent B550 calls retain +0x4000. */
                    state->normal_collision_timer_62 = 8u;
                    state->normal_collision_source_valid = 1u;
                    state->normal_collision_source_from_native_state = 1u;
                    state->normal_collision_source_use_b550_table = 1u;
                    state->normal_collision_source_use_motion = 1u;
                }
                if (!state->vector_effect_pending)
                {
                    state->vector_effect_pending = 1;
                    state->vector_effect_mode = (int8_t)state->vector_trigger.mode;
                    state->vector_effect_alternate_state_branch =
                        state->vector_trigger.alternate_state_branch;
                }
                /* B2B4/B018 reflects the prior component pair before B550
                 * holds it or computes the eventual release resultant. */
                if (state->source_update_order)
                {
                    result = apply_pending_vector_effect(state, dependencies);
                    if (result != PORTABLE_DRIVER_PILOT_OK)
                        return result;
                }
            }
            if (schedule.normal_collision_scheduled)
            {
                ++state->normal_collision_schedule_count;
                if (state->normal_collision_source_valid)
                {
                    /* sub_803B550 sets +0x48 bit 0x1000 when the armed
                     * normal response is handed to sub_8039B38. The
                     * source handler clears that bit on return; keep the
                     * transition in the native source-state view instead
                     * of manufacturing the post-handler flags in the
                     * response adapter. */
                    if (state->normal_collision_source_from_native_state)
                        state->steering.state_flags |= UINT32_C(0x00001000);
                    state->normal_collision_pending = 1;
                }
            }
            if (tick->source_b550_stage)
            {
                if (schedule.normal_collision_scheduled)
                {
                    PortableDriverB550Resultant b550_result;
                    int16_t x, y;
                    if (portable_driver_b550_normal_resultant(
                            state->motion.component_x, state->motion.component_y,
                            state->motion.speed_q16, state->vector_effect_source_direction,
                            dependencies->normal_resultant_x_table,
                            dependencies->normal_resultant_x_count,
                            dependencies->normal_resultant_y_table,
                            dependencies->normal_resultant_y_count, &x, &y,
                            &b550_result) != PORTABLE_DRIVER_B550_RESULTANT_OK)
                        return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
                    state->motion.component_x = x;
                    state->motion.component_y = y;
                }
                PortableDriverB550Flags flags = {
                    .flags = state->steering.state_flags,
                    .timer_38 = state->normal_collision_timer_38,
                    .timer_62 = state->normal_collision_timer_62,
                };
                if (portable_driver_b550_finish_flags(schedule.next_source_state,
                                                      schedule.normal_collision_scheduled,
                                                      &flags) != PORTABLE_DRIVER_B550_RESULTANT_OK)
                    return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
                tick->b550_flags_finished = 1;
                state->steering.state_flags = flags.flags;
                state->normal_collision_timer_38 = flags.timer_38;
                state->normal_collision_timer_62 = flags.timer_62;
                tick->skip_motion_this_tick = (flags.flags & UINT32_C(0x4000)) != 0;
            }
            else if (state->source_update_order &&
                     ((schedule.next_source_state & UINT16_C(0x8000)) != 0u ||
                      state->normal_collision_timer_38 != 0u ||
                      state->normal_collision_timer_62 != 0u))
            {
                PortableDriverB550Flags flags = {
                    .flags = state->steering.state_flags,
                    .timer_38 = state->normal_collision_timer_38,
                    .timer_62 = state->normal_collision_timer_62,
                };
                if (portable_driver_b550_finish_flags(schedule.next_source_state,
                                                      schedule.normal_collision_scheduled,
                                                      &flags) != PORTABLE_DRIVER_B550_RESULTANT_OK)
                    return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
                tick->b550_flags_finished = 1;
                state->steering.state_flags = flags.flags;
                state->normal_collision_timer_38 = flags.timer_38;
                state->normal_collision_timer_62 = flags.timer_62;
                tick->skip_motion_this_tick = (flags.flags & UINT32_C(0x4000)) != 0;
            }
            state->vector_effect_previous_cell_key = current_cell_key;
            state->vector_effect_previous_cell_key_valid = 1;
        }
    }

    /* Native terrain drivers still run B550 countdowns when optional vector
     * tables are absent. Explicit response adapters retain caller-owned flags. */
    if (state->source_update_order && !tick->b550_flags_finished &&
        (state->source_terrain22_enabled || state->flattened_64 ||
         (state->normal_collision_source_from_native_state &&
          ((state->steering.response_flags & UINT16_C(0x1200)) != 0u ||
           (state->normal_collision_timer_38 != 0u &&
            (state->steering.response_status == 5u || state->steering.response_status == 6u ||
             state->normal_collision_source_use_b550_table))))))
    {
        PortableDriverB550Flags flags = {.flags = state->steering.state_flags,
                                         .timer_38 = state->normal_collision_timer_38,
                                         .timer_62 = state->normal_collision_timer_62};
        if (portable_driver_b550_finish_flags(state->vector_effect_source_state, 0u, &flags) !=
            PORTABLE_DRIVER_B550_RESULTANT_OK)
            return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
        state->steering.state_flags = flags.flags;
        state->normal_collision_timer_38 = flags.timer_38;
        state->normal_collision_timer_62 = flags.timer_62;
        tick->skip_motion_this_tick = (flags.flags & UINT32_C(0x4000)) != 0u;
    }
    return PORTABLE_DRIVER_PILOT_OK;
}

static int pilot_apply_pre_motion_contacts(PortableDriverPilotState *state,
                                           const PortableDriverPilotDependencies *dependencies,
                                           DriverPilotTick *tick)
{
    int result;
    /* C4C0 checks this immediately after B550. The release tick retains
     * B550's 0x4000 flag; motion resumes on the following tick. */
    if (state->source_update_order && state->flattened_64 &&
        state->normal_collision_timer_38 == UINT16_C(0x6f80))
    {
        state->flattened_64 = 0u;
        state->normal_collision_timer_38 = 0u;
        if (dependencies->affine_rules != NULL)
        {
            (void)portable_driver_affine_request(&state->affine, dependencies->affine_rules, 7u);
            state->unflatten_cue_ticks = 0u;
        }
        else
            state->unflatten_cue_ticks = 112u;
    }
    else if (dependencies->affine_rules == NULL && state->unflatten_cue_ticks != 0u &&
             --state->unflatten_cue_ticks == 0u && !(state->steering.state_flags & UINT32_C(0x20)))
        state->contact_audio =
            (PortableDriverContactAudio){.serial = state->contact_audio.serial + 1u,
                                         .song_id = 133u,
                                         .volume = 256u,
                                         .restart = 1u};

    if (state->source_update_order)
    {
        portable_driver_sink_prefix(tick->hop_mode_at_entry, &state->fall_frames_76,
                                    &state->pose.position_z, &state->steering.position_82);
        state->hop.height_q16 = state->pose.position_z;
    }

    /* 9B38 calls AD6C only from entry mode 0. A fall, hop or retrieval
     * retains B550's pending contact until the grounded dispatch resumes. */
    if (state->normal_collision_pending &&
        (!state->source_update_order || tick->hop_mode_at_entry == 0u) &&
        !state->normal_collision_source_use_motion)
    {
        if (state->normal_collision_source_from_native_state)
            sync_normal_collision_source_from_native_state(state);
        tick->skip_motion_this_tick = state->normal_collision_source_skip_motion_tick != 0;
        if (tick->skip_motion_this_tick)
        {
            /* The source +0x4000 path consumes the existing resultant before
             * sub_8038200 and leaves those halfwords in place for the next
             * pose integration. */
            state->normal_collision_generated_resultant_x =
                state->normal_collision_source.resultant_x;
            state->normal_collision_generated_resultant_y =
                state->normal_collision_source.resultant_y;
        }
        result = portable_driver_pilot_apply_normal_collision_source_angle(
            state, state->normal_collision_source.resultant_x,
            state->normal_collision_source.resultant_y,
            state->normal_collision_source.current_angle,
            state->normal_collision_source.source_flags,
            state->normal_collision_source.prior_angle_history_7a,
            state->normal_collision_source.prior_angle_delta_80,
            state->normal_collision_source.prior_resistance_92,
            state->normal_collision_source.prior_magnitude_state_bc,
            state->normal_collision_source.prior_timer_value_84,
            state->normal_collision_source.prior_response_timer_7e);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
        result = apply_normal_collision_response_table(state, dependencies);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
        if (tick->skip_motion_this_tick)
        {
            state->motion.component_x = state->normal_collision_source.resultant_x;
            state->motion.component_y = state->normal_collision_source.resultant_y;
        }
        state->normal_collision_pending = 0;
    }

    /* sub_803B550 consumes the already-written +0x4e/+0x50 pair before the
     * following sub_8038200 motion stage. On the source-order path, produce
     * the native response from those prior components here; do not replace
     * them with the current tick's newly computed motion vector. */
    if (state->source_update_order && tick->hop_mode_at_entry == 0u &&
        state->normal_collision_pending && state->normal_collision_source_use_motion &&
        !state->normal_collision_source_skip_motion_tick &&
        (!tick->source_b550_stage || tick->skip_motion_this_tick))
    {
        int16_t resultant_x = state->motion.component_x;
        int16_t resultant_y = state->motion.component_y;

        if (state->normal_collision_source_from_native_state)
            sync_normal_collision_source_from_native_state(state);
        if (state->normal_collision_source_use_b550_table && !tick->source_b550_stage)
        {
            PortableDriverB550Resultant b550_result;

            if (portable_driver_b550_normal_resultant(
                    resultant_x, resultant_y, state->motion.speed_q16,
                    state->vector_effect_source_direction, dependencies->normal_resultant_x_table,
                    dependencies->normal_resultant_x_count, dependencies->normal_resultant_y_table,
                    dependencies->normal_resultant_y_count, &resultant_x, &resultant_y,
                    &b550_result) != PORTABLE_DRIVER_B550_RESULTANT_OK)
                return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
        }
        if (tick->source_b550_stage)
        {
            /* B550 stores these halfwords in the driver itself, not merely
             * in a diagnostic response packet. C4C0 may skip the next motion
             * calculation, and the next pose tick must see these values. */
            state->motion.component_x = resultant_x;
            state->motion.component_y = resultant_y;
        }
        state->normal_collision_generated_resultant_x = resultant_x;
        state->normal_collision_generated_resultant_y = resultant_y;
        result = portable_driver_pilot_apply_normal_collision_source_angle(
            state, resultant_x, resultant_y, state->normal_collision_source.current_angle,
            state->normal_collision_source.source_flags,
            state->normal_collision_source.prior_angle_history_7a,
            state->normal_collision_source.prior_angle_delta_80,
            state->normal_collision_source.prior_resistance_92,
            state->normal_collision_source.prior_magnitude_state_bc,
            state->normal_collision_source.prior_timer_value_84,
            state->normal_collision_source.prior_response_timer_7e);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
        result = apply_normal_collision_response_table(state, dependencies);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
        state->normal_collision_pending = 0;
    }
    return PORTABLE_DRIVER_PILOT_OK;
}

static int pilot_update_motion(PortableDriverPilotState *state,
                               const PortableDriverPilotDependencies *dependencies,
                               DriverPilotTick *tick)
{
    int result;
    if (!state->preserve_source_base_speed)
        state->input.base_speed_q16 = state->motion.speed_q16;
    result = portable_driver_input_tick(&state->input, tick->source_control_word);
    if (result != PORTABLE_DRIVER_INPUT_OK)
        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
    tick->control_word = state->input.control_word;
    /* sub_8037FD8 stores the current +0x96 control word after the source's
     * sub_8038A08 call. Keep the prior stored word visible to the response
     * and steering stages on the source-order path; the current word still
     * drives this tick's acceleration helpers. */
    sync_acceleration_inputs(state, tick->control_word,
                             state->source_update_order ? state->steering.control_word
                                                        : tick->control_word);

    if (state->source_update_order)
    {
        if (state->battle_enabled)
            state->balloon_event = portable_battle_balloon_tick(
                &state->recovery_flags_74, &state->balloons_remaining, state->battle_scene_state);
        /* The source's sub_8038200 consumes +0xbc/+0xc0 before the
         * post-motion acceleration path computes the next +0xc0. */
        if (!tick->skip_motion_this_tick && !tick->finished)
        {
            result = portable_kart_motion_tick(&state->motion, dependencies->sine_table,
                                               dependencies->sine_count);
            if (result != PORTABLE_KART_MOTION_OK)
                return PORTABLE_DRIVER_PILOT_MOTION_ERROR;
        }
        /* C702 skips only8200 on contact hold;9B38 still dispatches rescue. */
        if (native_recovery_active(state))
        {
            if (state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_CAPTURE)
            {
                if (!capture_step(state, tick->hop_mode_at_entry, dependencies))
                    return PORTABLE_DRIVER_PILOT_POSE_ERROR;
                /* Source 7B30/8E34 precede this 9B38 entry; their native
                 * successors below must not overwrite its +0x7e/+0x88. */
                if (tick->hop_mode_at_entry == 0u)
                    tick->skip_motion_this_tick = 1;
            }
            else if (state->source_hazard_kind)
            {
                if (state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_BATTLE_BOMB)
                {
                    if (portable_driver_bomb_respawn_tick(&state->hop, tick->hop_mode_at_entry,
                                                          &state->drift_charge.charge_counter) !=
                        PORTABLE_DRIVER_HOP_OK)
                        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
                }
                else if (state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_LAVA)
                {
                    if (portable_driver_lava_tick(
                            &state->hop, tick->hop_mode_at_entry, &state->motion.speed_q16,
                            &state->recovery_flags_74) != PORTABLE_DRIVER_HOP_OK)
                        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
                }
                else if (state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_BOUNDS)
                {
                    if (portable_driver_bounds_tick(&state->hop, tick->hop_mode_at_entry,
                                                    &state->motion.speed_q16) !=
                        PORTABLE_DRIVER_HOP_OK)
                        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
                }
                else if (portable_driver_fall_tick(
                             &state->hop, tick->hop_mode_at_entry, &state->motion.speed_q16,
                             &state->drift_charge.charge_counter,
                             &state->recovery_contact_34) != PORTABLE_DRIVER_HOP_OK)
                    return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
            }
            else if (portable_driver_terrain22_tick(
                         &state->hop, tick->hop_mode_at_entry, tick->material.material_flags,
                         &state->motion.speed_q16, &state->drift_charge.charge_counter,
                         dependencies->sine_table,
                         dependencies->sine_count) != PORTABLE_DRIVER_HOP_OK)
                return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
            state->pose.position_z = state->hop.height_q16;
            state->steering.state_flags = state->hop.state_flags;
            state->steering.response_flags = state->hop.animation_flags;
            state->forward_acceleration.state_flags = state->hop.animation_flags;
            if (tick->hop_mode_at_entry == 6u || tick->hop_mode_at_entry == 7u)
            {
                uint16_t heading = (uint16_t)state->steering.steering_offset;
                if (portable_driver_recovery_tick(
                        &state->hop, &state->pose.position_x, &state->pose.position_y, &heading,
                        &state->recovery_flags_74, &state->recovery_contact_34,
                        (int16_t)state->recovery_target_x_9e, (int16_t)state->recovery_target_y_a0,
                        state->recovery_target_angle_a2) != PORTABLE_DRIVER_HOP_OK)
                    return PORTABLE_DRIVER_PILOT_POSE_ERROR;
                state->steering.steering_offset = (int16_t)heading;
                state->steering.angle = (int16_t)heading;
                state->steering.source_angle_56 = heading;
                state->motion.angle = (int16_t)heading;
                if (state->hop.recovery_checkpoint_sample && state->source_update_order)
                {
                    /* A8EE calls B98C again after the Y move, before steering.
                     * The carry sample updates checkpoints but consumes no tick. */
                    uint8_t cell;
                    state->pose.sampled_world_x = state->pose.position_x / 65536;
                    state->pose.sampled_world_y = state->pose.position_y / 65536;
                    const int sample = portable_track_raster_query16(
                        dependencies->raster, state->pose.sampled_world_x,
                        state->pose.sampled_world_y, &cell);
                    if (sample != PORTABLE_TRACK_RASTER_OK &&
                        sample != PORTABLE_TRACK_RASTER_OUT_OF_BOUNDS)
                        return PORTABLE_DRIVER_PILOT_POSE_ERROR;
                    state->pose.track_cell_valid = (uint8_t)(sample == PORTABLE_TRACK_RASTER_OK);
                    if (state->pose.track_cell_valid)
                        state->pose.track_cell = cell;
                    state->source_progress.height_q16 = state->hop.height_q16;
                    state->source_progress.hop_mode = state->hop.mode;
                    state->source_progress.checkpoint_resample = 1u;
                    if (!sample_source_progress(state, dependencies))
                        return PORTABLE_DRIVER_PILOT_PROGRESS_ERROR;
                    state->hop.state_flags = state->steering.state_flags;
                }
            }
            /* Carry/descent retain driver84: 2 for void, 3 for water/lava. */
            /* Capture owns +0x84 itself: unchanged in mode 8, 1 at launch. */
            if (state->source_hazard_kind != PORTABLE_DRIVER_HAZARD_CAPTURE)
                state->steering.driver_mode =
                    state->hop.mode == 6u || state->hop.mode == 7u
                        ? ((state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_VOID ||
                            state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_BATTLE_BOMB)
                               ? 2u
                           : state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_BOUNDS ? 5u
                                                                                        : 3u)
                        : state->hop.mode;
            if (state->source_hazard_kind == PORTABLE_DRIVER_HAZARD_BATTLE_BOMB &&
                state->hop.mode >= 2u)
                state->steering.driver_mode = 2u;
            state->auto_acceleration.driver_mode = state->steering.driver_mode;
            state->pose.position_z = state->hop.height_q16;
            if (state->hop.recovery_started)
            {
                state->hazard_audio_kind = state->source_hazard_kind;
                state->hazard_audio_variant = (state->item_cue_flags_58 & 0x10u) ? 0u : 1u;
            }
            if (state->hop.retrieval_started || state->hop.recovery_started)
            {
                state->item_cue_flags_58 |= UINT16_C(0x10);
                state->motion.acceleration_q16 = 0;
                state->steering.response_q8 = 0;
                state->forward_acceleration.response_q8 = 0;
                state->steering.response_status =
                    state->source_terrain22_active && state->hop.recovery_started ? 14u : 0u;
                state->steering.base_angle = 0;
                state->steering.position_82 = 0u;
                state->normal_collision.resistance_92 = 0;
            }
            /* A6C4 clears components only on the turn AFTER99A4.
             * Prior-component pose integration has already happened. */
            if (tick->hop_mode_at_entry == 3u || tick->hop_mode_at_entry == 5u ||
                tick->hop_mode_at_entry == 6u || tick->hop_mode_at_entry == 7u)
            {
                state->motion.component_x = 0;
                state->motion.component_y = 0;
                state->motion.acceleration_q16 = 0;
            }
            if (state->hop.retrieval_started ||
                (tick->hop_mode_at_entry == 4u && state->drift_charge.charge_counter <= 119u))
            {
                PortableDriverRecoveryTarget target;
                if (!portable_driver_recovery_target(
                        dependencies->source_track_route, dependencies->source_track_route_count,
                        state->source_route_index_90, dependencies->raster,
                        dependencies->source_track_angle_lut,
                        dependencies->source_track_angle_lut_count, &target))
                    return PORTABLE_DRIVER_PILOT_POSE_ERROR;
                state->recovery_target_x_9e = (uint16_t)target.position.x;
                state->recovery_target_y_a0 = (uint16_t)target.position.y;
                state->recovery_target_angle_a2 = target.heading;
            }
        }
        if (!tick->skip_motion_this_tick && !state->source_hazard_kind &&
            !state->source_terrain22_active &&
            !consume_mushroom_request(state, tick->hop_mode_at_entry, dependencies))
            return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
        if (!tick->skip_motion_this_tick && tick->material_result == PORTABLE_DRIVER_MATERIAL_OK &&
            !state->source_hazard_kind && !state->source_terrain22_active)
        {
            state->hop.state_flags = state->steering.state_flags;
            state->hop.animation_flags = state->forward_acceleration.state_flags;
            if (portable_driver_surface_jump(
                    &state->hop, tick->hop_mode_at_entry, tick->material.material_flags,
                    &state->motion.speed_q16, dependencies->ramp_speed_limit_q16,
                    dependencies->sine_table, dependencies->sine_count) != PORTABLE_DRIVER_HOP_OK)
                return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
            if (portable_driver_protected_surface_hop(
                    &state->hop, tick->hop_mode_at_entry, tick->material.material_flags,
                    state->forward_limit_selector, &state->motion.speed_q16,
                    dependencies->track_material->protected_hop_thresholds,
                    dependencies->track_material->protected_hop_threshold_count) !=
                PORTABLE_DRIVER_HOP_OK)
                return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
            if (tick->hop_mode_at_entry == 0u && tick->material.material_flags == 0x28u &&
                (state->hop.state_flags & UINT32_C(0x2000)))
                state->steering.response_status = 14u;
            const uint16_t prior_boost_flags = state->hop.animation_flags;
            uint8_t surface_boost_started;
            if (portable_driver_surface_boost(
                    tick->hop_mode_at_entry, tick->material.material_flags,
                    &state->steering.driver_mode, &state->steering.exit_timer,
                    &state->hop.animation_flags, &state->steering.response_status,
                    &surface_boost_started) != PORTABLE_DRIVER_MUSHROOM_OK)
                return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
            if (surface_boost_started)
                queue_boost_audio(state, dependencies, prior_boost_flags);
            if (state->steering.driver_mode == 8u)
                state->auto_acceleration.driver_mode = 8u;
            state->pose.position_z = state->hop.height_q16;
            state->steering.state_flags = state->hop.state_flags;
            state->steering.response_flags = state->hop.animation_flags;
            state->forward_acceleration.state_flags = state->hop.animation_flags;
            state->steering.speed_q16 = state->motion.speed_q16;
            if (portable_driver_surface_spin(&state->steering, tick->hop_mode_at_entry,
                                             tick->material.material_flags) !=
                PORTABLE_DRIVER_STEERING_OK)
                return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
            if (tick->hop_mode_at_entry == 0u && tick->material.material_flags <= 0x1fu &&
                ((tick->material.material_flags & 0x0fu) == 8u ||
                 ((tick->material.material_flags & 0x0fu) == 12u &&
                  !(state->steering.state_flags & UINT32_C(0x2000)))))
            {
                state->forward_acceleration.response_q8 = state->steering.response_q8;
                state->forward_acceleration.state_flags = state->steering.response_flags;
                state->hop.animation_flags = state->steering.response_flags;
            }
            if (state->hop.surface_jump_started)
            {
                state->steering.driver_mode = 1u;
                state->auto_acceleration.driver_mode = 1u;
            }
        }
        state->auto_acceleration.speed_q16 = state->motion.speed_q16;
        state->brake.speed_q16 = state->motion.speed_q16;
        state->forward_acceleration.speed_q16 = state->motion.speed_q16;
        if (tick->finished)
        {
            tick->finished_resolved = 1;
            return finished_resolve(state, dependencies, tick->finished);
        }
        /* The C4C0 contact hold skips 7B30/8298 as well as 8200. Keep
         * +0xc0 from the prior frame until the held response releases;
         * the source frame-3088 contact retains -0x00100000 here. */
        if (!tick->skip_motion_this_tick)
        {
            tick->defer_source_response_acceleration =
                !source_recovery_stops_motion(state) &&
                (state->steering.driver_mode == 12u ||
                 (state->steering.response_status == UINT16_C(0x000b) &&
                  dependencies->normal_response_table != NULL));
            result = tick->defer_source_response_acceleration
                         ? 1
                         : compute_source_mode_acceleration(state, dependencies);
            if (result < 0)
                return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
            if (result == 0)
                tick->source_control_acceleration_due = 1;
            else if (!tick->defer_source_response_acceleration)
                apply_source_normal_speed_floor(state);
        }
    }
    else
    {
        if (!consume_mushroom_request(state, tick->hop_mode_at_entry, dependencies))
            return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
        result = compute_source_mode_acceleration(state, dependencies);
        if (result < 0)
            return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
        if (result == 0)
        {
            result = compute_control_acceleration(state, tick->control_word, dependencies);
            if (result != PORTABLE_DRIVER_PILOT_OK)
                return result;
            sync_motion_inputs(state);
        }
        if (!tick->skip_motion_this_tick)
        {
            result = portable_kart_motion_tick(&state->motion, dependencies->sine_table,
                                               dependencies->sine_count);
            if (result != PORTABLE_KART_MOTION_OK)
                return PORTABLE_DRIVER_PILOT_MOTION_ERROR;
            state->auto_acceleration.speed_q16 = state->motion.speed_q16;
            state->auto_acceleration.acceleration_q16 = state->motion.acceleration_q16;
            state->forward_acceleration.speed_q16 = state->motion.speed_q16;
            state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        }
    }
    return PORTABLE_DRIVER_PILOT_OK;
}

static int pilot_apply_post_motion_contacts(PortableDriverPilotState *state,
                                            const PortableDriverPilotDependencies *dependencies,
                                            DriverPilotTick *tick)
{
    int result;
    /* On the source path where the +0x4000 branch does not bypass
     * sub_8038200, sub_803B550 schedules before it produces the resultant
     * halfwords consumed by sub_8039B38. Keep the scheduled event pending
     * until this motion tick has produced the native result. */
    if (state->normal_collision_pending &&
        (!state->source_update_order || tick->hop_mode_at_entry == 0u) &&
        state->normal_collision_source_use_motion && !tick->skip_motion_this_tick &&
        (!state->source_update_order || tick->source_b550_stage))
    {
        if (state->normal_collision_source_from_native_state)
            sync_normal_collision_source_from_native_state(state);
        state->normal_collision_generated_resultant_x = state->motion.component_x;
        state->normal_collision_generated_resultant_y = state->motion.component_y;
        result = portable_driver_pilot_apply_normal_collision_source_angle(
            state, state->motion.component_x, state->motion.component_y,
            state->normal_collision_source.current_angle,
            state->normal_collision_source.source_flags,
            state->normal_collision_source.prior_angle_history_7a,
            state->normal_collision_source.prior_angle_delta_80,
            state->normal_collision_source.prior_resistance_92,
            state->normal_collision_source.prior_magnitude_state_bc,
            state->normal_collision_source.prior_timer_value_84,
            state->normal_collision_source.prior_response_timer_7e);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
        result = apply_normal_collision_response_table(state, dependencies);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
        state->normal_collision_pending = 0;
    }

    /* 9B38 consumes DD58/0128's +1000 request after8200, including the
     * timer-one FBFC escape which deliberately does not queue another hit. */
    if (state->source_update_order && tick->hop_mode_at_entry == 0u &&
        (state->steering.state_flags & UINT32_C(0x1000)) &&
        !(state->steering.state_flags & UINT32_C(0x2000)))
    {
        result = portable_driver_pilot_apply_normal_collision_source_angle(
            state, state->motion.component_x, state->motion.component_y,
            (uint16_t)state->steering.steering_offset, state->steering.state_flags,
            (uint16_t)state->steering.angle, (uint16_t)state->steering.base_angle,
            state->normal_collision.resistance_92, state->motion.speed_q16,
            state->steering.driver_mode, state->steering.response_status);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
    }
    /* Grounded A9AC consumes strong hits before ordinary FF80 hits. */
    if (state->source_update_order && tick->hop_mode_at_entry == 0u &&
        ((state->forward_acceleration.state_flags & UINT16_C(0x0200)) ||
         ((state->forward_acceleration.state_flags & UINT16_C(0x1000)) &&
          !(state->forward_acceleration.state_flags & UINT16_C(0x0800)))))
    {
        state->steering.speed_q16 = state->motion.speed_q16;
        if (portable_driver_queued_hit(&state->steering) != PORTABLE_DRIVER_STEERING_OK)
            return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
        state->motion.speed_q16 = state->steering.speed_q16;
        state->auto_acceleration.speed_q16 = state->motion.speed_q16;
        state->forward_acceleration.speed_q16 = state->motion.speed_q16;
        state->forward_acceleration.response_q8 = state->steering.response_q8;
        state->forward_acceleration.state_flags = state->steering.response_flags;
        state->hop.animation_flags = state->steering.response_flags;
        state->auto_acceleration.driver_mode = state->steering.driver_mode;
        if (state->steering.driver_mode == 10u && !tick->skip_motion_this_tick)
            tick->source_control_acceleration_due = 1;
    }
    return PORTABLE_DRIVER_PILOT_OK;
}

static int pilot_update_steering_response(PortableDriverPilotState *state,
                                          const PortableDriverPilotDependencies *dependencies,
                                          DriverPilotTick *tick)
{
    int result;
    if (dependencies->modern_drift_rules != NULL)
    {
        /* Keep the source charge counter for recovery, but its reward belongs
         * exclusively to the selected rules profile. */
        state->forward_acceleration.state_flags &= (uint16_t)~UINT16_C(0x40);
        state->steering.response_flags &= (uint16_t)~UINT16_C(0x40);
        state->hop.animation_flags &= (uint16_t)~UINT16_C(0x40);
    }
    if (dependencies->steering_delta_table != NULL)
    {
        tick->status_before_steering = state->steering.response_status;
        state->steering.speed_q16 = state->motion.speed_q16;
        state->steering.response_q8 = state->forward_acceleration.response_q8;
        result = portable_driver_steering_tick(&state->steering, dependencies->steering_delta_table,
                                               dependencies->steering_delta_count);
        if (result != PORTABLE_DRIVER_STEERING_OK)
            return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
        state->motion.angle = state->steering.angle;
        state->forward_acceleration.response_q8 = state->steering.response_q8;
    }

    const int status14_at_response = state->steering.response_status == UINT16_C(0x000e);

    /* C4C0 composes the prior impact angle before 8E34 consumes current
     * controls. The complete case-11 response owns its signed thresholds. */
    if (state->source_update_order && !tick->skip_motion_this_tick &&
        state->steering.response_status == 11u)
    {
        state->steering.control_word = tick->control_word;
        if (portable_driver_collision_response_tick(&state->steering) !=
            PORTABLE_DRIVER_STEERING_OK)
            return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
        state->normal_collision.angle_delta_80 = (uint16_t)state->steering.base_angle;
        if (state->steering.response_status == 14u)
        {
            state->normal_collision.response_timer_7e = 14u;
            state->normal_collision.timer_value_84 = 0u;
            state->auto_acceleration.driver_mode = state->steering.driver_mode;
        }
    }

    /* C4C0's hold skips 7B30; 7FD8 returns at38044 on the same flag,
     * retaining +96. 8A08 still uses it for low-speed heading composition. */
    if (state->source_update_order && !tick->skip_motion_this_tick)
        state->steering.control_word = tick->control_word;

    if (state->source_update_order && status14_at_response && !tick->skip_motion_this_tick)
    {
        PortableDriverChargedExitEvent event;
        if (portable_driver_source_status14_tick(&state->steering, state->input.speed_target_q16, 1,
                                                 1, dependencies->response_rules,
                                                 &event) != PORTABLE_DRIVER_STEERING_OK)
            return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
        if (event.audio_cue_140)
            ++state->charged_audio_cue_count;
        if (event.particle_effect)
        {
            ++state->charged_particle_count;
            state->charged_particle_age = 1u;
        }
        if (event.strong_exit)
        {
            ++state->charged_exit_count;
            state->charged_exit_active = 0u;
            tick->defer_source_response_acceleration = 1;
            tick->source_control_acceleration_due = 0;
        }
        state->auto_acceleration.driver_mode = state->steering.driver_mode;
        if (state->steering.response_status == 11u)
        {
            state->normal_collision.angle_delta_80 = (uint16_t)state->steering.base_angle;
            tick->defer_source_response_acceleration = 1;
            tick->source_control_acceleration_due = 0;
        }
    }

    /* sub_8037B30 branches directly to its return when +0x48 carries
     * 0x4000; that frame does not call sub_8038E34. In particular, the
     * frame-2383 Peach contact must retain +0x82 and +0xc8. */
    /* 8E34 dispatches one entry status. A status14 ->0 reset must not
     * immediately run the ordinary status0 case too (+2054 Beach). */
    if (state->source_update_order && !tick->skip_motion_this_tick && !status14_at_response)
    {
        const uint8_t prior_response_status = state->steering.response_status;
        const uint16_t prior_spin_delay = state->steering.spin_delay;
        PortableDriverChargedExitEvent event = {0};
        const int response_result =
            prior_response_status == 13u
                ? portable_driver_strong_spin_tick(&state->steering, &state->strong_spin_rate_b6)
            : prior_response_status == 12u
                ? (portable_driver_launch_flip_tick(
                       &state->steering.response_status, &state->steering.response_flags,
                       &state->steering.position_82,
                       state->hop.velocity_q8) == PORTABLE_DRIVER_HOP_OK
                       ? PORTABLE_DRIVER_STEERING_OK
                       : PORTABLE_DRIVER_STEERING_INVALID_ARGUMENT)
                : portable_driver_source_turn_response_tick(&state->steering,
                                                            state->input.speed_target_q16,
                                                            dependencies->response_rules, &event);
        if (response_result != PORTABLE_DRIVER_STEERING_OK)
            return PORTABLE_DRIVER_PILOT_STEERING_ERROR;
        if (prior_response_status == 15u)
        {
            tick->source_status15_processed = 1;
            state->forward_acceleration.response_q8 = state->steering.response_q8;
        }
        if (event.audio_cue_140)
            ++state->charged_audio_cue_count;
        if (event.particle_effect)
        {
            ++state->charged_particle_count;
            state->charged_particle_age = 1u;
        }
        if (event.strong_exit)
        {
            ++state->charged_exit_count;
            state->charged_exit_active = 0u;
            state->auto_acceleration.driver_mode = state->steering.driver_mode;
            tick->source_control_acceleration_due = 0;
            tick->defer_source_response_acceleration = 1;
        }
        else if (state->steering.response_status == 0u)
            state->charged_exit_active = 0u;
        if (prior_response_status == 13u)
            state->auto_acceleration.driver_mode = state->steering.driver_mode;
        if (prior_spin_delay != 0u && state->steering.spin_delay == 0u &&
            (state->steering.response_status >= 5u && state->steering.response_status <= 8u))
            state->normal_collision.resistance_92 = 0;
        /* 8BF4: the player's brake-out stops song180's player before 165;
         * expiry plays 162 unless it is already sounding. */
        if (prior_spin_delay != 0u && state->steering.spin_delay_cue != 0u &&
            !(state->steering.state_flags & UINT32_C(0x20)))
        {
            const int braked = state->steering.spin_delay_cue == 165u;
            state->contact_audio = (PortableDriverContactAudio){
                .serial = state->contact_audio.serial + 1u,
                .song_id = state->steering.spin_delay_cue,
                .stop_song = braked ? 180u : 0u,
                .stop_first = (uint8_t)braked,
                .volume = 256u,
                .restart = (uint8_t)braked,
            };
        }
        if ((prior_response_status >= 5u && prior_response_status <= 8u) &&
            (state->steering.response_status >= 5u && state->steering.response_status <= 8u))
        {
            tick->source_control_acceleration_due = 0;
            if (compute_source_mode_acceleration(state, dependencies) < 0)
                return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
            apply_source_normal_speed_floor(state);
        }
        /* The source writes +0xc0=0 on the status-1 -> status-8 frame;
         * sub_8038200 consumes that zero on the next tick. */
        if ((prior_response_status == 1u || tick->status_before_steering == 1u) &&
            state->steering.response_status == 8u)
        {
            /* 8E34 zeroes +0x88 before the next C4C0/8A08 call. Keep the
             * shared acceleration-side copy from restoring its old turn
             * response on that next frame. */
            state->forward_acceleration.response_q8 = 0;
            tick->source_control_acceleration_due = 0;
            state->motion.acceleration_q16 = 0;
            state->auto_acceleration.acceleration_q16 = 0;
            state->forward_acceleration.acceleration_q16 = 0;
            state->brake.acceleration_q16 = 0;
        }
        else if ((prior_response_status >= 5u && prior_response_status <= 8u) &&
                 state->steering.response_status == 14u)
        {
            /* 7B30/8E34 exits status 8 before 8298/8710 dispatches
             * acceleration. The zero-speed frame 2932 stores held-A +5
             * for the following motion tick. 8298 still dispatches the
             * driver mode first: an airborne spin exit must coast. */
            result = compute_source_mode_acceleration(state, dependencies);
            if (result < 0)
                return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
            if (result == 0)
            {
                result = compute_control_acceleration(state, tick->control_word, dependencies);
                if (result != PORTABLE_DRIVER_PILOT_OK)
                    return result;
                state->motion.acceleration_q16 = state->auto_acceleration.acceleration_q16;
                state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
                state->brake.acceleration_q16 = state->motion.acceleration_q16;
            }
        }
    }
    if (state->source_update_order && !tick->skip_motion_this_tick &&
        state->steering.driver_mode == 11u)
    {
        tick->defer_source_response_acceleration = 1;
        tick->source_control_acceleration_due = 0;
    }
    if (tick->defer_source_response_acceleration)
    {
        /* C4C0 calls 8E34 before DAEC writes the next +0xc0. Use the
         * response angle after its status-11 step, including the frame
         * where that step exits to status 14. */
        result = compute_source_mode_acceleration(state, dependencies);
        if (result < 0)
            return PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR;
        /* A collision exit can return to ordinary8710/8640 dispatch.
         * Keep that work after8AFC, like every other ordinary control tick. */
        if (result == 0)
            tick->source_control_acceleration_due = 1;
    }

    if (tick->defer_source_response_acceleration)
    {
        /* The live post-response dispatch owns C0 now. A queued impact
         * sample from before the hold must not overwrite its newer result. */
        state->normal_collision_response_output_pending = 0u;
        if (!tick->source_control_acceleration_due)
            apply_source_normal_speed_floor(state);
    }

    /* C4C0 composes heading through 8A08 before 7B30 calls 8E34 and
     * then 8AFC. The new +0x88 response belongs to the next heading tick.
     * The 0x4000 branch skips both source calls. */
    if (state->source_update_order && !tick->skip_motion_this_tick &&
        dependencies->steering_response_data != NULL)
    {
        result = apply_steering_response(state, dependencies);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
    }
    if (tick->source_control_acceleration_due)
    {
        /* sub_8037B30 calls 8E34 and 8AFC before 8298 dispatches 8710.
         * At GP frame 5484, 8AFC changes +0x88 from 0xf63e to 0xf5d0;
         * 8710 must consume that value, and the next heading sees its output. */
        result = compute_control_acceleration(state, tick->control_word, dependencies);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
        state->motion.acceleration_q16 = state->auto_acceleration.acceleration_q16;
        state->forward_acceleration.acceleration_q16 = state->motion.acceleration_q16;
        state->brake.acceleration_q16 = state->motion.acceleration_q16;
        state->steering.response_q8 = state->forward_acceleration.response_q8;
        apply_source_normal_speed_floor(state);
    }
    if (state->source_update_order)
        state->forward_acceleration.state_flags = state->steering.response_flags;
    return PORTABLE_DRIVER_PILOT_OK;
}

static int pilot_modern_drift_tick(PortableDriverPilotState *state,
                                   const PortableDriverPilotDependencies *dependencies,
                                   const DriverPilotTick *tick)
{
    if (dependencies->modern_drift_rules == NULL)
        return PORTABLE_DRIVER_PILOT_OK;
    const uint8_t status = state->steering.response_status;
    const uint8_t mode = state->steering.driver_mode;
    const PortableModernDriftInput input = {
        .held_drift = (uint8_t)((tick->control_word & UINT16_C(0x8010)) == UINT16_C(0x8010) &&
                                (tick->control_word & UINT16_C(0x0300)) != 0u),
        .grounded = (uint8_t)(state->hop.height_q16 == 0 && state->hop.mode == 0u),
        .eligible = (uint8_t)(!tick->skip_motion_this_tick && !state->source_hazard_kind &&
            !state->source_terrain22_active && !state->flattened_64 &&
            !state->lightning_timer_68 && !state->item_lock_ac &&
            (mode == 0u || mode == 1u) && (status == 0u || status == 1u || status == 14u) &&
            state->input.speed_target_q16 > 0),
        .cancel = (uint8_t)((tick->control_word & UINT16_C(0x4000)) != 0u),
        .speed_q16 = state->motion.speed_q16,
        .target_q16 = state->input.speed_target_q16,
    };
    PortableModernDriftResult effect;
    if (portable_modern_drift_tick(&state->modern_drift, dependencies->modern_drift_rules,
                                  &input, &effect) != 1)
        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
    state->forward_acceleration.state_flags &= (uint16_t)~UINT16_C(0x40);
    state->steering.response_flags &= (uint16_t)~UINT16_C(0x40);
    state->hop.animation_flags &= (uint16_t)~UINT16_C(0x40);
    PortableModernAirResult air_effect={0};
    const PortableModernAirRules *air=&dependencies->modern_drift_rules->air;
    if(portable_modern_air_rules_enabled(air)){
        /* The source arms mode1 on launch at height zero; vertical motion
         * starts on its following tick, so height alone loses the launch. */
        const uint8_t airborne=(uint8_t)(state->hop.mode==1u);
        const uint8_t launch=(uint8_t)(state->hop.surface_jump_started || state->hop.protected_hop_started ? 1u :
            tick->hop_mode_at_entry==0u && airborne ? 2u : 0u);
        const PortableModernAirInput air_input={
            .hop_pressed=(uint8_t)((tick->control_word&4u)!=0u),
            .hop_held=(uint8_t)((tick->control_word&16u)!=0u),
            .airborne=airborne,.launch_kind=launch,.landed=state->hop.landed,
            .eligible=input.eligible,.cancel=input.cancel,
            .speed_q16=input.speed_q16,.target_q16=input.target_q16};
        if(portable_modern_air_tick(&state->modern_air,air,&air_input,&air_effect)!=1)
            return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
    }
    if (effect.released || air_effect.released)
    {
        ++state->charged_exit_count;
        ++state->charged_audio_cue_count;
        ++state->charged_particle_count;
        state->charged_particle_age = 1u;
    }
    if (effect.applied_tier != 0u || air_effect.applied)
    {
        /* Both clocks advance independently, but publish one strongest reward.
         * A smaller concurrent cap must not truncate the stronger boost. */
        int32_t acceleration=effect.acceleration_q16,cap=effect.speed_cap_q16;
        if(air_effect.applied){
            if(air_effect.acceleration_q16>acceleration)acceleration=air_effect.acceleration_q16;
            if(air_effect.speed_cap_q16>cap)cap=air_effect.speed_cap_q16;
        }
        /* Motion already consumed the previous acceleration. Publish exactly
         * one next-turn reward after the source acceleration writers. */
        if (state->motion.speed_q16 > cap)
            state->motion.speed_q16 = cap;
        state->motion.acceleration_q16 = acceleration;
        state->auto_acceleration.acceleration_q16 = acceleration;
        state->forward_acceleration.acceleration_q16 = acceleration;
        state->brake.acceleration_q16 = acceleration;
        state->auto_acceleration.speed_q16 = state->motion.speed_q16;
        state->forward_acceleration.speed_q16 = state->motion.speed_q16;
        state->brake.speed_q16 = state->motion.speed_q16;
        state->steering.speed_q16 = state->motion.speed_q16;
    }
    return PORTABLE_DRIVER_PILOT_OK;
}

static int pilot_finish_tick(PortableDriverPilotState *state,
                             const PortableDriverPilotDependencies *dependencies,
                             DriverPilotTick *tick)
{
    int result;
    /* Preserve the existing default adapter order for callers that have not
     * opted into the source frame ordering. */
    if (!state->source_update_order && dependencies->steering_response_data != NULL)
    {
        result = apply_steering_response(state, dependencies);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
    }

    /* sub_803B018 mutates the kart's component halfwords. Automatic scheduling
     * above covers the recovered source predicate; the explicit pending bit
     * remains available for a controlled event. The +0x4000 branch is applied
     * through its separate direct-write seam when the caller supplies its
     * source direction and exact table views. */
    if (state->vector_effect_pending)
    {
        result = apply_pending_vector_effect(state, dependencies);
        if (result != PORTABLE_DRIVER_PILOT_OK)
            return result;
    }

    /* C4C0's contact hold skips 7B30/8298, including DAEC. Keep the
     * entry C0 until motion resumes; the release tick consumes it first. */
    if (state->normal_collision_response_output_pending && !tick->skip_motion_this_tick)
    {
        state->motion.acceleration_q16 = state->normal_collision_response_output_c0;
        state->normal_collision_response_output_pending = 0;
    }

    if (!(state->source_hazard_kind ||
          (state->source_terrain22_active && (state->hop.mode == 3u || state->hop.mode == 4u ||
                                              state->hop.mode == 6u || state->hop.mode == 7u))) &&
        portable_driver_drift_charge_post_tick(
            &state->drift_charge, tick->control_word, state->motion.speed_q16,
            state->input.speed_target_q16,
            &state->forward_acceleration.state_flags) != PORTABLE_DRIVER_HOP_OK)
        return PORTABLE_DRIVER_PILOT_INPUT_ERROR;
    /* 7D6C charges before8E34; case15 has the final9C write. */
    if (tick->source_status15_processed)
        state->drift_charge.charge_counter = 0u;
    state->steering.response_flags = state->forward_acceleration.state_flags;
    state->hop.animation_flags = state->forward_acceleration.state_flags;

    if (state->source_update_order && !state->battle_enabled && state->source_route_check_due &&
        state->steering.response_status < 16u &&
        /* Source _080EC280 permits statuses 0-4, 9-12 and 14-15. */
        ((UINT16_C(0xde1f) >> state->steering.response_status) & 1u) != 0u &&
        (state->hop.mode == 0u || state->hop.mode == 7u) &&
        tick->material_result == PORTABLE_DRIVER_MATERIAL_OK && tick->material.track_cell_valid &&
        dependencies->source_track_route != NULL && dependencies->source_track_angle_lut != NULL)
    {
        const size_t index = tick->material.track_cell & UINT8_C(0x7f);
        uint16_t track_angle;
        if (index < dependencies->source_track_route_count &&
            portable_driver_track_direction_source_angle(
                state->pose.position_x, state->pose.position_y,
                dependencies->source_track_route[index].x,
                dependencies->source_track_route[index].y, dependencies->source_track_angle_lut,
                dependencies->source_track_angle_lut_count,
                &track_angle) == PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        {
            portable_driver_pilot_wrong_way(state, track_angle);
        }
    }

    result = pilot_modern_drift_tick(state, dependencies, tick);
    if (result != PORTABLE_DRIVER_PILOT_OK)
        return result;
    state->pose.component_x = state->motion.component_x;
    state->pose.component_y = state->motion.component_y;
    return PORTABLE_DRIVER_PILOT_OK;
}

static int driver_pilot_tick(PortableDriverPilotState *state, uint16_t source_control_word,
                             const PortableDriverPilotDependencies *dependencies,
                             const PortableDriverFinishedInputs *finished_options)
{
    DriverPilotTick tick = {
        .source_control_word = source_control_word,
        .finished_options = finished_options,
        .material_result = PORTABLE_DRIVER_MATERIAL_INVALID_ARGUMENT,
    };
    int result;

    /* Preserve C4C0's order: prior-vector pose, terrain/contact producers,
     * motion, queued hits, response/acceleration, then next-turn publication. */
    result = pilot_begin_tick(state, dependencies, &tick);
    if (result != PORTABLE_DRIVER_PILOT_OK)
        return result;
    result = pilot_schedule_terrain_contacts(state, dependencies, &tick);
    if (result != PORTABLE_DRIVER_PILOT_OK)
        return result;
    result = pilot_apply_pre_motion_contacts(state, dependencies, &tick);
    if (result != PORTABLE_DRIVER_PILOT_OK)
        return result;
    result = pilot_update_motion(state, dependencies, &tick);
    if (tick.finished_resolved && dependencies->modern_drift_rules != NULL){
        state->modern_drift = (PortableModernDriftState){0};
        if(portable_modern_air_rules_enabled(&dependencies->modern_drift_rules->air))
            state->modern_air=(PortableModernAirState){0};
    }
    if (result != PORTABLE_DRIVER_PILOT_OK || tick.finished_resolved)
        return result;
    result = pilot_apply_post_motion_contacts(state, dependencies, &tick);
    if (result != PORTABLE_DRIVER_PILOT_OK)
        return result;
    result = pilot_update_steering_response(state, dependencies, &tick);
    if (result != PORTABLE_DRIVER_PILOT_OK)
        return result;
    result = pilot_finish_tick(state, dependencies, &tick);
    if (result != PORTABLE_DRIVER_PILOT_OK)
        return result;
    return PORTABLE_DRIVER_PILOT_OK;
}

/* AF10: absolute wrapped halfword differences, then asymmetric hysteresis. */
void portable_driver_pilot_wrong_way(PortableDriverPilotState *state, uint16_t route_angle)
{
    if (state == NULL)
        return;
    const uint16_t delta56 = (uint16_t)(route_angle - state->steering.source_angle_56);
    const uint16_t delta7a = (uint16_t)(route_angle - state->steering.angle);
    const uint32_t deviation56 = delta56 > 32768u ? 65536u - delta56 : delta56;
    const uint32_t deviation7a = delta7a > 32768u ? 65536u - delta7a : delta7a;
    const uint8_t was_wrong = (state->steering.state_flags & UINT32_C(0x400)) != 0u;
    if (state->steering.response_slot == 3u || (was_wrong && deviation56 <= 0x2fffu))
    {
        if (was_wrong)
        {
            state->steering.state_flags &= ~UINT32_C(0x400);
            state->hop.recovery_requests |= UINT16_C(0x800);
        }
    }
    else if (!was_wrong && deviation56 > 0x67ffu && deviation7a > 0x57ffu)
    {
        state->steering.state_flags |= UINT32_C(0x400);
        state->hop.recovery_requests |= UINT16_C(0x1000);
    }
}

int portable_driver_pilot_apply_normal_collision(
    PortableDriverPilotState *state, int16_t resultant_x, int16_t resultant_y,
    uint16_t impact_angle, uint16_t current_angle, uint32_t source_flags,
    uint16_t prior_angle_history_7a, uint16_t prior_angle_delta_80, int16_t prior_resistance_92,
    int32_t prior_magnitude_state_bc, uint8_t prior_timer_value_84, uint8_t prior_response_timer_7e)
{
    if (state == NULL)
        return PORTABLE_DRIVER_PILOT_INVALID_ARGUMENT;
    if (portable_driver_vector_effect_apply_normal_collision(
            resultant_x, resultant_y, impact_angle, current_angle, source_flags,
            prior_angle_history_7a, prior_angle_delta_80, prior_resistance_92,
            prior_magnitude_state_bc, prior_timer_value_84, prior_response_timer_7e,
            &state->normal_collision) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
    sync_normal_collision_response_state(state);
    /* sub_803AD6C clears +0x1000 and leaves the remaining +0x48 bits for
     * the next source scheduler boundary. Keep that source-owned value in
     * the native state so a later event can set the transient bit again. */
    state->steering.state_flags = state->normal_collision.source_flags;
    ++state->normal_collision_count;
    return PORTABLE_DRIVER_PILOT_OK;
}

int portable_driver_pilot_apply_normal_collision_source_angle(
    PortableDriverPilotState *state, int16_t resultant_x, int16_t resultant_y,
    uint16_t current_angle, uint32_t source_flags, uint16_t prior_angle_history_7a,
    uint16_t prior_angle_delta_80, int16_t prior_resistance_92, int32_t prior_magnitude_state_bc,
    uint8_t prior_timer_value_84, uint8_t prior_response_timer_7e)
{
    if (state == NULL)
        return PORTABLE_DRIVER_PILOT_INVALID_ARGUMENT;
    if (portable_driver_vector_effect_apply_normal_collision_source_angle(
            resultant_x, resultant_y, current_angle, source_flags, prior_angle_history_7a,
            prior_angle_delta_80, prior_resistance_92, prior_magnitude_state_bc,
            prior_timer_value_84, prior_response_timer_7e,
            &state->normal_collision) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR;
    sync_normal_collision_response_state(state);
    /* The source-angle adapter follows the same handler exit as the direct
     * normal-response path: preserve the cleared +0x1000 state for the next
     * scheduler event. */
    state->steering.state_flags = state->normal_collision.source_flags;
    ++state->normal_collision_count;
    return PORTABLE_DRIVER_PILOT_OK;
}
