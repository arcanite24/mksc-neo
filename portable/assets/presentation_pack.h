#ifndef PORTABLE_PRESENTATION_PACK_H
#define PORTABLE_PRESENTATION_PACK_H

#include "asset_pack.h"
#include "character_asset.h"
#include "driver_projection_asset.h"
#include "race_particle_asset.h"
#include "course_scenery_asset.h"
#include "hud_asset.h"
#include "item_box_asset.h"
#include "lightning_effect_asset.h"
#include "gp_finish_banner_asset.h"
#include "gp_minimap_marker_asset.h"
#include "gp_race_hud_asset.h"
#include "gp_opening_asset.h"
#include "gp_results_asset.h"
#include "gp_standings_asset.h"
#include "minimap_asset.h"
#include "recovered_pack.h"
#include "road_map_asset.h"
#include "sky_asset.h"
#include "track_geometry.h"
#include "track_tileset_asset.h"

typedef struct
{
    uint8_t geometry_data[5u + PORTABLE_RECOVERED_ROUTE_CAPACITY * 8u];
    uint8_t road_tiles[PORTABLE_TRACK_TILESET_TILE_BYTES];
    uint16_t road_palette[64];
    PortableSkyMotionRules sky_motion_rules;
    uint8_t sky_tiles[0x2800];
    uint16_t sky_palette[64];
    uint8_t sky_front[0x1000];
    uint8_t sky_mid[0x1000];
    uint8_t sky_back[0x1000];
    PortableCharacterPackedStorage character_storage;
    PortableDriverProjectionRules driver_projection;
    uint16_t battle_bomb_palette[256];
    PortableRaceParticleStorage charged_exit;
    PortableRaceParticleStorage mushroom_burst;
    PortableRaceParticleStorage coin_pickup;
    PortableRaceParticleStorage lava_smoke;
    PortableRaceParticleStorage entry_splash;
    PortableRaceParticleStorage recovery_thaw,recovery_drip;
    PortableRaceParticleStorage surface_trail_art[5];
    PortableRaceParticleStorage coin_effect_art[3];
    PortableRaceParticleStorage impact_effect_art[6];
    uint8_t item_box_tiles[PORTABLE_ITEM_BOX_TILE_BYTES];
    uint16_t item_box_palette[PORTABLE_ITEM_BOX_PALETTE_COUNT];
    PortableCourseSceneryArt lava_bubble_art[2];
    PortableCourseSceneryArt thwomp_art[1];
    PortableCourseSceneryArt crab_art[4];
    PortableCourseSceneryArt seagull_art[3];
    PortableCourseSceneryArt piranha_art[5];
    PortableCourseSceneryArt snowman_art[2];
    PortableCourseSceneryArt mouse_art[2];
    PortableCourseSceneryArt thundercloud_art[PORTABLE_THUNDERCLOUD_ART_COUNT];
    PortableCourseSceneryArt falling_star_art[5];
    PortableCourseSceneryArt penguin_art[6];
    PortableCourseSceneryArt boo_art[2];
    PortableCourseSceneryArt cannonball_art[7];
    PortableCourseSceneryArt scenery_art[PORTABLE_COURSE_SCENERY_MAX_ART];
    uint8_t scenery_tiles[PORTABLE_COURSE_SCENERY_ALL_TILE_BYTES];
    uint16_t scenery_palette[PORTABLE_COURSE_SCENERY_PALETTE_COUNT];
    uint8_t gp_minimap_marker_tiles[PORTABLE_GP_MINIMAP_MARKER_TILE_BYTES];
    uint16_t gp_minimap_marker_palette[PORTABLE_GP_MINIMAP_MARKER_PALETTE_COUNT];
    PortableCourseSceneryObject scenery_objects[
        PORTABLE_COURSE_SCENERY_MAX_OBJECTS];
    uint8_t lightning_effect_tiles[PORTABLE_LIGHTNING_EFFECT_TILE_BYTES];
    uint8_t gp_finish_banner_tiles[PORTABLE_GP_FINISH_BANNER_TILE_BYTES];
    uint16_t gp_finish_banner_palette[
        PORTABLE_GP_FINISH_BANNER_PALETTE_COUNT];
    uint8_t gp_finish_banner_cells[PORTABLE_GP_FINISH_BANNER_CELL_BYTES];
    uint8_t gp_results_tiles[PORTABLE_GP_RESULTS_TILE_BYTES];
    uint16_t gp_results_palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    uint16_t gp_results_digits[PORTABLE_GP_RESULTS_DIGIT_COUNT];
    uint8_t gp_results_headers[PORTABLE_GP_RESULTS_HEADER_COUNT *
                               PORTABLE_GP_RESULTS_HEADER_BYTES];
    uint8_t gp_results_rows[PORTABLE_GP_RESULTS_ROW_COUNT *
                            PORTABLE_GP_RESULTS_ROW_BYTES];
    uint16_t gp_results_row_bytes[PORTABLE_GP_RESULTS_ROW_COUNT];
    uint16_t gp_results_background_map[PORTABLE_GP_RESULTS_MAP_COUNT];
    uint8_t gp_results_followup_tiles[PORTABLE_GP_RESULTS_TILE_BYTES];
    uint16_t gp_results_followup_palette[PORTABLE_GP_RESULTS_PALETTE_COUNT];
    uint16_t gp_results_followup_map[PORTABLE_GP_RESULTS_FOLLOWUP_MAP_COUNT];
    uint16_t gp_results_scroll_map[PORTABLE_GP_RESULTS_SCROLL_MAP_COUNT];
    uint16_t gp_results_record_patterns[
        PORTABLE_GP_RESULTS_RECORD_PATTERN_COUNT *
        PORTABLE_GP_RESULTS_RECORD_PATTERN_CELLS];
    uint8_t gp_results_object_tiles[PORTABLE_GP_RESULTS_OBJECT_TILE_BYTES];
    uint16_t gp_results_object_palette[
        PORTABLE_GP_RESULTS_OBJECT_PALETTE_COUNT];
    uint16_t gp_results_menu_palette[PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT];
    uint16_t gp_results_menu_pulse_base_palette[
        PORTABLE_GP_RESULTS_MENU_PALETTE_COUNT];
    uint16_t gp_results_menu_pulse_factors[
        PORTABLE_GP_RESULTS_MENU_PULSE_FACTOR_COUNT];
    uint8_t gp_results_rank_tiles[PORTABLE_GP_RESULTS_RANK_COUNT *
                                  PORTABLE_GP_RESULTS_RANK_TILE_BYTES];
    uint16_t gp_results_rank_palette[PORTABLE_GP_RESULTS_RANK_PALETTE_COUNT];
    uint16_t gp_results_compact_row_bytes[PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT];
    uint8_t gp_results_compact_rows[PORTABLE_GP_RESULTS_COMPACT_ROW_COUNT *
                                    PORTABLE_GP_RESULTS_ROW_BYTES];
    uint16_t gp_results_time_board_map[PORTABLE_GP_RESULTS_MAP_COUNT];
    uint8_t gp_results_record_identities[8],gp_results_menu_option_codes[5];
    uint8_t gp_standings_tiles[PORTABLE_GP_STANDINGS_TILE_BYTES];
    uint16_t gp_standings_palette[PORTABLE_GP_STANDINGS_PALETTE_COUNT];
    uint16_t gp_standings_background_map[PORTABLE_GP_STANDINGS_MAP_COUNT];
    uint16_t gp_standings_driver_cells[
        PORTABLE_GP_STANDINGS_DRIVER_COUNT *
        PORTABLE_GP_STANDINGS_DRIVER_CELLS];
    uint16_t gp_standings_footer_cells[PORTABLE_GP_STANDINGS_FOOTER_CELLS];
    uint16_t minimap_cells[PORTABLE_MINIMAP_CELL_COUNT];
    uint8_t minimap_tiles[PORTABLE_MINIMAP_TILE_BYTES];
    uint8_t hud_tiles[PORTABLE_HUD_CHAR_BYTES];
    uint16_t hud_palette[PORTABLE_HUD_PALETTE_COUNT];
    uint16_t hud_digits[PORTABLE_HUD_DIGIT_COUNT];
    uint8_t hud_clock[PORTABLE_HUD_CLOCK_STEP_COUNT];
    uint16_t hud_left[PORTABLE_HUD_LEFT_CELL_COUNT];
    uint16_t hud_slash[PORTABLE_HUD_SLASH_CELL_COUNT];
    uint16_t hud_right[PORTABLE_HUD_RIGHT_CELL_COUNT];
    uint8_t hud_item_tiles[PORTABLE_HUD_ITEM_TILE_BYTES];
    uint16_t hud_item_cells[PORTABLE_HUD_ITEM_CELL_COUNT];
    uint16_t hud_item_palette_selectors[PORTABLE_HUD_ITEM_PALETTE_COUNT];
    uint8_t mirrored_gp_course_tiles[PORTABLE_GP_RACE_HUD_COURSE_BYTES];
} PortablePresentationPackStorage;

typedef struct
{
    PortableTrackGeometryView geometry;
    PortableRoadMapView world_map;
    PortableTrackTilesetView tileset;
    PortableSkyAssetView sky;
    const PortableAmbientRules *ambient_rules;
    const PortableCharacterViewRules *view_rules;
    const PortableFinishRules *finish_rules;
    const PortableWeatherAssetView *weather_profile;
    const PortableRecoveryFxRules *recovery_fx_rules;
    const PortableSurfaceTrailRules *surface_trail_rules;
    const PortableCoinEffectRules *coin_effect_rules;
    const PortableImpactEffectRules *impact_effect_rules;
    const PortableGpFinishLakituRules *finish_lakitu_rules;
    const PortableTimeTrialRecordDefaults *time_trial_record_defaults;
    const PortableAmbientSeed *ambient_seed;
    PortableCharacterAssetView character;
    const PortableDriverProjectionRules *driver_projection;
    PortableRaceParticleAssetView charged_exit_particle;
    PortableRaceParticleAssetView mushroom_burst;
    PortableRaceParticleAssetView coin_pickup;
    PortableRaceParticleAssetView lava_smoke;
    PortableRaceParticleAssetView entry_splash;
    PortableRaceParticleAssetView recovery_thaw,recovery_drip;
    PortableRaceParticleAssetView surface_trail_art[5];
    PortableRaceParticleAssetView coin_effect_art[3];
    PortableRaceParticleAssetView impact_effect_art[6];
    PortableNativeImage battle_balloon;
    PortableCharacterAssetView battle_bomb;
    PortableItemBoxAssetView item_box;
    PortableCourseSceneryView scenery;
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
    PortableGpMinimapMarkerAssetView gp_minimap_markers;
    PortableGpRaceHudAssetView gp_race_hud;
    PortableGpOpeningAssetView gp_opening;
    PortableLightningEffectAssetView lightning_effect;
    PortableGpFinishBannerAssetView gp_finish_banner;
    PortableGpResultsAssetView gp_results;
    PortableGpStandingsAssetView gp_standings;
    PortableMinimapAssetView minimap;
    PortableHudAssetView hud;
} PortablePresentationPackView;

PortableRecoveredPackStatus portable_presentation_pack_open(
    const PortableAssetPackView *pack,
    const PortableContentSelection *selection,
    const PortableRecoveredPackStorage *race_storage,
    const PortableRecoveredRaceView *race,
    PortablePresentationPackStorage *storage,
    PortablePresentationPackView *out);

#endif
