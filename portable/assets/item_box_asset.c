#include "item_box_asset.h"
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)p[1] << 8u;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8u |
           (uint32_t)p[2] << 16u | (uint32_t)p[3] << 24u;
}

const uint8_t *portable_item_box_resource_cell(
    const PortableItemBoxResourcesView *resources, uint32_t source_id)
{
    if (resources == NULL) return NULL;
    for (unsigned i = 0u; i < PORTABLE_ITEM_BOX_CELL_COUNT; ++i)
        if (source_id != 0u && source_id == resources->cell_ids[i]) return resources->cells[i];
    return NULL;
}

int portable_item_box_resources_read(const uint8_t *bytes, size_t size,
                                    PortableItemBoxResourcesView *out)
{
    const size_t directory_start=32u;
    const size_t sequence_start=directory_start+PORTABLE_ITEM_BOX_CELL_COUNT*12u;
    const size_t cell_start=sequence_start+PORTABLE_ITEM_BOX_SEQUENCE_COUNT*8u;
    const size_t shard_bytes=PORTABLE_ITEM_BOX_SHARD_GROUP_COUNT*8u*PORTABLE_ITEM_BOX_SHARD_FRAME_BYTES;
    PortableItemBoxResourcesView next = {0};
    size_t offset=cell_start;
    if(bytes==NULL || out==NULL || size!=PORTABLE_ITEM_BOX_RESOURCE_BYTES ||
       le32(bytes)!=UINT32_C(0x31504249) ||
       le32(bytes+4u)!=PORTABLE_ITEM_BOX_CELL_COUNT ||
       le32(bytes+8u)!=PORTABLE_ITEM_BOX_SEQUENCE_COUNT || le32(bytes+12u)!=8u ||
       le32(bytes+16u)!=PORTABLE_ITEM_BOX_SHARD_GROUP_COUNT || le32(bytes+28u)!=0u ||
       le32(bytes+24u)!=shard_bytes || le32(bytes+20u)!=size-cell_start-shard_bytes)return 0;
    const size_t cell_end=size-shard_bytes;
    for(unsigned i=0u;i<PORTABLE_ITEM_BOX_CELL_COUNT;++i){
        const uint8_t *entry=bytes+directory_start+i*12u;
        const uint32_t id=le32(entry),length=le32(entry+8u);
        if(id==0u || le32(entry+4u)!=offset || offset>cell_end ||
           length<2u || length>cell_end-offset)return 0;
        for(unsigned previous=0u;previous<i;++previous)
            if(next.cell_ids[previous]==id)return 0;
        const uint8_t *cell=bytes+offset;
        const unsigned count=le16(cell);
        if(count==0u || count>3u || length!=2u+6u*count)return 0;
        for(unsigned j=0u;j<count;++j){
            const uint16_t a0=le16(cell+2u+j*6u),a1=le16(cell+4u+j*6u),a2=le16(cell+6u+j*6u);
            const unsigned size_code=a1>>14u,tile=a2&1023u;
            if((a0&0xff00u)!=0u || (a1&0x3e00u)!=0u ||
               (size_code!=1u && size_code!=2u) || (a2&0xfc00u)!=0x6400u ||
               tile<0x160u || tile+(size_code==2u?16u:4u)>0x180u)return 0;
        }
        next.cell_ids[i] = id;
        next.cells[i] = cell;
        offset += length;
    }
    if(offset!=cell_end)return 0;
    next.sequence=bytes+sequence_start;
    for(unsigned i=0u;i<PORTABLE_ITEM_BOX_SEQUENCE_COUNT;++i){
        const uint8_t *entry=next.sequence+i*8u;
        const uint32_t id=le32(entry),duration=le32(entry+4u);
        if(duration==0u || duration>65535u ||
           (i+1u==PORTABLE_ITEM_BOX_SEQUENCE_COUNT ? id!=0u :
            portable_item_box_resource_cell(&next,id)==NULL))return 0;
    }
    next.shard_tiles=bytes+cell_end;
    *out=next;
    return 1;
}

int portable_item_box_asset_open(const PortableAssetSectionView *s, uint8_t *tiles,
                                 size_t tile_capacity, uint16_t *palette, size_t palette_capacity,
                                 PortableItemBoxAssetView *v)
{
    if (!s || !s->bytes ||
        s->size > 24u + PORTABLE_ITEM_BOX_TILE_BYTES + PORTABLE_ITEM_BOX_PALETTE_COUNT * 2u ||
        !tiles || tile_capacity < PORTABLE_ITEM_BOX_TILE_BYTES || !palette ||
        palette_capacity < PORTABLE_ITEM_BOX_PALETTE_COUNT || !v)
        return 0;
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    const uint32_t tn = s->size >= 24u ? le32(b + 4u) : 0u;
    const uint32_t pn = s->size >= 24u ? le32(b + 8u) : 0u;
    const uint32_t fn = s->size >= 24u ? le32(b + 12u) : 0u;
    if (s->version != 1u || s->size < 24u || le32(b) != UINT32_C(0x31584249) ||
        tn != PORTABLE_ITEM_BOX_TILE_BYTES || pn != PORTABLE_ITEM_BOX_PALETTE_COUNT ||
        fn != PORTABLE_ITEM_BOX_FRAME_COUNT || le32(b + 16u) != 32u || le32(b + 20u) != 32u ||
        s->size != 24u + tn + pn * 2u)
        return 0;
    p = b + 24u;
    memcpy(tiles, p, tn);
    p += tn;
    for (size_t i = 0; i < pn; ++i)
        palette[i] = le16(p + i * 2u);
    p += pn * 2u;
    if (p != end)
        return 0;
    *v = (PortableItemBoxAssetView){tiles, tn, palette, pn, fn, 32u, 32u};
    return 1;
}
