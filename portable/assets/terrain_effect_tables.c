#include "terrain_effect_tables.h"

#include <stdint.h>

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int32_t signed32_from_bits(uint32_t bits)
{
    if (bits < UINT32_C(0x80000000))
        return (int32_t)bits;
    return (int32_t)(-((int64_t)(UINT64_C(0x100000000) - bits)));
}

static int source_range_valid(size_t rom_size,
                              size_t offset,
                              size_t byte_count)
{
    return offset <= rom_size && byte_count <= rom_size - offset;
}

int portable_terrain_effect_tables_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableTerrainEffectTableBuffers *buffers,
    PortableTerrainEffectTableView *view)
{
    size_t index;

    if (rom == NULL || buffers == NULL || view == NULL ||
        buffers->x_offsets == NULL || buffers->y_offsets == NULL)
        return PORTABLE_TERRAIN_EFFECT_TABLES_INVALID_ARGUMENT;
    if (buffers->x_capacity < PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT ||
        buffers->y_capacity < PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT)
        return PORTABLE_TERRAIN_EFFECT_TABLES_CAPACITY;
    if (!source_range_valid(rom_size, PORTABLE_TERRAIN_EFFECT_X_OFFSET,
                            PORTABLE_TERRAIN_EFFECT_OFFSET_BYTES) ||
        !source_range_valid(rom_size, PORTABLE_TERRAIN_EFFECT_Y_OFFSET,
                            PORTABLE_TERRAIN_EFFECT_OFFSET_BYTES))
        return PORTABLE_TERRAIN_EFFECT_TABLES_OUT_OF_BOUNDS;

    for (index = 0; index < PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT; ++index)
    {
        buffers->x_offsets[index] = signed32_from_bits(read_le32(
            rom + PORTABLE_TERRAIN_EFFECT_X_OFFSET + index * 4u));
        buffers->y_offsets[index] = signed32_from_bits(read_le32(
            rom + PORTABLE_TERRAIN_EFFECT_Y_OFFSET + index * 4u));
    }

    view->x_offsets = buffers->x_offsets;
    view->x_count = PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT;
    view->y_offsets = buffers->y_offsets;
    view->y_count = PORTABLE_TERRAIN_EFFECT_OFFSET_COUNT;
    return PORTABLE_TERRAIN_EFFECT_TABLES_OK;
}
