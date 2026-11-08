#ifndef PORTABLE_MESH_BATTLE_H
#define PORTABLE_MESH_BATTLE_H

#include "battle_balloon.h"
#include <stddef.h>

enum
{
    PORTABLE_MESH_BATTLE_RACERS = 8
};
typedef struct
{
    uint8_t racer_count, starting_balloons, elimination_ticks;
} PortableMeshBattleConfig;
typedef struct
{
    PortableMeshBattleConfig config;
    uint16_t hit_flags[PORTABLE_MESH_BATTLE_RACERS];
    uint8_t balloons[PORTABLE_MESH_BATTLE_RACERS];
    PortableBattleEliminationState elimination[PORTABLE_MESH_BATTLE_RACERS];
    uint8_t standings[PORTABLE_MESH_BATTLE_RACERS], ranks[PORTABLE_MESH_BATTLE_RACERS];
    uint32_t accepted_tick;
    uint8_t tick_valid, finished;
} PortableMeshBattle;
typedef struct
{
    uint32_t popped_mask, eliminated_mask, bomb_mask;
} PortableMeshBattleEvents;

/* Pointer-free role owner, independent of kart handling and item geometry.
 * Admit 2..8 racers, 1..3 balloons, duration 1..127; source defaults are 3/1.
 * Initial standings use racer order. All failures preserve every output. */
int portable_mesh_battle_init(PortableMeshBattle *, const PortableMeshBattleConfig *, char *error,
                              size_t capacity);
/* Call after all racers' accepted damage is known, only on live fixed ticks.
 * Each bit queues one actual damage request; repeated presentations must not
 * fabricate damage. C444 runs for every racer, then D328 for every racer.
 * Same accepted tick is silent; later live ticks must be consecutive. Results
 * freeze at <=1 survivor, including simultaneous zero-survivor stable ties.
 * Popped includes the last balloon; eliminated/bomb mark only fresh changes.
 * Finished owners remain unchanged and produce no events. */
int portable_mesh_battle_step(PortableMeshBattle *, uint32_t accepted_tick, uint32_t damage_mask,
                              PortableMeshBattleEvents *, char *error, size_t capacity);
#endif
