#ifndef PORTABLE_MESH_COURSE_STRONG_HIT_H
#define PORTABLE_MESH_COURSE_STRONG_HIT_H
#include "mesh_kart.h"
#include <stdint.h>

/* Source E490/A9AC/3956C angular response, independent of SI knockback.
 * Serialize named fields only: rate/angle LE16, then the three flag bytes. */
typedef struct
{
    uint16_t rate, angle;
    uint8_t pending, active, direction;
} PortableMeshStrongHitState;

int portable_mesh_course_strong_hit_valid(const PortableMeshStrongHitState *);
/* Initial rate is source0x1000 or0x2000; direction selects positive rotation.
 * Re-admission preserves the source angle; eligibility belongs to the caller. */
int portable_mesh_course_strong_hit_admit(PortableMeshStrongHitState *, uint16_t, uint8_t);
/* Once per accepted racing turn. Airborne pending requests wait for grounding;
 * an active response continues in air. The completion turn remains held.
 * Recovery clears state. Failures preserve owner and held output. */
int portable_mesh_course_strong_hit_tick(PortableMeshStrongHitState *, int grounded,
                                        int recovering, int *held);
/* No velocity mutation: preserve separately admitted radial and vertical motion.
 * Suppress driving/hop and MT/panel/item boosts through ordinary mesh effects.
 * Canceling a retained travel angle resynchronizes scalar speed with facing.
 * Invalid state/projection preserves both kart and effects. */
PortableMeshStatus portable_mesh_course_strong_hit_hold(PortableMeshKartState *,
                                                        PortableMeshKartEffects *);
#endif
