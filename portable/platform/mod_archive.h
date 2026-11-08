#ifndef PORTABLE_MOD_ARCHIVE_H
#define PORTABLE_MOD_ARCHIVE_H
/* Read-only .zip mods: stored and deflate entries, no zip64, encryption or
 * multi-disk archives. Opening validates the whole central directory: names
 * are relative '/'-separated paths without '.', '..', '\\' or ':', unique, and
 * declared sizes stay under the per-entry and total caps. Reads inflate into
 * a buffer of exactly the declared size and check the CRC, so a lying or
 * oversized entry fails instead of expanding. */
#include <stddef.h>
#include <stdint.h>

#define PORTABLE_MOD_ARCHIVE_MAX_BYTES (96u*1024u*1024u)
#define PORTABLE_MOD_ARCHIVE_MAX_ENTRIES 256u
#define PORTABLE_MOD_ARCHIVE_MAX_ENTRY_BYTES (64u*1024u*1024u)
#define PORTABLE_MOD_ARCHIVE_MAX_TOTAL_BYTES (320u*1024u*1024u)
#define PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY 192u

typedef struct PortableModArchive PortableModArchive;
/* Takes ownership of bytes (malloc'd) on success and failure. When mod.json is
 * not at the root but "<root_folder>/mod.json" is, names resolve inside that
 * folder. A NULL root_folder detects a unique one-level valid-ID folder,
 * preferring a root mod.json. NULL with a reason in error on any violation. */
PortableModArchive *portable_mod_archive_open_memory(uint8_t *bytes,size_t size,
    const char *root_folder,char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY]);
PortableModArchive *portable_mod_archive_open(const char *path,const char *root_folder,
    char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY]);
/* malloc'd contents of a non-empty entry no larger than maximum, else NULL. */
uint8_t *portable_mod_archive_read(const PortableModArchive *archive,const char *name,
    size_t maximum,size_t *size,char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY]);
/* Verify inflation and CRC of every entry, including unused and empty files. */
int portable_mod_archive_validate(const PortableModArchive *archive,
    char error[PORTABLE_MOD_ARCHIVE_ERROR_CAPACITY]);
/* Borrowed original container and detected folder ID (empty at archive root). */
const uint8_t *portable_mod_archive_bytes(const PortableModArchive *archive,size_t *size);
const char *portable_mod_archive_root(const PortableModArchive *archive);
void portable_mod_archive_free(PortableModArchive *archive);
/* Raw DEFLATE (RFC 1951) into exactly output_size bytes; 1 on success. */
int portable_inflate(const uint8_t *input,size_t input_size,uint8_t *output,size_t output_size);
uint32_t portable_crc32(const uint8_t *bytes,size_t size);
#endif
