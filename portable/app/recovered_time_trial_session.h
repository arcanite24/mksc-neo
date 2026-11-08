#ifndef PORTABLE_RECOVERED_TIME_TRIAL_SESSION_H
#define PORTABLE_RECOVERED_TIME_TRIAL_SESSION_H

#include <stdint.h>
#include "ambient.h"
#include "lap_hud.h"
#include "driver_engine_sound.h"
#include "thwomp_sound.h"
#include "lava_smoke.h"
#include "recovery_fx.h"
#include "surface_trail.h"
#include "entry_splash.h"
#include "course_impact_sound.h"

#include "race_runtime.h"
#include "recovered_pack.h"
#include "water_recovery.h"
#include "character_camera.h"
#include "finish_camera.h"
#include "snow_weather.h"
#include "time_trial_ghost.h"
#include "gp_start_lakitu.h"
#include "cannonball_sound.h"
#include "boo_attack.h"
#include "race_script_hooks.h"

typedef enum
{
    PORTABLE_RECOVERED_TIME_TRIAL_OK = 0,
    PORTABLE_RECOVERED_TIME_TRIAL_INVALID_ARGUMENT,
    PORTABLE_RECOVERED_TIME_TRIAL_RUNTIME_ERROR,
    PORTABLE_RECOVERED_TIME_TRIAL_ACTION_REJECTED,
} PortableRecoveredTimeTrialStatus;

typedef struct
{
    PortableDriverPilotState driver;
    PortableLapHud lap_hud;
    PortableCourseProps course_props;
    PortableLavaBubbleCourse lava_bubbles;
    PortableThwompCourse thwomps;
    PortableCrabCourse crabs;
    PortableSeagullCourse seagulls;
    PortablePiranhaCourse piranhas;
    PortableSnowmanCourse snowmen;
    PortableMouseCourse mice;
    PortableThundercloudCourse thunderclouds;
    PortableFallingStarCourse falling_stars;
    PortablePenguinCourse penguins;
    PortableBooCourse boos;
    PortablePiranhaSounds piranha_sounds;
    PortableCannonballCourse cannonballs;
    PortableCannonballSounds cannonball_sounds;
    PortableThwompSounds thwomp_sounds;
    PortableCoursePropSounds prop_sounds;
    PortableCourseImpactSounds impact_sounds;
    PortableRaceRuntime runtime;
    uint32_t scene_clock;
    uint32_t marker_clock,marker_display_clock; /* Next and latched source4E7EC submissions. */
    const PortableSkyMotionRules *sky_motion_rules;
    const PortableAmbientRules *ambient_rules;
    const PortableFinishRules *finish_rules;
    const PortableWeatherAssetView *weather_profile;
    const PortableRecoveryFxRules *recovery_fx_rules;
    const PortableSurfaceTrailRules *surface_trail_rules;
    uint8_t icy_recovery;
    PortableSnowWeatherState snow_weather;
    uint32_t main_visual_rng_state;
    PortableAmbientSeed ambient_seed;
    PortableAmbientState ambient;
    PortableSkyMotion sky_motion;
    PortableRaceRuntimeStatus last_runtime_status;
    uint16_t previous_semantic_buttons;
    uint8_t mushrooms_remaining;
    uint8_t previous_item_control;
    uint8_t paused;
    const uint8_t *recovery_bob_offsets;
    const PortableGpFinishLakituRules *finish_lakitu_rules;
    PortableGpRecoveryLakituState hazard_lakitu;
    uint16_t hazard_effect_72;
    PortableCharacterCameraState character_camera;
    PortableFinishCamera finish_camera;
    uint8_t ghost_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE];
    PortableTimeTrialGhostEncoder ghost_encoder;
    uint8_t ghost_recording_valid;
    /* Broken Pier Boo haunts (4886C) on the TT driver; effect2 music latch. */
    PortableBooAttack haunts[PORTABLE_BOO_ATTACK_CAPACITY];
    PortableLavaSmoke lava_smoke;
    PortableRecoveryFx recovery_fx;
    PortableSurfaceTrail surface_trails;
    uint8_t recovery_fx_events;
    PortableEntrySplash entry_splash;
    const PortableBooRules *boo_rules;
    uint16_t previous_source_control;
    uint8_t haunt_music;
    /* A press whose mushroom a mod script cancelled stays swallowed until
     * release, so the ghost records it as never pressed. Zero without mods. */
    uint8_t script_item_swallowed;
    /* Optional mod scripts (item_use sees the mushroom count as triple,
     * double or single mushroom). Not race state; ghosts never carry it. */
    const PortableRaceScriptHooks *script_hooks;
    uint8_t voice_override; /* The race view's: 1 + the voice class, or zero. */
} PortableRecoveredTimeTrialSession;

typedef struct
{
    PortableRacePhase phase;
    uint32_t marker_clock;
    PortableLapHud lap_hud;
    PortableRaceCompatibilityId compatibility;
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t height_q16;
    int32_t speed_q16;
    PortableDriverEngineSoundInput engine_sound;
    uint8_t engine_rev_due, engine_scene_stop, engine_revs_owned;
    uint16_t engine_revs;
    uint8_t finish_audio_ready,finish_audio_record,finish_audio_replay;
    uint16_t heading;
    uint16_t camera_yaw;
    PortableFinishCamera finish_camera;
    uint32_t source_flags;
    uint16_t camera_adjustment;
    uint16_t driver_angle;
    int16_t steering_response_q8;
    uint16_t steering_position_82;
    uint32_t charged_exit_count;
    uint32_t charged_audio_cue_count;
    uint32_t charged_particle_count;
    uint8_t charged_particle_age;
    uint8_t modern_drift_enabled;
    PortableModernDriftState modern_drift;
    uint8_t modern_air_enabled;
    PortableModernAirState modern_air;
    uint8_t mushrooms_remaining;
    uint8_t mushroom_active;
    uint16_t mushroom_timer;
    uint16_t laps_completed;
    uint16_t lap_count;
    uint16_t countdown_remaining;
    uint16_t start_route_index;
    size_t next_route_index;
    size_t route_count;
    uint32_t elapsed_ticks;
    uint32_t total_ticks;
    PortableDriverContactAudio contact_audio;
    PortableDriverContactAudio boost_voice;
    uint8_t boost_particle_age;
    int32_t boost_particle_birth_height_q16;
    uint8_t palette_bank;
    PortableCourseProps course_props;
    PortableLavaBubbleCourse lava_bubbles;
    PortableThwompCourse thwomps;
    PortableCrabCourse crabs;
    PortableSeagullCourse seagulls;
    PortablePiranhaCourse piranhas;
    PortableSnowmanCourse snowmen;
    PortableMouseCourse mice;
    PortableThundercloudCourse thunderclouds;
    PortableFallingStarCourse falling_stars;
    PortablePenguinCourse penguins;
    PortableBooCourse boos;
    PortablePiranhaSounds piranha_sounds;
    PortableCannonballCourse cannonballs;
    PortableCannonballSounds cannonball_sounds;
    PortableThwompSounds thwomp_sounds;
    PortableCoursePropSounds prop_sounds;
    PortableCourseImpactSounds impact_sounds;
    uint8_t flattened;
    PortableDriverAffine affine;
    PortableDriverArtMotion art_motion;
    PortableSkyMotionSnapshot sky_motion;
    PortableAmbientSnapshot ambient;
    uint32_t ambient_digest;
    PortableSnowWeatherSnapshot snow_weather;
    uint64_t snow_weather_digest;
    uint32_t main_visual_rng_state;
    uint16_t lightning_timer;
    uint32_t current_lap_ticks;
    uint32_t lap_ticks[PORTABLE_RACE_MAX_LAPS];
    uint32_t rescue_count;
    PortableWaterRecoveryPhase recovery_phase;
    uint16_t recovery_elapsed_ticks;
    uint16_t recovery_phase_ticks;
    int16_t lakitu_x, lakitu_y;
    uint16_t lakitu_state; /* driver184: C000 warning, E000..E004 recovery */
    uint16_t lakitu_scale_q8;
    uint8_t lakitu_visible, lakitu_art_bank;
    uint8_t fall_mode;
    uint16_t fall_timer;
    uint16_t fall_frames_76;
    uint8_t driver_hidden;
    uint8_t paused;
    uint8_t off_track;
    PortableBooAttack boo_attacks[PORTABLE_BOO_ATTACK_CAPACITY];
    PortableLavaSmoke lava_smoke;
    PortableRecoveryFx recovery_fx;
    PortableSurfaceTrail surface_trails;
    PortableImpactEffects impact_effects;
    uint8_t recovery_fx_events;
    PortableEntrySplash entry_splash;
    uint8_t haunt_music_active;
    uint16_t item_lock;
} PortableRecoveredTimeTrialSnapshot;

uint16_t portable_recovered_time_trial_source_control(
    uint16_t semantic_buttons);

/* Initialization writes the supplied owner in place. After failure, discard
 * or reinitialize it before use. Race views and their borrowed payloads must
 * not overlap the destination and must remain valid for its lifetime. */
PortableRecoveredTimeTrialStatus portable_recovered_time_trial_init(
    PortableRecoveredTimeTrialSession *session,
    const PortableRecoveredRaceView *race);

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_tick(
    PortableRecoveredTimeTrialSession *session,
    uint16_t semantic_buttons,
    PortableRaceTickDisposition *disposition);

/* Deterministic reference/smoke callers already own source-width controls. */
PortableRecoveredTimeTrialStatus portable_recovered_time_trial_tick_source(
    PortableRecoveredTimeTrialSession *session,
    uint16_t source_control_word,
    PortableRaceTickDisposition *disposition);

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_set_paused(
    PortableRecoveredTimeTrialSession *session,
    int paused);

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_restart(
    PortableRecoveredTimeTrialSession *session);

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_rescue(
    PortableRecoveredTimeTrialSession *session);

PortableRecoveredTimeTrialStatus portable_recovered_time_trial_snapshot(
    const PortableRecoveredTimeTrialSession *session,
    PortableRecoveredTimeTrialSnapshot *snapshot);

int portable_recovered_time_trial_ghost_candidate(
    const PortableRecoveredTimeTrialSession *session,
    uint8_t out_record[PORTABLE_TIME_TRIAL_GHOST_RECORD_SIZE]);

#endif
