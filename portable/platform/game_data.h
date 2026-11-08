#ifndef PORTABLE_GAME_DATA_H
#define PORTABLE_GAME_DATA_H
#include "asset_pack.h"
#include "mod_manifest.h"

typedef struct PortableGameDataOverlay PortableGameDataOverlay;
/* Normalize JSON against the effective lower layers. No input bytes are changed;
 * output owns patched tables and borrows unchanged course menu images. */
PortableGameDataOverlay *portable_game_data_apply(const PortableAssetPackView *effective,
    const PortableAssetSectionView *json,char error[PORTABLE_MOD_ERROR_CAPACITY]);
const PortableAssetPackView *portable_game_data_pack(const PortableGameDataOverlay *overlay);
size_t portable_game_data_bytes(const PortableGameDataOverlay *overlay);
void portable_game_data_free(PortableGameDataOverlay *overlay);
#endif
