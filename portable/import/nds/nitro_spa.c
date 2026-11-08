#include "nitro_spa.h"

#include <stdio.h>
#include <string.h>

#define HEADER_BYTES 32u
#define EMITTER_BYTES 88u
#define TEXTURE_HEADER_BYTES 32u

/* Optional blocks after the 88-byte header, in order: flag bit, size. */
static const struct
{
    uint32_t bit, size;
} blocks[] = {{8, 12}, {9, 12}, {10, 8}, {11, 12}, {16, 20}, {24, 8}, {25, 8}, {26, 16}, {27, 4}, {28, 8}, {29, 16}};
#define SCALE_ANIMATION 8u
#define GRAVITY 24u

NitroSpa nitro_spa(RiCtx *ctx, RiData file, const char *name)
{
    NitroSpa spa;
    memset(&spa, 0, sizeof(spa));
    spa.file = name;
    const uint8_t *header = ri_at(ctx, file, 0, HEADER_BYTES);
    if (memcmp(header, " APS12_1", 8) != 0)
        ri_fail(ctx, "%s: not an SPL 12_1 particle file", name);
    spa.emitter_count = ri_le16(header + 8);
    spa.texture_count = ri_le16(header + 10);
    const uint32_t resource_size = ri_le32(header + 16), texture_offset = ri_le32(header + 24);
    if (spa.emitter_count > NITRO_SPA_EMITTER_LIMIT || spa.texture_count > NITRO_SPA_TEXTURE_LIMIT ||
        texture_offset != HEADER_BYTES + resource_size)
        ri_fail(ctx, "%s: %u emitters, %u textures or the texture offset are out of bounds", name,
                (unsigned)spa.emitter_count, (unsigned)spa.texture_count);
    RiBuf *emitters = ri_buf_zero(ctx, (spa.emitter_count ? spa.emitter_count : 1u) * sizeof(NitroSpaEmitter));
    spa.emitters = (NitroSpaEmitter *)(void *)emitters->data;
    size_t at = HEADER_BYTES;
    for (uint32_t i = 0; i < spa.emitter_count; ++i)
    {
        const uint8_t *p = ri_at(ctx, file, at, EMITTER_BYTES);
        NitroSpaEmitter *e = &spa.emitters[i];
        e->flags = ri_le32(p);
        e->count = (int32_t)ri_le32(p + 16);
        e->radius = (int32_t)ri_le32(p + 20);
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            e->axis[axis] = (int16_t)ri_le16(p + 28 + axis * 2);
            e->gravity[axis] = 0;
        }
        e->color = (uint16_t)ri_le16(p + 34);
        e->outward_speed = (int32_t)ri_le32(p + 36);
        e->axis_speed = (int32_t)ri_le32(p + 40);
        e->scale = (int32_t)ri_le32(p + 44);
        e->life = ri_le16(p + 62);
        const uint32_t misc = ri_le32(p + 68), tiling = ri_le32(p + 72);
        e->interval = misc & 255u;
        e->alpha = misc >> 8 & 255u;
        e->texture = misc >> 24;
        e->tile_s = tiling >> 24 & 3u;
        e->tile_t = tiling >> 26 & 3u;
        e->scale_start = e->scale_end = 4096;
        at += EMITTER_BYTES;
        for (size_t b = 0; b < sizeof(blocks) / sizeof(blocks[0]); ++b)
        {
            if (!(e->flags >> blocks[b].bit & 1u))
                continue;
            const uint8_t *block = ri_at(ctx, file, at, blocks[b].size);
            if (blocks[b].bit == SCALE_ANIMATION)
            {
                e->scale_start = (int16_t)ri_le16(block);
                e->scale_end = (int16_t)ri_le16(block + 4);
            }
            else if (blocks[b].bit == GRAVITY)
                for (unsigned axis = 0; axis < 3; ++axis)
                    e->gravity[axis] = (int16_t)ri_le16(block + axis * 2);
            at += blocks[b].size;
        }
        if (at > texture_offset)
            ri_fail(ctx, "%s: emitter %u runs past the emitter table", name, (unsigned)i);
        if (e->texture >= spa.texture_count || e->alpha > 31u)
            ri_fail(ctx, "%s: emitter %u names texture %u of %u or alpha %u", name, (unsigned)i,
                    (unsigned)e->texture, (unsigned)spa.texture_count, (unsigned)e->alpha);
    }
    if (at != texture_offset)
        ri_fail(ctx, "%s: emitter table ends at %#zx, not %#x", name, at, (unsigned)texture_offset);
    static const uint32_t texel_bits[8] = {0, 8, 2, 4, 8, 2, 8, 16};
    at = texture_offset;
    for (uint32_t i = 0; i < spa.texture_count; ++i)
    {
        const uint8_t *p = ri_at(ctx, file, at, TEXTURE_HEADER_BYTES);
        const uint32_t param = ri_le32(p + 4), data_size = ri_le32(p + 8), palette_offset = ri_le32(p + 12),
                       palette_size = ri_le32(p + 16), total = ri_le32(p + 28);
        NitroTexture *t = &spa.textures[i];
        snprintf(t->name, sizeof(t->name), "spa texture %u", (unsigned)i);
        snprintf(spa.palettes[i].name, sizeof(spa.palettes[i].name), "spa palette %u", (unsigned)i);
        t->format = param & 15u;
        t->width = 8u << (param >> 4 & 15u);
        t->height = 8u << (param >> 8 & 15u);
        t->color0 = param >> 16 & 1u;
        spa.flips[i] = param >> 14 & 3u;
        if (memcmp(p, " TPS", 4) != 0 || t->format == NITRO_TEXTURE_NONE || t->format == NITRO_TEXTURE_COMPRESSED ||
            t->format > NITRO_TEXTURE_DIRECT || t->width > 256u || t->height > 256u ||
            data_size < (size_t)t->width * t->height * texel_bits[t->format] / 8u || total < TEXTURE_HEADER_BYTES)
            ri_fail(ctx, "%s: texture %u (param %#x) is not a supported image", name, (unsigned)i, (unsigned)param);
        t->texels = ri_span(ctx, file, at + TEXTURE_HEADER_BYTES, data_size);
        if (t->format != NITRO_TEXTURE_DIRECT)
            spa.palettes[i].colors = ri_span(ctx, file, at + palette_offset, palette_size);
        at += total;
    }
    if (at != file.size)
        ri_fail(ctx, "%s: texture bank ends at %#zx of %#zx", name, at, file.size);
    return spa;
}

RiBuf *nitro_spa_emitter_argb(RiCtx *ctx, const NitroSpa *spa, uint32_t emitter, uint32_t *width, uint32_t *height)
{
    if (emitter >= spa->emitter_count)
        ri_fail(ctx, "%s: no emitter %u", spa->file, (unsigned)emitter);
    const NitroSpaEmitter *e = &spa->emitters[emitter];
    const NitroTexture *t = &spa->textures[e->texture];
    const NitroTextures bank = {spa->file, NULL, 0, NULL, 0};
    RiBuf *source = nitro_texture_argb(ctx, &bank, t, t->format == NITRO_TEXTURE_DIRECT ? NULL : &spa->palettes[e->texture]);
    *width = t->width << e->tile_s;
    *height = t->height << e->tile_t;
    RiBuf *out = ri_buf_zero(ctx, (size_t)*width * *height * 4u);
    const uint32_t flips = spa->flips[e->texture];
    for (uint32_t y = 0; y < *height; ++y)
        for (uint32_t x = 0; x < *width; ++x)
        {
            uint32_t sx = x % t->width, sy = y % t->height;
            if (flips & 1u && (x / t->width) & 1u)
                sx = t->width - 1u - sx;
            if (flips & 2u && (y / t->height) & 1u)
                sy = t->height - 1u - sy;
            ri_set_pixel(out, (size_t)y * *width + x, ri_le32(source->data + ((size_t)sy * t->width + sx) * 4u));
        }
    ri_buf_free(ctx, source);
    return out;
}
