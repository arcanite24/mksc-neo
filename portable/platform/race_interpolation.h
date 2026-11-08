#ifndef PORTABLE_RACE_INTERPOLATION_H
#define PORTABLE_RACE_INTERPOLATION_H
#include "race_scene_renderer.h"
#include "recovered_time_trial_session.h"
/* alpha is Q16 in [0,65536]. Copy discrete state from current; never mutate
 * simulation snapshots. Large position jumps and lifecycle changes snap. */
void portable_race_interpolate(const PortableRacePresentationState *previous,
    const PortableRacePresentationState *current,uint32_t alpha,PortableRacePresentationState *out);
void portable_trial_interpolate(const PortableRecoveredTimeTrialSnapshot *previous,
    const PortableRecoveredTimeTrialSnapshot *current,uint32_t alpha,PortableRecoveredTimeTrialSnapshot *out);
uint16_t portable_angle_interpolate(uint16_t previous,uint16_t current,uint32_t alpha);
int portable_present_due(uint64_t now,uint16_t fps,uint64_t *next_deadline);
#endif
