#ifndef PORTABLE_GP_RACE_ROSTER_H
#define PORTABLE_GP_RACE_ROSTER_H

#include <stdint.h>
#include "driver_affine.h"
#include "driver_art_motion.h"

#include "race_session.h"
#include "course_prop.h"
#include "piranha.h"
#include "battle_balloon.h"
#include "boo_attack.h"
#include "track_raster.h"
#include "gp_item_inventory.h"
#include "gp_item_hud.h"
#include "gp_green_shell.h"
#include "gp_red_shell.h"
#include "gp_banana.h"
#include "driver_mushroom.h"
#include "recovered_pack.h"
#include "race_script_hooks.h"

#define PORTABLE_GP_RACER_COUNT 8u
#define PORTABLE_GP_ITEM_POOL_CAPACITY 16u
#define PORTABLE_GP_BANANA_ACTOR_SLOTS (PORTABLE_GP_RACER_COUNT+PORTABLE_GP_ITEM_POOL_CAPACITY)
#define PORTABLE_GP_RED_ACTOR_SLOTS (PORTABLE_GP_RACER_COUNT+PORTABLE_GP_ITEM_POOL_CAPACITY)
#define PORTABLE_GP_GREEN_ACTOR_SLOTS (PORTABLE_GP_RACER_COUNT+PORTABLE_GP_ITEM_POOL_CAPACITY)

typedef enum
{
    PORTABLE_GP_ROSTER_OK = 0,
    PORTABLE_GP_ROSTER_INVALID_ARGUMENT,
    PORTABLE_GP_ROSTER_UNSUPPORTED_COURSE,
    PORTABLE_GP_ROSTER_ROUTE_LOOKUP_FAILED,
    PORTABLE_GP_ROSTER_ITEM_INELIGIBLE
} PortableGpRosterStatus;

typedef struct { uint8_t racer, ticks, taunt; } PortableGpPendingVoice;

typedef struct
{
    uint16_t song_id;
    uint8_t player;
} PortableGpLightningAudioEvent;

typedef struct
{
    uint16_t script_tick;
    uint8_t target_index;
    uint8_t frame_index;
    uint8_t occupied;
    uint8_t visible;
} PortableGpLightningEffect;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int16_t contact_center_x;
    int16_t contact_center_y;
    uint32_t source_flags;
    uint16_t grid_slot;
    uint8_t driver_class;
    uint8_t is_player;
    uint8_t route_index;
    uint8_t decision_valid;
    uint16_t control;
    uint16_t route_angle;
    int32_t acceleration_q16;
    int32_t speed_q16;
    int16_t battle_waypoint_x,battle_waypoint_y;
    uint16_t battle_speed_target;
    uint8_t battle_navigation_valid;
    uint8_t lightning_hit_pending; /* a local's pilot adopts it on its own pass */
    uint16_t heading;
    uint16_t source_heading;
    int16_t release_angle_offset;
    int16_t component_x;
    int16_t component_y;
    int16_t progress_ae;
    uint8_t route_wrap_91;
    uint16_t max_progress_c6;
    uint16_t lap_marker_a4;
    uint8_t route_row_191;
    uint8_t item_decision_190;
    uint16_t item_decision_timer_18e;
    uint8_t rank_row_9a;
    uint16_t rank_flags_ba;
    uint8_t acceleration_mode_19b;
    uint8_t speed_target_bias_19d;
    uint16_t coin_count_178;
    uint32_t kart_contact_events; /* physical shared-pair callbacks, excluding view-art reactions */
    /* D4E4 requests retain their cause after the gameplay subtraction. */
    struct { uint8_t cause, loss; uint16_t remaining; uint32_t clock; } coin_loss_events[8];
    uint8_t coin_loss_event_count, coin_loss_event_overflow;
    int16_t contact_axis_1e;
    int16_t contact_axis_20;
    uint16_t contact_height_24;
    /* D00C runs in the player's motion pass before rival motion. Native
     * composition retains the rival heading from that preceding turn. */
    uint16_t contact_heading_56;
    uint8_t contact_frame_6c;
    uint8_t contact_flags_6d;
    uint8_t contact_mode_6e;
    uint8_t contact_phase_6f;
    PortableGpItemInventory item_inventory;
    PortableGpGreenShell green_shell;
    PortableGpRedShell red_shell;
    PortableGpBanana banana;
    uint16_t shell_item_18c;
    uint8_t item_use_state_17c;
    PortableBattleBalloonEvent balloon_event; /* Per-turn presentation request. */
    uint8_t balloons_remaining; /* Source driver+17D, initialized to three. */
    PortableBattleEliminationState elimination;
    int16_t arena_start_x, arena_start_y;
    uint16_t arena_start_heading;
    uint8_t bomb_respawn_pending; /* Player pilot adopts E7F0 before its next turn. */
    uint8_t bomb_walk_audio; /* CC80 one-turn19A footfall bits */
    uint8_t bomb_audio_pending;
    uint16_t bomb_audio_volume; /* E7F0/CD28 at contact, before driver movement. */
    uint16_t mushroom_response_flags_b4;
    uint16_t hit_response_b6;
    uint16_t effect_state_72;
    uint16_t hit_state_flags_74;
    uint16_t hit_cue_flags_58;
    uint16_t contact_status_60;
    int16_t contact_status_66;
    uint16_t hit_ground_flags_96;
    uint16_t item_response_flags_b2;
    uint16_t item_lock_ac; /* 7B30/C4C0 spin-input suppression; star clears it. */
    uint16_t lightning_timer_68;
    int16_t lightning_status_6a;
    uint16_t hit_angle_80;
    int16_t hit_spin_82;
    uint16_t hit_counter_88;
    uint8_t hit_state_7e;
    uint8_t lightning_state_78;
    /* World+8 and signed+52; contact_height_24 remains projected depth. */
    int32_t world_height_q16;
    int16_t vertical_velocity_q8;
    uint8_t source_material_85;
    uint8_t source_surface_selector_86;
    uint8_t source_material_valid;
    uint8_t terrain_jump_landed;
    uint8_t recovery_checkpoint_sample;
    /* Native ownership of9B38 across changing terrain during carry/descent. */
    uint8_t recovery_material;
    uint32_t fall_audio_serial;
    uint8_t fall_audio_variant;
    uint16_t recovery_timer_9c;
    uint16_t recovery_contact_34;
    uint16_t fall_frames_76; /* C4C0 prefix: independent of material/timer9c. */
    int16_t recovery_target_x_9e;
    int16_t recovery_target_y_a0;
    uint16_t recovery_target_heading_a2;
    uint8_t red_shell_hit_audio_pending;
    uint16_t banana_hit_timer_38;
    uint16_t banana_spin_timer_c8;
    uint16_t banana_ground_timer_b8;
    uint16_t banana_reaction_volume;
    int16_t banana_reaction_pitch;
    uint8_t banana_reaction_audio_pending;
    uint8_t banana_special_entry_audio_pending;
    uint16_t banana_special_class_song;
    uint16_t banana_special_class_volume;
    uint8_t banana_special_class_player;
    uint8_t banana_special_class_audio_pending;
    const PortableMushroomRules *mushroom_rules;
    const PortableGreenShellRules *green_shell_rules;
    const PortableShellOrbitRules *shell_orbit_rules;
    const PortableRedShellRules *red_shell_rules;
    const PortableBananaRules *banana_rules;
    uint16_t mushroom_timer_cc;
    uint8_t mushroom_mode_84;
    uint16_t golden_mushroom_ticks; /* Observation of the live 48310 timer, owned by mushroom_pool. */
    uint8_t motion_active;
    uint16_t kart_contact_state_5a;
    PortableDriverAffine affine;
    const PortableDriverAffineRules *affine_rules;
    PortableDriverArtMotion art_motion;
    const PortableDriverArtRules *art_motion_rules;
    uint8_t course_contact_hold,flattened_64;
    uint16_t palette_requests_194;
    uint16_t burn_ticks_72;
    uint8_t palette_bank;
    uint32_t last_update_serial;
    uint8_t voice_override; /* Nonzero: 1 + the class whose voices it uses. */
} PortableGpRacer;

/* Voice tables are keyed by class; roster drivers may borrow another's. */
static inline uint8_t portable_gp_racer_voice_class(const PortableGpRacer *racer)
{
    return (uint8_t)((racer->voice_override!=0u?racer->voice_override-1u:racer->driver_class)&7u);
}

/* Invisible item nodes share the source pool. Mushrooms48138/48310 expire;
 * reserved48134 leaves an inert zero-lifetime node allocated until reuse. */
typedef struct
{
    uint32_t creation_order;
    uint16_t ticks_remaining;
    uint16_t flags_34;
    uint8_t owner, golden, occupied;
} PortableGpMushroomActor;

typedef struct
{
    PortableGpRacer racers[PORTABLE_GP_RACER_COUNT];
    /* Live plants for material 0x2A captures, borrowed only during a tick. */
    const PortablePiranhaCourse *piranhas;
    uint8_t racer_count; /* Zero retains legacy eight-racer callers; arenas use four. */
    /* The owner slot holds the latest shell; older live shells retain native
     * actor identity here. All belong to the same16-actor item chain. */
    struct { PortableGpGreenShell shell; uint8_t owner; } green_pool[PORTABLE_GP_ITEM_POOL_CAPACITY];
    struct { PortableGpRedShell shell; uint8_t owner; } red_pool[PORTABLE_GP_ITEM_POOL_CAPACITY];
    struct { PortableGpBanana banana; uint8_t owner; } banana_pool[PORTABLE_GP_ITEM_POOL_CAPACITY];
    PortableGpMushroomActor mushroom_pool[PORTABLE_GP_ITEM_POOL_CAPACITY];
    uint32_t authoritative_ticks;
    uint32_t update_serial;
    uint32_t course_id;
    const PortableDriverRosterRules *roster_rules;
    uint8_t rank_cursor;
    /* RaceState+C64, selected by42DE0 at cup race setup. */
    uint8_t ai_mode_row_c64;
    uint8_t standings[PORTABLE_GP_RACER_COUNT];
    uint16_t race_mode_flags_7da;
    uint8_t race_phase_7d4;
    uint8_t battle_scene_state; /* Source scene+7F8, separate from finish+7D4. */
    uint16_t finish_countdown;
    uint8_t finish_result_delay_be4;
    uint8_t finish_player_rank;
    uint8_t finish_overlay_variant;
    uint8_t finish_banner_occupied;
    uint8_t finish_banner_visible;
    uint8_t finish_banner_stage;
    uint16_t finish_banner_y;
    uint16_t finish_banner_affine_scale;
    uint16_t finish_banner_hold_counter;
    uint32_t finish_banner_value;
    uint32_t contact_events;
    /* F450 plays 167 for the local kart before E92C; consumed by the session. */
    uint8_t player_bump_pending;
    int32_t player_bump_speed_q16;
    /* Delayed actor-script voices (EC91C/EC8F4/EC934): ouch, then taunt. */
    PortableGpPendingVoice pending_voices[8];
    uint8_t pending_voice_count;
    uint32_t coin_events;
    uint32_t coin_effect_clock; /* derived producer clock for contact requests */
    uint32_t item_box_events;
    uint32_t item_settlements;
    PortableGpItemHud item_hud;
    uint32_t item_uses;
    uint32_t player_reserved_uses;
    uint32_t cpu_reserved_uses;
    uint32_t triple_green_uses;
    uint32_t triple_red_uses;
    uint32_t triple_mushroom_uses;
    uint32_t golden_mushroom_uses;
    uint32_t player_mushroom_uses;
    uint32_t cpu_mushroom_uses;
    uint32_t cpu_star_uses;
    uint32_t player_star_uses;
    uint32_t cpu_banana_uses;
    uint32_t boo_uses;
    uint32_t boo_thefts;
    PortableBooAttack boo_attacks[PORTABLE_BOO_ATTACK_CAPACITY];
    uint32_t cpu_item_clock;
    uint16_t cpu_item_cooldown_c62;
    uint8_t cpu_item_engine;
    uint8_t cpu_items_enabled;
    PortableBattleCombatRules battle_combat;
    uint8_t cpu_banana_release[2][4];
    uint8_t cpu_banana_release_valid;
    uint8_t cpu_green_release[2][4];
    uint8_t cpu_green_release_valid;
    uint32_t cpu_green_uses;
    uint8_t cpu_red_release[2][4];
    uint8_t cpu_red_release_valid;
    uint8_t shell_orbit_delay[2][3]; /* green/red473FC/47560 spawn delays */
    uint8_t shell_orbit_delay_valid;
    uint32_t cpu_red_uses;
    uint32_t cpu_spiny_uses;
    uint32_t green_shell_hits;
    uint32_t red_shell_hits;
    uint32_t banana_hits;
    const PortableHazardSoundRules *hazard_sound_rules;
    const PortableGpRouletteRules *roulette_rules;
    const PortableItemStatusRules *item_status_rules;
    const PortableBooRules *boo_rules;
    uint16_t lightning_duration_c82;
    uint8_t lightning_state_80d;
    uint8_t lightning_audio_pending;
    uint32_t lightning_events;
    PortableGpLightningEffect lightning_effect;
    uint32_t lightning_effect_events;
    uint8_t local_racer; /* the local whose HUD and pilot the session holds */
    /* Mod scripts see CPU item activations; the session keeps it in step
     * with its own hooks. Not part of race state. */
    const PortableRaceScriptHooks *script_hooks;
} PortableGpRaceRoster;

/* Green/red/banana actor indices0..7 select owner slots;8..23 select retained pool
 * actors. The semantic owner is independent of storage and survives copies. */
/* Storage/handling classes remain eight; only participating racers update. */
static inline size_t portable_gp_race_roster_count(const PortableGpRaceRoster *roster)
{
    return roster->racer_count != 0u ? roster->racer_count : PORTABLE_GP_RACER_COUNT;
}

const PortableGpGreenShell *portable_gp_green_actor(const PortableGpRaceRoster *roster,size_t actor);
PortableGpGreenShell *portable_gp_green_actor_mutable(PortableGpRaceRoster *roster,size_t actor);
size_t portable_gp_green_actor_owner(const PortableGpRaceRoster *roster,size_t actor);
unsigned portable_gp_item_pool_count(const PortableGpRaceRoster *roster);
unsigned portable_gp_green_actor_order(const PortableGpRaceRoster *roster, uint8_t actors[PORTABLE_GP_GREEN_ACTOR_SLOTS]);
void portable_gp_item_pool_reserve(PortableGpRaceRoster *roster);
void portable_gp_item_pool_construct_mushroom(PortableGpRaceRoster *roster,
    size_t owner, uint16_t lifetime, uint8_t golden);
void portable_gp_item_pool_retain_green(PortableGpRaceRoster *roster,size_t owner);
const PortableGpRedShell *portable_gp_red_actor(const PortableGpRaceRoster *roster,size_t actor);
PortableGpRedShell *portable_gp_red_actor_mutable(PortableGpRaceRoster *roster,size_t actor);
size_t portable_gp_red_actor_owner(const PortableGpRaceRoster *roster,size_t actor);
unsigned portable_gp_red_actor_order(const PortableGpRaceRoster *roster,uint8_t actors[PORTABLE_GP_RED_ACTOR_SLOTS]);
void portable_gp_item_pool_retain_red(PortableGpRaceRoster *roster,size_t owner);
const PortableGpBanana *portable_gp_banana_actor(const PortableGpRaceRoster *roster,size_t actor);
PortableGpBanana *portable_gp_banana_actor_mutable(PortableGpRaceRoster *roster,size_t actor);
size_t portable_gp_banana_actor_owner(const PortableGpRaceRoster *roster,size_t actor);
unsigned portable_gp_banana_actor_order(const PortableGpRaceRoster *roster,uint8_t actors[PORTABLE_GP_BANANA_ACTOR_SLOTS]);
void portable_gp_item_pool_retain_banana(PortableGpRaceRoster *roster,size_t owner);

PortableGpRosterStatus portable_gp_race_roster_init_for_rules(
    PortableGpRaceRoster *roster, const PortableRaceRulesView *rules,
    uint8_t selected_driver_class);
/* VS/Battle locals: racers 0..count-1 become players of those classes and
 * the roster shrinks to them; there are no CPU racers. */
PortableGpRosterStatus portable_gp_race_roster_set_locals(
    PortableGpRaceRoster *roster, const uint8_t driver_classes[], uint8_t count);
/* 42DE0 assigns AE progress offsets and32B50's global mode row. Points
 * remain in actor creation order; course_position is2C24's linear index. */
PortableGpRosterStatus portable_gp_race_roster_apply_cup_ai_setup(
    PortableGpRaceRoster *roster, const uint8_t points[8],
    uint32_t course_position, uint8_t speed_class);
/* Replace the approximate start-marker rows with packed source grid rows.
 * rank_by_racer is a permutation in actor creation order, with player at 0. */
PortableGpRosterStatus portable_gp_race_roster_apply_course_grid(
    PortableGpRaceRoster *roster, const PortableGpGridRow *grid,
    const uint8_t *rank_by_racer);
PortableGpRosterStatus portable_gp_race_roster_tick_stationary(
    PortableGpRaceRoster *roster);
PortableGpRosterStatus portable_gp_race_roster_decide_initial(
    PortableGpRaceRoster *roster, const PortableRaceRulesView *rules,
    const PortableTrackRaster *raster);
void portable_gp_race_roster_cache_contact_center(PortableGpRacer *racer);

PortableGpRosterStatus portable_gp_race_roster_tick_initial_all(
    PortableGpRaceRoster *roster, const PortableRaceRulesView *rules,
    const PortableTrackRaster *raster, const int32_t *sine,
    size_t sine_count);
/* Shared9B38 ground jumps and A58C landing before8200. Material and ramp
 * cap come from the course pack, independently of kart contact/lap flags. */
PortableGpRosterStatus portable_gp_race_roster_terrain_jump(
    PortableGpRacer *racer,const int32_t *sine,size_t sine_count,
    int32_t ramp_speed_limit_q16);
/* CPU9B38 water/fall/lava/bounds dispatch, including checkpoint carry and
 * descent. Movement by the preceding components belongs to the caller. */
PortableGpRosterStatus portable_gp_race_roster_terrain_tick(
    PortableGpRacer *racer,const PortableRaceRulesView *rules,
    const PortableTrackRaster *raster,const PortableTrackMaterialView *material,
    const int32_t *sine,size_t sine_count,
    int32_t ramp_speed_limit_q16,
    const uint16_t *angle_lut,size_t angle_lut_count);
/* F450/E92C protected hitter against an unprotected CPU. Geometry belongs
 * to the caller. Local victims and mutual protected pushes fail closed. */
/* Schedule a victim "ouch" after `ticks`; taunt adds the local 40D8C taunt
 * 60 ticks later. A full queue drops the voice (presentation only). */
void portable_gp_race_roster_schedule_voice(PortableGpRaceRoster *roster,
    uint8_t racer, uint8_t ticks, uint8_t taunt);
PortableGpRosterStatus portable_gp_race_roster_resolve_protected_kart_contact(
    PortableGpRaceRoster *roster, size_t first, size_t second,
    uint32_t scene_clock, const int32_t *sine, size_t sine_count);
/* CPU pending-spin consumer B550/9B38/BC00/8200/8298. Caller publishes
 * preceding velocity and B98C first. Airborne victims retain queued AE18
 * until the tick after landing; BA98 settles through halfword crossing. */
PortableGpRosterStatus portable_gp_race_roster_cpu_pending_hit_motion(
    PortableGpRacer *racer,const int32_t *sine,size_t sine_count);
/* E490 writes local or CPU strong damage for course actors and kart hits.
 * Contact selection/dispatch,
 * ranked order and current scene clock belong to the caller; no audio/effect
 * allocation is implied. NULL hitter selects the course-actor branch. */
PortableGpRosterStatus portable_gp_race_roster_begin_kart_hit(
    PortableGpRacer *victim,const PortableGpRacer *hitter,uint32_t scene_clock,
    const uint32_t *ranked_flags,size_t ranked_count,
    const int32_t *sine,size_t sine_count);
/* Normal CPU F450/E92C pair, using preceding EE/F0 centers. Guard/timers and
 * both velocity responses commit together; selection belongs to collision.c. */
PortableGpRosterStatus portable_gp_race_roster_begin_normal_kart_contact(
    PortableGpRaceRoster *roster,size_t first_index,size_t second_index,
    const int32_t *sine,size_t sine_count);
/* E92C/EB54 grounded unboosted dual-CPU response. Restarts both E490
 * responses and applies opposite E68C impulses atomically. Native standings
 * supply the rank scan; dispatch, class cue and private effects remain
 * separate mandatory owners. The legacy push API above stays unchanged. */
PortableGpRosterStatus portable_gp_race_roster_begin_unboosted_kart_contact(
    PortableGpRaceRoster *roster,size_t first_index,size_t second_index,
    uint32_t scene_clock,const int32_t *sine,size_t sine_count);
typedef PortableGpRosterStatus (*PortableGpAfterDriver)(void *context,
    PortableGpRaceRoster *roster,size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_tick_initial_all_terrain(
    PortableGpRaceRoster *roster,const PortableRaceRulesView *rules,
    const PortableTrackRaster *raster,const int32_t *sine,size_t sine_count,
    const PortableTerrainView *terrain,const PortableTrackMaterialView *material,
    const PortableRecoveredCoin *coins,size_t coin_count,
    int32_t ramp_speed_limit_q16,
    const uint16_t *angle_lut,size_t angle_lut_count,
    PortableCourseProps *course_props,PortableGpAfterDriver after_driver,
    void *after_driver_context);
/* B98C/B7B8, once after prior-component integration and before9B38.
 * contact_center_y is C4C0's signed pre-motion F0 anchor. */
PortableGpRosterStatus portable_gp_race_roster_track_progress(
    PortableGpRacer *racer,const PortableRaceRulesView *rules,
    const PortableTrackRaster *raster,uint8_t ai_mode_row);
void portable_gp_race_roster_apply_route_wrap(
    PortableGpRacer *racer, const PortableRaceRulesView *rules,
    uint8_t new_wrap);
PortableGpRosterStatus portable_gp_race_roster_resolve_initial_contact(
    PortableGpRaceRoster *roster, size_t route_count);

/* sub_803D00C's player pass using preceding native camera projections.
 * Supply the exact camera yaw so AA6C can check the selected rival's
 * view, as well as the player's. AAF0/ABB8 advance animation separately.
 * Cancellation execution remains outside this bounded adapter. */
/* D00C per ordinary local C4C0 pass. Views are CA78 results from the
 * preceding camera, including selected steering and recipient contact gates. */
PortableGpRosterStatus portable_gp_race_roster_resolve_view_contacts(
    PortableGpRaceRoster *roster,size_t selected,size_t route_count,
    const uint8_t view_angles[PORTABLE_GP_RACER_COUNT]);
PortableGpRosterStatus portable_gp_race_roster_resolve_player_contact(
    PortableGpRaceRoster *roster, size_t route_count, uint16_t camera_yaw);
PortableGpRosterStatus portable_gp_race_roster_tick_contact_animation(
    PortableGpRacer *racer, uint8_t view_angle,const PortableDriverRosterRules *rules);
enum { PORTABLE_GP_KART_CONTACT_NONE=0, PORTABLE_GP_KART_CONTACT_PUSH=1,
    PORTABLE_GP_KART_CONTACT_HIT=2 };
typedef struct {
    uint8_t response[PORTABLE_GP_RACER_COUNT],bump[PORTABLE_GP_RACER_COUNT];
    int32_t bump_speed_q16[PORTABLE_GP_RACER_COUNT];
} PortableGpKartContactReport;
PortableGpRosterStatus portable_gp_race_roster_collide_karts(
    PortableGpRaceRoster *roster,uint16_t checkpoints,uint32_t scene_clock,
    const int32_t *sine,size_t sine_count,PortableGpKartContactReport *report);
PortableGpRosterStatus portable_gp_race_roster_collect_coin(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_begin_item_box(
    PortableGpRaceRoster *roster, size_t racer_index,
    uint16_t item_box_kind, uint16_t roulette_seed);
PortableGpRosterStatus portable_gp_race_roster_tick_item(
    PortableGpRaceRoster *roster, size_t racer_index,
    uint8_t stop_pressed, uint16_t selected_item);

/* 48694 theft and4886C leader attack share the existing race item stack. */
PortableGpRosterStatus portable_gp_race_roster_use_boo(
    PortableGpRaceRoster *roster,size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_tick_boo_attacks(
    PortableGpRaceRoster *roster,const int32_t *sine,size_t count);
/* 59FAC/48C2C: a Broken Pier Boo starts the same 4886C actor on the toucher. */
PortableGpRosterStatus portable_gp_race_roster_haunt(
    PortableGpRaceRoster *roster,size_t racer_index);
/* 7B30 player AC falls by ten on each accelerator press; CPU C4C0 falls by one. */
PortableGpRosterStatus portable_gp_race_roster_tick_boo_timers(
    PortableGpRaceRoster *roster,size_t index,uint8_t accelerate_pressed);
PortableGpRosterStatus portable_gp_race_roster_use_star(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_tick_protection(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_use_lightning(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_take_lightning_audio(
    PortableGpRaceRoster *roster, PortableGpLightningAudioEvent *event);
PortableGpRosterStatus portable_gp_race_roster_tick_lightning_timer(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_tick_lightning_effect(
    PortableGpRaceRoster *roster);
PortableGpRosterStatus portable_gp_race_roster_contact_weight(
    const PortableGpRaceRoster *roster, size_t racer_index,
    uint32_t *weight);
PortableGpRosterStatus portable_gp_race_roster_lightning_affine_scale(
    const PortableGpRaceRoster *roster, size_t racer_index,
    uint16_t normal_scale, uint16_t *affine_scale);
PortableGpRosterStatus portable_gp_race_roster_update_standings(
    PortableGpRaceRoster *roster, uint8_t compare_laps);
PortableGpRosterStatus portable_gp_race_roster_tick_finish_transition(
    PortableGpRaceRoster *roster);
/* C444, called by C4C0 before terrain recovery. scene_state is the source
 * scene+7F8, independently of the race finish phase at+7D4. */
/* E7F0: detonation selects this racer's content-owned directional grid. */
PortableGpRosterStatus portable_battle_race_roster_detonate_bomb(
    PortableGpRaceRoster *roster, size_t racer_index);

/* F450/E92C: bomb first, optional Star immunity, shared strong-hit impulse. */
PortableGpRosterStatus portable_battle_race_roster_bomb_contact(
    PortableGpRaceRoster *roster,size_t first_index,size_t second_index,
    uint32_t scene_clock,const int32_t *sine,size_t sine_count,uint8_t *contacted);

PortableGpRosterStatus portable_battle_race_roster_tick_balloons(
    PortableGpRaceRoster *roster, size_t racer_index, uint8_t scene_state,
    PortableBattleBalloonEvent *event);
PortableGpRosterStatus portable_battle_race_roster_tick_finish_transition(
    PortableGpRaceRoster *roster);
PortableGpRosterStatus portable_battle_race_roster_tick_finish_banner(
    PortableGpRaceRoster *roster,const int32_t *sine,size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_use_green_shell(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_orbit(
    PortableGpRaceRoster *roster, size_t actor,
    const int32_t *sine, size_t sine_count);

PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_attached(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_held_active(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_release_green_shell(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_released(
    PortableGpRaceRoster *roster, size_t racer_index,const PortableGpGreenShellWorld *world);
PortableGpRosterStatus portable_gp_race_roster_resolve_green_shell_kart_hit(
    PortableGpRaceRoster *roster, size_t owner_index, size_t victim_index,
    uint8_t alternate_race_state, uint8_t side_test_nonzero,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_green_shell_destroying(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_use_red_shell(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_attached(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_held_active(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_release_red_shell_backward(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_release_red_shell_forward_leader(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_release_red_shell_forward_target(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_actor(
    PortableGpRaceRoster *roster,size_t index,const PortableGpRedShellWorld *world,
    const PortableGpRedShellRoute *route);
PortableGpRosterStatus portable_gp_race_roster_tick_red_shell_destroying(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_collide_red_shell(
    PortableGpRaceRoster *roster, size_t racer_index,
    uint16_t other_kind, int32_t other_z_q16, int16_t other_field_66);
/* F8C8/F7EC victim callback, independent of the item's445CC/446FC callback. */
PortableGpRosterStatus portable_gp_race_roster_resolve_shell_victim(
    PortableGpRaceRoster *roster,size_t victim_index,
    int32_t shell_x_q16,int32_t shell_y_q16,int32_t shell_z_q16,
    uint16_t shell_state_50,uint8_t speed_byte,
    uint8_t alternate_race_state,uint8_t side_test_nonzero,
    const int32_t *sine,size_t sine_count);

PortableGpRosterStatus portable_gp_race_roster_resolve_red_shell_kart_hit(
    PortableGpRaceRoster *roster, size_t owner_index, size_t victim_index,
    uint8_t alternate_race_state, uint8_t side_test_nonzero,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_take_red_shell_hit_audio(
    PortableGpRaceRoster *roster, size_t victim_index, uint8_t suppressed,
    uint16_t spatial_volume, PortableGpRedShellAudioEvent *event);

PortableGpRosterStatus portable_gp_race_roster_use_banana(
    PortableGpRaceRoster *roster, size_t racer_index,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_tick_banana(
    PortableGpRaceRoster *roster,size_t racer_index,const PortableGpBananaWorld *world,
    const int32_t *sine,size_t sine_count);
/* F8C8/banana victim callback, independent of4791C/479E0 item retirement. */
PortableGpRosterStatus portable_gp_race_roster_resolve_banana_victim(
    PortableGpRaceRoster *roster,size_t victim_index,int32_t item_height,
    uint16_t item_state_50,uint32_t scene_clock,uint16_t spatial_volume);
PortableGpRosterStatus portable_gp_race_roster_resolve_banana_kart_hit(
    PortableGpRaceRoster *roster, size_t owner_index, size_t victim_index);
PortableGpRosterStatus portable_gp_race_roster_resolve_banana_kart_hit_with_context(
    PortableGpRaceRoster *roster, size_t owner_index, size_t victim_index,
    uint32_t scene_clock, uint16_t spatial_volume);
PortableGpRosterStatus portable_gp_race_roster_tick_banana_destroying(
    PortableGpRaceRoster *roster, size_t owner_index);
PortableGpRosterStatus portable_gp_race_roster_tick_banana_hit_response(
    PortableGpRaceRoster *roster, size_t victim_index);
PortableGpRosterStatus portable_gp_race_roster_take_banana_reaction_audio(
    PortableGpRaceRoster *roster, size_t victim_index, uint8_t suppressed,
    PortableGpBananaAudioEvent *event);
PortableGpRosterStatus portable_gp_race_roster_take_banana_special_entry_audio(
    PortableGpRaceRoster *roster, size_t victim_index, uint8_t suppressed,
    PortableGpBananaAudioEvent *event);
PortableGpRosterStatus portable_gp_race_roster_take_banana_special_class_audio(
    PortableGpRaceRoster *roster, size_t victim_index, uint8_t suppressed,
    PortableGpBananaAudioEvent *event);
/*42AE4/42104/42454/42A28/42CEE/42004: CPU banana and timed item use after motion.
 * The roster clock is the scene clock; rank neighbors use current standings. */
PortableGpRosterStatus portable_gp_race_roster_use_cpu_item(
    PortableGpRaceRoster *roster, size_t racer_index,
    size_t route_count, uint16_t lap_count,
    const int32_t *sine, size_t sine_count);
PortableGpRosterStatus portable_gp_race_roster_use_reserved_item(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_use_mushroom(
    PortableGpRaceRoster *roster, size_t racer_index);
PortableGpRosterStatus portable_gp_race_roster_tick_mushroom_actors(
    PortableGpRaceRoster *roster, size_t racer_index);
uint64_t portable_gp_race_roster_identity(
    const PortableGpRaceRoster *roster);
const char *portable_gp_roster_status_name(PortableGpRosterStatus status);

#endif
