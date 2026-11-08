#ifndef PORTABLE_MESH_ITEM_SCENE_H
#define PORTABLE_MESH_ITEM_SCENE_H
#include "mesh_item_art.h"
#include "mesh_items.h"
#include "model_asset.h"
#include "render_scene.h"
/* Instances the scene may add beyond one per box and actor (box shards). */
enum { PORTABLE_MESH_ITEM_SCENE_EFFECTS = 48 };
typedef struct PortableMeshItemScene PortableMeshItemScene;
/* Zero initialize *owner. Stable quad/texture identities borrow art until
 * backend meshes/textures are forgotten, then scene is closed, then art.
 * Open failure preserves the prior owner; no SDL or simulation ownership. */
int portable_mesh_item_scene_open(const PortableMeshItemArt *, PortableMeshItemScene **owner,
                                  char *error, size_t capacity);
void portable_mesh_item_scene_close(PortableMeshItemScene *);
/* Draw the content platform's item models (PORTABLE_MODEL_ITEM bindings of
 * item rules profile_id) instead of the stock sprites: per item, the box
 * (looping its rig's IDLE clip), shards of a collected box and effects.
 * Models draw at kart_scale (the course's META mksc:kart-scale, 1 for stock)
 * times their binding scale; the Bob-omb blast keeps its physical radius.
 * Slots without a binding keep the stock art. Forget backend meshes first;
 * failure leaves no models. */
int portable_mesh_item_scene_open_models(PortableMeshItemScene *, const PortableAssetPackView *,
                                         uint32_t profile_id, double kart_scale, char *error, size_t capacity);
/* The platform's HUD icon of item (a PORTABLE_MODEL_ITEM_ICON binding), or
 * NULL to keep the stock icon. Borrowed from the pack opened above. */
const PortableNativeImage *portable_mesh_item_scene_icon(const PortableMeshItemScene *, uint16_t item);
/* The model of an item that replaces its rider's kart (Bullet Bill) at
 * position facing yaw; 0 when the platform binds none (keep the kart). */
int portable_mesh_item_scene_rider(const PortableMeshItemScene *, uint16_t item, const double position[3], double yaw,
                                   uint32_t tick, PortableRenderInstance *out);
/* Material 0's texture of a bound slot (the Blooper ink splat for screen
 * overlays), or NULL. */
const PortableNativeImage *portable_mesh_item_scene_overlay(const PortableMeshItemScene *, unsigned slot);
/* Whole-kart omission from source 49D78: self blinks on the accepted scene
 * clock; other views hide through the interior of Boo's timer. No state writes.
 * View indices correspond to the first human racers, at most four. */
uint32_t portable_mesh_item_scene_kart_hidden_views(const PortableMeshItems *, unsigned racer,
                                                   unsigned viewers);
/* Append immutable accepted world poses. One native16px span is 2*MIR actor
 * radius metres (a32px box is4r); source anchors are normalized to image
 * centers because ITEM and actor positions are collision-sphere centers.
 * The caller retains every existing instance and owns the global frame budget.
 * All placements, including cooldown-hidden boxes, are validated before writes.
 * Inputs must remain immutable throughout both passes. Failure preserves count
 * and every caller instance. No allocation, cache/texture writes or clocks. */
int portable_mesh_item_scene_append(const PortableMeshItemScene *, const PortableCourse3DView *,
                                    const PortableMeshItems *, const PortableMeshItemRules *, uint32_t mode,
                                    PortableRenderInstance *, size_t capacity, size_t *count,
                                    char *error, size_t error_capacity);
#endif
