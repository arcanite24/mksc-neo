#include "driver_b550_resultant.h"

#include <stdint.h>

static int32_t arithmetic_shift(int32_t value, unsigned int shift)
{
    const int64_t divisor = INT64_C(1) << shift;

    if (value >= 0)
        return value / (int32_t)divisor;
    return (int32_t)(-((-((int64_t)value) + divisor - 1) / divisor));
}

static int16_t source_halfword(int32_t value)
{
    const uint16_t bits = (uint16_t)(uint32_t)value;

    if (bits < UINT16_C(0x8000))
        return (int16_t)bits;
    return (int16_t)(-((int32_t)(UINT16_C(0x10000) - bits)));
}

static int32_t source_abs16(int16_t value)
{
    const int32_t widened = value;
    /* The source narrows the absolute value before a signed comparison. */
    return source_halfword(widened < 0 ? -widened : widened);
}

static int16_t quarter_component(int16_t value)
{
    int32_t widened = value;

    /* sub_803B550 adds three before the ARM arithmetic >>2 for negatives. */
    if (widened < 0)
        widened += 3;
    return source_halfword(arithmetic_shift(widened, 2));
}

static int16_t table_component(int16_t value, int16_t factor)
{
    int32_t product = (int32_t)value * factor;

    /* The source adds 0xff before its signed arithmetic >>8. */
    if (product < 0)
        product += 0xff;
    return source_halfword(arithmetic_shift(product, 8));
}

static int16_t clamp_component(int16_t value)
{
    if (value < 0)
        return (int16_t)-0x100;
    if (value > 0)
        return (int16_t)0x100;
    return 0;
}

int portable_driver_b550_normal_resultant(
    int16_t component_x,
    int16_t component_y,
    int32_t speed_q16,
    uint16_t source_direction,
    const int16_t *x_table,
    size_t x_count,
    const int16_t *y_table,
    size_t y_count,
    int16_t *out_x,
    int16_t *out_y,
    PortableDriverB550Resultant *out)
{
    const int32_t abs_x = source_abs16(component_x);
    const int32_t abs_y = source_abs16(component_y);
    const int32_t maximum = abs_x > abs_y ? abs_x : abs_y;

    if (out_x == NULL || out_y == NULL || out == NULL)
        return PORTABLE_DRIVER_B550_RESULTANT_INVALID_ARGUMENT;
    if (maximum > 0xbf && speed_q16 <= INT32_C(0x04ffffff))
    {
        if (x_table == NULL || y_table == NULL ||
            source_direction >= x_count || source_direction >= y_count)
            return PORTABLE_DRIVER_B550_RESULTANT_DIRECTION_OUT_OF_RANGE;
        *out_x = table_component(component_x, x_table[source_direction]);
        *out_y = table_component(component_y, y_table[source_direction]);
        *out = (PortableDriverB550Resultant){
            .branch = PORTABLE_DRIVER_B550_BRANCH_TABLE,
            .direction_index = (uint8_t)source_direction,
        };
        return PORTABLE_DRIVER_B550_RESULTANT_OK;
    }
    if (maximum > 0xbf)
    {
        *out_x = quarter_component(component_x);
        *out_y = quarter_component(component_y);
        *out = (PortableDriverB550Resultant){
            .branch = PORTABLE_DRIVER_B550_BRANCH_QUARTER,
            .direction_index = 0,
        };
        return PORTABLE_DRIVER_B550_RESULTANT_OK;
    }

    *out_x = clamp_component(component_x);
    *out_y = clamp_component(component_y);
    *out = (PortableDriverB550Resultant){
        .branch = PORTABLE_DRIVER_B550_BRANCH_CLAMP,
        .direction_index = 0,
    };
    return PORTABLE_DRIVER_B550_RESULTANT_OK;
}

int portable_driver_b550_finish_flags(
    uint16_t next_scheduler_state,
    uint8_t normal_scheduled,
    PortableDriverB550Flags *state)
{
    if (state == NULL || normal_scheduled > 1u)
        return PORTABLE_DRIVER_B550_RESULTANT_INVALID_ARGUMENT;

    if (normal_scheduled)
        state->flags |= UINT32_C(0x1000);

    /* 0803B770: an armed scheduler holds both countdowns. A timer that
     * decrements from one to zero still suppresses motion on this tick. */
    if ((next_scheduler_state & UINT16_C(0x8000)) != 0)
        state->flags |= UINT32_C(0x4000);
    else if (state->timer_38 != 0)
    {
        --state->timer_38;
        state->flags |= UINT32_C(0x4000);
    }
    else if (state->timer_62 != 0)
    {
        --state->timer_62;
        state->flags |= UINT32_C(0x4000);
    }
    else
        state->flags &= ~UINT32_C(0x4000);

    return PORTABLE_DRIVER_B550_RESULTANT_OK;
}
