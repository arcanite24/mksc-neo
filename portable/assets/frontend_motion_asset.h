#ifndef PORTABLE_FRONTEND_MOTION_ASSET_H
#define PORTABLE_FRONTEND_MOTION_ASSET_H

#include "asset_pack.h"
#include "../core/menu_heading.h"

/* FEM1: nine (entry X/Y, settled X/Y) rows, fourteen medallion X/Y rows,
 * five tab X/Y rows, then the source 1024-entry signed Q15 sine table.
 * Borrowed immutable bytes; no pointer or native structure is serialized. */
#define PORTABLE_FRONTEND_MOTION_BYTES 4280u
#define PORTABLE_FRONTEND_MOTION_LEGACY_BYTES 4260u
#define PORTABLE_FRONTEND_MOTION_MAX_BYTES 4848u
typedef struct
{
    const uint8_t *bytes;
    size_t size;
} PortableFrontendMotion;

static inline uint32_t portable_menu_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline int32_t portable_menu_i32(const uint8_t *p)
{
    uint32_t u = portable_menu_u32(p);
    return u <= INT32_MAX ? (int32_t)u : -1 - (int32_t)(UINT32_MAX - u);
}
static inline int portable_menu_i16(const uint8_t *p)
{
    unsigned u = (unsigned)p[0] | (unsigned)p[1] << 8;
    return u < 32768u ? (int)u : (int)u - 65536;
}
static inline int portable_frontend_motion_open(const uint8_t *p, size_t size,
                                                PortableFrontendMotion *out)
{
    if (!p || !out ||
        (size != PORTABLE_FRONTEND_MOTION_BYTES && size != PORTABLE_FRONTEND_MOTION_LEGACY_BYTES &&
         (size < PORTABLE_FRONTEND_MOTION_BYTES + 56u ||
          size > PORTABLE_FRONTEND_MOTION_MAX_BYTES)) ||
        portable_menu_u32(p) != 0x314d4546u || portable_menu_u32(p + 4) != 9u ||
        portable_menu_u32(p + 8) != 14u || portable_menu_u32(p + 12) != 5u)
        return 0;
    for (unsigned i = 16u; i < 164u; i += 2u)
    {
        int v = portable_menu_i16(p + i);
        if (v < -1024 || v > 1024)
            return 0;
    }
    for (unsigned i = 164u; i < 4260u; i += 4u)
    {
        int32_t v = portable_menu_i32(p + i);
        if (v < -32768 || v > 32768)
            return 0;
    }
    for (unsigned i = 4260u; i < size && i < 4280u; i += 2u)
    {
        int v = portable_menu_i16(p + i);
        if (v < -1024 || v > 1024)
            return 0;
    }
    if (size > 4280u)
    {
        if (portable_menu_u32(p + 4280u) != 0x3147484du || portable_menu_u32(p + 4284u) != 4u)
            return 0;
        size_t at = 4288u;
        for (unsigned group = 0; group < 4u; ++group)
        {
            if (size - at < 12u)
                return 0;
            const unsigned count = portable_menu_u32(p + at);
            if (!portable_menu_u32(p + at + 4u))
                return 0;
            at += 12u;
            if (!count || count > 16u || size - at < count * 8u)
                return 0;
            for (unsigned i = 0; i < count; ++i)
            {
                if (!portable_menu_u32(p + at))
                    return 0;
                int x = portable_menu_i16(p + at + 4u), y = portable_menu_i16(p + at + 6u);
                if (x < -1024 || x > 1024 || y < -1024 || y > 1024)
                    return 0;
                at += 8u;
            }
        }
        if (at != size)
            return 0;
    }
    out->bytes = p;
    out->size = size;
    return 1;
}
static inline int portable_frontend_motion_headings(const PortableFrontendMotion *m,
                                                    PortableMenuHeadingRules *rules)
{
    if (!m || !rules || m->size <= 4280u)
        return 0;
    *rules = (PortableMenuHeadingRules){0};
    size_t at = 4288u;
    for (unsigned group = 0; group < 4u; ++group)
    {
        const unsigned count = portable_menu_u32(m->bytes + at);
        rules->groups[group].strip_image_id = portable_menu_u32(m->bytes + at + 4u);
        rules->groups[group].strip_digest = portable_menu_u32(m->bytes + at + 8u);
        at += 12u;
        rules->groups[group].count = (uint8_t)count;
        for (unsigned i = 0; i < count; ++i)
        {
            rules->groups[group].letters[i] = (PortableMenuHeadingLetter){
                portable_menu_u32(m->bytes + at), (int16_t)portable_menu_i16(m->bytes + at + 4u),
                (int16_t)portable_menu_i16(m->bytes + at + 6u)};
            at += 8u;
        }
    }
    return 1;
}
/* Missing old-pack metadata settles the page; malformed new metadata fails. */
static inline int portable_frontend_motion_load(const PortableAssetPackView *pack,
                                                PortableFrontendMotion *out)
{
    PortableAssetSectionView section;
    out->bytes = NULL;
    out->size = 0u;
    PortableAssetPackStatus status = portable_asset_pack_find_section(
        pack, PORTABLE_ASSET_SECTION_FRONTEND_MOTION, 1u, &section);
    if (status == PORTABLE_ASSET_PACK_NOT_FOUND)
        return 1;
    return status == PORTABLE_ASSET_PACK_OK &&
           (section.version == 1u || section.version == 2u || section.version == 3u) &&
           portable_frontend_motion_open(section.bytes, section.size, out);
}
static inline int32_t portable_menu_sine(const PortableFrontendMotion *m, unsigned index)
{
    return m->bytes ? portable_menu_i32(m->bytes + 164u + (index & 1023u) * 4u) : 32768;
}
static inline int portable_menu_asr15(int64_t v)
{
    return v >= 0 ? (int)(v / 32768) : -(int)((-v + 32767) / 32768);
}
static inline int portable_menu_sine_lerp(const PortableFrontendMotion *m, int start, int end,
                                          unsigned tick, unsigned duration)
{
    if (!m->bytes || tick >= duration)
        return end;
    return start + portable_menu_asr15((int64_t)(end - start) *
                                       portable_menu_sine(m, tick * 256u / duration));
}
/* E930 case10: signed16 angular narrowing, truncating divisions, then ASR15.
 * Its cosine envelope deliberately overshoots, unlike a generic ease-out. */
static inline int portable_menu_curve10(const PortableFrontendMotion *m, int start, int end,
                                        int tick, int duration)
{
    if (!m->bytes || tick >= duration)
        return end;
    if (tick < 0)
        return start;
    int angle = (tick * tick * 131072) / (70 * duration);
    angle = (int)((unsigned)angle & 65535u);
    if (angle >= 32768)
        angle -= 65536;
    int cosine = portable_menu_sine(m, (unsigned)(256 - angle / 64));
    int q = cosine * (duration - tick) * (duration - tick) / (duration * duration);
    if (q < 0)
        q = -q;
    return end - portable_menu_asr15((int64_t)q * (end - start));
}
/* 9124 updates the current preview offset, so this is a recurrence rather
 * than interpolation from its original anchor. Replay its bounded16 turns. */
static inline int portable_menu_preview_offset(const PortableFrontendMotion *m, unsigned tick)
{
    if (!m->bytes || tick >= 16u)
        return 0;
    int x = 160;
    for (unsigned t = 1u; t <= tick; ++t)
        x += portable_menu_asr15(-(int64_t)x * portable_menu_sine(m, t * 16u));
    return x;
}
static inline void portable_menu_character_anchor(const PortableFrontendMotion *m, unsigned slot,
                                                  unsigned tick, int *x, int *y)
{
    *x = *y = 0;
    if (!m->bytes || slot >= 9u)
        return;
    const uint8_t *p = m->bytes + 16u + slot * 8u;
    int end_x = portable_menu_i16(p + 4), end_y = portable_menu_i16(p + 6);
    *x = portable_menu_sine_lerp(m, portable_menu_i16(p), end_x, tick, 16u) - end_x;
    *y = portable_menu_sine_lerp(m, portable_menu_i16(p + 2), end_y, tick, 16u) - end_y;
}
#endif
