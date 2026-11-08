#include "rom_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Inflate (RFC 1951), canonical Huffman decoding one bit at a time. ---- */

enum { MAX_BITS = 15, MAX_LITLEN = 288, MAX_DIST = 30 };

typedef struct
{
    const uint8_t *in;
    size_t in_size, in_pos;
    uint32_t bits;
    unsigned bit_count;
    uint8_t *out;
    size_t out_size, out_pos;
} Inflate;

typedef struct
{
    uint16_t count[MAX_BITS + 1]; /* Codes of each length. */
    uint16_t symbol[MAX_LITLEN];  /* Symbols ordered by code. */
} Huffman;

/* Next n bits, least significant first; -1 past the end of input. */
static int read_bits(Inflate *s, unsigned n)
{
    while (s->bit_count < n)
    {
        if (s->in_pos >= s->in_size) return -1;
        s->bits |= (uint32_t)s->in[s->in_pos++] << s->bit_count;
        s->bit_count += 8u;
    }
    const int value = (int)(s->bits & ((1u << n) - 1u));
    s->bits >>= n;
    s->bit_count -= n;
    return value;
}

/* Builds the decoding table from code lengths; 0 when over-subscribed. */
static int build_huffman(Huffman *h, const uint8_t *lengths, unsigned count)
{
    memset(h->count, 0, sizeof h->count);
    for (unsigned i = 0; i < count; ++i) ++h->count[lengths[i]];
    if (h->count[0] == count) return 1; /* No codes: decoding any will fail. */
    int left = 1;
    for (unsigned len = 1; len <= MAX_BITS; ++len)
    {
        left = left * 2 - h->count[len];
        if (left < 0) return 0;
    }
    uint16_t offset[MAX_BITS + 1];
    offset[1] = 0;
    for (unsigned len = 1; len < MAX_BITS; ++len) offset[len + 1] = (uint16_t)(offset[len] + h->count[len]);
    for (unsigned i = 0; i < count; ++i)
        if (lengths[i] != 0) h->symbol[offset[lengths[i]]++] = (uint16_t)i;
    return 1;
}

static int decode(Inflate *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0;
    for (unsigned len = 1; len <= MAX_BITS; ++len)
    {
        const int bit = read_bits(s, 1u);
        if (bit < 0) return -1;
        code |= bit;
        const int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static int inflate_codes(Inflate *s, const Huffman *litlen, const Huffman *dist)
{
    static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                           193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;)
    {
        int symbol = decode(s, litlen);
        if (symbol < 0) return 0;
        if (symbol < 256)
        {
            if (s->out_pos >= s->out_size) return 0;
            s->out[s->out_pos++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256) return 1;
        symbol -= 257;
        if (symbol >= 29) return 0;
        const int extra = read_bits(s, length_extra[symbol]);
        const int dist_symbol = decode(s, dist);
        if (extra < 0 || dist_symbol < 0 || dist_symbol >= 30) return 0;
        const int dist_more = read_bits(s, dist_extra[dist_symbol]);
        if (dist_more < 0) return 0;
        const size_t length = (size_t)length_base[symbol] + (size_t)extra;
        const size_t distance = (size_t)dist_base[dist_symbol] + (size_t)dist_more;
        if (distance > s->out_pos || length > s->out_size - s->out_pos) return 0;
        for (size_t i = 0; i < length; ++i, ++s->out_pos) s->out[s->out_pos] = s->out[s->out_pos - distance];
    }
}

static int inflate_stored(Inflate *s)
{
    s->bits = 0u; /* Stored blocks start on a byte boundary. */
    s->bit_count = 0u;
    if (s->in_size - s->in_pos < 4u) return 0;
    const unsigned length = s->in[s->in_pos] | (unsigned)s->in[s->in_pos + 1u] << 8;
    const unsigned check = s->in[s->in_pos + 2u] | (unsigned)s->in[s->in_pos + 3u] << 8;
    s->in_pos += 4u;
    if (length != (~check & 0xffffu) || s->in_size - s->in_pos < length || s->out_size - s->out_pos < length) return 0;
    memcpy(s->out + s->out_pos, s->in + s->in_pos, length);
    s->in_pos += length;
    s->out_pos += length;
    return 1;
}

static int inflate_fixed(Inflate *s)
{
    Huffman litlen, dist;
    uint8_t lengths[MAX_LITLEN];
    unsigned i = 0;
    for (; i < 144u; ++i) lengths[i] = 8u;
    for (; i < 256u; ++i) lengths[i] = 9u;
    for (; i < 280u; ++i) lengths[i] = 7u;
    for (; i < MAX_LITLEN; ++i) lengths[i] = 8u;
    build_huffman(&litlen, lengths, MAX_LITLEN);
    memset(lengths, 5, MAX_DIST);
    build_huffman(&dist, lengths, MAX_DIST);
    return inflate_codes(s, &litlen, &dist);
}

static int inflate_dynamic(Inflate *s)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = read_bits(s, 5u) + 257, ndist = read_bits(s, 5u) + 1, ncode = read_bits(s, 4u) + 4;
    if (nlen < 257 || ndist < 1 || ncode < 4 || nlen > 286 || ndist > MAX_DIST) return 0;
    uint8_t lengths[MAX_LITLEN + MAX_DIST] = {0};
    for (int i = 0; i < ncode; ++i)
    {
        const int length = read_bits(s, 3u);
        if (length < 0) return 0;
        lengths[order[i]] = (uint8_t)length;
    }
    Huffman code_lengths, litlen, dist;
    if (!build_huffman(&code_lengths, lengths, 19u)) return 0;
    for (int index = 0; index < nlen + ndist;)
    {
        int symbol = decode(s, &code_lengths);
        if (symbol < 0) return 0;
        if (symbol < 16)
        {
            lengths[index++] = (uint8_t)symbol;
            continue;
        }
        uint8_t value = 0u;
        int repeat;
        if (symbol == 16)
        {
            if (index == 0) return 0;
            value = lengths[index - 1];
            repeat = 3 + read_bits(s, 2u);
        }
        else if (symbol == 17)
            repeat = 3 + read_bits(s, 3u);
        else
            repeat = 11 + read_bits(s, 7u);
        if (repeat < 3 || index + repeat > nlen + ndist) return 0;
        while (repeat-- > 0) lengths[index++] = value;
    }
    if (lengths[256] == 0u) return 0; /* No end-of-block code. */
    if (!build_huffman(&litlen, lengths, (unsigned)nlen) || !build_huffman(&dist, lengths + nlen, (unsigned)ndist))
        return 0;
    return inflate_codes(s, &litlen, &dist);
}

static int inflate_all(Inflate *s)
{
    for (;;)
    {
        const int last = read_bits(s, 1u), type = read_bits(s, 2u);
        int ok;
        if (last < 0 || type < 0) return 0;
        if (type == 0) ok = inflate_stored(s);
        else if (type == 1) ok = inflate_fixed(s);
        else if (type == 2) ok = inflate_dynamic(s);
        else return 0;
        if (!ok) return 0;
        if (last) return 1;
    }
}

/* ---- Zip container. ---- */

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int mksc_rom_zip_detect(const uint8_t *data, size_t size)
{
    return data != NULL && size >= 4u && le32(data) == 0x04034b50u;
}

/* extension is like ".gba", compared without regard to ASCII case. */
static int ends_with(const uint8_t *name, size_t length, const char *extension)
{
    /* A semicolon-separated extension set lets the shared game picker
     * extract DS or N64 ROMs without guessing from the archive filename. */
    while (*extension) {
        const char *end = strchr(extension, ';');
        const size_t tail_length = end ? (size_t)(end-extension) : strlen(extension);
        if (tail_length && length >= tail_length) {
            const uint8_t *tail = name + length - tail_length;
            size_t index = 0;
            while (index < tail_length && (tail[index] | 0x20) == ((uint8_t)extension[index] | 0x20)) ++index;
            if (index == tail_length) return 1;
        }
        if (!end) break;
        extension = end + 1;
    }
    return 0;
}

int mksc_rom_zip_extract(const uint8_t *zip, size_t zip_size, const char *extension, size_t max_size, uint8_t **out,
                         size_t *out_size, char *error, size_t error_size)
{
    *out = NULL;
    *out_size = 0u;
    /* End of central directory: the last 22+ bytes, before an optional comment. */
    size_t eocd = 0u;
    int found = 0;
    for (size_t back = 22u; back <= zip_size && back <= 22u + 0xffffu; ++back)
        if (le32(zip + zip_size - back) == 0x06054b50u)
        {
            eocd = zip_size - back;
            found = 1;
            break;
        }
    if (!found)
    {
        snprintf(error, error_size, "the zip archive is damaged (no directory)");
        return 0;
    }
    const size_t entries = le16(zip + eocd + 10u), directory = le32(zip + eocd + 16u);
    const uint8_t *first_match = NULL, *first_file = NULL;
    size_t files = 0u, position = directory;
    for (size_t i = 0; i < entries; ++i)
    {
        if (position > zip_size || zip_size - position < 46u || le32(zip + position) != 0x02014b50u) break;
        const uint8_t *entry = zip + position;
        const size_t name_length = le16(entry + 28u);
        const size_t next = 46u + name_length + le16(entry + 30u) + le16(entry + 32u);
        if (zip_size - position < next) break;
        if (name_length == 0u || entry[46u + name_length - 1u] != '/')
        {
            ++files;
            if (first_file == NULL) first_file = entry;
            if (first_match == NULL && ends_with(entry + 46u, name_length, extension)) first_match = entry;
        }
        position += next;
    }
    /* The matching entry, or the only file when its name has another extension. */
    const uint8_t *chosen = first_match != NULL ? first_match : files == 1u ? first_file : NULL;
    if (chosen == NULL)
    {
        snprintf(error, error_size, "the zip archive has no %s file", extension);
        return 0;
    }
    const uint32_t method = le16(chosen + 10u), crc = le32(chosen + 16u);
    const size_t packed = le32(chosen + 20u), size = le32(chosen + 24u), local = le32(chosen + 42u);
    if (size == 0u || size > max_size)
    {
        snprintf(error, error_size, "the ROM in the zip archive has an unexpected size");
        return 0;
    }
    if (local > zip_size || zip_size - local < 30u || le32(zip + local) != 0x04034b50u)
    {
        snprintf(error, error_size, "the zip archive is damaged");
        return 0;
    }
    const size_t data = local + 30u + le16(zip + local + 26u) + le16(zip + local + 28u);
    if (data > zip_size || zip_size - data < packed || (le16(chosen + 8u) & 1u) != 0u)
    {
        snprintf(error, error_size, "the zip archive is damaged or encrypted");
        return 0;
    }
    uint8_t *rom = malloc(size);
    if (rom == NULL)
    {
        snprintf(error, error_size, "out of memory");
        return 0;
    }
    int ok = 0;
    if (method == 0u && packed == size)
    {
        memcpy(rom, zip + data, size);
        ok = 1;
    }
    else if (method == 8u)
    {
        Inflate state = {zip + data, packed, 0u, 0u, 0u, rom, size, 0u};
        ok = inflate_all(&state) && state.out_pos == size;
    }
    if (!ok || crc32(rom, size) != crc)
    {
        free(rom);
        snprintf(error, error_size, method == 0u || method == 8u ? "the zip archive is damaged"
                                                                : "the zip archive uses an unsupported compression");
        return 0;
    }
    *out = rom;
    *out_size = size;
    return 1;
}
