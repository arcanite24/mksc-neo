#ifndef PORTABLE_GAME_IMPORT_HOST_H
#define PORTABLE_GAME_IMPORT_HOST_H

/* Imports content from another game the player owns into a generated folder
 * mod beside their other mods (ARCHITECTURE "Imported game content"). Mario Kart DS and Mario Kart 64 (US) are supported. */

#include <stddef.h>
#include <stdint.h>

#include "mkds_import.h"
#include "mk64_import.h"

typedef struct PortableGameImportReport
{
    MkscMkdsInventory inventory;
    MkscMk64Inventory n64_inventory;
    int n64;
    char mod_directory[4096];
    char pack_sha256[65];
} PortableGameImportReport;

/* Reads rom_path (raw or zipped), converts it, and writes
 * <mods_directory>/<game-id>/{content.pack,mod.json}, each through a
 * sibling temporary file, so an older import stays usable until the new one is
 * complete. A folder of that name not made by the importer is left alone.
 * Remembers rom_path for updates. progress may be NULL. Returns 1 imported, 0
 * failed (error filled), -1 cancelled through progress. */
int portable_game_import_file(const char *rom_path, const char *mods_directory, MkscImportProgress progress,
                              void *context, PortableGameImportReport *report, char *error, size_t error_size);

/* The version the current importer writes into the generated mod `id`'s
 * mod.json ("1.0.<version>"; mario-kart-ds or mario-kart-64). An installed mod
 * with another version should be imported again. */
void portable_game_import_version(const char *id, char *out, size_t capacity);
/* Absolute ROM path of that game's last successful import, if remembered. */
int portable_game_import_stored_rom(const char *id, char *out, size_t capacity);

/* `--import-game ROM [MODS_DIR]`: -1 when argv selects another command, else
 * an exit code. MODS_DIR defaults to mods/ in the data directory. */
int portable_game_import_command(int argc, char **argv);

#ifdef PORTABLE_ENABLE_SDL3
/* The same import on a worker thread, for menus. */
typedef struct PortableGameImportJob PortableGameImportJob;

PortableGameImportJob *portable_game_import_start(const char *rom_path, const char *mods_directory);
enum { PORTABLE_GAME_IMPORT_RUNNING = 2 };
/* PORTABLE_GAME_IMPORT_RUNNING with done/total filled; once finished 1
 * imported, 0 failed or -1 cancelled, with message (a summary or the error). */
int portable_game_import_poll(PortableGameImportJob *job, uint32_t *done, uint32_t *total, char *message,
                              size_t capacity);
/* After a successful import: the generated mod's ID, else NULL. */
const char *portable_game_import_mod_id(const PortableGameImportJob *job);
void portable_game_import_cancel(PortableGameImportJob *job);
/* Cancels if still running and waits for the worker. */
void portable_game_import_free(PortableGameImportJob *job);
#endif

#endif
