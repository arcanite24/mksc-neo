#include "character_asset.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static int span_is_valid(size_t data_size, size_t offset, size_t length)
{
    return offset <= data_size && length <= data_size - offset;
}

PortableCharacterAssetStatus portable_character_asset_build(
    const uint8_t *tile_data,
    size_t tile_data_size,
    const uint8_t *palette_data,
    size_t palette_data_size,
    uint32_t character_id,
    uint32_t frame_slot,
    const PortableCharacterAssetBuffers *buffers,
    PortableCharacterAssetView *out_view)
{
    size_t tile_offset;
    size_t palette_offset;

    if (tile_data == NULL || palette_data == NULL || buffers == NULL ||
        out_view == NULL)
        return PORTABLE_CHARACTER_ASSET_INVALID_ARGUMENT;
    if (buffers->palette == NULL ||
        buffers->palette_capacity < PORTABLE_CHARACTER_PALETTE_COUNT)
        return PORTABLE_CHARACTER_ASSET_CAPACITY;
    if ((uint64_t)character_id * PORTABLE_CHARACTER_BLOCK_BYTES > SIZE_MAX ||
        (uint64_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES > SIZE_MAX)
        return PORTABLE_CHARACTER_ASSET_OUT_OF_BOUNDS;
    tile_offset = (size_t)character_id * PORTABLE_CHARACTER_BLOCK_BYTES;
    if (frame_slot >= PORTABLE_CHARACTER_BLOCK_BYTES /
                         PORTABLE_CHARACTER_FRAME_BYTES ||
        (size_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES >
            SIZE_MAX - tile_offset)
        return PORTABLE_CHARACTER_ASSET_FORMAT;
    tile_offset += (size_t)frame_slot * PORTABLE_CHARACTER_FRAME_BYTES;
    if (!span_is_valid(tile_data_size, tile_offset,
                       PORTABLE_CHARACTER_FRAME_BYTES))
        return PORTABLE_CHARACTER_ASSET_OUT_OF_BOUNDS;
    if ((uint64_t)character_id * PORTABLE_CHARACTER_PALETTE_STRIDE >
        SIZE_MAX)
        return PORTABLE_CHARACTER_ASSET_OUT_OF_BOUNDS;
    palette_offset = (size_t)character_id * PORTABLE_CHARACTER_PALETTE_STRIDE;
    if (!span_is_valid(palette_data_size, palette_offset,
                       PORTABLE_CHARACTER_PALETTE_COUNT * sizeof(uint16_t)))
        return PORTABLE_CHARACTER_ASSET_OUT_OF_BOUNDS;

    for (size_t index = 0; index < PORTABLE_CHARACTER_PALETTE_COUNT; ++index)
        buffers->palette[index] = read_le16(
            palette_data + palette_offset + index * sizeof(uint16_t));

    memset(out_view, 0, sizeof(*out_view));
    out_view->tiles = tile_data + tile_offset;
    out_view->tile_bytes = PORTABLE_CHARACTER_FRAME_BYTES;
    out_view->palette = buffers->palette;
    out_view->palette_count = PORTABLE_CHARACTER_PALETTE_COUNT;
    out_view->width = PORTABLE_CHARACTER_WIDTH;
    out_view->height = PORTABLE_CHARACTER_HEIGHT;
    out_view->character_id = character_id;
    out_view->frame_slot = frame_slot;
    out_view->tile_offset = tile_offset;
    out_view->palette_offset = palette_offset;
    out_view->direction_tiles = out_view->tiles;
    out_view->direction_tile_bytes = PORTABLE_CHARACTER_FRAME_BYTES;
    out_view->direction_frame_count = 1u;
    return PORTABLE_CHARACTER_ASSET_OK;
}

PortableCharacterAssetStatus portable_character_asset_open_borrowed(
    const PortableAssetSectionView *section,uint16_t *palette,size_t palette_capacity,
    PortableCharacterAssetView *out_view)
{
    if (section==NULL || palette==NULL || out_view==NULL)
        return PORTABLE_CHARACTER_ASSET_INVALID_ARGUMENT;
    if (section->kind!=PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS || section->version!=4u ||
        section->bytes==NULL || section->size<32u)
        return PORTABLE_CHARACTER_ASSET_FORMAT;
    const uint8_t *b=section->bytes;
    const uint32_t frames=read_le32(b+24u),colors=read_le32(b+8u);
    const uint32_t magic=read_le32(b),has_lod=(uint32_t)(magic==UINT32_C(0x33524143));
    if ((magic!=UINT32_C(0x32524143) && !has_lod) || frames==0u ||
        frames>(has_lod?PORTABLE_CHARACTER_LOD_FRAME_COUNT:PORTABLE_CHARACTER_FULL_FRAME_COUNT) ||
        colors<16u || colors>256u || colors%16u!=0u ||
        read_le32(b+4u)!=frames*PORTABLE_CHARACTER_FRAME_BYTES ||
        read_le32(b+12u)!=64u || read_le32(b+16u)!=64u ||
        read_le32(b+28u)!=PORTABLE_CHARACTER_VIEW_SLOT_COUNT)
        return PORTABLE_CHARACTER_ASSET_FORMAT;
    const size_t full=(size_t)frames*PORTABLE_CHARACTER_FRAME_BYTES;
    const size_t lod32=has_lod?(size_t)frames*PORTABLE_CHARACTER_LOD32_FRAME_BYTES:0u;
    const size_t lod16=has_lod?(size_t)frames*PORTABLE_CHARACTER_LOD16_FRAME_BYTES:0u;
    if (section->size!=32u+64u+full+lod32+lod16+(size_t)colors*2u)
        return PORTABLE_CHARACTER_ASSET_FORMAT;
    if (palette_capacity<colors) return PORTABLE_CHARACTER_ASSET_CAPACITY;
    const uint8_t *slots=b+32u,*tiles=slots+64u;
    for (size_t i=0u;i<64u;++i)
        if (slots[i]>=frames) return PORTABLE_CHARACTER_ASSET_FORMAT;
    const uint8_t *source_palette=tiles+full+lod32+lod16;
    for (size_t i = 0u; i < colors; ++i)
        palette[i] = read_le16(source_palette + i * 2u);
    *out_view=(PortableCharacterAssetView){
        .tiles=tiles+(size_t)slots[0]*PORTABLE_CHARACTER_FRAME_BYTES,
        .tile_bytes=PORTABLE_CHARACTER_FRAME_BYTES,.palette=palette,.palette_count=16u,
        .palette_banks=palette,.palette_bank_count=colors/16u,
        .width=64u,.height=64u,.character_id=read_le32(b+20u),.frame_slot=slots[0],
        .direction_tiles=tiles,.direction_tile_bytes=full,.direction_frame_count=frames,
        .lod32_tiles=has_lod?tiles+full:NULL,.lod16_tiles=has_lod?tiles+full+lod32:NULL,
        .view_frame_slots=slots,.view_frame_slot_count=64u};
    return PORTABLE_CHARACTER_ASSET_OK;
}

PortableCharacterAssetStatus portable_character_asset_open_packed(
    const PortableAssetSectionView *section,
    PortableCharacterPackedStorage *storage,
    PortableCharacterAssetView *out_view)
{
    enum { header_size = 32 };
    const uint32_t direction_magic = UINT32_C(0x32524143);
    const uint32_t lod_magic = UINT32_C(0x33524143);
    const uint8_t *bytes;
    const uint8_t *palette;
    uint32_t tile_bytes;
    uint32_t palette_count;
    uint32_t frame_count;
    uint32_t view_slot_count;
    int has_lod;
    size_t expected_size;

    if (section == NULL || storage == NULL || out_view == NULL)
        return PORTABLE_CHARACTER_ASSET_INVALID_ARGUMENT;
    if (section->kind == PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS && section->bytes != NULL &&
        section->size >= header_size && read_le32(section->bytes) == UINT32_C(0x31524143))
    {
        /* Legacy CAR1, formerly decoded by presentation_pack. Retain its
         * single-frame metadata and exact palette/tile output. */
        const uint8_t *b = section->bytes, *p = b + header_size;
        const uint32_t tiles = read_le32(b + 4u), colors = read_le32(b + 8u);
        if (section->version != 1u || tiles != PORTABLE_CHARACTER_FRAME_BYTES || colors != 16u ||
            read_le32(b + 12u) != 64u || read_le32(b + 16u) != 64u ||
            section->size != header_size + tiles + colors * 2u)
            return PORTABLE_CHARACTER_ASSET_FORMAT;
        memcpy(storage->tiles, p, tiles);
        p += tiles;
        for (size_t index = 0u; index < colors; ++index)
            storage->palette[index] = read_le16(p + index * 2u);
        *out_view = (PortableCharacterAssetView){.tiles = storage->tiles,
                                                 .tile_bytes = tiles,
                                                 .palette = storage->palette,
                                                 .palette_count = colors,
                                                 .palette_banks = storage->palette,
                                                 .palette_bank_count = 1u,
                                                 .width = 64u,
                                                 .height = 64u,
                                                 .character_id = read_le32(b + 20u),
                                                 .frame_slot = read_le32(b + 24u),
                                                 .direction_tiles = storage->tiles,
                                                 .direction_tile_bytes = tiles,
                                                 .direction_frame_count = 1u};
        return PORTABLE_CHARACTER_ASSET_OK;
    }
    if (section->version==4u) {
        PortableCharacterAssetStatus result=portable_character_asset_open_borrowed(
            section,storage->palette,sizeof storage->palette/sizeof storage->palette[0],out_view);
        if (result!=PORTABLE_CHARACTER_ASSET_OK) return result;
        const size_t lod_frames=out_view->lod32_tiles!=NULL?out_view->direction_frame_count:0u;
        memcpy(storage->tiles,out_view->direction_tiles,out_view->direction_tile_bytes);
        memcpy(storage->view_frame_slots,out_view->view_frame_slots,64u);
        if (lod_frames!=0u) {
            memcpy(storage->lod32_tiles,out_view->lod32_tiles,lod_frames*PORTABLE_CHARACTER_LOD32_FRAME_BYTES);
            memcpy(storage->lod16_tiles,out_view->lod16_tiles,lod_frames*PORTABLE_CHARACTER_LOD16_FRAME_BYTES);
        }
        out_view->tiles=storage->tiles+(size_t)out_view->frame_slot*PORTABLE_CHARACTER_FRAME_BYTES;
        out_view->direction_tiles=storage->tiles;
        out_view->view_frame_slots=storage->view_frame_slots;
        out_view->lod32_tiles=lod_frames?storage->lod32_tiles:NULL;
        out_view->lod16_tiles=lod_frames?storage->lod16_tiles:NULL;
        return result;
    }
    if (section->kind != PORTABLE_ASSET_SECTION_CHARACTER_GRAPHICS ||
        (section->version < 1u || section->version > 3u) ||
        section->bytes == NULL ||
        section->size < header_size)
        return PORTABLE_CHARACTER_ASSET_FORMAT;
    bytes = section->bytes;
    tile_bytes = read_le32(bytes + 4u);
    palette_count = read_le32(bytes + 8u);
    frame_count = read_le32(bytes + 24u);
    view_slot_count = read_le32(bytes + 28u);
    has_lod = read_le32(bytes) == lod_magic;
    expected_size = header_size + (size_t)view_slot_count + tile_bytes +
                    (has_lod ? sizeof storage->lod32_tiles +
                               sizeof storage->lod16_tiles : 0u) +
                    (size_t)palette_count * sizeof(uint16_t);
    if ((read_le32(bytes) != direction_magic && !has_lod) ||
        tile_bytes != (section->version >= 2u ?
                      PORTABLE_CHARACTER_FULL_FRAME_COUNT :
                      PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT) *
                     PORTABLE_CHARACTER_FRAME_BYTES ||
        palette_count != PORTABLE_CHARACTER_PALETTE_COUNT *
            (section->version == 3u ? PORTABLE_CHARACTER_PALETTE_BANK_COUNT : 1u) ||
        frame_count != (section->version >= 2u ?
                       PORTABLE_CHARACTER_FULL_FRAME_COUNT :
                       PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT) ||
        view_slot_count != PORTABLE_CHARACTER_VIEW_SLOT_COUNT ||
        section->size != expected_size)
        return PORTABLE_CHARACTER_ASSET_FORMAT;
    memcpy(storage->view_frame_slots, bytes + header_size, view_slot_count);
    for (size_t index = 0u; index < view_slot_count; ++index)
        if (storage->view_frame_slots[index] >= frame_count ||
            (has_lod && storage->view_frame_slots[index] >=
                            PORTABLE_CHARACTER_LOD_FRAME_COUNT))
            return PORTABLE_CHARACTER_ASSET_FORMAT;
    memset(storage->tiles, 0, sizeof storage->tiles);
    memcpy(storage->tiles, bytes + header_size + view_slot_count, tile_bytes);
    palette = bytes + header_size + view_slot_count + tile_bytes;
    if (has_lod) {
        memcpy(storage->lod32_tiles, palette, sizeof storage->lod32_tiles);
        palette += sizeof storage->lod32_tiles;
        memcpy(storage->lod16_tiles, palette, sizeof storage->lod16_tiles);
        palette += sizeof storage->lod16_tiles;
    }
    for (size_t index = 0u; index < palette_count; ++index)
        storage->palette[index] = read_le16(palette + index * 2u);
    *out_view = (PortableCharacterAssetView){
        .tiles = storage->tiles +
                 (PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT - 1u) *
                     PORTABLE_CHARACTER_FRAME_BYTES,
        .tile_bytes = PORTABLE_CHARACTER_FRAME_BYTES,
        .palette = storage->palette,
        .palette_count = PORTABLE_CHARACTER_PALETTE_COUNT,
        .palette_banks = storage->palette,
        .palette_bank_count = palette_count/PORTABLE_CHARACTER_PALETTE_COUNT,
        .width = read_le32(bytes + 12u),
        .height = read_le32(bytes + 16u),
        .character_id = read_le32(bytes + 20u),
        .frame_slot = PORTABLE_CHARACTER_DIRECTION_FRAME_COUNT - 1u,
        .direction_tiles = storage->tiles,
        .direction_tile_bytes = tile_bytes,
        .direction_frame_count = frame_count,
        .lod32_tiles = has_lod ? storage->lod32_tiles : NULL,
        .lod16_tiles = has_lod ? storage->lod16_tiles : NULL,
        .view_frame_slots = storage->view_frame_slots,
        .view_frame_slot_count = view_slot_count,
    };
    return out_view->width == PORTABLE_CHARACTER_WIDTH &&
                   out_view->height == PORTABLE_CHARACTER_HEIGHT
               ? PORTABLE_CHARACTER_ASSET_OK
               : PORTABLE_CHARACTER_ASSET_FORMAT;
}

int portable_character_asset_palette(const PortableCharacterAssetView *view,
    uint8_t bank,uint16_t output[PORTABLE_CHARACTER_PALETTE_COUNT])
{
    if(!view || !output || !view->palette || view->palette_count<16u)return 0;
    memcpy(output,view->palette,PORTABLE_CHARACTER_PALETTE_COUNT*sizeof(uint16_t));
    if(bank==0u)return 1;
    if(!view->palette_banks || bank>=view->palette_bank_count)return 0;
    memcpy(output+3u,view->palette_banks+(size_t)bank*16u+3u,13u*sizeof(uint16_t));
    return 1;
}
