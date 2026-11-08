#include "driver_track_state.h"

PortableDriverTrackStateStatus portable_driver_track_state_sample(
    const PortableTrackGeometryView *geometry,
    const PortableTrackRaster *raster,
    const PortableTerrainView *terrain,
    uint32_t checkpoint_index,
    uint8_t checkpoint_row,
    int32_t world_x,
    int32_t world_y,
    PortableDriverTrackState *out)
{
    uint32_t selected_checkpoint;
    PortableTrackGeometryStatus geometry_status;

    if (geometry == NULL || raster == NULL || terrain == NULL || out == NULL)
        return PORTABLE_DRIVER_TRACK_STATE_INVALID_ARGUMENT;

    *out = (PortableDriverTrackState){
        .requested_checkpoint = checkpoint_index,
        .selected_checkpoint = checkpoint_index,
        .checkpoint_row = checkpoint_row,
        .world_x = world_x,
        .world_y = world_y,
    };

    if (geometry->nr_checkpoints == 0)
        return PORTABLE_DRIVER_TRACK_STATE_GEOMETRY_ERROR;

    /* Match sub_8039828's previous-target selection, with a one-row guard. */
    selected_checkpoint = checkpoint_index;
    if (geometry->nr_checkpoints > 1 &&
        selected_checkpoint == (uint32_t)geometry->nr_checkpoints - 1u)
        --selected_checkpoint;
    out->selected_checkpoint = selected_checkpoint;
    geometry_status = portable_track_geometry_read_checkpoint(
        geometry, selected_checkpoint, checkpoint_row, &out->checkpoint);
    if (geometry_status != PORTABLE_TRACK_GEOMETRY_OK)
        return PORTABLE_DRIVER_TRACK_STATE_GEOMETRY_ERROR;

    out->track_query_status = portable_track_raster_query16(
        raster, world_x, world_y, &out->track_cell);
    if (out->track_query_status == PORTABLE_TRACK_RASTER_OK)
        out->track_cell_valid = 1;

    out->terrain_query_status = portable_terrain_query8(
        terrain, world_x, world_y, &out->terrain_cell);
    if (out->terrain_query_status == PORTABLE_TERRAIN_QUERY_OK)
        out->terrain_cell_valid = 1;

    if (!out->track_cell_valid)
        return PORTABLE_DRIVER_TRACK_STATE_TRACK_QUERY_ERROR;
    if (!out->terrain_cell_valid)
        return PORTABLE_DRIVER_TRACK_STATE_TERRAIN_QUERY_ERROR;
    return PORTABLE_DRIVER_TRACK_STATE_OK;
}
