#ifndef MKSC_PORTABLE_DRIVER_VECTOR_EFFECT_H
#define MKSC_PORTABLE_DRIVER_VECTOR_EFFECT_H

#include <stddef.h>
#include <stdint.h>
#include "race_session.h"
#include "track_raster.h"

#include "driver_material.h"
#include "terrain_effect_tables.h"

enum
{
    PORTABLE_DRIVER_VECTOR_EFFECT_OK = 0,
    PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_VECTOR_EFFECT_UNSUPPORTED_MODE = 2,
    PORTABLE_DRIVER_VECTOR_EFFECT_QUERY_ERROR = 3,
};

enum
{
    PORTABLE_DRIVER_VECTOR_TRIGGER_OK = 0,
    PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE = 1,
    PORTABLE_DRIVER_VECTOR_TRIGGER_INVALID_ARGUMENT = 2,
};

enum
{
    PORTABLE_DRIVER_VECTOR_SCHEDULER_OK = 0,
    PORTABLE_DRIVER_VECTOR_SCHEDULER_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_VECTOR_SCHEDULER_INVALID_RESULT = 2,
};

typedef struct
{
    int32_t mode;
    uint8_t x_neighbor_flags;
    uint8_t y_neighbor_flags;
    uint8_t orientation;
    uint8_t component_mutated;
    uint8_t alternate_state_branch;
    uint8_t source_direction_index;
    uint16_t contact_word_5a;
} PortableDriverVectorEffectResult;

typedef struct
{
    uint32_t source_flags;
    uint16_t angle_delta;
    uint8_t timer_value;
    uint8_t response_applied;
} PortableDriverVectorCollisionResult;

typedef struct
{
    uint32_t source_flags;
    uint16_t angle_history_7a;
    uint16_t angle_delta_80;
    uint16_t response_angle_7a;
    uint16_t base_heading_7c;
    int16_t resistance_92;
    int32_t magnitude_state_bc;
    uint8_t timer_value_84;
    uint8_t response_timer_7e;
    uint8_t response_applied;
} PortableDriverNormalCollisionResult;

typedef struct
{
    uint16_t previous_cell_key;
    uint16_t current_cell_key;
    int16_t cell_delta;
    int32_t mode;
    uint8_t material_flags;
    uint8_t candidate; /* B018 can reflect a supported cell transition */
    uint8_t contact;   /* B2B4 returns true even without a cell transition */
    uint8_t alternate_state_branch;
} PortableDriverVectorTriggerResult;

typedef struct
{
    uint16_t next_source_state;
    uint8_t alternate_state_branch;
    uint8_t normal_collision_scheduled;
} PortableDriverVectorScheduleResult;

typedef struct
{
    int16_t resultant_x;
    int16_t resultant_y;
    uint16_t current_angle;
    uint32_t source_flags;
    uint16_t prior_angle_history_7a;
    uint16_t prior_angle_delta_80;
    int16_t prior_resistance_92;
    int32_t prior_magnitude_state_bc;
    uint8_t prior_timer_value_84;
    uint8_t prior_response_timer_7e;
} PortableDriverNormalCollisionSourceInput;

typedef struct
{
    uint16_t impact_angle;
    uint16_t response_angle;
    int16_t resultant_x;
    int16_t resultant_y;
} PortableDriverObjectContactResult;

/*
 * Recover the scheduling algebra in sub_803B2B4 without applying the effect.
 * The source caller supplies sub_8032990's material byte at +0x85, the
 * previous/current position cell keys, the state+8 height gate and +0x60.
 * Bit 0x4000 does not block sub_803B2B4: sub_803B550 still calls it but takes
 * an alternate component-write path afterward. The result reports that branch
 * so the caller does not mistake the normal sub_803B018 write for the full
 * alternate behavior. The sub_803B110 special-cell helper remains separate.
 */
int portable_driver_vector_effect_source_trigger(
    uint8_t material_flags,
    uint16_t previous_cell_key,
    uint16_t current_cell_key,
    uint8_t previous_cell_key_valid,
    int32_t height_state_q16,
    uint16_t source_state,
    PortableDriverVectorTriggerResult *out);

/* Reproduce sub_803B550's state+0x60 scheduling boundary after the caller has
 * evaluated sub_803B2B4. A nonzero result arms the low-byte counter and its
 * 0x8000/0xc000 high bits; a zero result consumes that state and schedules the
 * normal +0x1000 response only when the 0x8000 arm was present. The caller
 * still owns the source result payload and any later handler timing. */
int portable_driver_vector_effect_source_schedule_normal_collision(
    uint16_t source_state,
    uint8_t sub_b2b4_result,
    PortableDriverVectorScheduleResult *out);

/*
 * Apply the kart-owned component effect from sub_803B018. The source writes
 * the signed halfword components at +0x4e/+0x50, so this helper deliberately
 * wraps negation at 16 bits. Odd modes inspect the two source material
 * neighbors and then follow sub_803DC64's orientation dispatch; the caller
 * owns all map/table storage.
 */
int portable_driver_vector_effect_apply(
    const PortableDriverMaterialState *material,
    const PortableTrackMaterialView *track_material,
    const PortableTerrainView *terrain,
    const PortableTerrainEffectTableView *tables,
    int32_t mode,
    int16_t *component_x,
    int16_t *component_y,
    PortableDriverVectorEffectResult *out);

/* Apply sub_803B550's alternate branch after sub_803B2B4 succeeds. */
int portable_driver_vector_effect_apply_alternate(
    uint16_t source_direction,
    int32_t trigger_mode,
    const int16_t *x_components,
    size_t x_count,
    const int16_t *y_components,
    size_t y_count,
    int16_t *component_x,
    int16_t *component_y,
    PortableDriverVectorEffectResult *out);

/* Apply the kart-owned sub_803AE18 response contract. The source computes
 * impact_angle with sub_802EC38 before entering this helper; keeping that
 * value caller-supplied avoids inventing a host trigonometry replacement.
 * The helper clears source +0x1000 flags on every call, and only overwrites
 * +0x82/+0x84 when the measured resultant vector is nonzero. */
int portable_driver_vector_effect_apply_collision(
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t impact_angle,
    uint16_t source_heading,
    uint32_t source_flags,
    uint16_t prior_angle_delta,
    uint8_t prior_timer_value,
    PortableDriverVectorCollisionResult *out);

/* Reproduce sub_802EC38's GBA BIOS SWI #0x0a angle producer for signed
 * halfword components. This is fixed-point integer arithmetic matching the
 * source ArcTan2 wrapper plus 0x4000; it deliberately does not use host
 * floating-point trigonometry. */
int portable_driver_vector_effect_source_angle(
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t *out_angle);

/* sub_8040570's fixed-point terrain-object resultant. The caller supplies
 * sub_8040810's object center and the driver's +0xee/+0xf0 anchor, prior
 * +0x4e/+0x50 motion, +0x7a angle and the source 1024-word sine table.
 * Scheduling, flags and timers are handled by the caller. */
/* 4073C: persistent object contact uses current position and a fixed push. */
int portable_driver_object_contact_escape(
    int16_t position_x, int16_t position_y,
    int16_t object_x, int16_t object_y,
    const int32_t *sine_table, size_t sine_count,
    PortableDriverObjectContactResult *out);

int portable_driver_object_contact_resultant(
    int16_t anchor_x, int16_t anchor_y,
    int16_t object_x, int16_t object_y,
    int16_t prior_x, int16_t prior_y, uint16_t heading_7a,
    const int32_t *sine_table, size_t sine_count,
    PortableDriverObjectContactResult *out);

typedef struct
{
    size_t checkpoint_index;
    PortableRacePoint position;
    uint16_t heading;
} PortableDriverRecoveryTarget;

/* sub_8039828: row-zero target from driver90, excluding the final sentinel;
 * sub_803285C obtains its heading from the raster at that target. */
int portable_driver_recovery_target(
    const PortableRacePoint *route, size_t route_count, uint8_t checkpoint,
    const PortableTrackRaster *raster,
    const uint16_t *angle_lut, size_t angle_lut_count,
    PortableDriverRecoveryTarget *out);

/* sub_803285C(x, y, row 0): the course direction from a world position
 * toward the route point its raster cell names. */
int portable_driver_course_heading(int32_t position_x_q16, int32_t position_y_q16,
    const PortableRacePoint *route, size_t route_count,
    const PortableTrackRaster *raster,
    const uint16_t *angle_lut, size_t angle_lut_count, uint16_t *out_heading);

/* sub_803285C -> sub_802EAEC course direction from a raster checkpoint. */
int portable_driver_track_direction_source_angle(
    int32_t position_x_q16, int32_t position_y_q16,
    int32_t checkpoint_x_world, int32_t checkpoint_y_world,
    const uint16_t *angle_lut, size_t angle_lut_count,
    uint16_t *out_angle);

/* Apply sub_803AD6C's normal resultant-vector response. The source impact
 * angle and sub_802EBC0 magnitude are represented by caller-owned inputs or
 * exact local arithmetic; this helper does not substitute host trigonometry.
 * Source +0x7c is supplied as current_angle, while +0x7a/+0x80/+0x92/+0xbc
 * and the two byte timers are carried through the result. */
int portable_driver_vector_effect_apply_normal_collision(
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t impact_angle,
    uint16_t current_angle,
    uint32_t source_flags,
    uint16_t prior_angle_history_7a,
    uint16_t prior_angle_delta_80,
    int16_t prior_resistance_92,
    int32_t prior_magnitude_state_bc,
    uint8_t prior_timer_value_84,
    uint8_t prior_response_timer_7e,
    PortableDriverNormalCollisionResult *out);

/* Apply the same normal response after producing impact_angle through the
 * source sub_802EC38 ArcTan2 wrapper. This is a bounded source-angle adapter;
 * the caller still owns the event boundary and prior state. */
int portable_driver_vector_effect_apply_normal_collision_source_angle(
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t current_angle,
    uint32_t source_flags,
    uint16_t prior_angle_history_7a,
    uint16_t prior_angle_delta_80,
    int16_t prior_resistance_92,
    int32_t prior_magnitude_state_bc,
    uint8_t prior_timer_value_84,
    uint8_t prior_response_timer_7e,
    PortableDriverNormalCollisionResult *out);

#endif
