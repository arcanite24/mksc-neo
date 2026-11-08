#include "track_object_renderer.h"

static uint16_t read_le16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static int32_t sign_extend8(uint32_t value)
{
    return (value & 0x80u) != 0 ? (int32_t)value - 0x100 : (int32_t)value;
}

static int32_t sign_extend9(uint32_t value)
{
    return (value & 0x100u) != 0 ? (int32_t)value - 0x200 : (int32_t)value;
}

static uint32_t color_to_rgb(uint16_t color)
{
    uint32_t red = (uint32_t)(color & 0x1fu) * 255u / 31u;
    uint32_t green = (uint32_t)((color >> 5) & 0x1fu) * 255u / 31u;
    uint32_t blue = (uint32_t)((color >> 10) & 0x1fu) * 255u / 31u;

    return (red << 16) | (green << 8) | blue;
}

static void object_dimensions(uint32_t shape, uint32_t size,
                              uint32_t *out_width, uint32_t *out_height)
{
    static const uint8_t widths[3][4] = {
        {8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const uint8_t heights[3][4] = {
        {8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

    if (shape > 2u)
        shape = 0;
    if (size > 3u)
        size = 0;
    *out_width = widths[shape][size];
    *out_height = heights[shape][size];
}

int portable_track_object_render(
    const PortableTrackObjectArtView *art,
    PortableSoftwareFrame *frame,
    int32_t origin_x,
    int32_t origin_y,
    uint16_t scale_x,
    uint16_t scale_y)
{
    const uint8_t *cell;
    uint32_t count;
    uint32_t index;

    if (art == NULL || frame == NULL || frame->pixels == NULL ||
        art->tiles == NULL || art->palette == NULL || art->cell_data == NULL ||
        art->tile_bytes < PORTABLE_TRACK_OBJECT_ART_TILE_BYTES ||
        art->palette_count < PORTABLE_TRACK_OBJECT_ART_PALETTE_COUNT ||
        art->cell_data_bytes < 2u || scale_x == 0 || scale_y == 0)
        return 0;
    cell = art->cell_data;
    count = read_le16(cell);
    if (2u + (size_t)count * 6u > art->cell_data_bytes)
        return 0;
    cell += 2;

    for (index = 0; index < count; ++index, cell += 6)
    {
        const uint16_t attr0 = read_le16(cell);
        const uint16_t attr1 = read_le16(cell + 2);
        const uint16_t attr2 = read_le16(cell + 4);
        uint32_t width;
        uint32_t height;
        uint32_t scaled_width;
        uint32_t scaled_height;
        uint32_t source_tile;
        uint32_t palette_base;
        uint32_t y;

        object_dimensions((attr0 >> 14) & 3u, (attr1 >> 14) & 3u,
                          &width, &height);
        scaled_width = (width * scale_x + 0xffu) >> 8;
        scaled_height = (height * scale_y + 0xffu) >> 8;
        source_tile = attr2 & 0x03ffu;
        palette_base = ((uint32_t)(attr2 >> 12) & 0xfu) * 16u;
        if (palette_base + 15u >= art->palette_count)
            return 0;

        for (y = 0; y < scaled_height; ++y)
        {
            uint32_t source_y = (y * 256u) / scale_y;
            uint32_t x;

            if ((attr1 & 0x2000u) != 0)
                source_y = height - 1u - source_y;
            for (x = 0; x < scaled_width; ++x)
            {
                uint32_t source_x = (x * 256u) / scale_x;
                size_t tile_offset;
                size_t byte_offset;
                uint8_t packed;
                uint32_t color_index;
                int32_t destination_x;
                int32_t destination_y;

                if ((attr1 & 0x1000u) != 0)
                    source_x = width - 1u - source_x;
                tile_offset = (size_t)source_tile +
                              (size_t)(source_y / 8u) * (width / 8u) +
                              source_x / 8u;
                byte_offset = tile_offset * 32u +
                              (source_y & 7u) * 4u +
                              (source_x & 7u) / 2u;
                if (byte_offset >= art->tile_bytes)
                    return 0;
                packed = art->tiles[byte_offset];
                color_index = (source_x & 1u) != 0 ? packed >> 4
                                                   : packed & 0xfu;
                if (color_index == 0)
                    continue;
                destination_x = origin_x + sign_extend9(attr1 & 0x1ffu) +
                                (int32_t)x;
                destination_y = origin_y + sign_extend8(attr0 & 0xffu) +
                                (int32_t)y;
                if (destination_x < 0 || destination_y < 0 ||
                    destination_x >= (int32_t)frame->width ||
                    destination_y >= (int32_t)frame->height)
                    continue;
                portable_software_frame_store(frame,(size_t)destination_y * frame->width +
                              (size_t)destination_x, color_to_rgb(art->palette[palette_base + color_index]));
            }
        }
    }
    return 1;
}
