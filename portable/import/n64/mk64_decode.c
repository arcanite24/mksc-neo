#include "mk64_internal.h"
#include <string.h>

uint32_t mk64_be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
uint32_t mk64_be32(const uint8_t *p) { return mk64_be16(p) << 16 | mk64_be16(p + 2); }
int32_t mk64_i16(const uint8_t *p) {
    const uint32_t v = mk64_be16(p);
    return v < 32768 ? (int32_t)v : (int32_t)v - 65536;
}

int mksc_mk64_byte_order(const uint8_t *rom, size_t size)
{
    if (!rom || size < 4) return 0;
    const uint32_t magic = mk64_be32(rom);
    return magic == 0x80371240u ? 1 : magic == 0x37804012u ? 2 : magic == 0x40123780u ? 3 : 0;
}

RiBuf *mk64_normalize(RiCtx *ctx, RiData data)
{
    const int order = mksc_mk64_byte_order(data.data, data.size);
    if (!order || data.size % 4) ri_fail(ctx, "MK64: invalid N64 byte order or truncated word");
    RiBuf *out = ri_buf_zero(ctx, data.size);
    const size_t mask = order == 2 ? 1 : order == 3 ? 3 : 0;
    for (size_t i = 0; i < data.size; ++i) out->data[i] = data.data[i ^ mask];
    return out;
}

/* MIO0: MSB-first literal mask, BE length/distance pairs and a separate
 * literal stream. The three streams must not overlap or leave the block. */
RiBuf *mk64_mio0(RiCtx *ctx, RiData data) { return mk64_mio0_block(ctx, data, NULL); }

RiBuf *mk64_mio0_block(RiCtx *ctx, RiData data, size_t *end)
{
    const uint8_t *h = ri_at(ctx, data, 0, 16);
    const size_t length = mk64_be32(h + 4), compressed = mk64_be32(h + 8), raw = mk64_be32(h + 12);
    if (memcmp(h, "MIO0", 4) || !length || length > 4u * 1024u * 1024u ||
        compressed < 16 || compressed > raw || raw > data.size)
        ri_fail(ctx, "MK64: invalid MIO0 header/size/streams");
    RiBuf *out = ri_buf_zero(ctx, length);
    size_t flags = 16, pair = compressed, literal = raw, written = 0;
    unsigned mask = 0, bits = 0;
    while (written < length) {
        if (!bits) {
            if (flags >= compressed) ri_fail(ctx, "MK64: truncated MIO0 mask");
            mask = data.data[flags++]; bits = 8;
        }
        if (mask & 128u) {
            if (literal >= data.size) ri_fail(ctx, "MK64: truncated MIO0 literal");
            out->data[written++] = data.data[literal++];
        } else {
            if (pair > raw || raw - pair < 2) ri_fail(ctx, "MK64: truncated MIO0 pair");
            const unsigned code = mk64_be16(data.data + pair); pair += 2;
            const size_t count = (code >> 12) + 3, distance = (code & 4095) + 1;
            if (distance > written || count > length - written)
                ri_fail(ctx, "MK64: invalid MIO0 back-reference");
            for (size_t i = 0; i < count; ++i) {
                out->data[written] = out->data[written - distance]; ++written;
            }
        }
        mask <<= 1; --bits;
    }
    if (end) *end = literal > pair ? literal : pair;
    return out;
}

size_t mk64_mio0_next(RiCtx *ctx, size_t at)
{
    if (at >= ctx->rom.size) ri_fail(ctx, "MK64: MIO0 block outside the cartridge");
    size_t end = 0;
    ri_buf_free(ctx, mk64_mio0_block(ctx, ri_span(ctx, ctx->rom, at, ctx->rom.size - at), &end));
    return (at + end + 3u) & ~(size_t)3u;
}

static void command(RiCtx *ctx, RiBuf *out, uint32_t a, uint32_t b)
{
    if (out->size >= 2u * 1024u * 1024u) ri_fail(ctx, "MK64: display-list size limit");
    const uint8_t bytes[8] = {(uint8_t)(a >> 24), (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a,
                              (uint8_t)(b >> 24), (uint8_t)(b >> 16), (uint8_t)(b >> 8), (uint8_t)b};
    ri_put(ctx, out, bytes, sizeof bytes);
}
static uint32_t triangle(uint32_t v)
{
    return ((v & 31u) * 2u << 16) | (((v >> 5) & 31u) * 2u << 8) | (((v >> 10) & 31u) * 2u);
}

/* Format facts: n64decomp/mk64 58cfcb0 src/racing/memory.c and
 * tools/displaylist_packer.c. Emit every expanded command slot, including
 * render-state slots, so segmented call offsets retain their meaning. */
RiBuf *mk64_display_lists(RiCtx *ctx, RiData packed, uint32_t expected_size)
{
    RiBuf *out = ri_buf(ctx);
    size_t at = 0;
    expected_size += 8; /* CourseTable stores the last command offset. */
    if (expected_size < 8 || expected_size > 2u * 1024u * 1024u || expected_size % 8)
        ri_fail(ctx, "MK64: invalid expanded display-list size");
    for (;;) {
        const uint32_t op = ri_get_u8(ctx, packed, at++);
        if (op == 0xFF) break;
        unsigned args = 0;
        if ((op >= 0x1A && op <= 0x1F) || op == 0x2C || op == 0x29 || op == 0x2B ||
            (op >= 0x33 && op <= 0x52)) args = 2;
        else if ((op >= 0x20 && op <= 0x25) || op == 0x30) args = 3;
        else if (op == 0x28 || op == 0x58) args = 4;
        const uint8_t *p = ri_at(ctx, packed, at, args); at += args;
        if (op <= 0x14) {
            command(ctx,out,0xBC000002,0x80000040); command(ctx,out,0x03860010,0x09000008+op*24);
            command(ctx,out,0x03880010,0x09000000+op*24);
        } else if ((op >= 0x1A && op <= 0x1F) || op == 0x2C) {
            const unsigned kind = op == 0x2C ? 0 : op - 0x1A;
            const unsigned width = kind % 3 == 1 ? 64 : 32, height = kind % 3 == 2 ? 64 : 32;
            const unsigned fmt = kind >= 3 ? 3 : 0;
            command(ctx,out,0xE8000000,0);
            command(ctx,out,0xF5100000 | fmt << 21 | (width / 4) << 9 | (op == 0x2C ? 256u : 0u),
                    (uint32_t)(p[1]&15) << 18 | (uint32_t)(p[1]>>4) << 14 | (uint32_t)(p[0]&15) << 8 | (uint32_t)(p[0]>>4) << 4);
            command(ctx,out,0xF2000000,(width-1) << 14 | (height-1) << 2);
        } else if (op >= 0x20 && op <= 0x25) {
            const unsigned kind=op-0x20, width=kind%3==1?64:32, height=kind%3==2?64:32;
            command(ctx,out,0xFD100000 | (kind>=3?3u:0u)<<21,0x05000000 | (uint32_t)p[0]<<11);
            command(ctx,out,0xE8000000,0);
            command(ctx,out,0xF5100000 | (kind>=3?3u:0u)<<21 | (p[2]&15), (uint32_t)(p[2]>>4)<<24);
            command(ctx,out,0xE6000000,0);
            command(ctx,out,0xF3000000,(uint32_t)(p[2]>>4)<<24 | (width*height-1)<<12 | (width==64?128u:256u));
        } else if (op == 0x28 || (op >= 0x33 && op <= 0x52)) {
            const unsigned count=op==0x28?p[2]&63u:op-0x32, first=op==0x28?p[3]&63u:0;
            if (!count || first+count>32) ri_fail(ctx,"MK64: invalid vertex cache range");
            command(ctx,out,0x04000000 | first*2u<<16 | (count*0x410u-1),0x04000000 | ri_le16(p)*16u);
        } else if (op == 0x29) command(ctx,out,0xBF000000,triangle(ri_le16(p)));
        else if (op == 0x58) command(ctx,out,0xB1000000|triangle(ri_le16(p)),triangle(ri_le16(p+2)));
        else if (op == 0x30) {
            const uint32_t v=ri_le16(p)|(uint32_t)p[2]<<16;
            command(ctx,out,0xB5000000,((v>>15)&31u)*2u<<24 | (v&31u)*2u<<16 |
                    ((v>>5)&31u)*2u<<8 | ((v>>10)&31u)*2u);
        }
        else if (op == 0x2B) command(ctx,out,0x06000000,0x07000000|ri_le16(p)*8u);
        else if (op == 0x2A) command(ctx,out,0xB8000000,0);
        else if (op == 0x26 || op == 0x27) command(ctx,out,op==0x26?0xBB000001u:0xBB000000u,op==0x26?0xFFFFFFFFu:0x00010001u); /* G_ON, G_OFF */
        else if (op == 0x56 || op == 0x57) command(ctx,out,op==0x56?0xB7000000:0xB6000000,0x2000);
        else if (op == 0x2D) command(ctx,out,0xBE000000,0);
        else if (op == 0x15 || op == 0x16 || op == 0x17 || op == 0x2E || op == 0x53)
            command(ctx,out,0xFC000000,op); /* conversion-only combine marker */
        else if (op == 0x18 || op == 0x19 || op == 0x2F || op == 0x54 || op == 0x55)
            command(ctx,out,0xB9000000,op);
        else ri_fail(ctx,"MK64: unsupported packed display-list opcode 0x%02X at %zu",(unsigned)op,at-args-1);
        if (out->size > expected_size) ri_fail(ctx,"MK64: expanded display list exceeds declared size");
    }
    if (out->size != expected_size) ri_fail(ctx,"MK64: expanded display-list size mismatch (%zu/%u)",out->size,(unsigned)expected_size);
    return out;
}
