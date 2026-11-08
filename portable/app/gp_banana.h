#ifndef PORTABLE_GP_BANANA_H
#define PORTABLE_GP_BANANA_H

#include <stddef.h>
#include <stdint.h>

#include "gp_item_inventory.h"
#include "banana_rules.h"
#include "driver_material.h"
#include "race_session.h"

enum
{
    PORTABLE_GP_BANANA_ITEM = 0x1005,
    PORTABLE_GP_BANANA_KIND = 0x8001,
    PORTABLE_GP_BANANA_HELD = 1,
    PORTABLE_GP_BANANA_DROPPED = 2,
    PORTABLE_GP_BANANA_DESTROYING = 3,
    PORTABLE_GP_BANANA_DESTROYED = 4,
    PORTABLE_GP_BANANA_THROWN = 5
};

typedef enum
{
    PORTABLE_GP_BANANA_OK = 0,
    PORTABLE_GP_BANANA_INVALID_ARGUMENT,
    PORTABLE_GP_BANANA_INELIGIBLE
} PortableGpBananaStatus;

typedef enum
{
    PORTABLE_GP_BANANA_COLLISION_NONE = 0,
    PORTABLE_GP_BANANA_COLLISION_IGNORED,
    PORTABLE_GP_BANANA_COLLISION_DESTROYING,
    PORTABLE_GP_BANANA_COLLISION_DESTROYING_ITEM
} PortableGpBananaCollisionResult;

typedef struct
{
    uint16_t song_id;
    uint16_t volume;
    int16_t pitch;
    uint8_t player;
} PortableGpBananaAudioEvent;

typedef struct
{
    const PortableBananaRules *rules;
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t position_z_q16;
    uint32_t flags_48;
    uint16_t control_96;
    uint16_t angle; /* D978 attachment heading. */
    uint16_t release_angle; /* D9B4 forward heading. */
    uint16_t target_heading_56;
    uint8_t response_status_7e;
    uint8_t mode_78;
    uint8_t decision_190;
    uint8_t route_row_191;
    uint16_t shell_item_18c;
    uint8_t floor_36;
    uint8_t item_use_state_17c;
} PortableGpBananaOwner;

typedef struct
{
    const PortableBananaRules *rules;
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t position_z_q16;
    uint32_t creation_order;
    uint16_t collision_state_50;
    uint16_t collision_guard_38;
    uint16_t motion_audio_pending;
    int32_t velocity_x_q16;
    int32_t velocity_y_q16;
    uint8_t material_4e;
    uint16_t kind_10;
    uint16_t angle_26;
    uint16_t flags_34;
    uint16_t attachment_ticks_4c;
    uint8_t floor_36;
    uint8_t phase;
    uint8_t occupied;
    uint8_t collision_active;
    int32_t vertical_velocity_q16;
    uint16_t destruction_field_5a;
    uint8_t collision_result;
    uint8_t contact_audio_pending;
} PortableGpBanana;

PortableGpBananaStatus portable_gp_banana_use(
    PortableGpItemInventory *inventory,
    PortableGpBananaOwner *owner,
    PortableGpBanana *banana,
    const int32_t *sine,
    size_t sine_count);

typedef struct {
    const PortableTrackMaterialView *material;
    const PortableTrackRaster *raster;
    const PortableTerrainView *terrain;
    const PortableRacePoint *route;
    const PortableRacePoint *route_rows;
    size_t route_row_count;
    size_t checkpoint_count;
} PortableGpBananaWorld;

/* 47F1C -> 47C2C/47DE8 -> 47AAC. Includes held visibility, ordinary
 * Up release, CPU route throw, ballistic motion and terrain landing. */
PortableGpBananaStatus portable_gp_banana_tick(
    PortableGpBananaOwner *owner, PortableGpBanana *banana,
    const PortableGpBananaWorld *world,const int32_t *sine,size_t sine_count);
PortableGpBananaStatus portable_gp_banana_take_motion_audio(
    PortableGpBanana *banana,uint8_t suppressed,int32_t camera_x,int32_t camera_y,
    PortableGpBananaAudioEvent *event);
PortableGpBananaStatus portable_gp_banana_collide(
    PortableGpBanana *banana, uint32_t *owner_flags,
    uint16_t other_kind, int16_t other_field_66);
/* 479E0 clears the shared held-item18C; unlike shells it leaves17C alone. */
PortableGpBananaStatus portable_gp_banana_collide_held(
    PortableGpBanana *banana, PortableGpBananaOwner *owner,
    uint16_t other_kind, int16_t other_field_66);
PortableGpBananaStatus portable_gp_banana_take_contact_audio(
    PortableGpBanana *banana, uint8_t suppressed, uint16_t spatial_volume,
    PortableGpBananaAudioEvent *event);
PortableGpBananaStatus portable_gp_banana_tick_destroying(
    PortableGpBanana *banana);

#endif
