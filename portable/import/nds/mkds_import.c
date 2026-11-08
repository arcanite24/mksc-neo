#include "mkds_import.h"
#include "mkds_course.h"
#include "mkds_driver.h"
#include "mkds_fx.h"
#include "mkds_item_art.h"
#include "mkds_rules.h"
#include "mkds_sound.h"
#include "nds_fs.h"

#include <stdio.h>
#include <string.h>

/* Facts the US ARM9 hard-codes rather than storing in the filesystem: the
 * course folders of each cup in order, the common.bmg messages naming cups,
 * courses and drivers, and each course's SDAT theme (byte 1 of its sound-set
 * row at 0x0215316C; battle stages share one). Battle stages follow the race
 * courses. */
typedef struct MkdsCourse
{
    const char *folder;
    uint8_t kind, cup, slot;
    uint16_t message;
    uint8_t music; /* SDAT sequence of its theme */
} MkdsCourse;

#define MKDS_CUP_MESSAGE 37u
#define MKDS_RACE(folder, cup, slot, music) {folder, MKSC_MKDS_ROW_COURSE, cup, slot, 45u + (cup) * 4u + (slot), music}
#define MKDS_BATTLE(folder, slot, music) {folder, MKSC_MKDS_ROW_BATTLE, 0u, slot, 77u + (slot), music}

static const MkdsCourse courses[] = {
    MKDS_RACE("cross_course", 0, 0, 74),   MKDS_RACE("bank_course", 0, 1, 16),
    MKDS_RACE("beach_course", 0, 2, 15),   MKDS_RACE("mansion_course", 0, 3, 21),
    MKDS_RACE("desert_course", 1, 0, 38),  MKDS_RACE("town_course", 1, 1, 17),
    MKDS_RACE("pinball_course", 1, 2, 19), MKDS_RACE("ridge_course", 1, 3, 36),
    MKDS_RACE("snow_course", 2, 0, 37),    MKDS_RACE("clock_course", 2, 1, 39),
    MKDS_RACE("mario_course", 2, 2, 74),   MKDS_RACE("airship_course", 2, 3, 18),
    MKDS_RACE("stadium_course", 3, 0, 19), MKDS_RACE("garden_course", 3, 1, 20),
    MKDS_RACE("koopa_course", 3, 2, 40),   MKDS_RACE("rainbow_course", 3, 3, 41),
    MKDS_RACE("old_mario_sfc", 4, 0, 22),  MKDS_RACE("old_momo_64", 4, 1, 30),
    MKDS_RACE("old_peach_agb", 4, 2, 26),  MKDS_RACE("old_luigi_gc", 4, 3, 33),
    MKDS_RACE("old_donut_sfc", 5, 0, 24),  MKDS_RACE("old_frappe_64", 5, 1, 31),
    MKDS_RACE("old_koopa_agb", 5, 2, 27),  MKDS_RACE("old_baby_gc", 5, 3, 34),
    MKDS_RACE("old_noko_sfc", 6, 0, 23),   MKDS_RACE("old_choco_64", 6, 1, 29),
    MKDS_RACE("old_luigi_agb", 6, 2, 26),  MKDS_RACE("old_kinoko_gc", 6, 3, 35),
    MKDS_RACE("old_choco_sfc", 7, 0, 25),  MKDS_RACE("old_hyudoro_64", 7, 1, 32),
    MKDS_RACE("old_sky_agb", 7, 2, 28),    MKDS_RACE("old_yoshi_gc", 7, 3, 33),
    MKDS_BATTLE("mini_block_64", 0, 43),   MKDS_BATTLE("mini_dokan_gc", 1, 43),
    MKDS_BATTLE("mini_stage1", 2, 43),     MKDS_BATTLE("mini_stage2", 3, 43),
    MKDS_BATTLE("mini_stage3", 4, 43),     MKDS_BATTLE("mini_stage4", 5, 43),
};

/* ds_id is the game's character number. Every character is its own
 * selectable 3D-only driver (IDs 200..212; the five without a Super Circuit
 * counterpart took 200..204 first) with a stock donor of the same build for
 * 2D fallbacks and the rival roster; it speaks with its own DS voices. Each character owns the
 * kartoffsetdata rows ds_id*3.. (Standard, then its own two karts) named from
 * kart_select.bmg; Shy Guy owns only a Standard, which the text never names. */
typedef struct MkdsDriver
{
    const char *folder, *code;
    uint8_t main_archive; /* Shy Guy's player model ships beside the karts */
    uint16_t message;
    uint8_t ds_id, driver_id, donor;
    const char *kart_folder, *kart_code; /* own karts in KartModelMain */
    uint16_t kart_message;               /* the Standard's name; the two others follow */
    const char *standard_name;           /* when the text has none */
} MkdsDriver;

#define MKDS_KART_NAMES(group) (uint16_t)(6u + (group) * 3u)
static const MkdsDriver drivers[] = {
    {"mario", "MR", 0, 20, 0, 205, 1, "mario", "MR", MKDS_KART_NAMES(0), NULL},
    {"luigi", "LG", 0, 21, 7, 206, 2, "luigi", "LG", MKDS_KART_NAMES(1), NULL},
    {"peach", "PC", 0, 22, 4, 207, 3, "peach", "PC", MKDS_KART_NAMES(2), NULL},
    {"yoshi", "YS", 0, 23, 6, 208, 5, "yoshi", "YS", MKDS_KART_NAMES(3), NULL},
    {"kinopio", "KO", 0, 24, 2, 209, 4, "kinopio", "KO", MKDS_KART_NAMES(4), NULL},
    {"donkey", "DK", 0, 25, 1, 210, 6, "donkey", "DK", MKDS_KART_NAMES(5), NULL},
    {"wario", "WR", 0, 26, 5, 211, 7, "wario", "WR", MKDS_KART_NAMES(6), NULL},
    {"koopa", "KP", 0, 27, 3, 212, 8, "koopa", "KP", MKDS_KART_NAMES(7), NULL},
    {"daisy", "DS", 0, 28, 9, 200, 3, "daisy", "DS", MKDS_KART_NAMES(8), NULL},
    {"karon", "KA", 0, 29, 8, 201, 8, "oyama", "OY", MKDS_KART_NAMES(9), NULL},
    {"waluigi", "WL", 0, 30, 10, 202, 7, "waluigi", "WL", MKDS_KART_NAMES(10), NULL},
    {"robo", "RB", 0, 31, 11, 203, 6, "robo", "RB", MKDS_KART_NAMES(11), NULL},
    {"heyho", "HH", 1, 32, 12, 204, 4, "heyho", "HH", 0, "STANDARD SG"},
};

/* Retro course names start with a two-glyph platform badge. */
static const NdsGlyph badges[] = {
    {0xE037, "SNES "}, {0xE038, ""}, {0xE039, "GBA "}, {0xE03A, ""},
    {0xE03B, "N64 "},  {0xE03C, ""}, {0xE03D, "GCN "}, {0xE03E, ""},
};

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const char *region_name(char letter)
{
    switch (letter)
    {
    case 'J':
        return "Japan";
    case 'P':
        return "Europe";
    case 'K':
        return "Korea";
    default:
        return "another region";
    }
}

static int identify(const uint8_t *rom, size_t size, uint8_t digest[20], char *error, size_t error_size)
{
    if (error && error_size)
        error[0] = 0;
    if (!nds_header_valid(rom, size))
    {
        snprintf(error, error_size, "not a Nintendo DS ROM");
        return 0;
    }
    char title[13], code[5];
    for (size_t index = 0; index < 12; ++index)
        title[index] = rom[index] >= 0x20 && rom[index] < 0x7F ? (char)rom[index] : 0;
    title[12] = 0;
    for (size_t index = 0; index < 4; ++index)
        code[index] = rom[12 + index] >= 0x20 && rom[12 + index] < 0x7F ? (char)rom[12 + index] : '?';
    code[4] = 0;
    if (memcmp(code, MKSC_MKDS_GAME_CODE, 3) != 0)
    {
        snprintf(error, error_size, "%s (%s) is not a supported game; import Mario Kart DS (US)",
                 title[0] ? title : "this DS game", code);
        return 0;
    }
    if (code[3] != MKSC_MKDS_GAME_CODE[3])
    {
        snprintf(error, error_size, "Mario Kart DS (%s) is not supported yet; use the US cartridge (%s)",
                 region_name(code[3]), MKSC_MKDS_GAME_CODE);
        return 0;
    }
    const size_t used = ri_le32(rom + 0x80);
    if (size > MKSC_MKDS_MAX_ROM_BYTES || used <= 0x8000 || used > size)
    {
        snprintf(error, error_size, "this Mario Kart DS ROM is truncated or oversized (%zu bytes, %zu used)",
                 size, used);
        return 0;
    }
    char hex[41];
    mksc_sha1(rom + 0x8000, used - 0x8000, digest);
    mksc_hex(digest, 20, hex);
    if (strcmp(hex, MKSC_MKDS_CONTENT_SHA1) != 0)
    {
        snprintf(error, error_size,
                 "this Mario Kart DS (US) ROM is modified or another revision; use an unmodified dump");
        return 0;
    }
    return 1;
}

int mksc_mkds_identify(const uint8_t *rom, size_t size, char *error, size_t error_size)
{
    uint8_t digest[20];
    return identify(rom, size, digest, error, error_size);
}

typedef struct MkdsJob
{
    MkscMkdsInventory inventory;
    uint32_t done, total;
    char cup_names[8][32];
    char course_names[COUNT(courses)][32];
    char driver_names[COUNT(drivers)][32];
    char kart_names[MKDS_KART_COUNT][32];
} MkdsJob;

/* The driver owning kartoffsetdata row kart. */
static size_t kart_owner(RiCtx *ctx, uint32_t kart)
{
    for (size_t index = 0; index < COUNT(drivers); ++index)
        if (drivers[index].ds_id == kart / 3u)
            return index;
    ri_fail(ctx, "Mario Kart DS kart %u has no owner", (unsigned)kart);
}

static void expect(RiCtx *ctx, const NdsFs *fs, const char *path, const char *magic)
{
    const RiData data = nds_file(ctx, fs, path);
    if (data.size < 4 || memcmp(data.data, magic, 4) != 0)
        ri_fail(ctx, "%s: %s is not %.4s data", fs->name, path, magic);
}

static void put_row(RiCtx *ctx, RiBuf *stamp, uint32_t kind, uint32_t cup, uint32_t slot,
                    const char *folder, const char *name)
{
    uint8_t row[64] = {(uint8_t)kind, (uint8_t)cup, (uint8_t)slot, 0};
    const size_t folder_length = strlen(folder), name_length = strlen(name);
    if (folder_length >= 28 || name_length >= 32 || name_length == 0)
        ri_fail(ctx, "Mario Kart DS inventory name \"%s\" (%s) does not fit", name, folder);
    memcpy(row + 4, folder, folder_length);
    memcpy(row + 32, name, name_length);
    ri_put(ctx, stamp, row, sizeof(row));
}

/* Checks that every course and driver the later conversions need is present
 * and records the inventory with names from the US text. */
static void group_inventory(RiCtx *ctx)
{
    MkdsJob *job = ctx->job;
    uint8_t digest[20];
    char why[256];
    if (!identify(ctx->rom.data, ctx->rom.size, digest, why, sizeof(why)))
        ri_fail(ctx, "%s", why);
    NdsHeader header;
    nds_header(ctx, ctx->rom, &header);
    const NdsFs rom = nds_rom_fs(ctx, ctx->rom, &header);
    job->total = 3u + 2u * (uint32_t)COUNT(courses) + 2u * (uint32_t)COUNT(drivers) + MKDS_KART_COUNT;
    ri_step(ctx, ++job->done, job->total);

    const NdsFs main2d = nds_narc(ctx, nds_file(ctx, &rom, "data/Main2D_us.carc"), "data/Main2D_us.carc");
    const NdsBmg text = nds_bmg(ctx, nds_file(ctx, &main2d, "common.bmg"), "data/Main2D_us.carc/common.bmg");
    RiBuf *stamp = ri_buf(ctx);
    ri_put(ctx, stamp, "MKDSIMP1", 8);
    ri_u32(ctx, stamp, MKSC_MKDS_IMPORT_VERSION);
    ri_put(ctx, stamp, digest, sizeof(digest));
    ri_u32(ctx, stamp, 8u + (uint32_t)COUNT(courses) + (uint32_t)COUNT(drivers) + MKDS_KART_COUNT);
    char name[32];
    for (uint32_t cup = 0; cup < 8u; ++cup)
    {
        nds_bmg_ascii(ctx, &text, MKDS_CUP_MESSAGE + cup, NULL, 0, name, sizeof(name));
        put_row(ctx, stamp, MKSC_MKDS_ROW_CUP, cup, 0, "", name);
        memcpy(job->cup_names[cup], name, sizeof(name));
        job->inventory.cups++;
    }
    ri_step(ctx, ++job->done, job->total);

    for (size_t index = 0; index < COUNT(courses); ++index)
    {
        const MkdsCourse *course = &courses[index];
        char path[64], texture_path[64];
        snprintf(path, sizeof(path), "data/Course/%s.carc", course->folder);
        snprintf(texture_path, sizeof(texture_path), "data/Course/%sTex.carc", course->folder);
        const NdsFs archive = nds_narc(ctx, nds_file(ctx, &rom, path), path);
        expect(ctx, &archive, "course_model.nsbmd", "BMD0");
        expect(ctx, &archive, "course_map.nkm", "NKMD");
        const RiData collision = nds_file(ctx, &archive, "course_collision.kcl");
        if (collision.size < 0x3C || ri_le32(collision.data) != 0x3Cu)
            ri_fail(ctx, "%s: course_collision.kcl lacks the 0x3C-byte KCL header", path);
        const NdsFs textures = nds_narc(ctx, nds_file(ctx, &rom, texture_path), texture_path);
        expect(ctx, &textures, "course_model.nsbtx", "BTX0");
        nds_bmg_ascii(ctx, &text, course->message, badges, COUNT(badges), name, sizeof(name));
        put_row(ctx, stamp, course->kind, course->cup, course->slot, course->folder, name);
        memcpy(job->course_names[index], name, sizeof(name));
        if (course->kind == MKSC_MKDS_ROW_COURSE)
            job->inventory.courses++;
        else
            job->inventory.battle_stages++;
        ri_step(ctx, ++job->done, job->total);
    }

    static const char *const archives[2] = {"data/KartModelSub.carc", "data/KartModelMain.carc"};
    NdsFs karts[2];
    for (size_t index = 0; index < 2; ++index)
        karts[index] = nds_narc(ctx, nds_file(ctx, &rom, archives[index]), archives[index]);
    for (size_t index = 0; index < COUNT(drivers); ++index)
    {
        const MkdsDriver *driver = &drivers[index];
        char path[64];
        snprintf(path, sizeof(path), "character/%s/P_%s.nsbmd", driver->folder, driver->code);
        expect(ctx, &karts[driver->main_archive], path, "BMD0");
        nds_bmg_ascii(ctx, &text, driver->message, NULL, 0, name, sizeof(name));
        put_row(ctx, stamp, MKSC_MKDS_ROW_DRIVER, 0, (uint32_t)index, driver->folder, name);
        memcpy(job->driver_names[index], name, sizeof(name));
        job->inventory.drivers++;
        ri_step(ctx, ++job->done, job->total);
    }
    const NdsFs select = nds_narc(ctx, nds_file(ctx, &rom, "data/CharacterKartSelect_us.carc"),
                                  "data/CharacterKartSelect_us.carc");
    const NdsBmg kart_text =
        nds_bmg(ctx, nds_file(ctx, &select, "kart_select.bmg"), "data/CharacterKartSelect_us.carc/kart_select.bmg");
    for (uint32_t kart = 0; kart < MKDS_KART_COUNT; ++kart)
    {
        const MkdsDriver *owner = &drivers[kart_owner(ctx, kart)];
        if (owner->standard_name && kart % 3u == 0u)
            snprintf(name, sizeof(name), "%s", owner->standard_name);
        else if (owner->standard_name)
            ri_fail(ctx, "Mario Kart DS kart %u of %s has no name", (unsigned)kart, owner->folder);
        else
            nds_bmg_ascii(ctx, &kart_text, owner->kart_message + kart % 3u, NULL, 0, name, sizeof(name));
        put_row(ctx, stamp, MKSC_MKDS_ROW_KART, 0, kart, owner->kart_folder, name);
        memcpy(job->kart_names[kart], name, sizeof(name));
        job->inventory.karts++;
    }
    /* Kind 80 is the import stamp; ID 1 belongs to the Super Circuit pack. */
    ri_section(ctx, 80, MKSC_MKDS_STAMP_ID, 1, stamp);
    ri_step(ctx, ++job->done, job->total);
}

static void convert_course(RiCtx *ctx, void *argument)
{
    NdsHeader header;
    nds_header(ctx, ctx->rom, &header);
    const NdsFs rom = nds_rom_fs(ctx, ctx->rom, &header);
    mkds_course_convert(ctx, &rom, argument);
}

/* Each course converts on its own: one using a feature the 3D engine cannot
 * show yet is left out and named in the report rather than failing the rest. */
static void group_courses(RiCtx *ctx)
{
    MkdsJob *job = ctx->job;
    mkds_physics_section(ctx, MKDS_PHYSICS_ID);
    NdsHeader header;
    nds_header(ctx, ctx->rom, &header);
    const NdsFs rom = nds_rom_fs(ctx, ctx->rom, &header);
    mkds_items_sections(ctx, &rom);
    mkds_cup_scoring_sections(ctx, &rom, 8u);
    const RiData sdat = nds_file(ctx, &rom, "data/Sound/sound_data.sdat");
    uint8_t themes[128] = {0};
    for (size_t index = 0; index < COUNT(courses); ++index)
        if (!themes[courses[index].music]++)
            mkds_sound_sequence(ctx, sdat, courses[index].music, 0u);
    /* Each DS driver speaks with its own voices, and so do the eight stock
     * drivers (IDs 1..8, the donors) whose DS selves race here. */
    uint32_t driver_ids[2u * COUNT(drivers)];
    uint8_t ds_ids[2u * COUNT(drivers)];
    uint32_t voiced = 0;
    for (size_t index = 0; index < COUNT(drivers); ++index)
    {
        driver_ids[voiced] = drivers[index].driver_id;
        ds_ids[voiced++] = drivers[index].ds_id;
    }
    for (size_t index = 0; index < COUNT(drivers); ++index)
        if (drivers[index].driver_id >= 205u)
        {
            driver_ids[voiced] = drivers[index].donor;
            ds_ids[voiced++] = drivers[index].ds_id;
        }
    mkds_sound_set(ctx, sdat, driver_ids, ds_ids, voiced);
    for (size_t index = 0; index < COUNT(courses); ++index)
    {
        const MkdsCourse *course = &courses[index];
        const uint32_t battle = course->kind == MKSC_MKDS_ROW_BATTLE;
        const MkdsCourseSource source = {course->folder, job->course_names[index],
                                         battle ? "Battle Stages" : job->cup_names[course->cup],
                                         (uint32_t)index, battle, course->cup, course->slot,
                                         MKDS_MUSIC_ID(course->music)};
        char reason[256];
        if (ri_try(ctx, convert_course, (void *)&source, reason, sizeof(reason)))
            job->inventory.converted++;
        else if (job->inventory.skipped++ == 0)
        {
            const char *named = strstr(reason, course->folder) ? "" : course->folder;
            snprintf(job->inventory.skipped_reason, sizeof(job->inventory.skipped_reason), "%s%s%s", named,
                     named[0] ? ": " : "", reason);
        }
        ri_step(ctx, ++job->done, job->total);
    }
}

/* Every driver must convert; their meshes bind as one kind-98 overlay so
 * other model mods keep their own drivers. */
static void group_drivers(RiCtx *ctx)
{
    MkdsJob *job = ctx->job;
    NdsHeader header;
    nds_header(ctx, ctx->rom, &header);
    const NdsFs rom = nds_rom_fs(ctx, ctx->rom, &header);
    RiBuf *rows = ri_buf(ctx);
    for (size_t index = 0; index < COUNT(drivers); ++index)
    {
        const MkdsDriver *driver = &drivers[index];
        const MkdsDriverSource source = {driver->folder, driver->code, job->driver_names[index], (uint32_t)index,
                                         driver->ds_id, driver->driver_id, driver->donor, driver->main_archive};
        mkds_driver_convert(ctx, &rom, &source, rows);
        ri_step(ctx, ++job->done, job->total);
    }
    MkdsDriverSource seated[COUNT(drivers)];
    for (size_t index = 0; index < COUNT(drivers); ++index)
        seated[index] = (MkdsDriverSource){drivers[index].folder, drivers[index].code, job->driver_names[index],
                                           (uint32_t)index, drivers[index].ds_id, drivers[index].driver_id,
                                           drivers[index].donor, drivers[index].main_archive};
    RiBuf *shadows = ri_buf(ctx);
    for (uint32_t kart = 0; kart < MKDS_KART_COUNT; ++kart)
    {
        const size_t owner = kart_owner(ctx, kart);
        const MkdsKartSource source = {kart, job->kart_names[kart], drivers[owner].kart_folder,
                                       drivers[owner].kart_code, &seated[owner], seated, COUNT(drivers)};
        mkds_kart_convert(ctx, &rom, &source);
        mkds_kart_shadow(ctx, &rom, &source, shadows);
        ri_step(ctx, ++job->done, job->total);
    }
    mkds_effects(ctx, &rom, shadows, MKDS_KART_COUNT);
    static const uint32_t item_profiles[] = {MKDS_ITEMS_ID, MKDS_ITEMS_VS_ID, MKDS_ITEMS_BATTLE_ID};
    const uint32_t item_rows = mkds_item_art(ctx, &rom, item_profiles, COUNT(item_profiles), rows);
    RiBuf *bindings = ri_buf(ctx);
    ri_put(ctx, bindings, "BND1", 4);
    ri_u32(ctx, bindings, (uint32_t)COUNT(drivers) + item_rows);
    ri_put_buf(ctx, bindings, rows);
    ri_section(ctx, 98, 1, 2, bindings);
}

int mksc_mkds_import(const uint8_t *rom, size_t size, MkscImportProgress progress, void *context,
                     MkscRomImportResult *result, MkscMkdsInventory *inventory, char *error,
                     size_t error_size)
{
    static const RiGroup groups[] = {group_inventory, group_courses, group_drivers};
    MkdsJob job;
    memset(&job, 0, sizeof(job));
    if (inventory)
        memset(inventory, 0, sizeof(*inventory));
    const int status = ri_build_steps(rom, size, groups, COUNT(groups), &job, progress, context, result,
                                      error, error_size);
    if (status == 1 && inventory)
        *inventory = job.inventory;
    return status;
}
