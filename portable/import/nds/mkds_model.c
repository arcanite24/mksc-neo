#include "mkds_model.h"
#include "fixed_3d.h"

#include <stdio.h>
#include <string.h>

static const struct
{
    const char *tag;
    uint32_t stride;
} table_layout[MKDS_T_COUNT] = {
    {"STRS", 1},  {"META", 128}, {"VERT", 32}, {"VCOL", 4},  {"TRIS", 16}, {"MATS", 48}, {"COLV", 12},
    {"COLT", 32}, {"SURF", 48},  {"CPNT", 64}, {"CPED", 4},  {"RTHD", 32}, {"RTND", 32}, {"RTED", 4},
    {"GRID", 32}, {"PROP", 32},  {"TXAN", 32}, {"TXSR", 24}, {"TXPT", 8},  {"RESP", 48}, {"ITEM", 32}, {"CAMR", 64},
    {"OBJS", 64}, {"ZONE", 80},
};

void mkds_model_init(MkdsModel *c, RiCtx *ctx, const char *where, uint32_t texture_base,
                     const int *required, size_t required_count, uint32_t material_limit)
{
    memset(c, 0, sizeof(*c));
    c->ctx = ctx;
    snprintf(c->where, sizeof(c->where), "%s", where);
    c->texture_base = texture_base;
    for (int t = 0; t < MKDS_T_COUNT; ++t)
    {
        memcpy(c->tables[t].tag, table_layout[t].tag, 5);
        c->tables[t].stride = table_layout[t].stride;
        c->tables[t].bytes = ri_buf(ctx);
    }
    for (size_t index = 0; index < required_count; ++index)
        c->tables[required[index]].required = 1;
    c->plan_capacity = material_limit;
    c->plans = (MkdsMaterialPlan *)(void *)ri_buf_zero(ctx, (material_limit + 1) * sizeof(MkdsMaterialPlan))->data;
    c->textures = (MkdsTexture *)(void *)ri_buf_zero(ctx, MKDS_TEXTURES_PER_MODEL * sizeof(MkdsTexture))->data;
}

/* ---- Wire helpers ------------------------------------------------------- */

void mkds_words(MkdsModel *c, int table, const uint32_t *values, size_t count)
{
    MkdsTable *t = &c->tables[table];
    if (count * 4 != t->stride)
        ri_fail(c->ctx, "%s: %s row has %zu words", c->where, t->tag, count);
    for (size_t index = 0; index < count; ++index)
        ri_u32(c->ctx, t->bytes, values[index]);
    t->count++;
}

uint32_t mkds_string(MkdsModel *c, const char *text)
{
    MkdsTable *t = &c->tables[MKDS_T_STRS];
    const uint32_t offset = t->count;
    ri_put(c->ctx, t->bytes, text, strlen(text) + 1);
    t->count += (uint32_t)strlen(text) + 1;
    return offset;
}

uint32_t mkds_property(MkdsModel *c, const char *key, uint32_t type, uint32_t a, uint32_t b)
{
    const uint32_t row[8] = {mkds_string(c, key), type, a, b, 0, 0, 0, 0};
    mkds_words(c, MKDS_T_PROP, row, 8);
    return c->tables[MKDS_T_PROP].count - 1;
}

uint32_t mkds_property_vector(MkdsModel *c, const char *key, const int32_t value[3])
{
    const uint32_t row[8] = {mkds_string(c, key), MKDS_PROPERTY_VECTOR, (uint32_t)value[0], (uint32_t)value[1],
                             (uint32_t)value[2], 0, 0, 0};
    mkds_words(c, MKDS_T_PROP, row, 8);
    return c->tables[MKDS_T_PROP].count - 1;
}

int32_t mkds_round_div(int64_t value, int64_t divisor)
{
    const int64_t half = divisor / 2;
    return (int32_t)(value >= 0 ? (value + half) / divisor : -((-value + half) / divisor));
}

/* fx32 map units (collision and map data) to Q16 metres. */
int32_t mkds_units(MkdsModel *c, int64_t map)
{
    const int32_t out = mkds_round_div(map * 16, MKDS_UNITS_PER_METRE);
    if (out < -(4096 << 16) || out > (4096 << 16))
        ri_fail(c->ctx, "%s: geometry lies beyond 4096 m", c->where);
    return out;
}

/* fx32 model units (drawn MKDS_MODEL_SCALE times larger) to Q16 metres. */
int32_t mkds_metres(MkdsModel *c, int32_t world)
{
    return mkds_units(c, (int64_t)world * MKDS_MODEL_SCALE);
}

int64_t mkds_q16_mul(int64_t a, int64_t b)
{
    const int64_t product = a * b;
    return product >= 0 ? (product + 32768) >> 16 : -((-product + 32768) >> 16);
}

/* Weld table: identical encoded rows share one index. */
uint32_t mkds_weld(MkdsModel *c, int which, const uint8_t *key, size_t size, int table)
{
    if (c->weld_capacity[which] == 0)
    {
        c->weld_capacity[which] = 1u << 16;
        c->weld[which] = (uint32_t *)(void *)ri_buf_zero(c->ctx, (size_t)c->weld_capacity[which] * 4)->data;
    }
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < size; ++index)
        hash = (hash ^ key[index]) * 16777619u;
    const uint32_t mask = c->weld_capacity[which] - 1;
    const MkdsTable *t = &c->tables[table];
    for (uint32_t slot = hash & mask;; slot = (slot + 1) & mask)
    {
        const uint32_t stored = c->weld[which][slot];
        if (stored == 0)
        {
            if (t->count >= 131072u || t->count * 2 >= c->weld_capacity[which])
                ri_fail(c->ctx, "%s: more than %u distinct vertices", c->where, (unsigned)(c->weld_capacity[which] / 2));
            c->weld[which][slot] = t->count + 1;
            return MKDS_ABSENT;
        }
        const uint32_t index = stored - 1;
        const int same = which == 0
            ? memcmp(t->bytes->data + (size_t)index * 32, key, 32) == 0 &&
                  memcmp(c->tables[MKDS_T_VCOL].bytes->data + (size_t)index * 4, key + 32, 4) == 0 &&
                  (size <= 36 || c->vertex_nodes->data[index] == key[36])
            : memcmp(t->bytes->data + (size_t)index * 12, key, 12) == 0;
        if (same)
            return index;
    }
}

void mkds_put_word(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

/* ---- Textures ------------------------------------------------------------- */

/* Mirrored repeat is baked by appending the flipped image along that axis;
 * the material then halves its coordinates on that axis. */
static RiBuf *mirrored(RiCtx *ctx, RiBuf *pixels, uint32_t width, uint32_t height, uint32_t mirror,
                       uint32_t *out_width, uint32_t *out_height)
{
    const uint32_t w = mirror & 1u ? width * 2 : width, h = mirror & 2u ? height * 2 : height;
    RiBuf *out = ri_buf_zero(ctx, (size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint32_t sx = x < width ? x : 2 * width - 1 - x, sy = y < height ? y : 2 * height - 1 - y;
            ri_set_pixel(out, (size_t)y * w + x, ri_pixel(pixels, (size_t)sy * width + sx));
        }
    *out_width = w;
    *out_height = h;
    return out;
}

MkdsTexture *mkds_texture(MkdsModel *c, const NitroTextures *set, const NitroTexture *texture,
                          const NitroPalette *palette, uint32_t mirror, const char *material)
{
    if (texture->format != NITRO_TEXTURE_DIRECT && palette == NULL)
        ri_fail(c->ctx, "%s: material %s texture %s has no palette", c->where, material, texture->name);
    for (uint32_t index = 0; index < c->texture_count; ++index)
    {
        MkdsTexture *entry = &c->textures[index];
        if (entry->texture == texture && entry->palette == palette && entry->mirror == mirror)
            return entry;
    }
    if (c->texture_count >= MKDS_TEXTURES_PER_MODEL)
        ri_fail(c->ctx, "%s: more than %u textures", c->where, MKDS_TEXTURES_PER_MODEL);
    MkdsTexture *entry = &c->textures[c->texture_count];
    entry->set = set;
    entry->texture = texture;
    entry->palette = palette;
    entry->mirror = mirror;
    entry->id = c->texture_base | c->texture_count;
    RiBuf *pixels = nitro_texture_argb(c->ctx, set, texture, palette);
    for (size_t index = 0; index < (size_t)texture->width * texture->height; ++index)
    {
        const uint32_t alpha = ri_pixel(pixels, index) >> 24;
        entry->transparent |= alpha == 0;
        entry->translucent |= alpha != 0 && alpha != 255;
    }
    uint32_t width = texture->width, height = texture->height;
    if (mirror)
        pixels = mirrored(c->ctx, pixels, texture->width, texture->height, mirror, &width, &height);
    entry->image = ri_image_asset(c->ctx, width, height, pixels, 0, 0);
    c->texture_count++;
    return entry;
}

uint32_t mkds_image(MkdsModel *c, const RiBuf *pixels, uint32_t width, uint32_t height)
{
    if (c->texture_count >= MKDS_TEXTURES_PER_MODEL)
        ri_fail(c->ctx, "%s: more than %u textures", c->where, MKDS_TEXTURES_PER_MODEL);
    MkdsTexture *entry = &c->textures[c->texture_count];
    memset(entry, 0, sizeof(*entry));
    entry->id = c->texture_base | c->texture_count;
    entry->image = ri_image_asset(c->ctx, width, height, pixels, 0, 0);
    c->texture_count++;
    return entry->id;
}

/* A material's texture and palette by name from its set. */
static MkdsTexture *named_texture(MkdsModel *c, const NitroTextures *set, const char *texture_name,
                                  const char *palette_name, uint32_t mirror, const char *material)
{
    const NitroTexture *texture = nitro_find_texture(set, texture_name);
    if (texture == NULL)
        ri_fail(c->ctx, "%s: material %s uses missing texture %s", c->where, material, texture_name);
    const NitroPalette *palette = NULL;
    if (texture->format != NITRO_TEXTURE_DIRECT)
    {
        palette = nitro_find_palette(set, palette_name);
        if (palette == NULL)
            ri_fail(c->ctx, "%s: material %s uses missing palette %s", c->where, material, palette_name);
    }
    return mkds_texture(c, set, texture, palette, mirror, material);
}

/* ---- Materials -------------------------------------------------------------- */

static uint32_t expand5(uint32_t value)
{
    return value << 3 | value >> 2;
}

/* Applies the mirror halving to a texture matrix's rows. */
static void halve(int32_t m[6], uint32_t mirror)
{
    if (mirror & 1u)
    {
        m[0] /= 2;
        m[2] /= 2;
        m[4] /= 2;
    }
    if (mirror & 2u)
    {
        m[1] /= 2;
        m[3] /= 2;
        m[5] /= 2;
    }
}

static const NitroSrtTrack *srt_track(const NitroSrt *srt, const char *material)
{
    for (size_t index = 0; srt && index < srt->count; ++index)
        if (strcmp(srt->tracks[index].material, material) == 0)
            return &srt->tracks[index];
    return NULL;
}

static const NitroPatternTrack *pattern_track(const NitroPattern *pattern, const char *material)
{
    for (size_t index = 0; pattern && index < pattern->count; ++index)
        if (strcmp(pattern->tracks[index].material, material) == 0)
            return &pattern->tracks[index];
    return NULL;
}

void mkds_materials(MkdsModel *c, const NitroModel *model, const NitroTextures *set, const NitroSrt *srt,
                      const NitroPattern *pattern, uint32_t sky)
{
    mkds_materials_at_frame(c, model, set, srt, pattern, sky, MKDS_ABSENT);
}

void mkds_materials_at_frame(MkdsModel *c, const NitroModel *model, const NitroTextures *set,
                             const NitroSrt *srt, const NitroPattern *pattern, uint32_t sky,
                             uint32_t pattern_frame)
{
    if (pattern_frame != MKDS_ABSENT && (!pattern || pattern_frame >= pattern->duration))
        ri_fail(c->ctx, "%s: fixed pattern frame %u is outside its resource", c->where, pattern_frame);
    for (uint32_t index = 0; index < model->material_count; ++index)
    {
        const NitroMaterial *m = &model->materials[index];
        const uint32_t row_index = c->tables[MKDS_T_MATS].count;
        if (row_index >= c->plan_capacity)
            ri_fail(c->ctx, "%s: more than %u materials", c->where, (unsigned)c->plan_capacity);
        MkdsMaterialPlan *plan = &c->plans[row_index];
        plan->source = m;
        plan->sky = sky;
        char what[96];
        snprintf(what, sizeof(what), "%s material %s", c->where, m->name);
        const uint32_t alpha = m->polygon_attr >> 16 & 31u, cull = m->polygon_attr >> 6 & 3u;
        if (alpha == 0)
            ri_fail(c->ctx, "%s draws wireframe polygons", what);
        plan->skip = cull == 0;
        plan->reverse = cull == 1;
        const uint32_t param = m->teximage_param, texgen = param >> 30;
        plan->mirror = (param >> 16 & 1u && param >> 18 & 1u ? 1u : 0u) |
                       (param >> 17 & 1u && param >> 19 & 1u ? 2u : 0u);
        uint32_t flags = (param >> 16 & 1u ? MKDS_MATERIAL_WRAP_U : 0u) | (param >> 17 & 1u ? MKDS_MATERIAL_WRAP_V : 0u) |
                         (cull == 3 ? MKDS_MATERIAL_TWO_SIDED : 0u) | ((m->polygon_attr & 15u) == 0 ? MKDS_MATERIAL_UNLIT : 0u);
        uint32_t texture = MKDS_ABSENT, transparent = 0, translucent = alpha < 31;
        const NitroPatternTrack *keys = pattern_track(pattern, m->name);
        const char *texture_name = m->texture, *palette_name = m->palette;
        if (keys && pattern_frame != MKDS_ABSENT)
        {
            /* NSBTP also stores selectable appearances. Hold the last key at
             * or before the chosen frame; never emit a clock for that choice. */
            for (size_t key = 0; key < keys->count; ++key)
            {
                if (keys->keys[key].frame >= pattern->duration)
                    ri_fail(c->ctx, "%s pattern key lies past the animation", what);
                if (keys->keys[key].frame <= pattern_frame)
                {
                    texture_name = keys->keys[key].texture;
                    palette_name = keys->keys[key].palette;
                }
            }
        }
        if (texture_name[0])
        {
            const MkdsTexture *entry = named_texture(c, set, texture_name, palette_name, plan->mirror, m->name);
            texture = entry->id;
            transparent = entry->transparent;
            translucent |= entry->translucent;
        }
        flags |= translucent ? MKDS_MATERIAL_BLEND : transparent ? MKDS_MATERIAL_MASK : 0u;

        const NitroSrtTrack *animation = srt_track(srt, m->name);
        if (texgen > 1)
            ri_fail(c->ctx, "%s generates texture coordinates from normals or positions", what);
        const NitroSrtFrame still = {m->scale_s, m->scale_t, m->sin_r, m->cos_r, m->translate_s, m->translate_t};
        static const NitroSrtFrame none = {4096, 4096, 0, 4096, 0, 0};
        nitro_texture_matrix(c->ctx, model->texmtx_mode, animation || texgen == 0 ? &none : &still, plan->uv, what);
        halve(plan->uv, plan->mirror);
        if (animation)
        {
            const uint32_t first = c->tables[MKDS_T_TXSR].count;
            const NitroSrtFrame *frames = (const NitroSrtFrame *)(const void *)animation->frames->data;
            for (uint32_t frame = 0; frame < srt->duration; ++frame)
            {
                int32_t matrix[6];
                nitro_texture_matrix(c->ctx, model->texmtx_mode, &frames[frame], matrix, what);
                halve(matrix, plan->mirror);
                mkds_words(c, MKDS_T_TXSR, (const uint32_t *)(const void *)matrix, 6);
            }
            const uint32_t row[8] = {row_index, 0, srt->duration, first, srt->duration, 0, 0, 0};
            mkds_words(c, MKDS_T_TXAN, row, 8);
        }
        if (keys && pattern_frame == MKDS_ABSENT)
        {
            const uint32_t first = c->tables[MKDS_T_TXPT].count;
            for (size_t key = 0; key < keys->count; ++key)
            {
                if (keys->keys[key].frame >= pattern->duration)
                    ri_fail(c->ctx, "%s pattern key lies past the animation", what);
                const MkdsTexture *entry =
                    named_texture(c, set, keys->keys[key].texture, keys->keys[key].palette, plan->mirror, m->name);
                if (entry->translucent && !(flags & MKDS_MATERIAL_BLEND))
                    flags = (flags & ~(uint32_t)MKDS_MATERIAL_MASK) | MKDS_MATERIAL_BLEND;
                else if (entry->transparent && !(flags & MKDS_MATERIAL_BLEND))
                    flags |= MKDS_MATERIAL_MASK;
                const uint32_t row[2] = {keys->keys[key].frame, entry->id};
                mkds_words(c, MKDS_T_TXPT, row, 2);
            }
            const uint32_t row[8] = {row_index, 1, pattern->duration, first, (uint32_t)keys->count, 0, 0, 0};
            mkds_words(c, MKDS_T_TXAN, row, 8);
        }
        /* Lit materials (any of lights 0-3 enabled) shade by their diffuse,
         * ambient and emission under the camera light; fog-enabled courses
         * fade materials with polygon attribute bit 15. */
        const int lit = !(flags & MKDS_MATERIAL_UNLIT) && !sky, fog = c->fog && (m->polygon_attr >> 15 & 1u) && !sky;
        uint32_t base = 0xFFFFFF00u | expand5(alpha), emission = 0x000000FFu;
        uint32_t property_first = sky ? c->sky_property : 0, property_count = sky ? 2u : 0u;
        if (lit || fog)
            property_first = c->tables[MKDS_T_PROP].count;
        if (lit)
        {
            base = (mkds_rgb_word(m->diffuse_ambient & 0x7FFFu) & 0xFFFFFF00u) | expand5(alpha);
            emission = mkds_rgb_word(m->specular_emission >> 16 & 0x7FFFu);
            mkds_property(c, "mksc:lit", MKDS_PROPERTY_BOOLEAN, 1, 0);
            mkds_property(c, "mksc:ambient", MKDS_PROPERTY_INTEGER, mkds_rgb_word(m->diffuse_ambient >> 16 & 0x7FFFu), 0);
            property_count += 2;
        }
        if (fog)
        {
            mkds_property(c, "mksc:fog", MKDS_PROPERTY_BOOLEAN, 1, 0);
            property_count++;
        }
        const uint32_t row[12] = {texture, MKDS_ABSENT, MKDS_ABSENT, MKDS_ABSENT, base, emission,
                                  0, 65536, 32768, flags, property_first, property_count};
        mkds_words(c, MKDS_T_MATS, row, 12);
    }
}

/* ---- Geometry ---------------------------------------------------------------- */

uint32_t mkds_rgb_word(uint32_t color)
{
    return expand5(color & 31u) << 24 | expand5(color >> 5 & 31u) << 16 | expand5(color >> 10 & 31u) << 8 | 255u;
}

MkdsCorner mkds_corner(MkdsModel *c, const NitroVertex *vertex, const MkdsMaterialPlan *plan)
{
    MkdsCorner out;
    /* DS axes are right-handed (x east, y up, z south); ours are left-handed
     * (x east, y south, z up), so this is a reflection: winding flips too. */
    if (c->placement)
    {
        const MkdsPlacement *place = c->placement;
        int64_t local[3], map[3];
        for (unsigned axis = 0; axis < 3; ++axis)
            local[axis] = mkds_round_div((int64_t)vertex->position[axis] * place->scale[axis] * MKDS_MODEL_SCALE, 4096);
        for (unsigned row = 0; row < 3; ++row)
            map[row] = mkds_round_div(place->r[row * 3] * local[0] + place->r[row * 3 + 1] * local[1] +
                                          place->r[row * 3 + 2] * local[2],
                                      4096) +
                       place->translate[row];
        out.position[0] = mkds_units(c, map[0]);
        out.position[1] = mkds_units(c, map[2]);
        out.position[2] = mkds_units(c, map[1]);
    }
    else if (plan->sky)
    {
        /* The world-placed sky may reach past the 4096 m position bound; its
         * outlying corners pull in toward the origin, so their direction
         * from the course stays and nearer sky geometry is unchanged. */
        const int64_t limit = (int64_t)4000 << 16;
        int64_t q[3], reach = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const int64_t scaled = (int64_t)vertex->position[axis] * MKDS_MODEL_SCALE * 16;
            q[axis] = scaled >= 0 ? (scaled + MKDS_UNITS_PER_METRE / 2) / MKDS_UNITS_PER_METRE
                                  : -((-scaled + MKDS_UNITS_PER_METRE / 2) / MKDS_UNITS_PER_METRE);
            reach = q[axis] > reach ? q[axis] : -q[axis] > reach ? -q[axis] : reach;
        }
        for (unsigned axis = 0; axis < 3 && reach > limit; ++axis)
            q[axis] = q[axis] * limit / reach;
        out.position[0] = (int32_t)q[0];
        out.position[1] = (int32_t)q[2];
        out.position[2] = (int32_t)q[1];
    }
    else
    {
        out.position[0] = mkds_metres(c, vertex->position[0]);
        out.position[1] = mkds_metres(c, vertex->position[2]);
        out.position[2] = mkds_metres(c, vertex->position[1]);
    }
    /* GX texcoords are Q4 texels; normalize by the material's extent. */
    if (plan->source->width == 0 || plan->source->height == 0)
        ri_fail(c->ctx, "%s: material %s has no texture extent", c->where, plan->source->name);
    const int64_t u = mkds_round_div((int64_t)vertex->texcoord[0] * 4096, plan->source->width);
    const int64_t v = mkds_round_div((int64_t)vertex->texcoord[1] * 4096, plan->source->height);
    const int64_t a = mkds_q16_mul(plan->uv[0], u) + mkds_q16_mul(plan->uv[2], v) + plan->uv[4];
    const int64_t b = mkds_q16_mul(plan->uv[1], u) + mkds_q16_mul(plan->uv[3], v) + plan->uv[5];
    if (a < -INT32_MAX || a > INT32_MAX || b < -INT32_MAX || b > INT32_MAX)
        ri_fail(c->ctx, "%s: material %s texture coordinates overflow", c->where, plan->source->name);
    out.uv[0] = (int32_t)a;
    out.uv[1] = (int32_t)b;
    /* A lit vertex's colour is its lighting; the display-list colour is unused. */
    out.color = mkds_rgb_word((plan->source->polygon_attr & 15u) ? 0x7FFFu : vertex->color);
    out.node = vertex->node;
    /* DS normals reflect like positions: (x, y, z) -> (x, z, y). A placed
     * model's normals take the inverse transpose of its scale (diag(sy*sz,
     * sx*sz, sx*sy) up to a factor) and then its rotation. */
    int64_t source[3] = {vertex->normal[0], vertex->normal[1], vertex->normal[2]};
    if (c->placement)
    {
        const MkdsPlacement *place = c->placement;
        const int64_t local[3] = {source[0] * place->scale[1] * place->scale[2] / 4096,
                                  source[1] * place->scale[0] * place->scale[2] / 4096,
                                  source[2] * place->scale[0] * place->scale[1] / 4096};
        for (unsigned row = 0; row < 3; ++row)
            source[row] = place->r[row * 3] * local[0] + place->r[row * 3 + 1] * local[1] + place->r[row * 3 + 2] * local[2];
    }
    const int64_t normal[3] = {source[0], source[2], source[1]};
    out.has_normal = vertex->has_normal && portable_fixed_3d_normalize(normal, out.normal);
    return out;
}

uint32_t mkds_render_vertex(MkdsModel *c, const MkdsCorner *corner, const int32_t normal[3])
{
    uint8_t key[37];
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        mkds_put_word(key + axis * 4, (uint32_t)corner->position[axis]);
        mkds_put_word(key + 12 + axis * 4, (uint32_t)normal[axis]);
    }
    mkds_put_word(key + 24, (uint32_t)corner->uv[0]);
    mkds_put_word(key + 28, (uint32_t)corner->uv[1]);
    mkds_put_word(key + 32, corner->color);
    key[36] = corner->node;
    const uint32_t found = mkds_weld(c, 0, key, c->vertex_nodes ? 37u : 36u, MKDS_T_VERT);
    if (found != MKDS_ABSENT)
        return found;
    if (c->vertex_nodes)
        ri_u8(c->ctx, c->vertex_nodes, corner->node);
    ri_put(c->ctx, c->tables[MKDS_T_VERT].bytes, key, 32);
    ri_put(c->ctx, c->tables[MKDS_T_VCOL].bytes, key + 32, 4);
    c->tables[MKDS_T_VCOL].count++;
    return c->tables[MKDS_T_VERT].count++;
}

uint32_t mkds_geometry(MkdsModel *c, const NitroModel *model, uint32_t first_material, int backdrop)
{
    const NitroMesh mesh = nitro_model_mesh(c->ctx, model, backdrop);
    if (mesh.billboard_nodes && !c->placement)
        ri_fail(c->ctx, "%s: %s has %u camera-facing billboard nodes", c->where, model->name,
                (unsigned)mesh.billboard_nodes);
    const NitroVertex *vertices = (const NitroVertex *)(const void *)mesh.vertices->data;
    const NitroTriangle *triangles = (const NitroTriangle *)(const void *)mesh.triangles->data;
    for (uint32_t index = 0; index < mesh.triangle_count; ++index)
    {
        const NitroTriangle *triangle = &triangles[index];
        const uint32_t material = first_material + triangle->material;
        const MkdsMaterialPlan *plan = &c->plans[material];
        if (plan->skip)
            continue;
        MkdsCorner corners[3];
        for (unsigned k = 0; k < 3; ++k)
            corners[k] = mkds_corner(c, &vertices[triangle->vertices[plan->reverse ? k : 2 - k]], plan);
        int64_t ab[3], ac[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            ab[axis] = (int64_t)corners[1].position[axis] - corners[0].position[axis];
            ac[axis] = (int64_t)corners[2].position[axis] - corners[0].position[axis];
        }
        const int64_t cross[3] = {ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                                  ab[0] * ac[1] - ab[1] * ac[0]};
        int32_t normal[3];
        if (!portable_fixed_3d_normalize(cross, normal))
            continue; /* degenerate after quantization (strip joins) */
        uint32_t row[4] = {0, 0, 0, material};
        for (unsigned k = 0; k < 3; ++k)
            row[k] = mkds_render_vertex(c, &corners[k], corners[k].has_normal ? corners[k].normal : normal);
        mkds_words(c, MKDS_T_TRIS, row, 4);
    }
    return mesh.billboard_nodes;
}


/* ---- Sections ------------------------------------------------------------------ */

RiBuf *mkds_model_blob(MkdsModel *c, const char *magic, uint32_t id)
{
    RiCtx *ctx = c->ctx;
    while (c->tables[MKDS_T_STRS].bytes->size % 4)
        ri_u8(ctx, c->tables[MKDS_T_STRS].bytes, 0);
    uint32_t present = 0;
    for (int t = 0; t < MKDS_T_COUNT; ++t)
        present += c->tables[t].count || c->tables[t].required;
    RiBuf *out = ri_buf(ctx);
    const uint32_t directory = 64, first = directory + present * 20;
    uint32_t offset = first;
    ri_put(ctx, out, magic, 4);
    ri_words(ctx, out, 6, 1u, 0u, id, 0u, directory, present);
    ri_put_zero(ctx, out, 9 * 4);
    for (int t = 0; t < MKDS_T_COUNT; ++t)
    {
        const MkdsTable *table = &c->tables[t];
        if (!table->count && !table->required)
            continue;
        ri_put(ctx, out, table->tag, 4);
        ri_words(ctx, out, 4, offset, table->count, table->stride, 1u);
        offset += (uint32_t)table->bytes->size;
    }
    for (int t = 0; t < MKDS_T_COUNT; ++t)
        ri_put_buf(ctx, out, c->tables[t].bytes);
    ri_set_u32(out, 8, (uint32_t)out->size);
    return out;
}

RiBuf *mkds_model_bundle(MkdsModel *c)
{
    RiCtx *ctx = c->ctx;
    RiBuf *out = ri_buf(ctx);
    ri_put(ctx, out, "TXB1", 4);
    ri_words(ctx, out, 3, 1u, 0u, c->texture_count);
    uint32_t offset = 16 + c->texture_count * 16;
    for (uint32_t index = 0; index < c->texture_count; ++index)
    {
        const RiBuf *image = c->textures[index].image;
        ri_words(ctx, out, 4, c->textures[index].id, offset, (uint32_t)image->size, 0u);
        offset += (uint32_t)image->size;
    }
    for (uint32_t index = 0; index < c->texture_count; ++index)
        ri_put_buf(ctx, out, c->textures[index].image);
    ri_set_u32(out, 8, (uint32_t)out->size);
    return out;
}

static const char *label(MkdsModel *c, const NdsFs *archive, const char *path)
{
    char text[160];
    snprintf(text, sizeof(text), "%s/%s", archive->name, path);
    return (const char *)ri_buf_copy(c->ctx, text, strlen(text) + 1)->data;
}

int mkds_resources(MkdsModel *c, const NdsFs *archive, const NdsFs *texture_archive, const char *stem,
                   MkdsResources *out)
{
    char path[64];
    RiData file;
    memset(out, 0, sizeof(*out));
    snprintf(path, sizeof(path), "%s.nsbmd", stem);
    if (!nds_find(archive, path, &file))
        return 0;
    const char *name = label(c, archive, path);
    out->models = nitro_bmd(c->ctx, file, name);
    if (out->models.count != 1)
        ri_fail(c->ctx, "%s holds %zu models", name, out->models.count);
    snprintf(path, sizeof(path), "%s.nsbtx", stem);
    if (texture_archive && nds_find(texture_archive, path, &file))
        out->textures = nitro_btx(c->ctx, file, label(c, texture_archive, path));
    else if (nds_find(archive, path, &file))
        out->textures = nitro_btx(c->ctx, file, label(c, archive, path));
    else if (out->models.has_textures)
        out->textures = out->models.textures;
    else
        ri_fail(c->ctx, "%s has no textures", name);
    snprintf(path, sizeof(path), "%s.nsbta", stem);
    if (nds_find(archive, path, &file))
    {
        out->srt = nitro_bta(c->ctx, file, label(c, archive, path));
        out->has_srt = 1;
    }
    snprintf(path, sizeof(path), "%s.nsbtp", stem);
    if (nds_find(archive, path, &file))
    {
        out->pattern = nitro_btp(c->ctx, file, label(c, archive, path));
        out->has_pattern = 1;
    }
    return 1;
}

/* ---- Rig poses ---------------------------------------------------------------- */

typedef struct Affine
{
    int64_t r[9], t[3]; /* fx (Q12), row-major, column vectors */
} Affine;

static Affine node_affine(const NitroNodeMatrix *node)
{
    Affine out;
    for (unsigned i = 0; i < 9; ++i)
        out.r[i] = node->evaluated ? node->r[i] : (i % 4 == 0 ? 4096 : 0);
    for (unsigned i = 0; i < 3; ++i)
        out.t[i] = node->evaluated ? node->t[i] : 0;
    return out;
}

static void multiply(const int64_t a[9], const int64_t b[9], int64_t out[9])
{
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned column = 0; column < 3; ++column)
            out[row * 3 + column] = mkds_round_div(a[row * 3] * b[column] + a[row * 3 + 1] * b[3 + column] +
                                                       a[row * 3 + 2] * b[6 + column],
                                                   4096);
}

static void apply(const int64_t r[9], const int64_t v[3], int64_t out[3])
{
    for (unsigned row = 0; row < 3; ++row)
        out[row] = mkds_round_div(r[row * 3] * v[0] + r[row * 3 + 1] * v[1] + r[row * 3 + 2] * v[2], 4096);
}

static void invert(MkdsModel *c, const int64_t r[9], int64_t out[9])
{
    const int64_t adjugate[9] = {
        r[4] * r[8] - r[5] * r[7], r[2] * r[7] - r[1] * r[8], r[1] * r[5] - r[2] * r[4],
        r[5] * r[6] - r[3] * r[8], r[0] * r[8] - r[2] * r[6], r[2] * r[3] - r[0] * r[5],
        r[3] * r[7] - r[4] * r[6], r[1] * r[6] - r[0] * r[7], r[0] * r[4] - r[1] * r[3],
    };
    const int64_t determinant = mkds_round_div(r[0] * adjugate[0] + r[1] * adjugate[3] + r[2] * adjugate[6], 4096);
    if (determinant == 0)
        ri_fail(c->ctx, "%s: a rest-pose node matrix is singular", c->where);
    for (unsigned i = 0; i < 9; ++i)
        out[i] = mkds_round_div(adjugate[i] * 4096, determinant);
}

void mkds_pose_relative(MkdsModel *c, const NitroNodeMatrix *posed, const NitroNodeMatrix *rest, int64_t r[9],
                        int64_t t[3])
{
    const Affine p = node_affine(posed), q = node_affine(rest);
    int64_t inverse[9], moved[3];
    invert(c, q.r, inverse);
    multiply(p.r, inverse, r);
    apply(r, q.t, moved);
    for (unsigned axis = 0; axis < 3; ++axis)
        t[axis] = p.t[axis] - moved[axis];
}

/* ---- Model meshes -------------------------------------------------------------- */

RiBuf *mkds_material_section(MkdsModel *m, uint32_t argb, uint32_t flags, const RiBuf *image)
{
    RiBuf *out = ri_buf(m->ctx);
    ri_put(m->ctx, out, "MTL1", 4);
    ri_words(m->ctx, out, 3, argb, flags, image ? (uint32_t)image->size : 0u);
    if (image)
        ri_put_buf(m->ctx, out, image);
    return out;
}

static uint32_t rgba_to_argb(uint32_t rgba)
{
    return (rgba & 0xFFu) << 24 | rgba >> 8;
}

void mkds_mesh_write(MkdsModel *m, uint32_t material_base, const RiBuf *vertices, uint32_t vertex_count,
                     const RiBuf *triangles, uint32_t triangle_count, uint32_t mesh_id)
{
    RiCtx *ctx = m->ctx;
    const MkdsTable *mats = &m->tables[MKDS_T_MATS];
    RiBuf *mesh = ri_buf(ctx);
    ri_put(ctx, mesh, "MSH1", 4);
    ri_words(ctx, mesh, 3, vertex_count, triangle_count, mats->count);
    /* One kind-97 section per MATS row, then the mesh's material list. */
    for (uint32_t slot = 0; slot < mats->count; ++slot)
    {
        const uint8_t *row = mats->bytes->data + slot * mats->stride;
        const uint32_t texture = ri_le32(row), flags = ri_le32(row + 36);
        const RiBuf *image = NULL;
        uint32_t argb = 0xFFFFFFFFu;
        if (texture != MKDS_ABSENT)
            image = m->textures[texture - m->texture_base].image;
        else
            argb = rgba_to_argb(mkds_rgb_word(m->plans[slot].source->diffuse_ambient & 0x7FFFu));
        const uint32_t wrap = (flags & (MKDS_MATERIAL_WRAP_U | MKDS_MATERIAL_WRAP_V |
                                        (m->mesh_blend ? MKDS_MATERIAL_BLEND : 0u))) |
                              (flags & MKDS_MATERIAL_UNLIT ? 0u : MKDS_MODEL_LIT) |
                              (m->plans[slot].source->polygon_attr >> 15 & 1u ? MKDS_MODEL_FOG : 0u);
        const uint32_t id = material_base | slot;
        ri_section(ctx, MKDS_KIND_MATERIAL, id, 1, mkds_material_section(m, argb, wrap, image));
        ri_u32(ctx, mesh, id);
    }
    ri_put_buf(ctx, mesh, vertices);
    ri_put_buf(ctx, mesh, triangles);
    ri_section(ctx, MKDS_KIND_MESH, mesh_id, 1, mesh);
}

/* ---- Object meshes --------------------------------------------------------- */

/* Model DS axes (x, y up, z) to engine axes (x, z, y). */
static const unsigned object_axis[3] = {0, 2, 1};

/* One node's pose relative to its rest pose in object-mesh axes (DS x, z, y)
 * and metres, as a row-major 3x4 Q16 matrix. */
static void object_pose_row(MkdsModel *o, const NitroNodeMatrix *posed, const NitroNodeMatrix *rest, RiBuf *out)
{
    int64_t r[9], t[3];
    mkds_pose_relative(o, posed, rest, r, t);
    for (unsigned row = 0; row < 3; ++row)
    {
        for (unsigned column = 0; column < 3; ++column)
            ri_u32(o->ctx, out, (uint32_t)(int32_t)(r[object_axis[row] * 3 + object_axis[column]] * 16));
        ri_u32(o->ctx, out, (uint32_t)mkds_units(o, t[object_axis[row]] * MKDS_MODEL_SCALE));
    }
}

MkdsObjectMesh mkds_object_mesh(RiCtx *ctx, const char *where, const MkdsResources *model,
                                const NitroJointAnimation *animation, uint32_t mesh_id, uint32_t material_base,
                                int blend)
{
    const NitroModel *nitro = &model->models.models[0];
    MkdsModel o;
    mkds_model_init(&o, ctx, where, 0, NULL, 0, 15);
    o.mesh_blend = blend != 0;
    mkds_materials(&o, nitro, &model->textures, model->has_srt ? &model->srt : NULL,
                   model->has_pattern ? &model->pattern : NULL, 0);
    if (animation)
    {
        if (!nitro->node_count || nitro->node_count > 64)
            ri_fail(ctx, "%s: animated model has %u nodes (rigs hold 1..64)", where, (unsigned)nitro->node_count);
        o.vertex_nodes = ri_buf(ctx);
    }
    MkdsPlacement identity = {{4096, 4096, 4096}, {4096, 0, 0, 0, 4096, 0, 0, 0, 4096}, {0, 0, 0}};
    o.placement = &identity;
    const uint32_t camera_facing = mkds_geometry(&o, nitro, 0, 0);
    const uint32_t billboard = camera_facing && nitro->node_count == 1u && !animation;
    if (camera_facing && !billboard)
    {
        if (animation)
            ri_fail(ctx, "%s: animated model has camera-facing nodes", where);
        /* Camera-facing parts of a larger model become crossed planes, as for scenery. */
        MkdsPlacement turned = {{4096, 4096, 4096}, {0, 0, 4096, 0, 4096, 0, -4096, 0, 0}, {0, 0, 0}};
        o.placement = &turned;
        mkds_geometry(&o, nitro, 0, 0);
    }
    const MkdsTable *vert = &o.tables[MKDS_T_VERT], *tris = &o.tables[MKDS_T_TRIS];
    if (vert->count > 2048 || tris->count > 4096 || !tris->count)
        ri_fail(ctx, "%s: %u vertices and %u triangles (meshes hold 1..4096 triangles over 2048 vertices)", where,
                (unsigned)vert->count, (unsigned)tris->count);
    RiBuf *vertices = ri_buf(ctx), *triangles = ri_buf(ctx);
    for (uint32_t index = 0; index < vert->count; ++index)
    {
        ri_put(ctx, vertices, vert->bytes->data + index * 32, 12);
        ri_put(ctx, vertices, vert->bytes->data + index * 32 + 24, 8);
    }
    for (uint32_t index = 0; index < tris->count; ++index)
        for (unsigned k = 0; k < 4; ++k)
            ri_u16(ctx, triangles, (uint16_t)ri_le32(tris->bytes->data + index * 16 + k * 4));
    const MkdsObjectMesh mesh = {mesh_id, vert->count, tris->count, billboard, vertices->data,
                                 (const uint16_t *)(const void *)triangles->data};
    mkds_mesh_write(&o, material_base, vertices, vert->count, triangles, tris->count, mesh.id);
    if (animation)
    {
        /* NITRO_NO_NODE (unplaced vertices) is also RIG1's static node. */
        NitroNodeMatrix rest[64], posed[64];
        nitro_model_pose(ctx, nitro, NULL, 0, rest);
        RiBuf *poses = ri_buf(ctx);
        for (uint32_t frame = 0; frame < animation->frames; ++frame)
        {
            nitro_model_pose(ctx, nitro, animation, frame, posed);
            for (uint32_t node = 0; node < nitro->node_count; ++node)
                object_pose_row(&o, &posed[node], &rest[node], poses);
        }
        RiBuf *rig = ri_buf(ctx);
        ri_put(ctx, rig, "RIG1", 4);
        ri_words(ctx, rig, 7, vert->count, (uint32_t)nitro->node_count, 1u,
                 animation->frames * (uint32_t)nitro->node_count, 0u, 0u, 0u);
        ri_put_buf(ctx, rig, o.vertex_nodes);
        ri_put_zero(ctx, rig, (4u - vert->count % 4u) % 4u);
        ri_put(ctx, rig, "IDLE", 4);
        ri_words(ctx, rig, 3, animation->frames, 0u, 1u /* loop */);
        ri_put_buf(ctx, rig, poses);
        ri_section(ctx, MKDS_KIND_RIG, mesh.id, 1, rig);
    }
    return mesh;
}

/* ---- Shadow volumes ------------------------------------------------------ */

#define SHADOW_SAMPLES 4u

static int covers(const NitroVertex *a, const NitroVertex *b, const NitroVertex *c, double x, double z)
{
    const double d0 = (b->position[0] - a->position[0]) * (z - a->position[2]) -
                      (b->position[2] - a->position[2]) * (x - a->position[0]);
    const double d1 = (c->position[0] - b->position[0]) * (z - b->position[2]) -
                      (c->position[2] - b->position[2]) * (x - b->position[0]);
    const double d2 = (a->position[0] - c->position[0]) * (z - c->position[2]) -
                      (a->position[2] - c->position[2]) * (x - c->position[0]);
    return (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
}

RiBuf *mkds_shadow_plan(RiCtx *ctx, const char *where, const NitroModel *model, int mirror_x, int32_t low[3],
                        int32_t high[3])
{
    const NitroMesh mesh = nitro_model_mesh(ctx, model, 0);
    const NitroVertex *vertices = (const NitroVertex *)(const void *)mesh.vertices->data;
    const NitroTriangle *triangles = (const NitroTriangle *)(const void *)mesh.triangles->data;
    if (!mesh.triangle_count)
        ri_fail(ctx, "%s: shadow volume has no triangles", where);
    for (unsigned axis = 0; axis < 3; ++axis)
        low[axis] = high[axis] = vertices[triangles[0].vertices[0]].position[axis];
    for (uint32_t t = 0; t < mesh.triangle_count; ++t)
        for (unsigned k = 0; k < 3; ++k)
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const int32_t v = vertices[triangles[t].vertices[k]].position[axis];
                low[axis] = v < low[axis] ? v : low[axis];
                high[axis] = v > high[axis] ? v : high[axis];
            }
    if (low[1] >= 0 || high[0] <= low[0] || high[2] <= low[2])
        ri_fail(ctx, "%s: shadow volume does not reach below its origin", where);
    const double cx = (low[0] + high[0]) / 2.0, cz = (low[2] + high[2]) / 2.0;
    const double hx = (high[0] - low[0]) / 2.0 * (mirror_x ? 1 : -1), hz = (high[2] - low[2]) / 2.0;
    RiBuf *pixels = ri_buf_zero(ctx, MKDS_SHADOW_TEXELS * MKDS_SHADOW_TEXELS * 4u);
    for (uint32_t y = 0; y < MKDS_SHADOW_TEXELS; ++y)
        for (uint32_t x = 0; x < MKDS_SHADOW_TEXELS; ++x)
        {
            uint32_t hits = 0;
            for (uint32_t s = 0; s < SHADOW_SAMPLES * SHADOW_SAMPLES; ++s)
            {
                const double u = (x + (s % SHADOW_SAMPLES + .5) / SHADOW_SAMPLES) / MKDS_SHADOW_TEXELS;
                const double v = (y + (s / SHADOW_SAMPLES + .5) / SHADOW_SAMPLES) / MKDS_SHADOW_TEXELS;
                const double px = cx + hx * (1 - 2 * u), pz = cz + hz * (1 - 2 * v);
                for (uint32_t t = 0; t < mesh.triangle_count; ++t)
                    if (covers(&vertices[triangles[t].vertices[0]], &vertices[triangles[t].vertices[1]],
                               &vertices[triangles[t].vertices[2]], px, pz))
                    {
                        ++hits;
                        break;
                    }
            }
            ri_set_pixel(pixels, (size_t)y * MKDS_SHADOW_TEXELS + x,
                         (hits * 255u / (SHADOW_SAMPLES * SHADOW_SAMPLES)) << 24 | 0xFFFFFFu);
        }
    return pixels;
}
