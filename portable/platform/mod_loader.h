#ifndef PORTABLE_MOD_LOADER_H
#define PORTABLE_MOD_LOADER_H
#include "asset_pack.h"
#include "mod_identity.h"
#include "mod_manifest.h"
#include "asset_sources.h"
#include "mod_archive.h"
typedef struct PortableModSet PortableModSet;
#define PORTABLE_MOD_ROOT_LIMIT 5u
#define PORTABLE_MOD_DIRECTORY_CAPACITY 4096u
/* Borrowed for each call. Root zero is the installation destination. */
typedef struct {
    const char *directories[PORTABLE_MOD_ROOT_LIMIT];
    size_t count;
} PortableModRoots;
#define PORTABLE_MOD_CATALOG_LIMIT 128u
typedef struct {
    PortableModManifest manifest;
    char id[PORTABLE_MOD_ID_CAPACITY],error[PORTABLE_MOD_ERROR_CAPACITY];
    uint8_t valid,from_zip,source_root;
} PortableModCatalogEntry;
/* Caller-owned, preferably heap allocated. Folder/ZIP pairs occupy one row.
 * Complete catalogs are sorted by ID. Overflow is an error, not a partial
 * actionable listing. Non-SDL backends report discovery unsupported. */
typedef struct {
    PortableModCatalogEntry entries[PORTABLE_MOD_CATALOG_LIMIT];
    size_t count;
    uint8_t overflow;
    char error[PORTABLE_MOD_ERROR_CAPACITY];
} PortableModCatalog;
typedef struct {
    char id[PORTABLE_MOD_ID_CAPACITY],message[PORTABLE_MOD_ERROR_CAPACITY];
    uint8_t accepted;
} PortableModLoadDiagnostic;
typedef struct {
    size_t count;
    PortableModLoadDiagnostic entries[PORTABLE_MOD_LIMIT];
    char first_error[PORTABLE_MOD_ERROR_CAPACITY];
} PortableModLoadReport;
int portable_mods_discover(const char *directory,PortableModCatalog *catalog);
int portable_mods_discover_roots(const PortableModRoots *roots,PortableModCatalog *catalog);
int portable_mod_roots_valid(const PortableModRoots *roots);
PortableModSet *portable_mods_load_roots_domains_report(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report);
PortableModSet *portable_mods_load_roots_domains_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report);
PortableModSet *portable_mods_load_roots_domains_archive_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    const char *archive_id,const PortableModArchive *archive,PortableModLoadReport *report);
/* Each ID loads <directory>/<id>/ or, without a mod.json there, <directory>/<id>.zip.
 * Invalid individual mods are named on stderr and skipped; required dependents
 * are skipped too. Explicit order is authoritative, never silently rearranged. */
PortableModSet *portable_mods_load(const PortableAssetPackView *base,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count);
/* Same tolerant loader and stderr messages, plus per-request diagnostics. */
PortableModSet *portable_mods_load_report(const PortableAssetPackView *base,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report);
/* Reject the whole candidate if any requested mod fails. Does not execute Lua;
 * the candidate owner must separately require every script to load. */
PortableModSet *portable_mods_load_strict(const PortableAssetPackView *base,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report);
/* Validate one uninstalled/update archive in the same ordered context. The
 * override ID must be requested. Archive is borrowed only during this call;
 * the returned set owns its decoded data and never retains/frees the archive. */
PortableModSet *portable_mods_load_archive_strict(const PortableAssetPackView *base,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    const char *archive_id,const PortableModArchive *archive,PortableModLoadReport *report);
/* Separate borrowed bases keep recovered-2D driving options out of the 3D
 * identity. Existing entry points retain their single-base semantics. */
PortableModSet *portable_mods_load_domains_report(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report);
PortableModSet *portable_mods_load_domains_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,PortableModLoadReport *report);
PortableModSet *portable_mods_load_domains_archive_strict(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    const char *archive_id,const PortableModArchive *archive,PortableModLoadReport *report);
const PortableAssetPackView *portable_mods_pack(const PortableModSet *mods);
/* Independent 3D projection. Both views borrow payloads owned by this set;
 * old accessors deliberately expose only 2D content and script membership. */
const PortableAssetPackView *portable_mods_pack_3d(const PortableModSet *mods);
size_t portable_mods_loaded_count(const PortableModSet *mods);
/* Display name of the last loaded 3D mod providing 3D course course_id
 * (borrowed until free), or NULL when no mod does. *order receives its
 * load index so callers can list providers in load order. */
const char *portable_mods_course_3d_provider(const PortableModSet *mods,uint32_t course_id,
    size_t *order);
/* The same for any 3D-domain section (kind, content_id), e.g. a DRV3 driver. */
const char *portable_mods_section_3d_provider(const PortableModSet *mods,uint32_t kind,
    uint32_t content_id,size_t *order);
/* Whether loaded mod order provides any 3D-domain section of kind. */
int portable_mods_provides_3d_kind(const PortableModSet *mods,size_t order,uint32_t kind);
/* Ordered IDs, versions and fingerprints of the loaded mods. With at least one
 * mod, portable_mods_pack's deterministic digest also mixes in this identity. */
const PortableModIdentity *portable_mods_identity(const PortableModSet *mods);
const PortableModIdentity *portable_mods_identity_3d(const PortableModSet *mods);
/* Loaded mod index's Lua source (borrowed until free), or 0 without a script. */
int portable_mods_script(const PortableModSet *mods,size_t index,const char **id,
    const char **source,size_t *size);
int portable_mods_script_3d(const PortableModSet *mods,size_t index,const char **id,
    const char **source,size_t *size);
/* Apply to a newly loaded candidate only, before publishing owners. Empty IDs
 * retain load order; "@base" restores built-in assets; other IDs must be loaded.
 * Failure leaves this candidate unsuitable for publication: discard it. */
int portable_mods_select_assets(PortableModSet *mods,const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,
    const char sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY],char *error,size_t capacity);
void portable_mods_free(PortableModSet *mods);
#endif
