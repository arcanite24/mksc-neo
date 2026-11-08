#ifndef PORTABLE_TITLE_RENDERER_H
#define PORTABLE_TITLE_RENDERER_H
#include "frontend_presentation.h"

/* Native title image IDs within the frontend graphics section kind. */
enum {
    PORTABLE_TITLE_BACKGROUND = 1,
    PORTABLE_TITLE_LABEL = 0x20000,
    PORTABLE_TITLE_SELECTED_LABEL = 0x20100,
    PORTABLE_TITLE_FRAME = 0x20200,
    PORTABLE_TITLE_UP = 0x20201,
    PORTABLE_TITLE_DOWN = 0x20202,
    PORTABLE_TITLE_LINK_FRAME = 0x20203,
    PORTABLE_TITLE_BACKGROUND_FRAME = 0x20300,
    PORTABLE_TITLE_NIGHT_BACKGROUND = 0x20400, /* +1..4 flag frames */
    PORTABLE_TITLE_SUNSET_BACKGROUND = 0x20500, /* +1..4 flag frames */
    PORTABLE_TITLE_NIGHT_TWINKLE = 0x20600, /* +1..7 star colour rows */
    PORTABLE_TITLE_WIDE_SKY = 0x20900, /* +2*variant: normal, sunset, night */
    PORTABLE_TITLE_WIDE_CLOUDS = 0x20901,
    PORTABLE_TITLE_WIDE_METADATA = 0x209ff
};
/* Optional source-scene wings; centered logo, drivers and flags remain native.
 * Explicit wide assets in the resolved pack take precedence over companion art. */
PortableFrontendPresentationStatus portable_title_background_render(
    const PortableAssetPackView *pack,const PortableAssetPackView *background_pack,
    const PortableFrontendFlow *flow,PortableSoftwareFrame *frame);
PortableFrontendPresentationStatus portable_title_render(
    const PortableAssetPackView *pack, const PortableFrontendFlow *flow,
    PortableFrontendPage page, PortableSoftwareFrame *frame);
#endif
