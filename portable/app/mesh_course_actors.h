#ifndef PORTABLE_MESH_COURSE_ACTORS_H
#define PORTABLE_MESH_COURSE_ACTORS_H
#include "crab.h"
#include "snowman.h"
#include "penguin.h"
#include "mouse.h"
#include "piranha.h"
#include "seagull.h"
#include "lava_bubble.h"
#include "thwomp.h"
#include "cannonball.h"
#include "boo.h"
#include "boo_attack.h"
#include "falling_star.h"
#include "thundercloud.h"
#include "mesh_course_coins.h"
#include "mesh_sky.h"
#include "course_prop.h"
#include "mesh_course_strong_hit.h"
#include "mesh_course_crush.h"
#include "mesh_course_capture.h"
#include "mesh_items.h"

typedef struct PortableMeshCourseActorsConfig
{
    uint32_t course_id, count, profile_id, revision;
    int32_t scale_q16, sine[1024];
    uint8_t ground_follow_version; /* META opt-in; zero preserves legacy poses. */
    PortableMeshCourseCoinsConfig coins;
    PortableMeshSkyConfig sky;
    PortableMeshSkyBindings sky_bindings;
    PortableCrab initial[64];
    int32_t anchors_q16[64][3];
    uint32_t object_indices[64], frame_ids[64][4];
    uint32_t snowman_count;
    PortableSnowman initial_snowmen[64];
    int32_t snowman_anchors_q16[64][3];
    uint32_t snowman_object_indices[64], snowman_frame_ids[64][2];
    uint32_t penguin_count;
    PortablePenguin initial_penguins[64];
    int32_t penguin_anchors_q16[64][3];
    uint32_t penguin_object_indices[64], penguin_frame_ids[64][6];
    uint32_t mouse_count;
    PortableMouse initial_mice[64];
    int32_t mouse_anchors_q16[64][3];
    uint32_t mouse_object_indices[64], mouse_frame_ids[64][2];
    uint32_t piranha_count;
    PortablePiranha initial_piranhas[64];
    int32_t piranha_anchors_q16[64][3];
    uint32_t piranha_object_indices[64], piranha_frame_ids[64][5];
    uint32_t capture_surface_count, capture_surfaces[256];
    int32_t capture_forward_speed_q16, capture_up_speed_q16;
    uint32_t seagull_count;
    PortableSeagull initial_seagulls[64];
    int32_t seagull_anchors_q16[64][3];
    uint32_t seagull_object_indices[64], seagull_frame_ids[64][3];
    uint32_t lava_count;
    PortableLavaBubble initial_lava[64];
    int32_t lava_anchors_q16[64][3];
    uint32_t lava_object_indices[64], lava_frame_ids[64][2];
    PortableMeshCrushRules crush_rules;
    uint32_t thwomp_count;
    PortableThwomp initial_thwomps[64];
    int32_t thwomp_anchors_q16[64][3];
    uint32_t thwomp_object_indices[64], thwomp_frame_ids[64][1];
    uint32_t cannonball_count;
    PortableCannonball initial_cannonballs[64];
    uint8_t cannonball_variants[64];
    int32_t cannonball_anchors_q16[64][3];
    uint32_t cannonball_object_indices[64], cannonball_frame_ids[64][7];
    uint32_t boo_count;
    PortableBoo initial_boos[64];
    int32_t boo_anchors_q16[64][3];
    uint32_t boo_object_indices[64], boo_frame_ids[64][2];
    uint32_t rock_count;
    PortableCourseProp initial_rocks[64];
    int32_t rock_anchors_q16[64][3];
    uint32_t rock_object_indices[64], rock_frame_ids[64][5];
    uint32_t teepee_count;
    PortableCourseProp initial_teepees[64];
    int32_t teepee_anchors_q16[64][3];
    uint32_t teepee_object_indices[64], teepee_frame_ids[64][11];
    uint32_t falling_star_count;
    PortableFallingStar initial_falling_stars[64];
    int32_t falling_star_anchors_q16[64][3];
    uint32_t falling_star_object_indices[64], falling_star_frame_ids[64][5];
    uint32_t thundercloud_count;
    PortableThundercloud initial_thunderclouds[64];
    int32_t thundercloud_anchors_q16[64][3];
    uint32_t thundercloud_object_indices[64], thundercloud_frame_ids[64][72];
    uint32_t common_prop_count;
    PortableCourseProp initial_common_props[64];
    int32_t common_prop_anchors_q16[64][3];
    uint32_t common_prop_object_indices[64], common_prop_frame_ids[64][2];
    uint32_t mechakoopa_count;
    PortableCourseProp initial_mechakoopas[64];
    int32_t mechakoopa_anchors_q16[64][3];
    uint32_t mechakoopa_object_indices[64], mechakoopa_frame_ids[64][4];
    uint32_t kamek_count;
    PortableCourseProp initial_kameks[64];
    int32_t kamek_anchors_q16[64][3];
    uint32_t kamek_object_indices[64], kamek_frame_ids[64][2];
    uint32_t pier_boo_count;
    PortableCourseProp initial_pier_boos[64];
    int32_t pier_boo_anchors_q16[64][3];
    uint32_t pier_boo_object_indices[64], pier_boo_frame_ids[64][3];
    PortableBooRules pier_boo_rules;
    uint32_t actor_visual_seed;
} PortableMeshCourseActorsConfig;
typedef struct PortableMeshCourseActors
{
    uint32_t count, tick;
    uint8_t ground_follow_version;
    /* SI floor deltas relative to each initial floor, not art-anchor height.
     * Accepted airborne source states retain the last grounded delta. */
    int32_t crab_ground_offset_q16[64], penguin_ground_offset_q16[64];
    int32_t mouse_ground_offset_q16[64], mechakoopa_ground_offset_q16[64];
    PortableMeshCourseCoins coins;
    PortableMeshSky sky;
    PortableCrab crabs[64];
    uint32_t snowman_count;
    PortableSnowman snowmen[64];
    uint8_t actor_grace[8];
    uint32_t penguin_count;
    PortablePenguin penguins[64];
    uint32_t mouse_count;
    PortableMouse mice[64];
    uint32_t piranha_count;
    PortablePiranha piranhas[64];
    uint8_t piranha_victim[64];
    PortableMeshCaptureState capture[8];
    uint32_t seagull_count;
    PortableSeagull seagulls[64];
    uint32_t lava_count;
    PortableLavaBubble lava[64];
    uint16_t lava_burn_ticks[8];
    uint32_t thwomp_count;
    PortableThwomp thwomps[64];
    PortableMeshCrushState crush[8];
    uint32_t cannonball_count;
    PortableCannonball cannonballs[64];
    PortableMeshStrongHitState strong_hit[8];
    uint32_t boo_count;
    PortableBoo boos[64];
    uint8_t boo_pending_mask, boo_flash_mask;
    uint32_t rock_count;
    PortableCourseProp rocks[64];
    uint32_t teepee_count;
    PortableCourseProp teepees[64];
    int32_t teepee_plane_q16[64];
    int32_t teepee_rider_origin_q16[64][2];
    uint32_t falling_star_count;
    PortableFallingStar falling_stars[64];
    uint32_t thundercloud_count;
    PortableThundercloud thunderclouds[64];
    uint8_t thundercloud_palette_phase[2];
    uint32_t common_prop_count;
    PortableCourseProp common_props[64];
    uint32_t mechakoopa_count;
    PortableCourseProp mechakoopas[64];
    uint32_t kamek_count;
    PortableCourseProp kameks[64];
    uint32_t pier_boo_count;
    PortableCourseProp pier_boos[64];
    uint32_t pier_boo_visual_rng;
    PortableBooAttack source_haunts[16];
    int32_t source_haunt_origins_q16[16][3];
    uint8_t source_haunt_source_indices[16], source_haunt_lock_mask;
} PortableMeshCourseActors;
typedef struct PortableMeshCourseActorEvents
{
    uint8_t hit_mask, protected_mask, bump_mask;
    PortableMeshCourseCoinEvents coins;
    uint64_t hit_actors, protected_actors, snowman_contacts, snowman_changed;
    uint64_t penguin_contacts, penguin_defeated, mouse_contacts, mouse_defeated;
    uint64_t piranha_contacts, piranha_changed, piranha_protected;
    uint8_t piranha_bite_mask, piranha_touch_mask, capture_mask, launch_mask;
    uint64_t seagull_contacts, seagull_changed, seagull_defeated;
    uint8_t seagull_touch_mask;
    uint64_t lava_contacts, lava_defeated;
    uint8_t lava_burn_mask;
    uint64_t thwomp_contacts, thwomp_defeated, thwomp_landed;
    uint8_t thwomp_bump_mask, thwomp_crush_mask, thwomp_recover_mask;
    uint64_t cannonball_contacts, cannonball_launched, cannonball_exploded;
    uint8_t cannonball_hit_mask, cannonball_protected_mask;
    uint64_t boo_contacts;
    uint8_t boo_touch_mask;
    uint64_t rock_contacts, rock_cue_masks[4];
    uint8_t rock_hit_mask, rock_protected_mask;
    uint64_t pier_boo_contacts;
    uint8_t pier_boo_touch_mask, pier_boo_protected_mask;
    uint8_t source_haunt_assigned_mask, source_haunt_clock_mask;
    uint8_t source_haunt_music_start_mask, source_haunt_music_release_mask;
    uint16_t source_haunt_lock_clock[8];
    /* Caller-observed newly accepted MIR HAUNT clears source provenance. */
    uint8_t observed_item_haunt_mask;
    uint64_t kamek_contacts;
    uint8_t kamek_touch_mask, kamek_protected_mask;
    uint64_t mechakoopa_contacts;
    uint8_t mechakoopa_hit_mask, mechakoopa_protected_mask, mechakoopa_strong_mask;
    uint64_t common_prop_contacts;
    uint8_t common_prop_hit_mask, common_prop_protected_mask;
    uint8_t common_prop_umbrella_open_mask, common_prop_umbrella_close_mask;
    uint64_t thundercloud_contacts;
    uint8_t thundercloud_hit_mask, thundercloud_protected_mask;
    uint64_t falling_star_contacts;
    uint8_t falling_star_hit_mask, falling_star_protected_mask;
    uint64_t teepee_contacts, teepee_attached, teepee_flung;
    uint8_t teepee_touch_mask, teepee_protected_mask, teepee_release_mask, teepee_assigned_mask;
    uint8_t teepee_clock_mask;
    uint16_t teepee_lock_clock[8];
    /* Wrapper supplies actual admitted movement shape; zero means identity. */
    int32_t movement_body_scale_q16[8];
} PortableMeshCourseActorEvents;
typedef struct
{
    uint32_t object_index, frame_count;
    const uint32_t *frame_ids; /* Borrowed from config. */
} PortableMeshCourseActorBinding;
typedef struct
{
    uint32_t object_index, model_id;
    int32_t position_q16[3];
    uint8_t visible, flip_y;
    uint8_t collidable; /* Karts can touch it this turn (simulation, not drawing). */
    /* Source walking direction or fixed heading, independent of render camera.
     * Hopping mice and nondirectional families leave has_facing zero. */
    int32_t facing_x_q16, facing_y_q16;
    uint8_t has_facing, turn_flip, facing_half_turn; /* Source unsigned boundary. */
    uint16_t affine_x_q8, affine_y_q8;
    uint16_t rotation_angle; /* Clockwise source art angle; zero for other families. */
    uint32_t alternate_model_id;
    uint16_t view_heading;
    uint8_t has_view_art;
} PortableMeshCourseActorView;
/* Flat indices are crabs first, then snowmen, then penguins, then mice, then piranhas, then seagulls, then lava bubbles, then Thwomps, then cannonballs, then Boos, then Lakeside rocks, then teepees, then falling stars, then thunderclouds, then common props, then Mechakoopas, then Kameks, then Pier Boos; invalid counts return zero. */
unsigned portable_mesh_course_actors_config_count(const PortableMeshCourseActorsConfig *);
/* Interactive actors and coins need MIR; skies alone do not. */
int portable_mesh_course_actors_needs_items(const PortableMeshCourseActorsConfig *);
/* Semantic ownership includes optional coins/skies even with no render actors. */
int portable_mesh_course_actors_required(const PortableMeshCourseActorsConfig *);
unsigned portable_mesh_course_actors_count(const PortableMeshCourseActors *);
int portable_mesh_course_actors_binding(const PortableMeshCourseActorsConfig *, unsigned,
                                        PortableMeshCourseActorBinding *);
int portable_mesh_course_actors_view(const PortableMeshCourseActorsConfig *,
                                     const PortableMeshCourseActors *, unsigned,
                                     PortableMeshCourseActorView *);
/* Live pool slot artwork is target-heading-selected by unchanged source core. */
int portable_mesh_course_actors_haunt_view(const PortableMeshCourseActorsConfig *,
                                           const PortableMeshCourseActors *, unsigned slot,
                                           PortableMeshCourseActorView *);
/* Separate rider view remains visible after tent scraps disappear. */
int portable_mesh_course_teepee_rider_view(const PortableMeshCourseActorsConfig *,
                                           const PortableMeshCourseActors *, unsigned teepee_index,
                                           uint16_t source_camera_heading,
                                           PortableMeshCourseActorView *);
/* Before MIR prepare/decrement: source +AC edge policy only on teepee courses.
 * Stages shared locks and emits adjusted pre-decrement clocks for coin parity. */
int portable_mesh_course_actors_prepare_drive(const PortableMeshCourseActorsConfig *,
                                              PortableMeshCourseActors *, PortableMeshItems *,
                                              const PortableMeshKartInput *,
                                              const PortableMeshRacerState *, size_t,
                                              PortableMeshCourseActorEvents *, char *, size_t);
/* All failures preserve outputs. Config owns decoded source data; no pack borrow. */
int portable_mesh_course_actors_config_open(const PortableCourse3DView *,
                                            PortableMeshCourseActorsConfig *, char *, size_t);
/* Opt-in geometry stage uses a nearest initial floor within 0..1m of the
 * authored anchor (2 Q16 LSB encoding reserve), then connected floor tracing.
 * NULL geometry is accepted only for legacy configurations. */
int portable_mesh_course_actors_init_on_course(const PortableMeshCourseActorsConfig *,
                                               const PortableMeshCourse *,
                                               PortableMeshCourseActors *, char *, size_t);
int portable_mesh_course_actors_advance_events_on_course(const PortableMeshCourseActorsConfig *,
    const PortableMeshCourse *, PortableMeshCourseActors *, const PortableMeshRacerState *, size_t,
    PortableMeshCourseActorEvents *, char *, size_t);
int portable_mesh_course_actors_init(const PortableMeshCourseActorsConfig *,
                                     PortableMeshCourseActors *, char *, size_t);
int portable_mesh_course_actors_advance(const PortableMeshCourseActorsConfig *,
                                        PortableMeshCourseActors *, char *, size_t);
/* Emits delayed bite cues for supplied human victim states. NULL racers/count0
 * advances identical actor motion without publishing delayed cues. */
int portable_mesh_course_actors_advance_events(const PortableMeshCourseActorsConfig *,
                                               PortableMeshCourseActors *,
                                               const PortableMeshRacerState *, size_t,
                                               PortableMeshCourseActorEvents *, char *, size_t);
int portable_mesh_course_actors_pose(const PortableMeshCourseActorsConfig *,
                                     const PortableMeshCourseActors *, unsigned,
                                     int32_t out_q16[3]);
uint64_t portable_mesh_course_actors_checksum(const PortableMeshCourseActors *);
/* Call after item resolution, before gates. Stable descending placement order,
 * ascending racer index. Stages world/status/karts/events as one transaction. */
int portable_mesh_course_actors_resolve(const PortableMeshCourseActorsConfig *,
                                        PortableMeshCourseActors *, PortableMeshItems *,
                                        const PortableMeshItemsConfig *, PortableMeshRacerState *,
                                        size_t, PortableMeshCourseActorEvents *, char *, size_t);
#endif
