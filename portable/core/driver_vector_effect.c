#include "driver_vector_effect.h"

#include <stdint.h>

static int16_t negate_component(int16_t value)
{
    return (int16_t)(uint16_t)(UINT16_C(0) - (uint16_t)value);
}

static int32_t source_vector_magnitude(int16_t x, int16_t y)
{
    int32_t x_abs = x;
    int32_t y_abs = y;
    int32_t maximum;
    int32_t minimum;
    int32_t iterations;

    if (x_abs < 0)
        x_abs = -x_abs;
    if (y_abs < 0)
        y_abs = -y_abs;
    maximum = x_abs > y_abs ? x_abs : y_abs;
    minimum = x_abs > y_abs ? y_abs : x_abs;
    if (minimum == 0)
        return maximum;

    /* Exact integer structure of sub_802EBC0. For the source halfword
     * components the ratio/factor steps commonly collapse to zero, but keep
     * the source loop and widths explicit instead of replacing it with a
     * floating-point norm. */
    iterations = 2;
    do
    {
        int32_t ratio = minimum / maximum;
        int32_t square = ratio * ratio;
        int32_t factor = square / (square + 4);

        maximum += (factor << 1) * maximum;
        minimum *= factor;
        --iterations;
    } while (iterations >= 0);
    return maximum;
}

static int16_t source_asr_one_s16(int16_t value)
{
    const int32_t widened = value;
    const int32_t shifted = widened >= 0
                                ? widened / 2
                                : -(((-widened) + 1) / 2);
    return (int16_t)shifted;
}

static int64_t source_asr_s64(int64_t value, unsigned int shift)
{
    const int64_t divisor = INT64_C(1) << shift;

    if (value >= 0)
        return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

static int32_t source_arc_tan(int32_t input)
{
    int64_t a = -source_asr_s64((int64_t)input * input, 14);
    int64_t b = source_asr_s64(INT64_C(0xa9) * a, 14) + 0x390;

    b = source_asr_s64(b * a, 14) + 0x91c;
    b = source_asr_s64(b * a, 14) + 0xfb6;
    b = source_asr_s64(b * a, 14) + 0x16aa;
    b = source_asr_s64(b * a, 14) + 0x2081;
    b = source_asr_s64(b * a, 14) + 0x3651;
    b = source_asr_s64(b * a, 14) + 0xa2f9;
    return (int32_t)source_asr_s64((int64_t)input * b, 16);
}

static int32_t source_ratio_q14(int32_t numerator, int32_t denominator)
{
    return (int32_t)(((int64_t)numerator * INT64_C(0x4000)) /
                     denominator);
}

static uint16_t source_arc_tan2(int32_t x, int32_t y)
{
    int32_t ratio;

    if (y == 0)
        return x < 0 ? UINT16_C(0x8000) : UINT16_C(0x0000);
    if (x == 0)
        return y < 0 ? UINT16_C(0xc000) : UINT16_C(0x4000);

    if (y >= 0)
    {
        if (x >= 0)
        {
            if (x >= y)
            {
                ratio = source_ratio_q14(y, x);
                return (uint16_t)source_arc_tan(ratio);
            }
        }
        else if (-x >= y)
        {
            ratio = source_ratio_q14(y, x);
            return (uint16_t)(source_arc_tan(ratio) + INT32_C(0x8000));
        }
        ratio = source_ratio_q14(x, y);
        return (uint16_t)(INT32_C(0x4000) - source_arc_tan(ratio));
    }

    if (x <= 0)
    {
        if (-x > -y)
        {
            ratio = source_ratio_q14(y, x);
            return (uint16_t)(source_arc_tan(ratio) + INT32_C(0x8000));
        }
    }
    else if (x >= -y)
    {
        ratio = source_ratio_q14(y, x);
        return (uint16_t)(source_arc_tan(ratio) + INT32_C(0x10000));
    }

    ratio = source_ratio_q14(x, y);
    return (uint16_t)(INT32_C(0xc000) - source_arc_tan(ratio));
}

int portable_driver_vector_effect_source_schedule_normal_collision(
    uint16_t source_state,
    uint8_t sub_b2b4_result,
    PortableDriverVectorScheduleResult *out)
{
    uint8_t next_count;

    if (out == NULL)
        return PORTABLE_DRIVER_VECTOR_SCHEDULER_INVALID_ARGUMENT;
    if (sub_b2b4_result > 1u)
        return PORTABLE_DRIVER_VECTOR_SCHEDULER_INVALID_RESULT;

    *out = (PortableDriverVectorScheduleResult){
        .next_source_state = source_state,
        .alternate_state_branch =
            (uint8_t)((source_state & UINT16_C(0x4000)) != 0),
    };

    /* sub_803B550 dispatches the same predicate first, but its 0x4000 arm
     * takes the direct component-write path and does not alter the normal
     * response scheduler state on success. */
    if (out->alternate_state_branch)
    {
        if (sub_b2b4_result == 0u)
            out->next_source_state = 0;
        return PORTABLE_DRIVER_VECTOR_SCHEDULER_OK;
    }

    if (sub_b2b4_result != 0u)
    {
        next_count = (uint8_t)((uint8_t)source_state + 1u);
        out->next_source_state = (uint16_t)(source_state & 0xff00u);
        out->next_source_state = (uint16_t)(
            out->next_source_state |
            (next_count <= 7u ? UINT16_C(0x8000) : UINT16_C(0xc000)) |
            next_count);
    }
    else
    {
        out->normal_collision_scheduled =
            (uint8_t)((source_state & UINT16_C(0x8000)) != 0);
        out->next_source_state = 0;
    }
    return PORTABLE_DRIVER_VECTOR_SCHEDULER_OK;
}

static int add_offset(int32_t value, int32_t offset, int32_t *out)
{
    const int64_t widened = (int64_t)value + offset;

    if (widened < INT32_MIN || widened > INT32_MAX)
        return 0;
    *out = (int32_t)widened;
    return 1;
}

static int query_neighbor_flags(
    const PortableTrackMaterialView *track_material,
    const PortableTerrainView *terrain,
    int32_t world_x,
    int32_t world_y,
    uint8_t *out_flags)
{
    uint8_t terrain_index;

    if (portable_terrain_query8(terrain, world_x, world_y,
                                &terrain_index) != PORTABLE_TERRAIN_QUERY_OK)
        return 0;
    return portable_track_material_query(track_material, terrain_index,
                                         out_flags) == 0;
}

int portable_driver_vector_effect_source_trigger(
    uint8_t material_flags,
    uint16_t previous_cell_key,
    uint16_t current_cell_key,
    uint8_t previous_cell_key_valid,
    int32_t height_state_q16,
    uint16_t source_state,
    PortableDriverVectorTriggerResult *out)
{
    int32_t delta;
    int32_t work;

    if (out == NULL)
        return PORTABLE_DRIVER_VECTOR_TRIGGER_INVALID_ARGUMENT;
    *out = (PortableDriverVectorTriggerResult){
        .previous_cell_key = previous_cell_key,
        .current_cell_key = current_cell_key,
        .cell_delta = (int16_t)(uint16_t)(current_cell_key -
                                          previous_cell_key),
        .mode = -1,
        .material_flags = material_flags,
        .alternate_state_branch =
            (uint8_t)((source_state & UINT16_C(0x4000)) != 0),
    };
    if (height_state_q16 > INT32_C(0x00040000) ||
        (material_flags & UINT8_C(0x90)) != UINT8_C(0x80))
        return PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE;
    out->contact = 1u;
    if (!previous_cell_key_valid)
        return PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE;

    delta = out->cell_delta;
    if (delta == 0)
        return PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE;

    /* This is the signed-halfword delta path at .L0803B4F8-.L0803B516. */
    work = delta;
    if (work < 0)
    {
        const int32_t magnitude = -work;
        /* sub_803B2B4: (magnitude << 16) - 0x00fa0000, then high half.
         * A one-row key change (-0x100) therefore selects mode 6. */
        const int32_t adjusted = (int32_t)(
            (int64_t)magnitude * INT64_C(0x10000) -
            INT64_C(0x00fa0000));

        if (adjusted < 0)
            work = 4;
        else
            work = (int32_t)((uint32_t)adjusted >> 16);
    }
    else
    {
        --work;
        if (work == 0)
            work = 0;
        else
            work -= 0xfd;
    }
    if (work < 0 || work > 7)
        return PORTABLE_DRIVER_VECTOR_TRIGGER_NO_CANDIDATE;

    out->mode = work;
    out->candidate = 1;
    return PORTABLE_DRIVER_VECTOR_TRIGGER_OK;
}

int portable_driver_vector_effect_apply(
    const PortableDriverMaterialState *material,
    const PortableTrackMaterialView *track_material,
    const PortableTerrainView *terrain,
    const PortableTerrainEffectTableView *tables,
    int32_t mode,
    int16_t *component_x,
    int16_t *component_y,
    PortableDriverVectorEffectResult *out)
{
    int32_t neighbor_x;
    int32_t neighbor_y;
    uint8_t x_flags;
    uint8_t y_flags;
    uint8_t orientation;

    if (component_x == NULL || component_y == NULL || out == NULL)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    *out = (PortableDriverVectorEffectResult){
        .mode = mode,
        .contact_word_5a = UINT16_C(0xc007),
    };

    /* B018 stores +0x5a=0xc007 after direct reflection. Unsupported
     * transition modes are rejected by the native API before mutation. */
    if (mode < 0 || mode > 7)
        return PORTABLE_DRIVER_VECTOR_EFFECT_UNSUPPORTED_MODE;

    if (mode == 0 || mode == 4)
    {
        *component_x = negate_component(*component_x);
        out->orientation = (uint8_t)(((uint16_t)*component_x >> 15) & 1u);
        out->source_direction_index = out->orientation;
        out->component_mutated = 1;
        return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
    }
    if (mode == 2 || mode == 6)
    {
        *component_y = negate_component(*component_y);
        out->orientation = (uint8_t)(*component_y < 0 ? 3u : 2u);
        out->source_direction_index = out->orientation;
        out->component_mutated = 1;
        return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
    }

    if (material == NULL || track_material == NULL || terrain == NULL ||
        tables == NULL || tables->x_offsets == NULL ||
        tables->y_offsets == NULL || tables->x_count < 8u ||
        tables->y_count < 8u || !material->in_bounds)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    if (!add_offset(material->world_x, tables->x_offsets[mode], &neighbor_x) ||
        !query_neighbor_flags(track_material, terrain, neighbor_x,
                              material->world_y, &x_flags) ||
        !add_offset(material->world_y, tables->y_offsets[mode], &neighbor_y) ||
        !query_neighbor_flags(track_material, terrain, material->world_x,
                              neighbor_y, &y_flags))
        return PORTABLE_DRIVER_VECTOR_EFFECT_QUERY_ERROR;

    orientation = (uint8_t)(((x_flags & UINT8_C(0x80)) != 0 ? 2u : 0u) |
                             ((y_flags & UINT8_C(0x80)) != 0 ? 1u : 0u));
    out->x_neighbor_flags = x_flags;
    out->y_neighbor_flags = y_flags;
    out->orientation = orientation;
    switch (orientation)
    {
    case 0:
    case 3:
        *component_x = negate_component(*component_x);
        *component_y = negate_component(*component_y);
        break;
    case 2:
        *component_y = negate_component(*component_y);
        break;
    case 1:
        *component_x = negate_component(*component_x);
        break;
    default:
        return PORTABLE_DRIVER_VECTOR_EFFECT_QUERY_ERROR;
    }
    /* DC64 calls B018 mode 0 only for neighbor orientation 1;
     * all other corners end in mode 2, which stores direction from Y. */
    out->source_direction_index = orientation == 1u
        ? (uint8_t)(((uint16_t)*component_x >> 15) & 1u)
        : (uint8_t)(*component_y < 0 ? 3u : 2u);
    out->component_mutated = 1;
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

int portable_driver_vector_effect_apply_alternate(
    uint16_t source_direction,
    int32_t trigger_mode,
    const int16_t *x_components,
    size_t x_count,
    const int16_t *y_components,
    size_t y_count,
    int16_t *component_x,
    int16_t *component_y,
    PortableDriverVectorEffectResult *out)
{
    const uint8_t direction_index = (uint8_t)(source_direction >> 14);

    if (x_components == NULL || y_components == NULL ||
        component_x == NULL || component_y == NULL || out == NULL)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;
    if (x_count < 4u || y_count < 4u)
        return PORTABLE_DRIVER_VECTOR_EFFECT_QUERY_ERROR;

    *out = (PortableDriverVectorEffectResult){
        .mode = trigger_mode,
        .alternate_state_branch = 1,
        .source_direction_index = direction_index,
        /* B6A4 clears contact word +0x5a after the forced component write. */
        .contact_word_5a = 0,
    };
    *component_x = x_components[direction_index];
    *component_y = y_components[direction_index];
    out->component_mutated = 1;
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

int portable_driver_vector_effect_apply_collision(
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t impact_angle,
    uint16_t source_heading,
    uint32_t source_flags,
    uint16_t prior_angle_delta,
    uint8_t prior_timer_value,
    PortableDriverVectorCollisionResult *out)
{
    if (out == NULL)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    *out = (PortableDriverVectorCollisionResult){
        .source_flags = source_flags & UINT32_C(0xffffefff),
        .angle_delta = prior_angle_delta,
        .timer_value = prior_timer_value,
    };
    if (resultant_x == 0 && resultant_y == 0)
        return PORTABLE_DRIVER_VECTOR_EFFECT_OK;

    /* sub_803AE18 stores a halfword subtraction, so unsigned conversion
     * preserves the source's 16-bit wrap at both ends of the angle range. */
    out->angle_delta = (uint16_t)(impact_angle - source_heading);
    out->timer_value = UINT8_C(0x0b);
    out->response_applied = 1;
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

int portable_driver_vector_effect_source_angle(
    int16_t resultant_x,
    int16_t resultant_y,
    uint16_t *out_angle)
{
    if (out_angle == NULL)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    /* sub_802EC38 returns (ArcTan2(x, y) + 0x4000) as a 16-bit value. */
    *out_angle = (uint16_t)(source_arc_tan2(resultant_x, resultant_y) +
                            UINT16_C(0x4000));
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

static int32_t object_contact_wrap_s32(int64_t value)
{
    const uint32_t wrapped = (uint32_t)value;
    return wrapped <= INT32_MAX ? (int32_t)wrapped
                                : (int32_t)((int64_t)wrapped - INT64_C(0x100000000));
}

static int16_t object_contact_wrap_s16(int32_t value)
{
    const uint16_t wrapped = (uint16_t)value;
    return wrapped <= INT16_MAX ? (int16_t)wrapped
                                : (int16_t)((int32_t)wrapped - INT32_C(0x10000));
}

static int32_t object_contact_q15(int32_t value)
{
    /* The source adds 0x7fff only for negative values before ASR 15.
     * Division in C truncates toward zero and gives that exact result. */
    return value / INT32_C(0x8000);
}

static int16_t object_contact_clamp(int32_t value)
{
    if (value > INT32_C(0x400)) return INT16_C(0x400);
    if (value < -INT32_C(0x400)) return -INT16_C(0x400);
    return (int16_t)value;
}

int portable_driver_object_contact_escape(
    int16_t position_x, int16_t position_y,
    int16_t object_x, int16_t object_y,
    const int32_t *sine_table, size_t sine_count,
    PortableDriverObjectContactResult *out)
{
    uint16_t angle;
    int32_t index, x, y;

    if (out == NULL || sine_table == NULL || sine_count < 1024u ||
        portable_driver_vector_effect_source_angle(
            object_contact_wrap_s16((int32_t)position_x - object_x),
            object_contact_wrap_s16((int32_t)position_y - object_y),
            &angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    /* 4073C divides the signed angle toward zero, scales by 512, then
     * divides each wrapped signed product toward zero before clamping. */
    index = (object_contact_wrap_s16(angle) / 64) & 1023;
    x = object_contact_q15(object_contact_wrap_s32(
        (int64_t)sine_table[index] * 512));
    y = object_contact_q15(object_contact_wrap_s32(
        -(int64_t)sine_table[(256 - index) & 1023] * 512));
    *out = (PortableDriverObjectContactResult){
        .impact_angle = angle,
        .response_angle = angle,
        .resultant_x = object_contact_clamp(x),
        .resultant_y = object_contact_clamp(y),
    };
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

int portable_driver_object_contact_resultant(
    int16_t anchor_x, int16_t anchor_y,
    int16_t object_x, int16_t object_y,
    int16_t prior_x, int16_t prior_y, uint16_t heading_7a,
    const int32_t *sine_table, size_t sine_count,
    PortableDriverObjectContactResult *out)
{
    uint16_t impact_angle, response_angle;
    int32_t opposite, difference, angle_index, sine, cosine;
    int32_t normal, tangent, half_tangent, rebound, next_x, next_y;

    if (out == NULL || sine_table == NULL || sine_count < 1024u ||
        portable_driver_vector_effect_source_angle(
            object_contact_wrap_s16((int32_t)anchor_x - object_x),
            object_contact_wrap_s16((int32_t)anchor_y - object_y),
            &impact_angle) != PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    /* sub_8040570 chooses a nearby heading only inside its narrow angle
     * windows. Every intermediate subtraction is a source halfword. */
    opposite = (int32_t)heading_7a - INT32_C(0x8000);
    response_angle = impact_angle;
    difference = object_contact_wrap_s16(opposite - impact_angle);
    if (difference >= 0)
    {
        if (difference <= INT32_C(0x800))
            response_angle = (uint16_t)(heading_7a + UINT16_C(0x7800));
        if (object_contact_wrap_s32(
                (int64_t)(uint32_t)(uint16_t)(opposite - response_angle)
                << 16) > INT32_C(0x77ff0000))
            response_angle = (uint16_t)(heading_7a + UINT16_C(0x0800));
    }
    else
    {
        if (difference >= -INT32_C(0x800))
            response_angle = (uint16_t)(heading_7a + UINT16_C(0x8800));
        if (object_contact_wrap_s16(opposite - response_angle) <=
            -INT32_C(0x7800))
            response_angle = (uint16_t)(heading_7a + UINT16_C(0xf800));
    }

    angle_index = (object_contact_wrap_s16(response_angle) / 64) & 1023;
    sine = sine_table[angle_index];
    cosine = sine_table[(256 - angle_index) & 1023];
    normal = object_contact_q15(object_contact_wrap_s32(
        (int64_t)sine * prior_x - (int64_t)cosine * prior_y));
    tangent = object_contact_q15(object_contact_wrap_s32(
        (int64_t)cosine * prior_x + (int64_t)sine * prior_y));
    half_tangent = tangent / 2;
    rebound = -normal / 2;
    if (rebound <= 255) rebound = 256;
    next_x = object_contact_q15(object_contact_wrap_s32(
        (int64_t)sine * rebound + (int64_t)cosine * half_tangent));
    next_y = object_contact_q15(object_contact_wrap_s32(
        (int64_t)sine * half_tangent - (int64_t)cosine * rebound));
    *out = (PortableDriverObjectContactResult){
        .impact_angle = impact_angle,
        .response_angle = response_angle,
        .resultant_x = object_contact_clamp(next_x),
        .resultant_y = object_contact_clamp(next_y),
    };
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

int portable_driver_track_direction_source_angle(
    int32_t position_x_q16, int32_t position_y_q16,
    int32_t checkpoint_x_world, int32_t checkpoint_y_world,
    const uint16_t *angle_lut, size_t angle_lut_count,
    uint16_t *out_angle)
{
    int32_t x;
    int32_t y;
    uint32_t quadrant;
    uint32_t x_byte;
    uint32_t y_byte;
    uint32_t index;
    uint16_t sample;

    if (angle_lut == NULL || out_angle == NULL)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    /* sub_803285C truncates the checkpoint coordinates to bytes and both
     * world coordinates to eighths before halving the signed difference. */
    x = ((int32_t)(uint8_t)(checkpoint_x_world / 8) -
         (position_x_q16 / INT32_C(0x10000)) / 8) / 2;
    y = ((int32_t)(uint8_t)(checkpoint_y_world / 8) -
         (position_y_q16 / INT32_C(0x10000)) / 8) / 2;
    x = (int16_t)x;
    y = (int16_t)y;

    if (x > 0)
        quadrant = y >= 0 ? 1u : 0u;
    else
        quadrant = y > 0 ? 2u : 3u;
    if (x == 0)
    {
        *out_angle = y > 0 ? UINT16_C(0x8000) : UINT16_C(0x0000);
        return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
    }
    if (y == 0)
    {
        *out_angle = x > 0 ? UINT16_C(0x4000) : UINT16_C(0xc000);
        return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
    }

    x_byte = (uint8_t)(x < 0 ? -x : x);
    y_byte = (uint8_t)(y < 0 ? -y : y);
    index = x_byte + (y_byte << 7u) - UINT32_C(0x81);
    if (index >= angle_lut_count)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;
    sample = angle_lut[index];
    if (quadrant == 0u)
        *out_angle = sample;
    else if (quadrant == 1u)
        *out_angle = (uint16_t)(UINT16_C(0x8000) - sample);
    else if (quadrant == 2u)
        *out_angle = (uint16_t)(sample + UINT16_C(0x8000));
    else
        *out_angle = (uint16_t)(UINT32_C(0x10000) - sample);
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

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
    PortableDriverNormalCollisionResult *out)
{
    const int32_t magnitude = source_vector_magnitude(resultant_x, resultant_y);

    if (out == NULL)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;

    *out = (PortableDriverNormalCollisionResult){
        .source_flags = source_flags & UINT32_C(0xffffefff),
        .angle_history_7a = prior_angle_history_7a,
        .angle_delta_80 = prior_angle_delta_80,
        .response_angle_7a = prior_angle_history_7a,
        .base_heading_7c = current_angle,
        .resistance_92 = source_asr_one_s16(prior_resistance_92),
        .magnitude_state_bc =
            (int32_t)((uint32_t)magnitude << 16),
        .timer_value_84 = prior_timer_value_84,
        .response_timer_7e = prior_response_timer_7e,
    };
    if (out->resistance_92 <= INT16_C(0x00ff))
        out->resistance_92 = INT16_C(0x0100);

    if (resultant_x == 0 && resultant_y == 0)
    {
        out->angle_history_7a = current_angle;
        out->angle_delta_80 = 0;
        out->response_angle_7a = current_angle;
        out->base_heading_7c = current_angle;
        return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
    }

    if (prior_magnitude_state_bc >= 0)
    {
        out->angle_delta_80 = (uint16_t)(impact_angle - current_angle);
        out->response_angle_7a = impact_angle;
        out->base_heading_7c = current_angle;
        out->timer_value_84 = UINT8_C(0x0b);
        out->response_timer_7e = UINT8_C(0x0b);
        out->response_applied = 1;
    }
    return PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}

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
    PortableDriverNormalCollisionResult *out)
{
    uint16_t impact_angle;

    if (out == NULL)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;
    if (portable_driver_vector_effect_source_angle(
            resultant_x, resultant_y, &impact_angle) !=
        PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return PORTABLE_DRIVER_VECTOR_EFFECT_INVALID_ARGUMENT;
    return portable_driver_vector_effect_apply_normal_collision(
        resultant_x, resultant_y, impact_angle, current_angle, source_flags,
        prior_angle_history_7a, prior_angle_delta_80, prior_resistance_92,
        prior_magnitude_state_bc, prior_timer_value_84,
        prior_response_timer_7e, out);
}

int portable_driver_recovery_target(
    const PortableRacePoint *route, size_t route_count, uint8_t checkpoint,
    const PortableTrackRaster *raster,
    const uint16_t *angle_lut, size_t angle_lut_count,
    PortableDriverRecoveryTarget *out)
{
    size_t index = checkpoint;
    uint8_t cell;
    uint16_t heading;
    if (route == NULL || route_count < 2u || raster == NULL || out == NULL)
        return 0;
    if (index == route_count - 1u)
        --index;
    if (index >= route_count)
        return 0;
    const PortableRacePoint target = route[index];
    if (target.x < 0 || target.x > INT16_MAX ||
        target.y < 0 || target.y > INT16_MAX ||
        portable_track_raster_query16(raster, target.x, target.y, &cell) !=
            PORTABLE_TRACK_RASTER_OK || (cell & 127u) >= route_count ||
        portable_driver_track_direction_source_angle(
            target.x * INT32_C(65536), target.y * INT32_C(65536),
            route[cell & 127u].x, route[cell & 127u].y,
            angle_lut, angle_lut_count, &heading) !=
                PORTABLE_DRIVER_VECTOR_EFFECT_OK)
        return 0;
    *out = (PortableDriverRecoveryTarget){index, target, heading};
    return 1;
}

int portable_driver_course_heading(int32_t position_x_q16, int32_t position_y_q16,
    const PortableRacePoint *route, size_t route_count,
    const PortableTrackRaster *raster,
    const uint16_t *angle_lut, size_t angle_lut_count, uint16_t *out_heading)
{
    uint8_t cell;
    if (route == NULL || raster == NULL || out_heading == NULL ||
        portable_track_raster_query16(raster, position_x_q16 / INT32_C(65536),
            position_y_q16 / INT32_C(65536), &cell) != PORTABLE_TRACK_RASTER_OK ||
        (cell & 127u) >= route_count)
        return 0;
    return portable_driver_track_direction_source_angle(position_x_q16, position_y_q16,
        route[cell & 127u].x, route[cell & 127u].y, angle_lut, angle_lut_count,
        out_heading) == PORTABLE_DRIVER_VECTOR_EFFECT_OK;
}
