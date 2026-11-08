#ifndef PORTABLE_MK64_IMPORT_H
#define PORTABLE_MK64_IMPORT_H

#include "rom_import.h"

#define MKSC_MK64_MOD_ID "mario-kart-64"
#define MKSC_MK64_MOD_NAME "Mario Kart 64"
#define MKSC_MK64_IMPORT_VERSION 24u
#define MKSC_MK64_ROM_BYTES 0xC00000u
#define MKSC_MK64_SHA1 "579c48e211ae952530ffc8738709f078d5dd215e"

typedef struct MkscMk64Inventory {
    uint32_t courses, battle_stages, converted;
} MkscMk64Inventory;

/* Detect by bytes, never the filename. 0 invalid, 1 z64, 2 v64, 3 n64. */
int mksc_mk64_byte_order(const uint8_t *rom, size_t size);
/* Accept only the US cartridge, after canonical big-endian SHA-1 checking. */
int mksc_mk64_identify(const uint8_t *rom, size_t size, char *error, size_t capacity);
/* Same ownership/cancellation contract as mksc_rom_import. No filesystem/SDL. */
int mksc_mk64_import(const uint8_t *rom, size_t size, MkscImportProgress progress, void *context,
                     MkscRomImportResult *result, MkscMk64Inventory *inventory, char *error, size_t capacity);
#endif
