#ifndef PORTABLE_MESH_BATTLE_ART_H
#define PORTABLE_MESH_BATTLE_ART_H

#include "render_scene.h"

typedef struct PortableMeshBattleArt PortableMeshBattleArt;

/* Global role art, not character manifest 9: Bomb kind10/id9/v4, balloon
 * kind19/id4 NativeImage, and DPR1 kind82/id1. Owns pixels/mesh identities.
 * Zero-init *owner; failure preserves it. Backend resources must be forgotten
 * before replacement/close. No SDL, pack borrowing, clocks or simulation. */
int portable_mesh_battle_art_open(const PortableAssetPackView *, PortableMeshBattleArt **owner,
                                  char *error, size_t capacity);
void portable_mesh_battle_art_close(PortableMeshBattleArt *);

/* Same relative 128-angle fold/mirror as source drivers. Use the existing
 * portable_mesh_driver_art_angle for immutable pose/view selection. The caller
 * selects walking alternate 0/1; even imported base slot plus alternate gives
 * one of 16 source frames. Invalid angle/alternate returns NULL. */
const PortableRenderMesh *portable_mesh_battle_art_bomb_view(const PortableMeshBattleArt *,
                                                             uint8_t angle, unsigned walk_frame);
const PortableRenderMesh *portable_mesh_battle_art_balloon(const PortableMeshBattleArt *);

/* Returned meshes have source cell offsets baked into local X/Z at .05 metres
 * per pixel. X points camera-right; Z points up. Bomb uses full 64-pixel DPR1 foot;
 * balloon uses its own image anchors. Host supplies world foot/attachment
 * translation, BILLBOARD_Z, scales and per-view visibility. No mutable texels. */
#endif
