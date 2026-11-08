#include "nds_fs.h"

#include <stdio.h>
#include <string.h>

/* An uncompressed course archive is a few hundred KiB; this bound only stops
 * hostile size fields from reserving huge buffers. */
#define NDS_NARC_LIMIT ((size_t)16u << 20)

uint16_t nds_crc16(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFu;
    for (size_t index = 0; index < size; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xA001u : crc >> 1;
    }
    return (uint16_t)crc;
}

int nds_header_valid(const uint8_t *rom, size_t size)
{
    return rom != NULL && size >= NDS_HEADER_BYTES && nds_crc16(rom, 0x15E) == ri_le16(rom + 0x15E);
}

/* NUL-padded printable ASCII, as the title and game code are stored. */
static void header_text(RiCtx *ctx, const uint8_t *bytes, size_t size, char *out, const char *field)
{
    size_t length = 0;
    while (length < size && bytes[length])
        ++length;
    for (size_t index = 0; index < size; ++index)
        if (index < length ? bytes[index] < 0x20 || bytes[index] > 0x7E : bytes[index] != 0)
            ri_fail(ctx, "DS header %s is not ASCII", field);
    memcpy(out, bytes, length);
    out[length] = 0;
}

void nds_header(RiCtx *ctx, RiData rom, NdsHeader *out)
{
    if (!nds_header_valid(rom.data, rom.size))
        ri_fail(ctx, "not a Nintendo DS ROM (header checksum does not match)");
    memset(out, 0, sizeof(*out));
    header_text(ctx, rom.data, 12, out->title, "title");
    header_text(ctx, rom.data + 12, 4, out->game_code, "game code");
    if (strlen(out->game_code) != 4)
        ri_fail(ctx, "DS header game code is shorter than four characters");
    out->version = rom.data[0x1E];
    out->fnt_offset = ri_le32(rom.data + 0x40);
    out->fnt_size = ri_le32(rom.data + 0x44);
    out->fat_offset = ri_le32(rom.data + 0x48);
    out->fat_size = ri_le32(rom.data + 0x4C);
}

/* ---- FNT/FAT filesystem (cartridge and NARC share the layout) ---------- */

typedef struct FsSource
{
    const char *name;
    RiData names;    /* FNT: directory table, then name subtables */
    RiData table;    /* FAT: start/end pairs, relative to files */
    RiData files;
} FsSource;

typedef struct FsWalk
{
    RiCtx *ctx;
    const FsSource *source;
    NdsEntry *entries;
    uint8_t *named;
    uint8_t *visited;
    size_t file_count;
    uint32_t directories;
    size_t named_count;
} FsWalk;

static void walk(FsWalk *walk_state, uint32_t directory, const char *prefix, size_t prefix_length,
                 unsigned depth)
{
    RiCtx *ctx = walk_state->ctx;
    const FsSource *source = walk_state->source;
    if (depth > 32)
        ri_fail(ctx, "%s: directories nest more than 32 levels", source->name);
    if (walk_state->visited[directory])
        ri_fail(ctx, "%s: directory %u is listed twice", source->name, (unsigned)directory);
    walk_state->visited[directory] = 1;
    size_t cursor = ri_get_u32(ctx, source->names, directory * 8u);
    uint32_t file = ri_get_u16(ctx, source->names, directory * 8u + 4u);
    for (;;)
    {
        const uint32_t type = ri_get_u8(ctx, source->names, cursor++);
        if (type == 0)
            return;
        const size_t length = type & 0x7Fu;
        const uint8_t *text = ri_at(ctx, source->names, cursor, length);
        cursor += length;
        char path[NDS_PATH_CAPACITY];
        if (length == 0 || prefix_length + length + 2u > sizeof(path))
            ri_fail(ctx, "%s: name of length %zu in directory %u does not fit", source->name, length,
                    (unsigned)directory);
        memcpy(path, prefix, prefix_length);
        for (size_t index = 0; index < length; ++index)
        {
            if (text[index] < 0x20 || text[index] > 0x7E || text[index] == '/' || text[index] == '\\')
                ri_fail(ctx, "%s: invalid name byte %#x in directory %u", source->name,
                        (unsigned)text[index], (unsigned)directory);
            path[prefix_length + index] = (char)text[index];
        }
        path[prefix_length + length] = 0;
        if (type & 0x80u)
        {
            const uint32_t child = ri_get_u16(ctx, source->names, cursor);
            cursor += 2;
            if ((child & 0xF000u) != 0xF000u || (child & 0xFFFu) >= walk_state->directories)
                ri_fail(ctx, "%s: %s has invalid directory ID %#x", source->name, path, (unsigned)child);
            path[prefix_length + length] = '/';
            path[prefix_length + length + 1] = 0;
            walk(walk_state, child & 0xFFFu, path, prefix_length + length + 1, depth + 1);
            continue;
        }
        if (file >= walk_state->file_count)
            ri_fail(ctx, "%s: %s has file ID %u beyond the %zu-entry file table", source->name, path,
                    (unsigned)file, walk_state->file_count);
        if (walk_state->named[file])
            ri_fail(ctx, "%s: file ID %u is named twice", source->name, (unsigned)file);
        const uint32_t start = ri_get_u32(ctx, source->table, file * 8u);
        const uint32_t end = ri_get_u32(ctx, source->table, file * 8u + 4u);
        if (start > end || end > source->files.size)
            ri_fail(ctx, "%s: %s spans %#x..%#x outside its %#zx data bytes", source->name, path,
                    (unsigned)start, (unsigned)end, source->files.size);
        NdsEntry *entry = &walk_state->entries[file];
        memcpy(entry->path, path, prefix_length + length + 1);
        entry->data = ri_span(ctx, source->files, start, end - start);
        walk_state->named[file] = 1;
        walk_state->named_count++;
        ++file;
    }
}

/* unnamed_ids: a NARC without names lists its files as "#<id>"; cartridge
 * files without names (overlays) are left out. */
static NdsFs parse_fs(RiCtx *ctx, const FsSource *source, int unnamed_ids)
{
    if (source->table.size % 8u || source->table.size / 8u > NDS_FILE_LIMIT)
        ri_fail(ctx, "%s: file table of %zu bytes is not 8-byte entries under %u files", source->name,
                source->table.size, NDS_FILE_LIMIT);
    const uint32_t directories = ri_get_u16(ctx, source->names, 6);
    if (directories == 0 || directories > 0x1000u || (size_t)directories * 8u > source->names.size)
        ri_fail(ctx, "%s: invalid directory count %u", source->name, (unsigned)directories);
    const size_t file_count = source->table.size / 8u;
    FsWalk walk_state = {ctx, source, NULL, NULL, NULL, file_count, directories, 0};
    walk_state.entries = (NdsEntry *)ri_buf_zero(ctx, (file_count ? file_count : 1u) * sizeof(NdsEntry))->data;
    walk_state.named = ri_buf_zero(ctx, file_count ? file_count : 1u)->data;
    walk_state.visited = ri_buf_zero(ctx, directories)->data;
    walk(&walk_state, 0, "", 0, 0);
    if (unnamed_ids && walk_state.named_count == 0)
        for (size_t file = 0; file < file_count; ++file)
        {
            const uint32_t start = ri_get_u32(ctx, source->table, file * 8u);
            const uint32_t end = ri_get_u32(ctx, source->table, file * 8u + 4u);
            if (start > end || end > source->files.size)
                ri_fail(ctx, "%s: file %zu spans %#x..%#x outside its data", source->name, file,
                        (unsigned)start, (unsigned)end);
            NdsEntry *entry = &walk_state.entries[file];
            snprintf(entry->path, sizeof(entry->path), "#%zu", file);
            entry->data = ri_span(ctx, source->files, start, end - start);
            walk_state.named[file] = 1;
            walk_state.named_count++;
        }
    /* Compact named files in ID order. */
    size_t count = 0;
    for (size_t file = 0; file < file_count; ++file)
        if (walk_state.named[file])
            walk_state.entries[count++] = walk_state.entries[file];
    NdsFs fs = {source->name, walk_state.entries, count};
    return fs;
}

NdsFs nds_rom_fs(RiCtx *ctx, RiData rom, const NdsHeader *header)
{
    FsSource source = {"ROM", {NULL, 0}, {NULL, 0}, rom};
    if (header->fnt_offset > rom.size || header->fnt_size > rom.size - header->fnt_offset ||
        header->fat_offset > rom.size || header->fat_size > rom.size - header->fat_offset)
        ri_fail(ctx, "ROM: filesystem tables lie outside the %#zx-byte image", rom.size);
    source.names = ri_span(ctx, rom, header->fnt_offset, header->fnt_size);
    source.table = ri_span(ctx, rom, header->fat_offset, header->fat_size);
    return parse_fs(ctx, &source, 0);
}

static RiData narc_chunk(RiCtx *ctx, RiData narc, size_t *at, const char *magic, const char *name)
{
    const uint8_t *head = ri_at(ctx, narc, *at, 8);
    if (memcmp(head, magic, 4) != 0)
        ri_fail(ctx, "%s: expected NARC %s chunk at %#zx", name, magic, *at);
    const size_t size = ri_le32(head + 4);
    if (size < 8 || size > narc.size - *at)
        ri_fail(ctx, "%s: NARC %s chunk size %#zx does not fit", name, magic, size);
    RiData payload = ri_span(ctx, narc, *at + 8, size - 8);
    *at += size;
    return payload;
}

NdsFs nds_narc(RiCtx *ctx, RiData file, const char *name)
{
    RiData narc = file;
    if (file.size < 4 || memcmp(file.data, "NARC", 4) != 0)
        narc = ri_view(nds_lz77(ctx, file, NDS_NARC_LIMIT, name));
    if (narc.size < 16 || memcmp(narc.data, "NARC", 4) != 0)
        ri_fail(ctx, "%s: missing NARC header", name);
    if (ri_le16(narc.data + 4) != 0xFFFEu || ri_le16(narc.data + 12) != 16u || ri_le16(narc.data + 14) != 3u)
        ri_fail(ctx, "%s: unsupported NARC byte order, header or chunk count", name);
    const size_t declared = ri_le32(narc.data + 8);
    if (declared < 16 || declared > narc.size)
        ri_fail(ctx, "%s: NARC declares %zu bytes but has %zu", name, declared, narc.size);
    narc.size = declared;
    size_t at = 16;
    const RiData allocation = narc_chunk(ctx, narc, &at, "BTAF", name);
    const RiData names = narc_chunk(ctx, narc, &at, "BTNF", name);
    const RiData files = narc_chunk(ctx, narc, &at, "GMIF", name);
    const size_t count = ri_get_u16(ctx, allocation, 0);
    FsSource source = {name, names, ri_span(ctx, allocation, 4, count * 8u), files};
    return parse_fs(ctx, &source, 1);
}

int nds_find(const NdsFs *fs, const char *path, RiData *out)
{
    for (size_t index = 0; index < fs->count; ++index)
        if (strcmp(fs->entries[index].path, path) == 0)
        {
            if (out)
                *out = fs->entries[index].data;
            return 1;
        }
    return 0;
}

RiData nds_file(RiCtx *ctx, const NdsFs *fs, const char *path)
{
    RiData data;
    if (!nds_find(fs, path, &data))
        ri_fail(ctx, "%s: missing %s", fs->name, path);
    return data;
}

/* ---- BIOS LZ77 ---------------------------------------------------------- */

static uint32_t lz_byte(RiCtx *ctx, RiData source, size_t *cursor, const char *name)
{
    if (*cursor >= source.size)
        ri_fail(ctx, "%s: truncated LZ77 stream", name);
    return source.data[(*cursor)++];
}

RiBuf *nds_lz77(RiCtx *ctx, RiData source, size_t limit, const char *name)
{
    if (source.size < 4)
        ri_fail(ctx, "%s: truncated LZ77 header", name);
    const uint32_t type = source.data[0];
    if (type != 0x10 && type != 0x11)
        ri_fail(ctx, "%s: unsupported compression type %#x", name, (unsigned)type);
    size_t size = ri_le32(source.data) >> 8, cursor = 4;
    if (size == 0)
    {
        size = ri_get_u32(ctx, source, 4);
        cursor = 8;
    }
    if (size > limit)
        ri_fail(ctx, "%s: expands to %zu bytes, over the %zu-byte limit", name, size, limit);
    RiBuf *out = ri_buf(ctx);
    ri_reserve(ctx, out, size ? size : 1);
    while (out->size < size)
    {
        const uint32_t flags = lz_byte(ctx, source, &cursor, name);
        for (int bit = 7; bit >= 0 && out->size < size; --bit)
        {
            if (!(flags & (1u << bit)))
            {
                out->data[out->size++] = (uint8_t)lz_byte(ctx, source, &cursor, name);
                continue;
            }
            const uint32_t first = lz_byte(ctx, source, &cursor, name);
            size_t length, distance;
            if (type == 0x10)
            {
                length = (first >> 4) + 3;
                distance = ((first & 0xFu) << 8 | lz_byte(ctx, source, &cursor, name)) + 1;
            }
            else if ((first >> 4) == 0)
            {
                const uint32_t second = lz_byte(ctx, source, &cursor, name);
                length = ((first & 0xFu) << 4 | second >> 4) + 0x11;
                distance = ((second & 0xFu) << 8 | lz_byte(ctx, source, &cursor, name)) + 1;
            }
            else if ((first >> 4) == 1)
            {
                const uint32_t second = lz_byte(ctx, source, &cursor, name);
                const uint32_t third = lz_byte(ctx, source, &cursor, name);
                length = ((first & 0xFu) << 12 | second << 4 | third >> 4) + 0x111;
                distance = ((third & 0xFu) << 8 | lz_byte(ctx, source, &cursor, name)) + 1;
            }
            else
            {
                length = (first >> 4) + 1;
                distance = ((first & 0xFu) << 8 | lz_byte(ctx, source, &cursor, name)) + 1;
            }
            if (distance > out->size)
                ri_fail(ctx, "%s: LZ77 match reaches before the output start", name);
            for (size_t index = 0; index < length && out->size < size; ++index)
            {
                out->data[out->size] = out->data[out->size - distance];
                out->size++;
            }
        }
    }
    return out;
}

/* ---- BMG text ----------------------------------------------------------- */

NdsBmg nds_bmg(RiCtx *ctx, RiData file, const char *name)
{
    if (file.size < 0x20 || memcmp(file.data, "MESGbmg1", 8) != 0)
        ri_fail(ctx, "%s: missing BMG header", name);
    const size_t declared = ri_le32(file.data + 8);
    const uint32_t sections = ri_le32(file.data + 12);
    if (declared < 0x20 || declared > file.size || sections > 16)
        ri_fail(ctx, "%s: BMG size %zu or section count %u is invalid", name, declared, (unsigned)sections);
    if (file.data[16] != 2)
        ri_fail(ctx, "%s: BMG encoding %u is not UTF-16", name, (unsigned)file.data[16]);
    const RiData body = {file.data, declared};
    NdsBmg bmg = {name, {NULL, 0}, {NULL, 0}, 0, 0};
    size_t at = 0x20;
    for (uint32_t index = 0; index < sections; ++index)
    {
        const uint8_t *head = ri_at(ctx, body, at, 8);
        const size_t size = ri_le32(head + 4);
        if (size < 8 || size > body.size - at)
            ri_fail(ctx, "%s: BMG section %u size %#zx does not fit", name, (unsigned)index, size);
        const RiData payload = ri_span(ctx, body, at + 8, size - 8);
        if (memcmp(head, "INF1", 4) == 0)
        {
            bmg.count = ri_get_u16(ctx, payload, 0);
            bmg.entry_size = ri_get_u16(ctx, payload, 2);
            if (bmg.entry_size < 4)
                ri_fail(ctx, "%s: BMG message entries of %u bytes", name, (unsigned)bmg.entry_size);
            bmg.offsets = ri_span(ctx, payload, 8, (size_t)bmg.count * bmg.entry_size);
        }
        else if (memcmp(head, "DAT1", 4) == 0)
            bmg.text = payload;
        at += size;
    }
    if (!bmg.offsets.data || !bmg.text.data)
        ri_fail(ctx, "%s: BMG lacks INF1 or DAT1", name);
    return bmg;
}

void nds_bmg_ascii(RiCtx *ctx, const NdsBmg *bmg, uint32_t index, const NdsGlyph *glyphs,
                   size_t glyph_count, char *out, size_t capacity)
{
    if (index >= bmg->count)
        ri_fail(ctx, "%s: message %u of %u", bmg->name, (unsigned)index, (unsigned)bmg->count);
    size_t at = ri_get_u32(ctx, bmg->offsets, (size_t)index * bmg->entry_size), used = 0;
    for (;;)
    {
        const uint32_t code = ri_get_u16(ctx, bmg->text, at);
        at += 2;
        if (code == 0)
            break;
        if (code == 0x1Au)
        {
            const size_t length = ri_get_u8(ctx, bmg->text, at);
            if (length < 3)
                ri_fail(ctx, "%s: message %u has a %zu-byte tag", bmg->name, (unsigned)index, length);
            (void)ri_span(ctx, bmg->text, at, length - 2);
            at += length - 2;
            continue;
        }
        char single[2] = {(char)code, 0};
        const char *text = code == '\n' || (code >= 0x20 && code < 0x7F) ? single : NULL;
        for (size_t glyph = 0; !text && glyph < glyph_count; ++glyph)
            if (glyphs[glyph].code == code)
                text = glyphs[glyph].text;
        if (!text)
            ri_fail(ctx, "%s: message %u has unsupported character U+%04X", bmg->name, (unsigned)index,
                    (unsigned)code);
        const size_t length = strlen(text);
        if (length >= capacity - used)
            ri_fail(ctx, "%s: message %u is longer than %zu bytes", bmg->name, (unsigned)index,
                    capacity - 1);
        memcpy(out + used, text, length);
        used += length;
    }
    out[used] = 0;
}
