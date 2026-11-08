#include "rom_import_internal.h"

#include <string.h>

/* Ports of build_lightning_effect_pack.py, build_gp_finish_banner_pack.py and
 * build_gp_results_pack.py. */

static void put_rom(RiCtx *ctx, RiBuf *out, size_t offset, size_t size)
{
    ri_put(ctx, out, ri_at(ctx, ctx->rom, offset, size), size);
}

static RiBuf *lightning(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 4, 0x3146584Cu, 0x800u, 4u, 0x1A0u);
    put_rom(ctx, out, 0x6C38C, 0x800);
    return out;
}

static RiBuf *finish_banner(RiCtx *ctx)
{
    RiBuf *tiles = ri_lz77(ctx, ctx->rom, 0x6D210);
    if (tiles->size != 0x800) ri_fail(ctx, "wrong GP finish-banner decoded tile size");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 8, 0x31424647u, 0x800u, 0x30u, 0x10u, 0xA0u, 120u, 63u, 0u);
    ri_put_buf(ctx, out, tiles);
    put_rom(ctx, out, 0x6D00C, 0x60);
    put_rom(ctx, out, 0x7B998, 0x10);
    ri_buf_free(ctx, tiles);
    return out;
}

static RiData pattern(RiCtx *ctx, size_t offset)
{
    uint32_t width = ri_get_u16(ctx, ctx->rom, offset), height = ri_get_u16(ctx, ctx->rom, offset + 2);
    if (!width || !height) ri_fail(ctx, "invalid scoreboard pattern");
    return ri_span(ctx, ctx->rom, offset, 4 + (size_t)width * height * 2);
}

static size_t rom_pointer(RiCtx *ctx, size_t offset, const char *what)
{
    uint32_t pointer = ri_get_u32(ctx, ctx->rom, offset);
    if (pointer < 0x08000000u || pointer >= 0x09000000u) ri_fail(ctx, "invalid %s pointer", what);
    return pointer - 0x08000000u;
}

static void copy_rom(RiCtx *ctx, RiBuf *target, size_t destination, size_t source, size_t size)
{
    memcpy(target->data + destination, ri_at(ctx, ctx->rom, source, size), size);
}

static RiBuf *results(RiCtx *ctx)
{
    static const size_t rom_ranges[][3] = {
        {0x0000, 0x0E00, 0x62834}, {0x0E00, 0x0E80, 0x6F53C}, {0x0F00, 0x1400, 0x63734},
        {0x1400, 0x1700, 0x655AC}, {0x1800, 0x1B00, 0x6524C}, {0x1E00, 0x2000, 0x64634},
        {0x2800, 0x2A00, 0x66C0C}};
    static const size_t followup_tiles[][3] = {
        {0x0000, 0xAAE3C, 0x0800}, {0x0800, 0xAB7FC, 0x01C0}, {0x1000, 0x63834, 0x0400},
        {0x1400, 0xACAFC, 0x0C00}, {0x2000, 0xABEFC, 0x0800}};
    static const size_t followup_palettes[][3] = {
        {0x180, 0x9B8A0, 0x20}, {0x1A0, 0x627D4, 0x20}, {0x1C0, 0xAACF0, 0x40}};
    static const size_t header_offsets[2] = {0xDB7A8, 0xDB798};
    if (ctx->rom.size < 0xF0000) ri_fail(ctx, "ROM is too small for GP result art");
    /* sub_804EC30 shared HUD glyphs and palette banks, then result uploads. */
    RiBuf *tiles = ri_buf_zero(ctx, 0x4000);
    copy_rom(ctx, tiles, 0, 0x62834, 0x2000);
    for (size_t i = 0; i < sizeof(rom_ranges) / sizeof(rom_ranges[0]); ++i)
        copy_rom(ctx, tiles, rom_ranges[i][0], rom_ranges[i][2], rom_ranges[i][1] - rom_ranges[i][0]);
    RiBuf *palette = ri_buf_zero(ctx, 0x200);
    copy_rom(ctx, palette, 0x140, 0x62774, 0xC0);
    RiData headers[2];
    for (int i = 0; i < 2; ++i) {
        headers[i] = pattern(ctx, header_offsets[i]);
        if (headers[i].size != 16) ri_fail(ctx, "wrong scoreboard header pattern size");
    }
    RiData rows[4];
    for (int i = 0; i < 4; ++i) {
        rows[i] = pattern(ctx, rom_pointer(ctx, 0xDB7B8 + (size_t)i * 4, "scoreboard row"));
        if (rows[i].size > 32) ri_fail(ctx, "scoreboard row pattern too large");
    }
    RiBuf *rank_palette = ri_buf_copy(ctx, palette->data + 0x180, 0x20);
    copy_rom(ctx, rank_palette, 0x12, 0x64C6C, 8);
    RiBuf *followup = ri_buf_copy(ctx, tiles->data, tiles->size);
    for (size_t i = 0; i < 5; ++i)
        copy_rom(ctx, followup, followup_tiles[i][0], followup_tiles[i][1], followup_tiles[i][2]);
    RiBuf *followup_palette = ri_buf_copy(ctx, palette->data, palette->size);
    for (size_t i = 0; i < 3; ++i)
        copy_rom(ctx, followup_palette, followup_palettes[i][0], followup_palettes[i][1], followup_palettes[i][2]);
    RiBuf *followup_map = ri_lz77(ctx, ctx->rom, 0x9BE28), *scroll_map = ri_lz77(ctx, ctx->rom, 0x9BD7C);
    if (followup_map->size != 0x500 || scroll_map->size != 0x300) ri_fail(ctx, "wrong GP followup map size");
    RiBuf *object_tiles = ri_lz77(ctx, ctx->rom, 0x9B8D4);
    if (object_tiles->size != 0x800) ri_fail(ctx, "wrong GP results object asset size");

    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 16, 0x31525047u, 0x4000u, 0x100u, 20u, 2u, 4u, 0x400u, 0x280u, 0x180u, 20u,
             0x800u, 10u, 16u, 16u, 64u, 8u);
    ri_put_buf(ctx, out, tiles);
    ri_put_buf(ctx, out, palette);
    put_rom(ctx, out, 0xEE67E, 40);
    for (int i = 0; i < 2; ++i)
        ri_put(ctx, out, headers[i].data, headers[i].size);
    for (int i = 0; i < 4; ++i)
        ri_u16(ctx, out, (uint32_t)rows[i].size);
    for (int i = 0; i < 4; ++i) {
        ri_put(ctx, out, rows[i].data, rows[i].size);
        ri_put_zero(ctx, out, 32 - rows[i].size);
    }
    ri_put_zero(ctx, out, 0x800); /* background map: dynamic HUD state is native */
    ri_put_buf(ctx, out, followup);
    ri_put_buf(ctx, out, followup_palette);
    ri_put_buf(ctx, out, followup_map);
    ri_put_buf(ctx, out, scroll_map);
    for (int i = 0; i < 20; ++i)
        put_rom(ctx, out, rom_pointer(ctx, 0xDB7E0 + (size_t)i * 4, "GP record-pattern"), 18);
    ri_put_buf(ctx, out, object_tiles);
    put_rom(ctx, out, 0x9B8C0, 0x14);
    put_rom(ctx, out, 0xAAD7C, 0x20);
    put_rom(ctx, out, 0xAAD3C, 0x20);
    for (int32_t phase = 0; phase < 64; ++phase) {
        int32_t signed_phase = phase < 32 ? phase : phase - 64;
        size_t index = (size_t)((256 - signed_phase * 16) & 0x3FF);
        int32_t value = 0x8000 - ri_get_i32(ctx, ctx->rom, 0xE645C + index * 4);
        int32_t factor = (value + (value < 0 ? 1 : 0));
        factor = factor >= 0 ? factor / 2 : -((-factor + 1) / 2); /* Python >> 1 */
        if (factor < 0 || factor > 0xFFFF) ri_fail(ctx, "GP results pulse factor outside uint16");
        ri_u16(ctx, out, (uint32_t)factor);
    }
    put_rom(ctx, out, 0x6590C, 8 * 0x160);
    ri_put_buf(ctx, out, rank_palette);
    /* Revision 7: sub_8022004's five-lap rows (DB7C8) and sub_8020770's time board map. */
    RiData compact[6];
    for (int i = 0; i < 6; ++i) {
        compact[i] = pattern(ctx, rom_pointer(ctx, 0xDB7C8 + (size_t)i * 4, "scoreboard row"));
        if (compact[i].size > 32) ri_fail(ctx, "scoreboard row pattern too large");
    }
    for (int i = 0; i < 6; ++i)
        ri_u16(ctx, out, (uint32_t)compact[i].size);
    for (int i = 0; i < 6; ++i) {
        ri_put(ctx, out, compact[i].data, compact[i].size);
        ri_put_zero(ctx, out, 32 - compact[i].size);
    }
    RiBuf *time_board = ri_lz77(ctx, ctx->rom, 0x9B4CC);
    if (time_board->size != 0x800) ri_fail(ctx, "wrong time board map size");
    ri_put_buf(ctx, out, time_board);
    ri_buf_free(ctx, time_board);
    for (size_t i = 0; i < 8; ++i) {
        const uint32_t identity = ri_get_u32(ctx, ctx->rom, 0xD97B0 + i * 4);
        if (identity > 7) ri_fail(ctx, "invalid record identity mapping");
        ri_u8(ctx, out, identity);
    }
    for (size_t i = 0; i < 5; ++i) {
        const uint8_t option = (uint8_t)ri_get_u8(ctx, ctx->rom, 0xDB6E0 + i);
        if (option > 4) ri_fail(ctx, "invalid results menu option mapping");
        ri_u8(ctx, out, option);
    }
    return out;
}

void ri_group_overlays(RiCtx *ctx)
{
    ri_section(ctx, 20, 1, 1, lightning(ctx));
    ri_section(ctx, 21, 1, 1, finish_banner(ctx));
    ri_section(ctx, 22, 1, 8, results(ctx));
}
