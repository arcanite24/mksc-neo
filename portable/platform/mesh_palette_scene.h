#ifndef PORTABLE_MESH_PALETTE_SCENE_H
#define PORTABLE_MESH_PALETTE_SCENE_H
#include "mesh_palette.h"
#include "render_scene.h"

enum
{
    PORTABLE_MESH_PALETTE_SCENE_TEXTURES = 2048,
    PORTABLE_MESH_PALETTE_SCENE_BYTES = 64 * 1024 * 1024,
    PORTABLE_MESH_PALETTE_SCENE_MESHES = 2048
};
typedef struct PortableMeshPaletteScene PortableMeshPaletteScene;
typedef struct
{
    size_t unique_textures, expanded_rgba_bytes, mesh_wrappers;
} PortableMeshPaletteSceneStats;
/* The config, pack payloads and every prepared base mesh remain immutable and
 * outlive the scene. Close renderer caches before closing this owner. Create
 * admits the complete indexed-image/palette texture budget before publication. */
PortableMeshPaletteScene *portable_mesh_palette_scene_create(const PortableMeshPaletteConfig *,
                                                             const PortableAssetPackView *, char *,
                                                             size_t);
/* First use prepares ALL variants of this base atomically. Call for terrain
 * and all models before live rendering to admit the aggregate wrapper budget.
 * Returns base unchanged when none of its materials is bound. Failed calls
 * preserve all previously returned wrappers and statistics. */
const PortableRenderMesh *portable_mesh_palette_scene_mesh(PortableMeshPaletteScene *,
                                                           const PortableRenderMesh *,
                                                           unsigned variant, char *, size_t);
/* Relocate copied embedded owners without reallocating any prepared view.
 * Config bytes must be identical, and the old/new base descriptors must name
 * identical borrowed arrays and counts. Failure preserves all scene state.
 * NULL scene is accepted only for a disabled, valid new config. */
int portable_mesh_palette_scene_relocate(PortableMeshPaletteScene *,
                                         const PortableMeshPaletteConfig *new_config,
                                         const PortableRenderMesh *old_base,
                                         const PortableRenderMesh *new_base, char *, size_t);
PortableMeshPaletteSceneStats portable_mesh_palette_scene_stats(const PortableMeshPaletteScene *);
void portable_mesh_palette_scene_close(PortableMeshPaletteScene *);
#endif
