#include "rom_import_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ri_fail(RiCtx *ctx, const char *format, ...)
{
    if (ctx->error && ctx->error_size)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(ctx->error, ctx->error_size, format, args);
        va_end(args);
    }
    longjmp(ctx->failure, 1);
}

RiBuf *ri_buf(RiCtx *ctx)
{
    RiBuf *buffer = calloc(1, sizeof(*buffer));
    if (!buffer)
        ri_fail(ctx, "out of memory");
    buffer->next = ctx->buffers;
    if (ctx->buffers)
        ctx->buffers->previous = buffer;
    ctx->buffers = buffer;
    return buffer;
}

void ri_buf_free(RiCtx *ctx, RiBuf *buffer)
{
    if (!buffer)
        return;
    if (buffer->previous)
        buffer->previous->next = buffer->next;
    else
        ctx->buffers = buffer->next;
    if (buffer->next)
        buffer->next->previous = buffer->previous;
    free(buffer->data);
    free(buffer);
}

void ri_reserve(RiCtx *ctx, RiBuf *buffer, size_t capacity)
{
    if (capacity <= buffer->capacity)
        return;
    size_t grown = buffer->capacity ? buffer->capacity : 64;
    while (grown < capacity)
    {
        if (grown > SIZE_MAX / 2)
            ri_fail(ctx, "importer buffer too large");
        grown *= 2;
    }
    uint8_t *data = realloc(buffer->data, grown);
    if (!data)
        ri_fail(ctx, "out of memory");
    buffer->data = data;
    buffer->capacity = grown;
}

void ri_resize(RiCtx *ctx, RiBuf *buffer, size_t size)
{
    ri_reserve(ctx, buffer, size);
    if (size > buffer->size)
        memset(buffer->data + buffer->size, 0, size - buffer->size);
    buffer->size = size;
}

RiBuf *ri_buf_zero(RiCtx *ctx, size_t size)
{
    RiBuf *buffer = ri_buf(ctx);
    ri_resize(ctx, buffer, size);
    return buffer;
}

RiBuf *ri_buf_copy(RiCtx *ctx, const void *data, size_t size)
{
    RiBuf *buffer = ri_buf(ctx);
    ri_put(ctx, buffer, data, size);
    return buffer;
}

void ri_put(RiCtx *ctx, RiBuf *buffer, const void *data, size_t size)
{
    if (!size)
        return;
    if (size > SIZE_MAX - buffer->size)
        ri_fail(ctx, "importer buffer too large");
    ri_reserve(ctx, buffer, buffer->size + size);
    memmove(buffer->data + buffer->size, data, size);
    buffer->size += size;
}

void ri_put_buf(RiCtx *ctx, RiBuf *buffer, const RiBuf *other)
{
    ri_put(ctx, buffer, other->data, other->size);
}

void ri_put_zero(RiCtx *ctx, RiBuf *buffer, size_t size)
{
    ri_resize(ctx, buffer, buffer->size + size);
}

void ri_u8(RiCtx *ctx, RiBuf *buffer, uint32_t value)
{
    uint8_t byte = (uint8_t)value;
    ri_put(ctx, buffer, &byte, 1);
}

void ri_u16(RiCtx *ctx, RiBuf *buffer, uint32_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    ri_put(ctx, buffer, bytes, 2);
}

void ri_u32(RiCtx *ctx, RiBuf *buffer, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 24)};
    ri_put(ctx, buffer, bytes, 4);
}

void ri_i16(RiCtx *ctx, RiBuf *buffer, int32_t value)
{
    ri_u16(ctx, buffer, (uint32_t)value);
}

void ri_i32(RiCtx *ctx, RiBuf *buffer, int32_t value)
{
    ri_u32(ctx, buffer, (uint32_t)value);
}

void ri_words(RiCtx *ctx, RiBuf *buffer, size_t count, ...)
{
    va_list args;
    va_start(args, count);
    for (size_t index = 0; index < count; ++index)
        ri_u32(ctx, buffer, va_arg(args, uint32_t));
    va_end(args);
}

void ri_set_u16(RiBuf *buffer, size_t offset, uint32_t value)
{
    buffer->data[offset] = (uint8_t)value;
    buffer->data[offset + 1] = (uint8_t)(value >> 8);
}

void ri_set_u32(RiBuf *buffer, size_t offset, uint32_t value)
{
    for (int index = 0; index < 4; ++index)
        buffer->data[offset + index] = (uint8_t)(value >> (8 * index));
}

RiData ri_view(const RiBuf *buffer)
{
    RiData view = {buffer->data, buffer->size};
    return view;
}

RiData ri_span(RiCtx *ctx, RiData source, size_t offset, size_t size)
{
    if (offset > source.size || size > source.size - offset)
        ri_fail(ctx, "data range outside source: %#zx+%#zx", offset, size);
    RiData view = {source.data + offset, size};
    return view;
}

const uint8_t *ri_at(RiCtx *ctx, RiData source, size_t offset, size_t size)
{
    return ri_span(ctx, source, offset, size).data;
}

uint32_t ri_le16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

uint32_t ri_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

uint32_t ri_get_u8(RiCtx *ctx, RiData source, size_t offset)
{
    return *ri_at(ctx, source, offset, 1);
}

uint32_t ri_get_u16(RiCtx *ctx, RiData source, size_t offset)
{
    return ri_le16(ri_at(ctx, source, offset, 2));
}

uint32_t ri_get_u32(RiCtx *ctx, RiData source, size_t offset)
{
    return ri_le32(ri_at(ctx, source, offset, 4));
}

int32_t ri_get_i16(RiCtx *ctx, RiData source, size_t offset)
{
    return (int32_t)(int16_t)(uint16_t)ri_get_u16(ctx, source, offset);
}

int32_t ri_get_i32(RiCtx *ctx, RiData source, size_t offset)
{
    return (int32_t)ri_get_u32(ctx, source, offset);
}

size_t ri_pointer(RiCtx *ctx, RiData source, size_t offset)
{
    uint32_t value = ri_get_u32(ctx, source, offset);
    if (value < 0x08000000u || (size_t)(value - 0x08000000u) >= source.size)
        ri_fail(ctx, "invalid ROM pointer 0x%08x at %#zx", (unsigned)value, offset);
    return value - 0x08000000u;
}

RiBuf *ri_lz77(RiCtx *ctx, RiData source, size_t offset)
{
    if (offset > source.size || source.size - offset < 4 || source.data[offset] != 0x10)
        ri_fail(ctx, "invalid LZ77 header at %#zx", offset);
    size_t size = (size_t)source.data[offset + 1] | (size_t)source.data[offset + 2] << 8 |
                  (size_t)source.data[offset + 3] << 16;
    size_t cursor = offset + 4;
    RiBuf *out = ri_buf(ctx);
    ri_reserve(ctx, out, size ? size : 1);
    while (out->size < size)
    {
        if (cursor >= source.size)
            ri_fail(ctx, "truncated LZ77 flags at %#zx", offset);
        uint8_t flags = source.data[cursor++];
        for (int bit = 7; bit >= 0 && out->size < size; --bit)
        {
            if (flags & (1u << bit))
            {
                if (cursor + 2 > source.size)
                    ri_fail(ctx, "truncated LZ77 match at %#zx", offset);
                uint32_t pair = (uint32_t)source.data[cursor] << 8 | source.data[cursor + 1];
                cursor += 2;
                size_t length = (pair >> 12) + 3, distance = (pair & 0xFFF) + 1;
                if (distance > out->size)
                    ri_fail(ctx, "invalid LZ77 distance at %#zx", offset);
                for (size_t index = 0; index < length && out->size < size; ++index)
                {
                    out->data[out->size] = out->data[out->size - distance];
                    out->size++;
                }
            }
            else
            {
                if (cursor >= source.size)
                    ri_fail(ctx, "truncated LZ77 literal at %#zx", offset);
                out->data[out->size++] = source.data[cursor++];
            }
        }
    }
    return out;
}

void ri_section(RiCtx *ctx, uint32_t kind, uint32_t content_id, uint32_t version, RiBuf *data)
{
    if (ctx->section_count == ctx->section_capacity)
    {
        size_t capacity = ctx->section_capacity ? ctx->section_capacity * 2 : 256;
        RiSection *sections = realloc(ctx->sections, capacity * sizeof(*sections));
        if (!sections)
            ri_fail(ctx, "out of memory");
        ctx->sections = sections;
        ctx->section_capacity = capacity;
    }
    RiSection *section = &ctx->sections[ctx->section_count++];
    section->kind = kind;
    section->content_id = content_id;
    section->version = version;
    section->data = data;
}

/* ---- Hashes ---------------------------------------------------------- */

static uint32_t rotl(uint32_t value, int count)
{
    return value << count | value >> (32 - count);
}
static uint32_t rotr(uint32_t value, int count)
{
    return value >> count | value << (32 - count);
}

typedef struct HashState
{
    uint32_t h[8];
    uint8_t block[64];
    size_t used;
    uint64_t total;
    int sha256;
} HashState;

static void hash_block(HashState *state, const uint8_t *block)
{
    uint32_t w[80];
    if (state->sha256)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
                   (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state->h[0], b = state->h[1], c = state->h[2], d = state->h[3];
        uint32_t e = state->h[4], f = state->h[5], g = state->h[6], h = state->h[7];
        for (int i = 0; i < 64; ++i)
        {
            uint32_t t1 =
                h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state->h[0] += a;
        state->h[1] += b;
        state->h[2] += c;
        state->h[3] += d;
        state->h[4] += e;
        state->h[5] += f;
        state->h[6] += g;
        state->h[7] += h;
        return;
    }
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
               (uint32_t)block[i * 4 + 2] << 8 | block[i * 4 + 3];
    for (int i = 16; i < 80; ++i)
        w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = state->h[0], b = state->h[1], c = state->h[2], d = state->h[3], e = state->h[4];
    for (int i = 0; i < 80; ++i)
    {
        uint32_t f, k;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = t;
    }
    state->h[0] += a;
    state->h[1] += b;
    state->h[2] += c;
    state->h[3] += d;
    state->h[4] += e;
}

static void hash_init(HashState *state, int sha256)
{
    static const uint32_t sha1_init[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                                          0xC3D2E1F0};
    static const uint32_t sha256_init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memset(state, 0, sizeof(*state));
    state->sha256 = sha256;
    memcpy(state->h, sha256 ? sha256_init : sha1_init,
           sha256 ? sizeof(sha256_init) : sizeof(sha1_init));
}

static void hash_update(HashState *state, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    state->total += size;
    while (size)
    {
        size_t take = 64 - state->used;
        if (take > size)
            take = size;
        memcpy(state->block + state->used, bytes, take);
        state->used += take;
        bytes += take;
        size -= take;
        if (state->used == 64)
        {
            hash_block(state, state->block);
            state->used = 0;
        }
    }
}

static void hash_final(HashState *state, uint8_t *digest)
{
    uint64_t bits = state->total * 8;
    uint8_t pad = 0x80, zero = 0, length[8];
    hash_update(state, &pad, 1);
    while (state->used != 56)
        hash_update(state, &zero, 1);
    for (int i = 0; i < 8; ++i)
        length[i] = (uint8_t)(bits >> (56 - 8 * i));
    hash_update(state, length, 8);
    int words = state->sha256 ? 8 : 5;
    for (int i = 0; i < words; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i * 4 + j] = (uint8_t)(state->h[i] >> (24 - 8 * j));
}

void mksc_sha1(const void *data, size_t size, uint8_t digest[20])
{
    HashState state;
    hash_init(&state, 0);
    hash_update(&state, data, size);
    hash_final(&state, digest);
}

void mksc_sha256(const void *data, size_t size, uint8_t digest[32])
{
    HashState state;
    hash_init(&state, 1);
    hash_update(&state, data, size);
    hash_final(&state, digest);
}

void mksc_hex(const uint8_t *digest, size_t size, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i)
    {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[size * 2] = 0;
}

int mksc_rom_import_supported(const uint8_t *rom, size_t size)
{
    uint8_t digest[20];
    char hex[41];
    if (!rom || size != 0x400000)
        return 0;
    mksc_sha1(rom, size, digest);
    mksc_hex(digest, 20, hex);
    return strcmp(hex, MKSC_ROM_IMPORT_SHA1) == 0;
}

/* ---- Pack writer (build_asset_pack.build_pack) --------------------------- */

enum
{
    PACK_HEADER = 112,
    PACK_ENTRY = 40,
    PACK_FLAGS = 3
};

static int section_order(const void *left, const void *right)
{
    const RiSection *a = left, *b = right;
    if (a->kind != b->kind)
        return a->kind < b->kind ? -1 : 1;
    if (a->content_id != b->content_id)
        return a->content_id < b->content_id ? -1 : 1;
    return 0;
}

static void put_u64(uint8_t *out, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out[i] = (uint8_t)(value >> (8 * i));
}

static void put_u32(uint8_t *out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out[i] = (uint8_t)(value >> (8 * i));
}

static void write_pack(RiCtx *ctx, MkscRomImportResult *result)
{
    size_t count = ctx->section_count;
    qsort(ctx->sections, count, sizeof(*ctx->sections), section_order);
    for (size_t i = 0; i < count; ++i)
    {
        const RiSection *section = &ctx->sections[i];
        if (!section->kind || !section->content_id)
            ri_fail(ctx, "section kind and content_id must be positive");
        if (i && !section_order(section, section - 1))
            ri_fail(ctx, "duplicate imported section %u/%u", (unsigned)section->kind,
                    (unsigned)section->content_id);
        if (!section->data || !section->data->size)
            ri_fail(ctx, "empty section %u/%u", (unsigned)section->kind,
                    (unsigned)section->content_id);
    }
    size_t payload = (PACK_HEADER + count * PACK_ENTRY + 7) & ~(size_t)7, total = payload;
    for (size_t i = 0; i < count; ++i)
        total = ((total + 7) & ~(size_t)7) + ctx->sections[i].data->size;
    uint8_t *pack = calloc(1, total);
    if (!pack)
        ri_fail(ctx, "out of memory");
    memcpy(pack, "MKSCPACK", 8);
    pack[8] = 1;
    pack[10] = PACK_HEADER;
    put_u32(pack + 12, 0x12345678);
    put_u64(pack + 16, total);
    put_u32(pack + 24, (uint32_t)count);
    put_u32(pack + 28, PACK_ENTRY);
    put_u64(pack + 32, PACK_HEADER);
    HashState deterministic;
    hash_init(&deterministic, 1);
    size_t cursor = payload;
    for (size_t i = 0; i < count; ++i)
    {
        const RiSection *section = &ctx->sections[i];
        uint8_t *entry = pack + PACK_HEADER + i * PACK_ENTRY;
        cursor = (cursor + 7) & ~(size_t)7;
        put_u32(entry, section->kind);
        put_u32(entry + 4, section->content_id);
        put_u32(entry + 8, section->version);
        put_u32(entry + 12, PACK_FLAGS);
        put_u64(entry + 16, cursor);
        put_u64(entry + 24, section->data->size);
        put_u32(entry + 32, 1);
        memcpy(pack + cursor, section->data->data, section->data->size);
        cursor += section->data->size;
        /* Every imported section carries FLAG_DETERMINISTIC (2). */
        hash_update(&deterministic, entry, PACK_ENTRY);
        hash_update(&deterministic, section->data->data, section->data->size);
    }
    hash_final(&deterministic, result->deterministic_sha256);
    memcpy(pack + 72, result->deterministic_sha256, 32);
    mksc_sha256(pack, total, result->pack_sha256);
    memcpy(pack + 40, result->pack_sha256, 32);
    result->pack = pack;
    result->pack_size = total;
    result->section_count = count;
}

static void release(RiCtx *ctx)
{
    while (ctx->buffers)
        ri_buf_free(ctx, ctx->buffers);
    free(ctx->sections);
    ctx->sections = NULL;
}

int ri_try(RiCtx *ctx, RiAttempt attempt, void *argument, char *reason, size_t reason_size)
{
    jmp_buf outer;
    char *const error = ctx->error;
    const size_t error_size = ctx->error_size;
    memcpy(&outer, &ctx->failure, sizeof(outer));
    ctx->error = reason;
    ctx->error_size = reason_size;
    volatile int succeeded = 0; /* written between setjmp and longjmp */
    if (setjmp(ctx->failure) == 0)
    {
        attempt(ctx, argument);
        succeeded = 1;
    }
    memcpy(&ctx->failure, &outer, sizeof(outer));
    ctx->error = error;
    ctx->error_size = error_size;
    if (!succeeded && ctx->cancelled)
        ri_fail(ctx, "import cancelled");
    return succeeded;
}

void ri_step(RiCtx *ctx, uint32_t done, uint32_t total)
{
    if (ctx->progress && ctx->progress(ctx->progress_context, done, total))
    {
        ctx->cancelled = 1;
        ri_fail(ctx, "import cancelled");
    }
}

static int build(const uint8_t *rom, size_t size, int verify_sha1, const RiGroup *groups,
                 size_t group_count, void *job, MkscImportProgress progress, void *context,
                 MkscRomImportResult *result, char *error, size_t error_size)
{
    /* volatile: modified between setjmp and a possible longjmp. */
    RiCtx *volatile ctx = calloc(1, sizeof(RiCtx));
    memset(result, 0, sizeof(*result));
    if (error && error_size)
        error[0] = 0;
    if (!ctx)
    {
        if (error && error_size)
            snprintf(error, error_size, "out of memory");
        return 0;
    }
    ctx->rom.data = rom;
    ctx->rom.size = size;
    ctx->error = error;
    ctx->error_size = error_size;
    ctx->progress = progress;
    ctx->progress_context = context;
    ctx->job = job;
    if (setjmp(ctx->failure))
    {
        const int cancelled = ctx->cancelled;
        free(result->pack);
        memset(result, 0, sizeof(*result));
        release(ctx);
        free(ctx);
        return cancelled ? -1 : 0;
    }
    if (verify_sha1 && !mksc_rom_import_supported(rom, size))
        ri_fail(ctx, "import requires the unmodified US revision 0 ROM (SHA-1 %s)",
                MKSC_ROM_IMPORT_SHA1);
    for (size_t index = 0; index < group_count; ++index)
        groups[index](ctx);
    write_pack(ctx, result);
    release(ctx);
    free(ctx);
    return 1;
}

int ri_build(const uint8_t *rom, size_t size, int verify_sha1, const RiGroup *groups,
             size_t group_count, MkscRomImportResult *result, char *error, size_t error_size)
{
    return build(rom, size, verify_sha1, groups, group_count, NULL, NULL, NULL, result, error,
                 error_size);
}

int ri_build_steps(const uint8_t *rom, size_t size, const RiGroup *groups, size_t group_count,
                   void *job, MkscImportProgress progress, void *context,
                   MkscRomImportResult *result, char *error, size_t error_size)
{
    return build(rom, size, 0, groups, group_count, job, progress, context, result, error,
                 error_size);
}

void mksc_rom_import_free(MkscRomImportResult *result)
{
    if (!result)
        return;
    free(result->pack);
    memset(result, 0, sizeof(*result));
}
