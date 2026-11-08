#ifndef PORTABLE_GP_GREEN_SHELL_H
#define PORTABLE_GP_GREEN_SHELL_H

#include <stddef.h>
#include <stdint.h>

#include "gp_item_inventory.h"
#include "green_shell_rules.h"
#include "shell_orbit_rules.h"
#include "driver_material.h"
#include "driver_terrain_effect.h"

enum
{
    PORTABLE_GP_GREEN_SHELL_ITEM = 0x1000,
    PORTABLE_GP_TRIPLE_GREEN_SHELL_ITEM = 0x1001,
    PORTABLE_GP_GREEN_SHELL_KIND = 0x8401,
    PORTABLE_GP_GREEN_SHELL_ATTACHED = 1,
    PORTABLE_GP_GREEN_SHELL_ACTIVE = 2,
    PORTABLE_GP_GREEN_SHELL_RELEASED = 3,
    PORTABLE_GP_GREEN_SHELL_DESTROYING = 4,
    PORTABLE_GP_GREEN_SHELL_DESTROYED = 5,
    PORTABLE_GP_GREEN_SHELL_TERRAIN_DESTRUCTION_PENDING = 6,
    PORTABLE_GP_GREEN_SHELL_ORBIT_PENDING = 7,
    PORTABLE_GP_GREEN_SHELL_ORBITING = 8
};

typedef enum
{
    PORTABLE_GP_GREEN_SHELL_OK = 0,
    PORTABLE_GP_GREEN_SHELL_INVALID_ARGUMENT,
    PORTABLE_GP_GREEN_SHELL_INELIGIBLE
} PortableGpGreenShellStatus;

typedef enum
{
    PORTABLE_GP_GREEN_SHELL_COLLISION_NONE = 0,
    PORTABLE_GP_GREEN_SHELL_COLLISION_IGNORED,
    PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING,
    PORTABLE_GP_GREEN_SHELL_COLLISION_DESTROYING_ITEM
} PortableGpGreenShellCollisionResult;

typedef struct
{
    uint16_t song_id;
    uint16_t volume;
    uint8_t player;
} PortableGpGreenShellAudioEvent;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t position_z_q16;
    uint32_t flags_48;
    uint16_t control_96;
    uint16_t angle; /* D978 attachment heading. */
    uint16_t target_heading_56;
    uint8_t response_status_7e,mode_78,decision_190;
    uint16_t shell_item_18c;
    int32_t speed_q16;
    int16_t release_angle_offset;
    uint8_t floor_36;
    uint8_t item_use_state_17c;
    const PortableShellOrbitRules *orbit_rules;
    const PortableGreenShellRules *rules;
} PortableGpGreenShellOwner;

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t position_z_q16;
    int32_t velocity_x_q16;
    int32_t velocity_y_q16;
    int32_t velocity_z_q16;
    int32_t vertical_velocity_q16;
    uint32_t creation_order; /* Native head-inserted item chain order. */
    uint16_t kind_10;
    uint16_t angle_26;
    uint16_t flags_34;
    uint16_t animation_tick_4c;
    uint16_t lifetime_38;
    uint16_t collision_state_50;
    uint16_t terrain_state_52;
    uint16_t destruction_field_5a;
    uint8_t floor_36;
    uint8_t animation_index_70;
    uint8_t display_index;
    uint8_t state_71;
    uint8_t collision_flags_4e;
    uint8_t release_timer_74;
    uint8_t phase;
    uint8_t occupied;
    uint8_t collision_result;
    uint8_t contact_audio_pending;
    uint8_t launch_audio_pending;
    uint8_t orbit_audio_pending;
    uint8_t retire_held_slot; /*478E8 remains installed until an orbit release. */
    const PortableShellOrbitRules *orbit_rules;
    const PortableGreenShellRules *rules; /* Immutable content; never serialized. */
} PortableGpGreenShell;

PortableGpGreenShellStatus portable_gp_green_shell_use(
    PortableGpItemInventory *inventory,
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count);

/*473FC/478C8/46F5C/46DE4: staggered construction and shared held collision. */
PortableGpGreenShellStatus portable_gp_green_shell_use_triple(
    PortableGpItemInventory *inventory, PortableGpGreenShellOwner *owner,
    PortableGpGreenShell shells[3], const uint8_t delays[3],
    const int32_t *sine, size_t sine_count);
PortableGpGreenShellStatus portable_gp_green_shell_tick_orbit(
    PortableGpGreenShellOwner *owner, PortableGpGreenShell *shell,
    const int32_t *sine, size_t sine_count);

PortableGpGreenShellStatus portable_gp_green_shell_tick_attached(
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count);

/* 6FF4 owns attachment, animation, recovery visibility and the held/release
 * decision, including the CPU timeout and forward/backward selection. */
PortableGpGreenShellStatus portable_gp_green_shell_tick_held_active(
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count);

PortableGpGreenShellStatus portable_gp_green_shell_release(
    PortableGpGreenShellOwner *owner,
    PortableGpGreenShell *shell,
    const int32_t *sine,
    size_t sine_count);

typedef struct {
    const PortableTrackMaterialView *material;
    const PortableTrackRaster *raster;
    const PortableTerrainView *terrain;
    const PortableTerrainEffectTableView *reflection;
} PortableGpGreenShellWorld;

/*4BA0/4AAC/49EC/4920: reflection, floor update, disposal and integration. */
PortableGpGreenShellStatus portable_gp_green_shell_tick_released(
    PortableGpGreenShellOwner *owner,PortableGpGreenShell *shell,
    const PortableGpGreenShellWorld *world);

/* Attached446FC response; consumes the owner's held slot on impact. */
PortableGpGreenShellStatus portable_gp_green_shell_collide_held(
    PortableGpGreenShell *shell, PortableGpGreenShellOwner *owner,
    uint16_t other_kind, int32_t other_z_q16, int16_t other_field_66);

PortableGpGreenShellStatus portable_gp_green_shell_collide(
    PortableGpGreenShell *shell,
    uint32_t *owner_flags,
    uint16_t other_kind,
    int32_t other_z_q16,
    int16_t other_field_66,
    uint8_t same_owner);

PortableGpGreenShellStatus portable_gp_green_shell_take_contact_audio(
    PortableGpGreenShell *shell,
    uint8_t suppressed,
    uint16_t spatial_volume,
    PortableGpGreenShellAudioEvent *event);

PortableGpGreenShellStatus portable_gp_green_shell_tick_destroying(
    PortableGpGreenShell *shell);

#endif
