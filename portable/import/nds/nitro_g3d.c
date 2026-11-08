#include "nitro_g3d.h"

#include <stdio.h>
#include <string.h>

/* Evaluated meshes stay far below this; it only bounds hostile files. */
#define NITRO_VERTEX_LIMIT (1u << 20)

/* ---- Shared resource dictionary ---------------------------------------- */

/* A resource dictionary: a PATRICIA tree (skipped), entries of size_unit
 * bytes and 16-byte NUL-padded names. */
typedef struct NitroDict
{
    uint32_t count, size_unit;
    size_t entries, names; /* offsets in the source view */
} NitroDict;

static NitroDict dict(RiCtx *ctx, RiData source, size_t offset, const char *file, const char *what)
{
    if (ri_get_u8(ctx, source, offset) != 0)
        ri_fail(ctx, "%s: %s dictionary has revision %u", file, what,
                (unsigned)ri_get_u8(ctx, source, offset));
    NitroDict out = {ri_get_u8(ctx, source, offset + 1), 0, 0, 0};
    const size_t entries = offset + ri_get_u16(ctx, source, offset + 6);
    out.size_unit = ri_get_u16(ctx, source, entries);
    out.names = entries + ri_get_u16(ctx, source, entries + 2);
    out.entries = entries + 4;
    if (out.count && out.size_unit < 4)
        ri_fail(ctx, "%s: %s dictionary entries are %u bytes", file, what, (unsigned)out.size_unit);
    (void)ri_span(ctx, source, out.entries, (size_t)out.count * out.size_unit);
    (void)ri_span(ctx, source, out.names, (size_t)out.count * 16u);
    return out;
}

static size_t dict_entry(const NitroDict *d, uint32_t index)
{
    return d->entries + (size_t)index * d->size_unit;
}

static void dict_name(RiCtx *ctx, RiData source, const NitroDict *d, uint32_t index,
                      char out[NITRO_NAME_CAPACITY])
{
    const uint8_t *name = ri_at(ctx, source, d->names + (size_t)index * 16u, 16);
    size_t length = 0;
    while (length < 16 && name[length])
        ++length;
    memcpy(out, name, length);
    out[length] = 0;
}

static void header(RiCtx *ctx, RiData file, const char *magic, uint32_t version, const char *name)
{
    if (file.size < 0x14 || memcmp(file.data, magic, 4) != 0 || ri_le16(file.data + 4) != 0xFEFFu)
        ri_fail(ctx, "%s is not %.4s data", name, magic);
    if (ri_le16(file.data + 6) != version || ri_le16(file.data + 12) != 0x10u)
        ri_fail(ctx, "%s: unsupported %.4s version %u", name, magic, (unsigned)ri_le16(file.data + 6));
}

static void *table(RiCtx *ctx, size_t count, size_t size)
{
    return ri_buf_zero(ctx, (count ? count : 1) * size)->data;
}

/* ---- Textures ------------------------------------------------------------ */

static const uint32_t texel_bits[8] = {0, 8, 2, 4, 8, 2, 8, 16};

static NitroTextures tex0(RiCtx *ctx, RiData file, size_t offset, const char *name)
{
    NitroTextures out = {name, NULL, 0, NULL, 0};
    const RiData block = ri_span(ctx, file, offset, file.size - offset);
    if (block.size < 0x3C || memcmp(block.data, "TEX0", 4) != 0)
        ri_fail(ctx, "%s: missing TEX0 block", name);
    const RiData data = ri_span(ctx, block, ri_get_u32(ctx, block, 0x14), (size_t)ri_get_u16(ctx, block, 0x0C) << 3);
    const RiData compressed =
        ri_span(ctx, block, ri_get_u32(ctx, block, 0x24), (size_t)ri_get_u16(ctx, block, 0x1C) << 3);
    const size_t index_offset = ri_get_u32(ctx, block, 0x28);
    const RiData indices = ri_span(ctx, block, index_offset, compressed.size / 2);
    const RiData palettes =
        ri_span(ctx, block, ri_get_u32(ctx, block, 0x38), (size_t)ri_get_u16(ctx, block, 0x30) << 3);

    const NitroDict textures = dict(ctx, block, ri_get_u16(ctx, block, 0x0E), name, "texture");
    out.texture_count = textures.count;
    out.textures = table(ctx, textures.count, sizeof(NitroTexture));
    for (uint32_t index = 0; index < textures.count; ++index)
    {
        NitroTexture *texture = &out.textures[index];
        dict_name(ctx, block, &textures, index, texture->name);
        const uint32_t param = ri_get_u32(ctx, block, dict_entry(&textures, index));
        texture->format = param >> 26 & 7u;
        texture->width = 8u << (param >> 20 & 7u);
        texture->height = 8u << (param >> 23 & 7u);
        texture->color0 = param >> 29 & 1u;
        if (texture->format == NITRO_TEXTURE_NONE)
            ri_fail(ctx, "%s: texture %s has no format", name, texture->name);
        const size_t start = (size_t)(param & 0xFFFFu) << 3;
        const size_t bytes = (size_t)texture->width * texture->height * texel_bits[texture->format] / 8u;
        if (texture->format == NITRO_TEXTURE_COMPRESSED)
        {
            if (start + bytes > compressed.size)
                ri_fail(ctx, "%s: compressed texture %s exceeds its block", name, texture->name);
            texture->texels = ri_span(ctx, compressed, start, bytes);
            texture->indices = ri_span(ctx, indices, start / 2, bytes / 2);
        }
        else
        {
            if (start + bytes > data.size)
                ri_fail(ctx, "%s: texture %s exceeds its block", name, texture->name);
            texture->texels = ri_span(ctx, data, start, bytes);
        }
    }

    const NitroDict entries = dict(ctx, block, ri_get_u16(ctx, block, 0x34), name, "palette");
    out.palette_count = entries.count;
    out.palettes = table(ctx, entries.count, sizeof(NitroPalette));
    for (uint32_t index = 0; index < entries.count; ++index)
    {
        NitroPalette *palette = &out.palettes[index];
        dict_name(ctx, block, &entries, index, palette->name);
        const size_t start = (size_t)ri_get_u16(ctx, block, dict_entry(&entries, index)) << 3;
        if (start >= palettes.size)
            ri_fail(ctx, "%s: palette %s starts past its block", name, palette->name);
        palette->colors = ri_span(ctx, palettes, start, palettes.size - start);
    }
    return out;
}

NitroTextures nitro_btx(RiCtx *ctx, RiData file, const char *name)
{
    header(ctx, file, "BTX0", 1, name);
    if (ri_get_u16(ctx, file, 14) != 1)
        ri_fail(ctx, "%s: expected one TEX0 block", name);
    return tex0(ctx, file, ri_get_u32(ctx, file, 16), name);
}

const NitroTexture *nitro_find_texture(const NitroTextures *textures, const char *name)
{
    for (size_t index = 0; index < textures->texture_count; ++index)
        if (strcmp(textures->textures[index].name, name) == 0)
            return &textures->textures[index];
    return NULL;
}

const NitroPalette *nitro_find_palette(const NitroTextures *textures, const char *name)
{
    for (size_t index = 0; index < textures->palette_count; ++index)
        if (strcmp(textures->palettes[index].name, name) == 0)
            return &textures->palettes[index];
    return NULL;
}

static uint32_t expand5(uint32_t value)
{
    return value << 3 | value >> 2;
}

static uint32_t rgb555(uint32_t value)
{
    return expand5(value & 31u) << 16 | expand5(value >> 5 & 31u) << 8 | expand5(value >> 10 & 31u);
}

static uint32_t palette_color(RiCtx *ctx, const NitroTextures *textures, const NitroTexture *texture,
                              const NitroPalette *palette, uint32_t index)
{
    if ((size_t)index * 2 + 2 > palette->colors.size)
        ri_fail(ctx, "%s: texture %s reads colour %u past palette %s", textures->file, texture->name,
                (unsigned)index, palette->name);
    return rgb555(ri_le16(palette->colors.data + (size_t)index * 2));
}

static uint32_t blend(uint32_t a, uint32_t b, uint32_t wa, uint32_t wb)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 24; shift += 8)
        out |= ((a >> shift & 255u) * wa + (b >> shift & 255u) * wb) / (wa + wb) << shift;
    return out;
}

/* 4x4 compressed: each block holds sixteen 2-bit indices and a palette word
 * whose mode selects 3 colours plus transparency or 4 colours, either read or
 * interpolated from the first two (GBATEK "Texture Formats"). */
static void decode_compressed(RiCtx *ctx, const NitroTextures *textures, const NitroTexture *texture,
                              const NitroPalette *palette, RiBuf *out)
{
    const uint32_t blocks_x = texture->width / 4;
    for (uint32_t block = 0; block < texture->width * texture->height / 16; ++block)
    {
        const uint32_t bits = ri_le32(texture->texels.data + (size_t)block * 4);
        const uint32_t word = ri_le16(texture->indices.data + (size_t)block * 2);
        const uint32_t base = (word & 0x3FFFu) * 2, mode = word >> 14;
        uint32_t colors[4];
        colors[0] = 0xFF000000u | palette_color(ctx, textures, texture, palette, base);
        colors[1] = 0xFF000000u | palette_color(ctx, textures, texture, palette, base + 1);
        switch (mode)
        {
        case 0:
            colors[2] = 0xFF000000u | palette_color(ctx, textures, texture, palette, base + 2);
            colors[3] = 0;
            break;
        case 1:
            colors[2] = 0xFF000000u | blend(colors[0], colors[1], 1, 1);
            colors[3] = 0;
            break;
        case 2:
            colors[2] = 0xFF000000u | palette_color(ctx, textures, texture, palette, base + 2);
            colors[3] = 0xFF000000u | palette_color(ctx, textures, texture, palette, base + 3);
            break;
        default:
            colors[2] = 0xFF000000u | blend(colors[0], colors[1], 5, 3);
            colors[3] = 0xFF000000u | blend(colors[0], colors[1], 3, 5);
            break;
        }
        const uint32_t x0 = block % blocks_x * 4, y0 = block / blocks_x * 4;
        for (uint32_t texel = 0; texel < 16; ++texel)
            ri_set_pixel(out, (size_t)(y0 + texel / 4) * texture->width + x0 + texel % 4,
                         colors[bits >> (texel * 2) & 3u]);
    }
}

RiBuf *nitro_texture_argb(RiCtx *ctx, const NitroTextures *textures, const NitroTexture *texture,
                          const NitroPalette *palette)
{
    const size_t count = (size_t)texture->width * texture->height;
    RiBuf *out = ri_buf_zero(ctx, count * 4);
    if (texture->format != NITRO_TEXTURE_DIRECT && palette == NULL)
        ri_fail(ctx, "%s: texture %s has no palette", textures->file, texture->name);
    if (texture->format == NITRO_TEXTURE_COMPRESSED)
    {
        decode_compressed(ctx, textures, texture, palette, out);
        return out;
    }
    const uint8_t *texels = texture->texels.data;
    for (size_t index = 0; index < count; ++index)
    {
        uint32_t color = 0;
        switch (texture->format)
        {
        case NITRO_TEXTURE_A3I5:
        {
            const uint32_t value = texels[index], alpha = value >> 5;
            color = (alpha << 5 | alpha << 2 | alpha >> 1) << 24 |
                    palette_color(ctx, textures, texture, palette, value & 31u);
            break;
        }
        case NITRO_TEXTURE_A5I3:
        {
            const uint32_t value = texels[index];
            color = expand5(value >> 3) << 24 | palette_color(ctx, textures, texture, palette, value & 7u);
            break;
        }
        case NITRO_TEXTURE_PALETTE4:
        case NITRO_TEXTURE_PALETTE16:
        case NITRO_TEXTURE_PALETTE256:
        {
            const uint32_t bits = texel_bits[texture->format];
            const size_t bit = index * bits;
            const uint32_t value = texels[bit / 8] >> (bit % 8) & ((1u << bits) - 1u);
            color = (value == 0 && texture->color0) ? 0u
                    : 0xFF000000u | palette_color(ctx, textures, texture, palette, value);
            break;
        }
        default: /* direct colour: bit 15 is opacity */
        {
            const uint32_t value = ri_le16(texels + index * 2);
            color = (value & 0x8000u ? 0xFF000000u : 0u) | rgb555(value);
            break;
        }
        }
        ri_set_pixel(out, index, color);
    }
    return out;
}

/* ---- Models ------------------------------------------------------------- */

static void material_pairs(RiCtx *ctx, RiData block, size_t offset, NitroModel *model, int palettes)
{
    const NitroDict pairs = dict(ctx, block, offset, model->file, palettes ? "palette pairing" : "texture pairing");
    for (uint32_t index = 0; index < pairs.count; ++index)
    {
        char name[NITRO_NAME_CAPACITY];
        dict_name(ctx, block, &pairs, index, name);
        const size_t entry = dict_entry(&pairs, index);
        const size_t list = ri_get_u16(ctx, block, entry);
        const uint32_t count = ri_get_u8(ctx, block, entry + 2);
        for (uint32_t item = 0; item < count; ++item)
        {
            const uint32_t material = ri_get_u8(ctx, block, list + item);
            if (material >= model->material_count)
                ri_fail(ctx, "%s: %s pairs %s with missing material %u", model->file, model->name, name,
                        (unsigned)material);
            memcpy(palettes ? model->materials[material].palette : model->materials[material].texture, name,
                   sizeof(name));
        }
    }
}

static void material(RiCtx *ctx, RiData source, NitroModel *model, NitroMaterial *out)
{
    if (ri_get_u16(ctx, source, 0) != 0)
        ri_fail(ctx, "%s: material %s has an unknown tag", model->file, out->name);
    out->diffuse_ambient = ri_get_u32(ctx, source, 0x04);
    out->specular_emission = ri_get_u32(ctx, source, 0x08);
    out->polygon_attr = ri_get_u32(ctx, source, 0x0C);
    out->teximage_param = ri_get_u32(ctx, source, 0x14);
    const uint32_t flag = ri_get_u16(ctx, source, 0x1E);
    out->width = ri_get_u16(ctx, source, 0x20);
    out->height = ri_get_u16(ctx, source, 0x22);
    out->scale_s = out->scale_t = out->cos_r = 4096;
    size_t at = 0x2C;
    if (!(flag & 2u))
    {
        out->scale_s = ri_get_i32(ctx, source, at);
        out->scale_t = ri_get_i32(ctx, source, at + 4);
        at += 8;
    }
    if (!(flag & 4u))
    {
        out->sin_r = ri_get_i32(ctx, source, at);
        out->cos_r = ri_get_i32(ctx, source, at + 4);
        at += 8;
    }
    if (!(flag & 8u))
    {
        out->translate_s = ri_get_i32(ctx, source, at);
        out->translate_t = ri_get_i32(ctx, source, at + 4);
    }
    /* Untextured materials (shadow volumes) may have no extent; meshes that
     * read texcoords through them fail in mkds_corner. */
}

static void model(RiCtx *ctx, RiData block, NitroModel *out)
{
    out->data = block;
    const size_t commands = ri_get_u32(ctx, block, 0x04), materials = ri_get_u32(ctx, block, 0x08);
    const size_t shapes = ri_get_u32(ctx, block, 0x0C);
    if (commands > materials)
        ri_fail(ctx, "%s: %s render commands overlap its materials", out->file, out->name);
    out->commands = ri_span(ctx, block, commands, materials - commands);
    out->texmtx_mode = ri_get_u8(ctx, block, 0x16);
    out->up_scale = ri_get_i32(ctx, block, 0x1C);
    if (out->up_scale <= 0)
        ri_fail(ctx, "%s: %s has a non-positive position scale", out->file, out->name);

    const NitroDict nodes = dict(ctx, block, 0x40, out->file, "node");
    out->node_count = nodes.count;
    out->nodes = table(ctx, nodes.count, sizeof(RiData));
    for (uint32_t index = 0; index < nodes.count; ++index)
    {
        const size_t at = 0x40 + ri_get_u32(ctx, block, dict_entry(&nodes, index));
        out->nodes[index] = ri_span(ctx, block, at, block.size > at ? block.size - at : 0);
    }

    const RiData material_block = ri_span(ctx, block, materials, block.size - materials);
    const NitroDict entries = dict(ctx, material_block, 4, out->file, "material");
    out->material_count = entries.count;
    out->materials = table(ctx, entries.count, sizeof(NitroMaterial));
    for (uint32_t index = 0; index < entries.count; ++index)
    {
        NitroMaterial *item = &out->materials[index];
        dict_name(ctx, material_block, &entries, index, item->name);
        const size_t at = ri_get_u32(ctx, material_block, dict_entry(&entries, index));
        material(ctx, ri_span(ctx, material_block, at, material_block.size - at), out, item);
    }
    material_pairs(ctx, material_block, ri_get_u16(ctx, material_block, 0), out, 0);
    material_pairs(ctx, material_block, ri_get_u16(ctx, material_block, 2), out, 1);

    const RiData shape_block = ri_span(ctx, block, shapes, block.size - shapes);
    const NitroDict shape_dict = dict(ctx, shape_block, 0, out->file, "shape");
    out->shape_count = shape_dict.count;
    out->shapes = table(ctx, shape_dict.count, sizeof(RiData));
    out->shape_names = table(ctx, shape_dict.count, NITRO_NAME_CAPACITY);
    for (uint32_t index = 0; index < shape_dict.count; ++index)
    {
        dict_name(ctx, shape_block, &shape_dict, index, out->shape_names[index]);
        const size_t at = ri_get_u32(ctx, shape_block, dict_entry(&shape_dict, index));
        const RiData shape = ri_span(ctx, shape_block, at, 16);
        if (ri_get_u16(ctx, shape, 0) != 0)
            ri_fail(ctx, "%s: shape %s has an unknown tag", out->file, out->shape_names[index]);
        out->shapes[index] =
            ri_span(ctx, shape_block, at + ri_get_u32(ctx, shape, 8), ri_get_u32(ctx, shape, 12));
    }
}

NitroModels nitro_bmd(RiCtx *ctx, RiData file, const char *name)
{
    header(ctx, file, "BMD0", 2, name);
    NitroModels out = {name, NULL, 0, 0, {name, NULL, 0, NULL, 0}};
    const uint32_t blocks = ri_get_u16(ctx, file, 14);
    if (blocks != 1 && blocks != 2)
        ri_fail(ctx, "%s: expected one or two blocks, found %u", name, (unsigned)blocks);
    const size_t offset = ri_get_u32(ctx, file, 16);
    const RiData models = ri_span(ctx, file, offset, file.size - offset);
    if (models.size < 8 || memcmp(models.data, "MDL0", 4) != 0)
        ri_fail(ctx, "%s: missing MDL0 block", name);
    const NitroDict entries = dict(ctx, models, 8, name, "model");
    out.count = entries.count;
    out.models = table(ctx, entries.count, sizeof(NitroModel));
    for (uint32_t index = 0; index < entries.count; ++index)
    {
        NitroModel *item = &out.models[index];
        item->file = name;
        dict_name(ctx, models, &entries, index, item->name);
        const size_t at = ri_get_u32(ctx, models, dict_entry(&entries, index));
        const RiData block = ri_span(ctx, models, at, models.size - at);
        model(ctx, ri_span(ctx, block, 0, ri_get_u32(ctx, block, 0)), item);
    }
    if (blocks == 2)
    {
        out.has_textures = 1;
        out.textures = tex0(ctx, file, ri_get_u32(ctx, file, 20), name);
    }
    return out;
}

/* ---- Model evaluation ---------------------------------------------------- */

/* Affine transform p' = R p + t with Q12 entries. */
typedef struct Affine
{
    int64_t r[9], t[3];
} Affine;

static int64_t fx_mul(int64_t a, int64_t b)
{
    const int64_t product = a * b;
    return product >= 0 ? (product + 2048) >> 12 : -((-product + 2048) >> 12);
}

static Affine identity(void)
{
    Affine out = {{4096, 0, 0, 0, 4096, 0, 0, 0, 4096}, {0, 0, 0}};
    return out;
}

static Affine compose(const Affine *a, const Affine *b)
{
    Affine out;
    for (unsigned row = 0; row < 3; ++row)
    {
        for (unsigned column = 0; column < 3; ++column)
            out.r[row * 3 + column] = fx_mul(a->r[row * 3], b->r[column]) +
                                      fx_mul(a->r[row * 3 + 1], b->r[3 + column]) +
                                      fx_mul(a->r[row * 3 + 2], b->r[6 + column]);
        out.t[row] = fx_mul(a->r[row * 3], b->t[0]) + fx_mul(a->r[row * 3 + 1], b->t[1]) +
                     fx_mul(a->r[row * 3 + 2], b->t[2]) + a->t[row];
    }
    return out;
}

static int32_t sign_extend(uint32_t value, unsigned bits)
{
    const uint32_t mask = 1u << (bits - 1);
    value &= (1u << bits) - 1u;
    return (int32_t)(value ^ mask) - (int32_t)mask;
}

/* A node's local transform, local = T * R * S. r is row-major for column
 * vectors (the transpose of NitroSystem's row-vector matrices). */
typedef struct NodeParts
{
    int64_t t[3], r[9], s[3];
} NodeParts;

/* Where a, b, c and d go around the +-1 of each pivot form, as cells of
 * NitroSystem's row-major matrix (MarioKartToolbox G3dUtil.PivotUtil). */
static const uint8_t pivot_cells[9][4] = {{4, 5, 7, 8}, {3, 5, 6, 8}, {3, 4, 6, 7}, {1, 2, 7, 8}, {0, 2, 6, 8},
                                          {0, 1, 6, 7}, {1, 2, 4, 5}, {0, 2, 3, 5}, {0, 1, 3, 4}};

/* A compressed rotation: one axis maps to +-1, the other four cells hold a,
 * b, +-b and +-a. Out is NitroSystem row-major. */
static void pivot_matrix(uint32_t pivot, int negative, int reverse_c, int reverse_d, int64_t a, int64_t b,
                         int64_t out[9])
{
    for (unsigned cell = 0; cell < 9; ++cell)
        out[cell] = 0;
    out[pivot] = negative ? -4096 : 4096;
    out[pivot_cells[pivot][0]] = a;
    out[pivot_cells[pivot][1]] = b;
    out[pivot_cells[pivot][2]] = reverse_c ? -b : b;
    out[pivot_cells[pivot][3]] = reverse_d ? -a : a;
}

static void transpose(const int64_t in[9], int64_t out[9])
{
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 3; ++column)
            out[row * 3 + column] = in[column * 3 + row];
}

/* Node description: flags, then optional translation, rotation (a full 3x3
 * or a compressed pivot form) and scale. */
static NodeParts node_parts(RiCtx *ctx, const NitroModel *model, uint32_t index)
{
    if (index >= model->node_count)
        ri_fail(ctx, "%s: %s render commands use missing node %u", model->file, model->name, (unsigned)index);
    const RiData node = model->nodes[index];
    const uint32_t flags = ri_get_u16(ctx, node, 0);
    NodeParts out = {{0, 0, 0}, {4096, 0, 0, 0, 4096, 0, 0, 0, 4096}, {4096, 4096, 4096}};
    size_t at = 4;
    if (!(flags & 1u))
    {
        for (unsigned axis = 0; axis < 3; ++axis)
            out.t[axis] = ri_get_i32(ctx, node, at + axis * 4);
        at += 12;
    }
    if (!(flags & 2u))
    {
        int64_t m[9];
        if (flags & 8u)
        {
            const uint32_t pivot = flags >> 4 & 15u;
            if (pivot > 8)
                ri_fail(ctx, "%s: %s node %u has pivot %u", model->file, model->name, (unsigned)index,
                        (unsigned)pivot);
            pivot_matrix(pivot, flags & 0x100u, flags & 0x200u, flags & 0x400u, ri_get_i16(ctx, node, at),
                         ri_get_i16(ctx, node, at + 2), m);
            at += 4;
        }
        else
        {
            /* Nine fx16 cells; the first sits in the header word beside the flags. */
            for (unsigned item = 0; item < 9; ++item)
                m[item] = item == 0 ? ri_get_i16(ctx, node, 2) : ri_get_i16(ctx, node, at + (item - 1) * 2);
            at += 16;
        }
        transpose(m, out.r);
    }
    if (!(flags & 4u))
        for (unsigned axis = 0; axis < 3; ++axis)
            out.s[axis] = ri_get_i32(ctx, node, at + axis * 4);
    return out;
}

static Affine parts_affine(const NodeParts *parts)
{
    Affine out;
    for (unsigned row = 0; row < 3; ++row)
    {
        for (unsigned axis = 0; axis < 3; ++axis)
            out.r[row * 3 + axis] = fx_mul(parts->r[row * 3 + axis], parts->s[axis]);
        out.t[row] = parts->t[row];
    }
    return out;
}

/* ---- Skeletal animation ---------------------------------------------------- */

NitroJointAnimation nitro_bca(RiCtx *ctx, RiData file, const char *name)
{
    header(ctx, file, "BCA0", 1, name);
    if (ri_get_u16(ctx, file, 14) != 1 || ri_get_u32(ctx, file, 16) != 0x14 ||
        memcmp(ri_at(ctx, file, 0x14, 4), "JNT0", 4) != 0)
        ri_fail(ctx, "%s: expected one JNT0 block", name);
    const NitroDict animations = dict(ctx, file, 0x1C, name, "animation");
    if (animations.count == 0)
        ri_fail(ctx, "%s: no skeletal animation", name);
    const size_t at = 0x14 + ri_get_u32(ctx, file, dict_entry(&animations, 0));
    NitroJointAnimation out;
    memset(&out, 0, sizeof(out));
    dict_name(ctx, file, &animations, 0, out.name);
    out.file = name;
    out.block = ri_span(ctx, file, at, file.size - at);
    if (memcmp(ri_at(ctx, out.block, 0, 4), "J\0AC", 4) != 0)
        ri_fail(ctx, "%s: animation is not a skeletal animation", name);
    out.frames = ri_get_u16(ctx, out.block, 4);
    out.track_count = ri_get_u16(ctx, out.block, 6);
    /* Flags 1 and 2 ask for sub-frame and end-to-start interpolation, which
     * whole-frame playback never reaches. */
    if (out.frames == 0 || (ri_get_u32(ctx, out.block, 8) & ~3u) != 0)
        ri_fail(ctx, "%s: animation has no frames or unknown flags 0x%X", name,
                (unsigned)ri_get_u32(ctx, out.block, 8));
    for (uint32_t track = 0; track < out.track_count; ++track)
        (void)ri_get_u32(ctx, out.block, ri_get_u16(ctx, out.block, 0x14 + track * 2));
    return out;
}

/* A sampled curve's info word: step 1, 2 or 4 frames up to last_frame,
 * then one sample per frame; fx16 samples when narrow. */
typedef struct Curve
{
    uint32_t step, last_frame, narrow;
    size_t samples;
} Curve;

static Curve curve(RiCtx *ctx, const NitroJointAnimation *a, size_t at)
{
    const uint32_t info = ri_get_u32(ctx, a->block, at);
    const Curve out = {1u << (info >> 30), info >> 16 & 0x1FFFu, info >> 29 & 1u, ri_get_u32(ctx, a->block, at + 4)};
    if (out.step > 4)
        ri_fail(ctx, "%s: %s uses an 8-frame curve step", a->file, a->name);
    return out;
}

/* Sample positions and weights of a whole frame (NitroSystem's
 * CalculateAnimationValueFrame): value = (w0 * s[i0] + w1 * s[i1]) / (w0 + w1). */
typedef struct Pick
{
    uint32_t first, second, weight_first, weight_second;
} Pick;

static Pick pick(const Curve *c, uint32_t frame)
{
    if (c->step == 1 || frame % c->step == 0)
        return (Pick){frame / c->step, 0, 1, 0};
    if (frame > c->last_frame)
        return (Pick){c->step == 2 ? (c->last_frame >> 1) + 1 : (c->last_frame >> 2) + (frame & 3u), 0, 1, 0};
    const uint32_t index = frame / c->step;
    if (c->step == 2 || (frame & 1u) == 0)
        return (Pick){index, index + 1, 1, 1};
    return frame & 2u ? (Pick){index, index + 1, 1, 3} : (Pick){index, index + 1, 3, 1};
}

static int64_t weigh(const Pick *p, int64_t first, int64_t second)
{
    const int64_t total = p->weight_first + p->weight_second;
    const int64_t sum = first * p->weight_first + second * p->weight_second;
    return sum >= 0 ? sum / total : -(-sum / total);
}

/* Translation or scale sample; scale samples pair the value with its inverse. */
static int64_t scalar_sample(RiCtx *ctx, const NitroJointAnimation *a, const Curve *c, uint32_t index, int scale)
{
    const size_t width = c->narrow ? 2 : 4, at = c->samples + (size_t)index * width * (scale ? 2 : 1);
    return c->narrow ? ri_get_i16(ctx, a->block, at) : ri_get_i32(ctx, a->block, at);
}

static int64_t scalar(RiCtx *ctx, const NitroJointAnimation *a, size_t at, int constant, uint32_t frame, int scale)
{
    if (constant)
        return ri_get_i32(ctx, a->block, at);
    const Curve c = curve(ctx, a, at);
    const Pick p = pick(&c, frame);
    const int64_t first = scalar_sample(ctx, a, &c, p.first, scale);
    return p.weight_second ? weigh(&p, first, scalar_sample(ctx, a, &c, p.second, scale)) : first;
}

static int64_t isqrt(uint64_t value)
{
    uint64_t root = 0, bit = UINT64_C(1) << 62;
    while (bit > value)
        bit >>= 2;
    while (bit)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1) + bit;
        }
        else
            root >>= 1;
        bit >>= 2;
    }
    return (int64_t)root;
}

static void normalize(int64_t v[3])
{
    const int64_t length = isqrt((uint64_t)(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    if (length == 0)
        return;
    for (unsigned axis = 0; axis < 3; ++axis)
        v[axis] = v[axis] * 4096 / length;
}

static void cross(const int64_t a[3], const int64_t b[3], int64_t out[3])
{
    out[0] = fx_mul(a[1], b[2]) - fx_mul(a[2], b[1]);
    out[1] = fx_mul(a[2], b[0]) - fx_mul(a[0], b[2]);
    out[2] = fx_mul(a[0], b[1]) - fx_mul(a[1], b[0]);
}

/* A rotation sample in NitroSystem row-major: a pivot form, or a basis whose
 * third row is the cross product of the first two (returns 1). */
static int rotation_sample(RiCtx *ctx, const NitroJointAnimation *a, uint32_t value, int64_t m[9])
{
    const uint32_t index = value & 0x7FFFu;
    if (value & 0x8000u)
    {
        const size_t at = ri_get_u32(ctx, a->block, 12) + (size_t)index * 6;
        const uint32_t info = ri_get_u16(ctx, a->block, at), pivot = info & 15u;
        if (pivot > 8)
            ri_fail(ctx, "%s: %s rotation %u has pivot %u", a->file, a->name, (unsigned)index, (unsigned)pivot);
        pivot_matrix(pivot, info & 0x10u, info & 0x20u, info & 0x40u, ri_get_i16(ctx, a->block, at + 2),
                     ri_get_i16(ctx, a->block, at + 4), m);
        return 0;
    }
    const size_t at = ri_get_u32(ctx, a->block, 16) + (size_t)index * 10;
    uint32_t data[5], low = 0;
    for (unsigned k = 0; k < 5; ++k)
        data[k] = ri_get_u16(ctx, a->block, at + k * 2);
    static const uint8_t cells[5] = {4, 0, 1, 2, 3}; /* data[4], then data[0..3] */
    for (unsigned k = 0; k < 5; ++k)
    {
        const uint32_t word = data[cells[k]];
        low = low << 3 | (word & 7u);
    }
    m[0] = sign_extend(data[0] >> 3, 13);
    m[1] = sign_extend(data[1] >> 3, 13);
    m[2] = sign_extend(data[2] >> 3, 13);
    m[3] = sign_extend(data[3] >> 3, 13);
    m[4] = sign_extend(data[4] >> 3, 13);
    m[5] = sign_extend(low, 13);
    cross(m, m + 3, m + 6);
    return 1;
}

static void rotation(RiCtx *ctx, const NitroJointAnimation *a, size_t at, int constant, uint32_t frame, int64_t r[9])
{
    int64_t m[9];
    if (constant)
        (void)rotation_sample(ctx, a, ri_get_u16(ctx, a->block, at), m);
    else
    {
        const Curve c = curve(ctx, a, at);
        const Pick p = pick(&c, frame);
        int basis = rotation_sample(ctx, a, ri_get_u16(ctx, a->block, c.samples + (size_t)p.first * 2), m);
        if (p.weight_second)
        {
            int64_t other[9];
            basis |= rotation_sample(ctx, a, ri_get_u16(ctx, a->block, c.samples + (size_t)p.second * 2), other);
            for (unsigned cell = 0; cell < 9; ++cell)
                m[cell] = m[cell] * p.weight_first + other[cell] * p.weight_second;
            normalize(m);
            normalize(m + 3);
            if (!basis)
                normalize(m + 6);
        }
        if (basis)
            cross(m, m + 3, m + 6);
        else if (!p.weight_second)
            normalize(m + 6);
    }
    transpose(m, r);
}

/* Replaces the parts a track animates; identity flags reset them and base
 * flags keep the model's own. */
static void animate_node(RiCtx *ctx, const NitroJointAnimation *a, uint32_t node, uint32_t frame, NodeParts *parts)
{
    for (uint32_t track = 0; track < a->track_count; ++track)
    {
        const size_t offset = ri_get_u16(ctx, a->block, 0x14 + track * 2);
        const uint32_t flags = ri_get_u32(ctx, a->block, offset);
        if (flags >> 24 != node)
            continue;
        const NodeParts identity_parts = {{0, 0, 0}, {4096, 0, 0, 0, 4096, 0, 0, 0, 4096}, {4096, 4096, 4096}};
        if (flags & 1u)
        {
            *parts = identity_parts;
            return;
        }
        size_t at = offset + 4;
        if (!(flags & 6u))
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const int constant = flags >> (3 + axis) & 1u;
                parts->t[axis] = scalar(ctx, a, at, constant, frame, 0);
                at += constant ? 4 : 8;
            }
        else if (flags & 2u)
            memcpy(parts->t, identity_parts.t, sizeof(parts->t));
        if (!(flags & 0xC0u))
        {
            rotation(ctx, a, at, flags & 0x100u, frame, parts->r);
            at += flags & 0x100u ? 4 : 8;
        }
        else if (flags & 0x40u)
            memcpy(parts->r, identity_parts.r, sizeof(parts->r));
        if (!(flags & 0x600u))
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                parts->s[axis] = scalar(ctx, a, at, flags >> (11 + axis) & 1u, frame, 1);
                at += 8;
            }
        else if (flags & 0x200u)
            memcpy(parts->s, identity_parts.s, sizeof(parts->s));
        return;
    }
}

/* Matrices remember the node that last set them, so each vertex knows its
 * node; posing runs only the matrix steps, at an animation frame. */
typedef struct Evaluator
{
    RiCtx *ctx;
    const NitroModel *model;
    NitroMesh *mesh; /* NULL: pose only */
    Affine stack[NITRO_MATRIX_STACK];
    uint8_t stack_node[NITRO_MATRIX_STACK];
    Affine current;
    uint8_t current_node;
    uint32_t material;
    const NitroJointAnimation *animation;
    uint32_t frame;
    NitroNodeMatrix *pose;
    int backdrop; /* the eye sits near the model origin */
} Evaluator;

/* A backdrop (sky) is seen from near its origin, so a camera-facing node
 * there turns its +Z toward the model origin: all axes for 7, only about its
 * own Y for 8. */
static void face_origin(Affine *m, int y_only)
{
    int64_t column[3][3], scale[3];
    for (unsigned c = 0; c < 3; ++c)
    {
        for (unsigned row = 0; row < 3; ++row)
            column[c][row] = m->r[row * 3 + c];
        scale[c] = isqrt((uint64_t)(column[c][0] * column[c][0] + column[c][1] * column[c][1] +
                                    column[c][2] * column[c][2]));
    }
    int64_t z[3] = {-m->t[0], -m->t[1], -m->t[2]}, y[3] = {0, 4096, 0}, x[3];
    if (y_only && scale[1])
    {
        memcpy(y, column[1], sizeof(y));
        normalize(y);
        const int64_t along = fx_mul(z[0], y[0]) + fx_mul(z[1], y[1]) + fx_mul(z[2], y[2]);
        for (unsigned axis = 0; axis < 3; ++axis)
            z[axis] -= fx_mul(along, y[axis]);
    }
    if (!z[0] && !z[1] && !z[2])
        return; /* at the eye: no direction to face */
    normalize(z);
    cross(y, z, x);
    if (!x[0] && !x[1] && !x[2])
        x[0] = 4096;
    normalize(x);
    cross(z, x, y);
    const int64_t *axes[3] = {x, y, z};
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned row = 0; row < 3; ++row)
            m->r[row * 3 + c] = fx_mul(axes[c][row], scale[c]);
}

static void emit_vertex(Evaluator *e, const Affine *matrix, uint8_t node, const int32_t position[3],
                        const int32_t texcoord[2], uint32_t color, const int32_t normal[3], int has_normal)
{
    if (e->mesh->vertex_count >= NITRO_VERTEX_LIMIT)
        ri_fail(e->ctx, "%s: %s has too many vertices", e->model->file, e->model->name);
    NitroVertex vertex;
    memset(&vertex, 0, sizeof(vertex));
    for (unsigned row = 0; row < 3; ++row)
    {
        const int64_t local = fx_mul(matrix->r[row * 3], position[0]) + fx_mul(matrix->r[row * 3 + 1], position[1]) +
                              fx_mul(matrix->r[row * 3 + 2], position[2]) + matrix->t[row];
        const int64_t world = fx_mul(local, e->model->up_scale);
        if (world < INT32_MIN || world > INT32_MAX)
            ri_fail(e->ctx, "%s: %s vertex position overflows", e->model->file, e->model->name);
        vertex.position[row] = (int32_t)world;
        vertex.local[row] = position[row];
        if (has_normal)
            vertex.normal[row] = (int32_t)(fx_mul(matrix->r[row * 3], normal[0]) +
                                           fx_mul(matrix->r[row * 3 + 1], normal[1]) +
                                           fx_mul(matrix->r[row * 3 + 2], normal[2]));
    }
    vertex.texcoord[0] = texcoord[0];
    vertex.texcoord[1] = texcoord[1];
    vertex.color = (uint16_t)(color & 0x7FFFu);
    vertex.has_normal = (uint8_t)has_normal;
    vertex.node = node;
    ri_put(e->ctx, e->mesh->vertices, &vertex, sizeof(vertex));
    e->mesh->vertex_count++;
}

static void emit_triangle(Evaluator *e, uint32_t a, uint32_t b, uint32_t c)
{
    const NitroTriangle triangle = {{a, b, c}, e->material};
    ri_put(e->ctx, e->mesh->triangles, &triangle, sizeof(triangle));
    e->mesh->triangle_count++;
}

/* Turns one BEGIN_VTXS..END_VTXS run into triangles, keeping the GX
 * front-face winding (counter-clockwise, as in OpenGL). */
static void assemble(Evaluator *e, uint32_t primitive, uint32_t first, uint32_t count)
{
    switch (primitive)
    {
    case 0:
        for (uint32_t i = 0; i + 3 <= count; i += 3)
            emit_triangle(e, first + i, first + i + 1, first + i + 2);
        break;
    case 1:
        for (uint32_t i = 0; i + 4 <= count; i += 4)
        {
            emit_triangle(e, first + i, first + i + 1, first + i + 2);
            emit_triangle(e, first + i, first + i + 2, first + i + 3);
        }
        break;
    case 2:
        for (uint32_t i = 0; i + 3 <= count; ++i)
            if (i % 2 == 0)
                emit_triangle(e, first + i, first + i + 1, first + i + 2);
            else
                emit_triangle(e, first + i + 1, first + i, first + i + 2);
        break;
    default:
        for (uint32_t i = 0; i + 4 <= count; i += 2)
        {
            emit_triangle(e, first + i, first + i + 1, first + i + 3);
            emit_triangle(e, first + i, first + i + 3, first + i + 2);
        }
        break;
    }
}

/* Parameter words of each geometry command; -1 marks unknown commands. */
static int command_parameters(uint32_t command)
{
    static const int8_t counts[0x73] = {
        [0x10] = 1, [0x11] = 0, [0x12] = 1, [0x13] = 1, [0x14] = 1, [0x15] = 0, [0x16] = 16, [0x17] = 12,
        [0x18] = 16, [0x19] = 12, [0x1A] = 9, [0x1B] = 3, [0x1C] = 3, [0x20] = 1, [0x21] = 1, [0x22] = 1,
        [0x23] = 2, [0x24] = 1, [0x25] = 1, [0x26] = 1, [0x27] = 1, [0x28] = 1, [0x29] = 1, [0x2A] = 1,
        [0x2B] = 1, [0x30] = 1, [0x31] = 1, [0x32] = 1, [0x33] = 1, [0x34] = 32, [0x40] = 1, [0x41] = 0,
        [0x50] = 1, [0x60] = 1, [0x70] = 3, [0x71] = 2, [0x72] = 1};
    if (command == 0)
        return 0;
    if (command >= sizeof(counts) || (counts[command] == 0 && command != 0x11 && command != 0x15 &&
                                       command != 0x41))
        return -1;
    return counts[command];
}

static void display_list(Evaluator *e, uint32_t shape)
{
    RiCtx *ctx = e->ctx;
    const RiData list = e->model->shapes[shape];
    const NitroMaterial *material = &e->model->materials[e->material];
    const char *name = e->model->shape_names[shape];
    Affine matrix = e->current;
    uint8_t node = e->current_node;
    int32_t position[3] = {0, 0, 0}, texcoord[2] = {0, 0}, normal[3] = {0, 0, 0};
    uint32_t color = material->diffuse_ambient & 0x7FFFu;
    int has_normal = 0, open = 0;
    uint32_t primitive = 0, first = 0;
    size_t at = 0;
    while (at + 4 <= list.size)
    {
        const uint8_t *packed = ri_at(ctx, list, at, 4);
        at += 4;
        for (unsigned slot = 0; slot < 4; ++slot)
        {
            const uint32_t command = packed[slot];
            const int words = command_parameters(command);
            if (words < 0)
                ri_fail(ctx, "%s: %s shape %s uses unknown GX command 0x%02X", e->model->file, e->model->name,
                        name, (unsigned)command);
            const RiData parameters = ri_span(ctx, list, at, (size_t)words * 4);
            at += (size_t)words * 4;
            const uint32_t p = words ? ri_le32(parameters.data) : 0;
            switch (command)
            {
            case 0x14:
                if (p >= NITRO_MATRIX_STACK)
                    ri_fail(ctx, "%s: %s shape %s restores matrix %u", e->model->file, e->model->name, name,
                            (unsigned)p);
                matrix = e->stack[p];
                node = e->stack_node[p];
                break;
            case 0x20:
                color = p & 0x7FFFu;
                break;
            case 0x21:
                normal[0] = sign_extend(p, 10);
                normal[1] = sign_extend(p >> 10, 10);
                normal[2] = sign_extend(p >> 20, 10);
                has_normal = 1;
                break;
            case 0x22:
                texcoord[0] = sign_extend(p, 16);
                texcoord[1] = sign_extend(p >> 16, 16);
                break;
            case 0x23:
                position[0] = sign_extend(p, 16);
                position[1] = sign_extend(p >> 16, 16);
                position[2] = sign_extend(ri_le32(parameters.data + 4), 16);
                break;
            case 0x24:
                position[0] = sign_extend(p, 10) << 6;
                position[1] = sign_extend(p >> 10, 10) << 6;
                position[2] = sign_extend(p >> 20, 10) << 6;
                break;
            case 0x25:
                position[0] = sign_extend(p, 16);
                position[1] = sign_extend(p >> 16, 16);
                break;
            case 0x26:
                position[0] = sign_extend(p, 16);
                position[2] = sign_extend(p >> 16, 16);
                break;
            case 0x27:
                position[1] = sign_extend(p, 16);
                position[2] = sign_extend(p >> 16, 16);
                break;
            case 0x28:
                position[0] += sign_extend(p, 10);
                position[1] += sign_extend(p >> 10, 10);
                position[2] += sign_extend(p >> 20, 10);
                break;
            case 0x30:
                if (p & 0x8000u)
                    color = p & 0x7FFFu;
                break;
            case 0x40:
                if (open)
                    assemble(e, primitive, first, e->mesh->vertex_count - first);
                primitive = p & 3u;
                first = e->mesh->vertex_count;
                open = 1;
                break;
            case 0x41:
                if (open)
                    assemble(e, primitive, first, e->mesh->vertex_count - first);
                open = 0;
                break;
            case 0x29: case 0x2A: case 0x2B: case 0x31: case 0x32: case 0x33: case 0x34:
            case 0x00:
                break; /* attributes and lighting tables do not change geometry */
            default:
                ri_fail(ctx, "%s: %s shape %s uses unsupported GX command 0x%02X", e->model->file, e->model->name,
                        name, (unsigned)command);
            }
            if (command >= 0x23 && command <= 0x28)
            {
                if (!open)
                    ri_fail(ctx, "%s: %s shape %s sends a vertex outside BEGIN_VTXS", e->model->file,
                            e->model->name, name);
                emit_vertex(e, &matrix, node, position, texcoord, color, normal, has_normal);
            }
        }
    }
    if (open)
        assemble(e, primitive, first, e->mesh->vertex_count - first);
}

static uint32_t command_byte(RiCtx *ctx, const NitroModel *model, size_t *at)
{
    const uint32_t value = ri_get_u8(ctx, model->commands, *at);
    ++*at;
    return value;
}

static uint32_t stack_slot(RiCtx *ctx, const NitroModel *model, size_t *at)
{
    const uint32_t slot = command_byte(ctx, model, at);
    if (slot >= NITRO_MATRIX_STACK)
        ri_fail(ctx, "%s: %s uses matrix stack slot %u", model->file, model->name, (unsigned)slot);
    return slot;
}

/* Runs the render commands (SBC): node matrices into the matrix stack,
 * material binds and shape draws. */
static void run(Evaluator *e)
{
    RiCtx *ctx = e->ctx;
    const NitroModel *model = e->model;
    e->current = identity();
    e->current_node = NITRO_NO_NODE;
    for (unsigned slot = 0; slot < NITRO_MATRIX_STACK; ++slot)
    {
        e->stack[slot] = identity();
        e->stack_node[slot] = NITRO_NO_NODE;
    }
    int bound = 0;
    size_t at = 0;
    for (;;)
    {
        if (at >= model->commands.size)
            ri_fail(ctx, "%s: %s render commands do not end", model->file, model->name);
        const uint32_t byte = command_byte(ctx, model, &at), command = byte & 31u, option = byte >> 5;
        if (command == 1)
            break;
        switch (command)
        {
        case 0:
            break;
        case 2:
            at += 2; /* node visibility: every node starts visible */
            break;
        case 3:
        {
            const uint32_t slot = stack_slot(ctx, model, &at);
            e->current = e->stack[slot];
            e->current_node = e->stack_node[slot];
            break;
        }
        case 4:
            e->material = command_byte(ctx, model, &at);
            if (e->material >= model->material_count)
                ri_fail(ctx, "%s: %s binds missing material %u", model->file, model->name, (unsigned)e->material);
            bound = 1;
            break;
        case 5:
        {
            const uint32_t shape = command_byte(ctx, model, &at);
            if (shape >= model->shape_count || !bound)
                ri_fail(ctx, "%s: %s draws shape %u without a material", model->file, model->name, (unsigned)shape);
            if (e->mesh)
                display_list(e, shape);
            break;
        }
        case 6:
        case 7:
        case 8:
        {
            const uint32_t node = command_byte(ctx, model, &at);
            if (command == 6)
                at += 2; /* parent and flags: the order of commands already encodes them */
            const uint32_t destination = option & 1u ? stack_slot(ctx, model, &at) : NITRO_MATRIX_STACK;
            if (option & 2u)
            {
                const uint32_t slot = stack_slot(ctx, model, &at);
                e->current = e->stack[slot];
                e->current_node = e->stack_node[slot];
            }
            if (command == 6)
            {
                if (node >= NITRO_NO_NODE)
                    ri_fail(ctx, "%s: %s has more than %u nodes", model->file, model->name, (unsigned)NITRO_NO_NODE);
                NodeParts parts = node_parts(ctx, model, node);
                if (e->animation)
                    animate_node(ctx, e->animation, node, e->frame, &parts);
                const Affine local = parts_affine(&parts);
                e->current = compose(&e->current, &local);
                e->current_node = (uint8_t)node;
                if (e->pose)
                {
                    NitroNodeMatrix *out = &e->pose[node];
                    for (unsigned cell = 0; cell < 9; ++cell)
                        out->r[cell] = (int32_t)fx_mul(e->current.r[cell], model->up_scale);
                    for (unsigned axis = 0; axis < 3; ++axis)
                        out->t[axis] = (int32_t)fx_mul(e->current.t[axis], model->up_scale);
                    out->evaluated = 1;
                }
            }
            else if (e->backdrop)
                face_origin(&e->current, command == 8);
            else if (e->mesh)
                e->mesh->billboard_nodes++;
            if (destination < NITRO_MATRIX_STACK)
            {
                e->stack[destination] = e->current;
                e->stack_node[destination] = e->current_node;
            }
            break;
        }
        case 11:
            break; /* position scale: applied to every vertex by emit_vertex */
        default:
            ri_fail(ctx, "%s: %s uses unsupported render command 0x%02X", model->file, model->name, (unsigned)byte);
        }
    }
}

NitroMesh nitro_model_mesh(RiCtx *ctx, const NitroModel *model, int backdrop)
{
    NitroMesh mesh = {ri_buf(ctx), ri_buf(ctx), 0, 0, 0};
    Evaluator e;
    memset(&e, 0, sizeof(e));
    e.ctx = ctx;
    e.model = model;
    e.mesh = &mesh;
    e.backdrop = backdrop;
    run(&e);
    return mesh;
}

void nitro_model_pose(RiCtx *ctx, const NitroModel *model, const NitroJointAnimation *animation,
                      uint32_t frame, NitroNodeMatrix *out)
{
    Evaluator e;
    memset(&e, 0, sizeof(e));
    memset(out, 0, (size_t)model->node_count * sizeof(*out));
    e.ctx = ctx;
    e.model = model;
    e.animation = animation;
    e.frame = animation && frame >= animation->frames ? animation->frames - 1 : frame;
    e.pose = out;
    run(&e);
}

/* ---- Texture SRT animation ---------------------------------------------- */

typedef struct Track
{
    uint32_t flags, value;
} Track;

/* Sample at frame of a stepped track, interpolating linearly between steps
 * and holding the last sample. Rotation packs sin (low) and cos (high). */
static void track_sample(RiCtx *ctx, RiData block, Track track, uint32_t duration, uint32_t frame, int rotation,
                         int32_t out[2], const char *file)
{
    if (track.flags & 0x20000000u)
    {
        if (rotation)
        {
            out[0] = sign_extend(track.value, 16);
            out[1] = sign_extend(track.value >> 16, 16);
        }
        else
            out[0] = (int32_t)track.value;
        return;
    }
    const uint32_t step = track.flags & 0x80000000u ? 4 : track.flags & 0x40000000u ? 2 : 1;
    const int half = rotation || (track.flags & 0x10000000u);
    const uint32_t samples = (duration + step - 1) / step;
    const uint32_t index = frame / step, next = index + 1 < samples ? index + 1 : index, part = frame % step;
    int32_t values[2][2] = {{0, 0}, {0, 0}};
    const uint32_t picks[2] = {index, next};
    for (unsigned k = 0; k < 2; ++k)
    {
        const size_t at = track.value + (size_t)picks[k] * (half ? (rotation ? 4 : 2) : 4);
        if (rotation)
        {
            const uint32_t word = ri_get_u32(ctx, block, at);
            values[k][0] = sign_extend(word, 16);
            values[k][1] = sign_extend(word >> 16, 16);
        }
        else
            values[k][0] = half ? ri_get_i16(ctx, block, at) : ri_get_i32(ctx, block, at);
    }
    (void)file;
    for (unsigned c = 0; c < (rotation ? 2u : 1u); ++c)
        out[c] = values[0][c] + (int32_t)(((int64_t)values[1][c] - values[0][c]) * (int64_t)part / (int64_t)step);
}

NitroSrt nitro_bta(RiCtx *ctx, RiData file, const char *name)
{
    header(ctx, file, "BTA0", 1, name);
    if (ri_get_u16(ctx, file, 14) != 1 || ri_get_u32(ctx, file, 16) != 0x14 ||
        memcmp(ri_at(ctx, file, 0x14, 4), "SRT0", 4) != 0)
        ri_fail(ctx, "%s: expected one SRT0 block", name);
    const NitroDict animations = dict(ctx, file, 0x1C, name, "animation");
    if (animations.count != 1)
        ri_fail(ctx, "%s: expected one animation, found %u", name, (unsigned)animations.count);
    const size_t at = 0x14 + ri_get_u32(ctx, file, dict_entry(&animations, 0));
    const RiData block = ri_span(ctx, file, at, file.size - at);
    if (memcmp(ri_at(ctx, block, 0, 4), "M\0AT", 4) != 0)
        ri_fail(ctx, "%s: animation is not a texture SRT animation", name);
    NitroSrt out = {ri_get_u16(ctx, block, 4), ri_get_u8(ctx, block, 7), NULL, 0};
    if (out.duration == 0)
        ri_fail(ctx, "%s: animation has no frames", name);
    const NitroDict materials = dict(ctx, block, 8, name, "material");
    if (materials.size_unit < 40)
        ri_fail(ctx, "%s: SRT entries are %u bytes", name, (unsigned)materials.size_unit);
    out.count = materials.count;
    out.tracks = table(ctx, materials.count, sizeof(NitroSrtTrack));
    for (uint32_t index = 0; index < materials.count; ++index)
    {
        NitroSrtTrack *track = &out.tracks[index];
        dict_name(ctx, block, &materials, index, track->material);
        const size_t entry = dict_entry(&materials, index);
        Track parts[5];
        for (unsigned part = 0; part < 5; ++part)
            parts[part] = (Track){ri_get_u32(ctx, block, entry + part * 8), ri_get_u32(ctx, block, entry + part * 8 + 4)};
        track->frames = ri_buf_zero(ctx, (size_t)out.duration * sizeof(NitroSrtFrame));
        NitroSrtFrame *frames = (NitroSrtFrame *)(void *)track->frames->data;
        for (uint32_t frame = 0; frame < out.duration; ++frame)
        {
            int32_t value[2];
            track_sample(ctx, block, parts[0], out.duration, frame, 0, value, name);
            frames[frame].scale_s = value[0];
            track_sample(ctx, block, parts[1], out.duration, frame, 0, value, name);
            frames[frame].scale_t = value[0];
            track_sample(ctx, block, parts[2], out.duration, frame, 1, value, name);
            frames[frame].sin_r = value[0];
            frames[frame].cos_r = value[1];
            track_sample(ctx, block, parts[3], out.duration, frame, 0, value, name);
            frames[frame].translate_s = value[0];
            track_sample(ctx, block, parts[4], out.duration, frame, 0, value, name);
            frames[frame].translate_t = value[0];
        }
    }
    return out;
}

/* ---- Texture pattern animation ------------------------------------------ */

static void name_at(RiCtx *ctx, RiData block, size_t at, char out[NITRO_NAME_CAPACITY])
{
    const uint8_t *name = ri_at(ctx, block, at, 16);
    size_t length = 0;
    while (length < 16 && name[length])
        ++length;
    memcpy(out, name, length);
    out[length] = 0;
}

NitroPattern nitro_btp(RiCtx *ctx, RiData file, const char *name)
{
    header(ctx, file, "BTP0", 1, name);
    if (ri_get_u16(ctx, file, 14) != 1 || ri_get_u32(ctx, file, 16) != 0x14 ||
        memcmp(ri_at(ctx, file, 0x14, 4), "PAT0", 4) != 0)
        ri_fail(ctx, "%s: expected one PAT0 block", name);
    const NitroDict animations = dict(ctx, file, 0x1C, name, "animation");
    if (animations.count == 0)
        ri_fail(ctx, "%s: no pattern animation", name);
    const size_t at = 0x14 + ri_get_u32(ctx, file, dict_entry(&animations, 0));
    const RiData block = ri_span(ctx, file, at, file.size - at);
    if (memcmp(ri_at(ctx, block, 0, 4), "M\0PT", 4) != 0)
        ri_fail(ctx, "%s: animation is not a texture pattern animation", name);
    NitroPattern out = {ri_get_u16(ctx, block, 4), NULL, 0};
    const uint32_t texture_count = ri_get_u8(ctx, block, 6), palette_count = ri_get_u8(ctx, block, 7);
    const size_t texture_names = ri_get_u16(ctx, block, 8), palette_names = ri_get_u16(ctx, block, 10);
    if (out.duration == 0)
        ri_fail(ctx, "%s: pattern animation has no frames", name);
    const NitroDict materials = dict(ctx, block, 12, name, "material");
    out.count = materials.count;
    out.tracks = table(ctx, materials.count, sizeof(NitroPatternTrack));
    for (uint32_t index = 0; index < materials.count; ++index)
    {
        NitroPatternTrack *track = &out.tracks[index];
        dict_name(ctx, block, &materials, index, track->material);
        const size_t entry = dict_entry(&materials, index);
        track->count = ri_get_u16(ctx, block, entry);
        const size_t keys = ri_get_u16(ctx, block, entry + 6);
        if (track->count == 0)
            ri_fail(ctx, "%s: material %s has no pattern keys", name, track->material);
        track->keys = table(ctx, track->count, sizeof(NitroPatternKey));
        for (size_t key = 0; key < track->count; ++key)
        {
            NitroPatternKey *item = &track->keys[key];
            item->frame = ri_get_u16(ctx, block, keys + key * 4);
            const uint32_t texture = ri_get_u8(ctx, block, keys + key * 4 + 2);
            const uint32_t palette = ri_get_u8(ctx, block, keys + key * 4 + 3);
            if (texture >= texture_count || palette >= palette_count)
                ri_fail(ctx, "%s: material %s key %zu names a missing texture or palette", name, track->material, key);
            if (key ? item->frame <= track->keys[key - 1].frame : item->frame != 0)
                ri_fail(ctx, "%s: material %s pattern keys are not ordered from frame 0", name, track->material);
            name_at(ctx, block, texture_names + (size_t)texture * 16, item->texture);
            name_at(ctx, block, palette_names + (size_t)palette * 16, item->palette);
        }
    }
    return out;
}

/* ---- Texture matrices ---------------------------------------------------- */

static int64_t q16_mul(int64_t a, int64_t b)
{
    const int64_t product = a * b;
    return product >= 0 ? (product + 32768) >> 16 : -((-product + 32768) >> 16);
}

/* Maya and 3ds Max texture matrices (noclip nns_g3d calcTexMtx_*) with the
 * texel scale factored out, as texture coordinates are already normalized. */
void nitro_texture_matrix(RiCtx *ctx, uint32_t mode, const NitroSrtFrame *srt, int32_t out[6], const char *what)
{
    const int64_t one = 65536, half = 32768;
    const int64_t ss = (int64_t)srt->scale_s * 16, st = (int64_t)srt->scale_t * 16;
    const int64_t sn = (int64_t)srt->sin_r * 16, cs = (int64_t)srt->cos_r * 16;
    const int64_t ts = (int64_t)srt->translate_s * 16, tt = (int64_t)srt->translate_t * 16;
    int64_t m[6] = {q16_mul(ss, cs), -q16_mul(st, sn), q16_mul(ss, sn), q16_mul(st, cs), 0, 0};
    if (mode == NITRO_TEXMTX_MAYA)
    {
        m[4] = q16_mul(ss, -q16_mul(half, cs) - (q16_mul(half, sn) - half) - ts);
        m[5] = q16_mul(st, -q16_mul(half, cs) + (q16_mul(half, sn) - half) + tt) + one;
    }
    else if (mode == NITRO_TEXMTX_MAX)
    {
        m[4] = q16_mul(q16_mul(ss, -cs), ts + half) + q16_mul(q16_mul(ss, sn), tt - half) + half;
        m[5] = q16_mul(q16_mul(st, sn), ts + half) + q16_mul(q16_mul(st, cs), tt - half) + half;
    }
    else
        ri_fail(ctx, "%s uses texture matrix mode %u", what, (unsigned)mode);
    for (unsigned i = 0; i < 6; ++i)
    {
        if (m[i] < INT32_MIN || m[i] > INT32_MAX)
            ri_fail(ctx, "%s texture matrix overflows", what);
        out[i] = (int32_t)m[i];
    }
}
