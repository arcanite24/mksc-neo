#include "nitro_2d.h"
#include <string.h>

/* The first section of a Nitro file, checked against its reversed magic. */
static RiData section(RiCtx *ctx, RiData file, const char *magic, const char *where)
{
    if (file.size < 16 || memcmp(ri_at(ctx, file, 0, 4), magic, 4) != 0 || ri_get_u16(ctx, file, 4) != 0xFEFFu)
        ri_fail(ctx, "%s: not a Nitro %.4s file", where, magic);
    const size_t header = ri_get_u16(ctx, file, 12);
    const size_t size = ri_get_u32(ctx, file, header + 4);
    if (size < 8)
        ri_fail(ctx, "%s: empty Nitro section", where);
    return ri_span(ctx, file, header, size);
}

Nitro2DImage nitro_screen(RiCtx *ctx, RiData nclr, RiData ncgr, RiData nscr, const char *where)
{
    const RiData palette_section = section(ctx, nclr, "RLCN", where);
    const RiData palette = ri_span(ctx, palette_section, 8 + ri_get_u32(ctx, palette_section, 20),
                                   ri_get_u32(ctx, palette_section, 16));
    const RiData characters = section(ctx, ncgr, "RGCN", where);
    const uint32_t depth = ri_get_u32(ctx, characters, 12), flags = ri_get_u32(ctx, characters, 20);
    if (depth != 3 || (flags & 1u))
        ri_fail(ctx, "%s: NCGR depth %u flags 0x%x unsupported (want tiled 4bpp)", where, (unsigned)depth,
                (unsigned)flags);
    RiData tiles = ri_span(ctx, characters, 8 + ri_get_u32(ctx, characters, 28), ri_get_u32(ctx, characters, 24));
    const RiData screen = section(ctx, nscr, "RCSN", where);
    const uint32_t width = ri_get_u16(ctx, screen, 8), height = ri_get_u16(ctx, screen, 10);
    if (!width || !height || width % 256 || height % 256)
        ri_fail(ctx, "%s: NSCR %ux%u is not whole 256-pixel screens", where, (unsigned)width, (unsigned)height);
    const RiData cells = ri_span(ctx, screen, 20, (size_t)width * height / 32);
    /* Characters load after the screen's blank ones: cells beyond the set
     * shift down onto it, the cells below it are blank. */
    const uint32_t count = (uint32_t)(tiles.size / 32);
    uint32_t highest = 0;
    for (size_t index = 0; index < cells.size / 2; ++index)
        if ((ri_le16(cells.data + index * 2) & 1023u) > highest)
            highest = ri_le16(cells.data + index * 2) & 1023u;
    if (highest >= count)
    {
        RiBuf *padded = ri_buf_zero(ctx, (size_t)(highest + 1 - count) * 32);
        ri_put(ctx, padded, tiles.data, tiles.size);
        tiles = ri_view(padded);
    }
    RiBuf *full = ri_tile_map(ctx, tiles, palette, cells, width / 8, height / 8, 4);
    uint32_t left = width, top = height, right = 0, bottom = 0;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            if (ri_pixel(full, (size_t)y * width + x) >> 24)
            {
                left = x < left ? x : left;
                top = y < top ? y : top;
                right = x + 1 > right ? x + 1 : right;
                bottom = y + 1 > bottom ? y + 1 : bottom;
            }
    if (right <= left)
        ri_fail(ctx, "%s: screen has no opaque pixel", where);
    Nitro2DImage out = {ri_buf_zero(ctx, (size_t)(right - left) * (bottom - top) * 4), right - left, bottom - top,
                        left, top};
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x)
            ri_set_pixel(out.pixels, (size_t)y * out.width + x, ri_pixel(full, (size_t)(top + y) * width + left + x));
    return out;
}
