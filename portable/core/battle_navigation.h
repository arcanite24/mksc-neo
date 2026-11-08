#ifndef PORTABLE_BATTLE_NAVIGATION_H
#define PORTABLE_BATTLE_NAVIGATION_H

#include "battle_ai_rules.h"
#include "terrain_query.h"
#include "track_material.h"

#define PORTABLE_BATTLE_NAV_SIDE 128u
#define PORTABLE_BATTLE_NAV_CELLS (PORTABLE_BATTLE_NAV_SIDE*PORTABLE_BATTLE_NAV_SIDE)
#define PORTABLE_BATTLE_NAV_GOALS 68u

typedef struct { int32_t x,y; } PortableBattleNavPoint;
typedef struct {
    uint8_t passable[PORTABLE_BATTLE_NAV_CELLS];
    uint16_t parent[PORTABLE_BATTLE_NAV_CELLS];
    uint16_t queue[PORTABLE_BATTLE_NAV_CELLS];
} PortableBattleNavWorkspace;
typedef struct {
    PortableBattleNavPoint waypoint;
    uint16_t goal;
    uint8_t reachable;
} PortableBattleNavRoute;

/* Rebuild from the current owned terrain, including destroyed walls. Ties follow
 * north/east/south/west expansion; goals sharing a cell use goal order. No RNG.
 * No reachable goal is a successful query with reachable=0, not a fake path. */
int portable_battle_navigation_route(PortableBattleNavWorkspace *work,
    const PortableTerrainView *terrain,const PortableTrackMaterialView *material,
    const PortableBattleAiRules *rules,PortableBattleNavPoint start,
    const PortableBattleNavPoint *goals,size_t goal_count,PortableBattleNavRoute *out);

#endif
