#include "rom_import_internal.h"

#include <string.h>

enum
{
    AM_ROAD,
    AM_SKY,
    AM_OBJECT,
    AM_TILES,
    AM_FRONT,
    AM_MID,
    AM_BACK
};
enum
{
    AM_PRE,
    AM_POST
};
enum
{
    AM_ENTRY,
    AM_EVERY
};
enum
{
    AM_TIMER,
    AM_SUNSET,
    AM_DIRECTION,
    AM_FINISH,
    AM_RAINBOW,
    AM_STORM
};

typedef struct
{
    RiCtx *ctx;
    RiBuf *data, *lanes, *frames, *patches, *initial;
    uint32_t lane_count, frame_count, patch_count, initial_count;
} AmbientBuilder;

static void append_patch(AmbientBuilder *b, unsigned target, unsigned destination, RiData bytes,
                         int initial)
{
    RiCtx *ctx = b->ctx;
    const size_t count = target <= AM_OBJECT ? bytes.size / 2u : bytes.size;
    static const size_t bounds[7] = {64u, 64u, 128u, 0x4000u, 0x1000u, 0x1000u, 0x1000u};
    if (target > 6u || count == 0u || destination + count > bounds[target] ||
        (target <= AM_OBJECT && (bytes.size & 1u) != 0u) || b->data->size + bytes.size > 262144u)
        ri_fail(ctx, "invalid ambient patch");
    RiBuf *out = initial ? b->initial : b->patches;
    ri_u8(ctx, out, target);
    ri_u8(ctx, out, 0u);
    ri_u16(ctx, out, destination);
    ri_u16(ctx, out, (uint32_t)count);
    ri_u16(ctx, out, 0u);
    ri_u32(ctx, out, (uint32_t)b->data->size);
    ri_put(ctx, b->data, bytes.data, bytes.size);
    if (initial)
    {
        if (++b->initial_count > 32u)
            ri_fail(ctx, "too many ambient initial patches");
    }
    else if (++b->patch_count > 768u)
        ri_fail(ctx, "too many ambient patches");
}

static void append_rom_patch(AmbientBuilder *b, unsigned target, unsigned destination,
                             size_t source, size_t bytes, int initial)
{
    append_patch(b, target, destination, ri_span(b->ctx, b->ctx->rom, source, bytes), initial);
}

static void append_frame(AmbientBuilder *b, uint32_t duration, uint32_t first_patch)
{
    if (duration == 0u || duration > 65535u || b->patch_count - first_patch > 3u ||
        ++b->frame_count > 256u)
        ri_fail(b->ctx, "invalid ambient frame");
    ri_u16(b->ctx, b->frames, duration);
    ri_u16(b->ctx, b->frames, first_patch);
    ri_u8(b->ctx, b->frames, b->patch_count - first_patch);
    ri_u8(b->ctx, b->frames, 0u);
    ri_u16(b->ctx, b->frames, 0u);
}

static void append_lane(AmbientBuilder *b, unsigned stage, unsigned policy, unsigned controller,
                        unsigned trigger, uint32_t first, uint32_t loop, uint32_t initial_frame,
                        uint32_t counter, const uint32_t params[4])
{
    if (++b->lane_count > 16u || b->frame_count <= first)
        ri_fail(b->ctx, "invalid ambient lane");
    RiCtx *ctx = b->ctx;
    RiBuf *out = b->lanes;
    ri_u8(ctx, out, stage);
    ri_u8(ctx, out, policy);
    ri_u8(ctx, out, controller);
    ri_u8(ctx, out, trigger);
    ri_u16(ctx, out, first);
    ri_u16(ctx, out, b->frame_count - first);
    ri_u16(ctx, out, loop);
    ri_u16(ctx, out, initial_frame);
    ri_u32(ctx, out, counter);
    for (unsigned i = 0u; i < 4u; ++i)
        ri_u32(ctx, out, params != NULL ? params[i] : 0u);
}

static void append_callback_patches(AmbientBuilder *b, uint32_t function, uint32_t index)
{
    static const struct
    {
        uint32_t callback, indirect, source, destination, count;
    } tables[] = {
        {0x5a0a0, 1, 0xf1704, 26, 2},  {0x5a0d0, 0, 0x71304, 16, 6},  {0x5a0fc, 0, 0x71310, 22, 4},
        {0x5a128, 0, 0x714bc, 32, 8},  {0x5a154, 0, 0x7159c, 0, 10},  {0x5a188, 0, 0x716dc, 25, 5},
        {0x5a1b4, 1, 0xf1a1c, 30, 2},  {0x5a1e4, 1, 0xf1b2c, 60, 2},  {0x5a214, 1, 0xf1b88, 113, 5},
        {0x5a244, 1, 0xf1c1c, 97, 15}, {0x5a36c, 1, 0xf20a4, 12, 3},  {0x5a39c, 1, 0xf211c, 37, 9},
        {0x5a3cc, 1, 0xf21c4, 125, 3}, {0x5a3fc, 1, 0xf2220, 22, 4},  {0x5a474, 1, 0xf22e8, 24, 5},
        {0x5a4a4, 1, 0xf23d0, 10, 4},  {0x5a4d4, 1, 0xf24e0, 10, 5},  {0x5a504, 1, 0xf2554, 29, 3},
        {0x5a534, 0, 0x718c4, 113, 4}, {0x5a560, 0, 0x718ce, 124, 4}, {0x5a58c, 1, 0xf2794, 113, 5},
        {0x5a5bc, 1, 0xf287c, 147, 11}};
    RiCtx *ctx = b->ctx;
    function = (function & ~1u) - 0x08000000u;
    if (function == 0x5a020u || function == 0x5a060u)
    {
        const size_t source = function == 0x5a020u ? (index < 10u ? 0x70f2eu : 0x7110eu)
                                                   : (index < 10u ? 0x7104cu : 0x7122cu);
        append_rom_patch(b, AM_ROAD, function == 0x5a020u ? 37u : 32u,
                         source + (index < 10u ? index : index - 10u) * 32u,
                         function == 0x5a020u ? 22u : 10u, 0);
        return;
    }
    if (function == 0x5a320u)
    {
        append_rom_patch(b, AM_SKY, 16u, ri_pointer(ctx, ctx->rom, 0xf1f44u + index * 4u), 32u, 0);
        append_rom_patch(b, AM_SKY, 0u, ri_pointer(ctx, ctx->rom, 0xf1f60u + index * 4u), 32u, 0);
        return;
    }
    if (function == 0x5a42cu)
    {
        const size_t source = ri_pointer(ctx, ctx->rom, 0xf2220u + index * 4u);
        append_rom_patch(b, AM_ROAD, 16u, source + 10u, 6u, 0);
        append_rom_patch(b, AM_ROAD, 19u, source + 18u, 6u, 0);
        return;
    }
    if (function == 0x5a274u || function == 0x5a2a4u)
    {
        const size_t table = function == 0x5a274u ? 0xf1cb0u : 0xf1e08u;
        append_rom_patch(b, AM_OBJECT, function == 0x5a274u ? 40u : 8u,
                         ri_pointer(ctx, ctx->rom, table + index * 4u), 12u, 0);
        return;
    }
    for (size_t i = 0u; i < sizeof tables / sizeof tables[0]; ++i)
    {
        if (tables[i].callback != function)
            continue;
        const size_t source = tables[i].indirect
                                  ? ri_pointer(ctx, ctx->rom, tables[i].source + index * 4u)
                                  : tables[i].source + index * 32u;
        const unsigned destination = tables[i].destination,
                       target = destination >= 96u ? AM_SKY : AM_ROAD;
        if (function == 0x5a154u)
        {
            uint8_t colors[20];
            memcpy(colors, ri_at(ctx, ctx->rom, source, 20u), 20u);
            colors[0] = 0u;
            colors[1] = 0u;
            append_patch(b, target, 0u, (RiData){colors, sizeof colors}, 0);
        }
        else
            append_rom_patch(b, target, destination >= 96u ? destination - 96u : destination,
                             source, tables[i].count * 2u, 0);
        return;
    }
    ri_fail(ctx, "unsupported ambient palette callback %x", (unsigned)function);
}

/* Resolve callback scripts into bounded frame lanes during import, never at runtime. */
static void import_palette_script(AmbientBuilder *b, size_t address, unsigned trigger)
{
    size_t seen_pc[256];
    uint32_t seen_frame[256], seen_count = 0u;
    size_t pc = address;
    uint32_t index = 0u, first = b->frame_count, pending = b->patch_count;
    for (unsigned step = 0u; step < 256u; ++step)
    {
        for (uint32_t i = 0u; i < seen_count; ++i)
            if (seen_pc[i] == pc)
            {
                append_lane(b, AM_POST, AM_ENTRY, AM_TIMER, trigger, first, seen_frame[i] - first,
                            0u, 0u, NULL);
                return;
            }
        seen_pc[seen_count] = pc;
        seen_frame[seen_count++] = b->frame_count;
        const uint32_t command = ri_get_u32(b->ctx, b->ctx->rom, pc),
                       argument = ri_get_u32(b->ctx, b->ctx->rom, pc + 4u);
        if (command == 0u)
        {
            if (pending != b->patch_count)
                ri_fail(b->ctx, "ambient script terminates without a wait");
            append_lane(b, AM_POST, AM_ENTRY, AM_TIMER, trigger, first, 65535u, 0u, 0u, NULL);
            return;
        }
        if (command == 0x41u)
            index = argument;
        else if (command == 0xf4u)
            append_callback_patches(b, argument, index);
        else if (command == 0xf1u)
        {
            append_frame(b, argument, pending);
            pending = b->patch_count;
        }
        else if (command == 0xf2u)
        {
            const int32_t delta = ri_get_i32(b->ctx, b->ctx->rom, pc + 4u);
            const int64_t next = (int64_t)pc + (int64_t)delta * 8;
            if (next < 0 || (uint64_t)next >= b->ctx->rom.size)
                ri_fail(b->ctx, "invalid ambient loop");
            pc = (size_t)next;
            continue;
        }
        else
            ri_fail(b->ctx, "unsupported ambient script command %x", (unsigned)command);
        pc += 8u;
    }
    ri_fail(b->ctx, "ambient script exceeds command budget");
}

static void append_table_lane(AmbientBuilder *b, size_t table, unsigned frames, unsigned duration,
                              unsigned target, unsigned destination, unsigned colors,
                              unsigned stage, unsigned policy, unsigned initial_frame,
                              uint32_t initial_counter)
{
    const uint32_t first = b->frame_count, params[4] = {1u, 0u, 0u, 0u};
    for (unsigned i = 0u; i < frames; ++i)
    {
        const uint32_t patch_first = b->patch_count;
        append_rom_patch(b, target, destination, ri_pointer(b->ctx, b->ctx->rom, table + i * 4u),
                         colors * 2u, 0);
        append_frame(b, duration, patch_first);
    }
    append_lane(b, stage, policy, AM_TIMER, 0u, first, 0u, initial_frame, initial_counter, params);
}

static void import_background_lanes(AmbientBuilder *b, unsigned behavior)
{
    RiCtx *ctx = b->ctx;
    if (behavior == 3u || behavior == 7u)
    {
        append_table_lane(b, 0xe8130u, 12u, 16u, AM_SKY, 23u, 9u, AM_PRE, AM_EVERY, 0u, 0u);
        append_table_lane(b, 0xe8160u, 12u, 16u, AM_SKY, 0u, 16u, AM_PRE, AM_EVERY, 0u, 0u);
    }
    if (behavior == 11u)
    {
        append_table_lane(b, 0xe8190u, 12u, 16u, AM_SKY, 6u, 5u, AM_PRE, AM_EVERY, 0u, 0u);
        const uint32_t first = b->frame_count, params[4] = {1u, 0u, 0u, 0u};
        for (unsigned i = 0u; i < 2u; ++i)
        {
            const uint32_t p = b->patch_count;
            append_rom_patch(b, AM_SKY, 40u, ri_pointer(ctx, ctx->rom, 0xe81c0u + i * 4u), 6u, 0);
            append_rom_patch(b, AM_SKY, 46u, ri_pointer(ctx, ctx->rom, 0xe81c8u + i * 4u), 4u, 0);
            append_frame(b, 5u, p);
        }
        append_lane(b, AM_PRE, AM_EVERY, AM_TIMER, 0u, first, 0u, 0u, 0u, params);
    }
    if (behavior == 8u)
    {
        const uint32_t first = b->frame_count;
        for (unsigned direction = 0u; direction < 3u; ++direction)
        {
            const size_t table = ri_pointer(ctx, ctx->rom, 0xe8124u + direction * 4u);
            for (unsigned i = 0u; i < 6u; ++i)
            {
                const uint32_t p = b->patch_count;
                append_rom_patch(b, AM_TILES, 0x1ca0u, ri_pointer(ctx, ctx->rom, table + i * 4u),
                                 0x680u, 0);
                append_frame(b, 2u, p);
            }
        }
        append_lane(b, AM_PRE, AM_ENTRY, AM_DIRECTION, 0u, first, 0u, 0u, 0u, NULL);
        append_table_lane(b, 0xe80ccu, 4u, 2u, AM_ROAD, 42u, 4u, AM_PRE, AM_EVERY, 0u, 0u);
    }
    if (behavior == 6u)
    {
        const uint32_t first = b->frame_count;
        for (unsigned i = 0u; i < 64u; ++i)
        {
            const uint32_t p = b->patch_count;
            append_rom_patch(b, AM_SKY, 0u, 0xe8374u + i * 96u, 96u, 0);
            append_rom_patch(b, AM_ROAD, 0u, 0xe9b74u + i * 96u, 64u, 0);
            append_rom_patch(b, AM_ROAD, 48u, 0xe9b74u + i * 96u + 64u, 32u, 0);
            append_frame(b, 4u, p);
        }
        append_lane(b, AM_PRE, AM_ENTRY, AM_SUNSET, 0u, first, 0u, 0u, 0u, NULL);
    }
    if (behavior == 17u)
    {
        const uint32_t first = b->frame_count;
        for (unsigned i = 0u; i < 41u; ++i)
        {
            const uint32_t p = b->patch_count;
            append_rom_patch(b, AM_ROAD, 0u,
                             i < 33u ? 0xe8322u + i * 2u : 0xe8362u + (i - 33u) * 2u, 2u, 0);
            append_frame(b, i < 33u ? 16u : 64u, p);
        }
        append_lane(b, AM_PRE, AM_ENTRY, AM_FINISH, 0u, first, 0u, 0u, 0u, NULL);
        append_table_lane(b, 0xe82f0u, 8u, 1u, AM_ROAD, 16u, 13u, AM_PRE, AM_EVERY, 0u, 0u);
        const uint32_t object_first = b->frame_count, params[4] = {1u, 0u, 0u, 0u};
        for (unsigned i = 0u; i < 3u; ++i)
        {
            const uint32_t p = b->patch_count;
            append_rom_patch(b, AM_OBJECT, 19u, 0x76b84u + i * 24u, 22u, 0);
            append_frame(b, 4u, p);
        }
        append_lane(b, AM_PRE, AM_ENTRY, AM_TIMER, 0u, object_first, 0u, 2u, 999u, params);
    }
    if (behavior == 10u)
        append_table_lane(b, 0xe8260u, 8u, 6u, AM_ROAD, 33u, 8u, AM_POST, AM_EVERY, 0u, 0u);
    if (behavior == 23u)
    {
        const uint32_t first = b->frame_count;
        for (unsigned i = 0u; i < 18u; ++i)
        {
            const uint32_t p = b->patch_count;
            const uint32_t cosine_index = (256u - i * 32u) & 1023u;
            const int32_t cosine = ri_get_i32(ctx, ctx->rom, 0xe645cu + cosine_index * 4u);
            const int32_t weight = (32768 - cosine) / 2;
            for (unsigned bank = 0u; bank < 2u; ++bank)
            {
                uint8_t colors[26];
                for (unsigned c = 0u; c < 13u; ++c)
                {
                    const uint32_t a = ri_get_u16(ctx, ctx->rom, 0x713bcu + bank * 32u + c * 2u);
                    const uint32_t z = ri_get_u16(ctx, ctx->rom, 0x713fcu + bank * 32u + c * 2u);
                    uint32_t value = 0u;
                    for (unsigned shift = 0u; shift < 15u; shift += 5u)
                    {
                        const int32_t x = (int32_t)((a >> shift) & 31u),
                                      y = (int32_t)((z >> shift) & 31u);
                        const int64_t product = (int64_t)(y - x) * weight;
                        const int32_t floor_product = product < 0
                                                          ? -(int32_t)((-product + 32767) / 32768)
                                                          : (int32_t)(product / 32768);
                        const int32_t component = i < 16u    ? x + floor_product
                                                  : i == 16u ? (x + y) / 2
                                                             : y;
                        value |= ((uint32_t)component & 31u) << shift;
                    }
                    colors[c * 2u] = (uint8_t)value;
                    colors[c * 2u + 1u] = (uint8_t)(value >> 8u);
                }
                append_patch(b, AM_SKY, 16u + bank * 16u, (RiData){colors, sizeof colors}, 0);
            }
            append_frame(b, 1u, p);
        }
        append_lane(b, AM_PRE, AM_ENTRY, AM_RAINBOW, 0u, first, 0u, 0u, 0u, NULL);
    }
}

RiBuf *ri_ambient_payload(RiCtx *ctx, uint32_t behavior, uint32_t animation, uint32_t material,
                          RiData object_palette)
{
    (void)material;
    if (object_palette.size != 96u)
        ri_fail(ctx, "ambient course palette must contain 48 colors");
    AmbientBuilder b = {.ctx = ctx,
                        .data = ri_buf(ctx),
                        .lanes = ri_buf(ctx),
                        .frames = ri_buf(ctx),
                        .patches = ri_buf(ctx),
                        .initial = ri_buf(ctx)};
    ri_put(ctx, b.data, object_palette.data, object_palette.size);
    ri_put(ctx, b.data, ri_at(ctx, ctx->rom, 0x66e4cu, 160u), 160u);
    size_t initial = animation == 2u                                             ? 0x66eecu
                     : (animation == 3u || animation == 11u || animation == 35u) ? 0x66ef4u
                     : animation == 6u                                           ? 0x66efcu
                     : animation == 13u                                          ? 0x66f04u
                                                                                 : 0u;
    if (initial != 0u)
        append_rom_patch(&b, AM_OBJECT, 103u, initial, 8u, 1);
    if (animation == 14u)
    {
        uint8_t colors[6];
        memcpy(colors, b.data->data + 246u, 6u);
        append_patch(&b, AM_OBJECT, 91u, (RiData){colors, sizeof colors}, 1);
    }
    static const struct
    {
        uint32_t animation, count, roots[4];
    } registrations[] = {{2, 2, {0xf128c, 0xf136c}},
                         {5, 2, {0xf128c, 0xf136c}},
                         {3, 1, {0xf212c}},
                         {35, 1, {0xf212c}},
                         {9, 2, {0xf171c, 0xf176c}},
                         {10, 1, {0xf160c}},
                         {13, 2, {0xf144c, 0xf152c}},
                         {16, 4, {0xf17d4, 0xf188c, 0xf1984, 0xf1a34}},
                         {17, 1, {0xf256c}},
                         {19, 2, {0xf27b4, 0xf28a0}},
                         {21, 3, {0xf256c, 0xf2664, 0xf26fc}},
                         {23, 3, {0xf21d0, 0xf2230, 0xf2298}},
                         {33, 1, {0xf20b4}},
                         {42, 2, {0xf2308, 0xf23e8}},
                         {44, 1, {0xf24ec}},
                         {49, 2, {0xf2664, 0xf26fc}},
                         {51, 1, {0xf27b4}}};
    /* Source registrations insert at the head, so callbacks run in reverse order. */
    import_palette_script(&b, 0xf1b38u, 0u);
    for (size_t i = 0u; i < sizeof registrations / sizeof registrations[0]; ++i)
        if (registrations[i].animation == animation)
            for (uint32_t root = registrations[i].count; root > 0u; --root)
                import_palette_script(&b, registrations[i].roots[root - 1u], 0u);
    import_background_lanes(&b, behavior);
    if (behavior == 13u)
    {
        const uint32_t first = b.frame_count, start = b.lane_count + 1u,
                       params[4] = {start, start + 1u, start + 2u, start + 3u};
        append_frame(&b, 1u, b.patch_count);
        append_lane(&b, AM_POST, AM_ENTRY, AM_STORM, 0u, first, 0u, 0u, 0u, params);
        static const size_t roots[4] = {0xf1b9cu, 0xf1c30u, 0xf1ce0u, 0xf1e34u};
        for (unsigned i = 0u; i < 4u; ++i)
            import_palette_script(&b, roots[i], 1u);
    }
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 8, 0x31424d41u, 1u, b.lane_count, b.frame_count, b.patch_count,
             b.initial_count, (uint32_t)b.data->size, 0u);
    ri_put_buf(ctx, out, b.lanes);
    ri_put_buf(ctx, out, b.frames);
    ri_put_buf(ctx, out, b.patches);
    ri_put_buf(ctx, out, b.initial);
    ri_put_buf(ctx, out, b.data);
    return out;
}
