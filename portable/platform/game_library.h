#ifndef PORTABLE_GAME_LIBRARY_H
#define PORTABLE_GAME_LIBRARY_H
#include "mod_manager.h"

/* Imported games share one ROM importer, which recognises DS and N64 ROMs. */
typedef enum { PORTABLE_GAME_BASE, PORTABLE_GAME_IMPORT } PortableGameImportKind;
typedef struct {
    const char *id,*name,*system;
    PortableGameImportKind importer;
    uint32_t color;
} PortableLibraryGame;
/* The supported-source catalog is presentation data, independent of runtime
 * asset resolution. New importers add rows here and a case in game_import_host.c. */
size_t portable_game_library_count(void);
const PortableLibraryGame *portable_game_library_at(size_t index);
const PortableModCatalogEntry *portable_game_library_entry(const PortableModManager *,size_t index);
#endif
