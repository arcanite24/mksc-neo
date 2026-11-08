#include "road_palette_asset.h"

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

PortableRoadPaletteStatus portable_road_palette_load_fixed(
    const uint8_t *rom,
    size_t rom_size,
    uint32_t source_offset,
    uint16_t *destination,
    size_t destination_capacity)
{
    size_t index;

    if (rom == NULL || destination == NULL)
        return PORTABLE_ROAD_PALETTE_INVALID_ARGUMENT;
    if (destination_capacity < PORTABLE_ROAD_PALETTE_FIXED_COUNT)
        return PORTABLE_ROAD_PALETTE_CAPACITY;
    if ((size_t)source_offset > rom_size ||
        PORTABLE_ROAD_PALETTE_FIXED_BYTES > rom_size - source_offset)
        return PORTABLE_ROAD_PALETTE_OUT_OF_BOUNDS;
    for (index = 0; index < PORTABLE_ROAD_PALETTE_FIXED_COUNT; ++index)
        destination[index] = read_le16(
            rom + source_offset + index * sizeof(uint16_t));
    return PORTABLE_ROAD_PALETTE_OK;
}
