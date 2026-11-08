#ifndef PORTABLE_MESH_ITEM_STATUS_H
#define PORTABLE_MESH_ITEM_STATUS_H
#include "mesh_item_inventory.h"
#include "mesh_kart.h"
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint16_t mushroom_ticks, golden_ticks, star_ticks, lightning_ticks;
    uint16_t spin_ticks, hit_grace_ticks, boo_invisible_ticks, boo_lock_ticks;
    uint16_t bullet_ticks, ink_ticks; /* Bullet Bill ride; Blooper ink */
    uint8_t pending_mushroom;
} PortableMeshItemRacerStatus;
enum
{
    PORTABLE_MESH_ITEM_STATUS_AUXILIARIES = 64,
    PORTABLE_MESH_SOURCE_RIDER_LOCK_TICKS = 360,
    PORTABLE_MESH_SOURCE_CLOUD_LIGHTNING_TICKS = 360
};
typedef struct
{
    uint16_t ticks;
    uint8_t owner;
} PortableMeshItemStatusAuxiliary;
typedef enum
{
    PORTABLE_MESH_ITEM_STATUS_OK,
    PORTABLE_MESH_ITEM_STATUS_INVALID,
    PORTABLE_MESH_ITEM_STATUS_INELIGIBLE,
    PORTABLE_MESH_ITEM_STATUS_RESOURCE
} PortableMeshItemStatusResult;
typedef struct
{
    PortableMeshItemStatusAuxiliary auxiliaries[PORTABLE_MESH_ITEM_STATUS_AUXILIARIES];
    PortableMeshItemRacerStatus racers[PORTABLE_MESH_ITEM_RACERS];
    uint8_t count, human_mask;
} PortableMeshItemStatusOwner;
typedef enum
{
    PORTABLE_MESH_STATUS_MUSHROOM,
    PORTABLE_MESH_STATUS_GOLDEN_START,
    PORTABLE_MESH_STATUS_GOLDEN_REPEAT,
    PORTABLE_MESH_STATUS_STAR,
    PORTABLE_MESH_STATUS_BOO_INVISIBLE,
    PORTABLE_MESH_STATUS_BOO_LOCK,
    PORTABLE_MESH_STATUS_RIDER_LOCK,
    PORTABLE_MESH_STATUS_CLOUD_LIGHTNING,
    PORTABLE_MESH_STATUS_BULLET, /* protected and boosted like Star; the race autopilots it */
    PORTABLE_MESH_STATUS_INK
} PortableMeshItemActivation;
typedef enum
{
    PORTABLE_MESH_DAMAGE_IGNORED,
    PORTABLE_MESH_DAMAGE_PROTECTED,
    PORTABLE_MESH_DAMAGE_APPLIED
} PortableMeshItemDamage;

/* Modern SI timer convention, not a claim about original callback chronology:
 * advance ONCE at the start of each accepted world tick, then activate/damage,
 * resolve effects, then physics. A fresh N timer affects this and the following
 * N-1 accepted ticks. Paused worlds do not call advance. All functions stage
 * their changes: validation failure leaves owner and outputs unchanged. */
PortableMeshItemStatusResult portable_mesh_item_status_init(PortableMeshItemStatusOwner *out,
                                                            unsigned count, unsigned human_mask);
/* Golden expiry reports a bit for the caller to clear CURRENT inventory, even
 * if stolen/replaced since activation. It never reads an inventory pointer. */
PortableMeshItemStatusResult portable_mesh_item_status_advance(PortableMeshItemStatusOwner *owner,
                                                               const PortableMeshItemRules *rules,
                                                               unsigned *golden_expired_mask);
/* Source course haunts assign the BOO1 data duration only after approach.
 * Signed-positive source clocks support1..32767; this does not change any MIR
 * activation duration or serialized field. Active Star refuses transactionally. */
PortableMeshItemStatusResult portable_mesh_item_status_source_haunt_lock(
    PortableMeshItemStatusOwner *, const PortableMeshItemRules *, unsigned racer,
    uint16_t duration);
/* Each accepted single/triple mushroom successor requests the same pending bit;
 * airborne requests coalesce like the source driver request, not extra stock.
 * RIDER_LOCK overwrites the shared Boo/source rider lock with360, refusing
 * under Star; ordinary BOO_LOCK still uses its authored profile duration.
 * CLOUD_LIGHTNING overwrites shared shrink with360, refusing active Star.
 * Actor admission happens after movement: fresh360 becomes359 next accepted
 * advance; this explicit modern phase differs from the source driver pass.
 * A golden repeat never extends independent expiries. Initial goldens each own
 * an auxiliary; caller must reserve shared world actor capacity before activation.
 * RESOURCE refuses creation without consuming its inventory or changing state. */
PortableMeshItemStatusResult
portable_mesh_item_status_activate(PortableMeshItemStatusOwner *owner,
                                   const PortableMeshItemRules *rules, unsigned racer,
                                   PortableMeshItemActivation activation);
/* Explicit caller eligibility excludes finished/respawning/special states.
 * Airborne by itself is not immunity. User and Star-protected victims skip. */
PortableMeshItemStatusResult
portable_mesh_item_status_lightning(PortableMeshItemStatusOwner *owner,
                                    const PortableMeshItemRules *rules, unsigned user,
                                    const uint8_t *eligible, size_t count, unsigned *affected_mask);
/* Only APPLIED authorizes the caller's one physical hit impulse/speed reduction.
 * Protection can destroy an attacker without damage. Invisibility/grace refuse
 * new damage. Applied damage cancels pending/current mushroom and Boo lock. */
PortableMeshItemStatusResult portable_mesh_item_status_damage(PortableMeshItemStatusOwner *owner,
                                                              const PortableMeshItemRules *rules,
                                                              unsigned racer, int eligible,
                                                              PortableMeshItemDamage *out);
/* Lightning response deliberately bypasses ordinary projectile invisibility
 * and old hit grace. Explicit eligibility still rejects recovery/finish, and
 * Star still reports PROTECTED. Invisibility's independent timer is retained. */
PortableMeshItemStatusResult
portable_mesh_item_status_lightning_impact(PortableMeshItemStatusOwner *,
                                           const PortableMeshItemRules *, unsigned racer,
                                           int eligible, PortableMeshItemDamage *);
/* Resolves/consumes a queued mushroom only on caller-approved grounded turns
 * without spin. Star has boost priority; shrink clocks run underneath Star.
 * Spin cancels MT/panel and inhibits drive/hop. Boo lock uses the shared MIR slow cap without
 * shrinking the body (modern SI adaptation of the source common slow-cap gate). Repeated views do not advance any timers. */
PortableMeshItemStatusResult
portable_mesh_item_status_prepare_drive(PortableMeshItemStatusOwner *owner,
                                        const PortableMeshItemRules *rules, unsigned racer,
                                        int eligible_ground, PortableMeshKartEffects *out);
/* Active auxiliary actors must participate in the world's shared actor budget. */
PortableMeshItemStatusResult
portable_mesh_item_status_auxiliary_count(const PortableMeshItemStatusOwner *owner,
                                          const PortableMeshItemRules *rules, unsigned *out);
/* Explicit respawn/finish cut: clear pending/mushroom/spin. Independent golden,
 * protection, shrink, grace and Boo clocks remain owned until their expiry. */
PortableMeshItemStatusResult
portable_mesh_item_status_clear_transient(PortableMeshItemStatusOwner *owner,
                                          const PortableMeshItemRules *rules, unsigned racer);
#endif
