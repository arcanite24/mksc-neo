#include "fx_set_asset.h"

#include <string.h>

#define HEADER_BYTES 48u
#define SHADOW_BYTES 32u
#define STYLE_BYTES 16u
#define EMITTER_BYTES 56u
#define METRES_LIMIT (64 * 65536)

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t sle32(const uint8_t *p)
{
    return (int32_t)le32(p);
}

static int metres(int32_t v, int positive)
{
    return v <= METRES_LIMIT && v >= (positive ? 1 : -METRES_LIMIT);
}

int portable_fx_set_open(const PortableAssetSectionView *section, PortableFxSet *out)
{
    if (!section || !out || section->kind != PORTABLE_ASSET_SECTION_FX_SET || section->version != 1u ||
        !section->bytes || section->size < HEADER_BYTES || memcmp(section->bytes, "FXS1", 4))
        return 0;
    const uint8_t *p = section->bytes;
    PortableFxSet set;
    memset(&set, 0, sizeof set);
    set.shadow_count = le32(p + 4);
    set.style_count = le32(p + 8);
    set.emitter_count = le32(p + 12);
    if (set.shadow_count > PORTABLE_FX_SET_SHADOW_LIMIT || set.style_count > PORTABLE_FX_SET_STYLE_LIMIT ||
        set.emitter_count > PORTABLE_FX_SET_EMITTER_LIMIT ||
        section->size != HEADER_BYTES + (size_t)set.shadow_count * SHADOW_BYTES +
                             (size_t)set.style_count * STYLE_BYTES + (size_t)set.emitter_count * EMITTER_BYTES)
        return 0;
    for (unsigned i = 0; i < 4u; ++i)
        set.tier_argb[i] = le32(p + 16 + i * 4u);
    set.smoke_argb = le32(p + 32);
    set.dust_argb = le32(p + 36);
    set.flame_argb[0] = le32(p + 40);
    set.flame_argb[1] = le32(p + 44);
    const uint8_t *row = p + HEADER_BYTES;
    for (uint32_t i = 0; i < set.shadow_count; ++i, row += SHADOW_BYTES)
    {
        PortableFxShadow *s = &set.shadows[i];
        *s = (PortableFxShadow){.key = le32(row), .material = le32(row + 4), .half_width_q16 = sle32(row + 8),
                                .half_length_q16 = sle32(row + 12), .forward_q16 = sle32(row + 16),
                                .vanish_q16 = sle32(row + 20), .flags = le32(row + 24)};
        if (!s->material || !metres(s->half_width_q16, 1) || !metres(s->half_length_q16, 1) ||
            !metres(s->forward_q16, 0) || !metres(s->vanish_q16, 1) || s->flags & ~PORTABLE_FX_SHADOW_VOLUME ||
            le32(row + 28))
            return 0;
        for (uint32_t j = 0; j < i; ++j)
            if (set.shadows[j].key == s->key)
                return 0;
    }
    for (uint32_t i = 0; i < set.style_count; ++i, row += STYLE_BYTES)
    {
        PortableFxStyle *s = &set.styles[i];
        *s = (PortableFxStyle){.slot = le32(row), .material = le32(row + 4), .frames = le32(row + 8),
                               .flags = le32(row + 12)};
        if (s->slot >= PORTABLE_FX_STYLE_SLOTS || !s->material || !s->frames || s->frames > 16u ||
            (s->flags & ~(uint32_t)(PORTABLE_FX_STYLE_ADDITIVE | PORTABLE_FX_STYLE_COLOURED | PORTABLE_FX_STYLE_WHEEL)) ||
            ((s->flags & PORTABLE_FX_STYLE_WHEEL) && s->slot != PORTABLE_FX_STYLE_SPARK))
            return 0;
        for (uint32_t j = 0; j < i; ++j)
            if (set.styles[j].slot == s->slot)
                return 0;
    }
    for (uint32_t i = 0; i < set.emitter_count; ++i, row += EMITTER_BYTES)
    {
        PortableFxEmitter *e = &set.emitters[i];
        *e = (PortableFxEmitter){.id = le32(row), .slot = le32(row + 4), .interval = le32(row + 8),
                                 .burst = le32(row + 12), .life = le32(row + 16), .size_q16 = sle32(row + 20),
                                 .grow_q16 = sle32(row + 24), .spread_q16 = sle32(row + 40),
                                 .gravity_q16 = sle32(row + 44), .radius_q16 = sle32(row + 48),
                                 .argb = le32(row + 52)};
        for (unsigned a = 0; a < 3u; ++a)
            e->velocity_q16[a] = sle32(row + 28 + a * 4u);
        if (!e->id || e->slot >= PORTABLE_FX_STYLE_SLOTS || !e->interval || e->interval > 600u || !e->burst ||
            e->burst > 16u || !e->life || e->life > 600u || !metres(e->size_q16, 1) || !metres(e->grow_q16, 0) ||
            !metres(e->spread_q16, 0) || e->spread_q16 < 0 || !metres(e->gravity_q16, 0) ||
            !metres(e->radius_q16, 0) || e->radius_q16 < 0 || !metres(e->velocity_q16[0], 0) ||
            !metres(e->velocity_q16[1], 0) || !metres(e->velocity_q16[2], 0))
            return 0;
        for (uint32_t j = 0; j < i; ++j)
            if (set.emitters[j].id == e->id)
                return 0;
    }
    *out = set;
    return 1;
}

int portable_fx_set_find(const PortableAssetPackView *pack, uint32_t id, PortableFxSet *out)
{
    PortableAssetSectionView section;
    return pack && id &&
           portable_asset_pack_find_section(pack, PORTABLE_ASSET_SECTION_FX_SET, id, &section) ==
               PORTABLE_ASSET_PACK_OK &&
           portable_fx_set_open(&section, out);
}

const PortableFxShadow *portable_fx_set_shadow(const PortableFxSet *set, uint32_t kart_id, uint32_t driver_id)
{
    if (!set)
        return NULL;
    const uint32_t keys[3] = {kart_id, driver_id, 0u};
    for (unsigned k = 0; k < 3u; ++k)
    {
        if (k < 2u && !keys[k])
            continue;
        for (uint32_t i = 0; i < set->shadow_count; ++i)
            if (set->shadows[i].key == keys[k])
                return &set->shadows[i];
    }
    return NULL;
}

const PortableFxStyle *portable_fx_set_style(const PortableFxSet *set, PortableFxStyleSlot slot)
{
    for (uint32_t i = 0; set && i < set->style_count; ++i)
        if (set->styles[i].slot == (uint32_t)slot)
            return &set->styles[i];
    return NULL;
}

const PortableFxEmitter *portable_fx_set_emitter(const PortableFxSet *set, uint32_t id)
{
    for (uint32_t i = 0; set && id && i < set->emitter_count; ++i)
        if (set->emitters[i].id == id)
            return &set->emitters[i];
    return NULL;
}
