#ifndef PORTABLE_AMBIENT_H
#define PORTABLE_AMBIENT_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_AMBIENT_LANES = 16,
    PORTABLE_AMBIENT_FRAMES = 256,
    PORTABLE_AMBIENT_PATCHES = 768,
    PORTABLE_AMBIENT_IMAGE_PATCHES = 32
};
enum
{
    PORTABLE_AMBIENT_PRE_PROFILE,
    PORTABLE_AMBIENT_POST_DRIVER
};
enum
{
    PORTABLE_AMBIENT_ON_ENTRY,
    PORTABLE_AMBIENT_EVERY_CALL
};
enum
{
    PORTABLE_AMBIENT_TIMER,
    PORTABLE_AMBIENT_SUNSET,
    PORTABLE_AMBIENT_DIRECTION_TILES,
    PORTABLE_AMBIENT_FINISH_BACKDROP,
    PORTABLE_AMBIENT_RAINBOW,
    PORTABLE_AMBIENT_STORM
};
enum
{
    PORTABLE_AMBIENT_AUTOSTART,
    PORTABLE_AMBIENT_MANUAL
};
enum
{
    PORTABLE_AMBIENT_ROAD,
    PORTABLE_AMBIENT_SKY,
    PORTABLE_AMBIENT_OBJECT,
    PORTABLE_AMBIENT_SKY_TILES,
    PORTABLE_AMBIENT_FRONT_MAP,
    PORTABLE_AMBIENT_MID_MAP,
    PORTABLE_AMBIENT_BACK_MAP
};

typedef struct
{
    uint8_t target;
    uint16_t offset, count; /* Colors for palettes; bytes for image patches. */
    uint32_t data_offset;
} PortableAmbientPatch;

typedef struct
{
    uint16_t duration, patch_count;
    uint32_t first_patch;
} PortableAmbientFrame;

typedef struct
{
    uint8_t stage, write_policy, controller, trigger;
    uint16_t first_frame, frame_count, loop_frame, initial_frame;
    uint32_t initial_counter, params[4];
} PortableAmbientLane;

typedef struct
{
    const PortableAmbientLane *lanes;
    const PortableAmbientFrame *frames;
    const PortableAmbientPatch *patches, *init_patches;
    const uint8_t *data;
    uint16_t lane_count, frame_count, patch_count, init_patch_count;
    uint32_t data_size, object_seed_offset;
} PortableAmbientRules;

typedef struct
{
    uint16_t road[64], sky[64], object[128];
} PortableAmbientSeed;

typedef struct
{
    uint16_t road[64], sky[64], object[128];
    uint8_t object_dirty[128];
    /* Indices borrow immutable data from Rules. Bit15 selects init_patches. */
    uint16_t image_patch_ids[PORTABLE_AMBIENT_IMAGE_PATCHES];
    uint8_t image_patch_count, initialized;
} PortableAmbientSnapshot;

typedef struct
{
    uint16_t lane, frame;
    uint32_t counter, creation_order;
    int32_t control_counter;
    uint8_t active, pending, started, controller_phase;
} PortableAmbientLaneState;

typedef struct
{
    PortableAmbientLaneState lanes[PORTABLE_AMBIENT_LANES];
    PortableAmbientLaneState profile_lanes[PORTABLE_AMBIENT_LANES];
    PortableAmbientSnapshot snapshot;
    uint32_t next_creation_order, post_serial;
    uint8_t initialized, in_tick;
} PortableAmbientState;

typedef struct
{
    uint32_t clock, selected_flags;
    int32_t selected_speed_q16, selected_base_speed_q16;
    uint16_t selected_progress, leader_progress;
    uint8_t leader_route_wrap, race_phase_7d4;
    uint8_t sky_phase_before, sky_phase_after;
    int32_t sky_elapsed_before, sky_counter_before;
    uint8_t paused, source_gate, scene_mode_80d;
} PortableAmbientInputs;

const PortableAmbientPatch *portable_ambient_patch(const PortableAmbientRules *rules, uint16_t id);
int portable_ambient_rules_valid(const PortableAmbientRules *r);
int portable_ambient_init(PortableAmbientState *s, const PortableAmbientRules *r,
                          const PortableAmbientSeed *seed);
int portable_ambient_spawn(PortableAmbientState *s, const PortableAmbientRules *r,
                           uint16_t lane_id);
int portable_ambient_tick(PortableAmbientState *s, const PortableAmbientRules *r, uint8_t stage,
                          const PortableAmbientInputs *in, const int32_t *sine, size_t sine_count);
uint32_t portable_ambient_digest(const PortableAmbientState *s);

#endif
