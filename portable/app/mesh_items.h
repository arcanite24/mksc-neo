#ifndef PORTABLE_MESH_ITEMS_H
#define PORTABLE_MESH_ITEMS_H
#include "mesh_item_boxes.h"
#include "mesh_item_status.h"
#include "mesh_item_target.h"
#include "mesh_racer.h"

enum
{
    PORTABLE_MESH_ITEMS_ACTORS = 64,
    PORTABLE_MESH_ITEMS_EVENTS = 1024
};
typedef enum
{
    PORTABLE_MESH_ACTOR_FREE,
    PORTABLE_MESH_ACTOR_HELD,
    PORTABLE_MESH_ACTOR_ORBIT_PENDING,
    PORTABLE_MESH_ACTOR_ORBIT,
    PORTABLE_MESH_ACTOR_FLYING,
    PORTABLE_MESH_ACTOR_TRAP_ARMING,
    PORTABLE_MESH_ACTOR_TRAP_WAITING,
    PORTABLE_MESH_ACTOR_TRAP_LOCKING,
    PORTABLE_MESH_ACTOR_HAUNT,
    PORTABLE_MESH_ACTOR_BLAST /* a Bob-omb exploding in place; delay counts down */
} PortableMeshItemActorPhase;
typedef struct
{
    PortableMeshItemMotionState motion;
    PortableMeshItemTargetCursor route;
    uint32_t serial, checkpoint;
    uint16_t item, lifetime, grace, delay, angle;
    uint8_t phase, owner, target, haunted;
    uint8_t struck; /* racers a blast has already hit */
} PortableMeshItemActor;
typedef struct
{
    uint8_t item_pressed, aim_backward, aim_forward, recovering;
} PortableMeshItemControls;
typedef struct
{
    const PortableCourse3DView *course;
    const PortableMeshItemRules *rules;
    const PortableMeshKartRules *kart_rules;
    uint32_t mode;
    uint8_t count, human_mask;
    /* Mode16 only: inactive roles cannot use/collect or take ordinary damage.
     * Live bombs remain contact/Lightning candidates; bomb is a subset. */
    uint8_t inactive_mask, bomb_mask;
    const PortableMeshCourse *geometry; /* Optional borrowed prevalidated snapshot. */
} PortableMeshItemsConfig;
typedef struct
{
    PortableMeshItemInventory inventory;
    PortableMeshItemStatusOwner status;
    PortableMeshItemBoxes boxes;
    PortableMeshItemActor actors[PORTABLE_MESH_ITEMS_ACTORS];
    int32_t before_q16[8][3];
    PortableMeshItemControls controls[8];
    uint16_t release_lock[8], cpu_wait[8], cpu_hold[8];
    uint8_t previous_item[8], standings[8];
    uint32_t tick, serial;
    uint8_t prepared;
} PortableMeshItems;
typedef enum
{
    PORTABLE_MESH_EVENT_USE,
    PORTABLE_MESH_EVENT_PICKUP,
    PORTABLE_MESH_EVENT_RELEASE,
    PORTABLE_MESH_EVENT_HIT,
    PORTABLE_MESH_EVENT_PROTECTED,
    PORTABLE_MESH_EVENT_DESTROY,
    PORTABLE_MESH_EVENT_THEFT,
    PORTABLE_MESH_EVENT_HAUNT,
    PORTABLE_MESH_EVENT_GOLDEN_EXPIRED,
    PORTABLE_MESH_EVENT_BOOST,
    PORTABLE_MESH_EVENT_ORBIT_READY,
    PORTABLE_MESH_EVENT_LANDED,
    PORTABLE_MESH_EVENT_IMPACT,
    PORTABLE_MESH_EVENT_BOMB_CONTACT
} PortableMeshItemEventKind;
enum
{
    PORTABLE_MESH_ITEM_EVENT_BACKWARD = 1,
    PORTABLE_MESH_ITEM_EVENT_FORWARD_THROW = 2
};
/* Transient presentation events are not simulation state or replay payload.
 * serial identifies the actor generation even after its slot is freed/reused;
 * nonactor transitions use serial0 and actorUINT32_MAX. */
typedef struct
{
    uint32_t actor, placement, serial;
    uint64_t fraction_q32;
    uint16_t item;
    uint8_t kind, racer, target, flags;
} PortableMeshItemEvent;
typedef struct
{
    PortableMeshItemEvent values[PORTABLE_MESH_ITEMS_EVENTS];
    uint32_t count;
} PortableMeshItemEvents;
typedef struct
{
    int32_t position_q16[3], velocity_q16[3];
    uint32_t serial;
    uint16_t item, angle, delay;
    uint8_t owner, phase;
} PortableMeshItemPose;
typedef enum
{
    PORTABLE_MESH_ITEMS_OK,
    PORTABLE_MESH_ITEMS_INVALID,
    PORTABLE_MESH_ITEMS_BUDGET
} PortableMeshItemsResult;

/* Immutable config borrows final content. World, racers and events are staged
 * locally by each API; caller additionally stages the complete race across both
 * phases. No accepted clocks advance while paused. Reset init on Retry/course.
 * Mode2 starts human inventory1008; boxes remain preview-only, with no pickup,
 * cooldown stepping or random draws. Other modes retain authored box behavior. */
PortableMeshItemsResult portable_mesh_items_init(PortableMeshItems *,
                                                 const PortableMeshItemsConfig *, uint32_t seed,
                                                 char *error, size_t capacity);
/* Call before all kart moves. Human item is held input; rising edges initiate
 * use/orbit release. Singles drag until release; Down reverses shells, Up throws
 * bananas. Caller marks recovering for a fresh/manual RESCUE accepted this turn;
 * it excludes all uses/effects/hits/pickups before movement enters recovery. Outside mode16, CPUs decide using authored SI distance/decision/hold knobs.
 * Mode16 supplies explicit held/aim controls for every racer from its arena
 * combat owner; human_mask still selects human versus CPU roulette rules. Outputs
 * are full core dynamics, never renderer getters or source unit conversions. */
PortableMeshItemsResult portable_mesh_items_prepare_tick(
    PortableMeshItems *, const PortableMeshItemsConfig *, PortableMeshRacerState *racers,
    const PortableMeshItemControls *controls, const uint8_t *standings,
    PortableMeshKartEffects *effects, PortableMeshItemEvents *, char *error, size_t capacity);
/* Resolve each stable i<j pair before ordinary physical push. Invisibility
 * passes through; one Star damages only the other; two Stars push normally;
 * unequal shrink damages only the small racer; equal size uses physical push. */
PortableMeshItemsResult portable_mesh_items_pair(PortableMeshItems *,
                                                 const PortableMeshItemsConfig *,
                                                 PortableMeshRacerState *racers, unsigned first,
                                                 unsigned second, PortableMeshItemEvents *,
                                                 char *error, size_t capacity);
/* Optional observation of an admitted equal-size ordinary physical contact.
 * Bits name unprotected racers with hit grace<=1; Star/shrink damage branches
 * do not produce ordinary bits. No source loss timer or event is introduced.
 * Success without eligible contact writes0; failure preserves every output.
 * NULL observation is exactly the legacy pair operation. */
PortableMeshItemsResult portable_mesh_items_pair_observed(PortableMeshItems *,
    const PortableMeshItemsConfig *, PortableMeshRacerState *racers,
    unsigned first, unsigned second, PortableMeshItemEvents *, uint8_t *ordinary_mask,
    char *error, size_t capacity);
/* Call after all moves/stable kart pairs and before gates. Resolves terrain,
 * held defense, actor contacts and full XYZ swept boxes; increments item tick.
 * Appends typed events from prepare. OUTSIDE/bounce exhaustion remove actors;
 * malformed content or collision-budget exhaustion rejects the whole stage. */
PortableMeshItemsResult portable_mesh_items_resolve_after_move(PortableMeshItems *,
                                                               const PortableMeshItemsConfig *,
                                                               PortableMeshRacerState *racers,
                                                               PortableMeshItemEvents *,
                                                               char *error, size_t capacity);
/* After resolved movement/balloon elimination, retire newly inactive owners'
 * inventory, transient boost/spin and attachments in stable actor-slot order.
 * Requires prepared0. Appends to accepted events; never advances clocks/RNG
 * or changes controls. Released actors and independent status clocks survive.
 * Failure preserves both world and events. Repeating the call is idempotent. */
PortableMeshItemsResult portable_mesh_items_clear_inactive(PortableMeshItems *,
    const PortableMeshItemsConfig *, PortableMeshItemEvents *, char *error, size_t capacity);
PortableMeshItemsResult portable_mesh_items_hud(const PortableMeshItems *, unsigned racer,
                                                uint16_t *out);
size_t portable_mesh_items_actor_poses(const PortableMeshItems *, PortableMeshItemPose *out,
                                       size_t capacity);
/* Boo has no fabricated coin subsystem. THEFT/HAUNT events and lock timers are
 * provided; original coin drain is outside this owner until coins are authored. */
#endif
