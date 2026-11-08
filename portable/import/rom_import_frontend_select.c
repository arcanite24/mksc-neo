#include "rom_import_frontend.h"

#include <string.h>

/* Ports of tools/portable/character_menu_rom.py and course_menu_rom.py. */

/* ---- character_menu_rom.character_sections ----------------------------- */

#define CHARACTER_HEADING 0x22000u
#define CHARACTER_CURSOR 0x22010u
#define CHARACTER_PORTRAIT 0x22100u
#define CHARACTER_NAME 0x22200u
#define CHARACTER_PREVIEW 0x22300u
#define CHARACTER_STATS 0x22020u         /* sub_80065CC: D93F4[source] at (60,115) */
#define CHARACTER_PLAYER_CURSOR 0x26100u /* 0x224xx is roster art */
#define CHARACTER_PLAYER_BADGE 0x26200u
#define CHARACTER_FIRST_IMAGE 0x30000u

/* character_sections.add_image: the `images` dict keyed by payload bytes. */
typedef struct ImageEntry
{
    uint64_t hash;
    const RiBuf *payload;
    uint32_t id;
} ImageEntry;

typedef struct ImageCache
{
    RiBuf *entries; /* ImageEntry array */
    uint32_t next_image;
} ImageCache;

static uint64_t fnv1a(const RiBuf *data)
{
    uint64_t hash = 0xcbf29ce484222325u;
    for (size_t i = 0; i < data->size; ++i)
        hash = (hash ^ data->data[i]) * 0x100000001b3u;
    return hash;
}

static uint32_t add_image(RiCtx *ctx, ImageCache *cache, RiBuf *payload)
{
    uint64_t hash = fnv1a(payload);
    size_t count = cache->entries->size / sizeof(ImageEntry);
    for (size_t i = 0; i < count; ++i)
    {
        ImageEntry entry;
        memcpy(&entry, cache->entries->data + i * sizeof(entry), sizeof(entry));
        if (entry.hash == hash && entry.payload->size == payload->size &&
            memcmp(entry.payload->data, payload->data, payload->size) == 0)
        {
            ri_buf_free(ctx, payload);
            return entry.id;
        }
    }
    ImageEntry entry = {hash, payload, cache->next_image++};
    ri_section(ctx, 15, entry.id, 1, payload);
    ri_put(ctx, cache->entries, &entry, sizeof(entry));
    return entry.id;
}

/* Frame list: pairs of u32 image id and u32 ticks. */
static void add_frame(RiCtx *ctx, RiBuf *frames, uint32_t image, uint64_t ticks)
{
    if (ticks > 0xffffffffu)
        ri_fail(ctx, "native animation ticks outside uint32");
    ri_u32(ctx, frames, image);
    ri_u32(ctx, frames, (uint32_t)ticks);
}

/* animation_asset(frames, loop): MKSCANI1 header then (image, ticks) pairs. */
static RiBuf *animation_asset(RiCtx *ctx, const RiBuf *frames, int loop)
{
    size_t count = frames->size / 8;
    uint64_t duration = 0;
    if (!count || count > 4096)
        ri_fail(ctx, "invalid native animation");
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t ticks = ri_le32(frames->data + i * 8 + 4);
        if (!ticks)
            ri_fail(ctx, "invalid native animation");
        duration += ticks;
    }
    if (duration > 0xffffffffu)
        ri_fail(ctx, "native animation duration overflows");
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCANI1", 8);
    ri_u16(ctx, out, 32);
    ri_u16(ctx, out, 1);
    ri_u16(ctx, out, (uint32_t)count);
    ri_u16(ctx, out, loop ? 1u : 0u);
    ri_words(ctx, out, 4, (uint32_t)duration, (uint32_t)32u, (uint32_t)(32 + count * 8),
             (uint32_t)0u);
    ri_put_buf(ctx, out, frames);
    return out;
}

/* character_menu_rom.flipped: mirror an MKSCIMG1 image horizontally. */
static RiBuf *flipped(RiCtx *ctx, RiBuf *image)
{
    uint32_t w = ri_le16(image->data + 12), h = ri_le16(image->data + 14);
    int32_t ax = (int16_t)ri_le16(image->data + 16), ay = (int16_t)ri_le16(image->data + 18);
    RiBuf *pixels = ri_buf_zero(ctx, (size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            ri_set_pixel(pixels, (size_t)y * w + x,
                         ri_le32(image->data + 32 + ((size_t)y * w + w - 1 - x) * 4));
    RiBuf *out = ri_image_asset(ctx, w, h, pixels, ax, ay);
    ri_buf_free(ctx, pixels);
    ri_buf_free(ctx, image);
    return out;
}

/* placed(cell_image(rom, address, tiles, palette), x, y) */
static RiBuf *placed_cell(RiCtx *ctx, size_t address, const RiBuf *tiles, const RiBuf *palette,
                          int32_t x, int32_t y)
{
    RiBuf *image = ri_cell_image(ctx, ctx->rom, address, ri_view(tiles), ri_view(palette));
    ri_fe_move_anchor(ctx, image, x, y);
    return image;
}

void ri_fe_character_sections(RiCtx *ctx)
{
    static const size_t uploads[5][2] = {
        {0xaf320, 0}, {0xc2fa8, 0x1000}, {0xafe60, 0x5800}, {0xb042c, 0x6c00}, {0xb05bc, 0x7000}};
    RiBuf *tiles = ri_buf_zero(ctx, 32768);
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, palette, 0, 64, ri_fe_rom_slice(ctx, 0xaf2c0, 0xaf300));
    ri_fe_splice(ctx, palette, 0x60, 0x80, ri_fe_rom_slice(ctx, 0xafe40, 0xafe60));
    for (size_t i = 0; i < 5; ++i)
        ri_fe_place(ctx, tiles, uploads[i][1], ri_view(ri_fe_lz(ctx, uploads[i][0])));
    ri_section(ctx, 15, CHARACTER_HEADING, 1, placed_cell(ctx, 0xc8828, tiles, palette, 128, 16));
    ImageCache cache = {ri_buf(ctx), CHARACTER_FIRST_IMAGE};
    RiBuf *blank = ri_buf_zero(ctx, 32);
    /* Source menu slots are a three-by-three grid with Random at the centre. */
    for (uint32_t slot = 0; slot < 9; ++slot)
    {
        size_t entry = 0xd9130 + (size_t)slot * 20;
        size_t cell = ri_fe_pointer(ctx, entry);
        int32_t x = ri_get_i16(ctx, ctx->rom, entry + 12),
                y = ri_get_i16(ctx, ctx->rom, entry + 14);
        int32_t source_value = ri_get_i16(ctx, ctx->rom, entry + 16);
        if (source_value < 0)
            ri_fail(ctx, "character menu source outside tables");
        size_t source = (size_t)source_value;
        ri_section(ctx, 15, CHARACTER_CURSOR + slot, 1,
                   placed_cell(ctx, ri_fe_pointer(ctx, 0xd93a8), tiles, palette, x, y));
        ri_fe_place(ctx, tiles, 0x3c00,
                    ri_view(ri_fe_lz(ctx, ri_fe_pointer(ctx, 0xd93d0 + source * 4))));
        ri_section(ctx, 15, CHARACTER_NAME + slot, 1,
                   placed_cell(ctx, 0xc888c, tiles, palette, 72, 48));
        uint32_t stats = ri_get_u32(ctx, ctx->rom, 0xd93f4 + source * 4);
        if (stats)
            ri_section(ctx, 15, CHARACTER_STATS + slot, 1,
                       placed_cell(ctx, ri_fe_pointer(ctx, 0xd93f4 + source * 4), tiles, palette,
                                   60, 115));
        for (uint32_t state = 0; state < 3; ++state)
        {
            RiBuf *frames = ri_buf(ctx);
            if (source == 8)
            {
                /* sub_8006A28's eight raw question-mark frames, four ticks each. */
                for (size_t phase = 0; phase < 8; ++phase)
                {
                    size_t address = ri_fe_pointer(ctx, 0xd945c + phase * 4);
                    ri_fe_splice(ctx, tiles, 0x6200, 0x6400,
                                 ri_fe_rom_slice(ctx, address, address + 512));
                    add_frame(ctx, frames,
                              add_image(ctx, &cache, placed_cell(ctx, cell, tiles, palette, x, y)),
                              4);
                }
            }
            else
            {
                size_t key = (source * 3 + state) * 4;
                uint32_t count = ri_get_u32(ctx, ctx->rom, 0xd9c68 + key);
                if (!count || count > 4096)
                    ri_fail(ctx, "invalid native animation");
                size_t table = ri_fe_pointer(ctx, 0xd9cc8 + key);
                size_t colors = ri_fe_pointer(ctx, 0xd9d28 + source * 8);
                ri_fe_splice(ctx, palette, (source + 7) * 32, (source + 8) * 32,
                             ri_fe_rom_slice(ctx, colors, colors + 32));
                for (size_t phase = 0; phase < count; ++phase)
                {
                    uint32_t ticks = ri_get_u32(ctx, ctx->rom, table + phase * 8);
                    size_t address = ri_fe_pointer(ctx, table + phase * 8 + 4);
                    ri_fe_place(ctx, tiles, 0x4000 + source * 512, ri_view(ri_fe_lz(ctx, address)));
                    add_frame(ctx, frames,
                              add_image(ctx, &cache, placed_cell(ctx, cell, tiles, palette, x, y)),
                              (uint64_t)ticks + 2);
                }
            }
            ri_section(ctx, 15, CHARACTER_PORTRAIT + slot * 3 + state, 1,
                       animation_asset(ctx, frames, state != 2));
            ri_buf_free(ctx, frames);
        }
        /* renderCharacterSprites uses the source view-angle table, reflected
         * across the second half of a turn. No source sprite state is retained. */
        RiBuf *preview = ri_buf(ctx);
        uint64_t graphics_id = source < 8 ? ri_get_u32(ctx, ctx->rom, 0xd9790 + source * 4) : 0;
        for (uint32_t angle = 0; angle < 128; ++angle)
        {
            uint32_t view = angle < 64 ? angle : 127 - angle;
            uint64_t frame = ri_get_u8(ctx, ctx->rom, 0xd9344 + view);
            uint64_t offset = 0x345800 + graphics_id * 0x16000 + frame * 0x800;
            uint64_t colors = 0x344600 + graphics_id * 512;
            size_t limit = ctx->rom.size;
            size_t start = offset < limit ? (size_t)offset : limit;
            ri_fe_splice(ctx, tiles, 0x5000, 0x5800, ri_fe_rom_slice(ctx, start, start + 0x800));
            if (source < 8)
            {
                start = colors < limit ? (size_t)colors : limit;
                ri_fe_splice(ctx, palette, 0x1e0, 0x200, ri_fe_rom_slice(ctx, start, start + 32));
            }
            else
            {
                ri_fe_splice(ctx, palette, 0x1e0, 0x200, ri_view(blank));
            }
            RiBuf *image = ri_cell_image(ctx, ctx->rom, 0xc8944, ri_view(tiles), ri_view(palette));
            if (angle >= 64)
                image = flipped(ctx, image);
            ri_fe_move_anchor(ctx, image, 72, 83);
            add_frame(ctx, preview, add_image(ctx, &cache, image), 1);
        }
        ri_section(ctx, 15, CHARACTER_PREVIEW + slot, 1, animation_asset(ctx, preview, 1));
        ri_buf_free(ctx, preview);
    }
    /* sub_8006518: players 2..4 cursors D93A8 and every player's lock badge
     * D93B8 at its D93C8 corner; the choosing player keeps the cursor. */
    tiles = ri_buf_zero(ctx, 32768);
    for (size_t i = 0; i < 5; ++i)
        ri_fe_place(ctx, tiles, uploads[i][1], ri_view(ri_fe_lz(ctx, uploads[i][0])));
    for (uint32_t player = 0; player < 4; ++player)
    {
        int32_t dx = (int8_t)ri_get_u8(ctx, ctx->rom, 0xd93c8 + player * 2);
        int32_t dy = (int8_t)ri_get_u8(ctx, ctx->rom, 0xd93c8 + player * 2 + 1);
        size_t cursor = ri_fe_pointer(ctx, 0xd93a8 + player * 4),
               badge = ri_fe_pointer(ctx, 0xd93b8 + player * 4);
        for (uint32_t slot = 0; slot < 9; ++slot)
        {
            size_t entry = 0xd9130 + (size_t)slot * 20;
            int32_t x = ri_get_i16(ctx, ctx->rom, entry + 12),
                    y = ri_get_i16(ctx, ctx->rom, entry + 14);
            if (player)
                ri_section(ctx, 15, CHARACTER_PLAYER_CURSOR + player * 16 + slot, 1,
                           placed_cell(ctx, cursor, tiles, palette, x, y));
            ri_section(ctx, 15, CHARACTER_PLAYER_BADGE + player * 16 + slot, 1,
                       placed_cell(ctx, badge, tiles, palette, x + dx, y + dy));
        }
    }
}

/* ---- course_menu_rom.course_sections ----------------------------------- */

#define COURSE_BACKGROUND 0x23000u
#define COURSE_HEADING 0x23010u
#define COURSE_CUP RI_FE_COURSE_CUP
#define COURSE_LOCKED_CUP 0x23120u
#define COURSE_CUP_NAME RI_FE_COURSE_CUP_NAME
#define COURSE_PREVIEW 0x23300u
#define COURSE_NAME 0x23400u
#define COURSE_SELECTED_NAME 0x23500u
/* 0800C78C records list: names on the panel's two stripe palettes, the
 * 79F18 panel over the records mushroom pattern, labels and time glyphs. */
#define RECORDS_NAME 0x23600u
#define RECORDS_PANEL 0x23800u
#define RECORDS_BEST 0x23801u
#define RECORDS_LAP 0x23802u
#define RECORDS_GLYPHS 0x23803u
/* sub_8009590 rank cell C8FE4 (+rating) and sub_8009754 trophy C8F60
 * (+medal*16+frame), anchored by their cell offsets. */
#define COURSE_RANK 0x23900u
#define COURSE_TROPHY 0x23a00u

static void emit_image(RiCtx *ctx, uint32_t id, uint32_t width, uint32_t height, RiBuf *pixels);

/* course_menu_rom.records_sections.strip: two-row cells of bank 15. */
static RiBuf *records_strip(RiCtx *ctx, const RiBuf *tiles, const RiBuf *palette,
                            const uint32_t *columns, uint32_t count)
{
    RiBuf *pixels = ri_buf_zero(ctx, (size_t)count * 8 * 16 * 4);
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < count * 8; ++x)
        {
            uint32_t tile = columns[x / 8];
            if (y >= 8)
                tile += tile < 0x2c4 ? 2 : 0x20;
            ri_set_pixel(
                pixels, y * count * 8 + x,
                ri_tile_pixel(ctx, ri_view(tiles), ri_view(palette), tile, x % 8, y % 8, 15, 4));
        }
    RiBuf *image = ri_image_asset(ctx, count * 8, 16, pixels, 0, 0);
    ri_buf_free(ctx, pixels);
    return image;
}

static void records_ghost_sections(RiCtx *ctx)
{
    RiBuf *palette = ri_fe_lz(ctx, 0xb2068u), *counts = ri_fe_lz(ctx, 0xb3424u),
          *ghosts = ri_fe_lz(ctx, 0xb2c38u);
    if (palette->size != 512u || counts->size != 576u || ghosts->size != 1024u)
        ri_fail(ctx, "invalid Records ghost graphics");
    RiBuf *tiles = ri_buf_zero(ctx, 32768u);
    ri_fe_place(ctx, tiles, 0x7400u, (RiData){counts->data, 192u});
    ri_fe_place(ctx, tiles, 0x7c00u, ri_view(ghosts));
    ri_section(ctx, 15u, 0x23860u, 1u,
               ri_cell_image(ctx, ctx->rom, 0xc96d4u, ri_view(tiles), ri_view(palette)));
    for (unsigned received = 0u; received < 2u; ++received)
    {
        unsigned limit = received ? 3u : 11u, first = received ? 0x2386cu : 0x23861u;
        for (unsigned count = 0u; count < limit; ++count)
        {
            unsigned tile = count ? count + (received ? 15u : 5u) : (received ? 3u : 0u);
            RiBuf *pixels = ri_buf_zero(ctx, 256u);
            for (unsigned y = 0u; y < 8u; ++y)
                for (unsigned x = 0u; x < 8u; ++x)
                    ri_set_pixel(
                        pixels, y * 8u + x,
                        ri_tile_pixel(ctx, ri_view(counts), ri_view(palette), tile, x, y, 15u, 4u));
            ri_section(ctx, 15u, first + count, 1u,
                       ri_image_asset(ctx, 8u, 8u, pixels, received ? 1 : -21, -4));
        }
    }
    RiBuf *rules = ri_buf(ctx);
    ri_words(ctx, rules, 4, 0x31484752u, 1u, 6u, 0u);
    ri_u32(ctx, rules, 0x23860u);
    for (unsigned i = 0u; i < 11u; ++i)
        ri_u32(ctx, rules, 0x23861u + i);
    for (unsigned i = 0u; i < 3u; ++i)
        ri_u32(ctx, rules, 0x2386cu + i);
    static const size_t tables[6] = {0xd9684u, 0xd9694u, 0xd9724u, 0xd9734u, 0xd96d4u, 0xd96e4u};
    size_t cells[48];
    unsigned unique = 0u;
    for (unsigned group = 0u; group < 6u; ++group)
    {
        unsigned count = (group & 1u) ? 8u : 2u;
        ri_u32(ctx, rules, count);
        for (unsigned row = 0u; row < count; ++row)
        {
            size_t cell = ri_fe_pointer(ctx, tables[group] + row * 8u);
            uint32_t duration = ri_get_u32(ctx, ctx->rom, tables[group] + row * 8u + 4u);
            if (duration >= 65535u)
                ri_fail(ctx, "invalid Records ghost frame");
            unsigned index = 0u;
            while (index < unique && cells[index] != cell)
                ++index;
            if (index == unique)
            {
                cells[unique++] = cell;
                ri_section(ctx, 15u, 0x23880u + index, 1u,
                           ri_cell_image(ctx, ctx->rom, cell, ri_view(tiles), ri_view(palette)));
            }
            ri_u32(ctx, rules, 0x23880u + index);
            ri_u16(ctx, rules, duration + 1u);
            ri_u16(ctx, rules, 0u);
        }
    }
    ri_section(ctx, 94u, 1u, 1u, rules);
}

static void records_sections(RiCtx *ctx)
{
    /* BG3 is the cup-0 mushroom pattern with records tiles; BG2 the panel. */
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, palette, 0xa0, 0xe0, ri_fe_rom_slice(ctx, 0xb2310, 0xb2350));
    ri_fe_splice(ctx, palette, 0xe0, 0x120, ri_fe_rom_slice(ctx, 0x7cd48, 0x7cd88));
    ri_fe_splice(ctx, palette, 0x120, 0x140, ri_fe_rom_slice(ctx, 0x7cd88, 0x7cda8));
    ri_fe_splice(ctx, palette, 0x1e0, 0x200, ri_fe_rom_slice(ctx, 0xaad10, 0xaad30));
    RiBuf *pattern_tiles = ri_fe_lz(ctx, 0x7d1e8);
    ri_fe_splice(ctx, pattern_tiles, 0x660, 0x860, ri_fe_rom_slice(ctx, 0x7d694, 0x7d894));
    RiBuf *pattern = ri_tile_map(ctx, ri_view(pattern_tiles), ri_view(palette),
                                 ri_view(ri_fe_lz(ctx, 0x7dcb4)), 32, 32, 4);
    RiBuf *tiles = ri_buf_zero(ctx, 0x8000);
    ri_fe_place(ctx, tiles, 0x7d20, ri_view(ri_fe_lz(ctx, 0xb3718)));
    RiBuf *panel = ri_tile_map(ctx, ri_view(tiles), ri_view(palette),
                               ri_view(ri_fe_lz(ctx, 0x79f18)), 32, 32, 4);
    /* The pattern sits at the settled list's scroll (174, 338). */
    RiBuf *pixels = ri_buf_zero(ctx, 240 * 160 * 4);
    for (uint32_t y = 0; y < 160; ++y)
        for (uint32_t x = 0; x < 240; ++x)
        {
            uint32_t value = ri_pixel(panel, y * 256 + x);
            if (!value)
                value = ri_pixel(pattern, ((y + 338) % 256) * 256 + (x + 174) % 256);
            ri_set_pixel(pixels, y * 240 + x, value);
        }
    emit_image(ctx, RECORDS_PANEL, 240, 160, pixels);
    ri_section(ctx, 15, 0x23804, 1, ri_image_asset(ctx, 256, 256, pattern, 0, 0));
    ri_section(ctx, 15, 0x23805, 1, ri_image_asset(ctx, 256, 256, panel, 0, 0));
    ri_fe_splice(ctx, tiles, 0x5800, 0x6400, ri_fe_rom_slice(ctx, 0xacafc, 0xad6fc));
    ri_fe_splice(ctx, tiles, 0x6400, 0x6c00, ri_fe_rom_slice(ctx, 0xabefc, 0xac6fc));
    uint32_t columns[14];
    for (uint32_t i = 0; i < 7; ++i)
        columns[i] = 0x32d + i;
    ri_section(ctx, 15, RECORDS_BEST, 1, records_strip(ctx, tiles, palette, columns, 7));
    for (uint32_t i = 0; i < 6; ++i)
        columns[i] = 0x334 + i;
    ri_section(ctx, 15, RECORDS_LAP, 1, records_strip(ctx, tiles, palette, columns, 6));
    for (uint32_t i = 0; i < 12; ++i)
        columns[i] = 0x320 + i;
    columns[12] = 0x2c0;
    columns[13] = 0x2c1;
    ri_section(ctx, 15, RECORDS_GLYPHS, 1, records_strip(ctx, tiles, palette, columns, 14));
    records_ghost_sections(ctx);
}

/* course_menu_rom.cup_record_sections */
static void cup_record_sections(RiCtx *ctx)
{
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, palette, 0x60, 0x80, ri_fe_rom_slice(ctx, 0x627d4, 0x627f4));
    ri_fe_place(ctx, palette, 0x1a0, ri_view(ri_fe_lz(ctx, 0xb07e4)));
    RiBuf *tiles = ri_buf_zero(ctx, 32768);
    ri_fe_place(ctx, tiles, 0x4c00, ri_view(ri_fe_lz(ctx, 0xb0e28)));
    RiBuf *glyphs = ri_fe_lz(ctx, 0xb0eb0);
    if (glyphs->size < 8 * 128)
        ri_fail(ctx, "invalid cup rank glyphs");
    for (uint32_t rating = 0; rating < 8; ++rating)
    {
        memcpy(tiles->data + 0x4800, glyphs->data + rating * 128, 128);
        ri_section(ctx, 15, COURSE_RANK + rating, 1,
                   ri_cell_image(ctx, ctx->rom, 0xc8fe4, ri_view(tiles), ri_view(palette)));
    }
    for (uint32_t medal = 0; medal < 3; ++medal)
    {
        size_t colors = ri_fe_pointer(ctx, 0xd961c + medal * 4);
        ri_fe_splice(ctx, palette, 0x1c0, 0x1e0, ri_fe_rom_slice(ctx, colors, colors + 32));
        for (uint32_t frame = 0; frame < 16; ++frame)
        {
            size_t list = ri_fe_pointer(ctx, 0xdbe28 + medal * 4);
            ri_fe_place(ctx, tiles, 0x5a00,
                        ri_view(ri_fe_lz(ctx, ri_fe_pointer(ctx, list + frame * 4))));
            ri_section(ctx, 15, COURSE_TROPHY + medal * 16 + frame, 1,
                       ri_cell_image(ctx, ctx->rom, 0xc8f60, ri_view(tiles), ri_view(palette)));
        }
    }
}

/* origin(cell_image(rom, address, tiles, palette)) */
static RiBuf *origin_cell(RiCtx *ctx, size_t address, const RiBuf *tiles, const RiBuf *palette)
{
    RiBuf *image = ri_cell_image(ctx, ctx->rom, address, ri_view(tiles), ri_view(palette));
    ri_fe_set_anchor(ctx, image, 0, 0);
    return image;
}

static void emit_image(RiCtx *ctx, uint32_t id, uint32_t width, uint32_t height, RiBuf *pixels)
{
    ri_section(ctx, 15, id, 1, ri_image_asset(ctx, width, height, pixels, 0, 0));
    ri_buf_free(ctx, pixels);
}

/* Extra and Battle previews: the course container's 8x8 thumbnail at C4,
 * through course_rom.Data (span/u32/lz). */
static RiBuf *container_cover(RiCtx *ctx, size_t definition)
{
    uint64_t source_id = ri_get_u32(ctx, ctx->rom, definition);
    RiData container = ri_fe_rom_slice(ctx, 0x258000, 0x308000);
    if (source_id * 4 + 4 > container.size)
        ri_fail(ctx, "course data range outside source");
    size_t start = ri_get_u32(ctx, container, (size_t)source_id * 4);
    if (start > container.size)
        start = container.size; /* container.data[start:] */
    RiData course = {container.data + start, container.size - start};
    size_t offset = ri_get_u32(ctx, course, 0xc4);
    ri_span(ctx, course, offset, 4);
    RiBuf *cover = ri_lz77(ctx, course, offset);
    if (cover->size != 0x800)
        ri_fail(ctx, "course decompression size at %#zx", offset);
    return cover;
}

static RiBuf *locked_cup_palette(RiCtx *ctx, const RiBuf *palette, uint32_t cup)
{
    RiBuf *result = ri_buf_copy(ctx, palette->data, palette->size);
    const size_t start = 0x100u + cup * 32u;
    for (size_t index = 0u; index < 16u; ++index)
    {
        const size_t offset = start + index * 2u;
        uint16_t color =
            (uint16_t)(result->data[offset] | ((uint16_t)result->data[offset + 1u] << 8u));
        uint16_t dim = 0u;
        for (unsigned shift = 0u; shift < 15u; shift += 5u)
            dim |= (uint16_t)((((color >> shift) & 31u) >> 1u) << shift);
        result->data[offset] = (uint8_t)dim;
        result->data[offset + 1u] = (uint8_t)(dim >> 8u);
    }
    /* 860C substitutes the authored Special lock bank for either track set. */
    if (cup == 4u)
        ri_fe_splice(ctx, result, start, start + 32u, ri_fe_rom_slice(ctx, 0xb1398u, 0xb13b8u));
    return result;
}

void ri_fe_course_sections(RiCtx *ctx)
{
    /* spm_loadBgGfx (0800A8F0): shared cells plus cup icons and names. */
    static const size_t uploads[6][2] = {{0xaf320, 0},      {0xc2fa8, 0x1000}, {0xb7720, 0x3c00},
                                         {0xb9950, 0x3800}, {0xb0848, 0x5c00}, {0xb0e28, 0x4c00}};
    static const size_t cup_maps[5] = {0x7dcb4, 0x7ddc8, 0x7dedc, 0x7dff8, 0x7e10c};
    RiBuf *tiles = ri_buf_zero(ctx, 32768);
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, palette, 0, 64, ri_fe_rom_slice(ctx, 0xaf2c0, 0xaf300));
    ri_fe_place(ctx, palette, 0xe0, ri_view(ri_fe_lz(ctx, 0xb06dc)));
    ri_fe_place(ctx, palette, 0x1a0, ri_view(ri_fe_lz(ctx, 0xb07e4)));
    ri_fe_splice(ctx, palette, 0x100, 0x1a0, ri_fe_rom_slice(ctx, 0xb75c0, 0xb7660));
    for (size_t i = 0; i < 6; ++i)
        ri_fe_place(ctx, tiles, uploads[i][1], ri_view(ri_fe_lz(ctx, uploads[i][0])));
    ri_section(ctx, 15, COURSE_HEADING, 1,
               origin_cell(ctx, ri_fe_pointer(ctx, 0xd95bc), tiles, palette));
    ri_section(ctx, 15, COURSE_HEADING + 1, 1,
               origin_cell(ctx, ri_fe_pointer(ctx, 0xd9588), tiles, palette));
    RiBuf *pattern_tiles = ri_fe_lz(ctx, 0x7d1e8);
    RiBuf *affine_tiles = ri_buf_zero(ctx, 0x4000);
    ri_fe_place(ctx, affine_tiles, 0, ri_view(ri_fe_lz(ctx, 0xae180)));
    ri_fe_place(ctx, affine_tiles, 0x2000, ri_view(ri_fe_lz(ctx, 0xae7c8)));
    RiBuf *affine_map = ri_fe_lz(ctx, 0xaee04);
    if (affine_map->size != 1024u)
        ri_fail(ctx, "invalid course affine map size");
    for (uint32_t cup = 0; cup < 5; ++cup)
    {
        RiBuf *background_palette = ri_buf_zero(ctx, 512);
        ri_fe_splice(ctx, background_palette, 0xa0, 0xc0,
                     ri_fe_rom_slice(ctx, 0xae07c + cup * 32, 0xae09c + cup * 32));
        ri_fe_splice(ctx, background_palette, 0xe0, 0x120, ri_fe_rom_slice(ctx, 0x7cd48, 0x7cd88));
        RiBuf *map = ri_buf(ctx), *cells = ri_fe_lz(ctx, cup_maps[cup]);
        for (uint32_t phase = 0u; phase < 7u; ++phase)
        {
            size_t colors = 0x7cd88u + cup * 224u + phase * 32u;
            ri_fe_splice(ctx, background_palette, 0x120, 0x140,
                         ri_fe_rom_slice(ctx, colors, colors + 32u));
            ri_put_buf(ctx, map,
                       ri_tile_map(ctx, ri_view(pattern_tiles), ri_view(background_palette),
                                   ri_view(cells), 32, 32, 4));
        }
        emit_image(ctx, COURSE_BACKGROUND + cup, 256, 256 * 7, map);
        RiBuf *affine = ri_buf_zero(ctx, 256u * 256u * 4u);
        for (uint32_t y = 0u; y < 256u; ++y)
            for (uint32_t x = 0u; x < 256u; ++x)
                ri_set_pixel(affine, y * 256u + x,
                             ri_tile_pixel(ctx, ri_view(affine_tiles), ri_view(background_palette),
                                           affine_map->data[(y / 8u) * 32u + x / 8u], x & 7u,
                                           y & 7u, 0u, 8));
        emit_image(ctx, 0x23020u + cup, 256u, 256u, affine);
        RiBuf *frame_palette = ri_buf_copy(ctx, palette->data, palette->size);
        size_t colors = 0xae11cu + cup * 20u;
        ri_fe_splice(ctx, frame_palette, 14u, 32u, ri_fe_rom_slice(ctx, colors, colors + 18u));
        colors = ri_fe_pointer(ctx, 0xd94a0u + cup * 4u);
        ri_fe_splice(ctx, frame_palette, 0x1e0u, 0x1eau,
                     ri_fe_rom_slice(ctx, colors, colors + 10u));
        ri_section(ctx, 15u, 0x23030u + cup, 1u,
                   ri_cell_image(ctx, ctx->rom, 0xc8dacu, ri_view(tiles), ri_view(frame_palette)));
        size_t cell = ri_fe_pointer(ctx, 0xd94fc + (size_t)cup * 20);
        ri_section(ctx, 15, COURSE_CUP + cup, 1,
                   ri_cell_image(ctx, ctx->rom, cell, ri_view(tiles), ri_view(palette)));
        RiBuf *extra_palette = ri_buf_copy(ctx, palette->data, palette->size);
        ri_fe_splice(ctx, extra_palette, 0x100, 0x1a0, ri_fe_rom_slice(ctx, 0xbb5a0, 0xbb640));
        ri_section(ctx, 15, COURSE_CUP + 5 + cup, 1,
                   ri_cell_image(ctx, ctx->rom, cell, ri_view(tiles), ri_view(extra_palette)));
        ri_section(ctx, 15, COURSE_LOCKED_CUP + cup, 1,
                   ri_cell_image(ctx, ctx->rom, cell, ri_view(tiles),
                                 ri_view(locked_cup_palette(ctx, palette, cup))));
        ri_section(ctx, 15, COURSE_LOCKED_CUP + 5 + cup, 1,
                   ri_cell_image(ctx, ctx->rom, cell, ri_view(tiles),
                                 ri_view(locked_cup_palette(ctx, extra_palette, cup))));
        ri_fe_place(ctx, tiles, 0x5800,
                    ri_view(ri_fe_lz(ctx, ri_fe_pointer(ctx, 0xd9628 + (size_t)cup * 4))));
        ri_section(ctx, 15, COURSE_CUP_NAME + cup, 1, origin_cell(ctx, 0xc8d9c, tiles, palette));
    }
    RiBuf *ok_palette = ri_buf_copy(ctx, palette->data, palette->size);
    size_t ok_colors = ri_fe_pointer(ctx, 0xd8ea0u);
    ri_fe_splice(ctx, ok_palette, 0x40u, 0x60u, ri_fe_rom_slice(ctx, ok_colors, ok_colors + 32u));
    RiBuf *ok_tiles = ri_buf_copy(ctx, tiles->data, tiles->size);
    ri_fe_place(ctx, ok_tiles, 0x2800u, ri_view(ri_fe_lz(ctx, 0xb83a4u)));
    ri_section(ctx, 15u, 0x23038u, 1u,
               ri_cell_image(ctx, ctx->rom, 0xc8a58u, ri_view(ok_tiles), ri_view(ok_palette)));
    RiBuf *text_palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, text_palette, 0x140, 0x180, ri_fe_rom_slice(ctx, 0xb23b0, 0xb23f0));
    RiBuf *records_palette = ri_buf_zero(ctx, 512);
    ri_fe_splice(ctx, records_palette, 0xa0, 0xe0, ri_fe_rom_slice(ctx, 0xb2310, 0xb2350));
    /* track_getMKSCTrack and gTrackDefTable supply every GBA course in cup
     * order. Covers contain a blank tile followed by ten columns by eight
     * rows of 8bpp tiles; names contain twelve columns by two rows of 4bpp. */
    for (uint32_t index = 0; index < 44; ++index)
    {
        int extra = index >= 20, arena = index >= 40;
        size_t table = arena   ? 0xe7524 + (size_t)(index - 40) * 4
                       : extra ? 0xe74c4 + (size_t)(index - 20) * 4
                               : 0xe7464 + (size_t)index * 4;
        size_t track = ri_get_u32(ctx, ctx->rom, table);
        size_t definition = ri_fe_pointer(ctx, 0xe7fec + track * 4);
        RiBuf *pixels = ri_buf_zero(ctx, 80 * 64 * 4);
        if (extra)
        {
            /* 8938/8850: Extra covers use the shared 66EAC palette. */
            RiBuf *cover = container_cover(ctx, definition);
            RiData colors = ri_fe_rom_slice(ctx, 0x66eac, 0x66ecc);
            for (uint32_t y = 0; y < 64; ++y)
                for (uint32_t x = 8; x < 72; ++x)
                    ri_set_pixel(pixels, y * 80 + x,
                                 ri_tile_pixel(ctx, ri_view(cover), colors,
                                               (y / 8) * 8 + (x - 8) / 8, (x - 8) % 8, y % 8, 0,
                                               4));
            ri_buf_free(ctx, cover);
        }
        else
        {
            RiBuf *cover = ri_fe_lz(ctx, ri_fe_pointer(ctx, definition + 0x24));
            RiBuf *colors = ri_fe_lz(ctx, ri_fe_pointer(ctx, definition + 0x28));
            if (cover->size != 81 * 64 || colors->size > 512)
                ri_fail(ctx, "invalid course selector cover");
            ri_resize(ctx, colors, 512);
            for (uint32_t y = 0; y < 64; ++y)
                for (uint32_t x = 0; x < 80; ++x)
                    ri_set_pixel(pixels, y * 80 + x,
                                 ri_tile_pixel(ctx, ri_view(cover), ri_view(colors),
                                               1 + (y / 8) * 10 + x / 8, x % 8, y % 8, 0, 8));
            ri_buf_free(ctx, cover);
            ri_buf_free(ctx, colors);
        }
        emit_image(ctx, COURSE_PREVIEW + index, 80, 64, pixels);
        RiBuf *name = ri_fe_lz(ctx, ri_fe_pointer(ctx, definition + 0x30));
        if (name->size != 24 * 32)
            ri_fail(ctx, "invalid course selector name");
        static const uint32_t name_banks[2][2] = {{COURSE_NAME, 10}, {COURSE_SELECTED_NAME, 11}};
        for (size_t b = 0; b < 2; ++b)
        {
            RiBuf *name_pixels = ri_buf_zero(ctx, 96 * 16 * 4);
            for (uint32_t y = 0; y < 16; ++y)
                for (uint32_t x = 0; x < 96; ++x)
                    ri_set_pixel(name_pixels, y * 96 + x,
                                 ri_tile_pixel(ctx, ri_view(name), ri_view(text_palette),
                                               (y / 8) * 12 + x / 8, x % 8, y % 8, name_banks[b][1],
                                               4));
            emit_image(ctx, name_banks[b][0] + index, 96, 16, name_pixels);
        }
        for (uint32_t stripe = 0; index < 40 && stripe < 2; ++stripe)
        {
            RiBuf *name_pixels = ri_buf_zero(ctx, 96 * 16 * 4);
            for (uint32_t y = 0; y < 16; ++y)
                for (uint32_t x = 0; x < 96; ++x)
                    ri_set_pixel(name_pixels, y * 96 + x,
                                 ri_tile_pixel(ctx, ri_view(name), ri_view(records_palette),
                                               (y / 8) * 12 + x / 8, x % 8, y % 8, 5 + stripe, 4));
            emit_image(ctx, RECORDS_NAME + stripe * 64 + index, 96, 16, name_pixels);
        }
        ri_buf_free(ctx, name);
    }
    records_sections(ctx);
    cup_record_sections(ctx);
}
