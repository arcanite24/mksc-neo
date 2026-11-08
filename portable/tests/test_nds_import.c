#include "course_3d.h"
#include "fx_set_asset.h"
#include "mkds_course.h"
#include "mkds_fx.h"
#include "mkds_import.h"
#include "mesh_kart_rules_asset.h"
#include "mesh_item_rules_asset.h"
#include "recovered_pack.h"
#include "mesh_collision_index.h"
#include "mkds_map.h"
#include "mkds_model.h"
#include "mkds_sound.h"
#include "nds_fs.h"
#include "nds_sequence.h"
#include "nitro_spa.h"
#include "render_scene.h"
#include "render_model.h"
#include "sound_set_asset.h"
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Synthetic DS cartridges, NARCs, LZ77 streams and BMG files, built here so
 * the readers' bounds checks run without the owner's ROM. --mkds ROM also
 * imports that cartridge (check.py --mkds-rom). */

static int failures;

static void expect(int condition, const char *what)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

typedef struct Bytes
{
    uint8_t data[8192];
    size_t size;
} Bytes;

static void put(Bytes *bytes, const void *data, size_t size)
{
    memcpy(bytes->data + bytes->size, data, size);
    bytes->size += size;
}

static void put8(Bytes *bytes, uint32_t value)
{
    bytes->data[bytes->size++] = (uint8_t)value;
}

static void put16(Bytes *bytes, uint32_t value)
{
    put8(bytes, value);
    put8(bytes, value >> 8);
}

static void put32(Bytes *bytes, uint32_t value)
{
    put16(bytes, value);
    put16(bytes, value >> 16);
}

static void set32(Bytes *bytes, size_t offset, uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index)
        bytes->data[offset + index] = (uint8_t)(value >> (index * 8u));
}

static void align(Bytes *bytes, size_t alignment)
{
    while (bytes->size % alignment)
        put8(bytes, 0);
}

/* "a.txt" then "sub/b.bin", numbered from first_file; sub_child is the
 * directory ID the root's "sub" entry names. */
static void fnt(Bytes *out, uint32_t first_file, uint32_t sub_child)
{
    out->size = 0;
    put32(out, 16);
    put16(out, first_file);
    put16(out, 2);
    put32(out, 29);
    put16(out, first_file + 1);
    put16(out, 0xF000);
    put8(out, 5);
    put(out, "a.txt", 5);
    put8(out, 0x83);
    put(out, "sub", 3);
    put16(out, sub_child);
    put8(out, 0);
    put8(out, 5);
    put(out, "b.bin", 5);
    put8(out, 0);
}

static void narc(Bytes *out, const Bytes *names, uint32_t file_count, uint32_t last_end)
{
    out->size = 0;
    put(out, "NARC", 4);
    put16(out, 0xFFFE);
    put16(out, 0x0100);
    put32(out, 0);
    put16(out, 16);
    put16(out, 3);
    put(out, "BTAF", 4);
    put32(out, 12 + file_count * 8u);
    put16(out, file_count);
    put16(out, 0);
    static const uint32_t spans[2][2] = {{0, 5}, {8, 11}};
    for (uint32_t file = 0; file < file_count; ++file)
    {
        put32(out, spans[file][0]);
        put32(out, file + 1 == file_count ? last_end : spans[file][1]);
    }
    const size_t names_at = out->size;
    put(out, "BTNF", 4);
    put32(out, 0);
    put(out, names->data, names->size);
    align(out, 4);
    set32(out, names_at + 4, (uint32_t)(out->size - names_at));
    put(out, "GMIF", 4);
    put32(out, 8 + 12);
    put(out, "hello\0\0\0\1\2\3\0", 12);
    set32(out, 8, (uint32_t)out->size);
}

/* All-literal LZ77 type 0x10, as a .carc wraps its NARC. */
static void lz10_literal(Bytes *out, const Bytes *in)
{
    out->size = 0;
    put32(out, 0x10u | (uint32_t)in->size << 8);
    for (size_t at = 0; at < in->size; at += 8)
    {
        put8(out, 0);
        put(out, in->data + at, in->size - at < 8 ? in->size - at : 8);
    }
}

/* Header, then FNT, FAT and three files; file 0 is an unnamed overlay. */
static void cartridge(Bytes *out, const char *code)
{
    memset(out->data, 0, 0x200);
    out->size = 0x200;
    memcpy(out->data, "TESTGAME", 8);
    memcpy(out->data + 12, code, 4);
    Bytes names;
    fnt(&names, 1, 0xF001);
    const size_t names_at = out->size;
    put(out, names.data, names.size);
    align(out, 4);
    const size_t table_at = out->size;
    out->size += 24;
    const size_t data_at = out->size;
    put(out, "overlay!hello\0\0\0\1\2\3", 19);
    const uint32_t spans[3][2] = {{0, 8}, {8, 13}, {16, 19}};
    for (unsigned file = 0; file < 3; ++file)
    {
        set32(out, table_at + file * 8u, (uint32_t)data_at + spans[file][0]);
        set32(out, table_at + file * 8u + 4u, (uint32_t)data_at + spans[file][1]);
    }
    set32(out, 0x40, (uint32_t)names_at);
    set32(out, 0x44, (uint32_t)names.size);
    set32(out, 0x48, (uint32_t)table_at);
    set32(out, 0x4C, 24);
    set32(out, 0x80, (uint32_t)out->size);
    const uint16_t crc = nds_crc16(out->data, 0x15E);
    out->data[0x15E] = (uint8_t)crc;
    out->data[0x15F] = (uint8_t)(crc >> 8);
}

static void bmg(Bytes *out, int with_text)
{
    static const uint16_t text[] = {
        0,
        'M', 'A', 'R', 'I', 'O', 0x1A, 0x0306, 0x0201, 0, /* tag: 6 bytes from the 0x1A */
        0xE039, 0xE03A, 'P', 'E', 'A', 'C', 'H', 0,
        0x00E9, 0};
    out->size = 0;
    put(out, "MESGbmg1", 8);
    put32(out, 0);
    put32(out, with_text ? 2 : 1);
    put8(out, 2);
    while (out->size < 0x20)
        put8(out, 0);
    put(out, "INF1", 4);
    put32(out, 8 + 8 + 3 * 8);
    put16(out, 3);
    put16(out, 8);
    put32(out, 0);
    static const uint32_t offsets[3] = {2, 20, 36};
    for (unsigned index = 0; index < 3; ++index)
    {
        put32(out, offsets[index]);
        put32(out, 0);
    }
    if (with_text)
    {
        put(out, "DAT1", 4);
        put32(out, 8 + (uint32_t)sizeof(text));
        for (size_t index = 0; index < sizeof(text) / sizeof(text[0]); ++index)
            put16(out, text[index]);
    }
    set32(out, 8, (uint32_t)out->size);
}

/* Each case runs as an import group so a reader failure unwinds through the
 * importer's ordinary error path. */
typedef struct Case
{
    const Bytes *input;
    const char *expected_error; /* NULL: must succeed */
    char text[3][32];
    size_t count;
} Case;

static Case *current;
static int run(RiGroup group, Case *test, const char *what);

static RiData input_view(void)
{
    const RiData data = {current->input->data, current->input->size};
    return data;
}

static void group_rom(RiCtx *ctx)
{
    NdsHeader header;
    nds_header(ctx, input_view(), &header);
    const NdsFs fs = nds_rom_fs(ctx, input_view(), &header);
    RiData file;
    current->count = fs.count;
    if (nds_find(&fs, "sub/b.bin", &file) && file.size == 3 && file.data[2] == 3 && nds_find(&fs, "a.txt", &file))
        memcpy(current->text[0], file.data, file.size);
    snprintf(current->text[1], sizeof current->text[1], "%s/%s", header.title, header.game_code);
}

static void group_narc(RiCtx *ctx)
{
    const NdsFs fs = nds_narc(ctx, input_view(), "test.carc");
    current->count = fs.count;
    const RiData file = nds_file(ctx, &fs, "sub/b.bin");
    if (file.size == 3)
        memcpy(current->text[0], nds_file(ctx, &fs, "a.txt").data, 5);
    (void)nds_file(ctx, &fs, "missing.bin");
}

static void group_lz(RiCtx *ctx)
{
    const RiBuf *out = nds_lz77(ctx, input_view(), 64, "test.lz");
    current->count = out->size;
    memcpy(current->text[0], out->data, out->size < 31 ? out->size : 31);
}

static void group_bmg(RiCtx *ctx)
{
    static const NdsGlyph badges[] = {{0xE039, "GBA "}, {0xE03A, ""}};
    const NdsBmg text = nds_bmg(ctx, input_view(), "test.bmg");
    current->count = text.count;
    nds_bmg_ascii(ctx, &text, 0, NULL, 0, current->text[0], sizeof current->text[0]);
    nds_bmg_ascii(ctx, &text, 1, badges, 2, current->text[1], sizeof current->text[1]);
    nds_bmg_ascii(ctx, &text, 2, badges, 2, current->text[2], sizeof current->text[2]);
}

/* One floor prism: the right triangle (0,0,0), (10,0,0), (0,0,10) in map
 * units, edge normals outward, face normal up. */
static void kcl(Bytes *out, uint32_t position_index, uint32_t attribute)
{
    out->size = 0;
    put32(out, 0x3C);           /* positions */
    put32(out, 0x3C + 12);      /* normals */
    put32(out, 0x3C + 12 + 24 - 0x10); /* prisms, less one prism */
    put32(out, 0x3C + 12 + 24 + 16); /* octree */
    put32(out, 30 * 4096);
    for (int k = 0; k < 9; ++k) /* area minimum, masks and shifts */
        put32(out, 0);
    put32(out, 25 * 4096);
    put32(out, 0), put32(out, 0), put32(out, 0); /* the one position */
    static const int16_t normals[4][3] = {{0, 4096, 0}, {-4096, 0, 0}, {0, 0, -4096}, {2896, 0, 2896}};
    for (int n = 0; n < 4; ++n)
        for (int axis = 0; axis < 3; ++axis)
            put16(out, (uint16_t)normals[n][axis]);
    put32(out, 28963); /* height: 10 / sqrt(2) */
    put16(out, position_index), put16(out, 0), put16(out, 1), put16(out, 2), put16(out, 3);
    put16(out, attribute);
    put32(out, 0); /* octree */
}

static void group_kcl(RiCtx *ctx)
{
    const MkdsKcl collision = mkds_kcl(ctx, input_view(), "test.kcl");
    current->count = collision.count;
    const MkdsKclTriangle *t = &collision.triangles[0];
    snprintf(current->text[0], sizeof current->text[0], "%d,%d,%d %d,%d,%d", t->vertex[1][0], t->vertex[1][1],
             t->vertex[1][2], t->vertex[2][0], t->vertex[2][1], t->vertex[2][2]);
    snprintf(current->text[1], sizeof current->text[1], "%u", (unsigned)MKDS_KCL_TYPE(t->attribute));
}

/* A map with every lap section: one item box, a start, a respawn, two
 * checkpoints on one looping path and two-point CPU and item routes. */
static void nkm(Bytes *out, const char *extra_section, int cpat_next)
{
    static const char *const tags[] = {"OBJI", "KTPS", "KTPJ", "CPOI", "CPAT", "IPOI", "IPAT", "EPOI", "EPAT"};
    const size_t count = 9 + (extra_section != NULL);
    out->size = 0;
    put(out, "NKMD", 4);
    put16(out, 37), put16(out, (uint32_t)(8 + count * 4));
    const size_t table = out->size;
    for (size_t k = 0; k < count; ++k)
        put32(out, 0);
    for (size_t k = 0; k < count; ++k)
    {
        const char *tag = k < 9 ? tags[k] : extra_section;
        set32(out, table + k * 4, (uint32_t)(out->size - (8 + count * 4)));
        put(out, tag, 4);
        if (!strcmp(tag, "OBJI"))
        {
            put32(out, 1);
            for (int w = 0; w < 9; ++w)
                put32(out, w == 0 ? 100 * 4096 : w >= 6 ? 4096 : 0);
            put16(out, 101u /* item box */), put16(out, 0xFFFF);
            for (int w = 0; w < 18; w += 2)
                put16(out, 0);
            put16(out, 0);
        }
        else if (!strcmp(tag, "KTPS") || !strcmp(tag, "KTPJ"))
        {
            put32(out, 1);
            for (int w = 0; w < 6; ++w)
                put32(out, w == 4 ? 90 * 4096 : 0);
            put16(out, 0), put16(out, !strcmp(tag, "KTPJ") ? 1 : 0xFFFF);
            if (!strcmp(tag, "KTPJ"))
                put16(out, 0), put16(out, 0);
        }
        else if (!strcmp(tag, "CPOI"))
        {
            put32(out, 2);
            for (int cp = 0; cp < 2; ++cp)
            {
                put32(out, cp * 400 * 4096), put32(out, -300 * 4096), put32(out, cp * 400 * 4096), put32(out, 300 * 4096);
                put32(out, 0), put32(out, 4096), put32(out, 0);
                put16(out, 0xFFFF), put16(out, cp ? 0xFFFF : 0), put16(out, cp ? 0 : 0xFFFF);
                put8(out, 0), put8(out, 0);
            }
        }
        else if (!strcmp(tag, "CPAT") || !strcmp(tag, "IPAT") || !strcmp(tag, "EPAT"))
        {
            put32(out, 1);
            put16(out, 0), put16(out, 2);
            put8(out, !strcmp(tag, "CPAT") ? (uint32_t)cpat_next : 0), put8(out, 0xFF), put8(out, 0xFF);
            put8(out, 0), put8(out, 0xFF), put8(out, 0xFF);
            put16(out, 0);
        }
        else if (!strcmp(tag, "IPOI") || !strcmp(tag, "EPOI"))
        {
            put32(out, 2);
            for (int point = 0; point < 2; ++point)
            {
                put32(out, point * 400 * 4096), put32(out, 0), put32(out, 0), put32(out, 50 * 4096);
                put32(out, 0);
                if (!strcmp(tag, "EPOI"))
                    put32(out, 0);
            }
        }
        else
            put32(out, 0);
    }
}

static void group_nkm(RiCtx *ctx)
{
    const MkdsMap map = mkds_nkm(ctx, input_view(), "test.nkm");
    current->count = map.checkpoint_count;
    snprintf(current->text[0], sizeof current->text[0], "%zu %zu %zu %zu %u", map.object_count,
             map.enemy_point_count, map.item_point_count, map.respawn_count, (unsigned)map.objects[0].id);
    snprintf(current->text[1], sizeof current->text[1], "%d %d %d", map.checkpoints[1].left[0],
             map.respawns[0].item_point, map.checkpoint_paths[0].next[0]);
}

static void test_map_readers(void)
{
    Bytes data;
    kcl(&data, 0, 0x8000);
    Case test = {&data, NULL, {{0}}, 0};
    run(group_kcl, &test, "KCL prism becomes a triangle");
    /* Q12 edge normals round the 10-unit legs up by four parts in 4096. */
    expect(test.count == 1 && strcmp(test.text[0], "0,0,40964 40964,0,0") == 0 && strcmp(test.text[1], "0") == 0,
           "KCL triangle corners wind counter-clockwise about the face normal");
    kcl(&data, 5, 0x8000);
    test = (Case){&data, "prism 0 position 5 is out of range", {{0}}, 0};
    run(group_kcl, &test, "KCL position index out of range rejected");

    nkm(&data, NULL, 0);
    test = (Case){&data, NULL, {{0}}, 0};
    run(group_nkm, &test, "NKMD lap sections read");
    expect(test.count == 2 && strcmp(test.text[0], "1 2 2 1 101") == 0 && strcmp(test.text[1], "1638400 1 0") == 0,
           "NKMD objects, points, checkpoints and respawn links");
    nkm(&data, "ZZZZ", 0);
    test = (Case){&data, "unknown NKMD section ZZZZ", {{0}}, 0};
    run(group_nkm, &test, "unknown NKMD section rejected");
    nkm(&data, NULL, 4);
    test = (Case){&data, "CPAT path 0 links to a missing path", {{0}}, 0};
    run(group_nkm, &test, "checkpoint path link out of range rejected");
}

static int run(RiGroup group, Case *test, const char *what)
{
    char error[256];
    MkscRomImportResult result;
    current = test;
    test->count = 0;
    memset(test->text, 0, sizeof test->text);
    const int status = ri_build_steps(test->input->data, test->input->size, &group, 1, NULL, NULL, NULL, &result,
                                      error, sizeof error);
    if (status == 1)
        mksc_rom_import_free(&result);
    const int ok = test->expected_error ? status == 0 && strstr(error, test->expected_error) != NULL : status == 1;
    if (!ok)
        fprintf(stderr, "%s: status %d error '%s'\n", what, status, error);
    expect(ok, what);
    return ok;
}

/* A pattern resource can be a palette/pose catalogue, not a clock. Exercise
 * held keys with nonuniform spacing and distinct palettes without ROM art. */
static void group_pattern_variants(RiCtx *ctx)
{
    uint8_t texels[16], clear[16] = {0};
    memset(texels, 0x55, sizeof texels); /* 2-bit palette index 1 */
    uint8_t colors[3][4] = {{0, 0, 31, 0}, {0, 0, 224, 3}, {0, 0, 0, 124}};
    NitroTexture textures[] = {
        {"paint", NITRO_TEXTURE_PALETTE4, 8, 8, 1, {texels, sizeof texels}, {0}},
        {"clear", NITRO_TEXTURE_PALETTE4, 8, 8, 1, {clear, sizeof clear}, {0}},
    };
    NitroPalette palettes[] = {{"red", {colors[0], 4}}, {"green", {colors[1], 4}},
                               {"blue", {colors[2], 4}}};
    NitroTextures set = {"synthetic", textures, 2, palettes, 3};
    NitroMaterial material = {.name = "crowd", .texture = "paint", .palette = "red",
        .polygon_attr = (31u << 16) | (3u << 6), .width = 8, .height = 8};
    NitroModel model = {.material_count = 1, .materials = &material};
    NitroPatternKey keys[] = {{0, "paint", "red"}, {2, "paint", "green"}, {5, "clear", "blue"}};
    NitroPatternTrack track = {"crowd", keys, 3};
    NitroPattern pattern = {8, &track, 1};
    MkdsModel out;
    mkds_model_init(&out, ctx, "pattern variants", 100, NULL, 0, 8);
    const uint32_t frames[] = {0, 3, 7, 3};
    for (unsigned i = 0; i < 4; ++i)
        mkds_materials_at_frame(&out, &model, &set, NULL, &pattern, 0, frames[i]);
    const uint8_t *mats = out.tables[MKDS_T_MATS].bytes->data;
    expect(out.tables[MKDS_T_MATS].count == 4 && out.texture_count == 3,
           "fixed variants share textures but own their material binding");
    expect(ri_le32(mats) == 100 && ri_le32(mats + 48) == 101 &&
               ri_le32(mats + 96) == 102 && ri_le32(mats + 144) == 101,
           "fixed frame chooses the preceding key, preserving earlier placements");
    const uint32_t expected_pixels[] = {0xffff0000u, 0xff00ff00u, 0};
    for (unsigned i = 0; i < 3; ++i)
    {
        PortableNativeImage image;
        const RiBuf *bytes = out.textures[i].image;
        expect(portable_native_image_open(bytes->data, bytes->size, &image) &&
                   portable_native_image_pixel(&image, 0, 0) == expected_pixels[i],
               "fixed variant decodes its own palette and transparency");
    }
    expect(!(ri_le32(mats + 36) & MKDS_MATERIAL_MASK) &&
               (ri_le32(mats + 96 + 36) & MKDS_MATERIAL_MASK),
           "fixed variant alpha mode follows the selected texture");
    expect(out.tables[MKDS_T_TXAN].count == 0 && out.tables[MKDS_T_TXPT].count == 0,
           "fixed variants never enter a timed texture animation");
    mkds_materials(&out, &model, &set, NULL, &pattern, 0);
    expect(out.tables[MKDS_T_TXAN].count == 1 && out.tables[MKDS_T_TXPT].count == 3 &&
               ri_le32(out.tables[MKDS_T_TXAN].bytes->data + 8) == 8,
           "ordinary pattern animation retains its duration and all keys");
    NitroSrtFrame poses[] = {{4096, 4096, 0, 4096, 0, 0}, {4096, 4096, 0, 4096, 4096, 0}};
    NitroSrtTrack srt_track = {"crowd", ri_buf_copy(ctx, poses, sizeof poses)};
    NitroSrt srt = {2, NITRO_TEXMTX_MAYA, &srt_track, 1};
    mkds_materials_at_frame(&out, &model, &set, &srt, &pattern, 0, 3);
    expect(out.tables[MKDS_T_TXAN].count == 2 && out.tables[MKDS_T_TXSR].count == 2 &&
               out.tables[MKDS_T_TXPT].count == 3 &&
               ri_le32(out.tables[MKDS_T_TXAN].bytes->data + 32 + 4) == 0,
           "holding a pattern leaves independent texture SRT animated");
}

/* One emitter with gravity drawing a mirrored 8x8 A3I5 texture twice across. */
static void spa_file(Bytes *out)
{
    out->size = 0;
    put(out, " APS12_1", 8);
    put16(out, 1);
    put16(out, 1);
    put32(out, 0);
    put32(out, 96);  /* emitter bytes */
    put32(out, 100); /* texture bytes */
    put32(out, 128); /* texture offset */
    put32(out, 0);
    const size_t emitter = out->size;
    put32(out, 1u << 24); /* gravity block */
    while (out->size < emitter + 88u)
        put8(out, 0);
    set32(out, emitter + 16, 2 * 4096);      /* two per emission */
    set32(out, emitter + 28, 4096u << 16);   /* axis (0, 1, 0) */
    out->data[emitter + 34] = 0x1f;          /* red */
    set32(out, emitter + 44, 2048);          /* half size */
    out->data[emitter + 62] = 30;            /* life */
    set32(out, emitter + 68, 4u | 31u << 8); /* interval 4, alpha 31, texture 0 */
    set32(out, emitter + 72, 1u << 24);      /* tile S once */
    put16(out, 0);
    put16(out, (uint32_t)-9 & 0xffffu);      /* gravity down */
    put16(out, 0);
    put16(out, 0);
    put(out, " TPS", 4);
    put32(out, 1u | 1u << 14); /* A3I5 8x8, mirrored in S */
    put32(out, 64);
    put32(out, 96); /* palette at entry + 96 */
    put32(out, 4);
    put32(out, 100);
    put32(out, 0);
    put32(out, 100);
    for (unsigned i = 0; i < 64; ++i)
        put8(out, i % 8 == 0 ? 0xE1u : 0u); /* column 0: opaque colour 1 */
    put16(out, 0);
    put16(out, 0x7c00); /* blue */
}

static void group_spa(RiCtx *ctx)
{
    const NitroSpa spa = nitro_spa(ctx, input_view(), "test.spa");
    uint32_t width, height;
    RiBuf *pixels = nitro_spa_emitter_argb(ctx, &spa, 0, &width, &height);
    const NitroSpaEmitter *e = &spa.emitters[0];
    current->count = width * 100u + height;
    snprintf(current->text[0], sizeof current->text[0], "%u %d %d %u %u", (unsigned)e->interval, (int)e->gravity[1],
             (int)e->axis[1], (unsigned)e->life, (unsigned)e->count);
    snprintf(current->text[1], sizeof current->text[1], "%08x %08x %08x", (unsigned)ri_le32(pixels->data),
             (unsigned)ri_le32(pixels->data + 15u * 4u), (unsigned)ri_le32(pixels->data + 4u));
}

static void test_spa(void)
{
    static Bytes spa;
    spa_file(&spa);
    Case test = {&spa, NULL, {{0}}, 0};
    run(group_spa, &test, "SPL particle file reads");
    expect(test.count == 1608u && strcmp(test.text[0], "4 -9 4096 30 8192") == 0,
           "SPA emitter fields and its gravity block decode");
    expect(strcmp(test.text[1], "ff0000ff ff0000ff 00000000") == 0,
           "a mirrored SPA texture repeats flipped across the quad");
    spa.data[128] = 'X';
    test = (Case){&spa, "texture 0", {{0}}, 0};
    run(group_spa, &test, "bad SPA texture rejected");
    spa_file(&spa);
    put8(&spa, 0);
    test = (Case){&spa, "texture bank ends", {{0}}, 0};
    run(group_spa, &test, "trailing bytes after the SPA texture bank rejected");
}

static void test_readers(void)
{
    Bytes rom, names, archive, packed, broken, text;
    cartridge(&rom, "TSTE");
    Case test = {&rom, NULL, {{0}}, 0};
    run(group_rom, &test, "cartridge filesystem reads");
    expect(test.count == 2 && strcmp(test.text[0], "hello") == 0 && strcmp(test.text[1], "TESTGAME/TSTE") == 0,
           "cartridge lists named files only, overlay skipped");

    broken = rom;
    broken.data[0x20] ^= 1;
    test = (Case){&broken, "header checksum", {{0}}, 0};
    run(group_rom, &test, "bad header checksum rejected");

    fnt(&names, 0, 0xF001);
    narc(&archive, &names, 2, 11);
    test = (Case){&archive, "missing missing.bin", {{0}}, 0};
    run(group_narc, &test, "NARC lists files and names a missing one");
    expect(test.count == 2 && strcmp(test.text[0], "hello") == 0, "NARC file contents");
    lz10_literal(&packed, &archive);
    test = (Case){&packed, "missing missing.bin", {{0}}, 0};
    run(group_narc, &test, "LZ77-wrapped NARC reads the same");
    expect(test.count == 2 && strcmp(test.text[0], "hello") == 0, "LZ77-wrapped NARC contents");

    narc(&broken, &names, 2, 64);
    test = (Case){&broken, "outside its", {{0}}, 0};
    run(group_narc, &test, "file beyond NARC data rejected");
    narc(&broken, &names, 1, 5);
    test = (Case){&broken, "beyond the 1-entry file table", {{0}}, 0};
    run(group_narc, &test, "file ID beyond table rejected");
    Bytes cycle;
    fnt(&cycle, 0, 0xF001);
    /* The subdirectory names the root as its child: a cycle. */
    cycle.size = 29;
    put8(&cycle, 0x83);
    put(&cycle, "top", 3);
    put16(&cycle, 0xF000);
    put8(&cycle, 0);
    narc(&broken, &cycle, 2, 11);
    test = (Case){&broken, "listed twice", {{0}}, 0};
    run(group_narc, &test, "directory cycle rejected");
    broken = archive;
    set32(&broken, 16 + 4, 4096);
    test = (Case){&broken, "does not fit", {{0}}, 0};
    run(group_narc, &test, "oversized NARC chunk rejected");
    broken = packed;
    broken.size -= 4;
    test = (Case){&broken, "truncated LZ77", {{0}}, 0};
    run(group_narc, &test, "truncated LZ77 rejected");

    static const uint8_t lz10[] = {0x10, 8, 0, 0, 0x20, 'A', 'B', 0x30, 0x01};
    static const uint8_t lz11[] = {0x11, 8, 0, 0, 0x20, 'A', 'B', 0x50, 0x01};
    static const uint8_t lz11_long[] = {0x11, 20, 0, 0, 0x40, 'Z', 0x00, 0x20, 0x00};
    static const uint8_t before_start[] = {0x10, 8, 0, 0, 0x80, 0x00, 0x04};
    static const uint8_t extended_size[] = {0x10, 0, 0, 0, 0x00, 0x00, 0x00, 0x02};
    static const uint8_t other_type[] = {0x30, 8, 0, 0, 0};
    Bytes stream;
    const struct
    {
        const uint8_t *data;
        size_t size;
        const char *error, *text, *what;
    } streams[] = {
        {lz10, sizeof lz10, NULL, "ABABABAB", "LZ10 back-reference"},
        {lz11, sizeof lz11, NULL, "ABABABAB", "LZ11 short back-reference"},
        {lz11_long, sizeof lz11_long, NULL, "ZZZZZZZZZZZZZZZZZZZZ", "LZ11 0x11+ back-reference"},
        {before_start, sizeof before_start, "before the output start", NULL, "LZ77 distance before start"},
        {extended_size, sizeof extended_size, "over the 64-byte limit", NULL, "LZ77 extended size limit"},
        {other_type, sizeof other_type, "unsupported compression type", NULL, "unknown compression type"},
    };
    for (size_t index = 0; index < sizeof(streams) / sizeof(streams[0]); ++index)
    {
        stream.size = 0;
        put(&stream, streams[index].data, streams[index].size);
        test = (Case){&stream, streams[index].error, {{0}}, 0};
        if (run(group_lz, &test, streams[index].what) && streams[index].text)
            expect(strcmp(test.text[0], streams[index].text) == 0, streams[index].what);
    }

    bmg(&text, 1);
    test = (Case){&text, "unsupported character U+00E9", {{0}}, 0};
    run(group_bmg, &test, "BMG text, tags and badges");
    expect(test.count == 3 && strcmp(test.text[0], "MARIO") == 0 && strcmp(test.text[1], "GBA PEACH") == 0,
           "BMG drops tags and maps badge glyphs");
    bmg(&text, 0);
    test = (Case){&text, "lacks INF1 or DAT1", {{0}}, 0};
    run(group_bmg, &test, "BMG without DAT1 rejected");
}

static void test_identify(void)
{
    char error[256];
    Bytes rom;
    cartridge(&rom, "TSTE");
    expect(!mksc_mkds_identify(rom.data, rom.size, error, sizeof error) &&
               strstr(error, "TESTGAME (TSTE) is not a supported game"),
           "other DS game named");
    cartridge(&rom, "AMCJ");
    expect(!mksc_mkds_identify(rom.data, rom.size, error, sizeof error) && strstr(error, "(Japan)"),
           "other Mario Kart DS region named");
    cartridge(&rom, "AMCE");
    expect(!mksc_mkds_identify(rom.data, rom.size, error, sizeof error) && strstr(error, "truncated"),
           "short US cartridge rejected");
    static uint8_t gba[0x400];
    expect(!mksc_mkds_identify(gba, sizeof gba, error, sizeof error) &&
               strcmp(error, "not a Nintendo DS ROM") == 0,
           "non-DS file rejected");
}

static uint8_t *read_all(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *data = length > 0 ? malloc((size_t)length) : NULL;
    if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
    {
        free(data);
        data = NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static int cancel_at_three(void *context, uint32_t done, uint32_t total)
{
    (void)total;
    *(uint32_t *)context = done;
    return done >= 3;
}

/* Every converted course opens through the 3D loader and renderer; Figure-8
 * Circuit keeps its vertex colours, sky and animated sky texture. */
static void check_courses(const MkscRomImportResult *result, uint32_t converted)
{
    PortableAssetPackView pack;
    char error[256] = "";
    expect(portable_asset_pack_open(result->pack, result->pack_size, &pack) == PORTABLE_ASSET_PACK_OK, "pack opens");
    expect(portable_course_3d_validate_pack(&pack, error, sizeof error), error);
    expect(portable_course_3d_catalog_count(&pack) == converted, "one catalog row per course");
    PortableCourse3DView course;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(0), &course, error, sizeof error), error);
    PortableRenderResources resources = {0};
    expect(portable_render_resources_open_course(&resources, &course, error, sizeof error), error);
    uint32_t sky = 0, white = 0, animated = 0;
    for (uint32_t i = 0; i < resources.mesh.material_count; ++i)
        sky += (resources.mesh.materials[i].flags & PORTABLE_RENDER_MATERIAL_BACKGROUND) != 0;
    for (uint32_t i = 0; i < resources.mesh.vertex_count; ++i)
        white += resources.mesh.vertex_argb[i] == UINT32_MAX;
    for (uint32_t i = 0; i < resources.mesh.material_count; ++i)
        animated += resources.mesh.materials[i].animation != NULL;
    expect(sky == 1 && animated == 1, "Figure-8 sky is one animated background material");
    expect(white < resources.mesh.vertex_count, "Figure-8 keeps vertex colours");
    portable_render_resources_close(&resources);
    /* TownMonte's three one-frame NSBTP entries are selectable appearances.
     * Delfino keeps its scrolling textures, but no material cycles that bank. */
    PortableCourse3DView town;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(5), &town, error, sizeof error), error);
    const PortableCourse3DTable *animations =
        portable_course_3d_table(&town, PORTABLE_COURSE_3D_TAG('T', 'X', 'A', 'N'));
    uint32_t scrolling = 0, patterns = 0;
    for (uint32_t i = 0; animations && i < animations->count; ++i)
    {
        PortableCourse3DTextureAnimation animation;
        expect(portable_course_3d_texture_animation(&town, i, &animation), "Delfino animation decodes");
        scrolling += animation.kind == 0;
        patterns += animation.kind == 1;
    }
    expect(scrolling > 0 && patterns == 0,
           "Delfino retains SRT motion without cycling crowd appearances");
    /* Its race tables come from the course's KCL and NKMD: 26 checkpoints,
     * one respawn per gate, 37 CPU points, 30 item points, 18 intro camera
     * points, 15 item boxes and
     * an eight-slot grid, racing on the imported physics profile. */
    static const struct
    {
        const char tag[5];
        uint32_t count;
    } tables[] = {{"CPNT", 26}, {"RESP", 26}, {"RTHD", 2 + 3}, {"RTND", 37 + 30 + 18}, {"ITEM", 15}, {"GRID", 8}};
    for (size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); ++i)
    {
        const char *t = tables[i].tag;
        const PortableCourse3DTable *table = portable_course_3d_table(&course, PORTABLE_COURSE_3D_TAG(t[0], t[1], t[2], t[3]));
        expect(table != NULL && table->count == tables[i].count, t);
    }
    PortableCourse3DRoute cpu;
    PortableCourse3DProperty continuous;
    expect(portable_course_3d_route(&course, 0, &cpu) && cpu.kind == 0 &&
               portable_course_3d_property_find(&course, cpu.property_first, cpu.property_count,
                                                "mksc:route-pilot-continuous", &continuous) &&
               continuous.value.boolean,
           "route aids drive the CPU route with the rivals' controller");
    PortableCourse3DMeta meta;
    PortableMeshKartRules rules;
    expect(portable_course_3d_meta(&course, &meta) && meta.laps == 3 && meta.physics_profile_id == MKDS_PHYSICS_ID &&
               portable_mesh_kart_rules_asset_load(&pack, MKDS_PHYSICS_ID, &rules),
           "Figure-8 races three laps on the imported physics profile");
    /* Figure-8 plays SDAT sequence 74, its theme; every course theme and the
     * battle theme are imported once each as playable DS songs. */
    expect(meta.music_kind == MKDS_SOUND_SECTION_KIND && meta.music_id == MKDS_MUSIC_ID(74u),
           "Figure-8 names its DS theme");
    uint32_t songs = 0;
    PortableAssetSectionView section;
    for (uint32_t i = 0; portable_asset_pack_section_at(&pack, i, &section) == PORTABLE_ASSET_PACK_OK; ++i)
    {
        PortableNdsSequenceView song;
        if (section.kind != MKDS_SOUND_SECTION_KIND)
            continue;
        songs += section.content_id < MKDS_EFFECT_ID(0, 0);
        expect(portable_nds_sequence_open(&song, section.bytes, section.size) == PORTABLE_NDS_OK, "DS song opens");
    }
    expect(songs == 36u, "29 distinct DS themes and 7 event songs");
    /* Courses race with the DS sounds: effects from the sequence archives,
     * each trimmed to what it reaches, and every driver's own voices. */
    PortableCourse3DProperty sound_property;
    PortableSoundSet sounds;
    expect(portable_course_3d_property_find(&course, meta.property_first, meta.property_count, "mksc:sound-set",
                                            &sound_property) &&
               sound_property.value.asset.kind == PORTABLE_ASSET_SECTION_SOUND_SET &&
               portable_sound_set_find(&pack, sound_property.value.asset.id, &sounds) && sounds.voice_count == 21u,
           "Figure-8 races with the DS sound set, its 13 drivers and the 8 stock ones voiced");
    /* ...and the DS shadows and particle art: one shadow-volume footprint per
     * kart and the race's SPL particle styles; no MapObj emits particles
     * (Yoshi Falls' FallsWaterDst are pull targets); Cheep Cheep Beach's
     * water throws spray. */
    PortableCourse3DProperty fx_property;
    static PortableFxSet fx;
    expect(portable_course_3d_property_find(&course, meta.property_first, meta.property_count, "mksc:fx-set",
                                            &fx_property) &&
               fx_property.value.asset.kind == PORTABLE_ASSET_SECTION_FX_SET &&
               portable_fx_set_find(&pack, fx_property.value.asset.id, &fx) && fx.shadow_count == 37u &&
               fx.style_count == 8u && fx.emitter_count == 0u,
           "Figure-8 races with the DS effect set");
    const PortableFxShadow *standard = portable_fx_set_shadow(&fx, 0x4D4B4B01u, 0);
    expect(standard && (standard->flags & PORTABLE_FX_SHADOW_VOLUME) && standard->half_width_q16 > 26000 &&
               standard->half_width_q16 < 30000 && standard->forward_q16 > 0,
           "the Standard kart's shadow volume is 0.86 m wide and reaches ahead of the kart");
    PortableCourse3DView falls, beach;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(1), &falls, error, sizeof error) &&
               portable_course_3d_open(&pack, MKDS_COURSE_ID(2), &beach, error, sizeof error), error);
    const PortableCourse3DTable *falls_zones = portable_course_3d_table(&falls, PORTABLE_COURSE_3D_TAG('Z', 'O', 'N', 'E'));
    uint32_t sprays = 0;
    for (uint32_t i = 0; falls_zones && i < falls_zones->count; ++i)
    {
        PortableCourse3DEmitterZone zone;
        sprays += portable_course_3d_emitter_zone(&falls, i, &zone) && zone.emitter == 2u;
    }
    expect(sprays == 0u, "Yoshi Falls places no spray at its falls-water pull targets");
    uint32_t wet = 0;
    const PortableCourse3DTable *beach_surfaces = portable_course_3d_table(&beach, PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'));
    for (uint32_t i = 0; beach_surfaces && i < beach_surfaces->count; ++i)
    {
        PortableCourse3DSurface surface;
        wet += portable_course_3d_surface(&beach, i, &surface) && surface.splash;
    }
    expect(wet > 0, "Cheep Cheep Beach's shallow water splashes");
    /* DS kart sphere 8 of 20 units/m; cam row 0 (58, 34, 20); half-FOV 5500 turns. */
    PortableCourse3DChase chase;
    char chase_error[128];
    expect(portable_course_3d_chase(&course, &chase, chase_error, sizeof chase_error) && chase.has_rig &&
               chase.kart_scale_q16 == 52429 && chase.fov_turns == 11000u && chase.eye_q16[0] == 0 &&
               chase.eye_q16[2] > chase.target_q16[2] && chase.eye_q16[1] < -65536,
           "Figure-8 carries the DS kart scale and chase camera");
    uint16_t engine = 0, boost = 0, struck = 0;
    expect(portable_sound_set_cue(&sounds, PORTABLE_SOUND_CUE_ENGINE_THROTTLE, &engine) &&
               engine == (0x8000u | MKDS_EFFECT_ID(0, 173u)) &&
               portable_sound_set_voice(&sounds, 201u, &boost, &struck) && boost != struck,
           "DS engine and Dry Bones' own voices are in the set");
    /* Goal fanfares lead into the results music; floors pick their sounds;
     * every driver voices the race events. */
    uint16_t fanfare = 0, grass = 0, asphalt = 0, start = 0, goal = 0;
    PortableNdsSequenceView fanfare_song;
    expect(sounds.final_lap_tempo == 300u && portable_sound_set_cue(&sounds, 5u, &fanfare) &&
               fanfare == (0x8000u | MKDS_MUSIC_ID(46u)) &&
               portable_asset_pack_find_section(&pack, MKDS_SOUND_SECTION_KIND, MKDS_MUSIC_ID(46u), &section) ==
                   PORTABLE_ASSET_PACK_OK &&
               portable_nds_sequence_open(&fanfare_song, section.bytes, section.size) == PORTABLE_NDS_OK &&
               fanfare_song.follow == MKDS_MUSIC_ID(56u),
           "DS first-place fanfare, chained into its results music, and the faster final lap");
    expect(portable_sound_set_cue(&sounds, PORTABLE_SOUND_CUE_SURFACE_DRIFT | 8u, &grass) &&
               portable_sound_set_cue(&sounds, PORTABLE_SOUND_CUE_SURFACE_DRIFT | 1u, &asphalt) &&
               grass == (0x8000u | MKDS_EFFECT_ID(0, 92u)) && asphalt == (0x8000u | MKDS_EFFECT_ID(0, 84u)),
           "grass and asphalt drift sounds differ");
    expect(portable_sound_set_voice_event(&sounds, 201u, PORTABLE_SOUND_VOICE_START_BOOST, &start) &&
               portable_sound_set_voice_event(&sounds, 201u, PORTABLE_SOUND_VOICE_FINISH_FIRST, &goal) &&
               start != goal && start != boost && start != struck,
           "Dry Bones voices the rocket start and the finish");
    int floors = 0;
    for (uint32_t i = 0; i < portable_course_3d_table(&course, PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'))->count; ++i)
    {
        PortableCourse3DSurface surface;
        floors += portable_course_3d_surface(&course, i, &surface) && surface.sound_surface;
    }
    expect(floors >= 2, "Figure-8 floors carry their DS sound material");
    static PortableNdsPlayer player;
    int16_t frame[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u];
    int heard = 0;
    /* Every DS song the set or Dry Bones' voices name plays. */
    uint16_t songs_heard[PORTABLE_SOUND_SET_ROW_LIMIT + PORTABLE_SOUND_VOICE_EVENTS + 4u] = {engine, boost, struck};
    uint32_t song_count = 3u;
    for (uint32_t i = 0; i < sounds.cue_count; ++i)
        if (sounds.cues[i * 4u + 3u] & 0x80u)
            songs_heard[song_count++] = (uint16_t)(sounds.cues[i * 4u + 2u] | sounds.cues[i * 4u + 3u] << 8);
    for (unsigned event = 0; event < PORTABLE_SOUND_VOICE_EVENTS; ++event)
        song_count += portable_sound_set_voice_event(&sounds, 201u, (PortableSoundVoiceEvent)event,
                                                     &songs_heard[song_count]);
    songs_heard[song_count] = 0;
    for (const uint16_t *song = songs_heard; *song; ++song)
    {
        PortableNdsSequenceView effect;
        expect(portable_asset_pack_find_section(&pack, MKDS_SOUND_SECTION_KIND, *song & 0x7fffu, &section) ==
                       PORTABLE_ASSET_PACK_OK &&
                   portable_nds_sequence_open(&effect, section.bytes, section.size) == PORTABLE_NDS_OK &&
                   section.size < 256u * 1024u,
               "DS effect is a small self-contained song");
        portable_nds_player_start(&player, &effect);
        for (unsigned tick = 0; tick < 60u; ++tick)
        {
            memset(frame, 0, sizeof frame);
            const int mixed = portable_nds_player_mix(&player, frame);
            if (mixed != PORTABLE_NDS_OK)
            {
                printf("DS song 0x%04x fails to play (%d)\n", *song, mixed);
                expect(0, "DS effect plays");
                break;
            }
            for (unsigned i = 0; i < PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u; ++i)
                heard |= frame[i] != 0;
        }
    }
    expect(heard, "DS effects make sound");
    /* Courses race with the DS item rules: ItemSlotList.dat's GP odds per
     * place (1st place: green shell 45, banana 90, fake box 35 and triple
     * bananas 30 of 200; last place ends on Bullet Bill), VS its own table and
     * battle stages the balloon battle one; each cup scores rankpoint.rpt. */
    uint32_t item_rules = 0, versus_rules = 0;
    expect(portable_course_3d_item_rules(&course, &item_rules, error, sizeof error) && item_rules == MKDS_ITEMS_ID &&
               portable_course_3d_item_rules_mode(&course, 8u, &versus_rules, error, sizeof error) &&
               versus_rules == MKDS_ITEMS_VS_ID,
           "Figure-8 races with the DS GP and VS item rules");
    PortableMeshItemRules odds, versus;
    expect(portable_mesh_item_rules_asset_load(&pack, MKDS_ITEMS_ID, &odds) &&
               odds.player_weights[0][0].item == 0x1000u && odds.player_weights[0][0].weight == 45u &&
               odds.player_weights[0][1].item == 0x1005u && odds.player_weights[0][1].weight == 90u &&
               odds.player_weights[0][2].item == PORTABLE_MESH_ITEM_FAKE_BOX && odds.player_weights[0][2].weight == 35u &&
               odds.player_weights[0][3].item == PORTABLE_MESH_ITEM_TRIPLE_BANANAS &&
               odds.player_weights[0][4].item == 0u && odds.player_weights[7][0].item == 0x1008u &&
               odds.player_weights[7][4].item == PORTABLE_MESH_ITEM_BULLET &&
               odds.player_weights[7][4].weight == 45u && odds.cpu_weights[0][0].item == 0x1000u &&
               odds.cpu_weights[0][0].weight == 60u,
           "DS item odds per place");
    expect(portable_mesh_item_rules_asset_load(&pack, MKDS_ITEMS_VS_ID, &versus) &&
               versus.player_weights[0][0].weight == 40u && versus.cpu_weights[0][0].weight == 80u,
           "DS VS item odds");
    PortableGpScoringRules points;
    static const uint8_t ds_points[8] = {10, 8, 6, 4, 3, 2, 1, 0};
    expect(portable_recovered_cup_scoring(&pack, MKDS_CUP_ID(0u), &points) &&
               memcmp(points.finish_points, ds_points, 8) == 0 && points.initial_retry_credits == 3u,
           "DS cup points");
    static const uint32_t battle_boxes[6] = {20, 15, 14, 16, 12, 8};
    for (uint32_t stage = 0; stage < 6u; ++stage)
    {
        PortableCourse3DView arena;
        expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(32u + stage), &arena, error, sizeof error), error);
        const PortableCourse3DTable *grid = portable_course_3d_table(&arena, PORTABLE_COURSE_3D_TAG('G', 'R', 'I', 'D'));
        const PortableCourse3DTable *boxes = portable_course_3d_table(&arena, PORTABLE_COURSE_3D_TAG('I', 'T', 'E', 'M'));
        expect(grid && grid->count == 8u && boxes && boxes->count == battle_boxes[stage] &&
                   portable_course_3d_item_rules(&arena, &item_rules, error, sizeof error) &&
                   item_rules == MKDS_ITEMS_BATTLE_ID,
               "battle stage spawns, item boxes and item rules");
    }
    /* Its 22 crossed-plane trees join the mesh with wall prisms; nothing is left out. */
    PortableCourse3DProperty missing;
    expect(!portable_course_3d_property_find(&course, meta.property_first, meta.property_count, "mksc:incomplete",
                                             &missing),
           "Figure-8 has no unconverted MapObj families");
    /* Figure-8's CAME chain 0 -> 2 -> 4 becomes three intro shots on camera routes. */
    static const uint32_t intro_ticks[3] = {210, 200, 250};
    for (uint32_t order = 0; order < 4u; ++order)
    {
        PortableCourse3DIntroShot shot;
        PortableCourse3DRoute camera_route;
        const int found = portable_course_3d_intro_shot(&course, order, &shot);
        expect(order == 3u ? !found
                           : found && shot.ticks == intro_ticks[order] &&
                                 portable_course_3d_route(&course, shot.route, &camera_route) && camera_route.kind == 2u,
               "Figure-8 plays its three intro shots");
    }
    /* Every course carries its Map2D minimap; race courses their cup-screen
     * picture and intro. */
    for (uint32_t index = 0; index < 38u; ++index)
    {
        PortableCourse3DView view;
        PortableCourse3DMeta row;
        PortableCourse3DMinimap minimap;
        PortableNativeImage picture;
        expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(index), &view, error, sizeof error), error);
        expect(portable_course_3d_meta(&view, &row) && portable_course_3d_minimap(&view, &minimap) &&
                   minimap.image.width > 16u && minimap.image.height > 16u,
               "imported courses show their minimap");
        expect(row.modes == 16u || (portable_course_3d_texture(&view, row.preview_id, &picture) &&
                                    !picture.palette && picture.width >= 64u && picture.width <= 80u && picture.height == 88u),
               "imported race courses show their cup-screen picture");
        PortableCourse3DIntroShot first_shot;
        expect(row.modes == 16u || portable_course_3d_intro_shot(&view, 0u, &first_shot),
               "imported race courses open with their intro cameras");
    }
    uint32_t walls = 0;
    for (uint32_t i = 0; i < portable_course_3d_table(&course, PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'))->count; ++i)
    {
        PortableCourse3DSurface surface;
        expect(portable_course_3d_surface(&course, i, &surface), "surface decodes");
        walls += surface.type == 3u;
        expect(surface.type != 3u || surface.one_sided, "DS walls are one-sided, as in KCL");
    }
    expect(walls > 0, "Figure-8 has walls");
    /* Waluigi Pinball's KCL cannon fires karts at its one KTPC point. */
    PortableCourse3DView pinball;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(6), &pinball, error, sizeof error), error);
    uint32_t cannons = 0;
    for (uint32_t i = 0; i < portable_course_3d_table(&pinball, PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'))->count; ++i)
    {
        PortableCourse3DSurface surface;
        expect(portable_course_3d_surface(&pinball, i, &surface), "surface decodes");
        if (surface.type == 9u)
            cannons += surface.launch_q16 > 0 && surface.cannon_target_q16[2] > 100 * 65536 &&
                       surface.cannon_heading_q16[1] < -65000;
    }
    expect(cannons == 1, "Waluigi Pinball's cannon targets the table top, heading on");
    /* Rainbow Road's KCL loop road holds karts to its face. */
    PortableCourse3DView rainbow;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(15), &rainbow, error, sizeof error), error);
    uint32_t loops = 0;
    for (uint32_t i = 0; i < portable_course_3d_table(&rainbow, PORTABLE_COURSE_3D_TAG('S', 'U', 'R', 'F'))->count; ++i)
    {
        PortableCourse3DSurface surface;
        expect(portable_course_3d_surface(&rainbow, i, &surface), "surface decodes");
        loops += surface.surface_gravity && surface.type == 0u;
    }
    expect(loops == 1, "Rainbow Road's loop road has surface gravity");
    /* Bowser Castle 2's jump pads: gate 23's KTPJ stands 9 m before a pad,
     * so its respawn moves back along the CPU line to 40 m of plain road,
     * facing the same way as gate 22's, whose road ahead is long enough to
     * keep its KTPJ. Every respawn keeps its own gate. */
    PortableCourse3DView castle;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(22), &castle, error, sizeof error), error);
    PortableCourse3DRespawn kept, moved;
    expect(portable_course_3d_respawn(&castle, 22, &kept) && portable_course_3d_respawn(&castle, 23, &moved),
           "Bowser Castle 2 respawns decode");
    expect(kept.position_q16[1] < -57 * 65536 && kept.position_q16[1] > -58 * 65536 && moved.position_q16[1] < -45 * 65536 &&
               moved.position_q16[1] > kept.position_q16[1],
           "Bowser Castle 2 respawn 23 runs up to the jump pads");
    int64_t facing = 0;
    for (unsigned k = 0; k < 4; ++k)
        facing += (int64_t)kept.quaternion_q30[k] * moved.quaternion_q30[k] >> 30;
    expect(facing > 1062895002, "the run-up start faces along the CPU line");
    for (uint32_t gate = 0; gate < portable_course_3d_table(&castle, PORTABLE_COURSE_3D_TAG('R', 'E', 'S', 'P'))->count;
         ++gate)
    {
        PortableCourse3DRespawn point;
        PortableCourse3DRoute line;
        expect(portable_course_3d_respawn(&castle, gate, &point) && point.checkpoint == gate,
               "respawns keep their gate");
        expect(portable_course_3d_route(&castle, point.route, &line) && line.kind == 0u,
               "respawns hand recovering CPUs the CPU route");
    }
    /* Desert Hills' sun and Luigi's Mansion's chandeliers, pictures and
     * moving trees play their NSBCA: OBJS meshes with a looping IDLE rig. */
    const uint32_t animated_courses[2] = {4, 3}, animated_expected[2] = {1, 4};
    for (unsigned course = 0; course < 2; ++course)
    {
        PortableCourse3DView view;
        expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(animated_courses[course]), &view, error, sizeof error),
               error);
        const PortableCourse3DTable *rows = portable_course_3d_table(&view, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
        uint32_t meshes[64], mesh_count = 0;
        for (uint32_t i = 0; rows && i < rows->count; ++i)
        {
            const uint32_t mesh_id = ri_le32(rows->bytes + i * 64u + 8u);
            PortableModelView mesh;
            PortableModelRig rig;
            int seen = 0;
            for (uint32_t k = 0; k < mesh_count; ++k)
                seen |= meshes[k] == mesh_id;
            if (seen || !portable_model_mesh_open(&pack, mesh_id, &mesh) ||
                !portable_model_rig_open(&pack, mesh_id, &mesh, &rig))
                continue;
            const int clip = portable_model_rig_clip(&rig, PORTABLE_MODEL_CLIP_TAG('I', 'D', 'L', 'E'));
            expect(clip >= 0 && (rig.clips[clip].flags & PORTABLE_MODEL_CLIP_LOOP) && rig.clips[clip].frames > 1,
                   "animated MapObj rig loops an IDLE clip");
            int moves = 0;
            for (uint32_t node = 0; node < rig.node_count && !moves; ++node)
            {
                int32_t first[12], later[12];
                portable_model_rig_pose(&rig, (uint32_t)clip, 0, node, first);
                portable_model_rig_pose(&rig, (uint32_t)clip, rig.clips[clip].frames / 2u, node, later);
                moves = memcmp(first, later, sizeof first) != 0;
            }
            expect(moves, "animated MapObj rig moves between its frames");
            meshes[mesh_count++] = mesh_id;
        }
        expect(mesh_count == animated_expected[course], "animated MapObj models per course");
    }
    /* Tick-Tock Clock's gears, cylinders, pendulum and clock hands are OBJS
     * rows drawing their own meshes; the collision they own turns with them,
     * and the pendulum's bob knocks karts. */
    PortableCourse3DView clock;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(9), &clock, error, sizeof error), error);
    const PortableCourse3DTable *objects = portable_course_3d_table(&clock, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
    uint32_t moving = 0;
    for (uint32_t i = 0; objects && i < objects->count; ++i)
    {
        PortableMeshMotion motion;
        moving += portable_mesh_collision_object_motion(&clock, i, &motion) == 1;
    }
    expect(moving == 10, "Tick-Tock Clock moves its ten clockwork parts");
    PortableMeshCollisionIndex *index = portable_mesh_collision_index_create(&clock, error, sizeof error);
    expect(index != NULL, error);
    const PortableMeshCourse *rest = portable_mesh_collision_index_course(index);
    uint32_t owned = PORTABLE_COURSE_3D_ABSENT;
    for (uint32_t i = 0; owned == PORTABLE_COURSE_3D_ABSENT && i < rest->triangle_count; ++i)
    {
        PortableCourse3DCollision row;
        expect(portable_course_3d_collision(&clock, i, &row), "collision decodes");
        if (row.owner_object != PORTABLE_COURSE_3D_ABSENT)
            owned = i;
    }
    PortableMeshCollisionPose posed;
    PortableMeshTriangle at_rest, turned;
    expect(owned != PORTABLE_COURSE_3D_ABSENT && rest->triangle(rest->context, owned, &at_rest) &&
               portable_mesh_collision_index_at(rest, 400, &posed)->triangle(&posed, owned, &turned) &&
               memcmp(at_rest.vertices_q16, turned.vertices_q16, sizeof turned.vertices_q16) != 0,
           "a gear's collision turns with it");
    int32_t bob[PORTABLE_MESH_COLLISION_HAZARD_LIMIT][3], swung[PORTABLE_MESH_COLLISION_HAZARD_LIMIT][3];
    int32_t radii[PORTABLE_MESH_COLLISION_HAZARD_LIMIT];
    expect(portable_mesh_collision_hazards(rest, 0, bob, radii, NULL) == 1 && radii[0] > 65536 &&
               portable_mesh_collision_hazards(rest, 40, swung, radii, NULL) == 1 &&
               memcmp(swung[0], bob[0], sizeof bob[0]) != 0,
           "the pendulum's bob is a hazard that swings with it");
    portable_mesh_collision_index_close(index);
    PortableCourse3DMeta clock_meta;
    expect(portable_course_3d_meta(&clock, &clock_meta) &&
               !portable_course_3d_property_find(&clock, clock_meta.property_first, clock_meta.property_count,
                                                 "mksc:incomplete", &missing),
           "Tick-Tock Clock's moving parts are complete");
    /* Patrols leave from their placement on open and closed PATHs, not
     * PATH[0] plus a persistent offset of up to hundreds of metres. */
    for (unsigned course_index = 6; course_index <= 7; ++course_index)
    {
        PortableCourse3DView patrol;
        expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(course_index), &patrol, error, sizeof error), error);
        objects = portable_course_3d_table(&patrol, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
        unsigned patrollers = 0;
        for (uint32_t object = 0; objects && object < objects->count; ++object)
        {
            PortableMeshMotion motion;
            const int found = portable_mesh_collision_object_motion(&patrol, object, &motion);
            expect(found >= 0, "patrol motion decodes");
            if (found != 1 || motion.kind != PORTABLE_MESH_MOTION_ROUTE) continue;
            ++patrollers;
            expect(motion.face, "patrol models face their authored route");
            expect(!memcmp(motion.points_q16[0], motion.pivot_q16, sizeof motion.pivot_q16),
                   "patrol routes leave from their placement");
            int64_t step = 0;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const int64_t delta = ((int64_t)motion.points_q16[1][axis] - motion.points_q16[0][axis]) / 256;
                step += delta * delta;
            }
            expect(step < 64*256 * (64*256), "a patrol's first leg runs to a nearby POIT");
        }
        expect(patrollers == (course_index == 6 ? 3u : 13u), "Pinball and Ridge retain all their patrollers");
    }
    /* Stadium's four Fireball2 assemblies: two 3x2 arms plus a center,
     * and two five-ball rings. Each orbit is an ordinary moving hazard. */
    PortableCourse3DView stadium;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(12), &stadium, error, sizeof error), error);
    index = portable_mesh_collision_index_create(&stadium, error, sizeof error);
    expect(index != NULL, error);
    rest = portable_mesh_collision_index_course(index);
    expect(portable_mesh_collision_hazards(rest, 0, bob, radii, NULL) == 24 &&
               portable_mesh_collision_hazards(rest, 40, swung, radii, NULL) == 24,
           "four Fireball2 assemblies have twenty-four individual contact spheres");
    objects = portable_course_3d_table(&stadium, PORTABLE_COURSE_3D_TAG('O', 'B', 'J', 'S'));
    unsigned orbiting = 0, centers = 0;
    for (uint32_t object = 0; objects && object < objects->count; ++object)
    {
        const uint8_t *row = objects->bytes + object * objects->stride;
        if (ri_le32(row) != 431) continue;
        PortableMeshMotion motion;
        const int moving_ball = portable_mesh_collision_object_motion(&stadium, object, &motion);
        if (!moving_ball) { ++centers; continue; }
        ++orbiting;
        expect(moving_ball == 1 && motion.kind == PORTABLE_MESH_MOTION_SPIN &&
                   motion.rate > 0, "Fireball2 has reflected local-Z spin");
        double transform[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
        int32_t position[3];
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            position[axis] = (int32_t)ri_le32(row + 12 + axis * 4);
            transform[axis * 4 + 3] = position[axis] / 65536.0;
        }
        expect(memcmp(position, motion.pivot_q16, sizeof position) != 0,
               "Fireball2 keeps its orbit origin separate from each ball placement");
        PortableMeshPose pose;
        portable_mesh_motion_pose(&motion, 40, &pose);
        int32_t moved[3];
        portable_mesh_pose_point(&pose, position, moved);
        portable_render_model_motion(&motion, 40, transform);
        for (unsigned axis = 0; axis < 3; ++axis)
            expect(fabs(transform[axis * 4 + 3] - moved[axis] / 65536.0) < 2.0 / 65536.0,
                   "Fireball2 model origin follows its collision orbit");
    }
    expect(orbiting == 22 && centers == 2, "all arms orbit and both center balls stay centered");
    expect(memcmp(swung, bob, 24 * sizeof bob[0]) != 0, "fireball hazards advance with the race tick");
    portable_mesh_collision_index_close(index);
    /* Desert Hills' Pokeys patrol their PATH routes and knock karts on the way. */
    PortableCourse3DView desert;
    expect(portable_course_3d_open(&pack, MKDS_COURSE_ID(4), &desert, error, sizeof error), error);
    index = portable_mesh_collision_index_create(&desert, error, sizeof error);
    expect(index != NULL, error);
    rest = portable_mesh_collision_index_course(index);
    const uint32_t pokeys = portable_mesh_collision_hazards(rest, 0, bob, radii, NULL);
    expect(pokeys > 0 && portable_mesh_collision_hazards(rest, 600, swung, radii, NULL) == pokeys &&
               memcmp(swung, bob, (size_t)pokeys * sizeof bob[0]) != 0,
           "Desert Hills' Pokeys patrol as hazards");
    portable_mesh_collision_index_close(index);
}

static void test_cartridge(const char *path)
{
    size_t size = 0;
    uint8_t *rom = read_all(path, &size);
    expect(rom != NULL, "read --mkds ROM");
    if (!rom)
        return;
    char error[256];
    expect(mksc_mkds_identify(rom, size, error, sizeof error), "owner ROM identified");
    MkscRomImportResult first, second;
    MkscMkdsInventory inventory;
    expect(mksc_mkds_import(rom, size, NULL, NULL, &first, &inventory, error, sizeof error) == 1, "import");
    expect(inventory.cups == 8 && inventory.courses == 32 && inventory.battle_stages == 6 && inventory.drivers == 13,
           "inventory counts");
    expect(inventory.converted == 38 && inventory.skipped == 0, "every course and battle stage converts");
    check_courses(&first, inventory.converted);
    expect(mksc_mkds_import(rom, size, NULL, NULL, &second, NULL, error, sizeof error) == 1, "second import");
    expect(first.pack_size == second.pack_size && memcmp(first.pack, second.pack, first.pack_size) == 0,
           "imports are byte-identical");
    char digest[65];
    mksc_hex(first.pack_sha256, 32, digest);
    mksc_rom_import_free(&first);
    mksc_rom_import_free(&second);

    uint32_t reached = 0;
    expect(mksc_mkds_import(rom, size, cancel_at_three, &reached, &first, NULL, error, sizeof error) == -1 &&
               reached == 3 && first.pack == NULL,
           "progress cancels");
    rom[size / 2] ^= 0xFF;
    expect(!mksc_mkds_identify(rom, size, error, sizeof error) && strstr(error, "modified"), "edited ROM named");
    free(rom);
    printf("mkds import: PASS pack_sha256=%s\n", digest);
}

int main(int argc, char **argv)
{
    test_readers();
    test_spa();
    test_map_readers();
    test_identify();
    Bytes empty = {{0}, 0};
    Case variants = {&empty, NULL, {{0}}, 0};
    run(group_pattern_variants, &variants, "fixed texture pattern variants");
    if (argc == 3 && strcmp(argv[1], "--mkds") == 0)
        test_cartridge(argv[2]);
    if (failures == 0)
        puts("nds import: PASS");
    return failures != 0;
}
