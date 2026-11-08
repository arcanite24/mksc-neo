#ifndef PORTABLE_MESH_ITEM_ART_H
#define PORTABLE_MESH_ITEM_ART_H
#include "asset_pack.h"
#include "native_image.h"

typedef struct PortableMeshItemArt PortableMeshItemArt;
/* Zero-initialize *owner. Open decodes only global HUD/item-box/race-effect
 * graphics ID1 into bounded owned images. Failure preserves the old owner.
 * Forget backend image resources BEFORE close/replacement; returned images
 * and pixels remain immutable and borrowed until then. No pack is retained. */
int portable_mesh_item_art_open(const PortableAssetPackView *, PortableMeshItemArt **owner,
                                char *error, size_t error_capacity);
void portable_mesh_item_art_close(PortableMeshItemArt *);
/* Canonical 1000..100f IDs only; reserved 1006/unused100d and empty return
 * NULL. HUD icons are the source 16x16 interior, without its HUD border. */
const PortableNativeImage *portable_mesh_item_art_hud(const PortableMeshItemArt *, uint16_t item);
/* Eight source frames and their exact 128-tick animated palette cycle. */
const PortableNativeImage *portable_mesh_item_art_box(const PortableMeshItemArt *, uint32_t tick);
/* Shells have six source orientations; banana/Boo require orientation 0.
 * Triple shells share their individual shell art. Boo uses its source icon. */
const PortableNativeImage *portable_mesh_item_art_actor(const PortableMeshItemArt *, uint16_t item,
                                                        unsigned orientation);
#endif
