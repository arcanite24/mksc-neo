#ifndef PORTABLE_MESH_ITEM_BOXES_H
#define PORTABLE_MESH_ITEM_BOXES_H
#include "course_3d.h"
#include "mesh_item_inventory.h"
#include <stddef.h>
#include <stdint.h>

enum
{
    PORTABLE_MESH_ITEM_BOX_CAPACITY = 4096
};
typedef struct
{
    uint16_t cooldowns[PORTABLE_MESH_ITEM_BOX_CAPACITY];
    uint32_t count;
} PortableMeshItemBoxes;
typedef struct
{
    int32_t before_q16[3], after_q16[3], collision_radius_q16;
    uint8_t eligible;
} PortableMeshItemBoxSample;
typedef struct
{
    uint32_t placement;
    uint64_t fraction_q32; /* Inclusive ceil(entry * 2^32), including endpoint 2^32. */
    uint8_t racer;
} PortableMeshItemBoxPickup;
typedef struct
{
    PortableMeshItemBoxPickup pickups[PORTABLE_MESH_ITEM_RACERS];
    uint8_t count;
} PortableMeshItemBoxEvents;

/* ITEM data and rules are immutable borrows for the call. Positions use the
 * kart +/-4096m domain; radii are positive and at most 4096m. All placements are
 * validated, including hidden/noncontacting boxes. No allocation or RNG at init. */
PortableMeshItemStatus portable_mesh_item_boxes_init(PortableMeshItemBoxes *out,
                                                     const PortableCourse3DView *course,
                                                     char *error, size_t capacity);
/* Call once per accepted world tick, on the caller's staged inventory. Existing
 * cooldowns decrement first; a newly collected box retains its full authored
 * cooldown until the following tick. Each racer may collect at most one box.
 * Global pickup order is entry fraction, racer index, then placement index.
 * Only admitted pickups consume one shared source RNG draw for roulette seed.
 * Failure preserves boxes, inventory and events. Outputs must not alias inputs
 * or one another; the caller may discard the entire stage on later world failure. */
PortableMeshItemStatus
portable_mesh_item_boxes_tick(PortableMeshItemBoxes *boxes, PortableMeshItemInventory *inventory,
                              const PortableCourse3DView *course,
                              const PortableMeshItemRules *rules,
                              const PortableMeshItemBoxSample *samples, size_t count,
                              PortableMeshItemBoxEvents *events, char *error, size_t capacity);
#endif
