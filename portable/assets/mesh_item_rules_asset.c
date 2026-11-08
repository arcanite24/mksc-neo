#include "mesh_item_rules_asset.h"
#include <limits.h>
static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u | (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}
int portable_mesh_item_rules_asset_open(const PortableAssetSectionView *section,
                                        PortableMeshItemRules *out)
{
    if (!section || !out || section->kind != PORTABLE_MESH_ITEM_RULES_KIND ||
        section->version != 1u || !section->bytes ||
        section->size != PORTABLE_MESH_ITEM_RULES_BYTES)
        return 0;
    const uint8_t *bytes = section->bytes;
    if (word(bytes) != UINT32_C(0x3152494d) || word(bytes + 4u) != 1u ||
        word(bytes + 8u) != PORTABLE_MESH_ITEM_RULES_BYTES ||
        word(bytes + 12u) != section->content_id ||
        word(bytes + 20u) != PORTABLE_MESH_ITEM_SCALAR_WORDS || word(bytes + 24u) != 8u ||
        word(bytes + 28u) != 16u)
        return 0;
    PortableMeshItemRules next = {0};
    next.profile_id = word(bytes + 12u);
    next.revision = word(bytes + 16u);
    size_t at = 32u;
#define MESH_ITEM_READ(name, minimum, maximum)                                                     \
    {                                                                                              \
        uint32_t value = word(bytes + at);                                                         \
        at += 4u;                                                                                  \
        if (value > INT32_MAX)                                                                     \
            return 0;                                                                              \
        next.name = (int32_t)value;                                                                \
    }
    PORTABLE_MESH_ITEM_FIELDS(MESH_ITEM_READ)
#undef MESH_ITEM_READ
    for (unsigned i = 0; i < 10u; ++i)
    {
        const uint32_t value = word(bytes + at);
        at += 4u;
        if (value > UINT16_MAX)
            return 0;
        next.roulette.preview[i] = (uint16_t)value;
    }
    for (unsigned i = 0; i < 27u; ++i)
    {
        const uint32_t value = word(bytes + at);
        at += 4u;
        if (value > UINT16_MAX)
            return 0;
        next.roulette.timeline[i] = (uint16_t)value;
    }
    for (unsigned domain = 0; domain < 2u; ++domain)
        for (unsigned rank = 0; rank < 8u; ++rank)
            for (unsigned i = 0; i < 16u; ++i)
            {
                PortableGpItemWeight *row =
                    domain ? next.cpu_weights[rank] : next.player_weights[rank];
                row[i].item = (uint16_t)((uint32_t)bytes[at] | (uint32_t)bytes[at + 1u] << 8u);
                row[i].weight =
                    (uint16_t)((uint32_t)bytes[at + 2u] | (uint32_t)bytes[at + 3u] << 8u);
                at += 4u;
            }
    if (!portable_mesh_item_rules_valid(&next))
        return 0;
    *out = next;
    return 1;
}
int portable_mesh_item_rules_asset_load(const PortableAssetPackView *pack, uint32_t id,
                                        PortableMeshItemRules *out)
{
    PortableAssetSectionView section;
    return pack && out && id &&
           portable_asset_pack_find_section(pack, PORTABLE_MESH_ITEM_RULES_KIND, id, &section) ==
               PORTABLE_ASSET_PACK_OK &&
           portable_mesh_item_rules_asset_open(&section, out);
}
