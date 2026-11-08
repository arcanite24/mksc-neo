#include "game_import_host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "file_replace.h"
#include "course_3d.h"
#include "file_read.h"
#include "mod_manifest.h"
#include "rom_import_host.h"
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#endif

#define STORED_ROM_NAME "mkds-rom-path.txt"
#define STORED_MK64_ROM_NAME "mk64-rom-path.txt"

void portable_game_import_version(const char *id, char *out, size_t capacity)
{
    const int n64 = id && strcmp(id, MKSC_MK64_MOD_ID) == 0;
    snprintf(out, capacity, "1.0.%u", n64 ? (unsigned)MKSC_MK64_IMPORT_VERSION : (unsigned)MKSC_MKDS_IMPORT_VERSION);
}

int portable_game_import_stored_rom(const char *id, char *out, size_t capacity)
{
    const int n64 = id && strcmp(id, MKSC_MK64_MOD_ID) == 0;
    return portable_stored_rom_path_as(n64 ? STORED_MK64_ROM_NAME : STORED_ROM_NAME, out, capacity);
}

static int make_directory(const char *path)
{
#ifdef _WIN32
    struct _stat info;
    return _mkdir(path) == 0 || (errno == EEXIST && _stat(path, &info) == 0 && (info.st_mode & _S_IFDIR));
#else
    struct stat info;
    return mkdir(path, 0755) == 0 || (errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode));
#endif
}

/* Creates path and any missing parents; only the last step must succeed. */
static int make_directories(const char *path)
{
    char partial[4096];
    const size_t length = strlen(path);
    if (length == 0 || length >= sizeof partial) return 0;
    memcpy(partial, path, length + 1);
    for (size_t index = 1; index < length; ++index)
    {
        if ((partial[index] != '/' && partial[index] != '\\') || partial[index - 1] == ':') continue;
        partial[index] = '\0';
        (void)make_directory(partial);
        partial[index] = path[index];
    }
    return make_directory(partial);
}

static int join(char *out, size_t capacity, const char *directory, const char *name)
{
    const size_t length = strlen(directory);
    const int separator = length != 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\');
    const int written = snprintf(out, capacity, "%s%s%s", directory, separator ? "" : "/", name);
    return written > 0 && (size_t)written < capacity;
}

static int write_file(const char *path, const void *data, size_t size)
{
    char temporary[4096];
    if (snprintf(temporary, sizeof temporary, "%s.importing", path) >= (int)sizeof temporary) return 0;
    FILE *file = fopen(temporary, "wb");
    int written = file != NULL && fwrite(data, 1, size, file) == size;
    if (file != NULL && fclose(file) != 0) written = 0;
    if (written && portable_file_replace(temporary, path)) return 1;
    remove(temporary);
    return 0;
}

/* An existing folder of the generated mod's name must hold the importer's
 * own manifest (or none, after an interrupted first import). */
static int folder_is_ours(const char *directory, const char *id, char *error, size_t error_size)
{
    char path[4096];
    if (!join(path, sizeof path, directory, "mod.json")) return 0;
    uint8_t *bytes = NULL;
    size_t size = 0;
    const PortableFileReadStatus status = portable_file_read_bounded(path, 16384u, &bytes, &size);
    if (status == PORTABLE_FILE_READ_NOT_FOUND) return 1;
    PortableModManifest manifest;
    char reason[PORTABLE_MOD_ERROR_CAPACITY];
    const int ours = status == PORTABLE_FILE_READ_OK &&
                     portable_mod_manifest_parse((const char *)bytes, size, &manifest, reason) &&
                     strcmp(manifest.id, id) == 0;
    free(bytes);
    if (!ours)
        snprintf(error, error_size, "%.300s holds another mod; move it away before importing", directory);
    return ours;
}

int portable_game_import_file(const char *rom_path, const char *mods_directory, MkscImportProgress progress,
                              void *context, PortableGameImportReport *report, char *error, size_t error_size)
{
    memset(report, 0, sizeof(*report));
    if (error_size) error[0]='\0';
    unsigned char *rom = NULL;
    size_t rom_size = 0;
    if (!portable_rom_read(rom_path, ".nds;.z64;.v64;.n64", MKSC_MKDS_MAX_ROM_BYTES, &rom, &rom_size, error, error_size))
        return 0;
    report->n64 = mksc_mk64_byte_order(rom,rom_size) != 0;
    const char *id = report->n64 ? MKSC_MK64_MOD_ID : MKSC_MKDS_MOD_ID;
    const char *name = report->n64 ? MKSC_MK64_MOD_NAME : MKSC_MKDS_MOD_NAME;
    MkscRomImportResult result;
    const int imported = report->n64 ?
        mksc_mk64_import(rom,rom_size,progress,context,&result,&report->n64_inventory,error,error_size) :
        mksc_mkds_import(rom,rom_size,progress,context,&result,&report->inventory,error,error_size);
    free(rom);
    if (imported != 1) return imported;
    if (report->n64) {
        PortableAssetPackView pack;
        if (portable_asset_pack_open(result.pack,result.pack_size,&pack)!=PORTABLE_ASSET_PACK_OK ||
            !portable_course_3d_validate_pack(&pack,error,error_size)) {
            if (error_size && !error[0]) snprintf(error,error_size,"MK64: generated pack is invalid");
            mksc_rom_import_free(&result);
            return 0;
        }
    }
    char directory[4096], pack_path[4096], manifest_path[4096];
    if (!join(directory, sizeof directory, mods_directory, id) ||
        !join(pack_path, sizeof pack_path, directory, "content.pack") ||
        !join(manifest_path, sizeof manifest_path, directory, "mod.json"))
    {
        snprintf(error, error_size, "mod folder path too long");
        mksc_rom_import_free(&result);
        return 0;
    }
    if (!make_directories(mods_directory) || !make_directory(directory) ||
        !folder_is_ours(directory, id, error, error_size))
    {
        if (error[0] == '\0') snprintf(error, error_size, "cannot create mod folder %.400s", directory);
        mksc_rom_import_free(&result);
        return 0;
    }
    char version[32], manifest[512];
    portable_game_import_version(id, version, sizeof version);
    const int manifest_size = snprintf(manifest, sizeof manifest,
        "{\n  \"id\": \"%s\",\n  \"name\": \"%s\",\n  \"version\": \"%s\",\n  \"api_version\": 1,\n"
        "  \"pack\": \"content.pack\",\n  \"targets\": [\"3d\"]\n}\n",
        id, name, version);
    /* The pack goes first: a manifest still naming the older version keeps
     * offering the update if the second write fails. */
    const int written = write_file(pack_path, result.pack, result.pack_size) &&
                        write_file(manifest_path, manifest, (size_t)manifest_size);
    mksc_hex(result.pack_sha256, 32, report->pack_sha256);
    mksc_rom_import_free(&result);
    if (!written)
    {
        snprintf(error, error_size, "cannot write the mod in %.400s", directory);
        return 0;
    }
    snprintf(report->mod_directory, sizeof report->mod_directory, "%s", directory);
    (void)portable_store_rom_path_as(report->n64 ? STORED_MK64_ROM_NAME : STORED_ROM_NAME, rom_path);
    return 1;
}

static void summary(const PortableGameImportReport *report, char *out, size_t capacity)
{
    if (report->n64) {
        snprintf(out,capacity,"Imported Mario Kart 64: %u courses in 4 cups race and %u battle stages fight, with scenery, vehicles, water and sky, and the eight racers as selectable sprite drivers.",
                 (unsigned)report->n64_inventory.courses,(unsigned)report->n64_inventory.battle_stages);
        return;
    }
    const MkscMkdsInventory *inventory = &report->inventory;
    const int length = snprintf(out, capacity,
        "Imported %s: %u cups, %u courses, %u battle stages, %u drivers, %u karts; %u viewable in 3D.",
        MKSC_MKDS_MOD_NAME, (unsigned)inventory->cups, (unsigned)inventory->courses,
        (unsigned)inventory->battle_stages, (unsigned)inventory->drivers, (unsigned)inventory->karts,
        (unsigned)inventory->converted);
    if (inventory->skipped && length > 0 && (size_t)length < capacity)
        snprintf(out + length, capacity - (size_t)length, " %u left out, first: %s", (unsigned)inventory->skipped,
                 inventory->skipped_reason);
}

int portable_game_import_command(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "--import-game") != 0) return -1;
    if (argc != 3 && argc != 4)
    {
        fprintf(stderr, "usage: %s --import-game ROM [MODS_DIR]\n", argv[0]);
        return 2;
    }
    char mods[4096];
    if (argc == 4)
        snprintf(mods, sizeof mods, "%s", argv[3]);
    else
    {
        char *directory = portable_data_directory();
        if (directory == NULL)
        {
            fprintf(stderr, "no data directory; pass MODS_DIR explicitly\n");
            return 2;
        }
        const int length = snprintf(mods, sizeof mods, "%smods", directory);
#ifdef PORTABLE_ENABLE_SDL3
        SDL_free(directory);
#endif
        if (length <= 0 || (size_t)length >= sizeof mods)
        {
            fprintf(stderr, "mods directory path too long\n");
            return 2;
        }
    }
    PortableGameImportReport report;
    char error[512];
    if (portable_game_import_file(argv[2], mods, NULL, NULL, &report, error, sizeof error) != 1)
    {
        fprintf(stderr, "import failed: %s\n", error);
        return 1;
    }
    char message[512];
    summary(&report, message, sizeof message);
    printf("%s\nmod=%s pack_sha256=%s\n", message, report.mod_directory, report.pack_sha256);
    return 0;
}

#ifdef PORTABLE_ENABLE_SDL3
struct PortableGameImportJob
{
    char rom_path[4096], mods_directory[4096], message[512];
    PortableGameImportReport report;
    SDL_Thread *worker;
    SDL_AtomicInt done, total, cancel, finished;
    int status;
};

static int job_progress(void *context, uint32_t done, uint32_t total)
{
    PortableGameImportJob *job = context;
    SDL_SetAtomicInt(&job->total, (int)total);
    SDL_SetAtomicInt(&job->done, (int)done);
    return SDL_GetAtomicInt(&job->cancel);
}

static int SDLCALL job_worker(void *data)
{
    PortableGameImportJob *job = data;
    job->status = portable_game_import_file(job->rom_path, job->mods_directory, job_progress, job, &job->report,
                                            job->message, sizeof job->message);
    if (job->status == 1) summary(&job->report, job->message, sizeof job->message);
    else if (job->status < 0) snprintf(job->message, sizeof job->message, "Import cancelled.");
    SDL_SetAtomicInt(&job->finished, 1);
    return 0;
}

PortableGameImportJob *portable_game_import_start(const char *rom_path, const char *mods_directory)
{
    PortableGameImportJob *job = calloc(1, sizeof(*job));
    if (job == NULL) return NULL;
    if (snprintf(job->rom_path, sizeof job->rom_path, "%s", rom_path) >= (int)sizeof job->rom_path ||
        snprintf(job->mods_directory, sizeof job->mods_directory, "%s", mods_directory) >=
            (int)sizeof job->mods_directory ||
        (job->worker = SDL_CreateThread(job_worker, "game import", job)) == NULL)
    {
        free(job);
        return NULL;
    }
    return job;
}

int portable_game_import_poll(PortableGameImportJob *job, uint32_t *done, uint32_t *total, char *message,
                              size_t capacity)
{
    *done = (uint32_t)SDL_GetAtomicInt(&job->done);
    *total = (uint32_t)SDL_GetAtomicInt(&job->total);
    if (!SDL_GetAtomicInt(&job->finished)) return PORTABLE_GAME_IMPORT_RUNNING;
    snprintf(message, capacity, "%s", job->message);
    return job->status;
}

const char *portable_game_import_mod_id(const PortableGameImportJob *job)
{
    if (job == NULL || !SDL_GetAtomicInt((SDL_AtomicInt *)&job->finished) || job->status != 1) return NULL;
    return job->report.n64 ? MKSC_MK64_MOD_ID : MKSC_MKDS_MOD_ID;
}

void portable_game_import_cancel(PortableGameImportJob *job)
{
    SDL_SetAtomicInt(&job->cancel, 1);
}

void portable_game_import_free(PortableGameImportJob *job)
{
    if (job == NULL) return;
    portable_game_import_cancel(job);
    SDL_WaitThread(job->worker, NULL);
    free(job);
}
#endif
