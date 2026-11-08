#ifndef PORTABLE_MESH_BATTLE_PILOT_H
#define PORTABLE_MESH_BATTLE_PILOT_H

#include "mesh_race.h"

/* Read-only combat controls for an accepted preturn Battle world. Stable full
 * XYZ nearest targets are genuine available ITEM rows or eligible live rivals.
 * No grants, damage, movement, clocks or controller state are synthesized.
 * Outputs must not alias the session/content; every failure preserves both.
 * Blocked/no targets patrol the first closed CPU route through the existing
 * route pilot. Missing such a fallback is named UNSUPPORTED, not a fake route. */
PortableMeshStatus portable_mesh_battle_pilot(const PortableMeshRaceSession *, unsigned racer,
    PortableMeshKartInput *, PortableMeshItemControls *, char *error, size_t capacity);
#endif
