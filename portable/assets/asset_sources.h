#ifndef PORTABLE_ASSET_SOURCES_H
#define PORTABLE_ASSET_SOURCES_H
#include "asset_pack.h"
#include "model_asset.h"

enum { PORTABLE_ASSET_SOURCE_DRIVERS, PORTABLE_ASSET_SOURCE_ITEMS,
       PORTABLE_ASSET_SOURCE_COURSES, PORTABLE_ASSET_SOURCE_COUNT };
/* NULL keeps ordinary mod priority. A preferred pack overrides matching keys;
 * resources keep their normal namespaced IDs. Base driver selection restores
 * source sprites by removing optional driver/kart descriptors and bindings. */
int portable_asset_source_available(const PortableAssetPackView *,unsigned category);
int portable_asset_sources_compose(const PortableAssetPackView *fallback,
    const PortableAssetPackView *const preferred[PORTABLE_ASSET_SOURCE_COUNT],
    int base_drivers, PortableAssetSectionView *sections, size_t capacity,
    PortableModelBindingOverlay *bindings, PortableAssetPackView *out,
    char *error, size_t error_capacity);
#endif
