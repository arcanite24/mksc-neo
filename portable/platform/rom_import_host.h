#ifndef PORTABLE_ROM_IMPORT_HOST_H
#define PORTABLE_ROM_IMPORT_HOST_H

#include <stddef.h>
#include "rom_import.h"

/* Player data directory with a trailing separator: MKSC_DATA_DIR when set,
 * otherwise SDL's "MKSC Neo/Portable" pref directory. Returns a string to
 * release with SDL_free, or NULL (always NULL without SDL). */
char *portable_data_directory(void);

/* Reads a ROM of at most max_size bytes, first unpacking a .zip archive's
 * entry named with extension (such as ".gba") or its only file. data is
 * malloc'd. Returns 1 on success, else 0 with error filled. */
int portable_rom_read(const char *path, const char *extension, size_t max_size, unsigned char **data,
                      size_t *size, char *error, size_t error_size);

/* Imports rom_path into a new pack_path (refuses an existing file) through a
 * sibling temporary file. Returns 1 on success, else 0 with error filled. */
int portable_rom_import_file(const char *rom_path, const char *pack_path,
                             char *error, size_t error_size);
/* Read the owner ROM once and return the separate optional title pack. */
int portable_rom_import_title_presentation_read(const char *rom_path,
    MkscRomImportResult *result, char *error, size_t error_size);

/* rom-path.txt in the data directory remembers the absolute path of the ROM
 * last imported into the default game.pack, so a stale pack can be imported
 * again without asking. Store returns 1 when written; load returns 1 when a
 * path was remembered (the file may since have moved). */
int portable_store_rom_path(const char *rom_path);
int portable_stored_rom_path(char *out, size_t capacity);
/* The same for another file name in the data directory, such as a ROM
 * imported into a generated mod. */
int portable_store_rom_path_as(const char *name, const char *rom_path);
int portable_stored_rom_path_as(const char *name, char *out, size_t capacity);

/* `--import ROM [PACK]`: -1 when argv selects another command, else an exit
 * code. PACK defaults to game.pack in the data directory. */
int portable_rom_import_command(int argc, char **argv);

#endif
