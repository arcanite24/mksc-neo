#include "rom_import_frontend.h"

/* Ports of tools/portable/menu_rom.py, countdown_rom.py, pause_menu_rom.py
 * and gp_failure_menu_rom.py. */

/* Screen-entry rectangle: cells are 16-bit BG entries (tile, h/v flip,
 * palette bank). bank >= 0 forces every cell's palette bank. Shared by
 * pause_sections.rectangle and failure_sections. */
static RiBuf *cell_rectangle(RiCtx *ctx, RiData tiles, RiData palette, const uint32_t *cells,
                             uint32_t columns, uint32_t rows, int32_t bank, int32_t anchor_x,
                             int32_t anchor_y)
{
    uint32_t width = columns * 8, height = rows * 8;
    RiBuf *pixels = ri_buf_zero(ctx, (size_t)width * height * 4);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            uint32_t cell = cells[(y / 8) * columns + x / 8];
            if (cell == UINT32_MAX)
                continue;
            ri_set_pixel(pixels, y * width + x,
                         ri_tile_pixel(ctx, tiles, palette, cell & 1023,
                                       cell & 0x400 ? 7 - x % 8 : x % 8,
                                       cell & 0x800 ? 7 - y % 8 : y % 8,
                                       bank >= 0 ? (uint32_t)bank : cell >> 12, 4));
        }
    RiBuf *image = ri_image_asset(ctx, width, height, pixels, anchor_x, anchor_y);
    ri_buf_free(ctx, pixels);
    return image;
}

/* ---- menu_rom.menu_sections -------------------------------------------- */

#define MENU_BACKGROUND 0x21000u
#define MENU_HEADING 0x21001u
#define MENU_MODE 0x21100u
#define MENU_CLASS 0x21200u
#define MENU_TRIAL 0x21300u
/* spm_loadOtherGfx (0800317C) breadcrumb tabs and sub_80036E4 player banner. */
#define MENU_TAB_GAME 0x26000u
#define MENU_TAB_MODE 0x26010u
#define MENU_TAB_CLASS 0x26020u
#define MENU_TAB_TRIAL 0x26030u
#define MENU_TAB_DRIVER 0x26040u
#define MENU_PLAYER_BANNER 0x26050u
/* sub_8004860 SETTINGS hint and the sub_8005590/sub_80058B0 settings window. */
#define MENU_SETTINGS_HINT 0x26060u
#define MENU_OPTION_CURSOR 0x26061u
#define MENU_OPTION_BRACKET 0x26062u
#define MENU_OPTION_WINDOW 0x26070u
#define MENU_FONT 0x26080u
#define MENU_FRAME 0x26081u

/* menu_rom.option_windows: sub_8005590 BG1 windows, menu font at char base
 * 0x6004000 with colors 251..255, cropped to the used cells; then the native
 * menu font atlas and the Quick Run border nine-slice. */
static void menu_option_windows(RiCtx *ctx)
{
    RiBuf *tiles = ri_buf_zero(ctx, 0x6000);
    static const size_t uploads[3][2] = {{0xa97f8, 0x1000}, {0xa9e2c, 0x2800}, {0xaa56c, 0x4000}};
    for (size_t i = 0; i < 3; ++i)
        ri_fe_place(ctx, tiles, uploads[i][1], ri_view(ri_fe_lz(ctx, uploads[i][0])));
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, palette, 0x1f6, 0x200, ri_fe_rom_slice(ctx, 0xaad30, 0xaad3a));
    static const size_t windows[3][2] = {{1, 0xafbc0}, {5, 0xafaa8}, {2, 0xafcd4}};
    uint32_t border[640] = {0};
    for (size_t w = 0; w < 3; ++w)
    {
        if (windows[w][0] == 2)
        {
            /* VS alone uploads its mushroom-start icon tiles and bank 12. */
            ri_fe_splice(ctx, tiles, 0x5200, 0x5300, ri_fe_rom_slice(ctx, 0xaf914, 0xafa14));
            ri_fe_splice(ctx, palette, 0x180, 0x1a0, ri_fe_rom_slice(ctx, 0x627f4, 0x62814));
        }
        RiBuf *map = ri_fe_lz(ctx, windows[w][1]);
        if (map->size < 1280)
            ri_fail(ctx, "settings window map is short");
        uint32_t cells[640], left = 32, right = 0, top = 20, bottom = 0;
        for (uint32_t i = 0; i < 640; ++i)
        {
            cells[i] = ri_le16(map->data + i * 2);
            if (windows[w][0] == 5)
                border[i] = cells[i];
            if (!cells[i])
                continue;
            if (i % 32 < left)
                left = i % 32;
            if (i % 32 + 1 > right)
                right = i % 32 + 1;
            if (i / 32 < top)
                top = i / 32;
            bottom = i / 32 + 1;
        }
        if (right <= left)
            ri_fail(ctx, "settings window map is empty");
        uint32_t columns = right - left, rows = bottom - top, rect[640];
        for (uint32_t y = 0; y < rows; ++y)
            for (uint32_t x = 0; x < columns; ++x)
                rect[y * columns + x] = cells[(top + y) * 32 + left + x];
        ri_section(ctx, 15, MENU_OPTION_WINDOW + (uint32_t)windows[w][0], 1,
                   cell_rectangle(ctx, ri_view(tiles), ri_view(palette), rect, columns, rows, -1,
                                  (int32_t)left * 8, (int32_t)top * 8));
    }
    uint32_t glyphs[192], frame[9];
    for (uint32_t row = 0; row < 2; ++row)
        for (uint32_t c = 0; c < 96; ++c)
            glyphs[row * 96 + c] = 0xf000u | (0x80u + (c >> 5) * 64 + (c & 31) + row * 32);
    ri_section(ctx, 15, MENU_FONT, 1,
               cell_rectangle(ctx, ri_view(tiles), ri_view(palette), glyphs, 96, 2, -1, 0, 0));
    static const uint32_t frame_rows[3] = {6, 7, 13}, frame_columns[3] = {5, 6, 24};
    for (uint32_t y = 0; y < 3; ++y)
        for (uint32_t x = 0; x < 3; ++x)
            frame[y * 3 + x] = border[frame_rows[y] * 32 + frame_columns[x]];
    ri_section(ctx, 15, MENU_FRAME, 1,
               cell_rectangle(ctx, ri_view(tiles), ri_view(palette), frame, 3, 3, -1, 0, 0));
}

/* menu_rom.tab_sections: tabs at OBJ 0x2400..0x3800; frame head, table
 * graphics and frame tail, colored by one palette bank. */
static RiBuf *menu_tab(RiCtx *ctx, size_t cell, const size_t uploads[3][2], uint32_t bank,
                       size_t colors)
{
    RiBuf *tiles = ri_buf_zero(ctx, 0x4000);
    RiBuf *palette = ri_buf_zero(ctx, 512);
    for (size_t i = 0; i < 3; ++i)
        ri_fe_place(ctx, tiles, uploads[i][1], ri_view(ri_fe_lz(ctx, uploads[i][0])));
    ri_fe_splice(ctx, palette, bank * 32, bank * 32 + 32,
                 ri_fe_rom_slice(ctx, colors, colors + 32));
    return ri_cell_image(ctx, ctx->rom, cell, ri_view(tiles), ri_view(palette));
}

static void menu_tab_sections(RiCtx *ctx)
{
    for (uint32_t player = 0; player < 4; ++player)
    {
        const size_t uploads[3][2] = {{0xb7e00, 0x2400},
                                      {ri_fe_pointer(ctx, 0xd8e9c + player * 8), 0x2500},
                                      {0xb7e40, 0x2700}};
        ri_section(ctx, 15, MENU_PLAYER_BANNER + player, 1,
                   menu_tab(ctx, 0xc8634, uploads, 2, ri_fe_pointer(ctx, 0xd8ea0 + player * 8)));
    }
    static const struct
    {
        uint32_t base;
        size_t table;
        uint32_t count;
        size_t cell, offset;
        uint32_t bank;
    } tabs[4] = {{MENU_TAB_GAME, 0xd8ebc, 4, 0xc8644, 0x3000, 4},
                 {MENU_TAB_MODE, 0xd8edc, 6, 0xc864c, 0x3200, 5},
                 {MENU_TAB_CLASS, 0xd8f0c, 3, 0xc8654, 0x3400, 6},
                 {MENU_TAB_TRIAL, 0xd8f24, 2, 0xc8654, 0x3400, 6}};
    for (size_t t = 0; t < 4; ++t)
        for (uint32_t index = 0; index < tabs[t].count; ++index)
        {
            size_t entry = tabs[t].table + (size_t)index * 8;
            if (ri_get_u32(ctx, ctx->rom, entry) == 0)
                continue; /* Action 4 has no tab. */
            const size_t uploads[3][2] = {{0xb7564, tabs[t].offset},
                                          {ri_fe_pointer(ctx, entry), tabs[t].offset + 0x80},
                                          {0xb7594, tabs[t].offset + 0x180}};
            ri_section(
                ctx, 15, tabs[t].base + index, 1,
                menu_tab(ctx, tabs[t].cell, uploads, tabs[t].bank, ri_fe_pointer(ctx, entry + 4)));
        }
    for (uint32_t driver = 0; driver < 8; ++driver)
    {
        /* 08002EA4 swaps faces 2 and 3; the frame shares the 1-P tab colors. */
        size_t face = 0x63834 + (size_t)(driver == 2 ? 3 : driver == 3 ? 2 : driver) * 128;
        RiBuf *tiles = ri_buf_zero(ctx, 0x4000);
        ri_fe_splice(ctx, tiles, 0x2c00, 0x2c80, ri_fe_rom_slice(ctx, face, face + 128));
        ri_fe_place(ctx, tiles, 0x3600, ri_view(ri_fe_lz(ctx, 0xb8534)));
        RiBuf *palette = ri_buf_zero(ctx, 512);
        ri_fe_splice(ctx, palette, 0x60, 0x80, ri_fe_rom_slice(ctx, 0x627d4, 0x627f4));
        size_t colors = ri_fe_pointer(ctx, 0xd8ec0);
        ri_fe_splice(ctx, palette, 0x80, 0xa0, ri_fe_rom_slice(ctx, colors, colors + 32));
        ri_section(ctx, 15, MENU_TAB_DRIVER + driver, 1,
                   ri_cell_image(ctx, ctx->rom, 0xc865c, ri_view(tiles), ri_view(palette)));
    }
}

static void menu_heading_sections(RiCtx *ctx, RiBuf *motion, const RiBuf *tiles,
                                  const RiBuf *palette)
{
    static const size_t tables[4] = {0xd90f8u, 0xd9230u, 0xd95bcu, 0xd9588u};
    static const uint32_t strip_ids[4] = {0x21001u, 0x22000u, 0x23010u, 0x23011u};
    ri_words(ctx, motion, 2, UINT32_C(0x3147484d), 4u);
    for (unsigned group = 0u; group < 4u; ++group)
    {
        const size_t full = ri_fe_pointer(ctx, tables[group]);
        const unsigned count = ri_get_u16(ctx, ctx->rom, full);
        if (count < 1u || count > 16u)
            ri_fail(ctx, "heading letter count outside1..16");
        RiBuf *strip = ri_cell_image(ctx, ctx->rom, full, ri_view(tiles), ri_view(palette));
        if (group == 1u)
            ri_fe_move_anchor(ctx, strip, 128, 16);
        else if (group >= 2u)
            ri_fe_set_anchor(ctx, strip, 0, 0);
        uint32_t digest = UINT32_C(2166136261);
        for (size_t i = 0u; i < strip->size; ++i)
            digest = (digest ^ strip->data[i]) * UINT32_C(16777619);
        ri_words(ctx, motion, 3, count, strip_ids[group], digest);
        ri_buf_free(ctx, strip);
        for (unsigned row = 0u; row < count; ++row)
        {
            const size_t part = full + 2u + (count - 1u - row) * 6u;
            const unsigned a = ri_get_u16(ctx, ctx->rom, part),
                           b = ri_get_u16(ctx, ctx->rom, part + 2u) & 511u;
            const int x = b <= 255u ? (int)b : -(int)(b ^ 511u);
            const int y = (a & 255u) < 128u ? (int)(a & 255u) : (int)(a & 255u) - 256;
            const uint32_t image_id = 0x29000u + group * 0x20u + row;
            ri_u32(ctx, motion, image_id);
            ri_i16(ctx, motion, 128 + x);
            ri_i16(ctx, motion, 24 + y);
            ri_section(ctx, 15u, image_id, 1u,
                       ri_cell_image(ctx, ctx->rom,
                                     ri_fe_pointer(ctx, tables[group] + 4u + row * 4u),
                                     ri_view(tiles), ri_view(palette)));
        }
    }
}

void ri_fe_menu_sections(RiCtx *ctx)
{
    /* sub_8004584 shared art; its rotating wheel becomes a native decoration. */
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, palette, 0xa0, 0xc0, ri_fe_rom_slice(ctx, 0xae03c, 0xae05c));
    ri_fe_splice(ctx, palette, 0xe0, 0x120, ri_fe_rom_slice(ctx, 0x7cd48, 0x7cd88));
    RiBuf *tiles = ri_fe_lz(ctx, 0x7d1e8);
    RiBuf *pattern = ri_tile_map(ctx, ri_view(tiles), ri_view(palette),
                                 ri_view(ri_fe_lz(ctx, 0x7da94)), 32, 32, 4);
    RiBuf *pixels = ri_fe_crop_screen(ctx, pattern, 256);
    /* The wheel is source cell art, not a reconstructed affine display layer. */
    RiBuf *object_tiles = ri_buf_zero(ctx, 32768);
    RiBuf *object_palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, object_palette, 0, 64, ri_fe_rom_slice(ctx, 0xaf2c0, 0xaf300));
    ri_fe_splice(ctx, object_palette, 0x60, 0x80, ri_fe_rom_slice(ctx, 0xaf300, 0xaf320));
    static const size_t uploads[3][2] = {{0xaf320, 0}, {0xc2fa8, 0x1000}, {0xaf778, 0x6c00}};
    for (size_t i = 0; i < 3; ++i)
        ri_fe_place(ctx, object_tiles, uploads[i][1], ri_view(ri_fe_lz(ctx, uploads[i][0])));
    RiBuf *wheel =
        ri_cell_image(ctx, ctx->rom, 0xc860c, ri_view(object_tiles), ri_view(object_palette));
    int32_t w = (int32_t)ri_le16(wheel->data + 12), h = (int32_t)ri_le16(wheel->data + 14);
    for (int32_t y = 0; y < 160; ++y)
        for (int32_t x = 0; x < 240; ++x)
        {
            int32_t sx = ri_fe_floordiv(x - 176, 2) + w / 2, sy = ri_fe_floordiv(y - 92, 2) + h / 2;
            if (sx < 0 || sx >= w || sy < 0 || sy >= h)
                continue;
            uint32_t pixel = ri_le32(wheel->data + 32 + (size_t)(sy * w + sx) * 4);
            if (pixel)
                ri_set_pixel(pixels, (size_t)(y * 240 + x), pixel);
        }
    ri_section(ctx, 15, MENU_BACKGROUND, 1, ri_image_asset(ctx, 240, 160, pixels, 0, 0));
    ri_section(ctx, 15, 0x21002, 1, ri_image_asset(ctx, 256, 256, pattern, 0, 0));
    RiBuf *back_pattern = ri_tile_map(ctx, ri_view(tiles), ri_view(palette),
                                      ri_view(ri_fe_lz(ctx, 0x7db98)), 32, 32, 4);
    /* 4584 BG2CNT448E: byte-indexed affine map over contiguous 8bpp tiles. */
    RiBuf *affine_tiles = ri_buf_zero(ctx, 0x4000);
    RiBuf *affine_first = ri_fe_lz(ctx, 0xae180), *affine_second = ri_fe_lz(ctx, 0xae7c8);
    if (affine_first->size != 0x2000u || affine_second->size != 0x2000u)
        ri_fail(ctx, "invalid menu affine graphics size");
    ri_fe_place(ctx, affine_tiles, 0, ri_view(affine_first));
    ri_fe_place(ctx, affine_tiles, 0x2000, ri_view(affine_second));
    RiBuf *affine_cells = ri_fe_lz(ctx, 0xaee04);
    if (affine_cells->size != 1024u)
        ri_fail(ctx, "invalid menu affine map size");
    RiBuf *affine = ri_buf_zero(ctx, 256u * 256u * 4u);
    for (uint32_t y = 0u; y < 256u; ++y)
        for (uint32_t x = 0u; x < 256u; ++x)
            ri_set_pixel(affine, y * 256u + x,
                         ri_tile_pixel(ctx, ri_view(affine_tiles), ri_view(palette),
                                       affine_cells->data[(y / 8u) * 32u + x / 8u], x & 7u, y & 7u,
                                       0u, 8));
    ri_section(ctx, 15, 0x21003, 1, ri_image_asset(ctx, 256, 256, affine, 0, 0));
    ri_section(
        ctx, 15, 0x21004, 1,
        ri_cell_image(ctx, ctx->rom, 0xc860c, ri_view(object_tiles), ri_view(object_palette)));
    ri_section(
        ctx, 15, 0x21005, 1,
        ri_cell_image(ctx, ctx->rom, 0xc8614, ri_view(object_tiles), ri_view(object_palette)));
    ri_section(ctx, 15, 0x21006, 1, ri_image_asset(ctx, 256, 256, back_pattern, 0, 0));
    ri_section(
        ctx, 15, MENU_HEADING, 1,
        ri_cell_image(ctx, ctx->rom, 0xc86f4, ri_view(object_tiles), ri_view(object_palette)));
    /* sub_80042C4 and sub_800441C: CellData, graphics, palette, x/y, action.
     * Normalize each medallion separately, including the common rim. The
     * native single-player shell also exposes Battle: its action-3 medallion
     * is row two of 42C4's multiplayer table; local VS is row zero of its
     * two-row link table D905C. */
    static const struct
    {
        uint32_t base;
        size_t table;
        uint32_t first, count;
    } groups[9] = {
        {MENU_MODE, 0xd8fe4, 0, 3},     {MENU_CLASS, 0xd9094, 0, 3},    {MENU_TRIAL, 0xd90d0, 0, 2},
        {MENU_MODE + 3, 0xd9020, 2, 1}, {MENU_MODE + 4, 0xd905c, 0, 1}, {0x21400, 0xd8f94, 0, 1},
        {0x21401, 0xd8fa8, 0, 1},       {0x21402, 0xd8fbc, 0, 1},       {0x21403, 0xd8fd0, 0, 1}};
    for (size_t g = 0; g < 9; ++g)
        for (uint32_t slot = 0; slot < groups[g].count; ++slot)
        {
            uint32_t index = groups[g].first + slot;
            size_t entry = groups[g].table + (size_t)index * 20;
            size_t cell = ri_fe_pointer(ctx, entry), graphics = ri_fe_pointer(ctx, entry + 4);
            size_t colors = ri_fe_pointer(ctx, entry + 8);
            size_t offset = 0x3c00 + (size_t)index * 0x800;
            ri_fe_place(ctx, object_tiles, offset, ri_view(ri_fe_lz(ctx, 0xb69ec)));
            ri_fe_place(ctx, object_tiles, offset + 0x100, ri_view(ri_fe_lz(ctx, graphics)));
            ri_fe_place(ctx, object_tiles, offset + 0x700, ri_view(ri_fe_lz(ctx, 0xb6a5c)));
            ri_fe_splice(ctx, object_palette, 0x100 + index * 32, 0x120 + index * 32,
                         ri_fe_rom_slice(ctx, colors, colors + 32));
            ri_section(
                ctx, 15, groups[g].base + slot, 1,
                ri_cell_image(ctx, ctx->rom, cell, ri_view(object_tiles), ri_view(object_palette)));
        }
    static const uint32_t hints[3][2] = {{MENU_SETTINGS_HINT, 0xc8758},
                                         {MENU_OPTION_CURSOR, 0xc87a0},
                                         {MENU_OPTION_BRACKET, 0xc8784}};
    for (size_t i = 0; i < 3; ++i)
        ri_section(ctx, 15, hints[i][0], 1,
                   ri_cell_image(ctx, ctx->rom, hints[i][1], ri_view(object_tiles),
                                 ri_view(object_palette)));
    menu_option_windows(ctx);
    menu_tab_sections(ctx);
    /* FEM1: source motion metadata stays separate from editable pixel art. */
    RiBuf *motion = ri_buf(ctx);
    ri_words(ctx, motion, 4, 0x314d4546u, 9u, 14u, 5u);
    for (unsigned slot = 0u; slot < 9u; ++slot)
    {
        ri_put(ctx, motion, ri_at(ctx, ctx->rom, 0xd91e8u + slot * 4u, 4u), 4u);
        ri_put(ctx, motion, ri_at(ctx, ctx->rom, 0xd9130u + slot * 20u + 12u, 4u), 4u);
    }
    static const size_t motion_tables[8] = {0xd8fe4u, 0xd9094u, 0xd90d0u, 0xd905cu,
                                            0xd8f94u, 0xd8fa8u, 0xd8fbcu, 0xd8fd0u};
    static const unsigned motion_counts[8] = {3u, 3u, 2u, 2u, 1u, 1u, 1u, 1u};
    for (unsigned table = 0u; table < 8u; ++table)
        for (unsigned row = 0u; row < motion_counts[table]; ++row)
            ri_put(ctx, motion, ri_at(ctx, ctx->rom, motion_tables[table] + row * 20u + 12u, 4u),
                   4u);
    ri_put(ctx, motion, ri_at(ctx, ctx->rom, 0xd8e88u, 20u), 20u);
    ri_put(ctx, motion, ri_at(ctx, ctx->rom, 0xe645cu, 4096u), 4096u);
    for (unsigned slot = 0; slot < 5; ++slot)
        ri_put(ctx, motion, ri_at(ctx, ctx->rom, 0xd94fcu + slot * 20u + 12u, 4u), 4u);
    menu_heading_sections(ctx, motion, object_tiles, object_palette);
    ri_section(ctx, 92, 1, 3, motion);
}

/* ---- countdown_rom.countdown_sections ---------------------------------- */

#define START_IMAGE 0x27000u

RiBuf *ri_fe_finish_lakitu_program(RiCtx *ctx, size_t cells[8])
{
    uint16_t banks[32] = {0}, durations[32] = {0};
    uint32_t elapsed = 0u, boundaries[3] = {0}, seen = 0u, row_count = 0u, bank = 0u;
    size_t cell = 0u;
    int ended = 0;
    for (unsigned i = 0u; i < 8u; ++i)
        cells[i] = 0u;
    for (unsigned turn = 0u; turn < 128u; ++turn)
    {
        size_t address = 0xef578u + turn * 8u;
        uint32_t command = ri_get_u32(ctx, ctx->rom, address),
                 argument = ri_get_u32(ctx, ctx->rom, address + 4u);
        if (command == 0x43u)
        {
            unsigned phase;
            if (argument == 0x0805045du)
                phase = 0u;
            else if (argument == 0x08050df9u)
                phase = 1u;
            else if (argument == 0x08050755u)
                phase = 2u;
            else
                ri_fail(ctx, "invalid finish Lakitu callback");
            if (seen & (1u << phase))
                ri_fail(ctx, "duplicate finish Lakitu callback");
            seen |= 1u << phase;
            boundaries[phase] = elapsed;
        }
        else if (command == 0x11u)
        {
            if (argument < 0x08000000u || (uint64_t)argument - 0x08000000u >= ctx->rom.size)
                ri_fail(ctx, "finish Lakitu cell outside ROM");
            cell = argument - 0x08000000u;
        }
        else if (command == 0x41u)
        {
            bank = argument;
            if (bank < 21u || bank > 28u || !cell ||
                (cells[bank - 21u] && cells[bank - 21u] != cell))
                ri_fail(ctx, "invalid finish Lakitu art bank");
            cells[bank - 21u] = cell;
        }
        else if (command == 0xf4u)
        {
            if (argument != 0x0804fea1u && argument != 0x08050c45u)
                ri_fail(ctx, "invalid finish Lakitu art callback");
        }
        else if (command == 0xf1u)
        {
            if (!bank || !argument || argument > 65535u || elapsed + argument >= 65535u)
                ri_fail(ctx, "invalid finish Lakitu hold");
            if (row_count && banks[row_count - 1u] == bank)
                durations[row_count - 1u] += (uint16_t)argument;
            else
            {
                if (row_count >= 32u)
                    ri_fail(ctx, "too many finish Lakitu frames");
                banks[row_count] = (uint16_t)bank;
                durations[row_count++] = (uint16_t)argument;
            }
            elapsed += argument;
        }
        else if (command == 0x42u)
        {
            ended = 1;
            break;
        }
        else
            ri_fail(ctx, "unsupported finish Lakitu command");
    }
    if (!ended || seen != 7u || boundaries[0] != 0u || boundaries[1] < 20u ||
        boundaries[1] >= boundaries[2] || boundaries[2] >= elapsed || !row_count)
        ri_fail(ctx, "invalid finish Lakitu phase schedule");
    for (unsigned i = 0u; i < 8u; ++i)
        if (!cells[i])
            ri_fail(ctx, "missing finish Lakitu art bank");
    RiBuf *out = ri_buf(ctx);
    ri_u32(ctx, out, 0x314b4c46u);
    ri_u16(ctx, out, row_count);
    ri_u16(ctx, out, boundaries[1]);
    ri_u16(ctx, out, boundaries[2] - boundaries[1]);
    ri_u16(ctx, out, elapsed - boundaries[2]);
    ri_u32(ctx, out, 0u);
    for (unsigned i = 0u; i < row_count; ++i)
    {
        ri_u8(ctx, out, banks[i]);
        ri_u8(ctx, out, 0u);
        ri_u16(ctx, out, durations[i]);
    }
    return out;
}

/* One OBJ part: origin, size, first tile; parts are drawn last to first. */
typedef struct Part
{
    uint32_t x, y, width, height, tile;
} Part;

static RiBuf *draw_parts(RiCtx *ctx, RiData tiles, RiData palette, const Part *parts, size_t count,
                         uint32_t width, uint32_t height, int32_t anchor_x, int32_t anchor_y)
{
    RiBuf *pixels = ri_buf_zero(ctx, (size_t)width * height * 4);
    for (size_t n = count; n-- > 0;)
    {
        const Part *part = &parts[n];
        for (uint32_t y = 0; y < part->height; ++y)
            for (uint32_t x = 0; x < part->width; ++x)
            {
                uint32_t pixel = ri_tile_pixel(ctx, tiles, palette,
                                               part->tile + y / 8 * (part->width / 8) + x / 8,
                                               x % 8, y % 8, 0, 4);
                if (pixel)
                    ri_set_pixel(pixels, (part->y + y) * width + part->x + x, pixel);
            }
    }
    RiBuf *image = ri_image_asset(ctx, width, height, pixels, anchor_x, anchor_y);
    ri_buf_free(ctx, pixels);
    return image;
}

void ri_fe_countdown_sections(RiCtx *ctx)
{
    /* sub_804FEA0 states 0..6 alternate the body and replace the lamp strip. */
    static const size_t states[7][2] = {{0x68F8C, 0x6938C}, {0x6918C, 0x6948C}, {0x68F8C, 0x6948C},
                                        {0x6918C, 0x6958C}, {0x68F8C, 0x6958C}, {0x6918C, 0x6968C},
                                        {0x68F8C, 0x6968C}};
    /* Source start-light composition: 8x32 lamp, 32x32 body, 8x16 overlay. */
    static const Part start_parts[3] = {{0, 5, 8, 32, 16}, {4, 0, 32, 32, 0}, {4, 0, 8, 16, 20}};
    static const Part recovery_parts[3] = {
        {5, 0, 8, 32, 16}, {5, 32, 8, 32, 20}, {0, 12, 32, 32, 0}};
    static const Part wrong_way_parts[2] = {{1, 20, 32, 16, 16}, {0, 0, 32, 32, 0}};
    RiData palette = ri_fe_rom_slice(ctx, 0x66ECC, 0x66EEC);
    for (uint32_t index = 0; index < 7; ++index)
    {
        size_t body = states[index][0], lamps = states[index][1];
        RiBuf *tiles = ri_buf(ctx);
        RiData part = ri_fe_rom_slice(ctx, body, body + 0x200);
        ri_put(ctx, tiles, part.data, part.size);
        if (index == 0)
        {
            part = ri_fe_rom_slice(ctx, lamps, lamps + 0x100);
            ri_put(ctx, tiles, part.data, part.size);
        }
        else
        {
            part = ri_fe_rom_slice(ctx, lamps, lamps + 0x80);
            ri_put(ctx, tiles, part.data, part.size);
            part = ri_fe_rom_slice(ctx, 0x6940C, 0x6948C);
            ri_put(ctx, tiles, part.data, part.size);
        }
        if (tiles->size != 0x300 || palette.size != 32)
            ri_fail(ctx, "truncated Lakitu start graphics");
        ri_section(ctx, 15, START_IMAGE + index, 1,
                   draw_parts(ctx, ri_view(tiles), palette, start_parts, 3, 36, 37, 0, -5));
        ri_buf_free(ctx, tiles);
    }
    /* 804FEA0 banks19/20, 807C830 recovery composition. */
    static const size_t recovery_bodies[2] = {0x6B18C, 0x6B38C};
    for (uint32_t bank = 0; bank < 2; ++bank)
    {
        RiBuf *tiles = ri_buf(ctx);
        RiData part = ri_fe_rom_slice(ctx, recovery_bodies[bank], recovery_bodies[bank] + 512);
        ri_put(ctx, tiles, part.data, part.size);
        part = ri_fe_rom_slice(ctx, 0x6B08C, 0x6B18C);
        ri_put(ctx, tiles, part.data, part.size);
        ri_section(ctx, 15, START_IMAGE + 16 + bank, 1,
                   draw_parts(ctx, ri_view(tiles), palette, recovery_parts, 3, 32, 64, -7, -28));
        ri_buf_free(ctx, tiles);
    }
    /* EF0D0 wrong-way banks15..18, 807C820: sign32x16 then body32x32. */
    static const size_t wrong_way[4] = {0x6A48C, 0x6A78C, 0x6AA8C, 0x6AD8C};
    for (uint32_t bank = 0; bank < 4; ++bank)
    {
        RiData tiles = ri_fe_rom_slice(ctx, wrong_way[bank], wrong_way[bank] + 768);
        if (tiles.size != 768)
            ri_fail(ctx, "truncated Lakitu wrong-way graphics");
        ri_section(ctx, 15, START_IMAGE + 32 + bank, 1,
                   draw_parts(ctx, tiles, palette, wrong_way_parts, 2, 33, 36, -16, -16));
    }
    static const size_t finish_banks[8][2] = {
        {0x6b58c, 0x6bb8c}, {0x6b78c, 0x6bc8c}, {0x6b78c, 0x6bd8c}, {0x6b78c, 0x6be8c},
        {0x6b58c, 0x6bf8c}, {0x6b98c, 0x6c08c}, {0x6b98c, 0x6c18c}, {0x6b98c, 0x6c28c}};
    size_t cells[8];
    RiBuf *program = ri_fe_finish_lakitu_program(ctx, cells);
    ri_buf_free(ctx, program);
    RiBuf *colors = ri_buf_zero(ctx, 512u);
    ri_fe_place(ctx, colors, 224u, palette);
    for (unsigned i = 0u; i < 8u; ++i)
    {
        RiBuf *tiles = ri_buf_zero(ctx, 32768u);
        ri_fe_place(ctx, tiles, 0x2800u,
                    ri_fe_rom_slice(ctx, finish_banks[i][0], finish_banks[i][0] + 512u));
        ri_fe_place(ctx, tiles, 0x2a00u,
                    ri_fe_rom_slice(ctx, finish_banks[i][1], finish_banks[i][1] + 256u));
        ri_section(ctx, 15u, START_IMAGE + 48u + i, 1u,
                   ri_cell_image(ctx, ctx->rom, cells[i], ri_view(tiles), ri_view(colors)));
        ri_buf_free(ctx, tiles);
    }
    RiData replay_tiles = ri_fe_rom_slice(ctx, 0x6500cu, 0x6524cu);
    RiData replay_palette = ri_fe_rom_slice(ctx, 0x627d4u, 0x627f4u);
    if (replay_tiles.size != 576u || replay_palette.size != 32u)
        ri_fail(ctx, "truncated Replay Mode graphics");
    uint32_t replay_cells[18];
    for (unsigned i = 0u; i < 18u; ++i)
        replay_cells[i] = i;
    ri_section(ctx, 15u, 0x27040u, 1u,
               cell_rectangle(ctx, replay_tiles, replay_palette, replay_cells, 9u, 2u, -1, 0, 0));
    RiData dial_tiles = ri_fe_rom_slice(ctx, 0x66c0cu, 0x66e4cu);
    RiData dial_palette = ri_fe_rom_slice(ctx, 0x627b4u, 0x627d4u);
    uint32_t dial_cells[30];
    for (unsigned i = 0u; i < 30u; ++i)
    {
        unsigned cell = ri_get_u16(ctx, ctx->rom, 0xee76cu + i * 2u);
        if (cell && ((cell >> 12u) != 12u || (cell & 1023u) < 0x140u || (cell & 1023u) >= 0x152u))
            ri_fail(ctx, "invalid replay speedometer cell");
        dial_cells[i] = cell ? ((cell & 0xc00u) | ((cell & 1023u) - 0x140u)) : UINT32_MAX;
    }
    ri_section(ctx, 15u, 0x27041u, 1u,
               cell_rectangle(ctx, dial_tiles, dial_palette, dial_cells, 6u, 5u, 0, 0, 0));
    RiBuf *needle_tiles = ri_buf_zero(ctx, 32768u), *needle_colors = ri_buf_zero(ctx, 512u);
    ri_fe_place(ctx, needle_tiles, 0x7800u, ri_fe_rom_slice(ctx, 0x6640cu, 0x66c0cu));
    ri_fe_place(ctx, needle_colors, 224u, ri_fe_rom_slice(ctx, 0x66eccu, 0x66eecu));
    ri_section(
        ctx, 15u, 0x27042u, 1u,
        ri_cell_image(ctx, ctx->rom, 0xee764u, ri_view(needle_tiles), ri_view(needle_colors)));
    ri_buf_free(ctx, needle_tiles);
    ri_buf_free(ctx, needle_colors);
}

/* ---- pause_menu_rom.pause_sections ------------------------------------- */

#define PAUSE_PANEL 0x24000u
#define PAUSE_LABEL 0x24100u
#define PAUSE_SELECTED_LABEL RI_FE_PAUSE_SELECTED_LABEL
#define PAUSE_CURSOR 0x24300u
#define PANEL_WIDTH 10u /* panel(height, width=10) */

typedef struct PauseArt
{
    RiBuf *tiles;
    RiBuf *palette;
} PauseArt;

/* pause_sections.panel(height, width=10): the bordered window. */
static RiBuf *pause_panel(RiCtx *ctx, const PauseArt *art, uint32_t height)
{
    uint32_t cells[PANEL_WIDTH * 16];
    if (height < 2 || height > 16)
        ri_fail(ctx, "invalid pause panel height");
    for (uint32_t y = 0; y < height; ++y)
    {
        uint32_t *row = &cells[y * PANEL_WIDTH];
        if (y == 0 || y == height - 1)
        {
            row[0] = 0xf001;
            row[1] = 0xf002;
            for (uint32_t x = 2; x < PANEL_WIDTH - 2; ++x)
                row[x] = 0xf022;
            row[PANEL_WIDTH - 2] = 0xf402;
            row[PANEL_WIDTH - 1] = 0xf401;
            if (y)
                for (uint32_t x = 0; x < PANEL_WIDTH; ++x)
                    row[x] ^= 0x800;
        }
        else
        {
            uint32_t edge = y == 1 ? 0xf021 : y == height - 2 ? 0xf821 : 0xf020;
            row[0] = edge;
            for (uint32_t x = 1; x < PANEL_WIDTH - 1; ++x)
                row[x] = 0;
            row[PANEL_WIDTH - 1] = edge ^ 0x400;
        }
    }
    return cell_rectangle(ctx, ri_view(art->tiles), ri_view(art->palette), cells, PANEL_WIDTH,
                          height, -1, 0, 0);
}

/* pause_sections.label(action, row): normal then selected (bank 14). */
static RiBuf *failure_indexed_rectangle(RiCtx *ctx, RiData tiles, RiData palette,
                                        const uint32_t *cells, uint32_t columns, uint32_t rows);
static void pause_label(RiCtx *ctx, const PauseArt *art, uint32_t action, uint32_t row)
{
    uint32_t pattern = ri_get_u8(ctx, ctx->rom, 0xddee8 + action);
    uint32_t cells[14];
    for (size_t i = 0; i < 14; ++i)
        cells[i] = ri_get_u16(ctx, ctx->rom, 0xddf58 + (size_t)pattern * 28 + i * 2);
    ri_section(
        ctx, 15, PAUSE_LABEL + row, 1,
        cell_rectangle(ctx, ri_view(art->tiles), ri_view(art->palette), cells, 7, 2, -1, 0, 0));
    for (unsigned i = 0u; i < 14u; ++i)
        cells[i] = (cells[i] & 0xfffu) | 0xe000u;
    ri_section(
        ctx, 15, PAUSE_SELECTED_LABEL + row, 1,
        failure_indexed_rectangle(ctx, ri_view(art->tiles), ri_view(art->palette), cells, 7u, 2u));
}

void ri_fe_pause_sections(RiCtx *ctx)
{
    /* Single-player tile uploads and the two pause palette banks. */
    static const size_t uploads[3][2] = {
        {0xaae3c, 0xab63c}, {0xab63c, 0xab7fc}, {0xad6fc, 0xad8bc}};
    PauseArt art;
    art.tiles = ri_buf(ctx);
    for (size_t i = 0; i < 3; ++i)
    {
        RiData part = ri_fe_rom_slice(ctx, uploads[i][0], uploads[i][1]);
        ri_put(ctx, art.tiles, part.data, part.size);
    }
    art.palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, art.palette, 0x1c0, 0x200, ri_fe_rom_slice(ctx, 0xaacf0, 0xaad30));
    /* Descriptors: x, y, width, height, count, pad, item pointer. */
    static const struct
    {
        size_t address;
        uint8_t layout[5];
        size_t items;
    } layouts[3] = {{0xddf18, {10, 4, 10, 12, 5}, 0xddf10}, /* Time Trial */
                    {0xddef4, {10, 4, 10, 8, 3}, 0xddef1},  /* GP with lives */
                    {0xddf04, {10, 5, 10, 6, 2}, 0xddf00}}; /* GP without lives */
    for (size_t i = 0; i < 3; ++i)
    {
        for (size_t b = 0; b < 5; ++b)
            if (ri_get_u8(ctx, ctx->rom, layouts[i].address + b) != layouts[i].layout[b])
                ri_fail(ctx, "invalid pause layout");
        if (ri_get_u32(ctx, ctx->rom, layouts[i].address + 8) != 0x8000000u + layouts[i].items)
            ri_fail(ctx, "invalid pause layout");
    }
    if (ri_get_u8(ctx, ctx->rom, 0xddef1) != 0 || ri_get_u8(ctx, ctx->rom, 0xddef2) != 5 ||
        ri_get_u8(ctx, ctx->rom, 0xddef3) != 1 || ri_get_u8(ctx, ctx->rom, 0xddf00) != 0 ||
        ri_get_u8(ctx, ctx->rom, 0xddf01) != 1)
        ri_fail(ctx, "invalid GP pause items");
    for (uint32_t index = 0; index < 3; ++index)
        ri_section(ctx, 15, PAUSE_PANEL + index, 1,
                   pause_panel(ctx, &art, layouts[index].layout[3]));
    /* Time Trial rows are the action order Continue, Retry, Track, Character, Quit. */
    for (uint32_t row = 0; row < 5; ++row)
        pause_label(ctx, &art, ri_get_u8(ctx, ctx->rom, 0xddf10 + row), row);
    pause_label(ctx, &art, 5, 5); /* GP Retry */
    uint32_t cursor[2] = {ri_get_u16(ctx, ctx->rom, 0xde054), ri_get_u16(ctx, ctx->rom, 0xde056)};
    ri_section(
        ctx, 15, PAUSE_CURSOR, 1,
        cell_rectangle(ctx, ri_view(art.tiles), ri_view(art.palette), cursor, 1, 2, -1, 0, 0));
}

/* ---- gp_failure_menu_rom.failure_sections ------------------------------ */

#define GP_FAILURE_MENU 0x25000u

static RiBuf *failure_indexed_rectangle(RiCtx *ctx, RiData tiles, RiData palette,
                                        const uint32_t *cells, uint32_t columns, uint32_t rows)
{
    const uint32_t width = columns * 8u, height = rows * 8u;
    RiBuf *image = ri_buf(ctx);
    ri_put(ctx, image, "MKSCIMG2", 8u);
    ri_u16(ctx, image, 32u);
    ri_u16(ctx, image, 2u);
    ri_u16(ctx, image, (uint16_t)width);
    ri_u16(ctx, image, (uint16_t)height);
    ri_u16(ctx, image, 0u);
    ri_u16(ctx, image, 0u);
    ri_u32(ctx, image, 32u);
    ri_u32(ctx, image, 32u);
    ri_u32(ctx, image, 32u + width * height + 64u);
    for (uint32_t y = 0u; y < height; ++y)
        for (uint32_t x = 0u; x < width; ++x)
        {
            const uint32_t cell = cells[(y / 8u) * columns + x / 8u], bank = cell >> 12u;
            if (cell == 0u)
            {
                ri_u8(ctx, image, 255u);
                continue;
            }
            if (bank != 14u && bank != 15u)
                ri_fail(ctx, "invalid GP failure palette bank");
            const uint32_t sx = cell & 0x400u ? 7u - x % 8u : x % 8u,
                           sy = cell & 0x800u ? 7u - y % 8u : y % 8u;
            const uint32_t color =
                (ri_get_u8(ctx, tiles, (cell & 1023u) * 32u + sy * 4u + sx / 2u) >>
                 (4u * (sx & 1u))) &
                15u;
            ri_u8(ctx, image, (uint8_t)(color ? 16u * (bank - 14u) + color : 255u));
        }
    ri_put(ctx, image, palette.data + 0x1c0u, 64u);
    return image;
}

void ri_fe_failure_sections(RiCtx *ctx)
{
    /* Source uploads use tile indices 0x100..0x15f and palette banks E/F. */
    RiBuf *tiles = ri_buf_zero(ctx, 0x2000);
    RiData part = ri_fe_rom_slice(ctx, 0xaae3c, 0xab63c);
    ri_put(ctx, tiles, part.data, part.size);
    part = ri_fe_rom_slice(ctx, 0xada7c, 0xade7c);
    ri_put(ctx, tiles, part.data, part.size);
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, palette, 0x1c0, 0x200, ri_fe_rom_slice(ctx, 0xaacf0, 0xaad30));
    static const size_t patterns[2] = {0xdb268, 0xdb348};
    for (uint32_t page = 0; page < 2; ++page)
    {
        size_t address = patterns[page];
        uint32_t width = ri_get_u16(ctx, ctx->rom, address),
                 height = ri_get_u16(ctx, ctx->rom, address + 2);
        if (width != 12 || height != 6)
            ri_fail(ctx, "invalid GP failure menu pattern");
        uint32_t original[72];
        for (size_t i = 0; i < 72; ++i)
            original[i] = ri_get_u16(ctx, ctx->rom, address + 4 + i * 2);
        for (uint32_t choice = 0; choice < 2; ++choice)
        {
            uint32_t cells[72];
            for (size_t i = 0; i < 72; ++i)
                cells[i] = original[i];
            uint32_t row = page == 0 ? 1 + choice * 2 : 3;
            uint32_t column = page == 0 ? 3 : 2 + choice * 5;
            uint32_t count = page == 0 ? 7 : 3;
            for (uint32_t y = row; y < row + 2; ++y)
                for (uint32_t x = column; x < column + count; ++x)
                    cells[y * width + x] = (cells[y * width + x] & 0xfff) | 0xe000;
            cells[row * width + column - 1] = 0xf103;
            cells[(row + 1) * width + column - 1] = 0xf903;
            /* Pages: retry, quit, confirm YES, confirm NO. */
            ri_section(ctx, 15, GP_FAILURE_MENU + page * 2 + choice, 1,
                       failure_indexed_rectangle(ctx, ri_view(tiles), ri_view(palette), cells,
                                                 width, height));
        }
    }
    /* sub_801E7C8 states 31/32: normalize Game Over directly from source art. */
    RiBuf *over_tiles = ri_buf_zero(ctx, 0x1000);
    ri_put_buf(ctx, over_tiles, ri_fe_lz(ctx, 0xc2fa8));
    RiBuf *over_palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, over_palette, 0x140, 0x160, ri_fe_rom_slice(ctx, 0xc3934, 0xc3954));
    RiBuf *cells = ri_fe_lz(ctx, 0xc3954);
    if (cells->size < 2048)
        ri_resize(ctx, cells, 2048);
    RiBuf *map =
        ri_tile_map(ctx, ri_view(over_tiles), ri_view(over_palette), ri_view(cells), 32, 32, 4);
    RiBuf *cropped = ri_fe_crop_screen(ctx, map, 256);
    ri_section(ctx, 15, GP_FAILURE_MENU + 4, 1, ri_image_asset(ctx, 240, 160, cropped, 0, 0));
    /* 1E7C8 state29 retains HUD tiles A0..A3 and palette bank B. */
    RiData flag_tiles = ri_fe_rom_slice(ctx, 0x63c34u, 0x63cb4u);
    RiData flag_palette = ri_fe_rom_slice(ctx, 0x62794u, 0x627b4u);
    if (flag_tiles.size != 128u || flag_palette.size != 32u)
        ri_fail(ctx, "truncated GP credit flag graphics");
    const uint32_t flag_cells[4] = {0u, 1u, 2u, 3u};
    ri_section(ctx, 15u, GP_FAILURE_MENU + 5u, 1u,
               cell_rectangle(ctx, flag_tiles, flag_palette, flag_cells, 2u, 2u, -1, 0, 0));
}
