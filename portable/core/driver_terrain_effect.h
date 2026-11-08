#ifndef MKSC_PORTABLE_DRIVER_TERRAIN_EFFECT_H
#define MKSC_PORTABLE_DRIVER_TERRAIN_EFFECT_H

#include <stdint.h>

#include "terrain_effect_tables.h"
#include "driver_material.h"

enum
{
    PORTABLE_DRIVER_TERRAIN_EFFECT_OK = 0,
    PORTABLE_DRIVER_TERRAIN_EFFECT_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_TERRAIN_EFFECT_HEIGHT_LIMIT = 2,
    PORTABLE_DRIVER_TERRAIN_EFFECT_OUT_OF_BOUNDS = 3,
    PORTABLE_DRIVER_TERRAIN_EFFECT_QUERY_ERROR = 4,
};

typedef struct
{
    uint16_t previous_cell_key;
    uint16_t cell_key;
    int32_t cell_delta;
    int32_t effect_dispatch_delta;
    uint8_t cell_key_valid;
    uint8_t cell_changed;
    uint8_t effect_active;
    uint8_t effect_dispatch_candidate;
    uint8_t height_gated;
    uint8_t bounds_gated;
} PortableDriverTerrainEffectState;

typedef struct
{
    int32_t mode;
    uint8_t x_neighbor_flags;
    uint8_t y_neighbor_flags;
    uint8_t orientation;
    uint8_t vector_mutated;
} PortableDriverTerrainEffectDispatch;

/*
 * Reproduce the stateful pre-dispatch portion of sub_80449EC. The source
 * stores the terrain cell key at state+0x52 and derives a directional argument
 * before calling sub_8044920. That callee still contains unresolved game-
 * visible side effects, so this native seam records its inputs without
 * mutating motion, damage, checkpoints or lap state.
 */
int portable_driver_terrain_effect_tick(
    const PortableDriverMaterialState *material,
    int32_t height_q16,
    PortableDriverTerrainEffectState *state);

/*
 * Reproduce sub_8044920/sub_8047808 for a caller-owned horizontal actor
 * vector. The vector is deliberately separate from kart motion: the source
 * fields are the generic actor's state+0x28/+0x2c, and this card has not yet
 * established that they are the native pilot's component fields.
 */
int portable_driver_terrain_effect_dispatch(
    const PortableDriverMaterialState *material,
    const PortableTrackMaterialView *track_material,
    const PortableTerrainView *terrain,
    const PortableTerrainEffectTableView *tables,
    int32_t mode,
    int32_t *vector_x_q16,
    int32_t *vector_y_q16,
    PortableDriverTerrainEffectDispatch *out);

#endif
