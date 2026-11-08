#ifndef PORTABLE_GP_ROSTER_MATH_H
#define PORTABLE_GP_ROSTER_MATH_H
#include <stdint.h>

static inline int32_t fixed_to_int(int32_t value)
{
    /* The source bias before ASR truncates negative fractions toward zero. */
    return value / INT32_C(65536);
}
#endif
