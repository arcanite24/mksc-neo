#include "hud_asset.h"
#include <string.h>

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static int copy_bytes(const uint8_t *data,
                      size_t data_size,
                      uint32_t source_offset,
                      uint8_t *destination,
                      size_t count)
{
    const size_t offset = (size_t)source_offset;

    if (offset > data_size || data_size - offset < count)
        return PORTABLE_HUD_ASSET_OUT_OF_BOUNDS;
    for (size_t index = 0; index < count; ++index)
        destination[index] = data[offset + index];
    return PORTABLE_HUD_ASSET_OK;
}

static int copy_halfwords(const uint8_t *data,
                          size_t data_size,
                          uint32_t source_offset,
                          uint16_t *destination,
                          size_t count)
{
    const size_t offset = (size_t)source_offset;

    if (offset > data_size || data_size - offset < count * 2u)
        return PORTABLE_HUD_ASSET_OUT_OF_BOUNDS;
    for (size_t index = 0; index < count; ++index)
        destination[index] = read_le16(data + offset + index * 2u);
    return PORTABLE_HUD_ASSET_OK;
}

static int copy_map(const uint8_t *data,
                    size_t data_size,
                    uint32_t source_offset,
                    uint16_t *destination,
                    size_t destination_count,
                    uint16_t expected_width,
                    uint16_t expected_height)
{
    const size_t offset = (size_t)source_offset;
    const uint16_t width =
        (offset <= data_size && data_size - offset >= 2u)
            ? read_le16(data + offset)
            : 0;
    const uint16_t height =
        (offset <= data_size && data_size - offset >= 4u)
            ? read_le16(data + offset + 2u)
            : 0;
    const size_t cell_count = (size_t)width * (size_t)height;

    if (offset > data_size || data_size - offset < 4u + cell_count * 2u)
        return PORTABLE_HUD_ASSET_OUT_OF_BOUNDS;
    if (width != expected_width || height != expected_height ||
        destination_count < cell_count)
        return PORTABLE_HUD_ASSET_INVALID_ARGUMENT;
    return copy_halfwords(data, data_size, source_offset + 4u, destination,
                          cell_count);
}

int portable_hud_asset_build(const uint8_t *data,
                             size_t data_size,
                             uint32_t tile_source_offset,
                             uint32_t palette_source_offset,
                             uint32_t digit_source_offset,
                             uint32_t clock_source_offset,
                             uint32_t left_source_offset,
                             uint32_t slash_source_offset,
                             uint32_t right_source_offset,
                             uint32_t item_tile_source_offset,
                             uint32_t item_map_source_offset,
                             uint32_t item_palette_source_offset,
                             const PortableHudAssetBuffers *buffers,
                             PortableHudAssetView *out)
{
    int status;

    if (data == NULL || buffers == NULL || out == NULL ||
        buffers->tiles == NULL || buffers->palette == NULL ||
        buffers->digit_tiles == NULL || buffers->clock_steps == NULL ||
        buffers->left_cells == NULL ||
        buffers->slash_cells == NULL || buffers->right_cells == NULL ||
        buffers->item_tiles == NULL || buffers->item_cells == NULL || buffers->item_palette_selectors==NULL)
        return PORTABLE_HUD_ASSET_INVALID_ARGUMENT;
    if (buffers->tile_capacity < PORTABLE_HUD_CHAR_BYTES ||
        buffers->palette_capacity < PORTABLE_HUD_PALETTE_COUNT ||
        buffers->digit_capacity < PORTABLE_HUD_DIGIT_COUNT ||
        buffers->clock_capacity < PORTABLE_HUD_CLOCK_STEP_COUNT ||
        buffers->left_capacity < PORTABLE_HUD_LEFT_CELL_COUNT ||
        buffers->slash_capacity < PORTABLE_HUD_SLASH_CELL_COUNT ||
        buffers->right_capacity < PORTABLE_HUD_RIGHT_CELL_COUNT ||
        buffers->item_tile_capacity < PORTABLE_HUD_ITEM_TILE_BYTES ||
        buffers->item_cell_capacity < PORTABLE_HUD_ITEM_CELL_COUNT ||
        buffers->item_palette_capacity < PORTABLE_HUD_ITEM_PALETTE_COUNT)
        return PORTABLE_HUD_ASSET_CAPACITY;

    status = copy_bytes(data, data_size, tile_source_offset, buffers->tiles,
                        PORTABLE_HUD_CHAR_BYTES);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;
    /* Preserve every physical page selected by sub_804CD98. Time Trial uses
       pages 4, 5, 6 and 0; GP items use the same source bank. */
    for (size_t page = 0; page < PORTABLE_HUD_ITEM_PAGE_COUNT; ++page)
    {
        status = copy_bytes(
            data, data_size,
            item_tile_source_offset + (uint32_t)page * PORTABLE_HUD_ITEM_PAGE_BYTES,
            buffers->item_tiles + page * PORTABLE_HUD_ITEM_PAGE_BYTES,
            PORTABLE_HUD_ITEM_PAGE_BYTES);
        if (status != PORTABLE_HUD_ASSET_OK)
            return status;
    }
    status = copy_map(data, data_size, item_map_source_offset,
                      buffers->item_cells, PORTABLE_HUD_ITEM_CELL_COUNT, 4, 4);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;
    status = copy_halfwords(data, data_size, palette_source_offset,
                            buffers->palette, PORTABLE_HUD_PALETTE_COUNT);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;
    status = copy_halfwords(data, data_size, digit_source_offset,
                            buffers->digit_tiles, PORTABLE_HUD_DIGIT_COUNT);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;
    status = copy_bytes(data, data_size, clock_source_offset,
                        buffers->clock_steps,
                        PORTABLE_HUD_CLOCK_STEP_COUNT);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;
    status = copy_map(data, data_size, left_source_offset, buffers->left_cells,
                      PORTABLE_HUD_LEFT_CELL_COUNT, 3, 2);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;
    status = copy_map(data, data_size, slash_source_offset,
                      buffers->slash_cells, PORTABLE_HUD_SLASH_CELL_COUNT, 1,
                      2);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;
    status = copy_map(data, data_size, right_source_offset,
                      buffers->right_cells, PORTABLE_HUD_RIGHT_CELL_COUNT, 3,
                      2);
    if (status != PORTABLE_HUD_ASSET_OK)
        return status;

    status = copy_halfwords(data, data_size, item_palette_source_offset,
                            buffers->item_palette_selectors, PORTABLE_HUD_ITEM_PALETTE_COUNT);
    if(status!=PORTABLE_HUD_ASSET_OK)return status;
    for(unsigned i=0u;i<PORTABLE_HUD_ITEM_PALETTE_COUNT;++i)
        if(buffers->item_palette_selectors[i]&UINT16_C(0x0fff))return PORTABLE_HUD_ASSET_INVALID_ARGUMENT;
    out->item_palette_selectors=buffers->item_palette_selectors;
    out->item_palette_count=PORTABLE_HUD_ITEM_PALETTE_COUNT;
    out->tiles = buffers->tiles;
    out->tile_bytes = PORTABLE_HUD_CHAR_BYTES;
    out->palette = buffers->palette;
    out->palette_count = PORTABLE_HUD_PALETTE_COUNT;
    out->digit_tiles = buffers->digit_tiles;
    out->digit_count = PORTABLE_HUD_DIGIT_COUNT;
    out->clock_steps = buffers->clock_steps;
    out->clock_step_count = PORTABLE_HUD_CLOCK_STEP_COUNT;
    out->left_cells = buffers->left_cells;
    out->left_cell_count = PORTABLE_HUD_LEFT_CELL_COUNT;
    out->slash_cells = buffers->slash_cells;
    out->slash_cell_count = PORTABLE_HUD_SLASH_CELL_COUNT;
    out->right_cells = buffers->right_cells;
    out->right_cell_count = PORTABLE_HUD_RIGHT_CELL_COUNT;
    out->item_tiles = buffers->item_tiles;
    out->item_tile_bytes = PORTABLE_HUD_ITEM_TILE_BYTES;
    out->item_cells = buffers->item_cells;
    out->item_cell_count = PORTABLE_HUD_ITEM_CELL_COUNT;
    out->tile_source_offset = tile_source_offset;
    out->palette_source_offset = palette_source_offset;
    out->digit_source_offset = digit_source_offset;
    out->clock_source_offset = clock_source_offset;
    out->left_source_offset = left_source_offset;
    out->slash_source_offset = slash_source_offset;
    out->right_source_offset = right_source_offset;
    out->item_tile_source_offset = item_tile_source_offset;
    out->item_map_source_offset = item_map_source_offset;
    return PORTABLE_HUD_ASSET_OK;
}

static uint32_t hud_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}
static int hud_read16s(const uint8_t **p, const uint8_t *end, uint16_t *out, size_t count)
{
    if ((size_t)(end - *p) < count * 2u)
        return 0;
    for (size_t i = 0; i < count; ++i)
        out[i] = read_le16(*p + i * 2u);
    *p += count * 2u;
    return 1;
}
int portable_hud_asset_open(const PortableAssetSectionView *s, const PortableHudAssetBuffers *st,
                            PortableHudAssetView *v)
{
    const size_t maximum_bytes =
        44u + PORTABLE_HUD_CHAR_BYTES + PORTABLE_HUD_ITEM_TILE_BYTES +
        PORTABLE_HUD_CLOCK_STEP_COUNT +
        2u * (PORTABLE_HUD_PALETTE_COUNT + PORTABLE_HUD_DIGIT_COUNT + PORTABLE_HUD_LEFT_CELL_COUNT +
              PORTABLE_HUD_SLASH_CELL_COUNT + PORTABLE_HUD_RIGHT_CELL_COUNT +
              PORTABLE_HUD_ITEM_CELL_COUNT + PORTABLE_HUD_ITEM_PALETTE_COUNT);
    if (!s || !s->bytes || s->size > maximum_bytes || !st || !v || !st->tiles || !st->palette ||
        !st->digit_tiles || !st->clock_steps || !st->left_cells || !st->slash_cells ||
        !st->right_cells || !st->item_tiles || !st->item_cells || !st->item_palette_selectors)
        return 0;
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    uint32_t n[9];
    uint32_t magic;
    if ((s->version != 1u && s->version != 2u) || s->size < 36)
        return 0;
    magic = hud_u32(b);
    if (magic != UINT32_C(0x31445548) && magic != UINT32_C(0x32445548))
        return 0;
    if (magic == UINT32_C(0x32445548) && s->size < 44u)
        return 0;
    if (s->version == 2u && magic != UINT32_C(0x32445548))
        return 0;
    const uint32_t selectors = s->version == 2u ? hud_u32(b + 40u) : 0u;
    if (s->version == 2u && (magic != UINT32_C(0x32445548) || selectors != 17u))
        return 0;
    for (size_t i = 0; i < 7; ++i)
        n[i] = hud_u32(b + 4 + i * 4u);
    n[7] = magic == UINT32_C(0x32445548) ? hud_u32(b + 32) : 0u;
    n[8] = magic == UINT32_C(0x32445548) ? hud_u32(b + 36) : 0u;
    if (n[0] > PORTABLE_HUD_CHAR_BYTES || n[1] > PORTABLE_HUD_PALETTE_COUNT ||
        n[2] > PORTABLE_HUD_DIGIT_COUNT || n[3] > PORTABLE_HUD_CLOCK_STEP_COUNT ||
        n[4] > PORTABLE_HUD_LEFT_CELL_COUNT || n[5] > PORTABLE_HUD_SLASH_CELL_COUNT ||
        n[6] > PORTABLE_HUD_RIGHT_CELL_COUNT || n[7] > PORTABLE_HUD_ITEM_TILE_BYTES ||
        n[8] > PORTABLE_HUD_ITEM_CELL_COUNT || n[0] > st->tile_capacity ||
        n[1] > st->palette_capacity || n[2] > st->digit_capacity || n[3] > st->clock_capacity ||
        n[4] > st->left_capacity || n[5] > st->slash_capacity || n[6] > st->right_capacity ||
        n[7] > st->item_tile_capacity || n[8] > st->item_cell_capacity ||
        selectors > st->item_palette_capacity ||
        s->size != (magic == UINT32_C(0x32445548) ? 44u : 36u) + n[0] + n[7] +
                       (n[1] + n[2] + n[4] + n[5] + n[6] + n[8] + selectors) * 2u + n[3])
        return 0;
    p = b + (magic == UINT32_C(0x32445548) ? 44u : 36u);
    memcpy(st->tiles, p, n[0]);
    p += n[0];
    if (!hud_read16s(&p, end, st->palette, n[1]) || !hud_read16s(&p, end, st->digit_tiles, n[2]))
        return 0;
    memcpy(st->clock_steps, p, n[3]);
    p += n[3];
    if (!hud_read16s(&p, end, st->left_cells, n[4]) ||
        !hud_read16s(&p, end, st->slash_cells, n[5]) ||
        !hud_read16s(&p, end, st->right_cells, n[6]))
        return 0;
    memcpy(st->item_tiles, p, n[7]);
    p += n[7];
    if (!hud_read16s(&p, end, st->item_cells, n[8]) ||
        !hud_read16s(&p, end, st->item_palette_selectors, selectors) || p != end)
        return 0;
    for (unsigned i = 0u; i < selectors; ++i)
        if (st->item_palette_selectors[i] & 0x0fffu)
            return 0;
    *v = (PortableHudAssetView){.tiles = st->tiles,
                                .tile_bytes = n[0],
                                .palette = st->palette,
                                .palette_count = n[1],
                                .digit_tiles = st->digit_tiles,
                                .digit_count = n[2],
                                .clock_steps = st->clock_steps,
                                .clock_step_count = n[3],
                                .left_cells = st->left_cells,
                                .left_cell_count = n[4],
                                .slash_cells = st->slash_cells,
                                .slash_cell_count = n[5],
                                .right_cells = st->right_cells,
                                .right_cell_count = n[6],
                                .item_tiles = st->item_tiles,
                                .item_tile_bytes = n[7],
                                .item_cells = st->item_cells,
                                .item_cell_count = n[8],
                                .item_palette_selectors =
                                    selectors ? st->item_palette_selectors : NULL,
                                .item_palette_count = selectors};
    return 1;
}
