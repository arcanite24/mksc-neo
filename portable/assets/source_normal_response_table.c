#include "source_normal_response_table.h"

#include <stdint.h>

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static int source_range_valid(size_t rom_size,
                              size_t offset,
                              size_t byte_count)
{
    return offset <= rom_size && byte_count <= rom_size - offset;
}

int portable_source_normal_response_table_build(
    const uint8_t *rom,
    size_t rom_size,
    const PortableSourceNormalResponseTableBuffers *buffers,
    PortableSourceNormalResponseTableView *view)
{
    size_t index;

    if (rom == NULL || buffers == NULL || view == NULL ||
        buffers->entries == NULL)
        return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_INVALID_ARGUMENT;
    if (buffers->capacity < PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT)
        return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_CAPACITY;
    if (!source_range_valid(rom_size, PORTABLE_SOURCE_NORMAL_RESPONSE_OFFSET,
                            PORTABLE_SOURCE_NORMAL_RESPONSE_BYTES))
        return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OUT_OF_BOUNDS;

    for (index = 0; index < PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT; ++index)
    {
        buffers->entries[index] = (int32_t)read_le32(
            rom + PORTABLE_SOURCE_NORMAL_RESPONSE_OFFSET + index * 4u);
    }
    view->entries = buffers->entries;
    view->count = PORTABLE_SOURCE_NORMAL_RESPONSE_COUNT;
    return PORTABLE_SOURCE_NORMAL_RESPONSE_TABLES_OK;
}
