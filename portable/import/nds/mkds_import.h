#ifndef PORTABLE_MKDS_IMPORT_H
#define PORTABLE_MKDS_IMPORT_H

/* Mario Kart DS (US) content import: turns the owner's verified cartridge
 * into the generated mod's pack in memory. No file, OS or SDL APIs; callers
 * own I/O and write the mod folder. See ARCHITECTURE "Imported game content". */

#include <stddef.h>
#include <stdint.h>

#include "rom_import.h"

/* SHA-1 of the cartridge from 0x8000 (after the secure area, which encrypted
 * and decrypted dumps store differently) to the header's used-ROM size, so
 * trimmed dumps match too. */
#define MKSC_MKDS_CONTENT_SHA1 "b521b5d00d6be91e0e50eba2ffa891744e5b7e23"
#define MKSC_MKDS_GAME_CODE "AMCE"
#define MKSC_MKDS_MAX_ROM_BYTES ((size_t)0x4000000)
#define MKSC_MKDS_MOD_ID "mario-kart-ds"
#define MKSC_MKDS_MOD_NAME "Mario Kart DS"
/* Bump whenever the generated pack changes; the shell then offers to import
 * again. The generated mod's version is "1.0.<this>". */
#define MKSC_MKDS_IMPORT_VERSION 23u

/* The pack's kind-80 import stamp (ID 2): "MKDSIMP1", u32 importer version,
 * 20-byte content SHA-1, u32 row count, then 64-byte rows: u8 row kind, cup,
 * slot, reserved, 28-byte source folder and 32-byte ASCII name, NUL padded. */
#define MKSC_MKDS_STAMP_ID 2u
enum
{
    MKSC_MKDS_ROW_CUP = 1,
    MKSC_MKDS_ROW_COURSE = 2,
    MKSC_MKDS_ROW_BATTLE = 3,
    MKSC_MKDS_ROW_DRIVER = 4,
    MKSC_MKDS_ROW_KART = 5,
};

typedef struct MkscMkdsInventory
{
    uint32_t cups, courses, battle_stages, drivers, karts;
    uint32_t converted, skipped; /* courses and stages viewable in 3D */
    char skipped_reason[256];    /* the first course left out and why */
} MkscMkdsInventory;

/* 1 when rom is the supported Mario Kart DS (US) cartridge; otherwise 0 and
 * error names what the file is and why it cannot be imported. */
int mksc_mkds_identify(const uint8_t *rom, size_t size, char *error, size_t error_size);

/* 1 success, 0 failure (error filled), -1 cancelled through progress, which
 * may be NULL. inventory may be NULL. Free the result with
 * mksc_rom_import_free. */
int mksc_mkds_import(const uint8_t *rom, size_t size, MkscImportProgress progress, void *context,
                     MkscRomImportResult *result, MkscMkdsInventory *inventory, char *error,
                     size_t error_size);

#endif
