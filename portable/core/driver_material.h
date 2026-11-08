#ifndef MKSC_PORTABLE_DRIVER_MATERIAL_H
#define MKSC_PORTABLE_DRIVER_MATERIAL_H

#include <stdint.h>

#include "track_material.h"
#include "track_raster.h"
#include "terrain_query.h"

enum
{
    PORTABLE_DRIVER_MATERIAL_OK = 0,
    PORTABLE_DRIVER_MATERIAL_INVALID_ARGUMENT = 1,
    PORTABLE_DRIVER_MATERIAL_OUT_OF_BOUNDS = 2,
    PORTABLE_DRIVER_MATERIAL_TERRAIN_QUERY_ERROR = 3,
    PORTABLE_DRIVER_MATERIAL_TRACK_QUERY_ERROR = 4,
};

typedef struct
{
    int32_t position_x_q16;
    int32_t position_y_q16;
    int32_t world_x;
    int32_t world_y;
    uint8_t in_bounds;
    uint8_t terrain_index;
    uint8_t terrain_index_valid;
    uint8_t track_cell;
    uint8_t track_cell_valid;
    uint8_t off_track;
    uint8_t material_flags;
    uint8_t material_flags_valid;
    uint8_t material_effect_candidate;
    int terrain_query_status;
    int track_query_status;
} PortableDriverMaterialState;

/*
 * Reproduce the source's Q16.16 boundary/material seam without mutating the
 * pilot. The material predicate is the source sub_08044560 condition; it is
 * reported as a candidate only and does not apply physics or damage.
 */
int portable_driver_material_sample(
    const PortableTrackMaterialView *material,
    const PortableTrackRaster *raster,
    const PortableTerrainView *terrain,
    int32_t position_x_q16,
    int32_t position_y_q16,
    PortableDriverMaterialState *out);

/* B110/32798: replace one destructible wall tile in caller-owned terrain.
 * The sampled material remains the contact owner for this turn. */
int portable_driver_wall_material_contact(
    const PortableDriverMaterialState *sample, const PortableTerrainView *terrain,
    uint8_t *owned_cells, size_t capacity, uint8_t replacement_tile,
    uint16_t invulnerability_timer, uint8_t *reflect, size_t *changed_cell);

#endif
