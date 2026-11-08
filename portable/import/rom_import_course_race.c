#include "rom_import_courses.h"

#include <string.h>

/* Port of tools/portable/course_rom.py course_sections (with its raster,
 * terrain, sky, scenery and actor decoders) and rom_course_art.rom_grid. */

/* 36D1C/365B0: weather uses the course scenery bank, not common effects. */
static RiBuf *weather_payload(RiCtx *ctx, uint32_t behavior, RiData tiles, RiData palette)
{
    const uint32_t enabled = behavior == 16u;
    RiBuf *out = ri_buf_zero(ctx, 48u);
    ri_set_u32(out, 0u, UINT32_C(0x31485457));
    ri_set_u32(out, 4u, 1u);
    out->data[8] = (uint8_t)enabled;
    out->data[9] = enabled ? 12u : 0u;
    if (!enabled)
        return out;
    for (unsigned direction = 0u; direction < 2u; ++direction)
        for (unsigned i = 0u; i < 8u; ++i)
        {
            const uint32_t choice = ri_get_u16(ctx, ctx->rom, 0xEB37Cu + direction * 16u + i * 2u);
            if (choice > 3u)
                ri_fail(ctx, "weather spawn choice outside stages0..3");
            out->data[12u + direction * 8u + i] = (uint8_t)choice;
        }
    static const size_t cells[3] = {0x7C2C8u, 0x7C2C0u, 0x7C2B8u};
    for (unsigned i = 0u; i < 3u; ++i)
    {
        RiBuf *image = ri_cell_image_indexed(ctx, ctx->rom, cells[i], tiles, palette, 0u);
        ri_set_u32(out, 28u + i * 4u, (uint32_t)image->size);
        ri_put_buf(ctx, out, image);
        ri_buf_free(ctx, image);
    }
    return out;
}

enum
{
    TRACK_TABLE = 0xE7FEC,
    COURSE_CONTAINER = 0x258000,
    COURSE_CONTAINER_SIZE = 0xB0000,
    DIRECTORY_COUNT = 49,
    HEADER_COUNT = 52,
    SKY_CONTAINER = 0x234000,
    SKY_CONTAINER_SIZE = 0x24000,
    ROAD_TILE_BYTES = 0x4000,
    SCENERY_ART_BYTES = 0x2000,
    SCENERY_PALETTE_BYTES = 96,
    MAX_SCENERY_DESCRIPTORS = 128,
};

/* ---- Python int helpers ------------------------------------------------- */

size_t rc_offset(RiCtx *ctx, int64_t value)
{
    if (value < 0 || value > INT32_MAX)
        ri_fail(ctx, "course data range outside source: %lld", (long long)value);
    return (size_t)value;
}

uint32_t rc_u8(RiCtx *ctx, RiData source, int64_t offset)
{
    return ri_get_u8(ctx, source, rc_offset(ctx, offset));
}

uint32_t rc_u16(RiCtx *ctx, RiData source, int64_t offset)
{
    return ri_get_u16(ctx, source, rc_offset(ctx, offset));
}

uint32_t rc_u32(RiCtx *ctx, RiData source, int64_t offset)
{
    return ri_get_u32(ctx, source, rc_offset(ctx, offset));
}

RiData rc_span(RiCtx *ctx, RiData source, int64_t offset, size_t size)
{
    return ri_span(ctx, source, rc_offset(ctx, offset), size);
}

void rc_put_span(RiCtx *ctx, RiBuf *out, RiData source, int64_t offset, size_t size)
{
    ri_put(ctx, out, rc_span(ctx, source, offset, size).data, size);
}

void rc_word(RiCtx *ctx, RiBuf *out, int64_t value)
{
    if (value < 0 || value > (int64_t)UINT32_MAX)
        ri_fail(ctx, "struct.pack 'I' value out of range: %lld", (long long)value);
    ri_u32(ctx, out, (uint32_t)value);
}

void rc_half(RiCtx *ctx, RiBuf *out, int64_t value)
{
    if (value < 0 || value > 0xFFFF)
        ri_fail(ctx, "struct.pack 'H' value out of range: %lld", (long long)value);
    ri_u16(ctx, out, (uint32_t)value);
}

void rc_byte(RiCtx *ctx, RiBuf *out, int64_t value)
{
    if (value < 0 || value > 0xFF)
        ri_fail(ctx, "struct.pack 'B' value out of range: %lld", (long long)value);
    ri_u8(ctx, out, (uint32_t)value);
}

static void put_i32(RiCtx *ctx, RiBuf *out, int64_t value)
{
    if (value < INT32_MIN || value > INT32_MAX)
        ri_fail(ctx, "struct.pack 'i' value out of range: %lld", (long long)value);
    ri_i32(ctx, out, (int32_t)value);
}

/* ---- rom_course_art.rom_grid --------------------------------------------- */

uint32_t rc_rom_grid(RiCtx *ctx, uint32_t header, uint32_t slots, RcGridRow rows[8])
{
    RiData rom = ctx->rom;
    int64_t size = (int64_t)rom.size;
    if (slots != 4 && slots != 8)
        ri_fail(ctx, "source grid supports four or eight slots");
    int64_t definition = (int64_t)rc_u32(ctx, rom, TRACK_TABLE + (int64_t)header * 4) - 0x08000000;
    if (definition < 0 || definition + 8 > size)
        ri_fail(ctx, "track definition pointer outside ROM");
    uint32_t course_id = rc_u32(ctx, rom, definition);
    if (course_id >= DIRECTORY_COUNT)
        ri_fail(ctx, "course outside directory");
    int64_t directory = COURSE_CONTAINER + (int64_t)course_id * 4;
    if (directory + 8 > size)
        ri_fail(ctx, "course directory outside ROM");
    int64_t start = COURSE_CONTAINER + (int64_t)rc_u32(ctx, rom, directory);
    /* The final entry terminates at the container boundary, not another offset. */
    int64_t end = COURSE_CONTAINER + (course_id + 1u < DIRECTORY_COUNT
                                          ? (int64_t)rc_u32(ctx, rom, directory + 4)
                                          : COURSE_CONTAINER_SIZE);
    if (end > size || start + 0x9C > end)
        ri_fail(ctx, "course block outside ROM");
    int64_t cursor = start + (int64_t)rc_u32(ctx, rom, start + 0x98);
    int filled[8] = {0};
    int headings = rc_u8(ctx, rom, start + 0xCA) != 0;
    if (headings && slots != 4)
        ri_fail(ctx, "source directional grid has four player starts");
    while (cursor + 4 <= end)
    {
        const uint8_t *record = rc_span(ctx, rom, cursor, 4).data;
        cursor += 4;
        uint32_t opcode = record[0];
        if (!opcode)
            break;
        /* sub_8031ED8 maps boundary opcodes 0x81..0x88 to grid rows 0..7. */
        if (opcode >= 0x81 && opcode < 0x81 + slots)
        {
            uint32_t rank = opcode - 0x81;
            if (filled[rank])
                ri_fail(ctx, "duplicate grid rank %u", (unsigned)rank);
            filled[rank] = 1;
            /* 31ED8 omits the X center for Extra headers (32BC8 bit5).
             * Directional Battle starts use the four E745C headings. */
            rows[rank].x = (record[1] * 8u + (header & 32 ? 0u : 4u)) << 16;
            rows[rank].y = (record[2] * 8u + 4u) << 16;
            rows[rank].heading = headings ? rc_u16(ctx, rom, 0xE745C + rank * 2) : 0;
        }
    }
    for (uint32_t rank = 0; rank < slots; ++rank)
        if (!filled[rank])
            ri_fail(ctx, "course grid is incomplete");
    return course_id;
}

/* ---- Container decoders ---------------------------------------------------- */

/* Data.lz: an exact-size decode. */
static RiBuf *exact_lz(RiCtx *ctx, RiData source, int64_t offset, size_t size)
{
    rc_span(ctx, source, offset, 4);
    RiBuf *data = ri_lz77(ctx, source, rc_offset(ctx, offset));
    if (data->size != size)
        ri_fail(ctx, "course decompression size at %#llx", (long long)offset);
    return data;
}

static RiBuf *time_trial_record_defaults(RiCtx *ctx, uint32_t header, size_t definition)
{
    int eligible = 0;
    for (unsigned catalog = 0u; catalog < 2u; ++catalog)
        for (unsigned slot = 0u; slot < 20u; ++slot)
            if (ri_get_u32(ctx, ctx->rom, (catalog ? 0xe74c4u : 0xe7464u) + slot * 4u) == header)
                eligible = 1;
    if (!eligible)
        return NULL;
    const size_t source = ri_pointer(ctx, ctx->rom, definition + 0x20u);
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31525454u, 6u);
    uint16_t previous = 0u;
    for (unsigned row = 0u; row < 6u; ++row)
    {
        const uint8_t *record = ri_at(ctx, ctx->rom, source + (row < 5u ? row + 1u : 0u) * 4u, 4u);
        const uint16_t time = (uint16_t)(record[2] | ((uint16_t)record[3] << 8u));
        if (record[0] > 7u || record[1] != 0u || !time || time > 59999u ||
            (row < 5u && time < previous))
            ri_fail(ctx, "invalid Time Trial default record");
        if (row < 5u)
            previous = time;
        ri_put(ctx, out, record, 4u);
    }
    return out;
}

static RiBuf *gp_opening_payload(RiCtx *ctx, uint32_t header, size_t definition)
{
    unsigned cup = 5u;
    for (unsigned catalog = 0u; catalog < 2u; ++catalog)
        for (unsigned slot = 0u; slot < 20u; ++slot)
            if (ri_get_u32(ctx, ctx->rom, (catalog ? 0xE74C4u : 0xE7464u) + slot * 4u) == header &&
                cup == 5u)
                cup = slot / 4u;
    if (cup == 5u)
        return NULL; /* Private stages have no cup opening. */
    RiBuf *out = ri_buf_zero(ctx, 5224u);
    ri_set_u32(out, 0u, UINT32_C(0x31544F47));
    ri_set_u32(out, 4u, 0xA00u);
    memcpy(out->data + 8u + 0x20u, ri_at(ctx, ctx->rom, 0x9AA4Cu, 192u), 192u);
    const size_t offsets[3] = {ri_pointer(ctx, ctx->rom, definition + 0x30u),
                               ri_pointer(ctx, ctx->rom, 0xEFD58u + cup * 4u), 0x76EA8u};
    const size_t sizes[3] = {768u, 1472u, 32u}, destinations[3] = {0xE0u, 0x400u, 0x9C0u};
    for (unsigned i = 0u; i < 3u; ++i)
    {
        RiBuf *tiles = exact_lz(ctx, ctx->rom, (int64_t)offsets[i], sizes[i]);
        memcpy(out->data + 8u + destinations[i], tiles->data, tiles->size);
        ri_buf_free(ctx, tiles);
    }
    memcpy(out->data + 8u + 0xA00u, ri_at(ctx, ctx->rom, 0x76EB4u, 0xC0u), 0xC0u);
    memcpy(out->data + 8u + 0xA00u + 18u * 64u, ri_at(ctx, ctx->rom, 0x76F74u, 128u), 128u);
    memcpy(out->data + 8u + 0xA00u + 0x800u,
           ri_at(ctx, ctx->rom, ri_pointer(ctx, ctx->rom, 0xEFD44u + cup * 4u), 32u), 32u);
    memcpy(out->data + 8u + 0xA00u + 0x800u + 32u, ri_at(ctx, ctx->rom, 0x9AA4Cu, 3u * 192u),
           3u * 192u);
    return out;
}

/* padded_lz: decode into a cleared fixed-capacity destination. */
static RiBuf *padded_lz(RiCtx *ctx, RiData source, int64_t offset, size_t size)
{
    rc_span(ctx, source, offset, 4);
    RiBuf *data = ri_lz77(ctx, source, rc_offset(ctx, offset));
    if (data->size > size)
        ri_fail(ctx, "course decompression exceeds destination");
    ri_resize(ctx, data, size);
    return data;
}

static RiBuf *split_lz(RiCtx *ctx, RiData source, int64_t offset, size_t blocks, size_t stride)
{
    RiBuf *data = ri_buf_zero(ctx, blocks * stride);
    for (size_t block = 0; block < blocks; ++block)
    {
        uint32_t relative = rc_u16(ctx, source, offset + (int64_t)block * 2);
        if (!relative)
            continue;
        RiBuf *chunk = padded_lz(ctx, source, offset + relative, stride);
        memcpy(data->data + block * stride, chunk->data, stride);
        ri_buf_free(ctx, chunk);
    }
    return data;
}

/* stream: consecutive 4-byte records up to a zero opcode. */
typedef struct Records
{
    const uint8_t *data;
    size_t count;
} Records;

static Records stream(RiCtx *ctx, RiData course, int64_t field)
{
    Records records = {NULL, 0};
    int64_t offset = rc_u32(ctx, course, field);
    if (!offset)
        return records;
    for (;;)
    {
        const uint8_t *record = rc_span(ctx, course, offset + (int64_t)records.count * 4, 4).data;
        if (!records.count)
            records.data = record;
        if (!record[0])
            return records;
        records.count++;
    }
}

/* raster: sub_8032CA0 checkpoint rectangles/triangles, in original write order. */
typedef struct Raster
{
    uint32_t width, count;
    RiBuf *cells;
} Raster;

static Raster raster(RiCtx *ctx, RiData course)
{
    Raster result;
    int64_t base = rc_u32(ctx, course, 0xCC);
    uint32_t count = rc_u8(ctx, course, base);
    int64_t offset = base + rc_u16(ctx, course, base + 1);
    int64_t width = (int64_t)rc_u8(ctx, course, 4) * 64;
    RiBuf *cells = ri_buf_zero(ctx, (size_t)(width * width));
    memset(cells->data, 127, cells->size);
    for (uint32_t index = 0; index < count; ++index)
    {
        const uint8_t *entry = rc_span(ctx, course, offset + (int64_t)index * 12, 12).data;
        uint32_t kind = entry[0];
        int64_t x = ri_le16(entry + 1), y = ri_le16(entry + 3), extent = ri_le16(entry + 5) + 1;
        if (kind > 4)
            ri_fail(ctx, "unsupported checkpoint shape %u", (unsigned)kind);
        int64_t height = kind == 0 ? ri_le16(entry + 7) + 1 : extent;
        int64_t signed_value = index == 0 || index == count - 1 ? (int64_t)index - 128 : index;
        uint8_t value = (uint8_t)(signed_value & 255);
        for (int64_t row = 0; row < height; ++row)
        {
            int64_t cy = kind >= 3 ? y - row : y + row;
            int64_t columns = kind == 0 ? extent : extent - row;
            for (int64_t column = 0; column < columns; ++column)
            {
                int64_t cx = kind == 2 || kind == 3 ? x - column : x + column;
                if (cx >= 0 && cx < width && cy >= 0 && cy < width)
                    cells->data[cy * width + cx] = value;
            }
        }
    }
    result.width = (uint32_t)width;
    result.count = count;
    result.cells = cells;
    return result;
}

typedef struct Terrain
{
    uint32_t width, height;
    RiBuf *cells;
} Terrain;

static Terrain terrain(RiCtx *ctx, RiData course)
{
    Terrain result;
    result.width = rc_u8(ctx, course, 4) * 128;
    result.height = rc_u8(ctx, course, 5) * 128;
    if (!result.width || !result.height)
        ri_fail(ctx, "empty course terrain dimensions");
    int64_t offset = rc_u32(ctx, course, 0xD4);
    RiBuf *data = NULL;
    if (rc_u8(ctx, course, 3) & 2)
    {
        data = ri_buf_zero(ctx, 0x10000);
        for (int64_t block = 0; block < 16; ++block)
        {
            uint32_t relative = rc_u16(ctx, course, offset + block * 2);
            if (!relative)
                continue;
            RiBuf *chunk = ri_lz77(ctx, course, rc_offset(ctx, offset + relative));
            if (chunk->size > 0x1000)
                ri_fail(ctx, "course terrain block exceeds destination");
            memcpy(data->data + block * 0x1000, chunk->data, chunk->size);
            ri_buf_free(ctx, chunk);
        }
    }
    else
    {
        data = ri_lz77(ctx, course, rc_offset(ctx, offset));
    }
    size_t cells = (size_t)result.width * result.height;
    if (data->size < cells)
        ri_fail(ctx, "course terrain is truncated");
    ri_resize(ctx, data, cells);
    result.cells = data;
    return result;
}

/* The course container with its validated 49-entry directory. */
typedef struct Directory
{
    RiData container;
    int64_t offsets[DIRECTORY_COUNT + 1];
} Directory;

static RiData directory_entry(RiCtx *ctx, const Directory *directory, uint32_t course_id)
{
    if (course_id >= DIRECTORY_COUNT)
        ri_fail(ctx, "referenced course outside directory");
    return rc_span(ctx, directory->container, directory->offsets[course_id],
                   (size_t)(directory->offsets[course_id + 1] - directory->offsets[course_id]));
}

static size_t track_definition(RiCtx *ctx, uint32_t header)
{
    return ri_pointer(ctx, ctx->rom, TRACK_TABLE + (size_t)header * 4);
}

/* source_course: signed header-table aliases used by loadtrack_loadTileset/sub_803159C. */
static RiData source_course(RiCtx *ctx, const Directory *directory, uint32_t header, int64_t field)
{
    RiData course =
        directory_entry(ctx, directory, ri_get_u32(ctx, ctx->rom, track_definition(ctx, header)));
    int32_t relative = (int32_t)(int8_t)(uint8_t)rc_u8(ctx, course, field);
    if (relative)
    {
        int64_t target = (int64_t)header + relative;
        if (target < 0 || target >= HEADER_COUNT)
            ri_fail(ctx, "course reference outside header table");
        course = directory_entry(
            ctx, directory, ri_get_u32(ctx, ctx->rom, track_definition(ctx, (uint32_t)target)));
    }
    return course;
}

static RiBuf *road_tiles(RiCtx *ctx, RiData course)
{
    int64_t offset = rc_u32(ctx, course, 0x80);
    uint32_t flags = rc_u8(ctx, course, 3);
    if (!rc_u8(ctx, course, 1))
        return ri_buf_copy(ctx, rc_span(ctx, course, offset, ROAD_TILE_BYTES).data,
                           ROAD_TILE_BYTES);
    if (flags & 1)
        return split_lz(ctx, course, offset, 4, 0x1000);
    return padded_lz(ctx, course, offset, ROAD_TILE_BYTES);
}

/* sky / sky_shapes: track_loadSkySize lets one layer extend horizontally or vertically. */
/* Source background callbacks recovered as native profile data. */
static RiBuf *sky_motion(RiCtx *ctx, uint32_t behavior, uint32_t material)
{
    static const struct
    {
        uint8_t behavior, controller, shifts[3];
        int16_t x[3], y[3];
    } profiles[] = {
        {1u, 0u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 38}},
        {2u, 1u, {7, 7, 8}, {0, -96, 0}, {38, 32, 38}},
        {3u, 0u, {6, 7, 8}, {0, 0, 0}, {38, 38, 38}},
        {4u, 0u, {6, 7, 8}, {-40, 0, 0}, {70, 70, 70}},
        {6u, 4u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 182}},
        {7u, 0u, {6, 7, 8}, {0, 0, 0}, {38, 38, 38}},
        {8u, 2u, {0, 8, 7}, {0, 0, 0}, {38, -10, 38}},
        {10u, 0u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 38}},
        {11u, 0u, {6, 7, 8}, {0, 0, 0}, {38, 38, 38}},
        {13u, 0u, {6, 7, 8}, {0, -40, 0}, {38, 38, 38}},
        {14u, 0u, {6, 7, 8}, {0, 0, 0}, {38, 38, 38}},
        {15u, 0u, {6, 7, 8}, {0, 0, 0}, {38, 38, 38}},
        {16u, 6u, {6, 6, 7}, {0, 0, -24}, {38, 38, 38}},
        {17u, 0u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 38}},
        {18u, 0u, {6, 7, 7}, {-40, 0, 0}, {38, 38, 38}},
        {19u, 3u, {8, 7, 6}, {0, 0, 0}, {352, 6, 6}},
        {20u, 7u, {8, 7, 6}, {0, 0, 0}, {38, 38, 38}},
        {21u, 0u, {6, 7, 8}, {0, 0, 0}, {38, 38, 38}},
        {22u, 0u, {6, 7, 8}, {0, 0, 0}, {38, 38, 38}},
        {23u, 5u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 38}},
        {28u, 0u, {6, 7, 8}, {-40, 0, 0}, {70, 70, 70}},
        {29u, 0u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 38}},
        {37u, 0u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 182}},
        {43u, 0u, {6, 7, 8}, {-40, 0, 0}, {38, 38, 182}},
        {51u, 0u, {8, 7, 6}, {0, 0, 0}, {32, -88, 6}},
    };
    for (size_t index = 0u; index < sizeof profiles / sizeof profiles[0]; ++index)
    {
        if (profiles[index].behavior != behavior)
            continue;
        RiBuf *out = ri_buf(ctx);
        ri_words(ctx, out, 2, 0x31524d53u, 1u);
        ri_u8(ctx, out, profiles[index].controller);
        ri_u8(ctx, out, 7u);
        ri_u8(ctx, out, 0u);
        ri_u8(ctx, out, 0u);
        ri_u8(ctx, out, 0u);
        for (unsigned layer = 0u; layer < 3u; ++layer)
            ri_u8(ctx, out, profiles[index].shifts[layer]);
        for (unsigned layer = 0u; layer < 3u; ++layer)
            ri_u16(ctx, out, (uint16_t)profiles[index].x[layer]);
        for (unsigned layer = 0u; layer < 3u; ++layer)
            ri_u16(ctx, out, (uint16_t)profiles[index].y[layer]);
        ri_u16(ctx, out, behavior == 8u ? 24u : UINT16_MAX);
        ri_u8(ctx, out, behavior == 8u ? 2u : 0u);
        ri_u8(ctx, out, behavior == 8u ? 8u : 0u);
        ri_u16(ctx, out, 0u);
        const int reverse = behavior == 19u || behavior == 20u || behavior == 51u;
        ri_u8(ctx, out, reverse ? 0u : 2u);
        ri_u8(ctx, out, 0u);
        ri_u8(ctx, out, reverse ? 2u : 0u);
        ri_u8(ctx, out, 1u);
        ri_u8(ctx, out, reverse ? 0u : 2u);
        const int occludes = behavior == 8u || behavior == 10u || behavior == 17u ||
                             behavior == 19u || behavior == 20u || behavior == 21u ||
                             behavior == 28u || behavior == 51u;
        ri_u8(ctx, out, occludes ? (material == 14u ? 3u : 1u) : 0u);
        return out;
    }
    ri_fail(ctx, "unsupported sky motion behavior %u", (unsigned)behavior);
}

static RiBuf *sky(RiCtx *ctx, uint32_t background, uint32_t behavior, uint32_t material)
{
    static const struct
    {
        uint32_t size, layer, width, height;
    } extended[6] = {{0x20, 0, 64, 32}, {4, 0, 32, 64},    {0x40, 1, 64, 32},
                     {8, 1, 32, 64},    {0x80, 2, 64, 32}, {0x10, 2, 32, 64}};
    RiData container = ri_span(ctx, ctx->rom, SKY_CONTAINER, SKY_CONTAINER_SIZE);
    int64_t base = rc_u32(ctx, container, (int64_t)background * 4);
    const uint8_t *head = rc_span(ctx, container, base, 2).data;
    uint32_t size = head[0], flags = head[1];
    uint32_t widths[3] = {32, 32, 32}, heights[3] = {32, 32, 32};
    int found = 0;
    for (int index = 0; index < 6; ++index)
        if (extended[index].size == size)
        {
            widths[extended[index].layer] = extended[index].width;
            heights[extended[index].layer] = extended[index].height;
            found = 1;
        }
    if (!found && size != 1 && size != 2)
        ri_fail(ctx, "unsupported sky shape %u", (unsigned)size);
    if ((flags & 3) != 3)
        ri_fail(ctx, "unsupported sky compression %u", (unsigned)flags);
    int64_t fields[5];
    for (int index = 0; index < 5; ++index)
        fields[index] = base + (int64_t)rc_u32(ctx, container, base + 4 + index * 4);
    RiBuf *tiles = split_lz(ctx, container, fields[0], 5, 0x800);
    RiBuf *maps[3];
    for (int index = 0; index < 3; ++index)
        maps[index] = padded_lz(ctx, container, fields[1 + index],
                                (size_t)widths[index] * heights[index] * 2);
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 3, 0x31594B53u, (uint32_t)tiles->size, 63u);
    for (int index = 0; index < 3; ++index)
        ri_u32(ctx, out, (uint32_t)maps[index]->size);
    for (int index = 0; index < 3; ++index)
        ri_u32(ctx, out, widths[index]);
    RiBuf *motion = sky_motion(ctx, behavior, material);
    ri_words(ctx, out, 7, 32u, size, flags, rc_u16(ctx, container, base + 2), 96u, background,
             (uint32_t)motion->size);
    ri_put_buf(ctx, out, tiles);
    rc_put_span(ctx, out, container, fields[4], 126);
    for (int index = 0; index < 3; ++index)
        ri_put_buf(ctx, out, maps[index]);
    ri_put_buf(ctx, out, motion);
    ri_buf_free(ctx, motion);
    ri_buf_free(ctx, tiles);
    for (int index = 0; index < 3; ++index)
        ri_buf_free(ctx, maps[index]);
    return out;
}

/* ---- Placed objects ------------------------------------------------------- */

/* loadtrack_loadObjects class tables per header 4..28; 53DC4 selects the
 * common table outside headers 4..28. */
static const uint32_t class_tables[25] = {
    0xF1054, 0xF1060, 0xF1088, 0xF10A0, 0xF1118, 0xF1130, 0xF1154, 0xF10A0, 0xF1174,
    0xF1188, 0xF11A0, 0xF10A0, 0xF11C0, 0xF11D8, 0xF1204, 0xF1228, 0xF125C, 0xF11EC,
    0xF10A0, 0xF126C, 0xF1008, 0xF10A0, 0xF1154, 0xF1008, 0xF1054};

static uint32_t object_code(RiCtx *ctx, uint32_t header, uint32_t kind)
{
    int64_t table = kind > 127     ? 0xF1008 - 0x200
                    : header <= 28 ? class_tables[header - 4]
                                   : 0xF1008;
    return rc_u32(ctx, ctx->rom, table + (int64_t)kind * 4);
}

/* STATIC_OBJECT_DESCRIPTORS (532DC common59E80 props and inert scenery). */
typedef struct StaticDescriptor
{
    uint32_t code, descriptor;
} StaticDescriptor;

static const StaticDescriptor static_descriptors[] = {
    {0x010000, 0xF06A8},
    {0x010001, 0xF06C8},
    {0x020000, 0xF0488},
    {0x070000, 0xF0468},
    {0x0B0000, 0xF0408},
    {0x0B0001, 0xF0428},
    {0x850000, 0xEFE48},
    {0x8C0000, 0xEFE48},
    {0x900000, 0xEFE48},
    {0xA00000, 0xF0E28},
    {0xA10000, 0xF0E68},
    {0xA10001, 0xF0E68},
    {0x9A0000, 0xF0BC8},
    {0x9B0000, 0xF0BE8},
    {0x040000, 0xF0FA8},
    {0x050000, 0xF0208},
    {0x050001, 0xF0228},
    {0x050002, 0xF0248},
    {0x060000, 0xF03C8},
    {0x060001, 0xF03E8},
    {0x080000, 0xF04A8},
    {0x090000, 0xF04C8},
    {0x0A0000, 0xF02C8},
    {0x0A0001, 0xF02E8},
    {0x0A0002, 0xF0308},
    {0x0A0003, 0xF0328},
    {0x0A0004, 0xF0348},
    {0x0A0005, 0xF0368},
    {0x0A0006, 0xF0388},
    {0x0A0007, 0xF03A8},
    /* STATIC_SCENERY_DESCRIPTORS: class 0x0c constructors in variant order. */
    {0x0C0000, 0xF09E8},
    {0x0C0001, 0xF0148},
    {0x0C0002, 0xF04E8},
    {0x0C0003, 0xEFDE8},
    {0x0C0004, 0xF0508},
    {0x0C0005, 0xF0168},
    {0x0C0006, 0xF0188},
    {0x0C0007, 0xF01A8},
    {0x0C0008, 0xF0D48},
    {0x0C0009, 0xF0DC8},
    {0x0C000A, 0xF0E08},
    {0x0C000B, 0xF0448},
    {0x0E0000, 0xF0FC8},
    {0x860000, 0xEFF88},
    {0x980000, 0xF0AA8},
    {0x980001, 0xF0AC8},
    {0xA30000, 0xF0FE8},
    {0x030000, 0xF0128},
    {0xA20000, 0xF0EE8},
    {0xA20001, 0xF0EE8},
    {0xA20002, 0xF0EE8},
    {0xA20003, 0xF0EE8},
};
#define STATIC_DESCRIPTOR_COUNT (sizeof(static_descriptors) / sizeof(static_descriptors[0]))

static int static_descriptor(uint32_t code, uint32_t *descriptor)
{
    for (size_t index = 0; index < STATIC_DESCRIPTOR_COUNT; ++index)
        if (static_descriptors[index].code == code)
        {
            *descriptor = static_descriptors[index].descriptor;
            return 1;
        }
    return 0;
}

/* The classes of STATIC_OBJECT_DESCRIPTORS; identical to static_scenery's
 * "unknown static scenery variant" class list. */
static int static_class(uint32_t object_class)
{
    for (size_t index = 0; index < STATIC_DESCRIPTOR_COUNT; ++index)
        if (static_descriptors[index].code >> 16 == object_class)
            return 1;
    return 0;
}

/* PLACED_OBJECT_CLASSES: static props plus one section family each. */
static int placed_class(uint32_t object_class)
{
    static const uint32_t actors[] = {0x80, 0x81, 0x82, 0x84, 0x87, 0x8A,
                                      0x8D, 0x8E, 0x8F, 0x92, 0x97, 0x9C};
    for (size_t index = 0; index < sizeof(actors) / sizeof(actors[0]); ++index)
        if (actors[index] == object_class)
            return 1;
    return static_class(object_class);
}

/* Four LOD (cell list, distance shift) pairs of a source descriptor. */
static void put_lod_images(RiCtx *ctx, RiBuf *out, uint32_t descriptor, RiData tiles,
                           RiData palette, int check_shift)
{
    for (uint32_t lod = 0; lod < 4; ++lod)
    {
        size_t cell = ri_pointer(ctx, ctx->rom, descriptor + lod * 8);
        uint32_t shift = ri_get_u32(ctx, ctx->rom, descriptor + lod * 8 + 4);
        if (check_shift && shift > 3)
            ri_fail(ctx, "invalid static scenery distance shift");
        RiBuf *image = ri_cell_image_indexed(ctx, ctx->rom, cell, tiles, palette, 0u);
        ri_u32(ctx, out, shift);
        rc_word(ctx, out, (int64_t)image->size);
        ri_put_buf(ctx, out, image);
        ri_buf_free(ctx, image);
    }
}

/* static_scenery: descriptors are source addresses, or ('cell', address). */
typedef struct SceneryDescriptor
{
    int cell;
    uint32_t address;
} SceneryDescriptor;

typedef struct SceneryList
{
    SceneryDescriptor items[MAX_SCENERY_DESCRIPTORS];
    size_t count;
} SceneryList;

static int scenery_index(const SceneryList *list, uint32_t address)
{
    for (size_t index = 0; index < list->count; ++index)
        if (!list->items[index].cell && list->items[index].address == address)
            return (int)index;
    return -1;
}

static void scenery_add(RiCtx *ctx, SceneryList *list, int cell, uint32_t address)
{
    if (list->count == MAX_SCENERY_DESCRIPTORS)
        ri_fail(ctx, "too many static scenery descriptors");
    list->items[list->count].cell = cell;
    list->items[list->count].address = address;
    list->count++;
}

static RiBuf *static_scenery(RiCtx *ctx, uint32_t header, Records objects, RiData tiles,
                             RiData palette)
{
    /* 58AB0 scrap frames, then 58E10's seven single-cell Shy Guy rider views. */
    static const uint32_t teepee_frames[3] = {0xF0CE8, 0xF0D08, 0xF0D28};
    SceneryList descriptors;
    SceneryList *list = &descriptors;
    list->count = 0;
    RiBuf *placements = ri_buf(ctx);
    size_t placement_count = 0;
    for (size_t order = 0; order < objects.count; ++order)
    {
        const uint8_t *record = objects.data + order * 4;
        uint32_t code = object_code(ctx, header, record[0]), object_class = code >> 16,
                 descriptor = 0;
        if (!static_descriptor(code, &descriptor))
        {
            if (static_class(object_class))
                ri_fail(ctx, "unknown static scenery variant");
            continue;
        }
        if (scenery_index(list, descriptor) < 0)
        {
            scenery_add(ctx, list, 0, descriptor);
            if (code == 0x860000)
            {
                scenery_add(ctx, list, 0, 0xEFF68); /* 55FC0 alternate contact state */
            }
            else if (object_class == 0xA0)
            {
                scenery_add(ctx, list, 0, 0xF0E48);
            }
            else if (object_class == 0xA1)
            {
                for (uint32_t extra = 0xF0E88; extra <= 0xF0EC8; extra += 0x20)
                    scenery_add(ctx, list, 0, extra);
            }
            else if (object_class == 0xA2)
            {
                for (uint32_t extra = 0xF0F28; extra <= 0xF0F88; extra += 0x20)
                    scenery_add(ctx, list, 0, extra); /* 599EC impact */
            }
            else if (object_class == 3)
            {
                for (int frame = 0; frame < 3; ++frame)
                    scenery_add(ctx, list, 0, teepee_frames[frame]);
                for (uint32_t view = 0; view < 7; ++view)
                    scenery_add(ctx, list, 1, 0x7C540 + 8 * view);
            }
        }
        uint32_t scale = (object_class == 12 && code != 0x0C000B) || object_class == 0xA0 ||
                                 object_class == 0xA1 || object_class == 0xA2
                             ? 224
                         : object_class == 6 ? 184
                                             : 256;
        /* struct.pack('<iiBBHIB3x') */
        put_i32(ctx, placements, ((int64_t)record[1] * 8 + 4) << 16);
        put_i32(ctx, placements, ((int64_t)record[2] * 8 + 4) << 16);
        rc_byte(ctx, placements, record[3]);
        rc_byte(ctx, placements, scenery_index(list, descriptor));
        rc_half(ctx, placements, scale);
        ri_u32(ctx, placements, code);
        rc_byte(ctx, placements, (int64_t)order);
        ri_put_zero(ctx, placements, 3);
        placement_count++;
    }
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 4, 0x314E4353u, (uint32_t)placement_count, (uint32_t)list->count, 0u);
    ri_put_buf(ctx, out, placements);
    ri_buf_free(ctx, placements);
    for (size_t index = 0; index < list->count; ++index)
    {
        const SceneryDescriptor *item = &list->items[index];
        if (item->cell)
        {
            RiBuf *image = ri_cell_image_indexed(ctx, ctx->rom, item->address, tiles, palette, 0u);
            for (int lod = 0; lod < 4; ++lod)
            {
                ri_u32(ctx, out, 0);
                rc_word(ctx, out, (int64_t)image->size);
                ri_put_buf(ctx, out, image);
            }
            ri_buf_free(ctx, image);
        }
        else
        {
            put_lod_images(ctx, out, item->address, tiles, palette, 1);
        }
    }
    return out;
}

/* The placed actor families of course_rom.py (lava_bubbles, thwomps, crabs,
 * seagulls, piranhas, snowmen, mice, falling_stars, thunderclouds, penguins,
 * boos, cannonballs) share one placement format: '<iiHBB' per object of the
 * class, then art as LOD images of each descriptor. */
typedef enum ActorArt
{
    ACTOR_ART_COURSE,       /* course scenery tiles and palette */
    ACTOR_ART_EFFECT_BANK,  /* cannonballs: 48D44 appends the race-effect bank */
    ACTOR_ART_THUNDERCLOUD, /* thunderclouds: nine palette phases */
} ActorArt;

typedef struct ActorFamily
{
    const char *name;
    uint32_t object_class;
    uint32_t max_variant; /* used when allowed_count is 0 */
    uint16_t allowed[5];
    size_t allowed_count;
    uint32_t motion_magic, art_magic;
    uint32_t motion_kind, motion_version, art_kind;
    ActorArt art;
    size_t descriptor_count;
    uint32_t descriptors[8];
} ActorFamily;

static const ActorFamily actor_families[] = {
    {"lava bubble",
     0x82,
     12,
     {0},
     0,
     0x3142564C,
     0x3141424C,
     39,
     2,
     40,
     ACTOR_ART_COURSE,
     2,
     {0xEFF08, 0xEFF28}},
    {"Thwomp", 0x81, 11, {0}, 0, 0x31505754, 0x31415754, 41, 2, 42, ACTOR_ART_COURSE, 1, {0xEFEE8}},
    {"crab",
     0x80,
     0,
     {0, 1, 0x8000, 0x8001, 0x8002},
     5,
     0x31425243,
     0x31415243,
     43,
     1,
     44,
     ACTOR_ART_COURSE,
     4,
     {0xEFFC8, 0xEFFE8, 0xF0008, 0xF0028}},
    {"seagull",
     0x87,
     0,
     {0},
     0,
     0x314C4753,
     0x31414753,
     45,
     1,
     46,
     ACTOR_ART_COURSE,
     3,
     {0xF0528, 0xF0548, 0xF0568}},
    {"cannonball",
     0x84,
     3,
     {0},
     0,
     0x314C4243,
     0x31414243,
     47,
     1,
     48,
     ACTOR_ART_EFFECT_BANK,
     7,
     {0xF0048, 0xF0068, 0xF0088, 0xF00A8, 0xF00C8, 0xF00E8, 0xF0108}},
    {"Piranha Plant",
     0x8A,
     0,
     {0},
     0,
     0x314E5250,
     0x31415250,
     49,
     1,
     50,
     ACTOR_ART_COURSE,
     5,
     {0xF0588, 0xF05A8, 0xF05C8, 0xF05E8, 0xF0608}},
    {"snowman",
     0x92,
     0,
     {0},
     0,
     0x314D4E53,
     0x31414E53,
     51,
     1,
     52,
     ACTOR_ART_COURSE,
     2,
     {0xF0668, 0xF0688}},
    {"penguin",
     0x9C,
     1,
     {0},
     0,
     0x314E4750,
     0x31414750,
     53,
     1,
     54,
     ACTOR_ART_COURSE,
     6,
     {0xF0C08, 0xF0C28, 0xF0C48, 0xF0C68, 0xF0C88, 0xF0CA8}},
    {"boo",
     0x8D,
     1,
     {0},
     0,
     0x314F4F42,
     0x31414F42,
     55,
     1,
     56,
     ACTOR_ART_COURSE,
     2,
     {0xF07A8, 0xF07C8}},
    {"mouse",
     0x8E,
     0,
     {0, 1, 0x8001},
     3,
     0x3153554D,
     0x3141534D,
     73,
     1,
     74,
     ACTOR_ART_COURSE,
     2,
     {0xF01C8, 0xF01E8}},
    {"thundercloud",
     0x8F,
     3,
     {0},
     0,
     0x31444354,
     0x31414354,
     75,
     1,
     76,
     ACTOR_ART_THUNDERCLOUD,
     8,
     {0xF08E8, 0xF0908, 0xF0928, 0xF0948, 0xF0968, 0xF0988, 0xF09A8, 0xF09C8}},
    {"falling star",
     0x97,
     5,
     {0},
     0,
     0x31525453,
     0x31415253,
     77,
     1,
     78,
     ACTOR_ART_COURSE,
     5,
     {0xF0A08, 0xF0A28, 0xF0A48, 0xF0A68, 0xF0A88}},
};

static int actor_variant_valid(const ActorFamily *family, uint32_t variant)
{
    if (!family->allowed_count)
        return variant <= family->max_variant;
    for (size_t index = 0; index < family->allowed_count; ++index)
        if (family->allowed[index] == variant)
            return 1;
    return 0;
}

/* thunderclouds: initial palette plus eight source five-color rows. Both banks
 * are patched together; the simulation selects each bank independently. */
static void thundercloud_art(RiCtx *ctx, RiBuf *art, const ActorFamily *family, RiData tiles,
                             RiData palette)
{
    uint8_t colors[SCENERY_PALETTE_BYTES];
    RiData view;
    view.data = colors;
    view.size = sizeof(colors);
    if (palette.size != sizeof(colors))
        ri_fail(ctx, "invalid thundercloud palette");
    ri_words(ctx, art, 2, family->art_magic, 72u);
    for (uint32_t phase = 0; phase < 9; ++phase)
    {
        memcpy(colors, palette.data, sizeof(colors));
        if (phase)
        {
            const uint8_t *row = ri_at(ctx, ctx->rom, 0x76BCA + (phase - 1) * 10, 10);
            memcpy(colors + 0x22, row, 10);
            memcpy(colors + 0x42, row, 10);
        }
        for (size_t index = 0; index < family->descriptor_count; ++index)
            put_lod_images(ctx, art, family->descriptors[index], tiles, view, 0);
    }
}

static void actor_sections(RiCtx *ctx, const ActorFamily *family, uint32_t header, Records objects,
                           uint32_t course_id, RiData tiles, RiData palette)
{
    RiBuf *placements = ri_buf(ctx);
    size_t count = 0;
    for (size_t order = 0; order < objects.count; ++order)
    {
        const uint8_t *record = objects.data + order * 4;
        uint32_t code = object_code(ctx, header, record[0]);
        if (code >> 16 != family->object_class)
            continue;
        uint32_t variant = code & 65535;
        if (!actor_variant_valid(family, variant))
            ri_fail(ctx, "unknown %s variant", family->name);
        put_i32(ctx, placements, ((int64_t)record[1] * 8 + 4) << 16);
        put_i32(ctx, placements, ((int64_t)record[2] * 8 + 4) << 16);
        ri_u16(ctx, placements, variant);
        ri_u8(ctx, placements, record[3]);
        rc_byte(ctx, placements, (int64_t)order);
        count++;
    }
    if (!count)
    {
        ri_buf_free(ctx, placements);
        return;
    }
    RiBuf *motion = ri_buf(ctx);
    ri_words(ctx, motion, 2, family->motion_magic, (uint32_t)count);
    ri_put_buf(ctx, motion, placements);
    ri_buf_free(ctx, placements);
    RiBuf *art = ri_buf(ctx);
    if (family->art == ACTOR_ART_THUNDERCLOUD)
    {
        thundercloud_art(ctx, art, family, tiles, palette);
    }
    else
    {
        ri_words(ctx, art, 2, family->art_magic, (uint32_t)family->descriptor_count);
        for (size_t index = 0; index < family->descriptor_count; ++index)
            put_lod_images(ctx, art, family->descriptors[index], tiles, palette, 0);
    }
    ri_section(ctx, family->motion_kind, course_id, family->motion_version, motion);
    ri_section(ctx, family->art_kind, course_id, 1, art);
}

/* ---- Section 6 tables ---------------------------------------------------- */

/* cpu_item_profiles: 43DE8 course/class branch tables, normalized at import
 * to three contiguous lap-variant tables per jump table. */
static void cpu_item_profiles(RiCtx *ctx, RiBuf *out, uint32_t header)
{
    static const struct
    {
        uint32_t branch, group;
    } groups[6] = {{0x08043F1C, 6}, {0x08043FB4, 3}, {0x08043F10, 0},
                   {0x08043FA8, 0}, {0x08043FDC, 0}, {0x08043FE8, 0}};
    static const uint32_t jump_tables[3] = {0x43E48, 0x43EC0, 0x43F58};
    if (!((header >= 4 && header <= 28) || (header >= 32 && header <= 51)))
        ri_fail(ctx, "CPU item profile requires a recovered course header");
    for (int table = 0; table < 3; ++table)
    {
        /* Headers outside 4..23 take 43FE8, using the generic three lap variants. */
        uint32_t branch = header <= 23
                              ? ri_get_u32(ctx, ctx->rom, jump_tables[table] + (header - 4) * 4)
                              : 0x08043FE8;
        int group = -1;
        for (int index = 0; index < 6; ++index)
            if (groups[index].branch == branch)
                group = (int)groups[index].group;
        if (group < 0)
            ri_fail(ctx, "unknown source CPU item branch");
        for (uint32_t variant = 0; variant < 3; ++variant)
        {
            size_t offset = 0xED0A8 + ((size_t)group + variant) * 256;
            for (size_t index = 0; index < 64; ++index)
            {
                uint32_t value = ri_get_u32(ctx, ctx->rom, offset + index * 4);
                if (value && !(value >= 0x1000 && value <= 0x100C))
                    ri_fail(ctx, "invalid source CPU item ID");
            }
            for (size_t index = 0; index < 64; ++index)
                ri_u16(ctx, out, ri_get_u32(ctx, ctx->rom, offset + index * 4));
        }
    }
}

/* cpu_banana/green/red_decisions: 42454, 421F4 and 4279C four-way choices. */
static void cpu_decisions(RiCtx *ctx, RiBuf *out, size_t offset, const char *what)
{
    const uint8_t *data = ri_at(ctx, ctx->rom, offset, 8);
    for (int index = 0; index < 8; ++index)
        if (data[index] > 3)
            ri_fail(ctx, "invalid CPU %s release decision", what);
    ri_put(ctx, out, data, 8);
}

/* battle_item_rules: 43BB0/44028 balloon-count odds; 439FC 120-turn hidden box wait. */
static void battle_item_rules(RiCtx *ctx, RiBuf *out)
{
    static const uint32_t tables[3] = {0xED080, 0xED06C, 0xED058};
    for (int table = 0; table < 3; ++table)
    {
        uint32_t total = 0;
        int terminated = 0;
        for (uint32_t index = 0; index < 10; ++index)
        {
            uint32_t item = ri_get_u16(ctx, ctx->rom, tables[table] + index * 4);
            uint32_t weight = ri_get_u16(ctx, ctx->rom, tables[table] + index * 4 + 2);
            if (!item)
            {
                if (weight != 255 || !(total > 0 && total <= 32767))
                    ri_fail(ctx, "invalid Battle roulette terminator or total");
                ri_u16(ctx, out, item);
                ri_u16(ctx, out, weight);
                ri_put_zero(ctx, out, (9 - index) * 4);
                terminated = 1;
                break;
            }
            if (!(item >= 0x1000 && item <= 0x100C))
                ri_fail(ctx, "invalid Battle roulette item");
            total += weight;
            ri_u16(ctx, out, item);
            ri_u16(ctx, out, weight);
        }
        if (!terminated)
            ri_fail(ctx, "unterminated Battle roulette table");
    }
    /* Thumb MOV r0,#0x78 in 439FC's Battle branch, not a runtime constant. */
    uint32_t instruction = ri_get_u16(ctx, ctx->rom, 0x43A6A);
    if (instruction != 0x2078)
        ri_fail(ctx, "unexpected Battle item box hidden timer instruction");
    ri_u32(ctx, out, instruction & 255);
}

/* surface_sound_rules: C12C copies EDD40, then patches the course material theme. */
static RiBuf *surface_sound_rules(RiCtx *ctx, uint32_t material)
{
    static const struct
    {
        uint32_t material, index, song;
    } patches[] = {{12, 6, 80}, {12, 7, 71}, {12, 8, 82}, {6, 1, 77},  {10, 1, 71},
                   {0, 0, 76},  {0, 7, 76},  {18, 8, 83}, {16, 10, 84}};
    uint32_t songs[16];
    for (uint32_t index = 0; index < 16; ++index)
        songs[index] = ri_get_u16(ctx, ctx->rom, 0xEDD40 + index * 2);
    for (size_t index = 0; index < sizeof(patches) / sizeof(patches[0]); ++index)
        if (patches[index].material == material)
            songs[patches[index].index] = patches[index].song;
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "SRF1", 4);
    ri_u16(ctx, out, 2);
    ri_u16(ctx, out, 16);
    for (int index = 0; index < 16; ++index)
        ri_u16(ctx, out, songs[index]);
    /* v2 appends the trackDef+0x10 theme: F9E4 plays 182 only for theme 8. */
    for (int index = 0; index < 16; ++index)
    {
        uint32_t song = songs[index];
        ri_u8(ctx, out, song == 81 || song == 137 ? 6u : song == 82 || song == 83 ? 4u : 0u);
    }
    ri_words(ctx, out, 1, material);
    return out;
}

/* route_sentinel_targets: AD0/B10's checkpoint 127 resolved against the whole
 * container (loadtrack_8031990 keeps courseData_bin + directory offset). */
static void route_sentinel_targets(RiCtx *ctx, RiBuf *out, RiData container, int64_t course_offset,
                                   int64_t route_offset, uint32_t count)
{
    uint32_t mask = 0;
    RiBuf *points = ri_buf(ctx);
    for (uint32_t row = 0; row < 3; ++row)
    {
        int64_t offset = course_offset + route_offset + ((int64_t)row * count + 127) * 8;
        if (offset >= 0 && offset + 4 <= (int64_t)container.size)
        {
            mask |= 1u << row;
            ri_u32(ctx, points, rc_u16(ctx, container, offset) * 8);
            ri_u32(ctx, points, rc_u16(ctx, container, offset + 2) * 8);
        }
        else
        {
            ri_words(ctx, points, 2, 0u, 0u);
        }
    }
    ri_u32(ctx, out, mask);
    ri_put_buf(ctx, out, points);
    ri_buf_free(ctx, points);
}

/* Player roulette strength budget per source course ID 24..43; every other
 * course uses 8 (keyed by the native ID). */
static uint32_t item_strength_threshold(int64_t course_id)
{
    static const struct
    {
        int64_t course, threshold;
    } table[] = {{30, 5}, {36, 4}, {41, 4}, {43, 4}, {32, 6}, {35, 6}, {37, 6}, {38, 6},
                 {40, 6}, {42, 6}, {24, 7}, {25, 7}, {28, 7}, {29, 7}, {39, 7}};
    for (size_t index = 0; index < sizeof(table) / sizeof(table[0]); ++index)
        if (table[index].course == course_id)
            return (uint32_t)table[index].threshold;
    return 8;
}

/* ---- course_sections ----------------------------------------------------- */

void rc_course_sections(RiCtx *ctx, uint32_t header, int source_progress, int64_t content_id,
                        int arena, const RiBuf *minimap)
{
    RiData rom = ctx->rom;
    size_t definition = track_definition(ctx, header);
    uint32_t directory_id = ri_get_u32(ctx, rom, definition);
    uint32_t background = ri_get_u32(ctx, rom, definition + 4);
    uint32_t background_behavior = ri_get_u32(ctx, rom, definition + 8);
    Directory directory;
    directory.container = ri_span(ctx, rom, COURSE_CONTAINER, COURSE_CONTAINER_SIZE);
    if (directory_id >= DIRECTORY_COUNT)
        ri_fail(ctx, "course %u outside directory", (unsigned)directory_id);
    for (int index = 0; index < DIRECTORY_COUNT; ++index)
        directory.offsets[index] = ri_get_u32(ctx, directory.container, (size_t)index * 4);
    directory.offsets[DIRECTORY_COUNT] = COURSE_CONTAINER_SIZE;
    for (int index = 0; index < DIRECTORY_COUNT; ++index)
        if (directory.offsets[index] > directory.offsets[index + 1])
            ri_fail(ctx, "invalid course directory");
    if (directory.offsets[0] < DIRECTORY_COUNT * 4)
        ri_fail(ctx, "invalid course directory");
    RiData c = directory_entry(ctx, &directory, directory_id);
    Raster checkpoints = raster(ctx, c);
    Terrain map = terrain(ctx, c);
    /* Native IDs are independent of the source course-directory index. */
    int64_t course_id = content_id >= 0 ? content_id : directory_id;
    RiBuf *tiles = road_tiles(ctx, source_course(ctx, &directory, header, 0x30));
    RiBuf *overlay = exact_lz(ctx, rom, 0x76C1C, 0x800);
    memcpy(tiles->data + 0x3800, overlay->data, 0x800);
    ri_buf_free(ctx, overlay);
    Records objects = stream(ctx, c, 0x8C);
    if (!((header >= 4 && header <= 28) || (header >= 32 && header <= 51)))
        ri_fail(ctx, "course header %u: object class not recovered", (unsigned)header);

    /* Every placed class needs a native owner; never drop one silently. */
    for (size_t index = 0; index < objects.count; ++index)
    {
        uint32_t object_class = object_code(ctx, header, objects.data[index * 4]) >> 16;
        if (!placed_class(object_class))
            ri_fail(ctx, "course object class not recovered: %#x", (unsigned)object_class);
    }

    /* Runtime tile aliases belong to static map-mutating objects. Actor-only
     * placements (e.g. crabs) keep their original terrain and render separately. */
    static const uint32_t alias_classes[] = {4, 5, 6, 8, 9, 10, 12, 14, 0x86, 0x95, 0x96, 0x98};
    int aliases[256];
    uint32_t alias_count = 0;
    for (int index = 0; index < 256; ++index)
        aliases[index] = -1;
    for (size_t index = 0; index < objects.count; ++index)
    {
        const uint8_t *record = objects.data + index * 4;
        uint32_t object_class = object_code(ctx, header, record[0]) >> 16;
        int aliased = 0;
        for (size_t entry = 0; entry < sizeof(alias_classes) / sizeof(alias_classes[0]); ++entry)
            if (alias_classes[entry] == object_class)
                aliased = 1;
        if (!aliased)
            continue;
        uint32_t x = record[1], y = record[2];
        if (x >= map.width || y >= map.height)
            ri_fail(ctx, "course object outside terrain");
        uint8_t *cell = map.cells->data + (size_t)y * map.width + x;
        uint8_t source = *cell;
        if (aliases[source] < 0)
        {
            uint32_t dynamic = 0xD0 + alias_count;
            if (dynamic >= 0xE0)
                ri_fail(ctx, "course has too many dynamic tile aliases");
            aliases[source] = (int)dynamic;
            alias_count++;
            memmove(tiles->data + dynamic * 64, tiles->data + (size_t)source * 64, 64);
        }
        *cell = (uint8_t)aliases[source];
    }
    RiData material = rc_span(ctx, c, rc_u32(ctx, c, 0x88), 256);
    RiData road_palette = rc_span(ctx, c, rc_u32(ctx, c, 0x84), 96);
    Records boundary_stream = stream(ctx, c, 0x98);
    size_t boundary_count = 0;
    const uint8_t *boundary = NULL;
    for (size_t index = 0; index < boundary_stream.count; ++index)
        if (boundary_stream.data[index * 4] == 0x8B)
        {
            if (!boundary_count)
                boundary = boundary_stream.data + index * 4;
            boundary_count++;
        }

    uint32_t revision = 0;
    RiBuf *rules = ri_buf(ctx);
    uint32_t count = checkpoints.count;
    if (arena)
    {
        if (boundary_count)
            ri_fail(ctx, "arena must not contain a start/finish line");
        RcGridRow starts[8];
        rc_rom_grid(ctx, header, 4, starts);
        size_t start_cell = (size_t)(starts[0].y >> 20) * checkpoints.width + (starts[0].x >> 20);
        if (start_cell >= checkpoints.cells->size)
            ri_fail(ctx, "arena start outside checkpoint raster");
        uint32_t start_checkpoint = checkpoints.cells->data[start_cell];
        if ((start_checkpoint & 127) >= count)
            ri_fail(ctx, "arena start outside checkpoint regions");
        revision = 4;
        rc_word(ctx, rules, 0x314C5552);
        rc_word(ctx, rules, course_id);
        ri_words(ctx, rules, 9, revision, 4u, 1u, 2u, 2u, 3u, 4u, starts[0].x, starts[0].y);
        /* struct.pack('<HHIIIHHHH') */
        ri_u16(ctx, rules, starts[0].heading);
        ri_u16(ctx, rules, start_checkpoint & 127);
        ri_words(ctx, rules, 3, 3u, 3u, 0u);
        ri_u16(ctx, rules, 0);
        ri_u16(ctx, rules, 180);
        ri_u16(ctx, rules, 180);
        ri_u16(ctx, rules, start_checkpoint);
    }
    else
    {
        if (boundary_count != 1)
            ri_fail(ctx, "course must have exactly one start/finish line");
        int64_t sx = boundary[1], sy = boundary[2];
        uint32_t laps = source_progress ? 2 : 1;
        revision = source_progress ? 3 : 1;
        rc_word(ctx, rules, 0x314C5552);
        rc_word(ctx, rules, course_id);
        ri_words(ctx, rules, 7, revision, revision, 1u, 2u, 2u, 3u, revision);
        rc_word(ctx, rules, (sx * 8 + 40) << 16);
        rc_word(ctx, rules, (sy * 8 + 28) << 16);
        rc_half(ctx, rules, 0);
        rc_half(ctx, rules, (int64_t)count - 1);
        ri_words(ctx, rules, 2, laps, laps);
        rc_word(ctx, rules, (sy * 8 + 8) << 16);
        rc_half(ctx, rules, ri_get_u32(ctx, rom, definition + 0x34));
        ri_u16(ctx, rules, 180);
        ri_u16(ctx, rules, 180);
        rc_half(ctx, rules, source_progress ? (0x80 | ((int64_t)count - 1)) : 0);
    }
    /* RUL1 v2 appends the roulette threshold, keyed by the native ID. */
    ri_u8(ctx, rules, item_strength_threshold(course_id));
    ri_put_zero(ctx, rules, 3);

    int64_t route_base = rc_u32(ctx, c, 0xCC);
    int64_t route_offset = route_base + rc_u16(ctx, c, route_base + 3);
    RiData target = ri_span(ctx, rom, ri_pointer(ctx, rom, definition + 0x1C), 40);
    for (size_t index = 0; index < 40; ++index)
        if (target.data[index] >= 3)
            ri_fail(ctx, "course AI target row outside original three rows");
    RiBuf *route = ri_buf(ctx);
    ri_words(ctx, route, 4, 0x31545552u, count, 3u, 40u);
    for (int64_t index = 0; index < (int64_t)count * 3; ++index)
    {
        const uint8_t *record = rc_span(ctx, c, route_offset + index * 8, 8).data;
        ri_words(ctx, route, 3, ri_le16(record) * 8, ri_le16(record + 2) * 8, (uint32_t)record[4]);
    }
    ri_put(ctx, route, target.data, 40);
    route_sentinel_targets(ctx, route, directory.container, directory.offsets[directory_id],
                           route_offset, count);

    Records item_stream = stream(ctx, c, 0x94);
    RiBuf *boxes = ri_buf(ctx);
    RiBuf *box_records = ri_buf(ctx);
    uint32_t box_count = 0;
    for (size_t index = 0; index < item_stream.count; ++index)
    {
        const uint8_t *record = item_stream.data + index * 4;
        if (object_code(ctx, header, record[0]) != 0xFFFF0000u)
            continue;
        /* struct.pack('<IIHBB') */
        ri_u32(ctx, box_records, (uint32_t)record[1] << 19);
        ri_u32(ctx, box_records, (uint32_t)record[2] << 19);
        ri_u16(ctx, box_records, 0x8800);
        ri_u8(ctx, box_records, record[3]);
        ri_u8(ctx, box_records, 0x1E);
        box_count++;
    }
    ri_words(ctx, boxes, 2, 0x314A424Fu, box_count);
    ri_put_buf(ctx, boxes, box_records);
    ri_buf_free(ctx, box_records);

    Records coins = stream(ctx, c, 0x90);
    for (size_t index = 0; index < coins.count; ++index)
        if (coins.data[index * 4] != 0x80)
            ri_fail(ctx, "invalid coin placement marker");

    RiBuf *manifest = ri_buf(ctx);
    rc_word(ctx, manifest, 0x31535243);
    rc_word(ctx, manifest, course_id);
    ri_u32(ctx, manifest, revision);
    for (int index = 0; index < 7; ++index)
        rc_word(ctx, manifest, course_id);
    ri_words(ctx, manifest, 7, 1u, 1u, 0u, 0u, 0u, 0u, 0u);

    /* sub_803159C resolves graphics through EC, but keeps this course's palette. */
    RiData scenery_source = source_course(ctx, &directory, header, 0xEC);
    int64_t art_offset = rc_u32(ctx, scenery_source, 0xE4);
    RiBuf *art = NULL;
    if (!art_offset)
        art = ri_buf_zero(ctx, SCENERY_ART_BYTES);
    else if (rc_u8(ctx, scenery_source, 3) & 4)
        art = split_lz(ctx, scenery_source, art_offset, 2, 0x1000);
    else
        art = padded_lz(ctx, scenery_source, art_offset, SCENERY_ART_BYTES);
    /* sub_803159C copies 0x18 words: three 16-color palette banks. */
    RiData scenery_palette = rc_span(ctx, c, rc_u32(ctx, c, 0xE8), SCENERY_PALETTE_BYTES);
    RiBuf *scenery = static_scenery(ctx, header, objects, ri_view(art), scenery_palette);
    /* 48D44 installs the common race-effect bank after course scenery and its
     * five palettes after the three course banks. Normalize once here. */
    RiBuf *effect_tiles = ri_buf_copy(ctx, art->data, art->size);
    ri_put(ctx, effect_tiles, ri_at(ctx, rom, 0x66F0C, 0x4000), 0x4000);
    RiBuf *effect_palette = ri_buf_copy(ctx, scenery_palette.data, scenery_palette.size);
    ri_put(ctx, effect_palette, ri_at(ctx, rom, 0x66E4C, 160), 160);
    uint32_t native_id = (uint32_t)course_id; /* range-checked by rc_word above */
    ri_section(ctx, 87u, native_id, 1u,
               weather_payload(ctx, background_behavior, ri_view(art), scenery_palette));
    ri_section(ctx, 84u, native_id, 1u,
               ri_ambient_payload(ctx, background_behavior, ri_get_u32(ctx, rom, definition + 12u),
                                  ri_get_u32(ctx, rom, definition + 16u), scenery_palette));
    for (size_t index = 0; index < sizeof(actor_families) / sizeof(actor_families[0]); ++index)
    {
        const ActorFamily *family = &actor_families[index];
        int effect = family->art == ACTOR_ART_EFFECT_BANK;
        actor_sections(ctx, family, header, objects, native_id,
                       ri_view(effect ? effect_tiles : art),
                       effect ? ri_view(effect_palette) : scenery_palette);
    }
    ri_buf_free(ctx, effect_tiles);
    ri_buf_free(ctx, effect_palette);
    ri_buf_free(ctx, art);

    /* sub_804DBF4 rank strip: five layouts and the source-class portrait pages. */
    RiData pages = ri_span(ctx, rom, 0xEE6FA, 8);
    for (size_t index = 0; index < 8; ++index)
        if (pages.data[index] >= 8)
            ri_fail(ctx, "GP portrait page outside bank");
    RiBuf *gp_hud = ri_buf(ctx);
    ri_words(ctx, gp_hud, 4, 0x31484747u, native_id, 5u, 52u);
    ri_put(ctx, gp_hud, pages.data, 8);
    ri_put(ctx, gp_hud, ri_at(ctx, rom, 0x64834, 0x400), 0x400);
    RiBuf *rank_tiles = exact_lz(ctx, c, rc_u32(ctx, c, 0xC4), 0x800);
    ri_put_buf(ctx, gp_hud, rank_tiles);
    ri_buf_free(ctx, rank_tiles);
    static const uint32_t strips[5] = {0xEED80, 0xEEDEC, 0xEEE58, 0xEEEC4, 0xEEB64};
    for (int index = 0; index < 5; ++index)
    {
        if (ri_get_u16(ctx, rom, strips[index]) != 4 ||
            ri_get_u16(ctx, rom, strips[index] + 2) != 13)
            ri_fail(ctx, "GP rank strip dimensions changed");
        ri_put(ctx, gp_hud, ri_at(ctx, rom, strips[index] + 4, 104), 104);
    }
    ri_put(ctx, gp_hud, ri_at(ctx, rom, 0x64C34, 64), 64);
    static const uint32_t transition_strips[4] = {0xEEBD0, 0xEEC3C, 0xEECA8, 0xEED14};
    for (unsigned index = 0u; index < 4u; ++index)
    {
        const uint32_t offset = transition_strips[index];
        if (ri_get_u16(ctx, rom, offset) != 4u || ri_get_u16(ctx, rom, offset + 2u) != 13u)
            ri_fail(ctx, "GP rank transition dimensions changed");
        ri_put(ctx, gp_hud, ri_at(ctx, rom, offset + 4u, 104u), 104u);
    }
    ri_put(ctx, gp_hud, ri_at(ctx, rom, 0x64C7C, 16), 16);
    ri_put(ctx, gp_hud, ri_at(ctx, rom, 0x64814, 32), 32);
    static const uint32_t coin_colors[3] = {0x64C94, 0x64C9C, 0x64CA4};
    for (unsigned index = 0u; index < 3u; ++index)
        ri_put(ctx, gp_hud, ri_at(ctx, rom, coin_colors[index], 6), 6);
    ri_put(ctx, gp_hud, ri_at(ctx, rom, 0x64C8C, 8), 8);
    static const uint32_t finish_frames[8] = {0xDB0B2, 0xDB0D6, 0xDB0FA, 0xDB11E,
                                              0xDB142, 0xDB166, 0xDB18A, 0xDB1AE};
    for (unsigned i = 0u; i < 8u; ++i)
    {
        const size_t offset = finish_frames[i];
        if (ri_get_u16(ctx, rom, offset) != 4u || ri_get_u16(ctx, rom, offset + 2u) != 4u)
            ri_fail(ctx, "finish portrait frame dimensions changed");
        ri_put(ctx, gp_hud, ri_at(ctx, rom, offset, 36u), 36u);
    }
    for (unsigned i = 0u; i < 8u; ++i)
    {
        const size_t offset = ri_pointer(ctx, rom, 0xDB234u + i * 4u);
        if (ri_get_u16(ctx, rom, offset) != 2u || ri_get_u16(ctx, rom, offset + 2u) != 2u)
            ri_fail(ctx, "finish portrait pattern dimensions changed");
        ri_put(ctx, gp_hud, ri_at(ctx, rom, offset, 12u), 12u);
    }

    ri_u32(ctx, gp_hud, (header & 32u) || rc_u8(ctx, c, 0xCA) != 0u ? 20u : 21u);
    uint32_t theme = ri_get_u32(ctx, rom, definition + 0x10) & 255;

    /* Section 6: material, theme constants and the CPU/item tables. */
    RiBuf *physics = ri_buf(ctx);
    ri_words(ctx, physics, 2, 0x3154414Du, 256u);
    ri_put(ctx, physics, material.data, 256);
    ri_words(ctx, physics, 3, theme == 12 ? 0x03A00000u : 0u, theme == 8 ? 8u : 0u,
             theme == 0    ? 170u
             : theme == 12 ? 169u
                           : 168u);
    for (uint32_t index = 0; index < 6; ++index)
        rc_half(ctx, physics, ri_get_u32(ctx, rom, 0xEC268 + index * 4));
    cpu_item_profiles(ctx, physics, header);
    cpu_decisions(ctx, physics, 0xECCEF, "banana");
    cpu_decisions(ctx, physics, 0xECCE7, "green shell");
    cpu_decisions(ctx, physics, 0xECCF7, "red shell");
    /* shell_orbit_delays: 473FC/47560 narrow each delay word to byte74. */
    for (uint32_t index = 0; index < 6; ++index)
        ri_u8(ctx, physics, ri_get_u32(ctx, rom, 0xEDAFC + index * 4) & 255);
    /* mushroom_voices: A9AC indexes ECC48 by driver, then normal/small. */
    for (uint32_t index = 0; index < 16; ++index)
        if (!ri_get_u16(ctx, rom, 0xECC48 + index * 2))
            ri_fail(ctx, "invalid mushroom voice song");
    ri_put(ctx, physics, ri_at(ctx, rom, 0xECC48, 32), 32);
    ri_put(ctx, physics, ri_at(ctx, rom, 0xEDD60, 2178), 2178);
    battle_item_rules(ctx, physics);
    /* battle_elimination_rules: B020/AFA4 one frame then the revenge toggle. */
    const uint8_t *elimination = ri_at(ctx, rom, 0xF2C50, 4);
    if (elimination[0] != 1 || elimination[1] < 1 || elimination[1] > 127 || elimination[2] ||
        elimination[3])
        ri_fail(ctx, "invalid Battle elimination animation");
    ri_u32(ctx, physics, elimination[1]);
    ri_words(ctx, physics, 9, 256u, 128u, 0x1000u, 0x3000u, 6u, 64u, 128u, 0x1000u, 48u);

    RiBuf *world = ri_buf(ctx);
    ri_words(ctx, world, 7, 0x31444C57u, checkpoints.width, checkpoints.width, map.width,
             map.height, (uint32_t)checkpoints.cells->size, (uint32_t)map.cells->size);
    ri_put_buf(ctx, world, checkpoints.cells);
    ri_put_buf(ctx, world, map.cells);
    ri_buf_free(ctx, checkpoints.cells);
    ri_buf_free(ctx, map.cells);

    RiBuf *road = ri_buf(ctx);
    ri_words(ctx, road, 5, 0x31444152u, (uint32_t)tiles->size, 64u, rc_u8(ctx, c, 3), 4u);
    ri_u32(ctx, road, 0);
    ri_put_buf(ctx, road, tiles);
    ri_put(ctx, road, road_palette.data, 96);
    ri_put(ctx, road, ri_at(ctx, rom, 0x76DE8, 32), 32);
    ri_buf_free(ctx, tiles);

    RiBuf *coin_section = ri_buf(ctx);
    ri_words(ctx, coin_section, 2, 0x314E4943u, (uint32_t)coins.count);
    if (coins.count)
        ri_put(ctx, coin_section, coins.data, coins.count * 4);

    ri_section(ctx, 1, native_id, 1, manifest);
    ri_section(ctx, 3, native_id, 2, rules);
    ri_section(ctx, 70, native_id, 2, surface_sound_rules(ctx, theme));
    ri_section(ctx, 4, native_id, 4, route);
    ri_section(ctx, 5, native_id, 1, world);
    ri_section(ctx, 6, native_id, 16, physics);
    ri_section(ctx, 8, native_id, 1, road);
    ri_section(ctx, 9, native_id, 2,
               sky(ctx, background, background_behavior, ri_get_u32(ctx, rom, definition + 16)));
    ri_section(ctx, 11, native_id, 1, ri_buf_copy(ctx, minimap->data, minimap->size));
    ri_section(ctx, 25, native_id, 1, boxes);
    ri_section(ctx, 28, native_id, 4, scenery);
    ri_section(ctx, 27, native_id, 1, coin_section);
    ri_section(ctx, 33, native_id, 5, gp_hud);
    if (!arena)
    {
        RiBuf *opening = gp_opening_payload(ctx, header, definition);
        if (opening)
            ri_section(ctx, 36u, native_id, 27u, opening);
        RiBuf *defaults = time_trial_record_defaults(ctx, header, definition);
        if (defaults)
            ri_section(ctx, 93u, native_id, 1u, defaults);
    }
}
