#ifndef MKSC_PORTABLE_TERRAIN_QUERY_H
#define MKSC_PORTABLE_TERRAIN_QUERY_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_TERRAIN_QUERY_OK = 0,
    PORTABLE_TERRAIN_QUERY_INVALID_ARGUMENT = 1,
    PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS = 2,
};

typedef struct
{
    const uint8_t *cells;
    size_t row_stride;
    size_t width;
    size_t height;
} PortableTerrainView;

/* Bounded native view of sub_8032CA0's separate 8-unit terrain table. */
int portable_terrain_query8(const PortableTerrainView *terrain,
                            int32_t x,
                            int32_t y,
                            uint8_t *out_value);

#endif
