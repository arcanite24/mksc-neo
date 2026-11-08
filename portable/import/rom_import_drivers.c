#include "rom_import_internal.h"

/* Port of tools/portable/build_driver_assets.py. */

enum {
    BLOCK_BYTES = 0x16000,
    FRAME_BYTES = 0x800,
    FULL_FRAME_COUNT = 34,
    VIEW_SLOT_COUNT = 64,
    LOD_FRAME_COUNT = 25,
    LOD32_FRAME_BYTES = 0x200,
    LOD16_FRAME_BYTES = 0x80,
    LOD32_OFFSET = 0xE800 + 20 * LOD32_FRAME_BYTES,
    LOD16_OFFSET = 0x14800,
    PALETTE_BYTES = 0x200,
    SINE_OFFSET = 0x0E645C,
    AUTO_OFFSET = 0x0EBEC8,
    BRAKE_OFFSET = 0x0EBEA8,
    BRAKE_ACCELERATE_OFFSET = 0x0EBEB8,
    FORWARD_RESPONSE_OFFSET = 0x0EC1D8,
    LOCAL_OFFSET = 0x0EBF78,
    STEERING_DELTA_OFFSET = 0x0EC71C,
    STEERING_RESPONSE_OFFSET = 0x0EC0D8,
    TRACK_ANGLE_OFFSET = 0x0DE3FC,
    TRACK_ANGLE_BYTES = 0x8000,
    ANGLE_TARGET_OFFSET = 0x0EC300,
    ANGLE_TARGET_BYTES = 0x1A0,
    VIEW_SLOT_OFFSET = 0x0ECD5C,
    TILES_START = 0x345800,
    PALETTES_START = 0x344600,
};

/* `_080D9790`: gameplay order (Mario..Bowser) to characterTiles.bin order. */
static const uint32_t graphics_source_ids[8] = {0, 1, 3, 6, 7, 4, 5, 2};

static int32_t floor_half(int32_t value)
{
    return value >= 0 ? value / 2 : -((-value + 1) / 2);
}

static void put_i16_checked(RiCtx *ctx, RiBuf *out, int32_t value)
{
    if (value < -32768 || value > 32767) ri_fail(ctx, "driver motion value outside int16");
    ri_i16(ctx, out, value);
}

static void put_rom(RiCtx *ctx, RiBuf *out, size_t offset, size_t size)
{
    ri_put(ctx, out, ri_at(ctx, ctx->rom, offset, size), size);
}

/* struct.pack('<i16h', (base + engine) * 65536, *limits) */
static void put_stats(RiCtx *ctx, RiBuf *out, int32_t base, int32_t engine, uint32_t source_id,
                      int32_t adjustment)
{
    int64_t speed = ((int64_t)base + engine) * 65536;
    if (speed < INT32_MIN || speed > INT32_MAX) ri_fail(ctx, "driver top speed outside int32");
    ri_i32(ctx, out, (int32_t)speed);
    for (int index = 0; index < 10; ++index)
        ri_i16(ctx, out, -1);
    for (int index = 0; index < 6; ++index)
        put_i16_checked(ctx, out, ri_get_i16(ctx, ctx->rom, 0xEC078 + source_id * 12 + index * 2) * 16 + adjustment);
}

static RiBuf *motion_payload(RiCtx *ctx, uint32_t source_id)
{
    static const uint32_t counts[8] = {1024, 64, 8, 8, 8, 256, 10, 16};
    RiData rom = ctx->rom;
    RiBuf *out = ri_buf(ctx);
    ri_u32(ctx, out, 0x31544F4D);
    for (int index = 0; index < 8; ++index)
        ri_u32(ctx, out, counts[index]);
    put_rom(ctx, out, SINE_OFFSET, 1024 * 4);
    put_rom(ctx, out, AUTO_OFFSET, 64 * 2);
    put_rom(ctx, out, BRAKE_OFFSET, 8 * 2);
    put_rom(ctx, out, BRAKE_ACCELERATE_OFFSET, 8 * 2);
    put_rom(ctx, out, FORWARD_RESPONSE_OFFSET, 8 * 2);
    /* sub_803C1BC: unscaled rows for GP selector 0/1, boosted otherwise. */
    for (int profile = 0; profile < 2; ++profile)
        for (uint32_t driver_class = 0; driver_class < 8; ++driver_class)
            for (uint32_t index = 0; index < 16; ++index) {
                int32_t value = ri_get_i16(ctx, rom, LOCAL_OFFSET + driver_class * 0x20 + index * 2) * 16;
                if (profile) {
                    value += floor_half(value);
                    if (value <= 0x31) value = 0xE0;
                }
                ri_u16(ctx, out, (uint32_t)value & 0xFFFF);
            }
    put_rom(ctx, out, STEERING_DELTA_OFFSET, 10 * 2);
    put_rom(ctx, out, STEERING_RESPONSE_OFFSET + source_id * 0x20, 16 * 2);
    put_rom(ctx, out, TRACK_ANGLE_OFFSET, TRACK_ANGLE_BYTES);
    put_rom(ctx, out, ANGLE_TARGET_OFFSET, ANGLE_TARGET_BYTES);
    /* C1BC: Time Trial uses engine-class row 2 and the boosted local profile. */
    int32_t base = ri_get_i32(ctx, rom, 0xEBF58 + source_id * 4);
    put_stats(ctx, out, base, ri_get_i16(ctx, rom, 0xEC7C0 + 2 * 2), source_id, 0xA0);
    put_rom(ctx, out, 0xEBF48, 16);
    static const int32_t adjustments[3] = {-128, 0, 160};
    for (uint32_t engine_class = 0; engine_class < 3; ++engine_class) {
        /* BB48 permits a four-bit row index: keep the whole addressable window. */
        put_rom(ctx, out, ANGLE_TARGET_OFFSET + engine_class * 328, ANGLE_TARGET_BYTES);
        put_stats(ctx, out, base, ri_get_i16(ctx, rom, 0xEC7C0 + engine_class * 2), source_id,
                  adjustments[engine_class]);
    }
    for(unsigned i=0u;i<8u;++i)
        if(ri_get_i16(ctx,rom,0xEC7B0+i*2u)>0)
            ri_fail(ctx,"finished-driver deceleration must be nonpositive");
    put_rom(ctx,out,0xEC7B0,16); /* BD24 finished-driver deceleration. */
    for(unsigned i=0u;i<64u;++i){
        const int32_t value=ri_get_i16(ctx,rom,0xEBE28u+i*2u);
        if(i%8u==0u?value>0:value<0)ri_fail(ctx,"invalid driver response coefficient");
    }
    for(unsigned i=0u;i<16u;++i){
        const int32_t selector=ri_get_i32(ctx,rom,0xEC1E8u+i*4u);
        if(selector<0||selector>7)ri_fail(ctx,"invalid driver response selector");
        for(unsigned c=0u;c<8u;++c){
            const int32_t offset=ri_get_i32(ctx,rom,0xEC228u+c*4u);
            if(offset< -7||offset>7||selector+offset>7)ri_fail(ctx,"invalid driver response class offset");
        }
    }
    put_rom(ctx,out,0xEBE28,128); /* 8E34 response matrix, signed halfwords. */
    put_rom(ctx,out,0xEC1E8,64); /* 7B30 selector rows, signed words. */
    put_rom(ctx,out,0xEC228,32); /* 7B30 per-character offsets, signed words. */
    for(unsigned row=0u;row<8u;++row){
        unsigned seen=0u;
        for(unsigned i=0u;i<8u;++i){
            const uint32_t value=ri_get_u32(ctx,rom,0xEDC34u+(row*8u+i)*4u);
            if(value>7u || (seen&(1u<<value)))ri_fail(ctx,"invalid rival class row");
            seen|=1u<<value;ri_u8(ctx,out,(uint8_t)value);
        }
    }
    for (unsigned i = 0u; i < 8u; ++i)
        ri_u16(ctx, out, (uint16_t)ri_get_u32(ctx, rom, 0xEC6DCu + i * 4u));
    put_rom(ctx,out,0xEC248,32); /* BD24 signed contact-recovery acceleration. */
    put_rom(ctx,out,0xECCA8,60); /* Rank pacing progress, route and last-place windows. */
    put_rom(ctx,out,0xF2C14,16); /* Contact-art frame/duration script and reset pair. */
    return out;
}

static RiBuf *battle_bomb_graphics(RiCtx *ctx)
{
    /* 435C8/43708: class8 bank, sixteen walking frames and both LODs. */
    if (ctx->rom.size < 0x400000) ri_fail(ctx, "truncated Battle bomb graphics");
    const uint8_t *slots = ri_at(ctx, ctx->rom, 0xECD9C, 64);
    for (int index = 0; index < 64; ++index)
        if (slots[index] > 14 || slots[index] % 2) ri_fail(ctx, "invalid Battle bomb view table");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 8, 0x33524143u, 16u * FRAME_BYTES, 256u, 64u, 64u, 8u, 16u, 64u);
    ri_put(ctx, out, slots, 64);
    put_rom(ctx, out, 0x3F5800, 0x400000 - 0x3F5800);
    put_rom(ctx, out, 0x345600, 0x200);
    return out;
}

void ri_group_drivers(RiCtx *ctx)
{
    for (uint32_t roster = 0; roster < 8; ++roster) {
        uint32_t content_id = roster + 1, source = graphics_source_ids[roster];
        size_t tiles = TILES_START + (size_t)source * BLOCK_BYTES;
        RiBuf *graphic = ri_buf(ctx);
        ri_words(ctx, graphic, 8, 0x33524143u, (uint32_t)(FULL_FRAME_COUNT * FRAME_BYTES), 256u, 64u,
                 64u, source, (uint32_t)FULL_FRAME_COUNT, (uint32_t)VIEW_SLOT_COUNT);
        put_rom(ctx, graphic, VIEW_SLOT_OFFSET, VIEW_SLOT_COUNT);
        put_rom(ctx, graphic, tiles, FULL_FRAME_COUNT * FRAME_BYTES);
        put_rom(ctx, graphic, tiles + LOD32_OFFSET, LOD_FRAME_COUNT * LOD32_FRAME_BYTES);
        put_rom(ctx, graphic, tiles + LOD16_OFFSET, LOD_FRAME_COUNT * LOD16_FRAME_BYTES);
        put_rom(ctx, graphic, PALETTES_START + (size_t)source * PALETTE_BYTES, PALETTE_BYTES);
        if (tiles + BLOCK_BYTES > 0x3FFF00) ri_fail(ctx, "character source assets are truncated");
        RiBuf *manifest = ri_buf(ctx);
        ri_words(ctx, manifest, 6, 0x31524843u, content_id, 1u, content_id, content_id, source);
        ri_section(ctx, 2, content_id, 1, manifest);
        ri_section(ctx, 10, content_id, 3, graphic);
        ri_section(ctx, 13, content_id, 8, motion_payload(ctx, source));
    }
    ri_section(ctx, 10, 9, 4, battle_bomb_graphics(ctx));
}
