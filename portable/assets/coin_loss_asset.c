#include "coin_loss_asset.h"

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)p[1] << 8u;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u |
        (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}

const uint8_t *portable_coin_loss_resource_cell(
    const PortableCoinLossResourcesView *resources, uint32_t source_id)
{
    if (resources == NULL || resources->cells == NULL ||
        source_id < UINT32_C(0x0807b410) ||
        source_id > UINT32_C(0x0807b430) || (source_id & 7u) != 0u)
        return NULL;
    return resources->cells + source_id - UINT32_C(0x0807b410);
}

int portable_coin_loss_resources_read(const uint8_t *bytes, size_t size,
                                     PortableCoinLossResourcesView *out)
{
    if (bytes == NULL || out == NULL || size != PORTABLE_COIN_LOSS_RESOURCE_BYTES)
        return 0;
    /* Scripts reference the bounded resident cell bank; appearance and hold
     * times are content, not a second compiled copy of the imported resource. */
    for (unsigned i=0u;i<8u;++i) {
        const uint32_t cell=le32(bytes+i*8u);
        const uint32_t ticks=le32(bytes+i*8u+4u);
        if (cell==0u) {
            if (ticks!=0u) return 0;
        } else if (cell<UINT32_C(0x0807b410) || cell>UINT32_C(0x0807b430) ||
                   (cell&7u)!=0u || ticks==0u || ticks>600u) return 0;
    }
    for (unsigned offset=0u;offset<32u;offset+=8u)
        if(le16(bytes+64u+offset)!=1u)return 0;
    if(le16(bytes+96u)!=0u)return 0;
    *out = (PortableCoinLossResourcesView){bytes,bytes+48u,
                                          bytes+64u,bytes+98u,bytes+354u};
    return 1;
}
