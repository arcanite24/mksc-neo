#ifndef PORTABLE_GP_RACE_SESSION_H
#define PORTABLE_GP_RACE_SESSION_H

#include "quick_run_options.h"
#include "battle_navigation.h"

#include <stdint.h>
#include "ambient.h"
#include "character_camera.h"
#include "finish_camera.h"
#include "snow_weather.h"
#include "character_view.h"
#include "driver_engine_sound.h"
#include "thwomp_sound.h"
#include "course_impact_sound.h"
#include <stddef.h>

#include "gp_race_roster.h"
#include "gp_result_clock.h"
#include "hud_asset.h"
#include "coin_pickup_effect.h"
#include "lava_smoke.h"
#include "recovery_fx.h"
#include "surface_trail.h"
#include "coin_effect.h"
#include "entry_splash.h"
#include "gp_driving_quality.h"
#include "race_runtime.h"
#include "recovered_pack.h"
#include "water_recovery.h"
#include "gp_start_lakitu.h"
#include "gp_item_art_clock.h"
#include "gp_rank_hud.h"
#include "gp_coin_hud.h"
#include "lap_hud.h"
#include "gp_race_hud_asset.h"
#include "cannonball_sound.h"

#define PORTABLE_GP_OPPONENT_COUNT (PORTABLE_GP_RACER_COUNT - 1u)
/* GP box sub_80439FC -> sub_8044428 -> sub_8044400/sub_804445C:
 * the natural Peach box consumed at source frame 535 regains sub_8043AB4
 * collision at frame 585. */
#define PORTABLE_GP_ITEM_BOX_RESPAWN_TICKS 50u
/* Bounded per-turn output for item/racer requests plus HUD and race cues. */
#define PORTABLE_GP_RACE_AUDIO_EVENT_CAPACITY 240u
#define PORTABLE_GP_LAUNCH_PRELUDE_TICKS 20u

typedef enum
{
    PORTABLE_GP_AUDIO_SONG,
    PORTABLE_GP_AUDIO_MUSIC_REQUEST,
    PORTABLE_GP_AUDIO_MUSIC_RELEASE
} PortableGpRaceAudioOperation;

typedef struct
{
    uint16_t song_id;
    uint16_t volume;
    int16_t pitch;
    uint8_t player;
    PortableGpRaceAudioOperation operation;
    uint8_t music_effect;
    uint8_t unless_busy; /* skip when the player sounds a different song */
} PortableGpRaceAudioEvent;

typedef struct
{
    PortableRecoveredCourseObject placement;
    uint16_t cooldown_ticks;
    uint8_t active;
} PortableGpItemBoxState;

typedef struct
{
    PortableRecoveredCoin placement;
    uint8_t active;
} PortableGpCoinState;

typedef enum
{
    PORTABLE_GP_RACE_SESSION_OK = 0,
    PORTABLE_GP_RACE_SESSION_INVALID_ARGUMENT,
    PORTABLE_GP_RACE_SESSION_UNSUPPORTED_CONTENT,
    PORTABLE_GP_RACE_SESSION_RUNTIME_ERROR,
    PORTABLE_GP_RACE_SESSION_ROSTER_ERROR
} PortableGpRaceSessionStatus;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t height_q16;
    uint16_t heading;
    uint16_t source_angle;
    uint32_t marker_source_flags;
    int16_t steering_response;
    uint8_t contact_mode, contact_phase, contact_frame, contact_flags;
    uint8_t driver_class;
    uint8_t voice_class; /* Engine, hazard and voice tables. */
    uint8_t star_active;
    PortableDriverEngineSoundInput engine_sound;
    int16_t engine_doppler;
    uint32_t fall_audio_serial;
    uint8_t fall_audio_variant;
    uint8_t palette_bank;
    uint8_t flattened;
    PortableDriverAffine affine;
    PortableDriverArtMotion art_motion;
    uint8_t visible;
    uint16_t fall_frames_76;
    uint8_t driver_hidden;
    uint8_t balloons_remaining;
    uint16_t balloon_heading;
    uint8_t revenge_bomb, bomb_walk_phase;
    uint16_t bomb_response;
    int32_t speed_q16;
} PortableGpOpponentSnapshot;

typedef struct
{
    int32_t player_x_q16;
    int32_t player_y_q16;
    int32_t player_height_q16;
    int32_t player_speed_q16;
    PortableDriverEngineSoundInput engine_sound;
    uint8_t engine_rev_due, engine_scene_stop, engine_revs_owned;
    uint16_t engine_revs;
    uint16_t race_audio_mode;
    uint16_t player_heading;
    uint16_t player_visual_yaw; /* Presentation only; excluded from replay codecs/digests. */
    uint8_t player_flattened;
    PortableDriverAffine player_affine;
    PortableDriverArtMotion player_art_motion;
    PortableSkyMotionSnapshot sky_motion;
    PortableAmbientSnapshot ambient;
    uint32_t ambient_digest;
    PortableSnowWeatherSnapshot snow_weather;
    uint64_t snow_weather_digest;
    uint32_t main_visual_rng_state;
    uint16_t item_hud;
    PortableGpRankHud rank_hud;
    PortableGpCoinHud coin_hud;
    PortableLapHud lap_hud;
    uint8_t player_palette_bank;
    uint8_t player_view_angle;
    uint16_t camera_yaw;
    uint16_t finished_target_angle_ca;
    uint8_t finished_initialized, finished_route_row_191;
    PortableFinishCamera finish_camera;
    PortableCharacterViewPose player_art_pose;
    uint8_t player_star_active, player_boo_active;
    uint32_t player_star_uses, lightning_uses;
    uint8_t lightning_music_active, music_finished, music_final_lap;
    uint8_t lightning_scene_active, lightning_scene_white;
    PortableRacePhase phase;
    uint16_t laps_completed;
    uint16_t lap_count;
    uint16_t countdown_remaining;
    uint8_t player_route_index;
    uint8_t player_rank;
    uint32_t player_kart_contacts;
    uint8_t player_balloons_remaining;
    uint32_t balloon_tick;
    uint16_t player_balloon_heading;
    uint8_t player_revenge_bomb, player_bomb_walk_phase;
    uint16_t player_bomb_response;
    uint8_t finish_transition_phase;
    uint8_t results_ready;
    uint32_t total_ticks;
    uint32_t marker_clock, marker_source_flags;
    uint8_t marker_class_flags;
    PortableCourseProps course_props;
    PortableMouseCourse mice;
    PortableThundercloudCourse thunderclouds;
    PortableFallingStarCourse falling_stars;
    PortableDriverContactAudio contact_audio;
    PortableDriverContactAudio boost_voice;
    PortableCoinPickupEffect coin_pickups[PORTABLE_COIN_PICKUP_EFFECT_CAPACITY];
    PortableLavaSmoke lava_smoke;
    PortableRecoveryFx recovery_fx;
    PortableSurfaceTrail surface_trails;
    PortableCoinEffects coin_effects;
    PortableImpactEffects impact_effects;
    uint8_t recovery_fx_events;
    PortableEntrySplash entry_splash;
    uint8_t boost_particle_age;
    uint8_t modern_drift_enabled;
    PortableModernDriftState modern_drift;
    uint8_t modern_air_enabled;
    PortableModernAirState modern_air;
    uint32_t modern_drift_release_serial;
    int32_t boost_particle_birth_height_q16;
    PortableCannonballSounds cannonball_sounds;
    PortableThwompSounds thwomp_sounds;
    PortableCoursePropSounds prop_sounds;
    PortableCourseImpactSounds impact_sounds;
    PortablePiranhaSounds piranha_sounds;
    uint32_t lap_ticks[PORTABLE_RACE_MAX_LAPS];
    uint32_t source_clock_centiseconds;
    uint16_t source_result_times_centiseconds[PORTABLE_RACE_MAX_LAPS + 1u];
    uint8_t source_clock_phase;
    uint8_t source_result_times_ready;
    uint32_t rescue_count;
    PortableWaterRecoveryPhase recovery_phase;
    uint16_t recovery_elapsed_ticks;
    uint16_t recovery_phase_ticks;
    PortableGpRaceAudioEvent audio_events[PORTABLE_GP_RACE_AUDIO_EVENT_CAPACITY];
    uint8_t audio_event_count;
    int16_t lakitu_x, lakitu_y;
    uint16_t lakitu_state; /* driver184: C000 warning, E000..E004 recovery */
    uint16_t lakitu_scale_q8;
    uint8_t lakitu_visible, lakitu_art_bank;
    uint8_t fall_mode;
    uint16_t fall_timer;
    uint16_t fall_frames_76;
    uint8_t driver_hidden;
    uint16_t recovery_effect_72;
    PortableGpOpponentSnapshot opponents[PORTABLE_GP_OPPONENT_COUNT];
} PortableGpRaceSnapshot;

/* Local split-screen players. The session's player fields always hold the
 * local selected by local_index; the others wait here. VS and Battle have
 * no CPUs: racer k is local k, and every local has its own pilot. */
#define PORTABLE_GP_MAX_LOCALS 4u
typedef struct
{
    PortableDriverPilotDependencies player_dependencies;
    PortableDriverPilotState player;
    PortableRaceRuntime runtime;
    PortableSkyMotion sky_motion;
    PortableCharacterCameraState character_camera;
    PortableFinishCamera finish_camera;
    PortableGpDrivingQuality driving_quality;
    PortableWaterRecoveryState water_recovery;
    PortableGpRecoveryLakituState hazard_lakitu;
    uint16_t hazard_effect_72;
    int16_t prior_projected_y, coin_birth_x, coin_birth_y, coin_birth_inverse;
    PortableCoinPickupEffect coin_pickups[PORTABLE_COIN_PICKUP_EFFECT_CAPACITY];
    PortableLavaSmoke lava_smoke;
    PortableRecoveryFx recovery_fx;
    PortableSurfaceTrail surface_trails;
    PortableCoinEffects coin_effects;
    uint8_t recovery_fx_events;
    PortableEntrySplash entry_splash;
    PortableGpSourceClock source_result_clock;
    PortableGpRankHud rank_hud;
    PortableGpCoinHud coin_hud;
    PortableLapHud lap_hud;
    uint16_t launch_revs_92, launch_rev_mode_98;
    uint8_t launch_prelude_applied, player_bomb_walk_audio, recovery_coin_loss;

    uint16_t previous_semantic_buttons;
    uint8_t player_contact_release_pending, player_contact_response_active;
    uint8_t player_contact_clear_status_pending;
    uint8_t voice_rank_ready, voice_previous_rank;
    PortableGpItemHud item_hud;
    uint8_t player_bump_pending;
    int32_t player_bump_speed_q16;
} PortableGpLocalState;

typedef struct PortableGpRaceSession
{
    PortableQuickRunOptions quick_run_options;
    uint8_t fix_doppler_wrap; /* Optional original-bug fix (audio only). */
    PortableRaceRulesView rules;
    const PortableSkyMotionRules *sky_motion_rules;
    const PortableCharacterViewRules *view_rules;
    const PortableFinishRules *finish_rules;
    const PortableWeatherAssetView *weather_profile;
    const PortableRecoveryFxRules *recovery_fx_rules;
    const PortableSurfaceTrailRules *surface_trail_rules;
    uint8_t icy_recovery;
    PortableSnowWeatherState snow_weather;
    uint32_t main_visual_rng_state;
    const PortableAmbientRules *ambient_rules;
    PortableAmbientSeed ambient_seed;
    PortableAmbientState ambient;
    PortableDriverPilotDependencies player_dependencies;
    PortableDriverPilotState player;
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
    PortableCannonballCourse cannonballs;
    PortableCannonballSounds cannonball_sounds;
    PortableThwompSounds thwomp_sounds;
    PortableCoursePropSounds prop_sounds;
    PortableCourseImpactSounds impact_sounds;
    PortablePiranhaSounds piranha_sounds;
    PortableRaceRuntime runtime;
    PortableSkyMotion sky_motion;
    PortableCharacterCameraState character_camera;
    PortableFinishCamera finish_camera;
    PortableGpRaceRoster roster;
    PortableBattleNavWorkspace battle_navigation;
    uint32_t course_contact_events, cpu_course_contact_events;
    PortableGpDrivingQuality driving_quality;
    PortableWaterRecoveryState water_recovery;
    PortableGpRecoveryLakituState hazard_lakitu;
    const uint8_t *recovery_bob_offsets;
    const PortableGpFinishLakituRules *finish_lakitu_rules;
    uint16_t hazard_effect_72;
    int16_t prior_projected_y, coin_birth_x, coin_birth_y, coin_birth_inverse;
    PortableGpItemBoxState item_boxes[PORTABLE_RECOVERED_COURSE_OBJECT_CAPACITY];
    PortableGpCoinState coins[PORTABLE_RECOVERED_COIN_CAPACITY];
    size_t coin_count;
    size_t item_box_count;
    /* Source DAT_03002344: advanced once by the original frame loop before
     * the race callback, then by any item selection on that frame. */
    uint32_t source_rng_state;
    const PortableGpScoringRules *scoring_rules;
    const PortableGpRaceHudAssetView *hud_rules; /* Immutable imported view. */
    const PortableGpPlayerItemRules *player_item_rules;
    const PortableGpCpuItemRules *cpu_item_rules;
    const PortableBattleItemRules *battle_item_rules;
    uint8_t engine_class;
    PortableEngineRevProfile engine_rev_profile;
    uint8_t retry_credits; /* Cup starts with three; Try Again consumes one. */
    PortableCoinPickupEffect coin_pickups[PORTABLE_COIN_PICKUP_EFFECT_CAPACITY];
    PortableLavaSmoke lava_smoke;
    PortableRecoveryFx recovery_fx;
    PortableSurfaceTrail surface_trails;
    PortableCoinEffects coin_effects;
    uint8_t recovery_fx_events;
    PortableEntrySplash entry_splash;
    uint32_t source_frame_clock;
    uint32_t marker_clock, marker_display_clock; /* Next and latched source4E7EC submissions. */
    PortableGpItemArtClock item_art_clock;
    PortableGpSourceClock source_result_clock;
    const uint8_t *hud_clock_steps;
    size_t hud_clock_step_count;
    PortableGpRankHud rank_hud;
    PortableGpCoinHud coin_hud;
    PortableLapHud lap_hud;
    /* DCF0: rival +dc lap splits and +e8 timestamps use the global clock. */
    uint16_t rival_lap_centiseconds[PORTABLE_GP_OPPONENT_COUNT][PORTABLE_RACE_MAX_LAPS];
    uint32_t rival_lap_timestamp[PORTABLE_GP_OPPONENT_COUNT];
    uint8_t source_clock_enable_boundary;
    uint16_t launch_revs_92;
    uint16_t launch_rev_mode_98;
    uint8_t launch_prelude_applied;
    uint8_t player_bomb_walk_audio;
    /* Scene +c88 (fixed-state +cac), reset by48C74 before course setup.
     * The qualified normal GP profile uses row zero. */
    int32_t source_scene_row_c88;
    uint8_t recovery_coin_loss;
    const PortableCoinEffectRules *coin_effect_rules;
    uint8_t source_coin_hud_enabled;
    uint32_t item_box_contacts;
    uint32_t opponent_item_box_contacts;
    uint16_t previous_semantic_buttons;
    uint8_t rival_coin_count_handoff;
    uint8_t failed_roster_stage;
    uint8_t player_contact_release_pending;
    uint8_t player_contact_response_active;
    uint8_t player_contact_clear_status_pending;
    uint8_t voice_rank_ready, voice_previous_rank; /* 804E144 rank latch */
    PortableGpRaceAudioEvent audio_events[PORTABLE_GP_RACE_AUDIO_EVENT_CAPACITY];
    uint8_t audio_event_count;
    /* Mod scripts: the local player's item use and, through the roster, CPU
     * item use. Not part of race state. */
    const PortableRaceScriptHooks *script_hooks;
    uint8_t local_count; /* 0 or 1: single player */
    uint8_t local_index; /* racer index of the local in the player fields */
    /* World-pass item hits waiting for that local's own pass: 1 strong, 2 banana. */
    uint8_t local_hit_pending[PORTABLE_GP_MAX_LOCALS];
    uint8_t vs_retired[PORTABLE_GP_MAX_LOCALS]; /* VS: unfinished at the end */
    uint32_t vs_finish_centiseconds[PORTABLE_GP_MAX_LOCALS];
    PortableGpLocalState locals[PORTABLE_GP_MAX_LOCALS - 1u];
} PortableGpRaceSession;

/* Snapshot slot `slot` of a local's view: every racer except local_index. */
static inline size_t portable_gp_race_session_other_racer(const PortableGpRaceSession *session,
                                                          size_t slot)
{
    return slot < session->local_index ? slot : slot + 1u;
}

/* D4E4 loss amounts: recovery2, strong hit4 and crush8. Mode/finished
 * gates are shared by the recovery actor and course-contact callbacks. */
/* One source coin FX callback pass; rendering only reads the result. */
int portable_gp_race_session_tick_dropped_coins(PortableGpRaceSession *session);
PortableGpRaceSessionStatus portable_gp_race_session_apply_coin_loss(PortableGpRaceSession *session,
                                                                     PortableCoinEffectCause cause);

typedef enum
{
    /* Fresh grid: sub_804F078 enables after the first motion-frame timer
     * update, so that frame's source clock is still zero. */
    PORTABLE_GP_CLOCK_ENABLE_ON_FIRST_RACING_TICK = 0,
    /* Retained ready-grid path: enable after the penultimate countdown tick;
     * the final countdown tick and first motion tick both advance the clock. */
    PORTABLE_GP_CLOCK_ENABLE_BEFORE_FINAL_COUNTDOWN = 1
} PortableGpClockEnableBoundary;

/* Initialization writes the supplied owner in place. After failure, discard
 * or reinitialize it before use. Race views and their borrowed payloads must
 * not overlap the destination and must remain valid for its lifetime. */
PortableGpRaceSessionStatus portable_gp_race_session_init(PortableGpRaceSession *session,
                                                          const PortableRecoveredRaceView *race,
                                                          uint8_t selected_driver_class);
/* The next cup race uses the prior finish order to choose grid rows. Actor
 * index zero is the player; rank_by_racer must be a permutation of 0..7. */
PortableGpRaceSessionStatus portable_gp_race_session_init_grid_order(
    PortableGpRaceSession *session, const PortableRecoveredRaceView *race,
    uint8_t selected_driver_class, const uint8_t rank_by_racer[8]);

/* Select C1BC engine tuning before runtime and CPU initialization. Older
 * packs support only 50cc; 100cc/150cc require imported engine profiles. */
PortableGpRaceSessionStatus portable_gp_race_session_init_engine_grid_order(
    PortableGpRaceSession *session, const PortableRecoveredRaceView *race,
    uint8_t selected_driver_class, const uint8_t rank_by_racer[8], uint8_t engine_class);

/* Optional v3 HUD data; initialize each local after roster/grid setup. */
PortableGpRaceSessionStatus
portable_gp_race_session_configure_hud(PortableGpRaceSession *session,
                                       const PortableGpRaceHudAssetView *rules,
                                       const PortableHudAssetView *hud);

/* Quick Run (mode 5): 48ED4 lap option, 49654 object toggles and BF34 coins. */
PortableGpRaceSessionStatus portable_gp_race_session_init_quick_run(
    PortableGpRaceSession *session, const PortableRecoveredRaceView *race,
    uint8_t selected_driver_class, uint8_t engine_class, PortableQuickRunOptions options);

/* Advance DAT_03002344 for launch frames that the host has already passed
 * before handing control to the race session. This does not advance racers. */
PortableGpRaceSessionStatus
portable_gp_race_session_handoff_launch_rng(PortableGpRaceSession *session,
                                            uint32_t elapsed_frames);

/* Replay the owned input history for the twenty engine updates preceding
 * this fresh-grid host boundary. World/RNG/timer clocks were handed off
 * separately. No observed rev, phase or particle counter is an input. */
PortableGpRaceSessionStatus
portable_gp_race_session_handoff_launch_inputs(PortableGpRaceSession *session,
                                               const uint16_t *semantic_inputs, size_t count);

/* Carry the source frontend's timer-enable event into the GP countdown.
 * This must be set before the first GP session tick. */
PortableGpRaceSessionStatus
portable_gp_race_session_handoff_clock_boundary(PortableGpRaceSession *session,
                                                PortableGpClockEnableBoundary boundary);

PortableGpRaceSessionStatus portable_gp_race_session_tick(PortableGpRaceSession *session,
                                                          uint16_t semantic_buttons,
                                                          PortableRaceTickDisposition *disposition);

/* These two entry points are the session-owned side of the recovered item-box
 * route. Course-object contact supplies the seed and selected source item; the
 * session retains the authoritative player inventory and use lifecycle. */
PortableGpRaceSessionStatus
portable_gp_race_session_begin_player_item_box(PortableGpRaceSession *session,
                                               uint16_t roulette_seed);

PortableGpRaceSessionStatus
portable_gp_race_session_tick_player_item(PortableGpRaceSession *session, uint8_t stop_pressed,
                                          uint16_t selected_item);

/* Put item 0x1000..0x100c straight into the local player's empty slot: no
 * roulette, box or RNG draw. INVALID_ARGUMENT when the slot is busy, the item
 * is unknown or the player has finished. Mod scripts call it between ticks. */
PortableGpRaceSessionStatus
portable_gp_race_session_grant_player_item(PortableGpRaceSession *session, uint16_t item);

PortableGpRaceSessionStatus portable_gp_race_session_snapshot(const PortableGpRaceSession *session,
                                                              PortableGpRaceSnapshot *snapshot);

/* VS (mode 0x400) or Battle for 2..4 locals on one race stack. races[k] is
 * opened for local k's character and shares the course world of races[0].
 * Local k is racer k on grid row k; there are no CPU racers. */
PortableGpRaceSessionStatus
portable_gp_race_session_init_locals(PortableGpRaceSession *session,
                                     const PortableRecoveredRaceView *const races[],
                                     const uint8_t driver_classes[], uint8_t local_count,
                                     uint8_t engine_class, PortableQuickRunOptions options);

/* One fixed tick: buttons[k] drives local k. The world advances once, during
 * local 0's pass; each later local then takes its own driver pass. */
PortableGpRaceSessionStatus
portable_gp_race_session_tick_locals(PortableGpRaceSession *session, const uint16_t buttons[],
                                     PortableRaceTickDisposition *disposition);

/* Move local k into the player fields, e.g. to snapshot its view. Callers
 * must select local 0 again before ticking. */
PortableGpRaceSessionStatus portable_gp_race_session_select_local(PortableGpRaceSession *session,
                                                                  uint8_t local);

/* VS: the race is over (results due) once all but one local finished. */
int portable_gp_race_session_locals_finished(const PortableGpRaceSession *session);

#endif
