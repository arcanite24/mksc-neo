#include "rom_import_internal.h"

#include <string.h>

uint32_t ri_color(uint32_t value)
{
    /* The software renderer's 5-bit expansion. */
    uint32_t r = value & 31, g = value >> 5 & 31, b = value >> 10 & 31;
    return 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
}

uint32_t ri_tile_pixel(RiCtx *ctx, RiData tiles, RiData palette, uint32_t tile, uint32_t x,
                       uint32_t y, uint32_t palette_bank, int depth)
{
    size_t offset = depth == 8 ? (size_t)tile * 64 + y * 8 + x : (size_t)tile * 32 + y * 4 + x / 2;
    if (offset >= tiles.size)
        ri_fail(ctx, "UI tile %u exceeds graphics", (unsigned)tile);
    uint32_t index = depth == 8 ? tiles.data[offset] : tiles.data[offset] >> (4 * (x & 1)) & 15;
    if (!index)
        return 0;
    size_t color = ((depth == 4 ? palette_bank * 16 : 0) + index) * 2;
    if (color + 2 > palette.size)
        ri_fail(ctx, "UI palette index exceeds colors");
    return ri_color(ri_le16(palette.data + color));
}

uint32_t ri_pixel(const RiBuf *pixels, size_t index)
{
    return ri_le32(pixels->data + index * 4);
}

void ri_set_pixel(RiBuf *pixels, size_t index, uint32_t value)
{
    ri_set_u32(pixels, index * 4, value);
}

RiBuf *ri_tile_map(RiCtx *ctx, RiData tiles, RiData palette, RiData cells, uint32_t width,
                   uint32_t height, int depth)
{
    if (width % 32 || height % 32 || cells.size != (size_t)width * height * 2)
        ri_fail(ctx, "invalid text tile map size");
    size_t pixel_width = (size_t)width * 8;
    RiBuf *out = ri_buf_zero(ctx, (size_t)width * height * 64 * 4);
    for (uint32_t cy = 0; cy < height; ++cy)
    {
        for (uint32_t cx = 0; cx < width; ++cx)
        {
            size_t block = (cy / 32) * (width / 32) + cx / 32;
            size_t index = block * 1024 + (cy % 32) * 32 + cx % 32;
            uint32_t cell = ri_le16(cells.data + index * 2);
            for (uint32_t y = 0; y < 8; ++y)
                for (uint32_t x = 0; x < 8; ++x)
                    ri_set_pixel(out, (cy * 8 + y) * pixel_width + cx * 8 + x,
                                 ri_tile_pixel(ctx, tiles, palette, cell & 1023,
                                               cell & 0x400 ? 7 - x : x, cell & 0x800 ? 7 - y : y,
                                               cell >> 12, depth));
        }
    }
    return out;
}

RiBuf *ri_image_asset(RiCtx *ctx, uint32_t width, uint32_t height, const RiBuf *pixels,
                      int32_t anchor_x, int32_t anchor_y)
{
    size_t count = (size_t)width * height;
    if (width < 1 || width > 4096 || height < 1 || height > 4096 || pixels->size != count * 4)
        ri_fail(ctx, "invalid native image dimensions");
    RiBuf *out = ri_buf(ctx);
    ri_reserve(ctx, out, 32 + count * 4);
    ri_put(ctx, out, "MKSCIMG1", 8);
    ri_u16(ctx, out, 32);
    ri_u16(ctx, out, 1);
    ri_u16(ctx, out, width);
    ri_u16(ctx, out, height);
    ri_i16(ctx, out, anchor_x);
    ri_i16(ctx, out, anchor_y);
    ri_u32(ctx, out, 0);
    ri_u32(ctx, out, 32);
    ri_u32(ctx, out, (uint32_t)(32 + count * 4));
    ri_put_buf(ctx, out, pixels);
    return out;
}

typedef struct CellPart
{
    int32_t x, y, width, height;
    uint32_t b, c;
} CellPart;

static RiBuf *cell_image(RiCtx *ctx, RiData rom, size_t address, RiData tiles, RiData palette,
                         int palette_base)
{
    static const int sizes[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
                                       {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
                                       {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};
    uint32_t count = ri_get_u16(ctx, rom, address);
    if (count < 1 || count > 128 || address + 2 + count * 6 > rom.size)
        ri_fail(ctx, "invalid UI cell list");
    CellPart parts[128];
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t *entry = rom.data + address + 2 + i * 6;
        uint32_t a = ri_le16(entry), b = ri_le16(entry + 2), c = ri_le16(entry + 4);
        if (a & 0x2000 || a >> 14 == 3)
            ri_fail(ctx, "unsupported UI cell format");
        CellPart *part = &parts[i];
        part->width = sizes[a >> 14][b >> 14][0];
        part->height = sizes[a >> 14][b >> 14][1];
        part->x = (int32_t)(b & 511);
        part->y = (int32_t)(a & 255);
        if (part->x >= 256)
            part->x -= 512;
        if (part->y >= 128)
            part->y -= 256;
        part->b = b;
        part->c = c;
        if (!i || part->x < left)
            left = part->x;
        if (!i || part->y < top)
            top = part->y;
        if (!i || part->x + part->width > right)
            right = part->x + part->width;
        if (!i || part->y + part->height > bottom)
            bottom = part->y + part->height;
    }
    uint32_t width = (uint32_t)(right - left), height = (uint32_t)(bottom - top);
    if (palette_base >= 0 && (palette.size % 2 || !palette.size || palette.size > 256 ||
                              (size_t)palette_base + palette.size / 2 > 128))
        ri_fail(ctx, "invalid indexed image palette");
    RiBuf *pixels = ri_buf_zero(ctx, (size_t)width * height * 4);
    if (palette_base >= 0)
        memset(pixels->data, 255, (size_t)width * height);
    for (uint32_t n = count; n-- > 0;)
    {
        const CellPart *part = &parts[n];
        for (int32_t y = 0; y < part->height; ++y)
        {
            for (int32_t x = 0; x < part->width; ++x)
            {
                int32_t sx = part->b & 0x1000 ? part->width - 1 - x : x;
                int32_t sy = part->b & 0x2000 ? part->height - 1 - y : y;
                uint32_t pixel = ri_tile_pixel(
                    ctx, tiles, palette,
                    (part->c & 1023) + (uint32_t)((sy / 8) * (part->width / 8) + sx / 8),
                    (uint32_t)(sx % 8), (uint32_t)(sy % 8), part->c >> 12, 4);
                if (pixel)
                {
                    const size_t target =
                        (size_t)(part->y - top + y) * width + (size_t)(part->x - left + x);
                    if (palette_base >= 0)
                    {
                        const size_t tile = (part->c & 1023) +
                                            (size_t)(sy / 8) * (part->width / 8) + (size_t)sx / 8;
                        const unsigned index =
                            tiles.data[tile * 32 + (size_t)(sy % 8) * 4 + (size_t)(sx % 8) / 2] >>
                                (4 * (sx & 1)) &
                            15;
                        pixels->data[target] =
                            (uint8_t)(palette_base + (part->c >> 12) * 16 + index);
                    }
                    else
                        ri_set_pixel(pixels, target, pixel);
                }
            }
        }
    }
    RiBuf *out;
    if (palette_base >= 0)
    {
        const size_t pixel_count = (size_t)width * height;
        out = ri_buf(ctx);
        ri_put(ctx, out, "MKSCIMG2", 8);
        ri_u16(ctx, out, 32);
        ri_u16(ctx, out, 2);
        ri_u16(ctx, out, width);
        ri_u16(ctx, out, height);
        ri_i16(ctx, out, left);
        ri_i16(ctx, out, top);
        ri_u32(ctx, out, (uint32_t)(palette.size / 2) | ((uint32_t)palette_base << 16));
        ri_u32(ctx, out, 32);
        ri_u32(ctx, out, (uint32_t)(32 + pixel_count + palette.size));
        ri_put(ctx, out, pixels->data, pixel_count);
        ri_put(ctx, out, palette.data, palette.size);
    }
    else
        out = ri_image_asset(ctx, width, height, pixels, left, top);
    ri_buf_free(ctx, pixels);
    return out;
}

RiBuf *ri_cell_image(RiCtx *ctx, RiData rom, size_t address, RiData tiles, RiData palette)
{
    return cell_image(ctx, rom, address, tiles, palette, -1);
}
RiBuf *ri_cell_image_indexed(RiCtx *ctx, RiData rom, size_t address, RiData tiles, RiData palette,
                             uint32_t palette_base)
{
    if (palette_base > 127u)
        ri_fail(ctx, "invalid indexed image palette base");
    return cell_image(ctx, rom, address, tiles, palette, (int)palette_base);
}
