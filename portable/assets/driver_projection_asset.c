#include "driver_projection_asset.h"

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Source sprite-cell geometry, normalized independently from hardware attrs. */
int portable_driver_projection_asset_open(const PortableAssetSectionView *section,
                                          PortableDriverProjectionRules *out)
{
    if (!section || !out || !section->bytes ||
        section->kind != PORTABLE_ASSET_SECTION_DRIVER_PROJECTION_RULES || section->version != 1u ||
        section->size != 44u || u32(section->bytes) != UINT32_C(0x31525044) ||
        u32(section->bytes + 4u) != 3u)
        return 0;
    uint16_t previous = 0u;
    for (unsigned index = 0u; index < PORTABLE_DRIVER_PROJECTION_LODS; ++index)
    {
        const uint8_t *row = section->bytes + 8u + index * 12u;
        const uint16_t distance = u16(row), size = u16(row + 2u);
        const int16_t x = (int16_t)u16(row + 4u), y = (int16_t)u16(row + 6u);
        if (distance <= previous || distance > 32767u || size != (64u >> index) || x < -128 ||
            x > 128 || y < -128 || y > 128 || row[8] > 1u || row[9] != 0u || u16(row + 10u) != 0u)
            return 0;
        out->rows[index] = (PortableDriverProjectionRow){distance, size, x, y, row[8]};
        previous = distance;
    }
    return 1;
}
