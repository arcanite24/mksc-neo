#include "modern_drift_asset.h"
#include <limits.h>

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

int portable_modern_drift_asset_open(const PortableAssetSectionView *section,
                                   PortableModernDriftRules *out)
{
    if (section == NULL || out == NULL || section->bytes == NULL ||
        section->kind != PORTABLE_ASSET_SECTION_MODERN_DRIFT_RULES ||
        section->content_id != 1u ||
        !((section->version == 1u && section->size == 48u) ||
          (section->version == 2u && section->size == 64u)))
        return 0;
    const uint8_t *bytes = section->bytes;
    if (read_u32(bytes) != UINT32_C(0x3152444d) || read_u32(bytes + 44u) != 0u)
        return 0;
    PortableModernDriftRules rules = {0};
    rules.profile_id = read_u32(bytes + 4u);
    rules.revision = read_u32(bytes + 8u);
    for (unsigned tier = 0u; tier < 3u; ++tier)
    {
        rules.charge_ticks[tier] = read_u16(bytes + 12u + tier * 2u);
        rules.boost_ticks[tier] = read_u16(bytes + 18u + tier * 2u);
        rules.cap_ratio_256[tier] = read_u16(bytes + 24u + tier * 2u);
        const uint32_t acceleration = read_u32(bytes + 32u + tier * 4u);
        /* Negative wire values are invalid. Check before signed conversion. */
        if (acceleration > INT32_MAX)
            return 0;
        rules.acceleration_q16[tier] = (int32_t)acceleration;
    }
    rules.minimum_speed_ratio_256 = read_u16(bytes + 30u);
    if (section->version == 2u)
    {
        rules.air.minimum_air_ticks = read_u16(bytes + 48u);
        rules.air.trick_window_ticks = read_u16(bytes + 50u);
        rules.air.boost_ticks = read_u16(bytes + 52u);
        rules.air.cap_ratio_256 = read_u16(bytes + 54u);
        const uint32_t acceleration = read_u32(bytes + 56u);
        if (acceleration > INT32_MAX || bytes[63u] != 0u || bytes[62u] == 0u)
            return 0;
        rules.air.acceleration_q16 = (int32_t)acceleration;
        rules.air.minimum_speed_ratio_256 = read_u16(bytes + 60u);
        rules.air.launch_mask = bytes[62u];
        if (!portable_modern_air_rules_valid(&rules.air))
            return 0;
    }
    if (!portable_modern_drift_rules_valid(&rules))
        return 0;
    *out = rules;
    return 1;
}
