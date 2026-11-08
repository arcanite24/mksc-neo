#ifndef PORTABLE_MESH_TIME_TRIAL_H
#define PORTABLE_MESH_TIME_TRIAL_H
#include "mesh_racer.h"
#include "mesh_items.h"
#include "mesh_course_actors.h"
enum
{
    PORTABLE_MESH_TT_COUNTDOWN = PORTABLE_MESH_RACER_COUNTDOWN,
    PORTABLE_MESH_TT_RACING = PORTABLE_MESH_RACER_RACING,
    PORTABLE_MESH_TT_FINISHED = PORTABLE_MESH_RACER_FINISHED
};
typedef PortableMeshRacerState PortableMeshTimeTrialState;
typedef PortableMeshRacer PortableMeshTimeTrialSession;
typedef PortableMeshRacer PortableMeshTimeTrial;
/* Fixed canonical LE words, shared by state checksums and replay checkpoint codecs. */
#define PORTABLE_MESH_TIME_TRIAL_STATE_WORDS 44u
int portable_mesh_time_trial_state_write(const PortableMeshTimeTrialState *state, uint8_t *bytes,
                                         size_t capacity);
int portable_mesh_time_trial_state_read(PortableMeshTimeTrialState *state, const uint8_t *bytes,
                                        size_t size);
uint64_t portable_mesh_time_trial_checksum(const PortableMeshTimeTrialState *state);
PortableMeshStatus portable_mesh_time_trial_init(PortableMeshTimeTrial *session,
                                                 const PortableCourse3DView *course,
                                                 const PortableMeshKartRules *rules, uint32_t grid,
                                                 char *error, size_t capacity);
PortableMeshStatus portable_mesh_time_trial_tick(PortableMeshTimeTrial *session,
                                                 PortableMeshKartInput input,
                                                 PortableMeshKartEvents *events, char *error,
                                                 size_t capacity);
/* Separate optional item owner; no fields are added to the legacy TT state or
 * its 44-word codec. Config borrows exactly session.course/session.rules and
 * uses mode2/count1/human_mask1. Initialize world with portable_mesh_items_init
 * at Start/Retry. Countdown and finished turns leave world clocks unchanged
 * and emit no item events; callers implement Pause by not calling either tick.
 * Live turns stage both owners and both event outputs through gate admission.
 * Fresh RESCUE derives recovering from kart input, not the caller's item flag.
 * Owners and event outputs must be separate, nonoverlapping storage. */
PortableMeshStatus portable_mesh_time_trial_tick_items(
    PortableMeshTimeTrial *session, PortableMeshItems *world, const PortableMeshItemsConfig *config,
    PortableMeshKartInput input, PortableMeshItemControls item_controls,
    PortableMeshKartEvents *events, PortableMeshItemEvents *item_events, char *error,
    size_t capacity);

/* Required source controllers share one transactional turn with movement and
 * gates. Hazards advance only while racing; sky advances during countdown and
 * racing. Finished turns freeze all owners. Sky-only courses pass NULL for both
 * item arguments and neutral item controls; interactive actors/coins need MIR. */
PortableMeshStatus portable_mesh_time_trial_tick_actors(
    PortableMeshTimeTrial *session, PortableMeshItems *world, const PortableMeshItemsConfig *config,
    PortableMeshCourseActors *actors, const PortableMeshCourseActorsConfig *actor_config,
    PortableMeshKartInput input, PortableMeshItemControls item_controls,
    PortableMeshKartEvents *events, PortableMeshItemEvents *item_events,
    PortableMeshCourseActorEvents *actor_events, char *error, size_t capacity);
#endif
