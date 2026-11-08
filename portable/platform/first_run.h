#ifndef PORTABLE_FIRST_RUN_H
#define PORTABLE_FIRST_RUN_H

#include <stddef.h>

/* Locates game.pack in the player data directory. When it is missing, shows
 * the "choose your ROM" screen (file dialog, drag and drop, or rom_path when
 * given) and imports the ROM natively. When it was made by an older importer
 * (import stamp below MKSC_ROM_IMPORT_VERSION), the same screen offers to
 * import again from rom_path or the remembered ROM (else asks for the file),
 * or to keep playing the old pack. rom_path "-" means the remembered ROM and
 * "keep" keeps a stale pack. hidden runs the screen without a window and
 * imports at once (automated checks; a failed import then returns -1).
 * Returns 1 with pack_path set, 0 when the player quit, -1 on error. */
int portable_first_run(const char *rom_path, int hidden, char *pack_path, size_t capacity);

#endif
