#ifndef PORTABLE_MESH_KART_FX_ART_H
#define PORTABLE_MESH_KART_FX_ART_H

#include "render_pose.h"

typedef struct PortableMeshKartFxArt PortableMeshKartFxArt;
enum
{
    PORTABLE_MESH_KART_FX_SPARK = 0,
    PORTABLE_MESH_KART_FX_FLAME = 1
};
/* Source FXC1 global drift ID1 and mushroom-burst ID2. Decodes cells once into
 * stable owned .05m/source-pixel local-X/Z meshes. Index0 stays transparent;
 * opaque RGB555 black stays opaque. No simulation, SDL or draw-time mutation.
 * Missing optional effects are allowed; malformed present effects fail named.
 * Zero-init *owner; failure preserves it. Forget backend resources BEFORE close
 * or successful replacement. No pack memory remains borrowed. */
int portable_mesh_kart_fx_art_open(const PortableAssetPackView *, PortableMeshKartFxArt **,
                                  char *error, size_t capacity);
void portable_mesh_kart_fx_art_close(PortableMeshKartFxArt *);
/* Source age0/1 and age>=lifetime draw nothing. Each frame lasts duration+1
 * accepted turns. Metadata0 means missing/invalid kind. Getters never advance. */
const PortableRenderMesh *portable_mesh_kart_fx_art_frame(const PortableMeshKartFxArt *,
                                                         unsigned kind, uint16_t age);
uint32_t portable_mesh_kart_fx_art_frame_count(const PortableMeshKartFxArt *, unsigned kind);
uint16_t portable_mesh_kart_fx_art_lifetime(const PortableMeshKartFxArt *, unsigned kind);
#endif
