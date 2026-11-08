#include "track_y.h"

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

uint32_t portable_track_read_y(const PortableRaceView *race,
                               uint32_t index,
                               uint8_t row)
{
    const uint8_t *track_base =
        race->track_data + read_le32(race->track_data + 0xCC);
    const uint16_t table_offset = read_le16(track_base + 3);
    const uint32_t masked_index = index & UINT32_C(0x7F);
    const uint32_t record_index =
        (uint32_t)race->nr_checkpoints * (uint32_t)row + masked_index;
    const uint8_t *record = track_base + table_offset + (record_index << 3);

    return (uint32_t)read_le16(record + 2);
}
