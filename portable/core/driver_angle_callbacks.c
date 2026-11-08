#include "driver_angle_callbacks.h"

#include <stddef.h>
#include <stdint.h>

int portable_driver_angle_limit_callback(
    uint16_t limit_b6,
    uint16_t source_flags_b4,
    uint16_t position_82,
    PortableDriverAngleLimitResult *out)
{
    int32_t limit;
    uint16_t applied_limit;
    int32_t amount;
    int32_t position;
    int32_t adjusted;

    if (out == NULL)
        return PORTABLE_DRIVER_ANGLE_CALLBACK_INVALID_ARGUMENT;

    *out = (PortableDriverAngleLimitResult){
        .source_flags_b4 = source_flags_b4,
        .next_flags_b4 = source_flags_b4,
        .limit_b6 = limit_b6,
        .next_limit_b6 = limit_b6,
        .position_82 = position_82,
        .next_position_82 = position_82,
        .next_angle_delta_80 = 0,
        .next_timer_84 = 0,
        .next_response_status_7e = 0,
    };

    limit = (int16_t)limit_b6;
    applied_limit = limit_b6;
    if (limit > INT32_C(0x1000))
        applied_limit = UINT16_C(0x1000);
    amount = (int16_t)applied_limit;
    if ((source_flags_b4 & UINT16_C(0x0100)) == 0)
        amount = -amount;
    position = (int16_t)position_82 + amount;
    out->amount = amount;
    out->position_after_amount = position;
    out->next_position_82 = (uint16_t)position;

    if ((int16_t)limit_b6 > INT16_C(0x00ff))
    {
        out->branch = PORTABLE_DRIVER_ANGLE_LIMIT_DECREMENT;
        out->next_limit_b6 = (uint16_t)(limit_b6 - UINT16_C(0x0040));
        return PORTABLE_DRIVER_ANGLE_CALLBACK_OK;
    }

    if (position >= 0)
    {
        adjusted = position - INT32_C(0x480);
        out->next_position_82 = (uint16_t)adjusted;
        if (adjusted >= 0)
        {
            out->branch = PORTABLE_DRIVER_ANGLE_LIMIT_POSITION_UPDATE;
            return PORTABLE_DRIVER_ANGLE_CALLBACK_OK;
        }
    }
    else
    {
        adjusted = position + INT32_C(0x480);
        out->next_position_82 = (uint16_t)adjusted;
        if (adjusted < 0)
        {
            out->branch = PORTABLE_DRIVER_ANGLE_LIMIT_POSITION_UPDATE;
            return PORTABLE_DRIVER_ANGLE_CALLBACK_OK;
        }
    }

    out->branch = PORTABLE_DRIVER_ANGLE_LIMIT_BOUNDARY_RESET;
    out->next_position_82 = 0;
    out->next_angle_delta_80 = 0;
    out->next_timer_84 = 0;
    out->next_response_status_7e = 0;
    out->next_flags_b4 =
        (uint16_t)((source_flags_b4 & UINT16_C(0x40ff)) |
                   UINT16_C(0x0400));
    return PORTABLE_DRIVER_ANGLE_CALLBACK_OK;
}

int portable_driver_angle_target_callback(
    uint16_t target_angle_ca,
    uint16_t current_angle_7a,
    uint8_t table_nibble_9a,
    const uint8_t *table,
    size_t table_count,
    PortableDriverAngleTargetCallbackResult *out)
{
    int32_t difference;
    uint8_t index;
    uint8_t table_value;
    int32_t result;

    if (out == NULL)
        return PORTABLE_DRIVER_ANGLE_CALLBACK_INVALID_ARGUMENT;

    *out = (PortableDriverAngleTargetCallbackResult){
        .target_angle_ca = target_angle_ca,
        .current_angle_7a = current_angle_7a,
        .table_nibble_9a = table_nibble_9a,
        .difference = (int16_t)(uint16_t)(target_angle_ca -
                                          current_angle_7a),
        .next_angle_7a = target_angle_ca,
        .next_angle_56 = target_angle_ca,
    };
    difference = out->difference;

    if ((difference >= 0 && difference <= INT32_C(0x1ff)) ||
        (difference < 0 && difference >= -INT32_C(0x200)))
    {
        out->branch = PORTABLE_DRIVER_ANGLE_TARGET_SNAP;
        return PORTABLE_DRIVER_ANGLE_CALLBACK_OK;
    }

    if (table == NULL ||
        table_count < PORTABLE_DRIVER_ANGLE_TARGET_TABLE_BYTES)
        return PORTABLE_DRIVER_ANGLE_CALLBACK_TABLE_TOO_SMALL;

    if (difference > 0)
    {
        if (difference > INT32_C(0x1ff))
            difference = INT32_C(0x1ff);
        index = (uint8_t)(((uint32_t)difference >> 6) & 7u);
        out->branch = PORTABLE_DRIVER_ANGLE_TARGET_TABLE_POSITIVE;
        result = (int32_t)current_angle_7a;
    }
    else
    {
        difference = -difference;
        if (difference > INT32_C(0x1ff))
            difference = INT32_C(0x1ff);
        index = (uint8_t)(((uint32_t)difference >> 6) & 7u);
        out->branch = PORTABLE_DRIVER_ANGLE_TARGET_TABLE_NEGATIVE;
        result = (int32_t)current_angle_7a;
    }
    index = (uint8_t)(index + ((table_nibble_9a & 0x0fu) << 3));
    table_value = table[PORTABLE_DRIVER_ANGLE_TARGET_TABLE_OFFSET + index];
    out->table_index = index;
    out->table_value = table_value;
    if (out->branch == PORTABLE_DRIVER_ANGLE_TARGET_TABLE_POSITIVE)
        result += (int32_t)table_value << 4;
    else
        result -= (int32_t)table_value << 4;
    out->next_angle_7a = (uint16_t)result;
    out->next_angle_56 = (uint16_t)result;
    return PORTABLE_DRIVER_ANGLE_CALLBACK_OK;
}
