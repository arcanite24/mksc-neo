#include "rom_import_frontend.h"

#include <string.h>

/* Frontend group: the kind-15 menu art importers called by
 * tools/portable/import_rom.py sections(), ui_text_mod.sections() (kind 66)
 * and ceremony_rom.ceremony_payload (kind 24). Shared helpers live here. */

/* ---- Shared helpers ---------------------------------------------------- */

void ri_fe_splice(RiCtx *ctx, RiBuf *target, size_t start, size_t stop, RiData data)
{
    size_t length = target->size;
    size_t first = start < length ? start : length;
    size_t last = stop < length ? stop : length;
    if (last < first) last = first;
    size_t tail = length - last, total = first + data.size + tail;
    /* data never aliases target: every source is the ROM or another buffer. */
    if (total > length) ri_resize(ctx, target, total);
    memmove(target->data + first + data.size, target->data + last, tail);
    if (data.size) memcpy(target->data + first, data.data, data.size);
    target->size = total;
}

void ri_fe_place(RiCtx *ctx, RiBuf *target, size_t offset, RiData data)
{
    ri_fe_splice(ctx, target, offset, offset + data.size, data);
}

RiData ri_fe_rom_slice(RiCtx *ctx, size_t start, size_t stop)
{
    size_t size = ctx->rom.size;
    if (start > size) start = size;
    if (stop > size) stop = size;
    if (stop < start) stop = start;
    RiData view = {ctx->rom.data + start, stop - start};
    return view;
}

RiBuf *ri_fe_lz(RiCtx *ctx, size_t offset)
{
    return ri_lz77(ctx, ctx->rom, offset);
}

size_t ri_fe_pointer(RiCtx *ctx, size_t address)
{
    return ri_pointer(ctx, ctx->rom, address);
}

int32_t ri_fe_floordiv(int32_t value, int32_t divisor)
{
    int32_t quotient = value / divisor;
    return value % divisor < 0 ? quotient - 1 : quotient;
}

RiBuf *ri_fe_crop_screen(RiCtx *ctx, const RiBuf *pixels, size_t width)
{
    if (width < 240 || pixels->size < width * 160 * 4) ri_fail(ctx, "frontend tile map too small");
    RiBuf *out = ri_buf_zero(ctx, 240 * 160 * 4);
    for (size_t y = 0; y < 160; ++y)
        memcpy(out->data + y * 240 * 4, pixels->data + y * width * 4, 240 * 4);
    return out;
}

void ri_fe_set_anchor(RiCtx *ctx, RiBuf *image, int32_t x, int32_t y)
{
    if (image->size < 20) ri_fail(ctx, "frontend image header truncated");
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) ri_fail(ctx, "image anchor outside int16");
    ri_set_u16(image, 16, (uint32_t)x);
    ri_set_u16(image, 18, (uint32_t)y);
}

void ri_fe_move_anchor(RiCtx *ctx, RiBuf *image, int32_t x, int32_t y)
{
    if (image->size < 20) ri_fail(ctx, "frontend image header truncated");
    int32_t ax = (int16_t)ri_le16(image->data + 16), ay = (int16_t)ri_le16(image->data + 18);
    ri_fe_set_anchor(ctx, image, ax + x, ay + y);
}

const RiBuf *ri_fe_section(RiCtx *ctx, uint32_t kind, uint32_t content_id)
{
    for (size_t i = 0; i < ctx->section_count; ++i)
        if (ctx->sections[i].kind == kind && ctx->sections[i].content_id == content_id)
            return ctx->sections[i].data;
    ri_fail(ctx, "missing frontend section %u/%#x", (unsigned)kind, (unsigned)content_id);
}

/* ---- ui_text_mod.sections ---------------------------------------------- */

typedef struct UiString {
    uint32_t id;
    const char *value;
} UiString;

static const UiString ui_strings[] = {
#define UI_STRING(id, value) {id, value},
#include "../core/ui_strings.inc"
#undef UI_STRING
};

static void ui_text_sections(RiCtx *ctx)
{
    for (size_t i = 0; i < sizeof(ui_strings) / sizeof(ui_strings[0]); ++i) {
        /* payload(): b'UIT1', u32 length, ASCII text, NUL. */
        size_t length = strlen(ui_strings[i].value);
        RiBuf *out = ri_buf(ctx);
        ri_put(ctx, out, "UIT1", 4);
        ri_u32(ctx, out, (uint32_t)length);
        ri_put(ctx, out, ui_strings[i].value, length + 1);
        ri_section(ctx, 66, ui_strings[i].id, 1, out);
    }
}

/* ---- ceremony_rom.ceremony_payload ------------------------------------- */

static RiBuf *ceremony_image(RiCtx *ctx, uint32_t width, uint32_t height, RiBuf *pixels)
{
    RiBuf *image = ri_image_asset(ctx, width, height, pixels, 0, 0);
    ri_buf_free(ctx, pixels);
    return image;
}

static RiBuf *ceremony_legacy_payload(RiCtx *ctx)
{
    static const size_t uploads[][2] = {{0xc19c4, 0}, {0xc1a10, 0x2a0}, {0xc1a38, 0x700},
                                        {0xc1ad8, 0xb00}, {0xc1b44, 0xf00}, {0xc1bcc, 0x1300},
                                        {0xc1c50, 0x1e40}};
    static const uint32_t digit_columns[3][2] = {{1, 2}, {3, 3}, {6, 3}};
    RiBuf *palette = ri_buf_zero(ctx, 512);
    ri_fe_place(ctx, palette, 0x120, ri_view(ri_fe_lz(ctx, 0xc18fc)));
    RiBuf *tiles = ri_buf_zero(ctx, 32768);
    for (size_t i = 0; i < 7; ++i)
        ri_fe_place(ctx, tiles, uploads[i][1], ri_view(ri_fe_lz(ctx, uploads[i][0])));
    const RiBuf *images[43];
    size_t count = 0;
    /* 27A6C writes the eleven rank-label tiles as 3,3,5 cells in bank12. */
    for (size_t rank = 0; rank < 8; ++rank) {
        size_t start = ri_fe_pointer(ctx, 0xee704 + rank * 4);
        ri_fe_splice(ctx, tiles, 0x1c00, 0x1c00 + 0x160, ri_fe_rom_slice(ctx, start, start + 0x160));
        RiBuf *pixels = ri_buf_zero(ctx, 40 * 24 * 4);
        for (uint32_t y = 0; y < 24; ++y)
            for (uint32_t x = 0; x < 40; ++x) {
                uint32_t row = y / 8, column = x / 8;
                if (column < (row == 2 ? 5u : 3u))
                    ri_set_pixel(pixels, y * 40 + x,
                                 ri_tile_pixel(ctx, ri_view(tiles), ri_view(palette),
                                               0xe0 + row * 3 + column, x % 8, y % 8, 12, 4));
            }
        images[count++] = ceremony_image(ctx, 40, 24, pixels);
    }
    /* The rating is a nine-tile square, shared by every cup and engine class. */
    for (size_t rating = 0; rating < 8; ++rating) {
        RiBuf *data = ri_fe_lz(ctx, ri_fe_pointer(ctx, 0xdc504 + rating * 4));
        if (data->size != 9 * 32) ri_fail(ctx, "invalid ceremony rating art");
        RiBuf *pixels = ri_buf_zero(ctx, 24 * 24 * 4);
        for (uint32_t y = 0; y < 24; ++y)
            for (uint32_t x = 0; x < 24; ++x)
                ri_set_pixel(pixels, y * 24 + x,
                             ri_tile_pixel(ctx, ri_view(data), ri_view(palette),
                                           (y / 8) * 3 + x / 8, x % 8, y % 8, 13, 4));
        images[count++] = ceremony_image(ctx, 24, 24, pixels);
    }
    /* C2330's class digits: 50,100,150. Reuse its source cells without text fonts. */
    RiBuf *data = ri_fe_lz(ctx, 0xc19c4);
    ri_fe_place(ctx, tiles, 0, ri_view(data));
    ri_fe_place(ctx, tiles, 0x1e40, ri_view(ri_fe_lz(ctx, 0xc1c50)));
    RiBuf *cells = ri_fe_lz(ctx, 0xc2330);
    for (size_t i = 0; i < 3; ++i) {
        uint32_t column = digit_columns[i][0], width = digit_columns[i][1] * 8;
        RiBuf *pixels = ri_buf_zero(ctx, (size_t)width * 16 * 4);
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t cell = ri_get_u16(ctx, ri_view(cells), ((5 + y / 8) * 32 + column + x / 8) * 2);
                ri_set_pixel(pixels, y * width + x,
                             ri_tile_pixel(ctx, ri_view(tiles), ri_view(palette), cell & 1023,
                                           x % 8, y % 8, cell >> 12, 4));
            }
        images[count++] = ceremony_image(ctx, width, 16, pixels);
    }
    for (uint32_t cup = 0; cup < 5; ++cup)
        images[count++] = ri_fe_section(ctx, 15, RI_FE_COURSE_CUP_NAME + cup);
    for (uint32_t cup = 0; cup < 5; ++cup)
        images[count++] = ri_fe_section(ctx, 15, RI_FE_COURSE_CUP + cup);
    /* The cheering audience is the original two-frame celebration cell pair. */
    RiBuf *object_tiles = ri_fe_lz(ctx, 0xc0910);
    ri_put_zero(ctx, object_tiles, 32768);
    RiBuf *object_palette = ri_fe_lz(ctx, 0xbf5a4);
    images[count++] = ri_cell_image(ctx, ctx->rom, 0xc997c, ri_view(object_tiles), ri_view(object_palette));
    images[count++] = ri_cell_image(ctx, ctx->rom, 0xc9984, ri_view(object_tiles), ri_view(object_palette));
    images[count++] = ri_fe_section(ctx, 15, RI_FE_PAUSE_SELECTED_LABEL);
    /* Result coin marker and digits, normalized into native eight-pixel images. */
    RiData hud_tiles = ri_fe_rom_slice(ctx, 0x62834, 0x64834);
    RiBuf *hud_palette = ri_buf_zero(ctx, 512);
    ri_fe_place(ctx, hud_palette, 0x160, ri_fe_rom_slice(ctx, 0x62794, 0x627b4));
    ri_fe_place(ctx, hud_palette, 0x180, ri_fe_rom_slice(ctx, 0x627b4, 0x627d4));
    for (uint32_t glyph = 0; glyph < 11; ++glyph) {
        uint32_t tile = glyph ? 0x10 + glyph - 1 : 0x20, bank = glyph ? 11 : 12;
        RiBuf *pixels = ri_buf_zero(ctx, 8 * 8 * 4);
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; ++x)
                ri_set_pixel(pixels, y * 8 + x,
                             ri_tile_pixel(ctx, hud_tiles, ri_view(hud_palette), tile, x, y, bank, 4));
        images[count++] = ceremony_image(ctx, 8, 8, pixels);
    }
    if (count != 43) ri_fail(ctx, "ceremony image count mismatch");
    /* Native presentation timing and colors; no original loading chronology. */
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 8, (uint32_t)0x33435047u, (uint32_t)43u, (uint32_t)120u, (uint32_t)180u,
             (uint32_t)32u, (uint32_t)0xff182848u, (uint32_t)0xff506830u, (uint32_t)0xffe8d890u);
    for (size_t i = 0; i < count; ++i) {
        ri_u32(ctx, out, (uint32_t)images[i]->size);
        ri_put_buf(ctx, out, images[i]);
    }
    return out;
}

/* Authored source layers and normalized animation tracks. The owner receives
 * native image IDs, never original cell addresses or uploaded graphics state. */
typedef struct
{
    /* Signed source coordinates remain raw halfword bits until serialized. */
    uint16_t x, y, depth_bias;
    uint16_t first_frame, frame_count, initial_frame, remaining, reserved;
} CeremonyActorTrack;

typedef struct
{
    uint16_t image_id, duration;
} CeremonyActorFrame;

typedef struct
{
    const RiBuf *images[341];
    uint8_t darkenable[341];
    size_t image_count, frame_count;
    RiBuf *object_tiles, *object_palette;
    RiBuf *background_tiles, *background_palette, *summary_map;
    uint16_t cup_labels[5], headers[5][23], flypast[8], trophies[3][16];
    uint16_t drop, shadow, summary_coin, sparkles[12], confetti[12], panels[3];
    uint16_t glyphs[12], rank_labels[8];
    uint16_t coin_digits[10], total_digits[10], highlight_classes[3], unlock_image;
    CeremonyActorTrack tracks[8];
    CeremonyActorFrame frames[32];
} CeremonyImport;

static const size_t ceremony_object_uploads[4][2] = {
    {0xbf788, 0x2000}, {0xbf7ec, 0x2800}, {0xbffa8, 0x6000}, {0xc0910, 0}};

static uint16_t ceremony_cell(RiCtx *ctx, CeremonyImport *build, size_t address)
{
    if (build->image_count >= 341u)
        ri_fail(ctx, "ceremony source images exceed bound");
    const uint16_t id = (uint16_t)(43u + build->image_count);
    const unsigned parts = ri_get_u16(ctx, ctx->rom, address);
    unsigned darken = 1u;
    for (unsigned i = 0; i < parts; ++i)
    {
        const unsigned bank = ri_get_u16(ctx, ctx->rom, address + 6u + i * 6u) >> 12u;
        if (bank == 5u || bank == 6u || bank == 7u || bank >= 12u)
            darken = 0u;
    }
    build->darkenable[build->image_count] = (uint8_t)darken;
    build->images[build->image_count++] = ri_cell_image(
        ctx, ctx->rom, address, ri_view(build->object_tiles), ri_view(build->object_palette));
    return id;
}

static RiBuf *ceremony_indexed_map(RiCtx *ctx, RiData cells, RiData tiles, RiData palette,
                                   unsigned cx, unsigned cy, unsigned width, unsigned height,
                                   unsigned bank)
{
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "MKSCIMG2", 8u);
    ri_u16(ctx, out, 32u);
    ri_u16(ctx, out, 2u);
    ri_u16(ctx, out, (uint16_t)width);
    ri_u16(ctx, out, (uint16_t)height);
    ri_u16(ctx, out, 0u);
    ri_u16(ctx, out, 0u);
    ri_words(ctx, out, 3, (uint32_t)16u, (uint32_t)32u, (uint32_t)(64u + width * height));
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const unsigned v = ri_get_u16(ctx, cells, ((cy + y / 8u) * 32u + cx + x / 8u) * 2u);
            const unsigned sx = v & 0x400u ? 7u - x % 8u : x % 8u,
                           sy = v & 0x800u ? 7u - y % 8u : y % 8u;
            const size_t at = (v & 1023u) * 32u + sy * 4u + sx / 2u;
            if (at >= tiles.size)
                ri_fail(ctx, "ceremony indexed tile outside graphics");
            const unsigned nibble = tiles.data[at] >> (4u * (sx & 1u)) & 15u;
            const uint8_t pixel = (uint8_t)(nibble ? nibble : 255u);
            ri_put(ctx, out, &pixel, 1u);
        }
    if ((bank + 1u) * 32u > palette.size)
        ri_fail(ctx, "ceremony indexed palette outside colors");
    ri_put(ctx, out, palette.data + bank * 32u, 32u);
    return out;
}
static void ceremony_podium_art(RiCtx *ctx, CeremonyImport *build)
{
    build->object_tiles = ri_buf_zero(ctx, 32768);
    build->object_palette = ri_fe_lz(ctx, 0xbf5a4);
    for (unsigned i = 0; i < 4u; ++i)
        ri_fe_place(ctx, build->object_tiles, ceremony_object_uploads[i][1],
                    ri_view(ri_fe_lz(ctx, ceremony_object_uploads[i][0])));
    for (unsigned cup = 0; cup < 5u; ++cup)
    {
        ri_fe_place(ctx, build->object_tiles, 0x2600,
                    ri_view(ri_fe_lz(ctx, ri_fe_pointer(ctx, 0xdc4f0 + cup * 4u))));
        build->cup_labels[cup] = ceremony_cell(ctx, build, 0xc9944);
        for (unsigned i = 0; i < 23u; ++i)
            build->headers[cup][i] = ceremony_cell(
                ctx, build,
                ri_fe_pointer(ctx, i < 16u ? 0xdba64 + i * 4u : 0xdbaa4 + (i - 16u) * 4u));
    }
    for (unsigned i = 0; i < 8u; ++i)
    {
        ri_fe_place(ctx, build->object_tiles, 0x5800,
                    ri_view(ri_fe_lz(ctx, ri_fe_pointer(ctx, 0xdbce0 + i * 4u))));
        build->flypast[i] = ceremony_cell(ctx, build, 0xc9974);
    }
    for (unsigned rank = 0; rank < 3u; ++rank)
    {
        const size_t source = ri_fe_pointer(ctx, 0xdbe28 + rank * 4u),
                     colors = ri_fe_pointer(ctx, 0xdbe38 + rank * 4u);
        ri_fe_place(ctx, build->object_palette, 0x1e0, ri_fe_rom_slice(ctx, colors, colors + 32u));
        for (unsigned frame = 0; frame < 16u; ++frame)
        {
            ri_fe_place(ctx, build->object_tiles, 0x2200,
                        ri_view(ri_fe_lz(ctx, ri_fe_pointer(ctx, source + frame * 4u))));
            build->trophies[rank][frame] = ceremony_cell(ctx, build, 0xc996c);
        }
    }
    build->drop = ceremony_cell(ctx, build, 0xc979c);
    build->shadow = ceremony_cell(ctx, build, 0xc9954);
    ri_fe_place(ctx, build->object_tiles, 0x2000, ri_view(ri_fe_lz(ctx, 0xc17b8)));
    for (unsigned phase = 0; phase < 12u; ++phase)
    {
        const size_t colors = ri_fe_pointer(ctx, 0xdbd38 + phase * 4u);
        ri_fe_place(ctx, build->object_palette, 0xe0, ri_fe_rom_slice(ctx, colors, colors + 32u));
        build->sparkles[phase] = ceremony_cell(ctx, build, 0xc993c);
        build->confetti[phase] = ceremony_cell(ctx, build, 0xc9934);
    }
}

static void ceremony_timed_actors(RiCtx *ctx, CeremonyImport *build)
{
    for (unsigned i = 0; i < 4u; ++i)
        ri_fe_place(ctx, build->object_tiles, ceremony_object_uploads[i][1],
                    ri_view(ri_fe_lz(ctx, ceremony_object_uploads[i][0])));
    build->object_palette = ri_fe_lz(ctx, 0xbf5a4);
    for (unsigned index = 0; index < 8u; ++index)
    {
        const size_t record = 0xdc014 + index * 16u, sequence = ri_fe_pointer(ctx, record + 4u),
                     first = build->frame_count;
        build->tracks[index].x = (uint16_t)ri_get_u16(ctx, ctx->rom, record);
        build->tracks[index].y = (uint16_t)ri_get_u16(ctx, ctx->rom, record + 2u);
        build->tracks[index].depth_bias = (uint16_t)ri_get_u16(ctx, ctx->rom, record + 12u);
        build->tracks[index].first_frame = (uint16_t)first;
        build->tracks[index].initial_frame = (uint16_t)ri_get_u16(ctx, ctx->rom, record + 8u);
        build->tracks[index].remaining = (uint16_t)ri_get_u16(ctx, ctx->rom, record + 10u);
        build->tracks[index].reserved = 0u;
        for (unsigned frame = 0; frame < 32u; ++frame)
        {
            const uint32_t source = ri_get_u32(ctx, ctx->rom, sequence + frame * 8u);
            if (!source)
                break;
            if (build->frame_count >= 32u)
                ri_fail(ctx, "ceremony actor frames exceed bound");
            build->frames[build->frame_count].image_id =
                ceremony_cell(ctx, build, source - 0x08000000u);
            build->frames[build->frame_count++].duration =
                (uint16_t)ri_get_u16(ctx, ctx->rom, sequence + frame * 8u + 4u);
        }
        build->tracks[index].frame_count = (uint16_t)(build->frame_count - first);
        if (!build->tracks[index].frame_count ||
            build->tracks[index].initial_frame >= build->tracks[index].frame_count)
            ri_fail(ctx, "invalid ceremony actor sequence");
    }
}

static void ceremony_summary_art(RiCtx *ctx, CeremonyImport *build)
{
    static const size_t bg_uploads[9][2] = {
        {0xc19c4, 0},     {0xc2858, 0x160},  {0xc1a10, 0x2a0},  {0xc1a38, 0x700}, {0xc1ad8, 0xb00},
        {0xc1b44, 0xf00}, {0xc1bcc, 0x1300}, {0xc1c50, 0x1e40}, {0xc2e60, 0x2180}};
    build->background_palette = ri_buf_zero(ctx, 512);
    build->background_tiles = ri_buf_zero(ctx, 32768);
    ri_fe_place(ctx, build->background_tiles, 0, ri_fe_rom_slice(ctx, 0x62834, 0x66834));
    ri_fe_place(ctx, build->background_tiles, 0x2000, ri_fe_rom_slice(ctx, 0xabefc, 0xacefc));
    ri_fe_place(ctx, build->background_palette, 0x120, ri_view(ri_fe_lz(ctx, 0xc18fc)));
    for (unsigned i = 0; i < 9u; ++i)
        ri_fe_place(ctx, build->background_tiles, bg_uploads[i][1],
                    ri_view(ri_fe_lz(ctx, bg_uploads[i][0])));
    const size_t map_addresses[3] = {0xc2e60, 0xc2420, 0xc2654};
    for (unsigned i = 0; i < 3u; ++i)
    {
        RiBuf *cells = ri_buf_zero(ctx, 32u * 20u * 2u),
              *pixels = ri_buf_zero(ctx, 240u * 160u * 4u);
        ri_fe_place(ctx, cells, i == 0u ? 0x180u : 0u, ri_view(ri_fe_lz(ctx, map_addresses[i])));
        if (i != 0u)
        {
            static const unsigned regions[3][4] = {
                {72, 80, 24, 16}, {208, 48, 24, 24}, {216, 72, 24, 16}};
            for (unsigned r = 0; r < 3u; ++r)
                for (unsigned cy = regions[r][1] / 8u; cy < (regions[r][1] + regions[r][3]) / 8u;
                     ++cy)
                    memset(cells->data + (cy * 32u + regions[r][0] / 8u) * 2u, 0,
                           regions[r][2] / 8u * 2u);
            static const unsigned columns[4][2] = {{112, 16}, {136, 56}, {200, 8}, {224, 16}};
            for (unsigned row = 0; row < 4u; ++row)
                for (unsigned c = 0; c < 4u; ++c)
                    for (unsigned cy = 12u + row * 2u; cy < 14u + row * 2u; ++cy)
                        memset(cells->data + (cy * 32u + columns[c][0] / 8u) * 2u, 0,
                               columns[c][1] / 8u * 2u);
        }
        for (unsigned y = 0; y < 160u; ++y)
            for (unsigned x = 0; x < 240u; ++x)
            {
                const uint16_t cell =
                    (uint16_t)ri_get_u16(ctx, ri_view(cells), ((y / 8u) * 32u + x / 8u) * 2u);
                ri_set_pixel(pixels, (size_t)y * 240u + x,
                             ri_tile_pixel(ctx, ri_view(build->background_tiles),
                                           ri_view(build->background_palette), cell & 1023u,
                                           cell & 0x400u ? 7u - x % 8u : x % 8u,
                                           cell & 0x800u ? 7u - y % 8u : y % 8u, cell >> 12u, 4));
            }
        build->panels[i] = (uint16_t)(43u + build->image_count);
        build->images[build->image_count++] = ceremony_image(ctx, 240u, 160u, pixels);
    }
    build->summary_map = ri_fe_lz(ctx, 0xc2330);
    for (unsigned index = 0; index < 20u; ++index)
    {
        const unsigned width = index < 12u ? 8u : 16u;
        RiBuf *pixels = ri_buf_zero(ctx, width * 16u * 4u);
        for (unsigned y = 0; y < 16u; ++y)
            for (unsigned x = 0; x < width; ++x)
            {
                const size_t at = index < 12u
                                      ? ((10u + y / 8u) * 32u + index) * 2u
                                      : ((3u + y / 8u) * 32u + (index - 12u) * 2u + x / 8u) * 2u;
                const unsigned value = ri_get_u16(ctx, ri_view(build->summary_map), at);
                ri_set_pixel(pixels, (size_t)y * width + x,
                             ri_tile_pixel(ctx, ri_view(build->background_tiles),
                                           ri_view(build->background_palette), value & 1023u,
                                           x % 8u, y % 8u, value >> 12u, 4));
            }
        const uint16_t id = (uint16_t)(43u + build->image_count);
        build->images[build->image_count++] = ceremony_image(ctx, width, 16u, pixels);
        if (index < 12u)
            build->glyphs[index] = id;
        else
            build->rank_labels[index - 12u] = id;
    }
    RiBuf *coin_pixels = ri_buf_zero(ctx, 8u * 16u * 4u);
    for (unsigned y = 0; y < 16u; ++y)
        for (unsigned x = 0; x < 8u; ++x)
            ri_set_pixel(coin_pixels, y * 8u + x,
                         ri_tile_pixel(ctx, ri_view(build->background_tiles),
                                       ri_view(build->background_palette), 0x15u + y / 8u, x,
                                       y % 8u, 12u, 4));
    build->summary_coin = (uint16_t)(43u + build->image_count);
    build->images[build->image_count++] = ceremony_image(ctx, 8u, 16u, coin_pixels);
}

static void ceremony_indexed_highlights(RiCtx *ctx, CeremonyImport *build)
{
    ri_fe_place(ctx, build->object_tiles, 0x5800, ri_view(ri_fe_lz(ctx, 0xc17e0)));
    build->object_palette = ri_fe_lz(ctx, 0xbf5a4);
    for (unsigned i = 0; i < 10u; ++i)
        build->coin_digits[i] = ceremony_cell(ctx, build, ri_fe_pointer(ctx, 0xdbbb8 + i * 4u));
    for (unsigned i = 0; i < 10u; ++i)
    {
        build->total_digits[i] = (uint16_t)(43u + build->image_count);
        build->images[build->image_count++] =
            ceremony_indexed_map(ctx, ri_view(build->summary_map), ri_view(build->background_tiles),
                                 ri_view(build->background_palette), i, 10u, 8u, 16u, 15u);
    }
    for (unsigned i = 0; i < 3u; ++i)
    {
        build->highlight_classes[i] = (uint16_t)(43u + build->image_count);
        build->images[build->image_count++] =
            ceremony_indexed_map(ctx, ri_view(build->summary_map), ri_view(build->background_tiles),
                                 ri_view(build->background_palette),
                                 i == 0u   ? 1u
                                 : i == 1u ? 3u
                                           : 6u,
                                 5u, i == 0u ? 16u : 24u, 16u, 14u);
    }
    build->unlock_image = (uint16_t)(43u + build->image_count);
    build->images[build->image_count++] =
        ceremony_indexed_map(ctx, ri_view(ri_fe_lz(ctx, 0xc2830)), ri_view(build->background_tiles),
                             ri_view(build->background_palette), 0u, 0u, 120u, 8u, 14u);
}

static RiBuf *ceremony_emit_payload(RiCtx *ctx, CeremonyImport *build)
{
    RiBuf *out = ceremony_legacy_payload(ctx);
    out->data[0] = 0x47u;
    out->data[1] = 0x50u;
    out->data[2] = 0x43u;
    out->data[3] = 0x34u;
    ri_words(ctx, out, 4, (uint32_t)0x31524543u, (uint32_t)100u, (uint32_t)build->image_count,
             (uint32_t)build->frame_count);
    for (unsigned cup = 0; cup < 5u; ++cup)
        for (unsigned i = 0; i < 23u; ++i)
            ri_u16(ctx, out, build->headers[cup][i]);
    for (unsigned i = 0; i < 8u; ++i)
        ri_u16(ctx, out, build->flypast[i]);
    for (unsigned rank = 0; rank < 3u; ++rank)
        for (unsigned i = 0; i < 16u; ++i)
            ri_u16(ctx, out, build->trophies[rank][i]);
    ri_u16(ctx, out, 0u);
    ri_u16(ctx, out, build->drop);
    ri_u16(ctx, out, build->sparkles[0]);
    ri_u16(ctx, out, build->confetti[0]);
    ri_put(ctx, out, ctx->rom.data + 0xdbe34, 3u);
    ri_put_zero(ctx, out, 1u);
    for (unsigned i = 0; i < 8u; ++i)
    {
        ri_u16(ctx, out, build->tracks[i].x);
        ri_u16(ctx, out, build->tracks[i].y);
        ri_u16(ctx, out, build->tracks[i].depth_bias);
        ri_u16(ctx, out, build->tracks[i].first_frame);
        ri_u16(ctx, out, build->tracks[i].frame_count);
        ri_u16(ctx, out, build->tracks[i].initial_frame);
        ri_u16(ctx, out, build->tracks[i].remaining);
        ri_u16(ctx, out, build->tracks[i].reserved);
    }
    for (size_t i = 0; i < build->frame_count; ++i)
    {
        ri_u16(ctx, out, build->frames[i].image_id);
        ri_u16(ctx, out, build->frames[i].duration);
    }
    for (unsigned i = 0; i < 3u; ++i)
        ri_u16(ctx, out, build->panels[i]);
    ri_u16(ctx, out, 0u);
    for (unsigned i = 0; i < 12u; ++i)
        ri_u16(ctx, out, build->sparkles[i]);
    for (unsigned i = 0; i < 12u; ++i)
        ri_u16(ctx, out, build->confetti[i]);
    ri_put(ctx, out, ctx->rom.data + 0xdc22c, 36u);
    ri_u16(ctx, out, build->shadow);
    ri_u16(ctx, out, build->summary_coin);
    for (unsigned group = 0; group < 2u; ++group)
    {
        const size_t cell_address = group ? 0xc9890u : 0xc97acu;
        const unsigned parts = ri_get_u16(ctx, ctx->rom, cell_address), length = group ? 7u : 16u;
        if (!parts)
            ri_fail(ctx, "empty ceremony header geometry");
        for (unsigned i = 0; i < length; ++i)
        {
            const size_t part = cell_address + 2u + (i < parts ? i : parts - 1u) * 6u;
            const unsigned a = ri_get_u16(ctx, ctx->rom, part),
                           b = ri_get_u16(ctx, ctx->rom, part + 2u) & 511u;
            const int x = b > 255u ? -(int)(b ^ 511u) : (int)b;
            const int y = (a & 255u) >= 128u ? (int)(a & 255u) - 256 : (int)(a & 255u);
            ri_u16(ctx, out, (uint16_t)(x + (group ? 136 : 160)));
            ri_u16(ctx, out, (uint16_t)(y + 32));
        }
    }
    for (unsigned i = 0; i < 5u; ++i)
        ri_u16(ctx, out, build->cup_labels[i]);
    for (unsigned i = 0; i < 12u; ++i)
        ri_u16(ctx, out, build->glyphs[i]);
    for (unsigned i = 0; i < 8u; ++i)
        ri_u16(ctx, out, build->rank_labels[i]);
    for (unsigned i = 0; i < 10u; ++i)
        ri_u16(ctx, out, build->coin_digits[i]);
    for (unsigned i = 0; i < 10u; ++i)
        ri_u16(ctx, out, build->total_digits[i]);
    for (unsigned i = 0; i < 3u; ++i)
        ri_u16(ctx, out, build->highlight_classes[i]);
    ri_u16(ctx, out, build->unlock_image);
    static const size_t pulse_addresses[4] = {0xaad9c, 0xaaddc, 0xaad3c, 0xaad7c};
    for (unsigned i = 0; i < 4u; ++i)
        ri_put(ctx, out, ctx->rom.data + pulse_addresses[i], 32u);
    ri_put(ctx, out, build->darkenable, build->image_count);
    for (size_t i = 0; i < build->image_count; ++i)
    {
        ri_u32(ctx, out, (uint32_t)build->images[i]->size);
        ri_put_buf(ctx, out, build->images[i]);
    }
    return out;
}

RiBuf *ri_fe_ceremony_payload(RiCtx *ctx)
{
    CeremonyImport build = {0};
    ceremony_podium_art(ctx, &build);
    /* Timed actors reload their constructor bank after trophy/palette edits. */
    ceremony_timed_actors(ctx, &build);
    ceremony_summary_art(ctx, &build);
    ceremony_indexed_highlights(ctx, &build);
    return ceremony_emit_payload(ctx, &build);
}

/* ---- Group ------------------------------------------------------------- */

void ri_group_frontend(RiCtx *ctx)
{
    ri_fe_title_sections(ctx);
    ri_fe_menu_sections(ctx);
    ri_fe_countdown_sections(ctx);
    ri_fe_character_sections(ctx);
    ri_fe_course_sections(ctx);
    ri_fe_pause_sections(ctx);
    ri_fe_failure_sections(ctx);
    ui_text_sections(ctx);
    ri_section(ctx, 24, 1, 4, ri_fe_ceremony_payload(ctx));
}
