#ifndef PORTABLE_MESH_FX_SET_H
#define PORTABLE_MESH_FX_SET_H

#include "fx_set_asset.h"
#include "model_asset.h"
#include "render_scene.h"

/* A shadow decal: a unit square (x right, y forward, texture top row
 * forward) whose multiply material darkens the floor by the art's texel
 * alpha times its material alpha. The mesh points into the struct, so it
 * must not move between open and close. */
typedef struct
{
    PortableRenderVertex vertices[4];
    PortableRenderMaterial material;
    PortableRenderRun run;
    PortableRenderMesh mesh;
    uint8_t *pixels;
} PortableMeshShadowDecal;
/* 0 when the material has no texture of at least 2x2 or allocation fails. */
int portable_mesh_shadow_decal_open(PortableMeshShadowDecal *decal, const PortableModelMaterial *material);
void portable_mesh_shadow_decal_close(PortableMeshShadowDecal *decal);

/* Render-ready kind-116 effect set: kart shadow decals (runtime multiply
 * materials darkening the floor by the art's alpha) and particle style
 * flipbooks for the 3D VFX pool. Decoded once from borrowed pack materials
 * into owned pixels; nothing here advances or reads the simulation. */
typedef struct PortableMeshFxSet PortableMeshFxSet;

typedef struct
{
    const PortableRenderMesh *mesh; /* unit square, x right, y forward */
    double half_width, half_length, forward, vanish; /* metres at kart scale 1 */
    int volume; /* PORTABLE_FX_SHADOW_VOLUME: no shrink */
} PortableMeshFxShadow;

#define PORTABLE_MESH_FX_FRAMES 16u
typedef struct
{
    const PortableRenderMesh *frames[PORTABLE_MESH_FX_FRAMES]; /* billboard squares, local X/Z */
    uint32_t frame_count, flags; /* PORTABLE_FX_STYLE_* */
} PortableMeshFxStyle;

/* Zero-init *owner. id 0 closes the owner (no set). Failure names the set and
 * preserves *owner. Forget backend resources before close or replacement. */
int portable_mesh_fx_set_open(const PortableAssetPackView *pack, uint32_t id, PortableMeshFxSet **owner,
                              char *error, size_t capacity);
void portable_mesh_fx_set_close(PortableMeshFxSet *owner);
const PortableFxSet *portable_mesh_fx_set_rows(const PortableMeshFxSet *owner);
int portable_mesh_fx_set_shadow(const PortableMeshFxSet *owner, uint32_t kart_id, uint32_t driver_id,
                                PortableMeshFxShadow *out);
/* NULL when the set has no art for the slot. */
const PortableMeshFxStyle *portable_mesh_fx_set_style(const PortableMeshFxSet *owner, unsigned slot);

#endif
