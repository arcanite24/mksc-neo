#include "mkds_driver.h"
#include "mkds_model.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>

/* One DS model unit in source world pixels: the race draws drivers at .05 m
 * per pixel times the 0.72 kart art scale, so the Standard kart is 0.87 m
 * across on the 1 m collision sphere. */
#define PIXELS_PER_UNIT 28
/* fx (Q12) model units to Q16 pixels. */
#define PIXEL_Q16 (PIXELS_PER_UNIT * 16)
/* kartoffsetdata places tires in world units, 16 per model unit. */
#define TIRE_POSITION_SCALE 16
/* The game picks blinks itself; ours close the eyes for the last frames of a
 * fixed cycle. */
#define BLINK_PERIOD 150u
#define BLINK_FRAMES 6u
#define MESH_MATERIAL_LIMIT 15u /* slot 15 of the ID range is the portrait */
#define MAX_NODES 32u
#define KART_ENTRY_BYTES 224u
#define KART_ENTRY_COUNT MKDS_KART_COUNT
#define CHARACTER_COUNT 13u

enum
{
    KIND_RIG = 109,
    KIND_DRIVER = 110,
    KIND_KART = 111,
    DRIVER_SELECTABLE = 1,
    KART_SELECTABLE = 1,
    KART_OWNER_DEFAULT = 2,
    KART_NO_SHADOW = 4, /* the DS draws its own shadow volume, never a blob */
    CLIP_LOOP = 1,
};
/* kartphysicalparam.bin in KartModelMenu: 152-byte rows (MKDS-decomp-headers
 * physp_kart_params_t), one per kart in kartoffsetdata order. */
#define PHYSICS_ROW_BYTES 152u
#define PHYSICS_WEIGHT 12u   /* fx16 */
#define PHYSICS_SPEED 16u    /* fx32 maxSpeed */
#define PHYSICS_ACCEL 20u    /* fx32 baseAcceleration */
#define PHYSICS_HANDLING 48u /* fx16 */

typedef struct Driver
{
    MkdsModel m;
    uint32_t material_base; /* kind-97 IDs are material_base | slot */
    RiBuf *vertices, *nodes, *triangles;
    uint32_t vertex_count, triangle_count, material_count;
    int32_t lowest; /* Q16 pixels */
} Driver;

static void put_i32(RiCtx *ctx, RiBuf *buffer, int64_t value, const char *where)
{
    if (value < INT32_MIN || value > INT32_MAX)
        ri_fail(ctx, "%s: value %lld out of range", where, (long long)value);
    ri_u32(ctx, buffer, (uint32_t)(int32_t)value);
}

/* DS axes (x left of the kart, y up, z forward) to ours (+X right, -Y
 * forward, +Z up); a reflection, so triangles reverse their winding. */
static void engine_axes(const int64_t ds[3], int64_t out[3])
{
    out[0] = -ds[0];
    out[1] = -ds[2];
    out[2] = ds[1];
}

/* Welds identical rows: MSH1 has no normals, so position, UV and the
 * skinning node decide. */
static uint32_t add_vertex(Driver *d, const int64_t position[3], const int32_t uv[2], uint8_t node)
{
    uint8_t row[20];
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        if (position[axis] < INT32_MIN || position[axis] > INT32_MAX)
            ri_fail(d->m.ctx, "%s: vertex out of range", d->m.where);
        mkds_put_word(row + axis * 4, (uint32_t)(int32_t)position[axis]);
    }
    mkds_put_word(row + 12, (uint32_t)uv[0]);
    mkds_put_word(row + 16, (uint32_t)uv[1]);
    for (uint32_t index = 0; index < d->vertex_count; ++index)
        if (memcmp(d->vertices->data + index * 20u, row, 20) == 0 && d->nodes->data[index] == node)
            return index;
    if (d->vertex_count >= 2048u)
        ri_fail(d->m.ctx, "%s: more than 2048 vertices", d->m.where);
    ri_put(d->m.ctx, d->vertices, row, sizeof(row));
    ri_u8(d->m.ctx, d->nodes, node);
    if (d->vertex_count == 0 || position[2] < d->lowest)
        d->lowest = (int32_t)position[2];
    return d->vertex_count++;
}

/* Places a model in the driver's DS space. */
typedef struct Offset
{
    int64_t scale, translate[3]; /* Q12 scale, fx translation */
    int mirror;                  /* negate x (left-hand tires) */
} Offset;

static void offset_place(const Offset *o, const int32_t in[3], int64_t out[3])
{
    for (unsigned axis = 0; axis < 3; ++axis)
        out[axis] = mkds_round_div((int64_t)in[axis] * o->scale, 4096) + o->translate[axis];
    if (o->mirror)
        out[0] = -out[0] + 2 * o->translate[0];
}

/* Appends the model's triangles. SKINNED vertices keep their own node;
 * otherwise every vertex takes node (NITRO_NO_NODE: static). */
#define SKINNED (-1)
static void add_triangles(Driver *d, const NitroMesh *mesh, uint32_t first_material, const Offset *how,
                          int node)
{
    const NitroVertex *vertices = (const NitroVertex *)(const void *)mesh->vertices->data;
    const NitroTriangle *triangles = (const NitroTriangle *)(const void *)mesh->triangles->data;
    for (uint32_t index = 0; index < mesh->triangle_count; ++index)
    {
        const NitroTriangle *triangle = &triangles[index];
        const uint32_t material = first_material + triangle->material;
        const MkdsMaterialPlan *plan = &d->m.plans[material];
        if (plan->skip)
            continue;
        uint32_t row[3];
        for (unsigned k = 0; k < 3; ++k)
        {
            /* Reflected axes reverse the winding; so does a mirrored tire. */
            const unsigned corner = (plan->reverse != 0) != (how->mirror != 0) ? k : 2 - k;
            const NitroVertex *vertex = &vertices[triangle->vertices[corner]];
            const MkdsCorner render = mkds_corner(&d->m, vertex, plan);
            int64_t ds[3], position[3];
            offset_place(how, vertex->position, ds);
            engine_axes(ds, position);
            for (unsigned axis = 0; axis < 3; ++axis)
                position[axis] *= PIXEL_Q16;
            row[k] = add_vertex(d, position, render.uv, node == SKINNED ? vertex->node : (uint8_t)node);
        }
        if (d->triangle_count >= 4096u)
            ri_fail(d->m.ctx, "%s: more than 4096 triangles", d->m.where);
        for (unsigned k = 0; k < 3; ++k)
            ri_u16(d->m.ctx, d->triangles, row[k]);
        ri_u16(d->m.ctx, d->triangles, material);
        d->triangle_count++;
    }
}

/* ---- Rig ------------------------------------------------------------------ */

/* The posed node relative to its rest pose, in our axes and pixels, as a
 * row-major 3x4 Q16 matrix. */
static void pose_row(Driver *d, const NitroNodeMatrix *posed, const NitroNodeMatrix *rest, RiBuf *out)
{
    int64_t r[9], t[3];
    mkds_pose_relative(&d->m, posed, rest, r, t);
    /* C r C^T with C mapping DS (x, y, z) to ours (-x, -z, y). */
    static const int axis_of[3] = {0, 2, 1}, sign_of[3] = {-1, -1, 1};
    int64_t engine[3];
    engine_axes(t, engine);
    for (unsigned row = 0; row < 3; ++row)
    {
        for (unsigned column = 0; column < 3; ++column)
            put_i32(d->m.ctx, out,
                    (int64_t)sign_of[row] * sign_of[column] * r[axis_of[row] * 3 + axis_of[column]] * 16, d->m.where);
        put_i32(d->m.ctx, out, engine[row] * PIXEL_Q16, d->m.where);
    }
}

typedef struct Clip
{
    const char *suffix, *tag;
    uint32_t flags;
} Clip;

static const Clip clips[] = {
    {"drive", "DRIV", 0},
    {"spin", "SPIN", 0},
    {"win", "WIN ", CLIP_LOOP},
    {"lose", "LOSE", CLIP_LOOP},
};

/* ---- Textures ---------------------------------------------------------------- */

/* A copy of set whose named texture and palette entries are replaced. */
static NitroTextures replaced(RiCtx *ctx, const NitroTextures *set)
{
    NitroTextures out = *set;
    out.textures = (NitroTexture *)(void *)ri_buf_copy(ctx, set->textures, set->texture_count * sizeof(NitroTexture))->data;
    out.palettes = (NitroPalette *)(void *)ri_buf_copy(ctx, set->palettes, set->palette_count * sizeof(NitroPalette))->data;
    return out;
}

static void replace_texture(Driver *d, NitroTextures *set, const NitroTexture *texture)
{
    for (size_t index = 0; index < set->texture_count; ++index)
        if (strcmp(set->textures[index].name, texture->name) == 0)
        {
            set->textures[index] = *texture;
            return;
        }
    ri_fail(d->m.ctx, "%s: kart has no texture %s", d->m.where, texture->name);
}

static void replace_palette(Driver *d, NitroTextures *set, const char *name, RiData colors)
{
    for (size_t index = 0; index < set->palette_count; ++index)
        if (strcmp(set->palettes[index].name, name) == 0)
        {
            set->palettes[index].colors = colors;
            return;
        }
    ri_fail(d->m.ctx, "%s: kart has no palette %s", d->m.where, name);
}

/* ---- Conversion ------------------------------------------------------------- */

static RiData file_in(RiCtx *ctx, const NdsFs *archive, const char *format, const char *a, const char *b)
{
    char path[96];
    snprintf(path, sizeof(path), format, a, b);
    return nds_file(ctx, archive, path);
}

static const char *named(RiCtx *ctx, const NdsFs *archive, const char *format, const char *a, const char *b)
{
    char path[160], text[192];
    snprintf(path, sizeof(path), format, a, b);
    snprintf(text, sizeof(text), "%s/%s", archive->name, path);
    return (const char *)ri_buf_copy(ctx, text, strlen(text) + 1)->data;
}

static int32_t word_at(RiCtx *ctx, RiData data, size_t offset, const char *what)
{
    if (offset + 4 > data.size)
        ri_fail(ctx, "%s is truncated", what);
    return (int32_t)ri_le32(data.data + offset);
}

static void part_begin(Driver *d, RiCtx *ctx, const char *where, uint32_t material_base)
{
    mkds_model_init(&d->m, ctx, where, 0, NULL, 0, MESH_MATERIAL_LIMIT);
    d->material_base = material_base;
    d->vertices = ri_buf(ctx);
    d->triangles = ri_buf(ctx);
    d->nodes = ri_buf(ctx);
    d->vertex_count = d->triangle_count = 0;
    d->lowest = 0;
}

/* Adds the kind-97 materials and the kind-96 mesh; nothing may fail after. */
static void part_mesh(Driver *d, uint32_t mesh_id)
{
    d->material_count = d->m.tables[MKDS_T_MATS].count;
    mkds_mesh_write(&d->m, d->material_base, d->vertices, d->vertex_count, d->triangles, d->triangle_count, mesh_id);
}

static NdsFs archive(RiCtx *ctx, const NdsFs *rom, const char *path)
{
    return nds_narc(ctx, nds_file(ctx, rom, path), path);
}

static RiData offset_data(RiCtx *ctx, const NdsFs *rom, const char *where)
{
    const RiData offsets = nds_file(ctx, rom, "data/KartModelMenu/kartoffsetdata.bin");
    if (offsets.size < KART_ENTRY_BYTES * KART_ENTRY_COUNT)
        ri_fail(ctx, "%s: kartoffsetdata.bin is truncated", where);
    return offsets;
}

/* A driver's seat in a kart, in our axes and Q16 pixels. */
static void seat_of(RiCtx *ctx, RiData offsets, uint32_t kart, uint32_t ds_id, int32_t out[3])
{
    int64_t seat[3], engine[3];
    for (unsigned axis = 0; axis < 3; ++axis)
        seat[axis] = word_at(ctx, offsets, kart * KART_ENTRY_BYTES + 16 + (13 + ds_id * 3 + axis) * 4u,
                             "kartoffsetdata.bin");
    engine_axes(seat, engine);
    for (unsigned axis = 0; axis < 3; ++axis)
        out[axis] = (int32_t)(engine[axis] * PIXEL_Q16);
}

/* The game's menu text is capitals; names read like the stock ones. */
static void display_name(const char *source, char name[32])
{
    memset(name, 0, 32);
    snprintf(name, 32, "%s", source);
    for (size_t index = 1; name[index]; ++index)
        if (isalpha((unsigned char)name[index - 1]))
            name[index] = (char)tolower((unsigned char)name[index]);
}

void mkds_driver_convert(RiCtx *ctx, const NdsFs *rom, const MkdsDriverSource *source, RiBuf *bindings)
{
    Driver d;
    char where[64];
    snprintf(where, sizeof(where), "Mario Kart DS driver %s", source->folder);
    part_begin(&d, ctx, where, MKDS_DRIVER_MATERIAL_ID(source->index, 0u));
    if (source->ds_id >= CHARACTER_COUNT)
        ri_fail(ctx, "%s: character number %u out of range", where, (unsigned)source->ds_id);

    const NdsFs main_archive = archive(ctx, rom, "data/KartModelMain.carc");
    const NdsFs menu_archive = archive(ctx, rom, "data/KartModelMenu.carc");
    const NdsFs sub_archive = archive(ctx, rom, "data/KartModelSub.carc");
    const NdsFs *character_archive = source->main_archive ? &main_archive : &sub_archive;
    const RiData weights = nds_file(ctx, rom, "data/KartModelMenu/charmenuparam.bin");
    if (weights.size < CHARACTER_COUNT * 2u)
        ri_fail(ctx, "%s: charmenuparam.bin is truncated", where);

    /* The player model alone, at its own origin: karts seat it at run time. */
    MkdsResources character;
    char stem[64];
    snprintf(stem, sizeof(stem), "character/%s/P_%s", source->folder, source->code);
    if (!mkds_resources(&d.m, character_archive, NULL, stem, &character))
        ri_fail(ctx, "%s: player model %s is missing", where, stem);
    const NitroTextures emblems =
        nitro_btx(ctx, file_in(ctx, &menu_archive, "emblem/%s_emblem.nsbtx", source->code, ""),
                  named(ctx, &menu_archive, "emblem/%s_emblem.nsbtx", source->code, ""));
    const NitroTexture *emblem = nitro_find_texture(&emblems, "emblem");
    const NitroPalette *emblem_palette = nitro_find_palette(&emblems, "emblem_pl");
    if (!emblem || !emblem_palette)
        ri_fail(ctx, "%s: emblem file lacks emblem/emblem_pl", where);

    const NitroModel *character_model = &character.models.models[0];
    if (character_model->node_count > MAX_NODES)
        ri_fail(ctx, "%s: more than %u nodes", where, MAX_NODES);
    const uint32_t character_material = d.m.tables[MKDS_T_MATS].count;
    mkds_materials(&d.m, character_model, &character.textures, NULL, NULL, 0);
    const Offset origin = {4096, {0, 0, 0}, 0};
    const NitroMesh character_mesh = nitro_model_mesh(ctx, character_model, 0);
    if (character_mesh.billboard_nodes)
        ri_fail(ctx, "%s: %u camera-facing nodes", where, (unsigned)character_mesh.billboard_nodes);
    add_triangles(&d, &character_mesh, character_material, &origin, SKINNED);

    /* The blink: the common face pattern's second key closes the eyes. */
    RiBuf *swaps = ri_buf(ctx);
    uint32_t swap_count = 0;
    const NitroPattern face =
        nitro_btp(ctx, nds_file(ctx, &main_archive, "character/common/P_faceanim.nsbtp"),
                  "data/KartModelMain.carc/character/common/P_faceanim.nsbtp");
    for (size_t track = 0; track < face.count; ++track)
        for (uint32_t index = 0; index < character_model->material_count; ++index)
        {
            const NitroMaterial *material = &character_model->materials[index];
            if (strcmp(material->name, face.tracks[track].material) != 0)
                continue;
            if (face.tracks[track].count != 2)
                ri_fail(ctx, "%s: face pattern %s has %zu keys, expected 2", where, material->name,
                        face.tracks[track].count);
            const NitroPatternKey *closed = &face.tracks[track].keys[1];
            const NitroTexture *texture = nitro_find_texture(&character.textures, closed->texture);
            const NitroPalette *palette = nitro_find_palette(&character.textures, closed->palette);
            if (!texture || !palette)
                ri_fail(ctx, "%s: face pattern texture %s/%s is missing", where, closed->texture, closed->palette);
            const uint32_t slot = character_material + index, alternate = d.m.tables[MKDS_T_MATS].count;
            if (alternate >= MESH_MATERIAL_LIMIT)
                ri_fail(ctx, "%s: more than %u materials", where, MESH_MATERIAL_LIMIT);
            /* The closed face copies the open one's MATS row with the other texture. */
            const MkdsTexture *entry_texture =
                mkds_texture(&d.m, &character.textures, texture, palette, d.m.plans[slot].mirror, material->name);
            uint32_t row[12];
            for (unsigned word = 0; word < 12; ++word)
                row[word] = ri_le32(d.m.tables[MKDS_T_MATS].bytes->data + slot * 48u + word * 4u);
            row[0] = entry_texture->id;
            mkds_words(&d.m, MKDS_T_MATS, row, 12);
            d.m.plans[alternate] = d.m.plans[slot];
            ri_words(ctx, swaps, 4, slot, alternate, BLINK_PERIOD, BLINK_PERIOD - BLINK_FRAMES);
            swap_count++;
        }

    /* Clips: every frame of every node, relative to the rest pose. */
    NitroNodeMatrix rest[MAX_NODES], posed[MAX_NODES];
    nitro_model_pose(ctx, character_model, NULL, 0, rest);
    RiBuf *clip_rows = ri_buf(ctx), *poses = ri_buf(ctx);
    uint32_t pose_count = 0;
    for (size_t index = 0; index < sizeof(clips) / sizeof(clips[0]); ++index)
    {
        const char *file = named(ctx, character_archive, "%s_%s.nsbca", stem, clips[index].suffix);
        char path[96];
        snprintf(path, sizeof(path), "%s_%s.nsbca", stem, clips[index].suffix);
        const NitroJointAnimation animation = nitro_bca(ctx, nds_file(ctx, character_archive, path), file);
        uint8_t tag[4];
        memcpy(tag, clips[index].tag, 4);
        ri_put(ctx, clip_rows, tag, 4);
        ri_words(ctx, clip_rows, 3, animation.frames, pose_count, clips[index].flags);
        for (uint32_t frame = 0; frame < animation.frames; ++frame)
        {
            nitro_model_pose(ctx, character_model, &animation, frame, posed);
            for (uint32_t node = 0; node < character_model->node_count; ++node)
                pose_row(&d, &posed[node], &rest[node], poses);
            pose_count += character_model->node_count;
        }
    }

    const MkdsTexture *emblem_texture = mkds_texture(&d.m, &emblems, emblem, emblem_palette, 0, "emblem");

    /* Sections last: nothing above may fail after one is added. */
    const uint32_t mesh_id = MKDS_DRIVER_MESH_ID(source->index);
    part_mesh(&d, mesh_id);

    RiBuf *rig = ri_buf(ctx);
    ri_put(ctx, rig, "RIG1", 4);
    ri_words(ctx, rig, 7, d.vertex_count, character_model->node_count, (uint32_t)(sizeof(clips) / sizeof(clips[0])),
             pose_count, swap_count, 0u, 0u);
    ri_put_buf(ctx, rig, d.nodes);
    ri_put_zero(ctx, rig, (4u - d.vertex_count % 4u) % 4u);
    ri_put_buf(ctx, rig, clip_rows);
    ri_put_buf(ctx, rig, poses);
    ri_put_buf(ctx, rig, swaps);
    ri_section(ctx, KIND_RIG, mesh_id, 1, rig);

    /* The 3D driver row: weight (Mario's 32 is the baseline) scales mass and
     * trades acceleration and handling for top speed. */
    const int64_t weight = (int64_t)ri_le16(weights.data + source->ds_id * 2u) - 32;
    const uint32_t portrait = MKDS_DRIVER_MATERIAL_ID(source->index, 15u);
    ri_section(ctx, MKDS_KIND_MATERIAL, portrait, 1, mkds_material_section(&d.m, 0xFFFFFFFFu, 0, emblem_texture->image));
    RiBuf *row = ri_buf(ctx);
    ri_put(ctx, row, "DRV3", 4);
    ri_words(ctx, row, 7, source->donor, (uint32_t)DRIVER_SELECTABLE,
             (uint32_t)((weight + 32) * 2048), (uint32_t)(65536 + weight * 164), (uint32_t)(65536 - weight * 393),
             (uint32_t)(65536 - weight * 262), portrait);
    char name[32];
    display_name(source->name, name);
    ri_put(ctx, row, name, sizeof(name));
    ri_section(ctx, KIND_DRIVER, source->driver_id, 1, row);

    /* Binding: driver kind 1 at its seat origin; its kart (kind 111) places it. */
    ri_words(ctx, bindings, 11, 1u, source->driver_id, 0u, 0u, mesh_id, 65536u, 65536u, 65536u, 0u, 0u, 0u);
}

/* Q16 ratio of value to the Standard MR's, moved halfway to 1 when soften. */
static uint32_t stat_ratio(RiCtx *ctx, int64_t value, int64_t standard, int soften, const char *where)
{
    if (value <= 0 || standard <= 0)
        ri_fail(ctx, "%s: kartphysicalparam.bin has a non-positive stat", where);
    int64_t ratio = value * 65536 / standard;
    if (soften)
        ratio = (ratio + 65536) / 2;
    return (uint32_t)ratio;
}

void mkds_kart_convert(RiCtx *ctx, const NdsFs *rom, const MkdsKartSource *source)
{
    Driver d;
    char where[64];
    snprintf(where, sizeof(where), "Mario Kart DS kart %u (%s)", (unsigned)source->index, source->name);
    part_begin(&d, ctx, where, MKDS_KART_MATERIAL_ID(source->index, 0u));
    const MkdsDriverSource *owner = source->owner;
    const uint32_t variant = source->index % 3u;
    if (source->index >= KART_ENTRY_COUNT || owner->ds_id != source->index / 3u)
        ri_fail(ctx, "%s: kart row does not belong to %s", where, owner->folder);

    const NdsFs main_archive = archive(ctx, rom, "data/KartModelMain.carc");
    const NdsFs menu_archive = archive(ctx, rom, "data/KartModelMenu.carc");
    const RiData offsets = offset_data(ctx, rom, where);
    const RiData physics = nds_file(ctx, rom, "data/KartModelMenu/kartphysicalparam.bin");
    if (physics.size < PHYSICS_ROW_BYTES * KART_ENTRY_COUNT)
        ri_fail(ctx, "%s: kartphysicalparam.bin is truncated", where);
    const RiData row_data = {offsets.data + source->index * KART_ENTRY_BYTES, KART_ENTRY_BYTES};

    /* Body: the Standard kart recoloured from the shared kart palette by
     * character number, or the character's own kart. Either wears the
     * owner's emblem where it has one. */
    MkdsResources kart, tire;
    char stem[64];
    if (variant == 0u)
        snprintf(stem, sizeof(stem), "kart/mario/kart_MR_a");
    else
        snprintf(stem, sizeof(stem), "kart/%s/kart_%s_%c", source->kart_folder, source->kart_code,
                 (char)('a' + variant));
    if (!mkds_resources(&d.m, &main_archive, NULL, stem, &kart))
        ri_fail(ctx, "%s: kart model %s is missing", where, stem);
    char tire_name[17];
    memcpy(tire_name, row_data.data, 16);
    tire_name[16] = 0;
    char tire_stem[48];
    snprintf(tire_stem, sizeof(tire_stem), "kart/tire/%s", tire_name);
    if (!mkds_resources(&d.m, &main_archive, NULL, tire_stem, &tire))
        ri_fail(ctx, "%s: tire model %s is missing", where, tire_name);
    const NitroTextures emblems =
        nitro_btx(ctx, file_in(ctx, &menu_archive, "emblem/%s_emblem.nsbtx", owner->code, ""),
                  named(ctx, &menu_archive, "emblem/%s_emblem.nsbtx", owner->code, ""));
    const NitroTexture *emblem = nitro_find_texture(&emblems, "emblem");
    const NitroPalette *emblem_palette = nitro_find_palette(&emblems, "emblem_pl");
    if (!emblem || !emblem_palette)
        ri_fail(ctx, "%s: emblem file lacks emblem/emblem_pl", where);
    NitroTextures kart_textures = replaced(ctx, &kart.textures);
    if (nitro_find_texture(&kart_textures, "emblem"))
    {
        replace_texture(&d, &kart_textures, emblem);
        replace_palette(&d, &kart_textures, "emblem_pl", emblem_palette->colors);
    }
    if (variant == 0u)
    {
        const RiData palette_file = nds_file(ctx, &main_archive, "kart/palette/kart_a.NCLR");
        if (palette_file.size < 0x28u + CHARACTER_COUNT * 32u || memcmp(palette_file.data, "RLCN", 4) != 0)
            ri_fail(ctx, "%s: kart/palette/kart_a.NCLR is not a 16-row palette", where);
        replace_palette(&d, &kart_textures, "kart_MR_a_pl",
                        (RiData){palette_file.data + 0x28u + owner->ds_id * 32u, 32u});
    }

    const NitroModel *kart_model = &kart.models.models[0], *tire_model = &tire.models.models[0];
    const uint32_t kart_material = d.m.tables[MKDS_T_MATS].count;
    mkds_materials(&d.m, kart_model, &kart_textures, NULL, NULL, 0);
    const uint32_t tire_material = d.m.tables[MKDS_T_MATS].count;
    mkds_materials(&d.m, tire_model, &tire.textures, NULL, NULL, 0);
    const Offset still = {4096, {0, 0, 0}, 0};
    const NitroMesh kart_mesh = nitro_model_mesh(ctx, kart_model, 0);
    add_triangles(&d, &kart_mesh, kart_material, &still, NITRO_NO_NODE);
    const NitroMesh tire_mesh = nitro_model_mesh(ctx, tire_model, 0);
    for (unsigned wheel = 0; wheel < 4; ++wheel)
    {
        Offset o = {wheel < 2 ? word_at(ctx, row_data, 16, "kartoffsetdata.bin") : 4096, {0, 0, 0}, 0};
        for (unsigned axis = 0; axis < 3; ++axis)
            o.translate[axis] = mkds_round_div(word_at(ctx, row_data, 20 + (wheel * 3 + axis) * 4u,
                                                       "kartoffsetdata.bin"),
                                               TIRE_POSITION_SCALE);
        o.mirror = o.translate[0] < 0;
        add_triangles(&d, &tire_mesh, tire_material, &o, (int)wheel);
    }

    /* Stats against the Standard MR: speed and handling as authored,
     * acceleration and weight halved toward 1 so light karts stay drivable. */
    const uint8_t *standard = physics.data, *stats = physics.data + source->index * PHYSICS_ROW_BYTES;
    const uint32_t speed = stat_ratio(ctx, (int32_t)ri_le32(stats + PHYSICS_SPEED),
                                      (int32_t)ri_le32(standard + PHYSICS_SPEED), 0, where);
    const uint32_t acceleration = stat_ratio(ctx, (int32_t)ri_le32(stats + PHYSICS_ACCEL),
                                             (int32_t)ri_le32(standard + PHYSICS_ACCEL), 1, where);
    const uint32_t handling = stat_ratio(ctx, (int16_t)ri_le16(stats + PHYSICS_HANDLING),
                                         (int16_t)ri_le16(standard + PHYSICS_HANDLING), 0, where);
    const uint32_t mass = stat_ratio(ctx, (int16_t)ri_le16(stats + PHYSICS_WEIGHT),
                                     (int16_t)ri_le16(standard + PHYSICS_WEIGHT), 1, where);
    if (source->driver_count > MKDS_KART_SEATS)
        ri_fail(ctx, "%s: more than %u drivers to seat", where, MKDS_KART_SEATS);
    RiBuf *seats = ri_buf(ctx);
    for (size_t index = 0; index < source->driver_count; ++index)
    {
        int32_t seat[3];
        seat_of(ctx, offsets, source->index, source->drivers[index].ds_id, seat);
        ri_words(ctx, seats, 4, source->drivers[index].driver_id, (uint32_t)seat[0], (uint32_t)seat[1],
                 (uint32_t)seat[2]);
    }

    const uint32_t kart_id = MKDS_KART_ID(source->index);
    part_mesh(&d, kart_id);

    /* A clipless rig names each tire's node (front pair first) so the
     * renderer can roll and steer them. */
    RiBuf *rig = ri_buf(ctx);
    ri_put(ctx, rig, "RIG1", 4);
    ri_words(ctx, rig, 7, d.vertex_count, 4u, 0u, 0u, 0u, 0u, 0u);
    ri_put_buf(ctx, rig, d.nodes);
    ri_put_zero(ctx, rig, (4u - d.vertex_count % 4u) % 4u);
    ri_section(ctx, KIND_RIG, kart_id, 1, rig);

    RiBuf *row = ri_buf(ctx);
    ri_put(ctx, row, "KRT3", 4);
    ri_words(ctx, row, 11, owner->driver_id,
             (uint32_t)(KART_SELECTABLE | KART_NO_SHADOW) | (variant == 0u ? (uint32_t)KART_OWNER_DEFAULT : 0u), kart_id, 0u, 0u,
             (uint32_t)d.lowest, speed, acceleration, handling, mass, (uint32_t)source->driver_count);
    char name[32];
    display_name(source->name, name);
    ri_put(ctx, row, name, sizeof(name));
    ri_put_buf(ctx, row, seats);
    ri_section(ctx, KIND_KART, kart_id, 1, row);
}

/* ---- Shadow --------------------------------------------------------------- */

/* The DS darkens the floor where it cuts the kart's stencil shadow volume
 * (kart/shadow/sh_*.nsbmd). Over a level road that cut is the volume's plan,
 * so the shadow is its footprint at the material's colour and alpha, the
 * same size until the floor drops below the volume. */
#define SHADOW_VOLUME 1u /* FXS1 shadow flag: constant size, no shrink */

/* fx model units to Q16 metres at kart scale 1 (28 pixels of .05 m at the
 * 0.72 kart art scale). */
static int32_t shadow_metres(int64_t fx)
{
    return (int32_t)(fx * 65536 * PIXELS_PER_UNIT * 36 / (4096 * 1000));
}

void mkds_kart_shadow(RiCtx *ctx, const NdsFs *rom, const MkdsKartSource *source, RiBuf *rows)
{
    char where[64], stem[48];
    snprintf(where, sizeof(where), "Mario Kart DS kart %u (%s) shadow", (unsigned)source->index, source->name);
    const uint32_t variant = source->index % 3u;
    const NdsFs main_archive = archive(ctx, rom, "data/KartModelMain.carc");
    RiData file;
    snprintf(stem, sizeof(stem), "kart/shadow/sh_%s_%c.nsbmd", variant ? source->kart_code : "MR",
             (char)('a' + variant));
    /* Karts without their own volume use the Standard kart's. */
    if (!nds_find(&main_archive, stem, &file))
        snprintf(stem, sizeof(stem), "kart/shadow/sh_MR_a.nsbmd");
    const NitroModels models = nitro_bmd(ctx, nds_file(ctx, &main_archive, stem), named(ctx, &main_archive, "%s%s", stem, ""));
    if (models.count != 1u || models.models[0].material_count != 1u)
        ri_fail(ctx, "%s: %s is not one single-material volume", where, stem);
    const NitroModel *model = &models.models[0];
    int32_t low[3], high[3];
    /* Columns run our left (DS +x) to right. */
    RiBuf *pixels = mkds_shadow_plan(ctx, where, model, 1, low, high);
    const double cz = (low[2] + high[2]) / 2.0;
    const double hx = (high[0] - low[0]) / 2.0, hz = (high[2] - low[2]) / 2.0;
    const NitroMaterial *material = &model->materials[0];
    const uint32_t alpha = material->polygon_attr >> 16 & 31u;
    const uint32_t colour = mkds_rgb_word(material->diffuse_ambient & 0x7FFFu); /* RGBA */
    const uint32_t id = MKDS_KART_MATERIAL_ID(source->index, 15u);
    RiBuf *image = ri_image_asset(ctx, MKDS_SHADOW_TEXELS, MKDS_SHADOW_TEXELS, pixels, 0, 0);
    RiBuf *section = ri_buf(ctx);
    ri_put(ctx, section, "MTL1", 4);
    ri_words(ctx, section, 3, (alpha * 255u / 31u) << 24 | colour >> 8, 0u, (uint32_t)image->size);
    ri_put_buf(ctx, section, image);
    ri_section(ctx, MKDS_KIND_MATERIAL, id, 1, section);
    ri_words(ctx, rows, 8, MKDS_KART_ID(source->index), id, (uint32_t)shadow_metres((int64_t)hx),
             (uint32_t)shadow_metres((int64_t)hz), (uint32_t)shadow_metres((int64_t)cz),
             (uint32_t)shadow_metres(-(int64_t)low[1]), SHADOW_VOLUME, 0u);
}
