#ifndef PORTABLE_RACE_SCENE_RENDERER_H
#define PORTABLE_RACE_SCENE_RENDERER_H

#include <stddef.h>
#include <stdint.h>
#include "gp_rank_hud.h"
#include "gp_coin_hud.h"
#include "course_prop.h"
#include "coin_pickup_effect.h"
#include "lava_smoke.h"
#include "recovery_fx.h"
#include "surface_trail.h"
#include "coin_effect.h"
#include "impact_effect.h"
#include "entry_splash.h"
#include "modern_drift.h"
#include "modern_air.h"
#include "native_image.h"
#include "scene_3d.h"
#include "model_asset.h"

#include "camera_projection.h"
#include "character_asset.h"
#include "character_view.h"
#include "driver_projection_asset.h"
#include "driver_art_motion.h"
#include "sky_motion.h"
#include "ambient.h"
#include "snow_weather.h"
#include "weather_asset.h"
#include "course_scenery_asset.h"
#include "race_particle_asset.h"
#include "hud_asset.h"
#include "item_box_asset.h"
#include "lightning_effect_asset.h"
#include "gp_finish_banner_asset.h"
#include "gp_race_hud_asset.h"
#include "gp_minimap_marker_asset.h"
#include "gp_results_asset.h"
#include "gp_results_renderer.h"
#include "minimap_asset.h"
#include "race_session.h"
#include "road_renderer.h"
#include "sky_asset.h"
#include "water_recovery.h"
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
#include "boo_attack.h"
#include "cannonball.h"

#define PORTABLE_RACE_SCENE_MAX_OPPONENTS 7u
#define PORTABLE_RACE_SCENE_MAX_ITEM_BOXES 64u

typedef struct {
    int32_t x_q16,y_q16,z_q16;
    uint8_t visible,animation_index;
    uint16_t kind;
} PortableRaceItemPresentation;

typedef struct
{
    PortableRoadMapView world_map;
    PortableTrackTilesetView tileset;
    const PortableSkyAssetView *sky;
    const PortableCharacterAssetView *character;
    /* Optional local Time Trial ghost. The simulation owns its state; the
       renderer only reads this independently resolved source-driver asset. */
    const PortableCharacterAssetView *ghost_character;
    const PortableDriverProjectionRules *driver_projection;
    const PortableCharacterAssetView *opponent_characters[
        PORTABLE_RACE_SCENE_MAX_OPPONENTS];
    size_t opponent_count;
    const PortableRaceParticleAssetView *charged_exit_particle;
    const PortableRaceParticleAssetView *mushroom_burst;
    const PortableRaceParticleAssetView *coin_pickup;
    const PortableRaceParticleAssetView *lava_smoke;
    const PortableRaceParticleAssetView *entry_splash;
    const PortableRaceParticleAssetView *recovery_thaw,*recovery_drip;
    const PortableRecoveryFxRules *recovery_fx_rules;
    const PortableRaceParticleAssetView *surface_trail_art;
    const PortableSurfaceTrailRules *surface_trail_rules;
    const PortableCoinEffectRules *coin_effect_rules;
    const PortableRaceParticleAssetView *coin_effect_art;
    const PortableRaceParticleAssetView *impact_effect_art;
    const PortableNativeImage *battle_balloon;
    const PortableCharacterAssetView *battle_bomb;
    const PortableLightningEffectAssetView *lightning_effect;
    const PortableGpFinishBannerAssetView *gp_finish_banner;
    const PortableMinimapAssetView *minimap;
    const PortableHudAssetView *hud;
    const PortableGpRaceHudAssetView *gp_hud;
    const PortableGpMinimapMarkerAssetView *gp_markers;
    const PortableGpResultsAssetView *gp_rank_art;
    const PortableItemBoxAssetView *item_box;
    const PortableCourseSceneryView *scenery;
    const PortableCourseSceneryArt *lava_bubble_art;
    const PortableCourseSceneryArt *thwomp_art;
    const PortableCourseSceneryArt *crab_art;
    const PortableCourseSceneryArt *seagull_art;
    const PortableCourseSceneryArt *piranha_art;
    const PortableCourseSceneryArt *snowman_art;
    const PortableCourseSceneryArt *mouse_art;
    const PortableCourseSceneryArt *thundercloud_art;
    const PortableCourseSceneryArt *falling_star_art;
    const PortableCourseSceneryArt *penguin_art;
    const PortableCourseSceneryArt *boo_art;
    const PortableCourseSceneryArt *cannonball_art;
    const PortableWeatherAssetView *weather_profile;
    const int32_t *sine;
    size_t sine_count;
    /* Roster drivers' own 16x16 race HUD portraits by racer index; NULL
     * keeps the class's stock portrait. */
    const PortableNativeImage *gp_racer_portraits[8];
    const uint16_t *object_palette;
    size_t object_palette_count;
    const uint8_t *object_palette_dirty;
    /* Optional presentation workspace, owned by the race host. */
    PortableScene3D *scene_3d;
    uint32_t course_id,character_id,ghost_character_id;
    uint32_t opponent_character_ids[PORTABLE_RACE_SCENE_MAX_OPPONENTS];
    const PortableModelCatalog *models;
    uint8_t centered_hud; /* Split panes retain the authored native HUD rectangle. */
} PortableRaceSceneView;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t height_q16;
    uint8_t character_view_angle;
    PortableCharacterViewPose art_pose;
    uint8_t art_pose_valid;
    uint16_t character_affine_scale;
    uint8_t palette_bank;
    uint8_t flattened;
    PortableDriverArtMotion art_motion;
    uint16_t driver_affine_x,driver_affine_y; /* inverse Q8; zero keeps legacy packs */
    uint8_t visible;
    uint8_t balloons_remaining;
    uint32_t marker_source_flags;
    uint8_t marker_class_flags;
    uint16_t heading;
    uint16_t visual_yaw; /* Full source angle corrected for visual steering. */
    uint8_t revenge_bomb,bomb_walk_phase;
    uint16_t bomb_response;
    int32_t bomb_speed_q16;
} PortableRaceOpponentPresentation;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t height_q16;
    uint8_t active;
} PortableRaceItemBoxPresentation;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t height_q16;
    PortableCourseProps course_props;
    uint32_t marker_clock,marker_source_flags,ghost_marker_source_flags;
    uint8_t marker_class_flags,ghost_marker_class_flags;
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
    PortableBooAttack boo_attacks[PORTABLE_BOO_ATTACK_CAPACITY];
    PortableCannonballCourse cannonballs;
    uint8_t flattened;
    PortableDriverArtMotion art_motion;
    uint16_t driver_affine_x,driver_affine_y; /* inverse Q8; zero keeps legacy packs */
    uint8_t palette_bank;
    uint16_t heading;
    uint16_t camera_yaw;
    int32_t camera_position_x_q16;
    int32_t camera_position_y_q16;
    uint8_t camera_position_override;
    uint8_t source_sky_parallax;
    PortableSkyMotionSnapshot sky_motion;
    PortableAmbientSnapshot ambient;
    PortableSnowWeatherSnapshot snow_weather;
    const PortableAmbientRules *ambient_rules;
    uint16_t opening_frame;
    uint8_t suppress_player;
    uint8_t result_clock_mode;
    uint8_t result_board_visible;
    uint16_t result_lap_centiseconds;
    uint32_t source_clock_centiseconds;
    uint8_t source_clock_override;
    uint8_t suppress_scenery;
    uint8_t suppress_native_minimap;
    uint8_t character_view_angle;
    PortableCharacterViewPose art_pose;
    uint8_t art_pose_valid;
    uint16_t character_affine_scale;
    int32_t ghost_position_x_q16;
    int32_t ghost_position_y_q16;
    int32_t ghost_height_q16;
    uint8_t ghost_character_view_angle;
    PortableCharacterViewPose ghost_art_pose;
    uint8_t ghost_art_pose_valid;
    uint16_t ghost_affine_x,ghost_affine_y,ghost_affine_scale;
    uint8_t ghost_flattened,ghost_palette_bank;
    PortableDriverArtMotion ghost_art_motion;
    uint8_t ghost_visible;
    uint16_t visual_yaw,ghost_visual_yaw;
    PortableRaceOpponentPresentation opponents[
        PORTABLE_RACE_SCENE_MAX_OPPONENTS];
    size_t opponent_count;
    PortableRaceItemBoxPresentation item_boxes[
        PORTABLE_RACE_SCENE_MAX_ITEM_BOXES];
    size_t item_box_count;
    uint8_t lightning_scene_active,lightning_scene_white;
    uint8_t charged_particle_age;
    uint8_t modern_drift_enabled;
    PortableModernDriftState modern_drift;
    uint8_t modern_air_enabled;
    PortableModernAirState modern_air;
    PortableCoinPickupEffect coin_pickups[PORTABLE_COIN_PICKUP_EFFECT_CAPACITY];
    PortableLavaSmoke lava_smoke;
    PortableRecoveryFx recovery_fx;
    PortableSurfaceTrail surface_trails;
    PortableCoinEffects coin_effects;
    PortableImpactEffects impact_effects;
    PortableEntrySplash entry_splash;
    uint8_t boost_particle_age;
    int32_t boost_particle_birth_height_q16;
    PortableRacePhase phase;
    uint16_t laps_completed;
    uint16_t lap_count;
    uint16_t countdown_remaining;
    size_t next_route_index;
    uint32_t total_ticks;
    uint8_t paused;
    uint8_t pause_open_ticks;
    PortableWaterRecoveryPhase recovery_phase;
    uint16_t recovery_phase_ticks;
    uint8_t show_time_trial_items;
    uint8_t mushrooms_remaining;
    uint8_t show_gp_item;
    uint8_t show_battle,balloons_remaining;
    uint32_t balloon_tick;
    uint16_t balloon_heading;
    uint8_t revenge_bomb,bomb_walk_phase;
    uint16_t bomb_response;
    int32_t bomb_speed_q16;
    uint16_t gp_item;
    uint8_t gp_rank;
    PortableGpRankHud gp_rank_hud;
    PortableGpCoinHud gp_coin_hud;
    uint8_t gp_coins;
    uint8_t gp_top_four_classes[4];
    uint8_t gp_top_four_racers[4];
    PortableRaceItemPresentation green_shells[24];
    PortableRaceItemPresentation red_shells[24];
    PortableRaceItemPresentation bananas[24];
    uint8_t lightning_effect_frame;
    uint8_t lightning_effect_visible;
    uint8_t gp_finish_banner_visible;
    uint8_t gp_finish_banner_variant;
    uint16_t gp_finish_banner_y;
    uint16_t gp_finish_banner_affine_scale;
    uint8_t suppress_native_hud;
    uint8_t suppress_racers;
} PortableRacePresentationState;

int portable_race_scene_player_projection(
    const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,
    int16_t *out_x,int16_t *out_y,
    uint16_t *out_scale);

/* C278 follows an already resolved owner body; puff art itself is unscaled. */
int portable_race_scene_render_lava_smoke(const PortableRaceParticleAssetView *art,
    const PortableLavaSmoke *smoke,const PortableCameraProjectionResult *body,
    PortableSoftwareFrame *frame);

int portable_race_scene_render_recovery_fx(const PortableRaceParticleAssetView *thaw,
    const PortableRaceParticleAssetView *drip,const PortableRecoveryFxRules *rules,
    const PortableRecoveryFx *fx,const PortableCameraProjectionResult *body,PortableSoftwareFrame *frame);

int portable_race_scene_render_impact_effects(const PortableRaceParticleAssetView art[6],
    const PortableImpactEffects *effects,PortableSoftwareFrame *frame);
int portable_race_scene_render_coin_effects(const PortableRaceParticleAssetView art[3],
    const PortableCoinEffects *effects,PortableSoftwareFrame *frame);
int portable_race_scene_render_surface_trails(const PortableRaceParticleAssetView art[5],
    const PortableSurfaceTrailRules *rules,const PortableSurfaceTrail *trails,
    const PortableCameraProjectionResult *body,PortableSoftwareFrame *frame);

/* Draw only the same ordered racer queue over an already composed backdrop. */
int portable_race_scene_render_racers(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,PortableSoftwareFrame *frame);

int portable_race_scene_render(
    const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,
    PortableSoftwareFrame *frame);
/* Reapply the race BG0 HUD after a later OBJ whose source priority is lower. */
int portable_race_scene_capture_lap_board_hud(const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,PortableLapBoardHud *hud);

int portable_race_scene_render_hud(
    const PortableRaceSceneView *scene,
    const PortableRacePresentationState *state,
    PortableSoftwareFrame *frame);

#endif
