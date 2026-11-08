#ifndef PORTABLE_MESH_ITEM_INVENTORY_H
#define PORTABLE_MESH_ITEM_INVENTORY_H

#include "mesh_item_rules.h"
#include <stddef.h>

enum { PORTABLE_MESH_ITEM_RACERS = 8 };
typedef struct
{
    uint16_t held_item, preview_item, settled_item;
    int32_t roulette_index, delay, timeline_index;
    uint32_t cpu_choice_delay;
    uint8_t previous_item_pressed, active;
} PortableMeshItemRacer;
typedef struct
{
    uint32_t seed;
    PortableMeshItemRacer racers[PORTABLE_MESH_ITEM_RACERS];
    uint8_t count, human_mask;
} PortableMeshItemInventory;
typedef struct
{
    int32_t strength_threshold;
    uint8_t rank, item_pressed, eligible, cpu_use;
} PortableMeshItemInput;
typedef struct
{
    uint16_t item, successor;
    uint8_t racer, inventory_action, inert;
} PortableMeshItemUse;
typedef struct
{
    PortableMeshItemInventory before, next;
    PortableMeshItemUse uses[PORTABLE_MESH_ITEM_RACERS];
    uint8_t use_count;
} PortableMeshItemStep;
typedef enum
{
    PORTABLE_MESH_ITEM_OK = 0,
    PORTABLE_MESH_ITEM_INVALID,
    PORTABLE_MESH_ITEM_INELIGIBLE,
    PORTABLE_MESH_ITEM_STALE
} PortableMeshItemStatus;

/* Owners and prepared steps contain no pointers. Rules are borrowed only for
 * the call. Failure preserves every output byte, including staged outputs. */
PortableMeshItemStatus portable_mesh_item_inventory_init(PortableMeshItemInventory *out,
    unsigned count, unsigned human_mask, uint32_t seed);
PortableMeshItemStatus portable_mesh_item_inventory_collect(const PortableMeshItemInventory *owner,
    const PortableMeshItemRules *rules, unsigned racer, int eligible, uint16_t roulette_seed,
    PortableMeshItemStep *out);
/* Inputs are in racer order; rank is 1..8. cpu_use is a world/AI decision, not
 * a source route-gap surrogate. eligible gates use requests; active roulette
 * callbacks still advance. CPUs settle on callback31 after fixed30 wait.
 * Human use/stop requires a fresh item edge. A settlement never also uses it. */
PortableMeshItemStatus portable_mesh_item_inventory_tick(const PortableMeshItemInventory *owner,
    const PortableMeshItemRules *rules, const PortableMeshItemInput *inputs, size_t count,
    PortableMeshItemStep *out);
PortableMeshItemStatus portable_mesh_item_inventory_hud(const PortableMeshItemInventory *owner,
    unsigned racer, uint16_t *out);
/* Bit i accepts uses[i]. Refusal retains its held item; discard the entire
 * step on world failure to roll back awards, RNG, timers and input edges too.
 * World handlers must finish successfully before publishing this step. */
PortableMeshItemStatus portable_mesh_item_inventory_commit(PortableMeshItemInventory *owner,
    const PortableMeshItemStep *step, unsigned accepted_uses);

#endif
