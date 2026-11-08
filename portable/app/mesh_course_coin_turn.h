#ifndef PORTABLE_MESH_COURSE_COIN_TURN_H
#define PORTABLE_MESH_COURSE_COIN_TURN_H
#include "mesh_course_actors.h"
/* Settle accepted typed outcomes once, before publishing gates. Input worlds
 * and racers are immutable snapshots; failure preserves coin state and events.
 * No loss is inferred from generic wall flags, velocity or proximity. */
int portable_mesh_course_coin_turn(
    const PortableMeshCourseCoinsConfig *, PortableMeshCourseCoins *, uint32_t mode,
    const PortableMeshKartRules *, const int32_t body_scale_q16[8],
    const PortableMeshRacerState *before, const PortableMeshRacerState *after, size_t count,
    const PortableMeshItems *before_items, const PortableMeshItems *after_items,
    const PortableMeshCourseActorEvents *, const PortableMeshItemEvents *,
    uint8_t ordinary_contact_mask, PortableMeshCourseCoinEvents *, char *, size_t);
#endif
