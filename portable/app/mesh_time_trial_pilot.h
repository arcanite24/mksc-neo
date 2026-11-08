#ifndef PORTABLE_MESH_TIME_TRIAL_PILOT_H
#define PORTABLE_MESH_TIME_TRIAL_PILOT_H
#include "mesh_time_trial.h"
#include "mesh_cpu.h"
#include "mesh_item_status.h"
typedef struct
{
    PortableMeshCpuState cpu;
    uint32_t last_tick, mode, requested_hint;
    PortableMeshKartInput input;
    uint8_t active;
} PortableMeshTimeTrialPilotState;
typedef struct
{
    int32_t finish_position_q16[3], distance_q16;
    uint32_t gate_count;
} PortableMeshFinishWindow;
/* Aid-only static straight CENTER support/obstacle clearance window,
 * at most40m. Reads the selected continuous flat route and actual final armed
 * gate graph. Requires matching borrowed indexed geometry; missing geometry,
 * uncertainty or bounded-work exhaustion refuses without changing out.
 * This is not a prediction of steering tracking, traffic or item outcomes.
 * It neither supplies inputs nor advances any race/CPU/item state. */
int portable_mesh_time_trial_pilot_finish_window(const PortableMeshTimeTrialSession *,
    const PortableMeshTimeTrialPilotState *, PortableMeshFinishWindow *out);

/* Shared-race G aid only: retain an accepted dragged green only while a
 * hostile flying green/red is inside authored fullXYZ cpu_use_distance and
 * closing relative to the kart (or a red explicitly targets this racer).
 * Otherwise retain the original pulse/release. Reads immutable world/state;
 * inactive/recovering owners, fresh RESCUE and invalid arguments return0. */
uint8_t portable_mesh_time_trial_pilot_defensive_hold(const PortableMeshItems *,
    const PortableMeshRacerState *, const PortableMeshItemRules *, unsigned player,
    PortableMeshKartInput driving, uint8_t periodic_pressed);

/* Shared-race G aid only. Other items retain periodic_pressed exactly.
 * Forced-acceleration items wait for the finish window, then request a fresh
 * edge independent of the periodic pulse. Inactive legacy pilots retain the
 * pulse. NULL arguments refuse. No inventory/status is advanced.
 * Pending/current boosts defer additional stock; this is a static aid choice,
 * not a claim of traffic or controller safety. */
uint8_t portable_mesh_time_trial_pilot_item_press(const PortableMeshTimeTrialSession *,
    const PortableMeshTimeTrialPilotState *, const PortableMeshItemRacer *,
    const PortableMeshItemRacerStatus *, PortableMeshKartInput driving, uint8_t periodic_pressed);

/* Shared G only: estimate a bounded midrace geometric runout from actual MIR
 * duration/cap, selected straight route, braking and body-offset support probes.
 * This conservative heuristic is not a tracking, traffic or XY-speed proof;
 * actual race verification remains required. Final-finish admission is retained.
 * Golden100e repeats are individually gated as one mushroom burst. */
uint8_t portable_mesh_time_trial_pilot_item_press_rules(const PortableMeshTimeTrialSession *,
    const PortableMeshTimeTrialPilotState *, const PortableMeshItemRacer *,
    const PortableMeshItemRacerStatus *, const PortableMeshItemRules *,
    PortableMeshKartInput driving, uint8_t periodic_pressed);

/* Opt-in route aid cursor. Same preturn tick returns cached input. Retry/tick
 * rollback or course/profile identity change selects the lowest nominal-cost
 * closed simple CPU route in GP/Quick/VS only, then uses the unchanged production
 * controller. TT retains its original first-route selection and input behavior.
 * Ranking integrates authored caps/corners/braking; it predicts no race result.
 * Valid graphs without a unique simple lap retain original ABSENT selection
 * after all candidates validate; malformed candidates still fail atomically.
 * Legacy non-opt-in and production ABSENT selection stay unchanged. All failures preserve
 * both outputs; next may alias previous. No planner fields enter replay state. */
PortableMeshStatus portable_mesh_time_trial_pilot_step(PortableMeshTimeTrialPilotState *next,
    const PortableMeshTimeTrialPilotState *previous, const PortableMeshTimeTrialSession *session,
    PortableMeshKartInput *input, char *error, size_t capacity);
/* Explicit aid-only RTHD index, or ABSENT for the default selection above.
 * Hints require continuous GP/Quick/VS routes and never override a later RESP
 * route handoff. Changing the requested hint resets only this input planner. */
PortableMeshStatus portable_mesh_time_trial_pilot_step_route(PortableMeshTimeTrialPilotState *next,
    const PortableMeshTimeTrialPilotState *previous, const PortableMeshTimeTrialSession *session,
    uint32_t route_hint, PortableMeshKartInput *input, char *error, size_t capacity);
/* Aid-only watchdog: route distance at 3 m/s, laps and countdown/settling
 * margin. Bounded malformed/missing routes return the 12000-tick minimum. */
uint32_t portable_mesh_time_trial_pilot_tick_budget(const PortableCourse3DView *course);
/* Aid-only hazard seek for actor contact routes. Each lap it steers the driver
 * to the nearest course actor ahead, hidden ones at their resting place, and
 * parks on it until the driver takes a non-bump contact from that actor or the
 * hold times out, then hands control back to the route controller. While an
 * actor lock (+AC) holds the driver, controls stay released so no fresh
 * accelerator edge cuts the lock short.
 * Reads accepted pre-turn state and the previous turn's actor events; it
 * never advances actors, items or the race. */
enum { PORTABLE_MESH_HAZARD_SEEK_WORDS = 18 }; /* 18 actor families of 64. */
typedef struct
{
    uint64_t visited[PORTABLE_MESH_HAZARD_SEEK_WORDS]; /* Flat actor indices, this lap. */
    uint32_t course_id, lap, last_tick, target, hold_ticks; /* target: flat index + 1. */
    int32_t goal_q16[3]; /* Fixed meeting point at kart height, or the target's resting place. */
    int32_t last_position_q16[3]; /* The target's position last turn, once tracked. */
    int32_t target_height_q16; /* The target's world height last turn, independent of kart motion. */
    PortableMeshKartInput input; /* This turn's override, when overriding. */
    uint8_t valid, overriding, visible, item_pending, goal_low;
    uint8_t tracked; /* last_position_q16 holds this target's previous turn. */
    uint8_t walked; /* The target has moved across the ground since it was chosen. */
    uint8_t fall_ticks; /* Consecutive visible downward steps, saturated at two. */
    uint8_t imminent; /* The actual collidable target is within reach, ahead of the kart. */
    uint8_t contacts; /* Targets left after a landed contact this attempt (saturating). */
} PortableMeshHazardSeekState;
/* Rewrites *input only while seeking, holding or locked. Retry/tick rollback
 * or a course change restarts the seek. Invalid arguments return 0 and
 * preserve both outputs. */
/* Bit f of family_mask admits actor family f (flat order: crabs, snowmen,
 * penguins, mice, piranhas, seagulls, lava bubbles, Thwomps, cannonballs, Boos,
 * rocks, teepees, falling stars, thunderclouds, common props, Mechakoopas,
 * Kamek, pier Boos); 0 admits every family. */
int portable_mesh_time_trial_pilot_seek_hazard(PortableMeshHazardSeekState *seek,
    const PortableMeshRacer *racer, unsigned racer_index, const PortableMeshItems *items,
    const PortableMeshCourseActorsConfig *config, const PortableMeshCourseActors *actors,
    const PortableMeshCourseActorEvents *events, uint32_t family_mask, PortableMeshKartInput *input);
/* While the seek heads for or waits on a hazard, it saves the aid's item
 * press and spends it as a fresh press once the collidable hazard is within
 * contact reach, so a Star covers the contact. Call once per turn after the seek. */
uint8_t portable_mesh_time_trial_pilot_seek_item(PortableMeshHazardSeekState *seek, uint8_t pressed);
/* Ordinary-input route aid. Observes immutable state/content; owns no physics clock. */
PortableMeshStatus portable_mesh_time_trial_pilot(const PortableMeshTimeTrialSession *session,
                                                  PortableMeshKartInput *input, char *error,
                                                  size_t capacity);
#endif
