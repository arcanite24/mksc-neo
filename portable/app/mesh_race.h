#ifndef PORTABLE_MESH_RACE_H
#define PORTABLE_MESH_RACE_H

#include "mesh_racer.h"
#include "mesh_cpu.h"
#include "mesh_items.h"
#include "mesh_battle.h"
#include "mesh_course_actors.h"

enum
{
    PORTABLE_MESH_RACE_MAX = 8,
    PORTABLE_MESH_LOCAL_MAX = 4
};
typedef struct
{
    PortableMeshRacerState racers[PORTABLE_MESH_RACE_MAX];
    uint32_t tick, player_count, finish_count;    /* VS includes its retired final rank. */
    uint8_t finish_order[PORTABLE_MESH_RACE_MAX]; /* Actual finishers, then frozen results. */
    uint8_t finish_rank[PORTABLE_MESH_RACE_MAX];  /* 1-based; pending=0 until results freeze. */
    uint8_t retired[PORTABLE_MESH_RACE_MAX];      /* Ranked without a fabricated finish time. */
    uint8_t standing_order[PORTABLE_MESH_RACE_MAX], standing_rank[PORTABLE_MESH_RACE_MAX];
    uint8_t results_ready;
} PortableMeshRaceState;
typedef struct
{
    const PortableCourse3DView *course;
    const PortableMeshKartRules *rules;
    /* Optional per-driver profiles (NULL: rules); borrowed like rules and
     * validated by portable_mesh_race_set_racer_rules. */
    const PortableMeshKartRules *racer_rules[PORTABLE_MESH_RACE_MAX];
    int32_t fall_limit_q16;
    uint32_t spawn_grids[PORTABLE_MESH_RACE_MAX];
    uint32_t mode, human_count, checkpoint_stages; /* player_count is total racers. */
    PortableMeshCpuState cpu[PORTABLE_MESH_RACE_MAX];
    PortableMeshRaceState state;
    /* Borrowed resolved MIR profile; platform may rebind to equivalent owned
     * content before the next call. No profile pointer enters item state. */
    const PortableMeshItemRules *item_rules;
    PortableMeshItems items;
    PortableMeshItemEvents item_events; /* Immutable last accepted item turn. */
    uint8_t items_enabled;
    PortableMeshBattle battle; /* Used only in mode16; TT/MIS1 codecs stay separate. */
    PortableMeshBattleEvents battle_events;
    uint8_t bomb_recovery_ticks[PORTABLE_MESH_RACE_MAX];
    uint8_t bullet_pilots; /* humans whose Bullet Bill rides on cpu[i] */
    uint8_t recovery_pending[PORTABLE_MESH_RACE_MAX], recovery_hit_mask;
    const PortableMeshCourseActorsConfig *actor_config; /* Borrowed immutable content. */
    PortableMeshCourseActors actors;
    PortableMeshCourseActorEvents actor_events;
    const PortableMeshCourse *geometry; /* Borrowed, outside serialized state. */

} PortableMeshRaceSession;

/* Separate explicit MBS1 checksum domain: kart44/MIS1 remain unchanged.
 * Hashes accepted simulation/roles/recovery, excluding pointers/events/art. */
uint64_t portable_mesh_race_battle_checksum(const PortableMeshRaceSession *);

/* Local VS currently admits 2..4 humans. NULL grids selects starts0..N-1. */
PortableMeshStatus portable_mesh_race_init(PortableMeshRaceSession *session,
                                           const PortableCourse3DView *course,
                                           const PortableMeshKartRules *rules, uint32_t players,
                                           const uint32_t *grids, char *error, size_t capacity);
/* GP/Quick currently admit one human and eight racers; VS admits 2..4 humans.
 * Battle admits 1..4 humans and 2..8 total racers, with three balloons and revenge Bomb recovery.
 * CPU controllers and every racer are prepared transactionally. */
PortableMeshStatus
portable_mesh_race_init_mode(PortableMeshRaceSession *session, const PortableCourse3DView *course,
                             const PortableMeshKartRules *rules, uint32_t mode, uint32_t humans,
                             uint32_t racers, const uint32_t *grids, char *error, size_t capacity);
/* Before any tick: racer drives with rules (NULL restores the shared profile).
 * It must be valid and fit the enabled item profile. */
PortableMeshStatus portable_mesh_race_set_racer_rules(PortableMeshRaceSession *, uint32_t racer,
                                                      const PortableMeshKartRules *rules, char *,
                                                      size_t);
/* Enable before any tick; interactive actors/coins need MIR, skies alone do not. */
PortableMeshStatus portable_mesh_race_enable_actors(PortableMeshRaceSession *,
                                                    const PortableMeshCourseActorsConfig *, char *,
                                                    size_t);
/* Display-only results: unfinished GP/Quick rivals have an explicit estimate;
 * this never changes their phase, lap or real finish time. */
int portable_mesh_race_result_ticks(const PortableMeshRaceSession *session, uint32_t racer,
                                    uint32_t *ticks, int *estimated);
/* Explicit opt-in before the first accepted tick. The caller resolves the
 * course's declared MIR asset; its asset ID need not equal profile_id.
 * Failure preserves the session. Retry uses init + enable again. */
PortableMeshStatus portable_mesh_race_enable_items(PortableMeshRaceSession *session,
                                                   const PortableMeshItemRules *rules,
                                                   uint32_t seed, char *error, size_t capacity);
/* Separate human held-item/aim controls; NULL supplies neutral controls.
 * CPU controls belong to the item world. Both phases and all movement/gates
 * commit together. Countdown does not consume item clocks or input edges. */
PortableMeshStatus portable_mesh_race_tick_items(PortableMeshRaceSession *session,
                                                 const PortableMeshKartInput *inputs,
                                                 const PortableMeshItemControls *item_controls,
                                                 PortableMeshKartEvents *events, char *error,
                                                 size_t capacity);
/* Inputs have human_count elements; events have player_count elements.
 * Failure preserves session/events, including every CPU controller.
 * Countdown, respawn/grace and finished racers never participate in contacts.
 * Same-turn finishes sort by crossing fraction, then by stable racer index.
 * Original VS ends after all but one finish; the remaining racer is retired. */
PortableMeshStatus portable_mesh_race_tick(PortableMeshRaceSession *session,
                                           const PortableMeshKartInput *inputs,
                                           PortableMeshKartEvents *events, char *error,
                                           size_t capacity);

#endif
