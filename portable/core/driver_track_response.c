#include "driver_track_response.h"

int portable_driver_track_response_tick(
    const PortableDriverMaterialState *material,
    PortableDriverTrackResponseState *state)
{
    uint8_t surface_cell;
    uint8_t changed;
    uint32_t source_flags = 0;

    if (material == NULL || state == NULL)
        return PORTABLE_DRIVER_TRACK_RESPONSE_INVALID_ARGUMENT;

    surface_cell = (uint8_t)(material->track_cell & 0x7f);
    changed = (uint8_t)(material->track_cell_valid &&
                        (!state->surface_cell_valid ||
                         state->surface_cell != surface_cell));

    /* sub_803B98C starts by clearing state+0x48 bits 0 and 1.  These are
     * source-owned per-frame response bits; retaining them would make a
     * boundary/off-track observation leak into later road cells. */
    if (!material->in_bounds)
        source_flags |= PORTABLE_DRIVER_TRACK_RESPONSE_BOUNDARY;
    else if (material->off_track)
        source_flags |= PORTABLE_DRIVER_TRACK_RESPONSE_OFF_TRACK;
    state->source_flags = source_flags;
    state->flags = source_flags;
    state->surface_cell_changed = 0;
    if (material->track_cell_valid)
    {
        state->surface_cell = surface_cell;
        state->surface_cell_valid = 1;
        state->surface_cell_changed = changed;
        if (changed)
            state->flags |= PORTABLE_DRIVER_TRACK_RESPONSE_TRANSITION;
    }
    return PORTABLE_DRIVER_TRACK_RESPONSE_OK;
}
