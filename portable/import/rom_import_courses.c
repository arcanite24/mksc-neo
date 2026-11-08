#include "rom_import_courses.h"
#include "rom_import_frontend.h"

#include <string.h>

/* The course group of tools/portable/import_rom.py sections(): shared race art
 * (course_rom.shared_sections), the GBA/Extra/Battle course loops with their
 * catalogs, grids (rom_course_art.rom_grid) and standings
 * (build_gp_standings_pack.payload), and the item/scoring rule sections of
 * course_rom.py. Per-course sections live in rom_import_course_race.c. */

static void put_rom(RiCtx *ctx, RiBuf *out, size_t offset, size_t size)
{
    ri_put(ctx, out, ri_at(ctx, ctx->rom, offset, size), size);
}

static uint32_t rom_u16(RiCtx *ctx, size_t offset)
{
    return ri_get_u16(ctx, ctx->rom, offset);
}

static uint32_t rom_u32(RiCtx *ctx, size_t offset)
{
    return ri_get_u32(ctx, ctx->rom, offset);
}

/* ---- shared_sections ------------------------------------------------------ */

/* Matched BC18/BC9E/BCD0/BD3A/BE08 camera constants and A90C donor schedules. */
static RiBuf *finish_payload(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x314e4946u, 8u);
    ri_u16(ctx, out, 1024u);
    ri_u16(ctx, out, 32768u);
    ri_u16(ctx, out, 80u);
    ri_u16(ctx, out, 220u);
    ri_u8(ctx, out, 3u);
    ri_u8(ctx, out, 0u);
    ri_u16(ctx, out, 0u);
    for (unsigned driver = 0u; driver < 8u; ++driver)
    {
        const size_t address = ri_pointer(ctx, ctx->rom, 0xf2bf4u + driver * 4u);
        uint8_t source[32][2], mapping[32], indices[32];
        unsigned source_count = 0u, count = 0u;
        memset(mapping, 255, sizeof mapping);
        for (unsigned index = 0u; index < 33u; ++index)
        {
            const uint8_t *row = ri_at(ctx, ctx->rom, address + index * 4u, 4u);
            if (row[2] != 0u || row[3] != 0u)
                ri_fail(ctx, "invalid finish schedule padding");
            if (row[0] == 0u && row[1] == 0u)
                break;
            if (index == 32u)
                ri_fail(ctx, "finish schedule exceeds row budget");
            if (row[0] != 255u)
            {
                if (row[0] > 33u || row[1] == 0u || row[1] > 127u)
                    ri_fail(ctx, "invalid finish schedule row");
                mapping[index] = (uint8_t)count;
                indices[count++] = (uint8_t)index;
            }
            source[index][0] = row[0];
            source[index][1] = row[1];
            ++source_count;
        }
        if (count == 0u)
            ri_fail(ctx, "empty finish schedule");
        ri_u32(ctx, out, count);
        for (unsigned row = 0u; row < count; ++row)
        {
            const unsigned index = indices[row];
            int cursor = (int)index + 1;
            uint32_t seen = 0u;
            while (cursor < (int)source_count)
            {
                if (cursor < 0 || (seen & (UINT32_C(1) << (unsigned)cursor)) != 0u)
                    ri_fail(ctx, "invalid finish schedule jump");
                seen |= UINT32_C(1) << (unsigned)cursor;
                if (source[cursor][0] != 255u)
                    break;
                const unsigned raw = source[cursor][1];
                cursor += raw < 128u ? (int)raw : (int)raw - 256;
            }
            if (cursor < 0 || cursor > (int)source_count)
                ri_fail(ctx, "invalid finish schedule jump");
            ri_u8(ctx, out, source[index][0]);
            ri_u8(ctx, out, source[index][1]);
            ri_u8(ctx, out, cursor == (int)source_count ? 255u : mapping[cursor]);
            ri_u8(ctx, out, 0u);
        }
    }
    return out;
}

/* CA78 response and speed steering limits. */
static RiBuf *driver_view_payload(RiCtx *ctx)
{
    const uint8_t *limits = ri_at(ctx, ctx->rom, 0xec846u, 30u);
    for (unsigned index = 0u; index < 30u; ++index)
        if (limits[index] > 18u)
            ri_fail(ctx, "invalid driver view limit");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31525644u, 15u);
    ri_put(ctx, out, limits, 30u);
    return out;
}

/* CBE4 bob spans and CD50/CC80 surface refresh policy. */
static RiBuf *driver_art_payload(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31524144u, 16u);
    for (unsigned index = 0u; index < 16u; ++index)
    {
        const uint32_t span = rom_u16(ctx, 0xEC826u + index * 2u);
        if (span > 2047u)
            ri_fail(ctx, "invalid driver art speed span");
        ri_u16(ctx, out, span);
    }
    for (unsigned index = 0u; index < 16u; ++index)
    {
        const uint32_t mask = rom_u32(ctx, 0xEC2C0u + index * 4u);
        if (mask != 0u && mask != UINT32_MAX)
            ri_fail(ctx, "invalid driver art refresh mask");
        ri_u8(ctx, out, mask == 0u ? 7u : 3u);
    }
    return out;
}

/* CD50 thresholds and 4378C/4379C cell geometry, independent of hardware attrs. */
static RiBuf *driver_projection_payload(RiCtx *ctx)
{
    static const uint16_t distances[3] = {511u, 1023u, 4096u};
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31525044u, 3u);
    for (unsigned index = 0u; index < 3u; ++index)
    {
        const uint16_t attr = (uint16_t)rom_u16(ctx, 0xECDE0u + index * 2u);
        const uint16_t size = (uint16_t)(8u << (attr >> 14u));
        const int16_t x =
            (int16_t)((attr & 511u) >= 256u ? (int)(attr & 511u) - 512 : (int)(attr & 511u));
        const uint8_t y = *ri_at(ctx, ctx->rom, 0xECDDCu + index, 1u);
        if (size != (64u >> index) || (attr & 0x3e00u) != 0u)
            ri_fail(ctx, "invalid driver projection cell");
        ri_u16(ctx, out, distances[index]);
        ri_u16(ctx, out, size);
        ri_u16(ctx, out, (uint16_t)x);
        ri_u16(ctx, out, (uint16_t)(y >= 128u ? (int)y - 256 : (int)y));
        ri_u8(ctx, out, 1u);
        ri_u8(ctx, out, 0u);
        ri_u16(ctx, out, 0u);
    }
    return out;
}

/* Normalize the byte tables: modes5/6 use x raw and y<<7; the others double both axes. */
static RiBuf *driver_affine_payload(RiCtx *ctx)
{
    static const struct
    {
        uint32_t mode;
        size_t address;
        unsigned x_shift, y_shift;
    } tables[] = {{1u, 0xF2AC0, 1u, 1u},
                  {4u, 0xF2C54, 1u, 1u},
                  {5u, 0xF2C80, 0u, 7u},
                  {6u, 0xF2C86, 0u, 7u},
                  {7u, 0xF2C8C, 1u, 1u}};
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31524641u, 5u);
    for (unsigned table = 0u; table < 5u; ++table)
    {
        RiBuf *rows = ri_buf(ctx);
        uint32_t count = 0u;
        for (unsigned index = 0u; index < 33u; ++index)
        {
            const uint8_t *row = ri_at(ctx, ctx->rom, tables[table].address + index * 3u, 3u);
            if (row[2] == 0u)
                break;
            if (index == 32u || row[0] == 0u || row[1] == 0u || row[2] > 127u)
                ri_fail(ctx, "invalid driver affine table");
            ri_u16(ctx, rows, (uint16_t)(row[0] << tables[table].x_shift));
            ri_u16(ctx, rows, (uint16_t)(row[1] << tables[table].y_shift));
            ri_u8(ctx, rows, row[2]);
            ri_u8(ctx, rows, 0u);
            ++count;
        }
        if (count == 0u)
            ri_fail(ctx, "empty driver affine table");
        ri_words(ctx, out, 2, tables[table].mode, count);
        ri_put_buf(ctx, out, rows);
    }
    return out;
}

/* D950 freeze spans and C4D8 signed drip displacements. */
static RiBuf *recovery_fx_payload(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31584652u, 3u);
    for (unsigned i = 0u; i < 3u; ++i)
    {
        const uint32_t frame = ri_get_u8(ctx, ctx->rom, 0xF2C48u + i * 2u);
        const uint32_t duration = ri_get_u8(ctx, ctx->rom, 0xF2C49u + i * 2u);
        if (frame != 1u || !duration || duration > 127u)
            ri_fail(ctx, "invalid recovery freeze sequence");
        ri_u8(ctx, out, duration);
    }
    if (ri_get_u16(ctx, ctx->rom, 0xF2C4Eu))
        ri_fail(ctx, "invalid recovery freeze terminator");
    for (unsigned i = 0u; i < 9u; ++i)
    {
        const uint32_t raw = ri_get_u8(ctx, ctx->rom, 0xF31A0u + i);
        const int value = raw < 128u ? (int)raw : (int)raw - 256;
        if (value < -16 || value > 16)
            ri_fail(ctx, "invalid recovery drip displacement");
        ri_u8(ctx, out, raw);
    }
    ri_u32(ctx, out, 0u);
    return out;
}

/* E68C/E1F4/DF10: normalized material selection and frame cut immediates. */
static RiBuf *surface_trail_payload(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    uint8_t kinds[256], cuts[10] = {9u, 19u, 9u, 19u, 9u, 19u, 5u, 11u, 5u, 11u};
    memset(kinds, 1, sizeof kinds);
    kinds[0x54] = 2u;
    kinds[0x5a] = 3u;
    kinds[0x5c] = 4u;
    kinds[0x58] = 5u;
    ri_words(ctx, out, 2, 0x314c5254u, 256u);
    ri_put(ctx, out, kinds, sizeof kinds);
    ri_put(ctx, out, cuts, sizeof cuts);
    const uint8_t tail[6] = {0x50u, 0x50u, 0x5cu, 0u, 0u, 0u};
    ri_put(ctx, out, tail, sizeof tail);
    return out;
}
/* DF10 age-ordered cells; 49D78 spray is the fifth group. */
static RiBuf *surface_trail_art_payload(RiCtx *ctx, unsigned group)
{
    static const size_t groups[5][3] = {{0x7afcc, 0x7afd4, 0x7afdc},
                                        {0x7affc, 0x7b004, 0x7b00c},
                                        {0x7b014, 0x7b01c, 0x7b024},
                                        {0x7b044, 0x7b04c, 0x7b054},
                                        {0x7af14, 0, 0}};
    if (group >= 5u)
        ri_fail(ctx, "invalid surface trail art group");
    const unsigned count = group == 4u ? 1u : 3u;
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 8, 0x31435846u, 0x2000u, 80u, count, 0x100u, 3u, count * 8u, 0u);
    for (unsigned i = 0u; i < count; ++i)
    {
        if (rom_u16(ctx, groups[group][i]) != 1u)
            ri_fail(ctx, "invalid surface trail cell count");
        ri_u32(ctx, out, i * 8u);
        ri_u16(ctx, out, 8u);
        ri_u16(ctx, out, 1u);
    }
    put_rom(ctx, out, 0x66f0c, 0x2000);
    put_rom(ctx, out, 0x66e4c, 160);
    for (unsigned i = 0u; i < count; ++i)
        put_rom(ctx, out, groups[group][i], 8);
    return out;
}

/* race_particle_payload: flatten 5B44C source cells/duration+1 frames. */
static RiBuf *race_particle_payload(RiCtx *ctx, size_t sequence)
{
    RiBuf *frames = ri_buf(ctx), *cells = ri_buf(ctx);
    uint32_t frame_count = 0;
    for (size_t index = 0; index < 33; ++index)
    {
        uint32_t address = rom_u32(ctx, sequence + index * 8);
        uint32_t duration = rom_u32(ctx, sequence + index * 8 + 4);
        if (!address && !duration)
            break;
        if (index == 32 || !(duration > 0 && duration <= 255))
            ri_fail(ctx, "invalid race particle sequence");
        size_t offset = ri_pointer(ctx, ctx->rom, sequence + index * 8);
        size_t size = 2 + (size_t)rom_u16(ctx, offset) * 6;
        if (cells->size + size > 528)
            ri_fail(ctx, "race particle cells exceed capacity");
        ri_u32(ctx, frames, (uint32_t)cells->size);
        ri_u16(ctx, frames, (uint32_t)size);
        ri_u16(ctx, frames, duration);
        put_rom(ctx, cells, offset, size);
        frame_count++;
    }
    if (!frame_count)
        ri_fail(ctx, "empty race particle sequence");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 8, 0x31435846u, 0x2000u, 80u, frame_count, 0x100u, 3u, (uint32_t)cells->size,
             0u);
    ri_put_buf(ctx, out, frames);
    put_rom(ctx, out, 0x66F0C, 0x2000);
    put_rom(ctx, out, 0x66E4C, 160);
    ri_put_buf(ctx, out, cells);
    ri_buf_free(ctx, frames);
    ri_buf_free(ctx, cells);
    return out;
}

/* B680/B7D0 motion and bounce tables; B44C source duration+1 sums. */
static RiBuf *coin_effect_payload(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 4, 0x31584643u, 9u, 3u, 4u);
    for (unsigned table = 0u; table < 2u; ++table)
        for (unsigned i = 0u; i < 9u; ++i)
        {
            const uint32_t bits = rom_u32(ctx, (table ? 0xf2f50u : 0xf2f2cu) + i * 4u);
            const int64_t value =
                bits >= 0x80000000u ? (int64_t)bits - INT64_C(4294967296) : (int64_t)bits;
            if (table ? (value < 0 || value > 64 * 65536) : (value < -64 || value > 64))
                ri_fail(ctx, "invalid coin effect motion");
            ri_u32(ctx, out, bits);
        }
    for (unsigned i = 0u; i < 3u; ++i)
    {
        const uint32_t volume = rom_u16(ctx, 0xf2f74u + i * 2u);
        if (volume > 256u)
            ri_fail(ctx, "invalid coin effect volume");
        ri_u16(ctx, out, volume);
    }
    const size_t scripts[4] = {ri_pointer(ctx, ctx->rom, 0xf2f04),
                               ri_pointer(ctx, ctx->rom, 0xf2f08), 0xf2eb4, 0xf2ee4};
    for (unsigned slot = 1u; slot < 9u; ++slot)
        if (ri_pointer(ctx, ctx->rom, 0xf2f04u + slot * 4u) != scripts[1])
            ri_fail(ctx, "invalid coin effect variant scripts");
    for (unsigned script = 0u; script < 4u; ++script)
    {
        RiBuf *art = race_particle_payload(ctx, scripts[script]);
        const RiData view = ri_view(art);
        const unsigned count = ri_get_u32(ctx, view, 12u);
        unsigned ticks = 0u;
        for (unsigned i = 0u; i < count; ++i)
            ticks += ri_get_u16(ctx, view, 32u + i * 8u + 6u) + 1u;
        ri_u16(ctx, out, ticks);
        ri_buf_free(ctx, art);
    }
    ri_u16(ctx, out, 0u);
    return out;
}

static const size_t impact_sequences[6] = {0xf2cbc, 0xf2cec, 0xf2d1c, 0xf2d4c, 0xf2d7c, 0xf2dac};
/* 378FC/B554/B5B8/B61C paired forward/reverse scripts, source duration+1. */
static RiBuf *impact_effect_payload(RiCtx *ctx)
{
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31584649u, 6u);
    for (unsigned script = 0u; script < 6u; ++script)
    {
        RiBuf *art = race_particle_payload(ctx, impact_sequences[script]);
        const RiData view = ri_view(art);
        const unsigned count = ri_get_u32(ctx, view, 12u);
        unsigned ticks = 0u;
        for (unsigned i = 0u; i < count; ++i)
            ticks += ri_get_u16(ctx, view, 32u + i * 8u + 6u) + 1u;
        ri_u16(ctx, out, ticks);
        ri_buf_free(ctx, art);
    }
    return out;
}

/* battle_balloon_payload: BA58/BAD0's single cell. EC30 loads this Battle-only
 * block at source tile 8C; the balloon uses tile 9C. */
static RiBuf *battle_balloon_payload(RiCtx *ctx)
{
    RiBuf *block = ri_lz77(ctx, ctx->rom, 0x6D06C);
    if (block->size != 640)
        ri_fail(ctx, "invalid Battle balloon tile block");
    RiBuf *tiles = ri_buf_zero(ctx, 0x1180);
    ri_put_buf(ctx, tiles, block);
    RiBuf *palette = ri_buf_zero(ctx, 96);
    put_rom(ctx, palette, 0x66E4C, 160);
    RiBuf *out =
        ri_cell_image_indexed(ctx, ctx->rom, 0x7B534, ri_view(tiles), ri_view(palette), 0u);
    ri_buf_free(ctx, block);
    ri_buf_free(ctx, tiles);
    ri_buf_free(ctx, palette);
    return out;
}

/* shared_sections: ROM art and collision tables shared by every course;
 * returns the minimap payload every course embeds as kind 11. */
static RiBuf *shared_sections(RiCtx *ctx)
{
    static const uint32_t collision_spans[7][2] = {{0xEDA74, 32}, {0xEDA94, 32}, {0xEC770, 8},
                                                   {0xEC778, 8},  {0xEC780, 8},  {0xEC788, 8},
                                                   {0xEC248, 32}};
    static const uint32_t hud_spans[9][2] = {
        {0x62834, 0x1000}, {0x62774, 192}, {0xEE67E, 40},        {0xEE6A6, 60}, {0xEE7AC, 12},
        {0xEEF88, 4},      {0xEE7BC, 12},  {0x6F33C, 14 * 0x80}, {0xEE7CC, 32}};
    static const uint32_t effect_frames[5][3] = {
        {0x7AE50, 8, 2}, {0x7AEEC, 8, 3}, {0x7AE58, 8, 4}, {0x7AE60, 62, 4}, {0x7AEA0, 74, 2}};

    RiBuf *collision = ri_buf(ctx);
    ri_words(ctx, collision, 8, 0x314C4F43u, 8u, 8u, 4u, 4u, 4u, 4u, 8u);
    for (int index = 0; index < 7; ++index)
        put_rom(ctx, collision, collision_spans[index][0], collision_spans[index][1]);

    RiBuf *hud = ri_buf(ctx);
    ri_words(ctx, hud, 10, 0x32445548u, 0x1000u, 96u, 20u, 60u, 6u, 2u, 6u, 14u * 0x80u, 16u);
    ri_u32(ctx, hud, 17u);
    for (int index = 0; index < 9; ++index)
        put_rom(ctx, hud, hud_spans[index][0], hud_spans[index][1]);
    put_rom(ctx, hud, 0xEE65C, 34);

    RiBuf *minimap = ri_buf(ctx);
    ri_words(ctx, minimap, 6, 0x314E494Du, 30u, 6u, 5u, 0x240u, 0u);
    put_rom(ctx, minimap, 0xEE76C, 60);
    put_rom(ctx, minimap, 0x66C0C, 0x240);

    RiBuf *effect = ri_buf(ctx);
    ri_words(ctx, effect, 8, 0x31435846u, 0x2000u, 80u, 5u, 0x100u, 3u, 304u, 0u);
    uint32_t cursor = 0;
    for (int index = 0; index < 5; ++index)
    {
        ri_u32(ctx, effect, cursor);
        ri_u16(ctx, effect, effect_frames[index][1]);
        ri_u16(ctx, effect, effect_frames[index][2]);
        cursor += effect_frames[index][1];
    }
    put_rom(ctx, effect, 0x66F0C, 0x2000);
    put_rom(ctx, effect, 0x66E4C, 160);
    for (int index = 0; index < 5; ++index)
        put_rom(ctx, effect, effect_frames[index][0], effect_frames[index][1]);

    put_rom(ctx, effect, 0x7ABB4, 144);

    RiBuf *box = ri_buf(ctx);
    ri_words(ctx, box, 5, 0x31584249u, 8u * 0x200u, 16u, 8u, 32u);
    ri_u32(ctx, box, 32);
    for (size_t index = 0; index < 8; ++index)
        put_rom(ctx, box, ri_pointer(ctx, ctx->rom, 0xED9AC + index * 4), 0x200);
    put_rom(ctx, box, 0x6D71C, 32);

    RiBuf *markers = ri_buf(ctx);
    ri_words(ctx, markers, 4, 0x314B4D47u, 64u, 32u, 0x118u);
    put_rom(ctx, markers, 0x6720C, 64);
    put_rom(ctx, markers, 0x66E8C, 32);
    put_rom(ctx, markers, 0x66ECC, 32);

    ri_section(ctx, 12, 1, 2, hud);
    ri_section(ctx, 14, 1, 1, collision);
    ri_section(ctx, 19, 1, 2, effect);
    ri_section(ctx, 19, 2, 1, race_particle_payload(ctx, 0xF2F7C));
    ri_section(ctx, 19, 3, 1, race_particle_payload(ctx, ri_pointer(ctx, ctx->rom, 0xF2F04)));
    ri_section(ctx, 19, 4, 1, battle_balloon_payload(ctx));
    ri_section(ctx, 19, 5, 1, race_particle_payload(ctx, 0xF312C)); /* C19C lava smoke puff */
    ri_section(ctx, 19, 6, 1,
               race_particle_payload(ctx, 0xF3014)); /* C09C water/lava entry splash */
    ri_section(ctx, 19, 7, 1, race_particle_payload(ctx, 0xF3158)); /* C388 thaw */
    ri_section(ctx, 19, 8, 1, race_particle_payload(ctx, 0xF3180)); /* C448 water drip */
    ri_section(ctx, 88, 1, 1, recovery_fx_payload(ctx));
    for (unsigned group = 0u; group < 5u; ++group)
        ri_section(ctx, 19, 9u + group, 1, surface_trail_art_payload(ctx, group));
    ri_section(ctx, 89, 1, 1, surface_trail_payload(ctx));
    ri_section(ctx, 19, 14, 1, race_particle_payload(ctx, ri_pointer(ctx, ctx->rom, 0xf2f08)));
    ri_section(ctx, 19, 15, 1, race_particle_payload(ctx, 0xf2eb4));
    ri_section(ctx, 19, 16, 1, race_particle_payload(ctx, 0xf2ee4));
    ri_section(ctx, 90, 1, 1, coin_effect_payload(ctx));
    for (unsigned i = 0u; i < 6u; ++i)
        ri_section(ctx, 19, 17u + i, 1, race_particle_payload(ctx, impact_sequences[i]));
    ri_section(ctx, 91, 1, 1, impact_effect_payload(ctx));
    ri_section(ctx, 81, 1, 1, driver_affine_payload(ctx));
    ri_section(ctx, 82, 1, 1, driver_projection_payload(ctx));
    ri_section(ctx, 83, 1, 1, driver_art_payload(ctx));
    ri_section(ctx, 85, 1, 1, driver_view_payload(ctx));
    ri_section(ctx, 86, 1, 1, finish_payload(ctx));
    ri_section(ctx, 26, 1, 1, box);
    ri_section(ctx, 29, 1, 2, markers);
    return minimap;
}

/* ---- Item and scoring rules (Thumb immediates read from the source) -------- */

/* `immediate`/`mov`: an 8-bit Thumb immediate with the expected opcode/register. */
static uint32_t thumb_immediate(RiCtx *ctx, size_t offset, uint32_t opcode, uint32_t reg,
                                const char *what)
{
    uint32_t instruction = rom_u16(ctx, offset);
    if ((instruction & 0xFF00) != (opcode | reg << 8))
        ri_fail(ctx, "%s: unexpected Thumb immediate", what);
    return instruction & 255;
}

/* `shifted`: MOVS rN,#imm followed by LSLS rN,rN,#shift. */
static uint64_t thumb_shifted(RiCtx *ctx, size_t offset, uint32_t reg, const char *what)
{
    uint64_t value = thumb_immediate(ctx, offset, 0x2000, reg, what);
    uint32_t instruction = rom_u16(ctx, offset + 2);
    if ((instruction & 0xF83F) != (reg << 3 | reg))
        ri_fail(ctx, "%s: expected LSLS immediate", what);
    return value << (instruction >> 6 & 31);
}

/* (0x100000000 - word) & 0xffffffff: a negated source gravity constant. */
static uint64_t negated_word(RiCtx *ctx, size_t offset)
{
    return (0x100000000ull - rom_u32(ctx, offset)) & 0xFFFFFFFFull;
}

/* words(magic, count, *values) after the per-value range checks. */
static RiBuf *rule_words(RiCtx *ctx, uint32_t magic, const uint64_t *values,
                         const uint64_t *minimums, const uint64_t *limits, size_t count,
                         const char *what)
{
    RiBuf *out = ri_buf(ctx);
    for (size_t index = 0; index < count; ++index)
        if (values[index] < minimums[index] || values[index] > limits[index])
            ri_fail(ctx, "%s: source values out of range", what);
    ri_words(ctx, out, 2, magic, (uint32_t)count);
    for (size_t index = 0; index < count; ++index)
        rc_word(ctx, out, (int64_t)values[index]);
    return out;
}

/* shell_orbit_rules_section: 46DE4/46F5C shared triple-shell orbit. */
static void shell_orbit_rules(RiCtx *ctx)
{
    if ((rom_u16(ctx, 0x46DFE) & 0xFF00) != 0x2100 || (rom_u16(ctx, 0x46F66) & 0xFF00) != 0x2300 ||
        (rom_u16(ctx, 0x46E5E) & 0xFF00) != 0x2000)
        ri_fail(ctx, "shell orbit: expected MOVS immediates");
    uint32_t radius = rom_u16(ctx, 0x46DFE) & 255;
    if (radius != (rom_u16(ctx, 0x46F66) & 255))
        ri_fail(ctx, "shell orbit: inconsistent radius");
    if (rom_u16(ctx, 0x46DFA) != 0x0088 || rom_u16(ctx, 0x46DFC) != 0x1840 ||
        rom_u16(ctx, 0x46E02) != 0x0600 || rom_u16(ctx, 0x46E04) != 0x1400)
        ri_fail(ctx, "shell orbit: unexpected angle expression");
    uint64_t step = ((1ull << (rom_u16(ctx, 0x46DFA) >> 6 & 31)) + 1) << 8;
    uint32_t period = (rom_u16(ctx, 0x46E5E) & 255) + 1;
    if (radius < 1 || radius > 255 || step < 1 || step > 65535 || period < 1 || period > 65535)
        ri_fail(ctx, "shell orbit: out of range");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 5, 0x3142524Fu, 3u, radius, (uint32_t)step, period);
    ri_section(ctx, 67, 1, 1, out);
}

/* banana_rules_section: 7C2C/7AAC/7F1C/791C/7848 player/CPU throws and disposal. */
static void banana_rules(RiCtx *ctx)
{
    static const char what[] = "banana rules";
    if (rom_u16(ctx, 0x47C94) != 0x0100 || rom_u16(ctx, 0x47CAA) != 0x0100)
        ri_fail(ctx, "banana rules: unequal player throw shifts");
    uint64_t speed = 1ull << (15 + (rom_u16(ctx, 0x47C94) >> 6 & 31));
    if (rom_u16(ctx, 0x47D1E) != 0x1140 || rom_u16(ctx, 0x47D2E) != 0x1140)
        ri_fail(ctx, "banana rules: unequal CPU flight divisors");
    uint64_t cpu_ticks = 1ull << (rom_u16(ctx, 0x47D1E) >> 6 & 31);
    uint64_t velocity = thumb_shifted(ctx, 0x47CAE, 0, what);
    if (velocity != thumb_shifted(ctx, 0x47D32, 0, what))
        ri_fail(ctx, "banana rules: unequal throw arcs");
    uint64_t values[9] = {speed,
                          cpu_ticks,
                          velocity,
                          negated_word(ctx, 0x47BB0),
                          thumb_immediate(ctx, 0x47D44, 0x2000, 0, what),
                          thumb_immediate(ctx, 0x47C08, 0x2800, 0, what),
                          thumb_immediate(ctx, 0x48014, 0x2800, 0, what) + 1u,
                          thumb_shifted(ctx, 0x4795E, 1, what),
                          negated_word(ctx, 0x47880)};
    /* No range check in the reference beyond struct.pack('<I'). */
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x314E4142u, 9u);
    for (int index = 0; index < 9; ++index)
        rc_word(ctx, out, (int64_t)values[index]);
    ri_section(ctx, 65, 1, 1, out);
}

/* red_shell_rules_section: 4676C/4D4C/5C18/5A3C/5910 shared red/Spiny lifecycle. */
static void red_shell_rules(RiCtx *ctx)
{
    static const char what[] = "red/Spiny shell rules";
    uint64_t bonus = thumb_shifted(ctx, 0x46AA6, 2, what);
    uint64_t cap = thumb_immediate(ctx, 0x46AC0, 0x2000, 0, what);
    if (thumb_immediate(ctx, 0x46ABC, 0x2800, 0, what) + 1u != cap)
        ri_fail(ctx, "red shell rules: speed cap mismatch");
    uint64_t grace = thumb_immediate(ctx, 0x46788, 0x2000, 4, what);
    uint64_t route = thumb_immediate(ctx, 0x46802, 0x2000, 0, what);
    uint64_t release = thumb_immediate(ctx, 0x45344, 0x2800, 0, what);
    uint64_t hold = thumb_immediate(ctx, 0x4714E, 0x2800, 0, what) + 1u;
    uint64_t speeds[3] = {thumb_immediate(ctx, 0x44EF0, 0x2000, 2, what),
                          thumb_immediate(ctx, 0x44EC6, 0x2000, 2, what),
                          thumb_immediate(ctx, 0x44EF4, 0x2000, 2, what)};
    uint64_t acceleration = thumb_immediate(ctx, 0x44F02, 0x3000, 0, what);
    uint64_t deceleration = thumb_immediate(ctx, 0x44EFA, 0x3800, 0, what);
    uint64_t arm = thumb_immediate(ctx, 0x45DD4, 0x2800, 0, what);
    uint64_t lock = thumb_immediate(ctx, 0x459A0, 0x2800, 0, what);
    uint64_t lifetime = (uint64_t)(rom_u32(ctx, 0x45B00) >> 16) + 1;
    if ((uint64_t)rom_u32(ctx, 0x4649C) + 1 != lifetime)
        ri_fail(ctx, "red shell rules: trap deadlines differ");
    if (thumb_immediate(ctx, 0x45DC2, 0x2800, 0, what) != release)
        ri_fail(ctx, "red shell rules: release clocks differ");
    uint64_t velocity = thumb_shifted(ctx, 0x47890, 2, what);
    uint64_t gravity = negated_word(ctx, 0x47880);
    const uint64_t values[16] = {bonus,     cap,       grace,     route,        release,      hold,
                                 speeds[0], speeds[1], speeds[2], acceleration, deceleration, arm,
                                 lifetime,  lock,      velocity,  gravity};
    static const uint64_t minimums[16] = {0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    static const uint64_t limits[16] = {32767, 255, 65535, 255,   32767, 32767, 255,      255,
                                        255,   255, 255,   32767, 32767, 32767, 0xFFFFFF, 0xFFFFFF};
    ri_section(ctx, 64, 1, 1,
               rule_words(ctx, 0x31485352, values, minimums, limits, 16, "red shell rules"));
}

/* green_shell_rules_section: 4676C/6FF4/4BA0/7884/7848 release and lifecycle. */
static void green_shell_rules(RiCtx *ctx)
{
    static const char what[] = "green shell rules";
    uint64_t bonus = thumb_shifted(ctx, 0x46876, 2, what);
    uint64_t cap = thumb_immediate(ctx, 0x46890, 0x2000, 0, what);
    if (thumb_immediate(ctx, 0x4688C, 0x2800, 0, what) + 1u != cap)
        ri_fail(ctx, "green shell rules: speed cap mismatch");
    const uint64_t values[8] = {bonus,
                                cap,
                                thumb_immediate(ctx, 0x46788, 0x2000, 4, what),
                                thumb_immediate(ctx, 0x468E4, 0x2000, 0, what),
                                thumb_immediate(ctx, 0x44CDA, 0x2800, 0, what),
                                thumb_immediate(ctx, 0x4714E, 0x2800, 0, what) + 1u,
                                thumb_shifted(ctx, 0x47890, 2, what),
                                negated_word(ctx, 0x47880)};
    static const uint64_t minimums[8] = {0, 1, 0, 1, 1, 1, 1, 1};
    static const uint64_t limits[8] = {32767, 255, 65535, 7, 32767, 32767, 0xFFFFFF, 0xFFFFFF};
    ri_section(ctx, 63, 1, 1, rule_words(ctx, 0x31485347, values, minimums, limits, 8, what));
}

/* boo_rules_section: 48694/4886C immunity, target lock, orbit and repulsion. */
static void boo_rules(RiCtx *ctx)
{
    static const char what[] = "Boo rules";
    uint64_t invisible = thumb_shifted(ctx, 0x486B8, 0, what),
             lock = thumb_shifted(ctx, 0x48974, 0, what);
    int64_t orbit = thumb_immediate(ctx, 0x488E6, 0x2000, 0, what);
    /* Python: u16 != 0x2800 | orbit - 1 (orbit 0 gives -1, never equal). */
    if ((int64_t)rom_u16(ctx, 0x48B4E) != (0x2800 | (orbit - 1)))
        ri_fail(ctx, "Boo rules: unequal approach and departure");
    uint64_t step = thumb_shifted(ctx, 0x4893C, 2, what);
    if (step != thumb_shifted(ctx, 0x489EC, 3, what) ||
        step != thumb_shifted(ctx, 0x48AD6, 5, what))
        ri_fail(ctx, "Boo rules: unequal orbit steps");
    uint64_t bob = thumb_shifted(ctx, 0x48A56, 3, what),
             velocity = thumb_shifted(ctx, 0x48BC0, 0, what);
    const uint64_t values[7] = {invisible, lock,     (uint64_t)orbit,           step,
                                bob,       velocity, negated_word(ctx, 0x48C00)};
    static const uint64_t minimums[7] = {1, 1, 1, 1, 1, 1, 1};
    static const uint64_t limits[7] = {32767, 32767, 255, 32767, 32767, 0xFFFFFF, 0xFFFFFF};
    ri_section(ctx, 62, 1, 1, rule_words(ctx, 0x314F4F42, values, minimums, limits, 7, what));
}

/* item_status_rules_section: 485DC/483A0 Star protection and Lightning timers. */
static uint32_t item_status_doubled(RiCtx *ctx, size_t offset)
{
    uint32_t value = thumb_immediate(ctx, offset, 0x2000, 0, "item status rules");
    if (rom_u16(ctx, offset + 2) != 0x0040)
        ri_fail(ctx, "item status rules: expected LSLS r0,r0,1");
    return value * 2;
}

static void item_status_rules(RiCtx *ctx)
{
    uint32_t player = item_status_doubled(ctx, 0x48612);
    uint32_t cpu = thumb_immediate(ctx, 0x4860C, 0x2000, 1, "item status rules");
    uint32_t lightning = item_status_doubled(ctx, 0x483E4);
    if (lightning != item_status_doubled(ctx, 0x4847C) || player < 1 || player > 32767 || cpu < 1 ||
        cpu > 32767 || lightning < 48 || lightning > 32767)
        ri_fail(ctx, "item status rules: invalid or unequal source duration");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 5, 0x31544153u, 3u, player, cpu, lightning);
    ri_section(ctx, 61, 1, 1, out);
}

/* mushroom_rules_section: A9AC/48138/482B8/8298 duration, turns and mode-8 drive. */
static void mushroom_rules(RiCtx *ctx)
{
    static const char what[] = "mushroom rules";
    uint64_t boost = thumb_immediate(ctx, 0x3AABE, 0x2000, 0, what);
    uint32_t single = rom_u16(ctx, 0x4814E);
    if ((single & 0xFF00) != 0x2800)
        ri_fail(ctx, "mushroom rules: expected actor counter comparison");
    single = (single & 255) + 1;
    uint64_t golden = thumb_shifted(ctx, 0x482CA, 1, what);
    uint64_t acceleration = thumb_shifted(ctx, 0x38510, 0, what);
    uint64_t speed = (uint64_t)rom_u32(ctx, 0x3851C) + 1;
    if (boost < 1 || boost > 65535 || golden < 1 || golden > 65535 || (acceleration & 65535) ||
        (acceleration >> 16) < 1 || (acceleration >> 16) > 32767 || speed < 1 || speed > 0x07E00000)
        ri_fail(ctx, "mushroom rules: source values out of range");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 7, 0x3153554Du, 5u, (uint32_t)boost, single, (uint32_t)golden,
             (uint32_t)(acceleration >> 16), (uint32_t)speed);
    ri_section(ctx, 60, 1, 1, out);
}

/* gp_scoring_section: 1E7C8 awards DB0A8[rank+1]; 2E98 initial retry credits. */
static void gp_scoring(RiCtx *ctx)
{
    const uint8_t *awards = ri_at(ctx, ctx->rom, 0xDB0A9, 8);
    for (int index = 0; index < 8; ++index)
        if (awards[index] > 63)
            ri_fail(ctx, "GP finish award exceeds four-race byte score budget");
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31545047u, 8u);
    ri_put(ctx, out, awards, 8);
    ri_u32(ctx, out, 3);
    ri_put(ctx, out, ri_at(ctx, ctx->rom, 0xECD00, 92), 92);
    ri_section(ctx, 59, 1, 3, out);
}

/* gp_player_item_sections: 43BB0 leader A/B, then nine leader-progress gap tables. */
static void gp_player_items(RiCtx *ctx)
{
    static const uint32_t offsets[11] = {0xECE1C, 0xECE50, 0xECE84, 0xECEB0, 0xECEE4, 0xECF10,
                                         0xECF4C, 0xECF88, 0xECFC4, 0xECFF8, 0xED02C};
    for (uint32_t identity = 1; identity <= 11; ++identity)
    {
        size_t offset = offsets[identity - 1];
        RiBuf *out = ri_buf(ctx);
        ri_words(ctx, out, 2, 0x31575047u, 16u);
        uint32_t total = 0;
        int terminated = 0;
        for (size_t index = 0; index < 16; ++index)
        {
            uint32_t item = rom_u16(ctx, offset + index * 4),
                     weight = rom_u16(ctx, offset + index * 4 + 2);
            ri_u16(ctx, out, item);
            ri_u16(ctx, out, weight);
            if (!item)
            {
                if (weight != 255 || !(total > 0 && total <= 32767))
                    ri_fail(ctx, "invalid GP player roulette terminator/total");
                ri_put_zero(ctx, out, (15 - index) * 4);
                terminated = 1;
                break;
            }
            if (!(item >= 0x1000 && item <= 0x100C) || !weight)
                ri_fail(ctx, "invalid GP player roulette item/weight");
            total += weight;
        }
        if (!terminated)
            ri_fail(ctx, "unterminated GP player roulette table");
        ri_section(ctx, 58, identity, 1, out);
    }
    RiBuf *roulette = ri_buf(ctx);
    ri_words(ctx, roulette, 2, 0x31544c52u, 10u);
    /* ECDF0 stores item/weight pairs; the preview owner reads the item. */
    for (unsigned i = 0u; i < 10u; ++i)
        ri_u16(ctx, roulette, rom_u16(ctx, 0xECDF0u + i * 4u));
    for (unsigned i = 0u; i < 27u; ++i)
    {
        const uint32_t duration = rom_u32(ctx, 0xEDA04u + i * 4u);
        if (duration == 0u || duration > 32767u)
            ri_fail(ctx, "invalid roulette duration");
        ri_u16(ctx, roulette, duration);
    }
    if (rom_u32(ctx, 0xEDA04u + 27u * 4u) != 0u)
        ri_fail(ctx, "invalid roulette terminator");
    static const uint16_t bounds[8] = {2u, 6u, 12u, 18u, 24u, 36u, 50u, 60u};
    for (unsigned i = 0u; i < 8u; ++i)
        ri_u16(ctx, roulette, bounds[i]);
    ri_put(ctx, roulette, ri_at(ctx, ctx->rom, 0xEC8B4u, 64u), 64u);
    uint8_t visible = 1u, frame = 0u;
    unsigned slots = 0u;
    for (unsigned offset = 0u; offset < 288u; offset += 8u)
    {
        const uint32_t command = rom_u32(ctx, 0xEDB14u + offset);
        const uint32_t value = rom_u32(ctx, 0xEDB18u + offset);
        if (command == 0u)
            break;
        if (command == 0x11u)
            visible = (uint8_t)(value != 0u);
        else if (command == 0xf4u)
        {
            switch (value)
            {
            case 0x08048545u:
                frame = 0u;
                break;
            case 0x08048565u:
                frame = 1u;
                break;
            case 0x0804858du:
                frame = 2u;
                break;
            case 0x080485b5u:
                frame = 3u;
                break;
            default:
                break;
            }
        }
        else if (command == 0xf1u)
        {
            if (value != 4u)
                ri_fail(ctx, "invalid lightning frame duration");
            const uint8_t cell = visible ? frame : 255u;
            ri_put(ctx, roulette, &cell, 1u);
            ++slots;
        }
        else if (command != 0x43u)
            ri_fail(ctx, "unknown lightning command");
    }
    if (slots != 14u)
        ri_fail(ctx, "invalid lightning frame count");
    ri_section(ctx, 58u, 12u, 1u, roulette);
}

/* ---- build_gp_standings_pack.payload ----------------------------------- */

static RiBuf *standings(RiCtx *ctx, uint32_t course_id, uint32_t source_header, uint32_t track_set)
{
    enum
    {
        TILE_BYTES = 0x3000,
        PALETTE_COUNT = 256,
        MAP_COUNT = 1024,
        DRIVER_COUNT = 8,
        DRIVER_CELLS = 18,
        FOOTER_CELLS = 9,
        CUPS_OFFSET = 0xE7464,
        TRACK_TABLE_OFFSET = 0xE7FEC,
        CUP_GRAPHICS_TABLE = 0xDB254,
        DRIVER_PATTERN_TABLE = 0xDB3DC,
        DRIVER_DISPLAY_TABLE = 0xD97B0,
    };
    /* 1DAA4 uploads the portrait bank last, replacing name-bank tiles A0..A3
     * with the shared portrait border. Preserve that overlapping copy order. */
    static const uint32_t static_tiles[4][3] = {{0x1400, 0xACAFC, 0x0C00},
                                                {0x2000, 0xABEFC, 0x0800},
                                                {0x0000, 0x9AA2C, 0x0400},
                                                {0x1000, 0x9AE2C, 0x0480}};
    RiData rom = ctx->rom;
    if (source_header >= (track_set == 2 ? 4u : 20u) || !course_id || track_set > 2)
        ri_fail(ctx, "unsupported course identity");
    if (rom.size < TRACK_TABLE_OFFSET + 24 * 4)
        ri_fail(ctx, "ROM is too small");
    static const uint32_t tables[3] = {CUPS_OFFSET, 0xE74C4, 0xE7524}; /* cups, Extra, Battle */
    uint32_t track_index = rom_u32(ctx, tables[track_set] + (size_t)source_header * 4);
    size_t track_def =
        ri_pointer(ctx, rom, rc_offset(ctx, TRACK_TABLE_OFFSET + (int64_t)track_index * 4));
    RiBuf *track_name = ri_lz77(ctx, rom, ri_pointer(ctx, rom, track_def + 0x30));
    uint32_t cup_index = source_header / 4;
    RiBuf *cup_name =
        ri_lz77(ctx, rom, ri_pointer(ctx, rom, CUP_GRAPHICS_TABLE + (size_t)cup_index * 4));
    if (track_name->size > 0x400 || cup_name->size > 0x800)
        ri_fail(ctx, "standings course/cup graphics exceed source slots");

    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 8, 0x31534750u, (uint32_t)TILE_BYTES, (uint32_t)PALETTE_COUNT,
             (uint32_t)MAP_COUNT, (uint32_t)DRIVER_COUNT, (uint32_t)DRIVER_CELLS, course_id,
             source_header);
    size_t tiles = out->size;
    ri_put_zero(ctx, out, TILE_BYTES);
    for (int index = 0; index < 4; ++index)
        memcpy(out->data + tiles + static_tiles[index][0],
               ri_at(ctx, rom, static_tiles[index][1], static_tiles[index][2]),
               static_tiles[index][2]);
    memcpy(out->data + tiles + 0x400, track_name->data, track_name->size);
    memcpy(out->data + tiles + 0x800, cup_name->data, cup_name->size);
    ri_buf_free(ctx, track_name);
    ri_buf_free(ctx, cup_name);

    size_t palette = out->size;
    ri_put_zero(ctx, out, PALETTE_COUNT * 2);
    memcpy(out->data + palette + 0x180, ri_at(ctx, rom, 0x9A9AC, 0x80), 0x80);
    memcpy(out->data + palette + 0x1C0, ri_at(ctx, rom, 0xAACF0, 0x40), 0x40);

    RiBuf *background = ri_lz77(ctx, rom, 0x9B2AC);
    if (background->size != MAP_COUNT * 2)
        ri_fail(ctx, "wrong standings background map size");
    for (uint32_t column = 0; column < 3; ++column)
    {
        uint32_t source_x = cup_index * 3 + column;
        static const uint32_t rows[2][2] = {{28, 1}, {29, 2}};
        for (int row = 0; row < 2; ++row)
        {
            size_t source = (rows[row][0] * 32 + source_x) * 2;
            size_t destination = (rows[row][1] * 32 + 15 + column) * 2;
            memmove(background->data + destination, background->data + source, 2);
        }
    }
    ri_put_buf(ctx, out, background);
    ri_buf_free(ctx, background);

    uint32_t display[DRIVER_COUNT];
    int seen[DRIVER_COUNT] = {0};
    for (size_t index = 0; index < DRIVER_COUNT; ++index)
    {
        display[index] = rom_u32(ctx, DRIVER_DISPLAY_TABLE + index * 4);
        if (display[index] >= DRIVER_COUNT || seen[display[index]])
            ri_fail(ctx, "driver display table is not a permutation");
        seen[display[index]] = 1;
    }
    for (int driver_class = 0; driver_class < DRIVER_COUNT; ++driver_class)
        for (uint32_t half = 0; half < 2; ++half)
        {
            size_t name =
                ri_pointer(ctx, rom, DRIVER_PATTERN_TABLE + (display[driver_class] * 2 + half) * 4);
            put_rom(ctx, out, name, (DRIVER_CELLS / 2) * 2);
        }
    put_rom(ctx, out, ri_pointer(ctx, rom, DRIVER_PATTERN_TABLE + DRIVER_COUNT * 2 * 4),
            FOOTER_CELLS * 2);
    return out;
}

/* ---- import_rom.sections: course group ------------------------------------ */

/* words(0x31475047, len(grid)) + rows: section 34. */
static void grid_section(RiCtx *ctx, uint32_t native_id, uint32_t header, uint32_t slots)
{
    RcGridRow rows[8];
    rc_rom_grid(ctx, header, slots, rows);
    RiBuf *out = ri_buf(ctx);
    ri_words(ctx, out, 2, 0x31475047u, slots);
    for (uint32_t index = 0; index < slots; ++index)
        ri_words(ctx, out, 3, rows[index].x, rows[index].y, rows[index].heading);
    ri_section(ctx, 34, native_id, 1, out);
}

static uint32_t course_song(RiCtx *ctx, size_t definition, const char *what)
{
    uint32_t song = rom_u32(ctx, definition + 0x18);
    if (!(song > 0 && song <= 0xFFFF))
        ri_fail(ctx, "%s music ID outside native song range", what);
    return song;
}

void ri_group_courses(RiCtx *ctx)
{
    RiBuf *minimap = shared_sections(ctx);

    /* GBA cups: twenty source headers 4..23, native IDs are directory IDs. */
    uint32_t headers[20];
    for (size_t index = 0; index < 20; ++index)
    {
        headers[index] = rom_u32(ctx, 0xE7464 + index * 4);
        if (headers[index] < 4 || headers[index] > 23)
            ri_fail(ctx, "invalid GBA cup course table");
        for (size_t other = 0; other < index; ++other)
            if (headers[other] == headers[index])
                ri_fail(ctx, "invalid GBA cup course table");
    }
    RiBuf *catalog = ri_buf(ctx);
    ri_words(ctx, catalog, 2, 0x31544143u, 20u);
    for (uint32_t index = 0; index < 20; ++index)
    {
        uint32_t header = headers[index];
        size_t definition = ri_pointer(ctx, ctx->rom, 0xE7FEC + (size_t)header * 4);
        uint32_t course_id = rom_u32(ctx, definition);
        uint32_t song = course_song(ctx, definition, "course");
        ri_words(ctx, catalog, 3, course_id, index / 4 | (index % 4) << 8 | song << 16, header);
        rc_course_sections(ctx, header, header != 4, -1, 0, minimap);
        grid_section(ctx, course_id, header, 8);
        ri_section(ctx, 23, course_id, 1, standings(ctx, course_id, index, 0));
    }
    ri_section(ctx, 37, 1, 2, catalog);

    /* track_getMKSCTrack's Extra table starts after the four unused slots.
     * Native IDs 64..83 reserve source directory entries 0..19. */
    uint32_t extra_headers[20];
    int extra_seen[20] = {0};
    for (size_t index = 0; index < 20; ++index)
    {
        extra_headers[index] = rom_u32(ctx, 0xE74C4 + index * 4);
        if (extra_headers[index] < 32 || extra_headers[index] > 51 ||
            extra_seen[extra_headers[index] - 32])
            ri_fail(ctx, "invalid Extra cup course table");
        extra_seen[extra_headers[index] - 32] = 1;
    }
    RiBuf *extra_catalog = ri_buf(ctx);
    ri_words(ctx, extra_catalog, 2, 0x31544143u, 20u);
    for (uint32_t index = 0; index < 20; ++index)
    {
        uint32_t header = extra_headers[index];
        size_t definition = ri_pointer(ctx, ctx->rom, 0xE7FEC + (size_t)header * 4);
        int64_t native_id = 64 + (int64_t)rom_u32(ctx, definition);
        uint32_t song = course_song(ctx, definition, "Extra course");
        rc_word(ctx, extra_catalog, native_id);
        ri_words(ctx, extra_catalog, 2, index / 4 | (index % 4) << 8 | song << 16, header);
        rc_course_sections(ctx, header, 1, native_id, 0, minimap);
        grid_section(ctx, (uint32_t)native_id, header, 8);
        ri_section(ctx, 23, (uint32_t)native_id, 1, standings(ctx, (uint32_t)native_id, index, 1));
    }
    ri_section(ctx, 37, 2, 2, extra_catalog);

    /* track_getBattleTrack: four arenas, native IDs 96..99, no lap gate. */
    uint32_t battle_headers[4];
    int battle_seen[4] = {0};
    for (size_t index = 0; index < 4; ++index)
    {
        battle_headers[index] = rom_u32(ctx, 0xE7524 + index * 4);
        if (battle_headers[index] < 24 || battle_headers[index] > 27 ||
            battle_seen[battle_headers[index] - 24])
            ri_fail(ctx, "invalid Battle course table");
        battle_seen[battle_headers[index] - 24] = 1;
    }
    RiBuf *battle_catalog = ri_buf(ctx);
    ri_words(ctx, battle_catalog, 2, 0x31544143u, 4u);
    for (uint32_t index = 0; index < 4; ++index)
    {
        uint32_t header = battle_headers[index];
        size_t definition = ri_pointer(ctx, ctx->rom, 0xE7FEC + (size_t)header * 4);
        int64_t native_id = 96 + (int64_t)rom_u32(ctx, definition) - 44;
        uint32_t song = rom_u32(ctx, definition + 0x18);
        if (native_id < 96 || native_id > 99 || !(song > 0 && song <= 0xFFFF))
            ri_fail(ctx, "Battle course directory or music ID outside range");
        ri_words(ctx, battle_catalog, 3, (uint32_t)native_id, index << 8 | song << 16, header);
        rc_course_sections(ctx, header, 1, native_id, 1, minimap);
        ri_section(ctx, 23, (uint32_t)native_id, 1, standings(ctx, (uint32_t)native_id, index, 2));
        grid_section(ctx, (uint32_t)native_id, header, 4);
    }
    ri_section(ctx, 37, 3, 2, battle_catalog);
    /* Source award stage; its ceremony asset references this non-catalog content. */
    rc_course_sections(ctx, 28u, 1, 100, 0, minimap);
    grid_section(ctx, 100u, 28u, 8u);
    ri_buf_free(ctx, minimap);

    gp_player_items(ctx);
    gp_scoring(ctx);
    mushroom_rules(ctx);
    item_status_rules(ctx);
    boo_rules(ctx);
    green_shell_rules(ctx);
    red_shell_rules(ctx);
    banana_rules(ctx);
    shell_orbit_rules(ctx);
    /* Native sixteen signed x/y bob pairs used by the recovery actor. */
    RiBuf *bob = ri_buf(ctx);
    ri_words(ctx, bob, 2, 0x31434B4Cu, 16u);
    put_rom(ctx, bob, 0xEF050, 0x80);
    size_t finish_cells[8];
    ri_put_buf(ctx, bob, ri_fe_finish_lakitu_program(ctx, finish_cells));
    /* F1 operands from the opening and lamp scripts, in execution order. */
    static const uint32_t wait_commands[8] = {0xEFD7Cu, 0xEFD84u, 0xEF248u, 0xEF258u,
                                              0xEF278u, 0xEF290u, 0xEF2B0u, 0xEF2C8u};
    ri_u32(ctx, bob, 0x31434149u);
    ri_u16(ctx, bob, 8u);
    ri_u16(ctx, bob, 0u);
    for (unsigned i = 0u; i < 8u; ++i)
    {
        const uint32_t command = rom_u32(ctx, wait_commands[i]);
        const uint32_t wait = rom_u32(ctx, wait_commands[i] + 4u);
        if (command != 0xf1u || wait == 0u || wait > 8192u)
            ri_fail(ctx, "invalid item art intro wait");
        ri_u16(ctx, bob, wait);
    }
    ri_section(ctx, 38, 1, 3, bob);
}
