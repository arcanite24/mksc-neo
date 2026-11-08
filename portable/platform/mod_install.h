#ifndef PORTABLE_MOD_INSTALL_H
#define PORTABLE_MOD_INSTALL_H
#include "mod_loader.h"
/* A reviewed archive owns the exact validated bytes. Preparation is read-only;
 * publishing changes installed files without touching settings or live owners. */
typedef struct PortableModInstall PortableModInstall;
PortableModInstall *portable_mod_install_prepare_roots_domains(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const PortableModRoots *roots,const char *path,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,char error[PORTABLE_MOD_ERROR_CAPACITY]);
PortableModInstall *portable_mod_install_prepare(const PortableAssetPackView *base,
    const char *directory,const char *path,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    char error[PORTABLE_MOD_ERROR_CAPACITY]);
typedef int (*PortableModInstallValidator)(const PortableAssetPackView *pack,void *context,
    char error[PORTABLE_MOD_ERROR_CAPACITY]);
int portable_mod_install_validate_roots_domains_with(const PortableModInstall *install,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    const PortableModRoots *roots,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    PortableModInstallValidator validator,void *context,char error[PORTABLE_MOD_ERROR_CAPACITY]);
int portable_mod_install_validate_roots_domains(const PortableModInstall *install,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    const PortableModRoots *roots,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    char error[PORTABLE_MOD_ERROR_CAPACITY]);
/* Run application presentation preflight while composed owners are alive. */
int portable_mod_install_validate_with(const PortableModInstall *install,const PortableAssetPackView *base,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    PortableModInstallValidator validator,void *context,char error[PORTABLE_MOD_ERROR_CAPACITY]);
/* Revalidate a second saved/running selection against the same reviewed bytes. */
int portable_mod_install_validate(const PortableModInstall *install,const PortableAssetPackView *base,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    char error[PORTABLE_MOD_ERROR_CAPACITY]);
PortableModInstall *portable_mod_install_prepare_domains(const PortableAssetPackView *base_2d,
    const PortableAssetPackView *base_3d,const char *directory,const char *path,
    const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,char error[PORTABLE_MOD_ERROR_CAPACITY]);
int portable_mod_install_validate_domains_with(const PortableModInstall *install,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    PortableModInstallValidator validator,void *context,char error[PORTABLE_MOD_ERROR_CAPACITY]);
int portable_mod_install_validate_domains(const PortableModInstall *install,
    const PortableAssetPackView *base_2d,const PortableAssetPackView *base_3d,
    const char *directory,const char ids[][PORTABLE_MOD_ID_CAPACITY],size_t count,
    char error[PORTABLE_MOD_ERROR_CAPACITY]);
const PortableModManifest *portable_mod_install_manifest(const PortableModInstall *install);
int portable_mod_install_is_update(const PortableModInstall *install);
int portable_mod_install_publish(PortableModInstall *install,char error[PORTABLE_MOD_ERROR_CAPACITY]);
void portable_mod_install_free(PortableModInstall *install);
#endif
