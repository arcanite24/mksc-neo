#include "driver_sprite_asset.h"

#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static size_t padded(size_t bytes)
{
    return (bytes + 3u) & ~(size_t)3u;
}

int portable_driver_sprites_open(const PortableAssetSectionView *section, PortableDriverSprites *out)
{
    if (!section || !out || section->kind != PORTABLE_ASSET_SECTION_DRIVER_SPRITES ||
        (section->version != 1u && section->version != 2u) || !section->bytes || section->size < 96u ||
        le32(section->bytes) != PORTABLE_DRIVER_SPRITE_MAGIC)
        return 0;
    const uint8_t *p = section->bytes;
    const size_t size = section->size;
    PortableDriverSprites s = {.version = section->version,
                               .width = le16(p + 4),
                               .height = le16(p + 6),
                               .frame_count = le32(p + 8),
                               .phase_count = le32(p + 12),
                               .shared_colors = le32(p + 16),
                               .metres_per_pixel_q16 = (int32_t)le32(p + 20),
                               .phase_distance_q16 = (int32_t)le32(p + 24),
                               .row_count = 1u};
    const uint32_t frame_limit = s.version == 1u ? 64u : PORTABLE_DRIVER_SPRITE_FRAMES;
    if (s.width < 8u || s.width > 128u || s.height < 8u || s.height > 128u || !s.frame_count ||
        s.frame_count > frame_limit || !s.phase_count || s.phase_count > PORTABLE_DRIVER_SPRITE_PHASES ||
        s.shared_colors > 256u || s.metres_per_pixel_q16 <= 0 || s.metres_per_pixel_q16 > 65536 ||
        s.phase_distance_q16 < 0)
        return 0;
    size_t at = 32u;
    s.row_pitch[0] = INT16_MIN;
    if (s.version == 1u)
    {
        if (le32(p + 28) != 0u)
            return 0;
        for (unsigned i = 0; i < PORTABLE_DRIVER_SPRITE_SLOTS; ++i)
            s.slots[0][i] = p[at + i];
        at += PORTABLE_DRIVER_SPRITE_SLOTS;
    }
    else
    {
        s.row_count = p[28];
        s.tumble_count = p[29];
        s.tumble_cycles = p[30];
        s.flags = p[31];
        if (!s.row_count || s.row_count > PORTABLE_DRIVER_SPRITE_ROWS ||
            s.tumble_count > PORTABLE_DRIVER_SPRITE_TUMBLE || (s.tumble_count != 0) != (s.tumble_cycles != 0) ||
            s.tumble_cycles > 15u || (s.flags & ~PORTABLE_DRIVER_SPRITE_FILTERED) || size < 64u)
            return 0;
        for (unsigned i = 0; i < 4u; ++i)
            s.star_add[i] = le32(p + 32 + i * 4u) & UINT32_C(0xffffff);
        s.star_ticks = p[48];
        s.ghost_alpha = p[49];
        s.boo_alpha = p[50];
        for (unsigned i = 51; i < 64; ++i)
            if (p[i])
                return 0;
        if ((s.star_add[0] | s.star_add[1] | s.star_add[2] | s.star_add[3]) && !s.star_ticks)
            return 0;
        at = 64u;
        const size_t lists = padded(s.row_count * 2u) + (size_t)s.row_count * PORTABLE_DRIVER_SPRITE_SLOTS * 2u +
                             padded(s.tumble_count * 2u);
        if (size - at < lists)
            return 0;
        for (unsigned r = 0; r < s.row_count; ++r)
        {
            s.row_pitch[r] = (int16_t)le16(p + at + r * 2u);
            if (r ? s.row_pitch[r] <= s.row_pitch[r - 1] : s.row_pitch[0] != INT16_MIN)
                return 0;
        }
        at += padded(s.row_count * 2u);
        for (unsigned r = 0; r < s.row_count; ++r)
            for (unsigned i = 0; i < PORTABLE_DRIVER_SPRITE_SLOTS; ++i, at += 2u)
                s.slots[r][i] = (uint16_t)le16(p + at);
        for (unsigned i = 0; i < s.tumble_count; ++i)
        {
            s.tumble[i] = (uint16_t)le16(p + at + i * 2u);
            if (s.tumble[i] >= s.frame_count)
                return 0;
        }
        at += padded(s.tumble_count * 2u);
    }
    for (unsigned r = 0; r < s.row_count; ++r)
        for (unsigned i = 0; i < PORTABLE_DRIVER_SPRITE_SLOTS; ++i)
            if (s.slots[r][i] >= s.frame_count)
                return 0;
    s.shared = p + at;
    s.frame_bytes = (size_t)s.width * s.height + (size_t)s.phase_count * (256u - s.shared_colors) * 4u;
    s.frames = s.shared + (size_t)s.shared_colors * 4u;
    if (size < at || size - at != (size_t)s.shared_colors * 4u + s.frame_bytes * s.frame_count)
        return 0;
    *out = s;
    return 1;
}

int portable_driver_sprites_find(const PortableAssetPackView *pack, uint32_t driver_id, PortableDriverSprites *out)
{
    PortableAssetSectionView section;
    return pack && portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_DRIVER_SPRITES, driver_id, &section) ==
                       PORTABLE_ASSET_PACK_OK &&
           portable_driver_sprites_open(&section, out);
}

uint32_t portable_driver_sprites_texel(const PortableDriverSprites *s, uint32_t frame, uint32_t phase, uint32_t x,
                                       uint32_t y)
{
    if (!s || frame >= s->frame_count || x >= s->width || y >= s->height)
        return 0;
    if (phase >= s->phase_count)
        phase = 0;
    const uint8_t *record = s->frames + (size_t)frame * s->frame_bytes;
    const uint32_t index = record[(size_t)y * s->width + x];
    if (index < s->shared_colors)
        return le32(s->shared + (size_t)index * 4u);
    return le32(record + (size_t)s->width * s->height +
                ((size_t)phase * (256u - s->shared_colors) + (index - s->shared_colors)) * 4u);
}

uint32_t portable_driver_sprites_row(const PortableDriverSprites *s, int32_t pitch)
{
    uint32_t row = 0;
    while (s && row + 1u < s->row_count && pitch >= s->row_pitch[row + 1u])
        ++row;
    return row;
}
