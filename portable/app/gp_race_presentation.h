#ifndef PORTABLE_GP_RACE_PRESENTATION_H
#define PORTABLE_GP_RACE_PRESENTATION_H

#include "gp_race_session.h"
#include "race_scene_renderer.h"

int portable_gp_race_presentation_build(
    const PortableGpRaceSession *session,
    const PortableGpRaceSnapshot *snapshot,
    PortableRacePresentationState *state);

#endif
