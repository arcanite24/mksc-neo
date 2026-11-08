#ifndef PORTABLE_MOD_MANAGER_H
#define PORTABLE_MOD_MANAGER_H
#include "mod_loader.h"
#include "script_host.h"

typedef struct PortableModManager PortableModManager;
/* The candidate borrows the base pack through its composed view. Free scripts
 * before mods; take both owners only after the shell commits its settings. */
typedef struct {
    PortableModSet *mods;
    PortableScriptHost *scripts;
    char ids[PORTABLE_MOD_LIMIT][PORTABLE_MOD_ID_CAPACITY];
    size_t count;
    char asset_sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY];
} PortableModManagerCandidate;

PortableModManager *portable_mod_manager_create(const char *directory,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count);
PortableModManager *portable_mod_manager_create_roots(const PortableModRoots *roots,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count);
/* Staged per-category source IDs; an empty ID restores normal load order. */
void portable_mod_manager_set_sources(PortableModManager *,
    const char sources[PORTABLE_ASSET_SOURCE_COUNT][PORTABLE_MOD_ID_CAPACITY]);
const char *portable_mod_manager_source(const PortableModManager *,unsigned kind);
int portable_mod_manager_cycle_source(PortableModManager *,unsigned kind,int direction);
/* Art for a library entry, invalidated on refresh/free. The first call for an
 * id starts a worker (load_job) and returns NULL; later calls return the image
 * once it is ready, so the menu never blocks on loading the imported pack.
 * preview_busy reports a load in flight; preview_poll returns 1 on the frame a
 * finished load is adopted (redraw). Free and refresh never wait: a running worker
 * is orphaned and its result dropped; call portable_mod_manager_drain once at
 * exit, before the base pack is freed, to wait for orphans. */
void portable_mod_manager_drain(void);
const PortableNativeImage *portable_mod_manager_preview_image(PortableModManager *,
    const char *id,const PortableAssetPackView *base);
int portable_mod_manager_preview_busy(const PortableModManager *);
int portable_mod_manager_preview_poll(PortableModManager *);
void portable_mod_manager_free(PortableModManager *manager);
int portable_mod_manager_refresh(PortableModManager *manager);
const PortableModCatalog *portable_mod_manager_catalog(const PortableModManager *manager);
size_t portable_mod_manager_count(const PortableModManager *manager);
const char *portable_mod_manager_id(const PortableModManager *manager,size_t index);
int portable_mod_manager_index(const PortableModManager *manager,const char *id);
const char *portable_mod_manager_notice(const PortableModManager *manager);
/* Toggle removes a staged ID even if it is no longer installed. New IDs must
 * name a valid discovered manifest. Reordering never silently fixes dependencies. */
int portable_mod_manager_toggle(PortableModManager *manager,const char *id);
int portable_mod_manager_remove(PortableModManager *manager,size_t index);
/* One-step enable: first enables installed dependencies it is missing, then
 * places the mod as late as possible while staying after its dependencies and
 * load_after mods and before enabled mods that depend on or load after it.
 * Returns 1 enabled (or already enabled), 0 with a notice naming the mod. */
int portable_mod_manager_enable(PortableModManager *manager,const char *id);
/* One-step disable: removes the mod and every enabled mod that (transitively)
 * depends on it. Returns the number of mods removed. */
size_t portable_mod_manager_disable(PortableModManager *manager,const char *id);
/* Replaces the staged order (for example, restoring the active selection after
 * a failed one-step change). IDs are not checked against the catalog. */
int portable_mod_manager_select(PortableModManager *manager,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count);
int portable_mod_manager_move(PortableModManager *manager,size_t from,size_t to);
/* Explain the first dependency/order problem for a staged row, or the explicit
 * last-wins policy. This is a preview; strict loading is authoritative. */
int portable_mod_manager_preview(const PortableModManager *manager,size_t index,
    char *text,size_t capacity);
PortableModManagerCandidate *portable_mod_manager_prepare(PortableModManager *manager,
    const PortableAssetPackView *base,PortableModLoadReport *report);
PortableModManagerCandidate *portable_mod_manager_prepare_domains(PortableModManager *manager,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    PortableModLoadReport *report);
void portable_mod_manager_candidate_free(PortableModManagerCandidate *candidate);
#endif
