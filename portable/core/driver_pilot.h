#ifndef MKSC_PORTABLE_DRIVER_PILOT_H
#define MKSC_PORTABLE_DRIVER_PILOT_H

#include <stddef.h>
#include <stdint.h>
#include "item_status_rules.h"
#include "driver_affine.h"
#include "impact_effect.h"
#include "driver_art_motion.h"
#include "course_prop.h"
#include "piranha.h"
#include "boo_rules.h"
#include "green_shell_rules.h"
#include "shell_orbit_rules.h"
#include "red_shell_rules.h"
#include "banana_rules.h"
#include "modern_drift.h"

#include "battle_balloon.h"
#include "driver_acceleration.h"
#include "driver_b550_resultant.h"
#include "driver_brake.h"
#include "driver_forward_acceleration.h"
#include "driver_forward_limit.h"
#include "driver_hop.h"
#include "driver_input.h"
#include "driver_mushroom.h"
#include "driver_pose.h"
#include "driver_steering.h"
#include "driver_vector_effect.h"
#include "kart_motion.h"
#include "race_session.h"
#include "source_normal_response_table.h"

enum
{
    PORTABLE_DRIVER_PILOT_OK = 0,
    PORTABLE_DRIVER_PILOT_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_PILOT_INPUT_ERROR = 2,
    PORTABLE_DRIVER_PILOT_ACCELERATION_ERROR = 3,
    PORTABLE_DRIVER_PILOT_MOTION_ERROR = 4,
    PORTABLE_DRIVER_PILOT_POSE_ERROR = 5,
    PORTABLE_DRIVER_PILOT_STEERING_ERROR = 6,
    PORTABLE_DRIVER_PILOT_VECTOR_EFFECT_ERROR = 7,
    PORTABLE_DRIVER_PILOT_PROGRESS_ERROR = 8,
};

enum { PORTABLE_DRIVER_HAZARD_NONE, PORTABLE_DRIVER_HAZARD_VOID,
       PORTABLE_DRIVER_HAZARD_LAVA, PORTABLE_DRIVER_HAZARD_BOUNDS,
       PORTABLE_DRIVER_HAZARD_BATTLE_BOMB, PORTABLE_DRIVER_HAZARD_CAPTURE };

/* Presentation-only request from B110/B2B4; never feeds simulation state. */
typedef struct
{
    uint32_t serial;
    uint16_t song_id;
    uint16_t stop_song; /* conditional m4aSongNumStop after this cue */
    uint16_t volume;
    int16_t pitch;
    uint8_t restart;
    uint8_t stop_first; /* unconditional MPlayStop of stop_song's player first */
} PortableDriverContactAudio;

typedef struct
{
    PortableImpactEffects impact_effects;
    int16_t impact_x,impact_y,impact_depth; /* cached prior actor20/22/24 */
    uint8_t impact_selected,impact_event; /* explicit local identity; transient quality18/19 */
    /* Original +0xb2: item constructors queue requests for A9AC. */
    uint16_t item_requests_b2;
    uint16_t item_cue_flags_58; /* bit10 selects normal-size character voice */
    PortableDriverInputState input;
    PortableDriverAccelerationState auto_acceleration;
    PortableDriverBrakeState brake;
    PortableDriverForwardAccelerationState forward_acceleration;
    uint8_t forward_limit_selector;
    PortableDriverSteeringState steering;
    PortableDriverHopState hop;
    PortableDriverDriftChargeState drift_charge;
    PortableModernDriftState modern_drift; /* optional rules; separate from source recovery charge */
    PortableModernAirState modern_air;
    uint32_t charged_exit_count;
    uint32_t charged_audio_cue_count;
    uint32_t charged_particle_count;
    uint8_t charged_particle_age;
    uint8_t boost_particle_age; /* BD34: twelve duration+1 spans, 50 ticks */
    uint8_t start_boosts; /* A9AC start boosts accepted; the owner records 349C. */
    int32_t boost_particle_birth_height_q16;
    uint8_t charged_exit_active;
    PortableKartMotionState motion;
    PortableDriverPoseState pose;
    int16_t contact_anchor_x_ee,contact_anchor_y_f0;
    /* Derived per-tick B98C observation, before hop/Lakitu callbacks; not
     * persistent driver state and not included in replay checksums. */
    PortableRaceMotionObservation source_progress;
    /* Explicit adapter mode for the observed sub_803C4C0 frame order:
     * integrate the prior acceleration first, then compute the next one.
     * This is not a claim that the source struct contains this byte. */
    uint8_t source_update_order;
    uint8_t battle_enabled;
    uint8_t balloons_remaining;
    uint8_t battle_scene_state;
    PortableBattleBalloonEvent balloon_event; /* Per-turn presentation request. */
    /* Opt-in source9DAC/A6F4 material22 owner; old recovery fixtures keep
     * their separate historical profile until this route is qualified. */
    uint8_t source_terrain22_enabled;
    uint8_t source_terrain22_active;
    /* Native ground hazards retain their owner across material changes. */
    uint8_t source_hazard_enabled;
    uint8_t source_hazard_kind;
    uint8_t source_route_index_90;
    uint16_t recovery_target_x_9e, recovery_target_y_a0, recovery_target_angle_a2;
    uint16_t recovery_flags_74, recovery_contact_34;
    uint16_t fall_frames_76; /* C4C0 sinking depth owner */
    /* sub_803C4C0 calls sub_803AF10 only when the source frame clock is
     * divisible by eight; the runtime supplies that clock gate. */
    uint8_t source_route_check_due;
    /* GP sub_803C1BC keeps +0x8c as the class/race base speed. */
    uint8_t preserve_source_base_speed;
    /* The source terrain helper returns material 0x26 at its finite map
     * boundary; an interactive host may carry that state past the renderer
     * raster instead of treating the diagnostic query miss as a fatal tick.
     * The strict default remains disabled for focused core callers. */
    uint8_t allow_track_boundary;
    uint8_t vector_effect_pending;
    /* sub_803B2B4's source +0x85 material byte, retained for GP quality
     * accounting after the same driver update. */
    uint8_t source_material_85;
    uint8_t source_material_valid;
    /* Source +0x9a/+0x19b/+0x19d select the GP acceleration target row.
     * The canonical Peach finish changes +0x9a from zero to three. */
    uint8_t source_speed_variant_19b;
    uint8_t source_speed_offset_19d;
    uint8_t source_steering_selector_9a;
    uint16_t finished_target_angle_ca;
    uint8_t finished_initialized;
    uint8_t finished_route_row_191;
    int8_t vector_effect_mode;
    PortableDriverVectorEffectResult vector_effect;
    PortableDriverVectorTriggerResult vector_trigger;
    PortableDriverAffine affine;
    PortableDriverArtMotion art_motion;
    uint8_t flattened_64; /* FD7C crush; C4C0 releases at contact timer 0x6f80 */
    uint16_t contact_guard_66; /* source actor-contact immunity; GP Boo owns its timer */
    uint16_t invulnerability_timer_6a;
    int16_t lightning_timer_68;
    uint16_t item_lock_ac;
    uint16_t palette_requests_194;
    uint16_t burn_ticks_72; /* 805ACFC lava/rock channel3 script remaining */
    uint8_t unflatten_cue_ticks; /* 805B3DC: 133 sounds 112 ticks after a crush release */
    uint8_t palette_bank;
    PortableDriverContactAudio contact_audio;
    PortableDriverContactAudio boost_voice;
    uint8_t hazard_audio_kind, hazard_audio_variant; /* one-turn39C9E/39F74 event */
    uint32_t wall_mutation_count;
    uint32_t wall_mutation_digest;
    uint32_t wall_mutation_cell;
    uint32_t vector_trigger_count;
    uint32_t vector_effect_count;
    uint16_t vector_effect_previous_cell_key;
    uint8_t vector_effect_previous_cell_key_valid;
    uint16_t vector_effect_source_state;
    uint16_t vector_effect_source_direction;
    uint8_t vector_effect_alternate_state_branch;
    PortableDriverNormalCollisionResult normal_collision;
    /* Source-shaped response fields kept distinct from the motion angle:
     * +0x7a is the response angle, +0x7c the base heading and +0x80 their
     * signed halfword delta. */
    uint16_t normal_collision_response_angle_7a;
    uint16_t normal_collision_base_heading_7c;
    uint16_t normal_collision_response_delta_80;
    int32_t normal_collision_response_output_c0;
    uint8_t normal_collision_response_output_written;
    uint8_t normal_collision_response_output_pending;
    /* The sub_8040570 object contact holds the prior +0xc0 until B550's
     * eight-tick +0x38 motion hold releases. Other response owners keep
     * their existing timing. */
    uint8_t normal_collision_response_next_status;
    uint32_t normal_collision_count;
    uint8_t normal_collision_source_valid;
    uint8_t normal_collision_pending;
    uint32_t normal_collision_schedule_count;
    uint8_t normal_collision_source_use_motion;
    uint8_t normal_collision_source_skip_motion_tick;
    /* Build the response input from the source-shaped native fields below
     * instead of consuming the caller-owned metadata struct. This remains an
     * explicit mode until the host has seeded every prior field at the same
     * source scheduler boundary. */
    uint8_t normal_collision_source_from_native_state;
    /* Apply B550's normal resultant transform before the native handler.
     * Callers enable it only after seeding the source-state inputs. */
    uint8_t normal_collision_source_use_b550_table;
    /* Source-owned B550 countdowns, consumed only by the source-state B550
     * adapter. Do not initialize them from a desired post-handler flag. */
    uint16_t strong_spin_rate_b6; /* signed angular rate for E490 hits */
    uint16_t normal_collision_timer_38;
    uint16_t normal_collision_timer_62;
    int16_t normal_collision_generated_resultant_x;
    int16_t normal_collision_generated_resultant_y;
    PortableDriverNormalCollisionSourceInput normal_collision_source;
} PortableDriverPilotState;

typedef struct
{
    /* Called after horizontal integration, before hop/terrain/retrieval.
     * The recovered runtime applies B98C progress here and supplies the
     * selected checkpoint mode. Return zero to abort an invalid race tick. */
    int (*observe_progress)(void *context, PortableRaceMotionObservation *motion);
    void *progress_context;
    PortableCourseProps *course_props; /* borrowed live common59E80 owner */
    /* Borrowed live Piranha Plants; with course_props, the +0x718 actors
     * that a material 0x2A capture may target. */
    const PortablePiranhaCourse *piranhas;
    const int16_t *auto_acceleration_table;
    size_t auto_acceleration_count;
    const int16_t *brake_table;
    size_t brake_table_count;
    const int16_t *brake_accelerate_table;
    size_t brake_accelerate_table_count;
    const int16_t *forward_response_curve;
    size_t forward_response_count;
    const int16_t *forward_local_table;
    size_t forward_local_count;
    const int16_t *forward_limit_thresholds;
    size_t forward_limit_threshold_count;
    const PortableDriverResponseRules *response_rules;
    const int16_t *steering_delta_table;
    size_t steering_delta_count;
    const int16_t *steering_response_data;
    size_t steering_response_data_count;
    const int32_t *sine_table;
    size_t sine_count;
    const PortableTrackRaster *raster;
    const PortableTrackMaterialView *track_material;
    const PortableDriverAffineRules *affine_rules;
    const PortableImpactEffectRules *impact_effect_rules;
    const PortableDriverArtRules *art_motion_rules;
    const PortableTerrainView *terrain;
    const PortableTerrainEffectTableView *terrain_effect_tables;
    const int16_t *vector_alternate_x_components;
    size_t vector_alternate_x_count;
    const int16_t *vector_alternate_y_components;
    size_t vector_alternate_y_count;
    const int16_t *normal_resultant_x_table;
    size_t normal_resultant_x_count;
    const int16_t *normal_resultant_y_table;
    size_t normal_resultant_y_count;
    const PortableSourceNormalResponseTableView *normal_response_table;
    /* Optional GP target-speed response from source sub_803DB4C. */
    const int16_t *forward_target_response_table;
    size_t forward_target_response_count;
    const PortableRacePoint *source_track_route;
    size_t source_track_route_count;
    const uint16_t *source_track_angle_lut;
    size_t source_track_angle_lut_count;
    const int16_t *source_auto_deceleration_table;
    size_t source_auto_deceleration_count;
    const uint8_t *source_angle_target_table;
    size_t source_angle_target_table_count;
    int32_t ramp_speed_limit_q16; /* content policy for A1BC; zero is uncapped */
    uint8_t *owned_terrain_cells;
    const uint8_t *initial_terrain_cells;
    size_t owned_terrain_capacity;
    uint8_t wall_replacement_tile; /* B110 course material policy */
    uint16_t wall_contact_song; /* B2B4 course audio policy; zero disables */
    const int16_t *spatial_pitch_table;
    size_t spatial_pitch_count;
    const PortableMushroomRules *mushroom_rules;
    const PortableItemStatusRules *item_status_rules;
    const PortableBooRules *boo_rules;
    const PortableGreenShellRules *green_shell_rules;
    const PortableShellOrbitRules *shell_orbit_rules;
    const PortableRedShellRules *red_shell_rules;
    const PortableBananaRules *banana_rules;
    const PortableModernDriftRules *modern_drift_rules; /* NULL preserves stock driving */
    uint16_t mushroom_voice_songs[2]; /* normal/small A9AC voice; zero disables */
} PortableDriverPilotDependencies;

/*
 * Compose one deterministic source-control frame. A-held frames use the accepted
 * forward helper; B-held frames use the source brake tables; frames with no
 * A/B bit use the accepted normal-mode table. Left/right update the source
 * heading angle from the caller-owned steering table.
 */
/* AF10 direction edge after the source route/mode/status gates. */
void portable_driver_pilot_wrong_way(PortableDriverPilotState *state,uint16_t route_angle);

/* Speed-scaled source contact cue; presentation only. */
/* FD7C: rebound unless the Thwomp is above one unit and falling. */
void portable_driver_pilot_thwomp_hit(PortableDriverPilotState *state,
    int32_t height_q16, int32_t velocity_z_q16);

void portable_driver_pilot_request_contact_audio(PortableDriverPilotState *state,
    uint16_t song, uint8_t restart);

typedef struct {
    const PortableRacePoint *alternate_route;
    size_t alternate_route_count;
    const uint8_t *ai_modes;
    size_t ai_mode_count;
    uint16_t coin_count;
    uint8_t ai_mode_row;
} PortableDriverFinishedInputs;

void portable_driver_pilot_retire_selected(PortableDriverPilotState *state);
/* D818 gate for smoke and other effects attached to the driver. */
int portable_driver_effects_visible(const PortableDriverPilotState *state);

/* Options are available before B98C; a progress callback may retire the kart
 * into flag2000 on this same tick, before C4C0 selects its motion branch. */
int portable_driver_pilot_tick_with_finished(PortableDriverPilotState *state,
    uint16_t source_control_word,const PortableDriverPilotDependencies *dependencies,
    const PortableDriverFinishedInputs *inputs);
/* C4C0 autonomous branch: integrate prior components once; BD24 then sets A. */
int portable_driver_pilot_finished_tick(PortableDriverPilotState *state,
    const PortableDriverPilotDependencies *dependencies,
    const PortableDriverFinishedInputs *inputs);

int portable_driver_pilot_tick(
    PortableDriverPilotState *state,
    uint16_t source_control_word,
    const PortableDriverPilotDependencies *dependencies);

/* Carry one caller-measured sub_803AD6C response through the native pilot
 * state. The event remains explicit until its source trigger is captured. */
int portable_driver_pilot_apply_normal_collision(
    PortableDriverPilotState *state,
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t impact_angle,
    uint16_t current_angle,
    uint32_t source_flags,
    uint16_t prior_angle_history_7a,
    uint16_t prior_angle_delta_80,
    int16_t prior_resistance_92,
    int32_t prior_magnitude_state_bc,
    uint8_t prior_timer_value_84,
    uint8_t prior_response_timer_7e);

/* Carry one caller-owned normal response through the source-angle adapter;
 * the event boundary and prior state remain explicit. */
int portable_driver_pilot_apply_normal_collision_source_angle(
    PortableDriverPilotState *state,
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t current_angle,
    uint32_t source_flags,
    uint16_t prior_angle_history_7a,
    uint16_t prior_angle_delta_80,
    int16_t prior_resistance_92,
    int32_t prior_magnitude_state_bc,
    uint8_t prior_timer_value_84,
    uint8_t prior_response_timer_7e);

#endif
