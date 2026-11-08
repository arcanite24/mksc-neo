#ifndef PORTABLE_ROM_ZIP_H
#define PORTABLE_ROM_ZIP_H

/* Reads a ROM out of a .zip archive, as ROM collections usually store them.
 * Stored and deflated entries are supported; no file or OS APIs. */

#include <stddef.h>
#include <stdint.h>

/* 1 when data starts with a zip local file header. */
int mksc_rom_zip_detect(const uint8_t *data, size_t size);

/* Extracts the archive's first entry named with extension (such as ".gba",
 * any case; a semicolon-separated set is also accepted), or its only file, into a new malloc'd buffer of at most max_size
 * bytes, verifying its CRC-32. Returns 1 on success; on failure returns 0 and
 * describes the problem in error. */
int mksc_rom_zip_extract(const uint8_t *zip, size_t zip_size, const char *extension, size_t max_size, uint8_t **out,
                         size_t *out_size, char *error, size_t error_size);

#endif
