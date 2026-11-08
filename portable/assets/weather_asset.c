#include "weather_asset.h"

#include <string.h>

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

int portable_weather_asset_open(const PortableAssetSectionView *section,
                                PortableWeatherAssetView *out)
{
    if (!section || !out || !section->bytes || section->version != 1u || section->size < 48u ||
        section->size > 48u + 3u * (32u + 64u + 256u))
        return 0;
    const uint8_t *b = section->bytes;
    if (u32(b) != UINT32_C(0x31485457) || u32(b + 4u) != 1u || b[8] > 1u ||
        (b[8] ? (b[9] == 0u || b[9] > 12u) : b[9] != 0u) || b[10] || b[11] || u32(b + 40u) ||
        u32(b + 44u))
        return 0;
    PortableWeatherAssetView candidate = {0};
    candidate.rules.enabled = b[8];
    candidate.rules.count = b[9];
    for (unsigned i = 0u; i < 8u; ++i)
    {
        if (b[12u + i] > 3u || b[20u + i] > 3u)
            return 0;
        candidate.rules.backward[i] = b[12u + i];
        candidate.rules.forward[i] = b[20u + i];
    }
    size_t offset = 48u;
    for (unsigned i = 0u; i < 3u; ++i)
    {
        const uint32_t size = u32(b + 28u + i * 4u);
        if (!candidate.rules.enabled)
        {
            if (size)
                return 0;
            continue;
        }
        if (size > section->size - offset ||
            !portable_native_image_open(b + offset, size, &candidate.images[i]))
            return 0;
        const PortableNativeImage *image = &candidate.images[i];
        if (!image->palette || image->width != 8u || image->height != 8u || image->anchor_x < -8 ||
            image->anchor_x > 8 || image->anchor_y < -8 || image->anchor_y > 8)
            return 0;
        offset += size;
    }
    if (offset != section->size || !portable_snow_weather_rules_valid(&candidate.rules))
        return 0;
    *out = candidate;
    return 1;
}

int portable_weather_asset_load(const PortableAssetPackView *pack, uint32_t course_id,
                                PortableWeatherAssetView *storage,
                                const PortableWeatherAssetView **out)
{
    if (!pack || !storage || !out)
        return 0;
    *out = NULL;
    PortableAssetSectionView section;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_WEATHER_PROFILE, course_id, &section);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
        return 1;
    if (status != PORTABLE_ASSET_PACK_OK || !portable_weather_asset_open(&section, storage))
        return 0;
    *out = storage;
    return 1;
}
