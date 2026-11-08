#ifndef MKSC_PORTABLE_FRONTEND_PRESENTATION_H
#define MKSC_PORTABLE_FRONTEND_PRESENTATION_H

#include "asset_pack.h"
#include "frontend_flow.h"
#include "software_frame.h"

typedef enum
{
    PORTABLE_FRONTEND_PRESENTATION_OK = 0,
    PORTABLE_FRONTEND_PRESENTATION_INVALID_ARGUMENT,
    PORTABLE_FRONTEND_PRESENTATION_MISSING_SECTION,
    PORTABLE_FRONTEND_PRESENTATION_BAD_SECTION,
    PORTABLE_FRONTEND_PRESENTATION_BAD_FRAME,
} PortableFrontendPresentationStatus;

/* Optional host drawing for 3D-only drivers on the character page. draw
 * puts the driver's preview centred at native (center_x, center_y), size
 * native pixels wide, in place of its seat driver's stock preview; portrait
 * fits its own portrait into the native rectangle of a grid slot. Each returns
 * 1 when it drew; 0 keeps the seat driver's stock art. */
typedef struct
{
    int (*draw)(void *owner, const PortableFrontendFlow *flow, uint32_t driver_id, int center_x, int center_y,
                unsigned size, PortableSoftwareFrame *frame);
    int (*portrait)(void *owner, uint32_t driver_id, int left, int top, int width, int height,
                    PortableSoftwareFrame *frame);
    void *owner;
} PortableCharacterPreviewHook;

typedef struct
{
    uint8_t reserved; /* Kept for the shared host API; no full-page compositor. */
    uint8_t extended_backgrounds; /* Opt-in; foreground remains native and centered. */
    const PortableAssetPackView *title_background_pack; /* Borrowed optional side layers. */
    PortableCharacterPreviewHook character_preview; /* Optional; zero keeps stock art. */
} PortableFrontendPresentationScratch;

PortableFrontendPresentationStatus portable_frontend_presentation_render(
    const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
    PortableSoftwareFrame *frame, PortableFrontendPresentationScratch *scratch);
const char *portable_frontend_presentation_status_name(
    PortableFrontendPresentationStatus status);

#endif
