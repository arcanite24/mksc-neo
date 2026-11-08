#ifndef PORTABLE_DRIVER_FORWARD_LIMIT_H
#define PORTABLE_DRIVER_FORWARD_LIMIT_H

#include <stddef.h>
#include <stdint.h>

enum {
    PORTABLE_DRIVER_FORWARD_LIMIT_FALLBACK = 0,
    PORTABLE_DRIVER_FORWARD_LIMIT_HANDLED = 1,
    PORTABLE_DRIVER_FORWARD_LIMIT_INVALID = 2,
};

int portable_driver_forward_limit(int16_t threshold, int32_t speed_q16,
    int32_t *acceleration_q16, const int16_t *table, size_t count);
int portable_driver_forward_limit_select(int32_t source_value,
    uint8_t *selector);

#endif
