#ifndef PORTABLE_GP_RED_SHELL_H
#define PORTABLE_GP_RED_SHELL_H

#include <stddef.h>
#include <stdint.h>

#include "gp_item_inventory.h"
#include "red_shell_rules.h"
#include "shell_orbit_rules.h"
#include "driver_terrain_effect.h"
#include "race_session.h"

enum
{
    PORTABLE_GP_RED_SHELL_ITEM = 0x1002,
    PORTABLE_GP_TRIPLE_RED_SHELL_ITEM = 0x1003,
    PORTABLE_GP_RED_SHELL_KIND = 0x8402,
    PORTABLE_GP_SPINY_SHELL_ITEM = 0x1004,
    PORTABLE_GP_SPINY_SHELL_KIND = 0x8403,
    PORTABLE_GP_RED_SHELL_ATTACHED = 1,
    PORTABLE_GP_RED_SHELL_ACTIVE = 2,
    PORTABLE_GP_RED_SHELL_RELEASED_BACKWARD = 3,
    PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_NO_TARGET = 4,
    PORTABLE_GP_RED_SHELL_RELEASED_FORWARD_TARGET = 5,
    PORTABLE_GP_RED_SHELL_DESTROYING_ROUTE = 6,
    PORTABLE_GP_RED_SHELL_DESTROYED = 7,
    PORTABLE_GP_RED_SHELL_TRAP_ARMED = 8,
    PORTABLE_GP_RED_SHELL_TRAP_LOCKING = 9,
    PORTABLE_GP_RED_SHELL_TRAP_CHASING = 10,
    PORTABLE_GP_RED_SHELL_TERRAIN_DESTRUCTION_PENDING = 11,
    PORTABLE_GP_RED_SHELL_ORBIT_PENDING = 12,
    PORTABLE_GP_RED_SHELL_ORBITING = 13
};

typedef enum
{
    PORTABLE_GP_RED_SHELL_OK = 0,
    PORTABLE_GP_RED_SHELL_INVALID_ARGUMENT,
    PORTABLE_GP_RED_SHELL_INELIGIBLE
} PortableGpRedShellStatus;

typedef enum
{
    PORTABLE_GP_RED_SHELL_COLLISION_NONE = 0,
    PORTABLE_GP_RED_SHELL_COLLISION_IGNORED,
    PORTABLE_GP_RED_SHELL_COLLISION_TARGET_CLEARED,
    PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING,
    PORTABLE_GP_RED_SHELL_COLLISION_DESTROYING_ITEM
} PortableGpRedShellCollisionResult;

typedef struct
{
    uint16_t song_id;
    uint16_t volume;
    uint8_t player;
} PortableGpRedShellAudioEvent;

typedef struct
{
    const PortableShellOrbitRules *orbit_rules;
    const PortableRedShellRules *rules;
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t position_z_q16;
    uint32_t flags_48;
    uint16_t control_96;
    uint16_t angle;
    uint16_t target_heading_56;
    uint8_t response_status_7e,mode_78,decision_190;
    uint16_t shell_item_18c;
    int16_t release_angle_offset;
    int32_t speed_q16;
    uint16_t rank_flags_ba;
    uint8_t floor_36;
    uint8_t item_use_state_17c;
} PortableGpRedShellOwner;

typedef struct
{
    const PortableShellOrbitRules *orbit_rules;
    const PortableRedShellRules *rules;
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t position_z_q16;
    uint32_t creation_order; /* Native head-inserted item chain order. */
    uint16_t kind_10;
    uint16_t angle_26;
    uint16_t flags_34;
    uint16_t animation_tick_4c;
    uint8_t material_4e;
    uint16_t collision_state_50;
    uint16_t terrain_cell_52;
    uint8_t trap_audio_pending;
    uint16_t lifetime_38;
    int32_t velocity_x_q16;
    int32_t velocity_y_q16;
    int32_t vertical_velocity_q16;
    uint8_t floor_36;
    uint8_t animation_index_70;
    uint8_t display_index;
    uint8_t phase;
    uint8_t occupied;
    uint8_t release_timer_74;
    uint8_t speed_byte_71;
    uint8_t route_budget_78;
    uint8_t previous_floor_79;
    uint8_t steering_delay_7a;
    uint8_t target_racer_index;
    uint8_t target_valid;
    uint8_t spiny_targeting; /* 44814 switches guarded contacts to ordinary4D4C. */
    uint16_t destruction_field_5a;
    uint8_t collision_active;
    uint8_t collision_result;
    uint8_t contact_audio_pending;
    uint16_t motion_audio_pending;
    int32_t flight_audio_x_q16,flight_audio_y_q16;
    uint16_t flight_audio_heading;
    uint8_t flight_audio_speed;
    uint8_t flight_audio_pending;
    uint8_t orbit_audio_pending;
    uint8_t retire_held_slot; /*478E8 until an orbit release. */
} PortableGpRedShell;

PortableGpRedShellStatus portable_gp_red_shell_use_triple(
    PortableGpItemInventory *inventory,PortableGpRedShellOwner *owner,
    PortableGpRedShell shells[3],const uint8_t delays[3],const int32_t *sine,size_t sine_count);
/*46DE4 motion only; the roster binds4676C's rank-selected release. */
PortableGpRedShellStatus portable_gp_red_shell_tick_orbit(
    const PortableGpRedShellOwner *owner,PortableGpRedShell *shell,
    const int32_t *sine,size_t sine_count);

/* Scene7f4 candidates in original list order; includes the owner. */
typedef struct {
    int32_t x_q16, y_q16, z_q16;
    uint32_t flags;
    int16_t guard_66;
    uint8_t checkpoint;
    uint8_t rank;
} PortableGpRedShellTarget;

typedef struct {
    const PortableTrackMaterialView *material;
    const PortableTrackRaster *raster;
    const PortableTerrainView *terrain;
    const PortableTerrainEffectTableView *reflection;
    const int32_t *sine;
    size_t sine_count;
    uint8_t engine;
    uint16_t mode_flags;
    uint8_t owner_index;
} PortableGpRedShellWorld;

typedef struct {
    const PortableRacePoint *row1;
    size_t checkpoint_count;
    const PortableRacePoint *sentinel_row1;
    const uint16_t *angle_lut;
    size_t angle_lut_count;
} PortableGpRedShellRoute;

/* 4D4C: normal-race route pursuit or Battle acquisition/pursuit (mode800). */
PortableGpRedShellStatus portable_gp_red_shell_tick_forward(
    PortableGpRedShell *shell,uint32_t *owner_flags,
    const PortableGpRedShellWorld *world,const PortableGpRedShellRoute *route,
    const PortableGpRedShellTarget *targets,size_t target_count);

/* 45c18 -> 45a3c -> 45910 -> 45378, including terrain disposal. */
PortableGpRedShellStatus portable_gp_red_shell_tick_trap(
    PortableGpRedShell *shell, uint32_t *owner_flags,
    const PortableGpRedShellWorld *world,
    const PortableGpRedShellTarget *targets, size_t target_count);

PortableGpRedShellStatus portable_gp_red_shell_use(
    PortableGpItemInventory *inventory,
    PortableGpRedShellOwner *owner,
    PortableGpRedShell *shell,
    const int32_t *sine,
    size_t sine_count);
PortableGpRedShellStatus portable_gp_red_shell_tick_attached(
    PortableGpRedShellOwner *owner,
    PortableGpRedShell *shell,
    const int32_t *sine,
    size_t sine_count);
PortableGpRedShellStatus portable_gp_red_shell_tick_held_active(
    PortableGpRedShellOwner *owner,
    PortableGpRedShell *shell,
    const int32_t *sine,
    size_t sine_count);
/* Source-complete backward sub_8046FF4 -> sub_804676C initializer. */
PortableGpRedShellStatus portable_gp_red_shell_release_backward(
    PortableGpRedShellOwner *owner,
    PortableGpRedShell *shell,
    const int32_t *sine,
    size_t sine_count);
/* Source forward initializer for the rank-zero/null-target branch. */
PortableGpRedShellStatus portable_gp_red_shell_release_forward_leader(
    PortableGpRedShellOwner *owner,
    PortableGpRedShell *shell,
    const int32_t *sine,
    size_t sine_count);
PortableGpRedShellStatus portable_gp_red_shell_release_forward_target(
    PortableGpRedShellOwner *owner,
    PortableGpRedShell *shell,
    uint8_t target_racer_index,
    const int32_t *sine,
    size_t sine_count);
/* 4D4C motion branches used after the full flight owner samples terrain. */
PortableGpRedShellStatus portable_gp_red_shell_tick_forward_no_target_clear(
    PortableGpRedShell *shell,
    uint8_t speed_mode,
    const int32_t *sine,
    size_t sine_count);
uint16_t portable_gp_red_shell_target_angle(int16_t delta_x, int16_t delta_y);
/* Direct pursuit when the target is within the neighboring checkpoints. */
PortableGpRedShellStatus portable_gp_red_shell_tick_forward_target_far_clear(
    PortableGpRedShell *shell,
    int32_t target_x_q16,
    int32_t target_y_q16,
    uint8_t speed_mode,
    const int32_t *sine,
    size_t sine_count);
PortableGpRedShellStatus portable_gp_red_shell_tick_forward_target_close_clear(
    PortableGpRedShell *shell,
    int32_t target_x_q16,
    int32_t target_y_q16,
    int32_t target_z_q16,
    uint32_t target_flags,
    uint8_t speed_mode,
    const int32_t *sine,
    size_t sine_count);
PortableGpRedShellStatus portable_gp_red_shell_tick_target_route_floor(
    PortableGpRedShell *shell,
    uint8_t current_floor);
PortableGpRedShellStatus portable_gp_red_shell_tick_destroying(
    PortableGpRedShell *shell);
/* Attached446FC response; consumes the owner's held slot on impact. */
PortableGpRedShellStatus portable_gp_red_shell_collide_held(
    PortableGpRedShell *shell, PortableGpRedShellOwner *owner,
    uint16_t other_kind, int32_t other_z_q16, int16_t other_field_66);

PortableGpRedShellStatus portable_gp_spiny_shell_collide(
    PortableGpRedShell *shell, uint32_t *owner_flags, uint16_t other_kind,
    int32_t other_z_q16, int16_t guard_66, uint8_t rank, int16_t guard_6a);

PortableGpRedShellStatus portable_gp_red_shell_collide(
    PortableGpRedShell *shell,
    uint32_t *owner_flags,
    uint16_t other_kind,
    int32_t other_z_q16,
    int16_t other_field_66);
PortableGpRedShellStatus portable_gp_red_shell_take_contact_audio(
    PortableGpRedShell *shell,
    uint8_t suppressed,
    uint16_t spatial_volume,
    PortableGpRedShellAudioEvent *event);

#endif
