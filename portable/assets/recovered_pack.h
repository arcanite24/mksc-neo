#ifndef PORTABLE_RECOVERED_PACK_H
#define PORTABLE_RECOVERED_PACK_H

#include "asset_pack.h"
#include "gp_scoring.h"
#include "../core/item_roulette.h"
#include "content_registry.h"
#include "driver_pilot.h"
#include "driver_affine.h"
#include "driver_art_motion.h"
#include "modern_drift_asset.h"
#include "sky_asset.h"
#include "ambient_asset.h"
#include "character_view_asset.h"
#include "finish_asset.h"
#include "weather_asset.h"
#include "recovery_fx_asset.h"
#include "surface_trail_asset.h"
#include "coin_effect_asset.h"
#include "impact_effect_asset.h"
#include "finish_lakitu.h"
#include "item_art_intro_rules.h"
#include "time_trial_record_defaults.h"
#include "driver_engine_sound.h"
#include "race_session.h"
#include "lava_bubble.h"
#include "thwomp.h"
#include "crab.h"
#include "seagull.h"
#include "piranha.h"
#include "snowman.h"
#include "mouse.h"
#include "thundercloud.h"
#include "falling_star.h"
#include "penguin.h"
#include "boo.h"
#include "cannonball.h"

/* Stock fallback is three credits; malformed scoring records fail. */
int portable_recovered_gp_scoring(const PortableAssetPackView *pack,PortableGpScoringRules *out);
/* A 3D cup's own scoring: the GP scoring section whose content ID is the
 * cup ID (an imported game's points). 0 when absent or malformed. */
int portable_recovered_cup_scoring(const PortableAssetPackView *pack,uint32_t cup_id,
                                   PortableGpScoringRules *out);
/* Imported MAT1 voice rows are global donor data. Equal resolved course tables
 * are accepted; absent, malformed or conflicting tables preserve out and fail.
 * Columns are normal/small. No 2D course or recovered storage is constructed. */
int portable_recovered_mushroom_voices(const PortableAssetPackView *pack, uint16_t out[8][2]);

#define PORTABLE_RECOVERED_ROUTE_CAPACITY 256u
#define PORTABLE_RECOVERED_RASTER_CAPACITY (128u * 128u)
#define PORTABLE_RECOVERED_TERRAIN_CAPACITY (256u * 256u)
#define PORTABLE_RECOVERED_COURSE_OBJECT_CAPACITY 64u
#define PORTABLE_RECOVERED_COIN_CAPACITY 64u

/* 43DE8 selection is normalized per course and engine at import. */
typedef struct
{
    uint16_t item[3][8][8]; /* lap variant, handling class, rank */
    uint8_t banana_release[2][4]; /* human ahead/behind, checkpoint gap */
    uint8_t banana_release_valid;
    uint8_t green_release[2][4];
    uint8_t green_release_valid;
    uint8_t red_release[2][4];
    uint8_t red_release_valid;
    uint8_t shell_orbit_delay[2][3]; /*473FC green /47560 red, source byte narrowing */
    uint8_t shell_orbit_delay_valid;
} PortableGpCpuItemRules;

/* C1BC selects one engine row; BB48 can address all sixteen heading rows. */
typedef struct
{
    PortableGpCpuTuning cpu;
    uint8_t angle_targets[0x1a0];
    int32_t player_base_speed_q16;
    int16_t player_forward_limits[16];
} PortableGpEngineProfile;

typedef struct
{
    int32_t x_q16;
    int32_t y_q16;
    uint32_t heading;
} PortableGpGridRow;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    uint16_t kind;
    uint8_t source_parameter;
    uint8_t collision_half_extent;
} PortableRecoveredCourseObject;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    uint8_t source_parameter;
} PortableRecoveredCoin;

typedef enum
{
    PORTABLE_RECOVERED_PACK_OK = 0,
    PORTABLE_RECOVERED_PACK_INVALID_ARGUMENT,
    PORTABLE_RECOVERED_PACK_MISSING_SECTION,
    PORTABLE_RECOVERED_PACK_BAD_PAYLOAD,
    PORTABLE_RECOVERED_PACK_UNSUPPORTED_CONTENT
} PortableRecoveredPackStatus;

typedef struct
{
    PortableRacePoint route[PORTABLE_RECOVERED_ROUTE_CAPACITY];
    PortableRacePoint gp_route_rows[2u * PORTABLE_RECOVERED_ROUTE_CAPACITY];
    uint8_t gp_target_table[40u];
    PortableRacePoint gp_sentinel_targets[3];
    /* 32B50 uses a global cup-points row, independently of steering's191. */
    uint8_t route_ai_modes[3u * PORTABLE_RECOVERED_ROUTE_CAPACITY];
    uint8_t raster_cells[PORTABLE_RECOVERED_RASTER_CAPACITY];
    uint8_t terrain_cells[PORTABLE_RECOVERED_TERRAIN_CAPACITY];
    const uint8_t *initial_terrain_cells;
    uint8_t mirrored_initial_terrain[PORTABLE_RECOVERED_TERRAIN_CAPACITY];
    uint8_t material_flags[256];
    uint16_t protected_hop_thresholds[6];
    int16_t spatial_pitch_table[33*33];
    uint16_t mushroom_voice_songs[8][2];
    PortableDriverAffineRow affine_rows[5][32];
    PortableDriverAffineRules affine_rules;
    PortableDriverArtRules art_motion_rules;
    PortableModernDriftRules modern_drift_rules;
    PortableCharacterViewRules view_rules;
    PortableFinishAssetStorage finish_storage;
    PortableWeatherAssetView weather_storage;
    PortableRecoveryFxRules recovery_fx_storage;
    PortableSurfaceTrailRules trail_storage;
    PortableCoinEffectRules coin_effect_storage;
    PortableImpactEffectRules impact_effect_storage;
    PortableGpFinishLakituRules finish_lakitu_storage;
    PortableItemArtIntroRules item_art_intro_storage;
    PortableTimeTrialRecordDefaults time_trial_record_defaults;
    PortableSkyMotionRules sky_motion_rules;
    PortableAmbientAssetStorage ambient_storage;
    PortableGpScoringRules gp_scoring;
    PortableHazardSoundRules hazard_sound_rules;
    uint8_t hazard_sound_rules_valid;
    PortableEngineSoundRules engine_sound_rules;
    uint8_t engine_sound_rules_valid;
    PortableMushroomRules mushroom_rules;
    PortableItemStatusRules item_status_rules;
    PortableBooRules boo_rules;
    PortableGreenShellRules green_shell_rules;
    PortableShellOrbitRules shell_orbit_rules;
    PortableRedShellRules red_shell_rules;
    PortableBananaRules banana_rules;
    PortableGpRouletteRules roulette_rules;
    uint8_t roulette_rules_valid;
    PortableGpPlayerItemRules gp_player_items;
    uint8_t gp_player_items_valid;
    PortableBattleItemRules battle_items;
    uint8_t battle_items_valid;
    PortableGpCpuItemRules gp_cpu_items[3];
    size_t gp_cpu_item_engine_count;
    int32_t sine[1024];
    int16_t auto_acceleration[64];
    int16_t source_auto_deceleration[8];
    size_t source_auto_deceleration_count;
    int16_t brake[8];
    int16_t brake_accelerate[8];
    int16_t forward_response[8];
    int16_t local[2][8][16];
    int32_t time_trial_base_speed_q16;
    int16_t time_trial_forward_limits[16];
    int16_t forward_target_response[8];
    PortableDriverResponseRules driver_response_rules;
    PortableDriverRosterRules driver_roster_rules;
    uint8_t driver_response_rules_valid;
    int16_t steering_delta[10];
    int16_t steering_response[16];
    size_t steering_response_count;
    uint16_t track_angle_lut[0x4000];
    size_t track_angle_lut_count;
    PortableGpEngineProfile gp_engines[3];
    size_t gp_engine_count;
    PortableGpCpuTuning cpu_tuning;
    uint8_t angle_target_table[0x1a0];
    size_t angle_target_table_count;
    int32_t terrain_x[8];
    int32_t terrain_y[8];
    int16_t vector_x[4];
    int16_t vector_y[4];
    int16_t resultant_x[4];
    int16_t resultant_y[4];
    int32_t normal_response[8];
    PortableRecoveredCourseObject
        course_objects[PORTABLE_RECOVERED_COURSE_OBJECT_CAPACITY];
    PortableCourseProp course_props[PORTABLE_COURSE_PROP_CAPACITY];
    PortableLavaBubble lava_bubbles[PORTABLE_LAVA_BUBBLE_CAPACITY];
    PortableThwomp thwomps[PORTABLE_THWOMP_CAPACITY];
    PortableCrab crabs[PORTABLE_CRAB_CAPACITY];
    PortableSeagull seagulls[PORTABLE_SEAGULL_CAPACITY];
    PortablePiranha piranhas[PORTABLE_PIRANHA_CAPACITY];
    PortableSnowman snowmen[PORTABLE_SNOWMAN_CAPACITY];
    PortableMouse mice[PORTABLE_MOUSE_CAPACITY];
    PortableThundercloud thunderclouds[PORTABLE_THUNDERCLOUD_CAPACITY];
    PortableFallingStar falling_stars[PORTABLE_FALLING_STAR_CAPACITY];
    PortablePenguin penguins[PORTABLE_PENGUIN_CAPACITY];
    PortableBoo boos[PORTABLE_BOO_CAPACITY];
    PortableCannonball cannonballs[PORTABLE_CANNONBALL_CAPACITY];
    PortableRecoveredCoin coins[PORTABLE_RECOVERED_COIN_CAPACITY];
    PortableGpGridRow gp_grid[8];
    PortableTrackRaster raster;
    PortableTerrainView terrain;
    PortableTrackMaterialView material;
    PortableTerrainEffectTableView terrain_effect;
    PortableSourceNormalResponseTableView normal_response_view;
} PortableRecoveredPackStorage;

typedef struct
{
    const PortableCourseProp *course_props;
    size_t course_prop_count;
    const PortableLavaBubble *lava_bubbles;
    size_t lava_bubble_count;
    const PortableThwomp *thwomps;
    size_t thwomp_count;
    const PortableCrab *crabs;
    const PortableSeagull *seagulls;
    const PortablePiranha *piranhas;
    const PortableSnowman *snowmen;
    const PortableMouse *mice;
    const PortableThundercloud *thunderclouds;
    const PortableFallingStar *falling_stars;
    const PortablePenguin *penguins;
    const PortableBoo *boos;
    const PortableCannonball *cannonballs;
    size_t crab_count;
    size_t seagull_count;
    size_t piranha_count;
    size_t snowman_count;
    size_t mouse_count;
    size_t thundercloud_count;
    size_t falling_star_count;
    size_t penguin_count;
    size_t boo_count;
    size_t cannonball_count;
    const uint8_t *recovery_bob_offsets;
    const PortableDriverAffineRules *affine_rules;
    const PortableDriverArtRules *art_motion_rules;
    const PortableModernDriftRules *modern_drift_rules;
    const PortableCharacterViewRules *view_rules;
    const PortableFinishRules *finish_rules;
    const PortableWeatherAssetView *weather_profile;
    const PortableRecoveryFxRules *recovery_fx_rules;
    const PortableSurfaceTrailRules *surface_trail_rules;
    const PortableCoinEffectRules *coin_effect_rules;
    const PortableImpactEffectRules *impact_effect_rules;
    const PortableGpFinishLakituRules *finish_lakitu_rules;
    const PortableItemArtIntroRules *item_art_intro_rules;
    const PortableTimeTrialRecordDefaults *time_trial_record_defaults;
    uint8_t icy_recovery;
    const PortableSkyMotionRules *sky_motion_rules;
    const PortableAmbientRules *ambient_rules;
    const PortableAmbientSeed *ambient_seed;
    PortableRaceRulesView rules;
    PortableDriverPilotDependencies dependencies;
    const PortableGpScoringRules *gp_scoring;
    const PortableHazardSoundRules *hazard_sound_rules;
    const PortableEngineSoundRules *engine_sound_rules;
    const PortableGpRouletteRules *roulette_rules;
    const PortableGpPlayerItemRules *gp_player_items;
    const PortableBattleItemRules *battle_items;
    const PortableGpCpuItemRules *gp_cpu_items;
    size_t gp_cpu_item_engine_count;
    const PortableGpEngineProfile *gp_engines;
    size_t gp_engine_count;
    const int16_t *gp_forward_local_table;
    size_t gp_forward_local_count;
    PortableDriverPilotState initial_driver;
    const PortableRecoveredCourseObject *course_objects;
    size_t course_object_count;
    const PortableRecoveredCoin *coins;
    size_t coin_count;
    const PortableGpGridRow *gp_grid;
    size_t gp_grid_count;
    /* Nonzero: 1 + the class whose voices this driver uses (a roster
     * driver's voice driver); zero: its own class's. */
    uint8_t voice_override;
} PortableRecoveredRaceView;

PortableRecoveredPackStatus portable_recovered_pack_open_race(
    const PortableAssetPackView *pack,
    const PortableContentSelection *selection,
    PortableRecoveredPackStorage *storage,
    PortableRecoveredRaceView *out);

/* Imported donor-class rival ordering; selected manifest ID remains racer0.
 * Failure leaves ids unchanged. No 2D course or simulation owner is opened. */
int portable_recovered_pack_rival_driver_ids(const PortableAssetPackView *pack,
                                            uint32_t selected_id, uint32_t ids[8]);

const char *portable_recovered_pack_status_name(
    PortableRecoveredPackStatus status);

#endif
