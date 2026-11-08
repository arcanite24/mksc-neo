#ifndef PORTABLE_UI_ART_H
#define PORTABLE_UI_ART_H

#include "asset_pack.h"
#include "ui_canvas.h"

/* Bounded cache of frontend images from the effective content pack, decoded
 * to straight ARGB and optionally smoothed with EPX (scale2x) passes so
 * pixel art stays crisp but rounded when drawn large by the modern menus.
 * The cache borrows nothing after decoding. Storage keys distinguish live
 * packs; clear before retiring an owner to prevent reused-address hits. */
typedef struct UiArtCache UiArtCache;

UiArtCache *ui_art_create(void);
/* Non-allocating content-generation boundary. Invalidates decoded images even
 * when a later pack reuses earlier allocation addresses. */
void ui_art_clear(UiArtCache *cache);
void ui_art_destroy(UiArtCache *cache);
/* smooth: 0 keeps source pixels, 1 applies two EPX passes (4x). Returns
 * NULL when the pack has no such image. */
const UiImage *ui_art_image(UiArtCache *cache, const PortableAssetPackView *pack, uint32_t id,
                            int smooth);
/* Image ID shown by a frontend animation at tick, or 0 when it is absent.
 * A plain image ID is returned unchanged. */
uint32_t ui_art_animation_frame(const PortableAssetPackView *pack, uint32_t id, uint32_t tick);
/* Total ticks of a frontend animation (0 when id is not an animation). */
uint32_t ui_art_animation_duration(const PortableAssetPackView *pack, uint32_t id);
/* Average opaque color of an image (portrait backgrounds tint their tiles). */
uint32_t ui_art_average_color(const UiImage *image);

#endif
