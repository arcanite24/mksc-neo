#ifndef PORTABLE_SOURCE_RANDOM_H
#define PORTABLE_SOURCE_RANDOM_H
#include <stdint.h>

/* main_getRandom and 43840 use the same arithmetic with separate owners. */
static inline uint32_t portable_source_random(uint32_t *state)
{
    if(!state)return 0u;
    *state=UINT32_C(1103515245)*(*state)+UINT32_C(12345);
    return (*state<<1u)>>17u;
}
#endif
