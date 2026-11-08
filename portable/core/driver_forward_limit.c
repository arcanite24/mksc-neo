#include "driver_forward_limit.h"

#include <stdint.h>

static int32_t signed32_from_bits(uint32_t bits)
{
    if(bits<UINT32_C(0x80000000))return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000)-bits)));
}

static int32_t arithmetic_high_half(int32_t value)
{
    if(value>=0)return value/65536;
    return (int32_t)(-((-(int64_t)value+65535)/65536));
}

int portable_driver_forward_limit(int16_t threshold, int32_t speed_q16,
    int32_t *acceleration_q16, const int16_t *table, size_t count)
{
    if(!acceleration_q16||!table||count<8)
        return PORTABLE_DRIVER_FORWARD_LIMIT_INVALID;
    if(threshold<0||threshold>=arithmetic_high_half(speed_q16))
        return PORTABLE_DRIVER_FORWARD_LIMIT_FALLBACK;
    uint32_t index=((uint32_t)speed_q16>>24)&7u;
    uint32_t bits=((uint32_t)(uint16_t)table[index]<<16)|
                  ((uint32_t)*acceleration_q16&UINT32_C(0xffff));
    *acceleration_q16=signed32_from_bits(bits);
    return PORTABLE_DRIVER_FORWARD_LIMIT_HANDLED;
}

int portable_driver_forward_limit_select(int32_t source_value,
    uint8_t *selector)
{
    if(!selector)return PORTABLE_DRIVER_FORWARD_LIMIT_INVALID;
    if(source_value<0)return PORTABLE_DRIVER_FORWARD_LIMIT_FALLBACK;
    *selector=(uint8_t)(((uint32_t)source_value>>25)&15u);
    return PORTABLE_DRIVER_FORWARD_LIMIT_HANDLED;
}
