#include "race_particle_asset.h"
#include <string.h>
#define CHARGED_EXIT_MAGIC UINT32_C(0x31435846)
static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}
static int read16s(const uint8_t **p, const uint8_t *end, uint16_t *out, size_t count)
{
    if ((size_t)(end - *p) < count * 2u)
        return 0;
    for (size_t i = 0; i < count; ++i)
        out[i] = u16(*p + i * 2u);
    *p += count * 2u;
    return 1;
}
int portable_race_particle_asset_open(const PortableAssetSectionView *s,
                                      PortableRaceParticleStorage *st, uint32_t expected_frames,
                                      uint32_t expected_cells, PortableRaceParticleAssetView *v)
{
    if (!s || !s->bytes || !st || !v ||
        s->size >
            32u + PORTABLE_RACE_PARTICLE_FRAME_CAPACITY * 8u + PORTABLE_RACE_PARTICLE_TILE_BYTES +
                PORTABLE_RACE_PARTICLE_PALETTE_COUNT * 2u + PORTABLE_MUSHROOM_BURST_CELL_BYTES)
        return 0;
    const uint8_t *b = s->bytes, *p, *end = b + s->size;
    uint32_t tn, pn, fn, cb;
    if ((s->version != 1u && s->version != 2u) || s->size < 32 || u32(b) != CHARGED_EXIT_MAGIC)
        return 0;
    if (s->version == 2u)
    {
        if (s->content_id != 1u || expected_cells != PORTABLE_DRIFT_SPARK_CELL_BYTES)
            return 0;
        expected_cells += 144u;
    }
    tn = u32(b + 4);
    pn = u32(b + 8);
    fn = u32(b + 12);
    cb = u32(b + 24);
    if (tn != sizeof st->tiles || pn != PORTABLE_RACE_PARTICLE_PALETTE_COUNT || !fn ||
        fn > PORTABLE_RACE_PARTICLE_FRAME_CAPACITY || (expected_frames && fn != expected_frames) ||
        u32(b + 16) != 0x100u || u32(b + 20) != 3u || (expected_cells && cb != expected_cells) ||
        cb > sizeof st->cells || s->size != 32u + fn * 8u + tn + pn * 2u + cb)
        return 0;
    p = b + 32;
    for (size_t i = 0; i < fn; ++i, p += 8)
    {
        st->frames[i].offset = u32(p);
        st->frames[i].size = u16(p + 4);
        st->frames[i].duration = u16(p + 6);
        if (!st->frames[i].duration || st->frames[i].duration > 255u || st->frames[i].size < 2u ||
            st->frames[i].offset > cb || st->frames[i].size > cb - st->frames[i].offset)
            return 0;
    }
    memcpy(st->tiles, p, tn);
    p += tn;
    if (!read16s(&p, end, st->palette, pn))
        return 0;
    memcpy(st->cells, p, cb);
    p += cb;
    if (p != end)
        return 0;
    for (size_t i = 0u; !expected_frames && i < fn; ++i)
    {
        const PortableRaceParticleFrame *frame = &st->frames[i];
        if (frame->size != 2u + (size_t)u16(st->cells + frame->offset) * 6u)
            return 0;
    }
    if (s->version == 2u)
    {
        for (unsigned cell = 0u; cell < 18u; ++cell)
        {
            const uint8_t *record = st->cells + PORTABLE_DRIFT_SPARK_CELL_BYTES + cell * 8u;
            if (u16(record) != 1u || (u16(record + 2u) & 0xff00u) != 0u ||
                (u16(record + 4u) & 0xc000u) != 0x4000u)
                return 0;
            const unsigned tile = u16(record + 6u) & 1023u, bank = u16(record + 6u) >> 12u;
            if (tile < 0x100u || tile + 4u > 0x100u + tn / 32u || bank < 3u || bank >= 8u)
                return 0;
        }
    }
    *v = (PortableRaceParticleAssetView){st->tiles, tn,         st->palette, pn,     st->cells,
                                         cb,        st->frames, fn,          0x100u, 3u};
    return 1;
}
