#include "terrain_query.h"

#include <stdint.h>

static int32_t arithmetic_shift3(int64_t value)
{
    if (value >= 0)
        return (int32_t)(value / 8);
    return (int32_t)(-((-value + 7) / 8));
}

int portable_terrain_query8(const PortableTerrainView *terrain,
                            int32_t x,
                            int32_t y,
                            uint8_t *out_value)
{
    const int32_t cell_x = arithmetic_shift3((int64_t)x + (x < 0 ? 7 : 0));
    const int32_t cell_y = arithmetic_shift3((int64_t)y + (y < 0 ? 7 : 0));

    if (terrain == NULL || terrain->cells == NULL || out_value == NULL ||
        terrain->width == 0 || terrain->height == 0)
        return PORTABLE_TERRAIN_QUERY_INVALID_ARGUMENT;
    if (cell_x < 0 || cell_y < 0 || (size_t)cell_x >= terrain->width ||
        (size_t)cell_y >= terrain->height ||
        (terrain->row_stride != 0 &&
         (size_t)cell_x >= terrain->row_stride))
        return PORTABLE_TERRAIN_QUERY_OUT_OF_BOUNDS;
    *out_value = terrain->cells[(size_t)cell_y * terrain->row_stride +
                                (size_t)cell_x];
    return PORTABLE_TERRAIN_QUERY_OK;
}
