#ifndef PORTABLE_GP_CEREMONY_RENDERER_H
#define PORTABLE_GP_CEREMONY_RENDERER_H
#include "character_asset.h"
#include "gp_ceremony_asset.h"
#include "gp_ceremony_session.h"
#include "software_frame.h"
#include "asset_pack.h"
#include "race_scene_renderer.h"
#include "recovered_pack.h"
typedef struct {
    const PortableRaceSceneView *scene;
    const PortableRacePresentationState *state;
    const PortableGpGridRow *grid;
    size_t grid_count;
} PortableGpCeremonyWorldView;
/* pack supplies the menu font for native captions; NULL leaves them out. */
int portable_gp_ceremony_render(const PortableAssetPackView *pack,
    const PortableGpCeremonyAssetView *asset,
    const PortableGpCeremonySession *ceremony,
    const PortableGpCeremonyPresentation *presentation,
    const PortableCharacterAssetView *const podium[3],
    const PortableCharacterAssetView *selected,
    const PortableGpCeremonyWorldView *world,PortableSoftwareFrame *frame);
#endif
