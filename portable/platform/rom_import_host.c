#include "rom_import_host.h"
#include "file_read.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rom_import.h"
#include "rom_zip.h"
#ifdef PORTABLE_ENABLE_SDL3
#include <SDL3/SDL.h>
#endif

char *portable_data_directory(void)
{
#ifdef PORTABLE_ENABLE_SDL3
    const char *override = getenv("MKSC_DATA_DIR");
    if (override == NULL || override[0] == '\0') return SDL_GetPrefPath("MKSC Neo", "Portable");
    if (!SDL_CreateDirectory(override)) return NULL;
    const size_t length = strlen(override);
    const int separator = override[length - 1] == '/' || override[length - 1] == '\\';
    char *directory = SDL_malloc(length + 2);
    if (directory == NULL) return NULL;
    memcpy(directory, override, length);
#ifdef _WIN32
    if (!separator) directory[length] = '\\';
#else
    if (!separator) directory[length] = '/';
#endif
    directory[length + (separator ? 0 : 1)] = '\0';
    return directory;
#else
    return NULL;
#endif
}

/* Shared bounded reader preserves SDL document URIs and nonseekable streams. */
static int read_file(const char *path, size_t max_size, unsigned char **data, size_t *size, char *error,
                     size_t error_size)
{
    uint8_t *buffer = NULL;
    size_t length = 0;
    const PortableFileReadStatus status = portable_file_read_bounded(path, max_size, &buffer, &length);
    if (status != PORTABLE_FILE_READ_OK) {
        const char *format = (status == PORTABLE_FILE_READ_OPEN || status == PORTABLE_FILE_READ_NOT_FOUND) ?
            "cannot open ROM %.400s" :
            status == PORTABLE_FILE_READ_LIMIT ? "ROM %.400s is too large" : "cannot read ROM %.400s";
        snprintf(error, error_size, format, path != NULL ? path : "(null)");
        return 0;
    }
    *data = buffer;
    *size = length;
    return 1;
}

int portable_rom_read(const char *path, const char *extension, size_t max_size, unsigned char **data,
                      size_t *size, char *error, size_t error_size)
{
    if (!read_file(path, max_size, data, size, error, error_size)) return 0;
    if (!mksc_rom_zip_detect(*data, *size)) return 1;
    unsigned char *rom = NULL;
    size_t rom_size = 0u;
    const int ok = mksc_rom_zip_extract(*data, *size, extension, max_size, &rom, &rom_size, error, error_size);
    free(*data);
    *data = rom;
    *size = rom_size;
    return ok;
}

static int read_rom(const char *path, unsigned char **data, size_t *size, char *error, size_t error_size)
{
    return portable_rom_read(path, ".gba", MKSC_ROM_IMPORT_MAX_ROM_BYTES, data, size, error, error_size);
}

int portable_rom_import_title_presentation_read(const char *rom_path,
    MkscRomImportResult *result, char *error, size_t error_size)
{
    unsigned char *rom = NULL;
    size_t size = 0;
    memset(result, 0, sizeof *result);
    if (!read_rom(rom_path, &rom, &size, error, error_size)) return 0;
    const int imported = mksc_rom_import_title_presentation(rom, size, result, error, error_size);
    free(rom);
    return imported;
}

static int exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return 0;
    fclose(file);
    return 1;
}

static int publish(const char *temporary, const char *path)
{
#ifdef _WIN32
    /* Never replace: a pack appearing meanwhile is kept. */
    return rename(temporary, path) == 0;
#else
    /* rename() would replace; keep a pack that appeared meanwhile. */
    if (exists(path)) return 0;
    return rename(temporary, path) == 0;
#endif
}

int portable_rom_import_file(const char *rom_path, const char *pack_path, char *error, size_t error_size)
{
    if (exists(pack_path)) {
        snprintf(error, error_size, "output already exists: %.400s (remove it to import again)", pack_path);
        return 0;
    }
    unsigned char *rom = NULL;
    size_t rom_size = 0;
    if (!read_rom(rom_path, &rom, &rom_size, error, error_size)) return 0;
    MkscRomImportResult result;
    const int imported = mksc_rom_import(rom, rom_size, &result, error, error_size);
    free(rom);
    if (!imported) return 0;
    char temporary[4096];
    if (snprintf(temporary, sizeof temporary, "%s.importing", pack_path) >= (int)sizeof temporary) {
        mksc_rom_import_free(&result);
        snprintf(error, error_size, "pack path too long");
        return 0;
    }
    FILE *file = fopen(temporary, "wb");
    int written = file != NULL && fwrite(result.pack, 1, result.pack_size, file) == result.pack_size;
    if (file != NULL && fclose(file) != 0) written = 0;
    char digest[65];
    mksc_hex(result.pack_sha256, 32, digest);
    const size_t sections = result.section_count;
    mksc_rom_import_free(&result);
    if (!written || !publish(temporary, pack_path)) {
        remove(temporary);
        snprintf(error, error_size, "cannot write pack %.400s", pack_path);
        return 0;
    }
    printf("pack=%s sections=%zu pack_sha256=%s\n", pack_path, sections, digest);
    return 1;
}

static int rom_path_file(const char *name, char *out, size_t capacity)
{
    char *directory = portable_data_directory();
    if (directory == NULL) return 0;
    const int length = snprintf(out, capacity, "%s%s", directory, name);
#ifdef PORTABLE_ENABLE_SDL3
    SDL_free(directory);
#endif
    return length > 0 && (size_t)length < capacity;
}

int portable_store_rom_path_as(const char *name, const char *rom_path)
{
#ifdef PORTABLE_ENABLE_SDL3
    if (rom_path == NULL || rom_path[0] == '\0') return 0;
    char absolute[4096], file_path[4096];
    const int rooted = strncmp(rom_path, "content://", 10u) == 0 || rom_path[0] == '/' || rom_path[0] == '\\' ||
                       (rom_path[0] != '\0' && rom_path[1] == ':');
    char *current = rooted ? NULL : SDL_GetCurrentDirectory();
    if (!rooted && current == NULL) return 0;
    const int length = snprintf(absolute, sizeof absolute, "%s%s", current != NULL ? current : "", rom_path);
    SDL_free(current);
    if (length <= 0 || (size_t)length >= sizeof absolute || !rom_path_file(name, file_path, sizeof file_path))
        return 0;
    FILE *file = fopen(file_path, "wb");
    if (file == NULL) return 0;
    int written = fputs(absolute, file) >= 0;
    if (fclose(file) != 0) written = 0;
    return written;
#else
    (void)name;
    (void)rom_path;
    return 0;
#endif
}

int portable_store_rom_path(const char *rom_path)
{
    return portable_store_rom_path_as("rom-path.txt", rom_path);
}

int portable_stored_rom_path_as(const char *name, char *out, size_t capacity)
{
    char file_path[4096];
    if (capacity == 0 || !rom_path_file(name, file_path, sizeof file_path)) return 0;
    FILE *file = fopen(file_path, "rb");
    if (file == NULL) return 0;
    const size_t length = fread(out, 1, capacity - 1, file);
    fclose(file);
    out[length] = '\0';
    return length > 0 && strlen(out) == length;
}

int portable_stored_rom_path(char *out, size_t capacity)
{
    return portable_stored_rom_path_as("rom-path.txt", out, capacity);
}

int portable_rom_import_command(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "--import") != 0) return -1;
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s --import ROM [PACK]\n", argv[0]);
        return 2;
    }
    char pack[4096];
    if (argc == 4) {
        snprintf(pack, sizeof pack, "%s", argv[3]);
    } else {
        char *directory = portable_data_directory();
        if (directory == NULL) {
            fprintf(stderr, "no data directory; pass PACK explicitly\n");
            return 2;
        }
        const int length = snprintf(pack, sizeof pack, "%sgame.pack", directory);
#ifdef PORTABLE_ENABLE_SDL3
        SDL_free(directory);
#endif
        if (length <= 0 || length >= (int)sizeof pack) return 2;
    }
    char error[512];
    if (!portable_rom_import_file(argv[2], pack, error, sizeof error)) {
        fprintf(stderr, "import failed: %s\n", error);
        return 1;
    }
    if (argc == 3) portable_store_rom_path(argv[2]);
    return 0;
}
