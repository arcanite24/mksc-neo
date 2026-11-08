#ifndef PORTABLE_FRONTEND_CONFIG_H
#define PORTABLE_FRONTEND_CONFIG_H
#include "asset_pack.h"
#include "model_asset.h"
#include "frontend_flow.h"
#include "gp_cup_session.h"
/* GBA, Extra and custom cups of four, plus four arenas. */
#define PORTABLE_FRONTEND_COURSE_CAPACITY \
    (3u * PORTABLE_FRONTEND_CUPS * PORTABLE_FRONTEND_TRACKS_PER_CUP + 4u)
/* Course manifests a layered pack may hold (stock 44 plus mod IDs). */
#define PORTABLE_FRONTEND_MANIFEST_CAPACITY 128u
/* Shared live/replay setup: title list sine, heading/medallion owners and
 * per-group legacy strip override fallback. Call after flow initialization. */
int portable_frontend_configure_motion(const PortableAssetPackView *pack,PortableFrontendFlow *flow);
int build_frontend_config(const PortableAssetPackView *pack,
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY],
    PortableFrontendCourseSlot courses[PORTABLE_FRONTEND_COURSE_CAPACITY],PortableFrontendConfig *config);
/* Appends the selectable 3D-only drivers of kind110 rows on pages after
 * the existing ones, borrowing their donor's seat for voices and fallbacks.
 * Returns 0 when they do not fit; config is then unchanged. */
int portable_frontend_add_3d_drivers(const PortableDriver3D *rows,size_t count,
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY],PortableFrontendConfig *config);
/* Appends one platform's character pages: its selectable rows and its rows
 * for stock drivers (its own versions of them), in row order around Random.
 * Returns 0 when they do not fit; config is then unchanged. */
int portable_frontend_add_platform_drivers(const PortableDriver3D *rows,size_t count,uint8_t platform,
    PortableFrontendDriverSlot drivers[PORTABLE_FRONTEND_DRIVER_CAPACITY],PortableFrontendConfig *config);
int portable_frontend_course_song(const PortableAssetPackView *pack,
                                  uint32_t course_id,uint16_t *song);
/* cup is a course_cup: GBA 0..4, Extra 5..9 or custom 11..15. */
int portable_frontend_gp_cup(const PortableAssetPackView *pack,uint8_t cup,
                             PortableGpCupDefinition *definition);
/* Bit t set for each track slot the cup defines (Quick Run needs only the
 * chosen one); fills definition when any slot exists. */
uint8_t portable_frontend_cup_tracks(const PortableAssetPackView *pack,uint8_t cup,
                                     PortableGpCupDefinition *definition);
#endif
