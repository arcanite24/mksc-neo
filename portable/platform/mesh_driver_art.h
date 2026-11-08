#ifndef PORTABLE_MESH_DRIVER_ART_H
#define PORTABLE_MESH_DRIVER_ART_H

#include "render_pose.h"
#include "character_view.h"
#include "driver_sprite_asset.h"

typedef struct PortableMeshDriverArt PortableMeshDriverArt;

/* Exact manifest ID -> graphics, never voice/seat/donor art. Owns all decoded
 * pixels and stable mesh identities; no pack bytes remain borrowed. Zero-init
 * *owner. Failure preserves it; success replaces it. Backend resources must be
 * forgotten BEFORE replacement/close. No SDL, course or simulation owner. */
int portable_mesh_driver_art_open(const PortableAssetPackView *pack, uint32_t driver_id,
                                  PortableMeshDriverArt **owner, char *error, size_t capacity);
void portable_mesh_driver_art_close(PortableMeshDriverArt *);
/* A KSP1 sprite driver (kind 113 with the driver's ID) is used before the
 * stock graphics: its own frame size, metres per pixel and view table. Its
 * wheel colours turn with distance driven (metres); other art ignores it. */
void portable_mesh_driver_art_set_wheel(PortableMeshDriverArt *, double distance);
/* KSP1 pitch rows and tumble frames: pitch is the kart's nose-up angle
 * (radians) and tumble the share [0, 1) of a tumbling hit, negative when
 * none. Later pose views use the row and, while it returns 1, the tumble
 * frame. Other art ignores both and returns 0. */
int portable_mesh_driver_art_set_pose(PortableMeshDriverArt *, double pitch, double tumble);
/* The KSP1 set's effect data (rows, star colours, ghost/Boo alphas), else NULL. */
const PortableDriverSprites *portable_mesh_driver_art_sprites(const PortableMeshDriverArt *);

/* Full 64-pixel cell at .05 metres/source pixel, using imported DPR1 foot offsets.
 * Local X points camera-right, Z points up. Caller supplies foot translation,
 * BILLBOARD_Z and signed scales. Texture/mirror data never changes per draw.
 * view uses the imported 64-entry direction table and folded 128-angle mirror.
 * frame addresses a decoded physical frame, including celebration frames.
 * Invalid angle/frame/mirror returns NULL. */
const PortableRenderMesh *portable_mesh_driver_art_view(const PortableMeshDriverArt *,
                                                        uint8_t angle);
const PortableRenderMesh *portable_mesh_driver_art_frame(const PortableMeshDriverArt *,
                                                         uint32_t frame, int mirror);

/* Local-Z edge below the lowest opaque source pixel, in metres. Cached from
 * bank0 indices per physical frame; mirrors/effect banks retain the same shape.
 * Fully transparent frames have anchor0. Failure preserves the output. This
 * does not change DPR1 cell geometry or any frame/view resource identity. */
int portable_mesh_driver_art_ground_anchor(const PortableMeshDriverArt *, uint8_t angle,
                                            double *z);

/* Optional imported burn banks5/6 and Boo bank7, composed over base indices0..2. Bank0
 * returns the ordinary view. Unsupported authored banks return NULL so the
 * caller can apply a model tint; angle/mirror and base resources stay immutable. */
const PortableRenderMesh *portable_mesh_driver_art_palette_view(const PortableMeshDriverArt *,
                                                                uint8_t angle, uint8_t bank);

/* Owned optional DVR1 tables. NULL means an older pack omitted the rules;
 * the legacy frame/view APIs remain available without steering correction. */
const PortableCharacterViewRules *portable_mesh_driver_art_rules(const PortableMeshDriverArt *);
/* Source pose's explicit mirror includes endpoint steering/contact behavior.
 * Override0 selects the direction table; a nonzero override must be a valid
 * physical frame. Banks0/5/6/7 retain their existing immutable shape/pixels. */
const PortableRenderMesh *portable_mesh_driver_art_pose_view(const PortableMeshDriverArt *,
    const PortableCharacterViewPose *, uint8_t bank);
/* The same frame drawn translucent (alpha from the instance tint) after
 * opaque scenery: Boo and Time Trial ghosts. Bank 0 art only. */
const PortableRenderMesh *portable_mesh_driver_art_pose_blend(const PortableMeshDriverArt *,
    const PortableCharacterViewPose *);
int portable_mesh_driver_art_pose_ground_anchor(const PortableMeshDriverArt *,
    const PortableCharacterViewPose *, double *z);
/* Cached opaque texel-edge AABB [minX,minY,minZ,maxX,maxY,maxZ]. Explicit
 * UV mirroring reflects opaque X within the unchanged DPR1 quad. Transparent
 * frames return zeros; invalid calls preserve all six output values. */
int portable_mesh_driver_art_pose_bounds(const PortableMeshDriverArt *,
    const PortableCharacterViewPose *, double bounds[6]);

/* Rear=0, front=64. Horizontal line of sight determines each camera's cell;
 * coincident XY uses camera forward, vertical facing uses rear deterministically.
 * Presentation-only floating point. Failure preserves angle. */
int portable_mesh_driver_art_angle(const PortableRenderPose *, const PortableRenderView *,
                                   uint8_t *angle);
#endif
