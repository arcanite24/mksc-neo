#include "source_b550_tables.h"

#include <stdint.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static int16_t signed16_from_bits(uint16_t bits)
{
    if (bits < UINT16_C(0x8000))
        return (int16_t)bits;
    return (int16_t)(-((int32_t)(UINT16_C(0x10000) - bits)));
}

static int source_range_valid(size_t rom_size, size_t offset, size_t byte_count)
{
    return offset <= rom_size && byte_count <= rom_size - offset;
}

int portable_source_b550_tables_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableSourceB550TableBuffers *buffers,
    PortableSourceB550TableView *view)
{
    size_t index;

    if (rom == NULL || buffers == NULL || view == NULL ||
        buffers->x_components == NULL || buffers->y_components == NULL)
        return PORTABLE_SOURCE_B550_TABLES_INVALID_ARGUMENT;
    if (buffers->x_capacity < PORTABLE_SOURCE_B550_COUNT ||
        buffers->y_capacity < PORTABLE_SOURCE_B550_COUNT)
        return PORTABLE_SOURCE_B550_TABLES_CAPACITY;
    if (!source_range_valid(rom_size, PORTABLE_SOURCE_B550_X_OFFSET,
                            PORTABLE_SOURCE_B550_BYTES) ||
        !source_range_valid(rom_size, PORTABLE_SOURCE_B550_Y_OFFSET,
                            PORTABLE_SOURCE_B550_BYTES))
        return PORTABLE_SOURCE_B550_TABLES_OUT_OF_BOUNDS;

    for (index = 0; index < PORTABLE_SOURCE_B550_COUNT; ++index)
    {
        buffers->x_components[index] = signed16_from_bits(read_le16(
            rom + PORTABLE_SOURCE_B550_X_OFFSET + index * 2u));
        buffers->y_components[index] = signed16_from_bits(read_le16(
            rom + PORTABLE_SOURCE_B550_Y_OFFSET + index * 2u));
    }

    view->x_components = buffers->x_components;
    view->x_count = PORTABLE_SOURCE_B550_COUNT;
    view->y_components = buffers->y_components;
    view->y_count = PORTABLE_SOURCE_B550_COUNT;
    return PORTABLE_SOURCE_B550_TABLES_OK;
}
