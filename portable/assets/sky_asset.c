#include "sky_asset.h"

#include <string.h>

#define SKY_TILE_BLOCK_BYTES 0x800u
#define SKY_TILE_BLOCK_COUNT 5u
#define SKY_TILE_BYTES (SKY_TILE_BLOCK_BYTES * SKY_TILE_BLOCK_COUNT)
#define SKY_PALETTE_COUNT 63u
#define SKY_FRONT_MAP_BYTES 0x800u
#define SKY_MID_MAP_BYTES 0x1000u
#define SKY_BACK_MAP_BYTES 0x800u
#define SKY_MAP_HEIGHT_TILES 32u

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

PortableSkyAssetStatus portable_sky_asset_open_motion(
    const PortableAssetSectionView *section,PortableSkyMotionRules *storage,
    const PortableSkyMotionRules **out_rules)
{
    if(section==NULL || storage==NULL || out_rules==NULL || section->bytes==NULL)
        return PORTABLE_SKY_ASSET_INVALID_ARGUMENT;
    *out_rules=NULL;
    const uint8_t *data=section->bytes;
    if(section->size<64u || (section->version!=1u && section->version!=2u) ||
       read_le32(data)!=UINT32_C(0x31594b53))return PORTABLE_SKY_ASSET_FORMAT;
    const uint32_t tiles=read_le32(data+4u),colors=read_le32(data+8u);
    const uint32_t front=read_le32(data+12u),mid=read_le32(data+16u),back=read_le32(data+20u);
    const uint32_t extension=read_le32(data+60u);
    if(tiles>0x2800u || colors>64u || front>0x1000u || mid>0x1000u || back>0x1000u)
        return PORTABLE_SKY_ASSET_CAPACITY;
    const size_t base=64u+(size_t)tiles+(size_t)colors*2u+front+mid+back;
    if(base>section->size || extension!=section->size-base ||
       (section->version==1u?extension!=0u:extension!=40u))return PORTABLE_SKY_ASSET_FORMAT;
    if(section->version==1u)return PORTABLE_SKY_ASSET_OK;
    const uint8_t *motion=data+base;
    if(read_le32(motion)!=UINT32_C(0x31524d53) || read_le32(motion+4u)!=1u ||
       motion[8]>7u || motion[9]>7u || motion[10]>1u || motion[11]>16u || motion[12]>16u ||
       motion[13]>15u || motion[14]>15u || motion[15]>15u ||
       motion[30]>2u || motion[31]>15u || motion[34]>2u || motion[35]!=0u ||
       motion[36]>2u || motion[37]>2u || motion[38]>2u || motion[39]>3u)
        return PORTABLE_SKY_ASSET_FORMAT;
    const int16_t split_y=(int16_t)read_le16(motion+28u);
    if(split_y < -1 || split_y>159)return PORTABLE_SKY_ASSET_FORMAT;
    memset(storage,0,sizeof *storage);
    storage->controller=motion[8];storage->layer_mask=motion[9];
    storage->blend_mode = motion[10];
    storage->blend_a = motion[11];
    storage->blend_b = motion[12];
    for(unsigned layer=0u;layer<3u;++layer){
        storage->yaw_shift[layer]=motion[13u+layer];
        storage->x_bias[layer]=(int16_t)read_le16(motion+16u+layer*2u);
        storage->y_bias[layer]=(int16_t)read_le16(motion+22u+layer*2u);
        storage->layer_source[layer]=motion[36u+layer];
    }
    storage->split_y=split_y;
    storage->split_layer=motion[30];storage->split_yaw_shift=motion[31];
    storage->split_x_bias=(int16_t)read_le16(motion+32u);
    storage->lightning_hide_layer=motion[34];
    storage->road_mode=motion[39];
    *out_rules=storage;
    return PORTABLE_SKY_ASSET_OK;
}

static int span_is_valid(size_t data_size, size_t offset, size_t length)
{
    return offset <= data_size && length <= data_size - offset;
}

static PortableSkyAssetStatus decode_lz77(const uint8_t *data,
                                          size_t data_size,
                                          size_t source_offset,
                                          uint8_t *destination,
                                          size_t destination_capacity,
                                          size_t *out_size)
{
    size_t source;
    size_t produced = 0;
    uint32_t expected;

    if (data == NULL || destination == NULL || out_size == NULL ||
        !span_is_valid(data_size, source_offset, 4))
        return PORTABLE_SKY_ASSET_INVALID_ARGUMENT;
    if (data[source_offset] != 0x10)
        return PORTABLE_SKY_ASSET_FORMAT;
    expected = (uint32_t)data[source_offset + 1] |
               ((uint32_t)data[source_offset + 2] << 8) |
               ((uint32_t)data[source_offset + 3] << 16);
    if (expected == 0 || expected > destination_capacity)
        return PORTABLE_SKY_ASSET_CAPACITY;

    source = source_offset + 4;
    while (produced < expected)
    {
        uint8_t flags;
        unsigned bit;

        if (!span_is_valid(data_size, source, 1))
            return PORTABLE_SKY_ASSET_OUT_OF_BOUNDS;
        flags = data[source++];
        for (bit = 0; bit < 8 && produced < expected; ++bit)
        {
            if ((flags & (uint8_t)(0x80u >> bit)) == 0)
            {
                if (!span_is_valid(data_size, source, 1))
                    return PORTABLE_SKY_ASSET_OUT_OF_BOUNDS;
                destination[produced++] = data[source++];
            }
            else
            {
                uint8_t first;
                uint8_t second;
                size_t distance;
                size_t length;
                size_t copy;

                if (!span_is_valid(data_size, source, 2))
                    return PORTABLE_SKY_ASSET_OUT_OF_BOUNDS;
                first = data[source++];
                second = data[source++];
                distance = (((size_t)first & 0x0fu) << 8) |
                           (size_t)second;
                distance += 1u;
                length = ((size_t)first >> 4) + 3u;
                if (distance > produced || length > expected - produced)
                    return PORTABLE_SKY_ASSET_FORMAT;
                for (copy = 0; copy < length; ++copy)
                {
                    destination[produced] =
                        destination[produced - distance];
                    ++produced;
                }
            }
        }
    }
    *out_size = produced;
    return PORTABLE_SKY_ASSET_OK;
}

static PortableSkyAssetStatus decode_map(const uint8_t *data,
                                          size_t data_size,
                                          size_t source_offset,
                                          uint8_t *destination,
                                          size_t destination_capacity,
                                          size_t expected_size)
{
    size_t decoded_size = 0;
    PortableSkyAssetStatus status = decode_lz77(
        data, data_size, source_offset, destination, destination_capacity,
        &decoded_size);

    if (status != PORTABLE_SKY_ASSET_OK)
        return status;
    return decoded_size == expected_size ? PORTABLE_SKY_ASSET_OK
                                         : PORTABLE_SKY_ASSET_FORMAT;
}

PortableSkyAssetStatus portable_sky_asset_build(
    const uint8_t *data,
    size_t data_size,
    uint32_t background_index,
    const PortableSkyAssetBuffers *buffers,
    PortableSkyAssetView *out_view)
{
    size_t table_offset;
    size_t background_offset;
    size_t gfx_offset;
    size_t front_offset;
    size_t mid_offset;
    size_t back_offset;
    size_t palette_offset;
    size_t front_bytes;
    size_t mid_bytes;
    size_t back_bytes;
    size_t block;
    PortableSkyAssetStatus status;

    if (data == NULL || buffers == NULL || out_view == NULL)
        return PORTABLE_SKY_ASSET_INVALID_ARGUMENT;
    if (data_size < 4u || background_index > (data_size - 4u) / 4u)
        return PORTABLE_SKY_ASSET_OUT_OF_BOUNDS;
    table_offset = (size_t)background_index * 4u;
    background_offset = (size_t)read_le32(data + table_offset);
    if (!span_is_valid(data_size, background_offset, 24))
        return PORTABLE_SKY_ASSET_OUT_OF_BOUNDS;
    if (buffers->tiles == NULL || buffers->palette == NULL ||
        buffers->front_map == NULL || buffers->mid_map == NULL ||
        buffers->back_map == NULL || buffers->tile_capacity < SKY_TILE_BYTES ||
        buffers->palette_capacity < SKY_PALETTE_COUNT ||
        buffers->front_capacity < SKY_FRONT_MAP_BYTES ||
        buffers->mid_capacity < SKY_MID_MAP_BYTES ||
        buffers->back_capacity < SKY_BACK_MAP_BYTES)
        return PORTABLE_SKY_ASSET_CAPACITY;

    if ((data[background_offset] != 0x40 &&
         data[background_offset] != 2u) ||
        (data[background_offset + 1] & 3u) != 3u)
        return PORTABLE_SKY_ASSET_UNSUPPORTED;
    front_bytes = SKY_FRONT_MAP_BYTES;
    mid_bytes = data[background_offset] == 2u ? SKY_FRONT_MAP_BYTES
                                              : SKY_MID_MAP_BYTES;
    back_bytes = SKY_BACK_MAP_BYTES;
    gfx_offset = (size_t)read_le32(data + background_offset + 4);
    front_offset = (size_t)read_le32(data + background_offset + 8);
    mid_offset = (size_t)read_le32(data + background_offset + 12);
    back_offset = (size_t)read_le32(data + background_offset + 16);
    palette_offset = (size_t)read_le32(data + background_offset + 20);
    if (gfx_offset > data_size - background_offset ||
        !span_is_valid(data_size, background_offset + gfx_offset, 10) ||
        front_offset > data_size - background_offset ||
        mid_offset > data_size - background_offset ||
        back_offset > data_size - background_offset ||
        palette_offset > data_size - background_offset ||
        !span_is_valid(data_size, background_offset + palette_offset,
                       SKY_PALETTE_COUNT * sizeof(uint16_t)))
        return PORTABLE_SKY_ASSET_OUT_OF_BOUNDS;

    for (block = 0; block < SKY_TILE_BLOCK_COUNT; ++block)
    {
        size_t relative = (size_t)read_le16(
            data + background_offset + gfx_offset + block * 2u);
        size_t source_offset;
        size_t decoded_size = 0;

        if (relative > data_size - background_offset - gfx_offset)
            return PORTABLE_SKY_ASSET_OUT_OF_BOUNDS;
        source_offset = background_offset + gfx_offset + relative;
        status = decode_lz77(data, data_size, source_offset,
                             buffers->tiles + block * SKY_TILE_BLOCK_BYTES,
                             SKY_TILE_BLOCK_BYTES, &decoded_size);
        if (status != PORTABLE_SKY_ASSET_OK)
            return status;
        if (decoded_size != SKY_TILE_BLOCK_BYTES)
            return PORTABLE_SKY_ASSET_FORMAT;
    }

    status = decode_map(data, data_size, background_offset + front_offset,
                        buffers->front_map, buffers->front_capacity,
                        front_bytes);
    if (status != PORTABLE_SKY_ASSET_OK)
        return status;
    status = decode_map(data, data_size, background_offset + mid_offset,
                        buffers->mid_map, buffers->mid_capacity,
                        mid_bytes);
    if (status != PORTABLE_SKY_ASSET_OK)
        return status;
    status = decode_map(data, data_size, background_offset + back_offset,
                        buffers->back_map, buffers->back_capacity,
                        back_bytes);
    if (status != PORTABLE_SKY_ASSET_OK)
        return status;

    for (block = 0; block < SKY_PALETTE_COUNT; ++block)
    {
        buffers->palette[block] = read_le16(
            data + background_offset + palette_offset + block * 2u);
    }

    memset(out_view, 0, sizeof(*out_view));
    out_view->tiles = buffers->tiles;
    out_view->tile_bytes = SKY_TILE_BYTES;
    out_view->palette = buffers->palette;
    out_view->palette_count = SKY_PALETTE_COUNT;
    out_view->front_map = buffers->front_map;
    out_view->front_map_bytes = front_bytes;
    out_view->mid_map = buffers->mid_map;
    out_view->mid_map_bytes = mid_bytes;
    out_view->back_map = buffers->back_map;
    out_view->back_map_bytes = back_bytes;
    out_view->front_width_tiles = 32;
    out_view->mid_width_tiles = data[background_offset] == 2u ? 32u : 64u;
    out_view->back_width_tiles = 32;
    out_view->map_height_tiles = SKY_MAP_HEIGHT_TILES;
    out_view->size = data[background_offset];
    out_view->compression_flags = data[background_offset + 1];
    out_view->descriptor = read_le16(data + background_offset + 2);
    out_view->background_offset = (uint32_t)background_offset;
    out_view->background_index = background_index;
    out_view->palette_hardware_base = 96;
    return PORTABLE_SKY_ASSET_OK;
}
