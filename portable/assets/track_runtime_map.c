#include "track_runtime_map.h"

#include <string.h>

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
    PortableTrackRuntimeMapReport *out_report)
{
    PortableTrackRuntimeMapReport report = {0};
    uint8_t source_to_dynamic[256] = {0};
    uint8_t source_seen[256] = {0};
    uint8_t dynamic_copied[PORTABLE_TRACK_RUNTIME_DYNAMIC_LIMIT -
                           PORTABLE_TRACK_RUNTIME_DYNAMIC_FIRST] = {0};
    uint32_t index;

    if (objects == NULL || map == NULL || tiles == NULL ||
        source_material == NULL || source_material->flags == NULL ||
        runtime_material_flags == NULL || out_report == NULL || width == 0u ||
        height == 0u || width > SIZE_MAX / height || map_size < width * height)
        return PORTABLE_TRACK_RUNTIME_MAP_INVALID_ARGUMENT;
    if (objects->record_count > PORTABLE_TRACK_RUNTIME_MAX_MUTATIONS ||
        source_material->count > runtime_material_capacity)
        return PORTABLE_TRACK_RUNTIME_MAP_CAPACITY;
    if (tile_bytes < PORTABLE_TRACK_RUNTIME_DYNAMIC_LIMIT * 64u ||
        source_material->count < PORTABLE_TRACK_RUNTIME_DYNAMIC_LIMIT)
        return PORTABLE_TRACK_RUNTIME_MAP_CAPACITY;

    /* Validate and assign source tile aliases before changing any buffer. */
    for (index = 0; index < objects->record_count; ++index)
    {
        PortableTrackRawObject object;
        size_t cell;
        uint8_t source;
        uint8_t dynamic;

        if (portable_track_raw_object_read(objects, index, &object) !=
            PORTABLE_TRACK_OBJECT_ASSET_OK)
            return PORTABLE_TRACK_RUNTIME_MAP_OUT_OF_BOUNDS;
        if ((size_t)object.x >= width || (size_t)object.y >= height)
            return PORTABLE_TRACK_RUNTIME_MAP_OUT_OF_BOUNDS;
        cell = (size_t)object.y * width + (size_t)object.x;
        source = map[cell];
        if ((size_t)source * 64u > tile_bytes - 64u)
            return PORTABLE_TRACK_RUNTIME_MAP_OUT_OF_BOUNDS;
        if (!source_seen[source])
        {
            if (report.dynamic_tile_count >=
                PORTABLE_TRACK_RUNTIME_DYNAMIC_LIMIT -
                    PORTABLE_TRACK_RUNTIME_DYNAMIC_FIRST)
                return PORTABLE_TRACK_RUNTIME_MAP_TOO_MANY_DYNAMIC_TILES;
            source_seen[source] = 1u;
            source_to_dynamic[source] =
                (uint8_t)(PORTABLE_TRACK_RUNTIME_DYNAMIC_FIRST +
                          report.dynamic_tile_count);
            ++report.dynamic_tile_count;
        }
        dynamic = source_to_dynamic[source];
        report.mutations[report.mutation_count++] =
            (PortableTrackRuntimeMapMutation){object.x, object.y, source,
                                              dynamic};
    }

    memcpy(runtime_material_flags, source_material->flags,
           source_material->count);
    for (index = 0; index < report.mutation_count; ++index)
    {
        const PortableTrackRuntimeMapMutation *mutation =
            &report.mutations[index];
        size_t source_offset = (size_t)mutation->source_tile * 64u;
        size_t dynamic_offset = (size_t)mutation->dynamic_tile * 64u;

        map[(size_t)mutation->y * width + mutation->x] =
            mutation->dynamic_tile;
        if (!dynamic_copied[mutation->dynamic_tile -
                            PORTABLE_TRACK_RUNTIME_DYNAMIC_FIRST])
        {
            memcpy(tiles + dynamic_offset, tiles + source_offset, 64u);
            dynamic_copied[mutation->dynamic_tile -
                           PORTABLE_TRACK_RUNTIME_DYNAMIC_FIRST] = 1u;
        }
    }
    *out_report = report;
    return PORTABLE_TRACK_RUNTIME_MAP_OK;
}
