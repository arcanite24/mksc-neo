#ifndef MKSC_PORTABLE_TRACK_RUNTIME_MAP_H
#define MKSC_PORTABLE_TRACK_RUNTIME_MAP_H

#include <stddef.h>
#include <stdint.h>

#include "track_material.h"
#include "track_object_asset.h"
#include "track_tileset_asset.h"

enum
{
    PORTABLE_TRACK_RUNTIME_DYNAMIC_FIRST = 0xd0u,
    PORTABLE_TRACK_RUNTIME_DYNAMIC_LIMIT = 0xe0u,
    PORTABLE_TRACK_RUNTIME_MAX_MUTATIONS = 256u,
};

typedef enum
{
    PORTABLE_TRACK_RUNTIME_MAP_OK = 0,
    PORTABLE_TRACK_RUNTIME_MAP_INVALID_ARGUMENT,
    PORTABLE_TRACK_RUNTIME_MAP_CAPACITY,
    PORTABLE_TRACK_RUNTIME_MAP_OUT_OF_BOUNDS,
    PORTABLE_TRACK_RUNTIME_MAP_TOO_MANY_DYNAMIC_TILES,
} PortableTrackRuntimeMapStatus;

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint8_t source_tile;
    uint8_t dynamic_tile;
} PortableTrackRuntimeMapMutation;

typedef struct
{
    PortableTrackRuntimeMapMutation
        mutations[PORTABLE_TRACK_RUNTIME_MAX_MUTATIONS];
    size_t mutation_count;
    size_t dynamic_tile_count;
} PortableTrackRuntimeMapReport;

/*
 * Reproduce sub_8053220 for the caller-selected source object stream.
 * `map`, `tiles`, and `runtime_material_flags` are caller-owned runtime
 * buffers. Dynamic art aliases keep the imported material table's reserved
 * 0xd0..0xdf object-contact flags; only the tileset and map are rewritten.
 * The imported course blob and its material table remain immutable.
 */
PortableTrackRuntimeMapStatus portable_track_runtime_map_apply_object_stream(
    const PortableTrackObjectStream *objects,
    uint8_t *map,
    size_t map_size,
    size_t width,
    size_t height,
    uint8_t *tiles,
    size_t tile_bytes,
    const PortableTrackMaterialView *source_material,
    uint8_t *runtime_material_flags,
    size_t runtime_material_capacity,
    PortableTrackRuntimeMapReport *out_report);

#endif
