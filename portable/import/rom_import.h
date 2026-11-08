#ifndef PORTABLE_ROM_IMPORT_H
#define PORTABLE_ROM_IMPORT_H

/* Native ROM importer: turns the owner's verified US ROM into the game's asset
 * pack in memory. Byte-identical to tools/portable/import_rom.py, which stays
 * the reference. No file, OS or SDL APIs; callers own I/O. */

#include <stddef.h>
#include <stdint.h>

#define MKSC_ROM_IMPORT_SHA1 "9d327c030c3e2d9007990518594f70c3340ac56f"
#define MKSC_ROM_IMPORT_MAX_ROM_BYTES ((size_t)0x2000000)
/* Written to every pack as the kind80/ID1 import stamp (IMPORTER_VERSION in
 * import_rom.py). Bump both whenever the imported bytes change, so `mksc`
 * offers to reimport packs made by an older importer. Packs from before the
 * stamp read as version 0. */
#define MKSC_ROM_IMPORT_VERSION 28u

/* Optional progress for long imports: done of total steps. Returning nonzero
 * cancels; the import then fails without a result. */
typedef int (*MkscImportProgress)(void *context, uint32_t done, uint32_t total);

typedef struct MkscRomImportResult {
    uint8_t *pack;
    size_t pack_size;
    size_t section_count;
    uint8_t pack_sha256[32];
    uint8_t deterministic_sha256[32];
} MkscRomImportResult;

/* 1 when rom is the unmodified US revision 0 dump the importer supports. */
int mksc_rom_import_supported(const uint8_t *rom, size_t size);

/* Builds the full pack. Returns 1 on success; on failure returns 0, leaves
 * result empty and writes a diagnostic to error. */
int mksc_rom_import(const uint8_t *rom, size_t size, MkscRomImportResult *result,
                    char *error, size_t error_size);
/* Optional, separate title presentation pack; never changes the base import.
 * Contains stock title backgrounds for identity checks, kind-15 images
 * 0x20900+variant*2 (256x256 sky; night: eight-row 256x2048 atlas) and +1
 * (256x512 clouds), and kind-15
 * 0x209ff metadata: "MKSCWUI1", u32 LE version 1, 20-byte source ROM SHA-1.
 * Variants are normal, sunset, night. Use the same result/free contract. */
int mksc_rom_import_title_presentation(const uint8_t *rom, size_t size,
    MkscRomImportResult *result, char *error, size_t error_size);
void mksc_rom_import_free(MkscRomImportResult *result);

void mksc_sha1(const void *data, size_t size, uint8_t digest[20]);
void mksc_sha256(const void *data, size_t size, uint8_t digest[32]);
void mksc_hex(const uint8_t *digest, size_t size, char *out);

#endif
