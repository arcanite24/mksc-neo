#include "recovery_fx_asset.h"
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}
int portable_recovery_fx_asset_open(const PortableAssetSectionView *section,
                                    PortableRecoveryFxRules *out)
{
    if (!section || !out || !section->bytes || section->version != 1u || section->size != 24u ||
        u32(section->bytes) != UINT32_C(0x31584652) || u32(section->bytes + 4u) != 3u ||
        u32(section->bytes + 20u) != 0u)
        return 0;
    PortableRecoveryFxRules rules = {0};
    for (unsigned i = 0u; i < 3u; ++i)
    {
        const uint8_t duration = section->bytes[8u + i];
        if (!duration || duration > 127u)
            return 0;
        rules.freeze_durations[i] = duration;
    }
    for (unsigned i = 0u; i < 9u; ++i)
    {
        const uint8_t raw = section->bytes[11u + i];
        const int value = raw < 128u ? (int)raw : (int)raw - 256;
        if (value < -16 || value > 16)
            return 0;
        rules.drip_dy[i] = (int8_t)value;
    }
    if (!portable_recovery_fx_rules_valid(&rules))
        return 0;
    *out = rules;
    return 1;
}
int portable_recovery_fx_asset_load(const PortableAssetPackView *pack,
                                    PortableRecoveryFxRules *storage,
                                    const PortableRecoveryFxRules **out)
{
    if (!pack || !storage || !out)
        return 0;
    *out = NULL;
    PortableAssetSectionView section;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_RECOVERY_FX_RULES, 1u, &section);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
        return 1;
    if (status != PORTABLE_ASSET_PACK_OK || !portable_recovery_fx_asset_open(&section, storage))
        return 0;
    *out = storage;
    return 1;
}
int portable_recovery_fx_ice_gate(const PortableAssetPackView *pack, uint32_t course, uint8_t *out)
{
    if (!pack || !out)
        return 0;
    *out = 0u;
    PortableAssetSectionView section;
    const PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_SURFACE_SOUND_RULES, course, &section);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
        return 1;
    if (status != PORTABLE_ASSET_PACK_OK || (section.version != 1u && section.version != 2u) ||
        section.size != (section.version == 1u ? 56u : 60u) ||
        u32(section.bytes) != UINT32_C(0x31465253) || section.bytes[4] != section.version ||
        section.bytes[5] != 0u || section.bytes[6] != 16u || section.bytes[7] != 0u)
        return 0;
    if (section.version == 2u)
    {
        if (section.bytes[57] || section.bytes[58] || section.bytes[59])
            return 0;
        *out = (uint8_t)(section.bytes[56] == 8u);
    }
    return 1;
}
