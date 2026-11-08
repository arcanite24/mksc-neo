#include "driver_tick.h"

static int16_t fixed_checkpoint_to_cell(uint16_t raw)
{
    const int32_t value = (int16_t)raw;

    /* Match the original ARM arithmetic shift for signed 16-bit values. */
    if (value >= 0)
        return (int16_t)(value / 8192);
    return (int16_t)-(((-value) + 8191) / 8192);
}

PortableDriverTickResult portable_driver_tick(
    const PortableTrackGeometryView *geometry,
    PortableDriverTickState *state,
    uint16_t target_heading)
{
    PortableTrackCheckpoint checkpoint;
    uint16_t selected_checkpoint;
    uint16_t current;
    uint16_t delta;
    int changed = 0;

    if (geometry == NULL || state == NULL)
        return PORTABLE_DRIVER_TICK_INVALID_ARGUMENT;
    if (geometry->nr_checkpoints == 0)
        return PORTABLE_DRIVER_TICK_GEOMETRY_ERROR;

    selected_checkpoint = state->checkpoint_index;
    if (selected_checkpoint == (uint16_t)(geometry->nr_checkpoints - 1u))
        --selected_checkpoint;
    if (portable_track_geometry_read_checkpoint(geometry,
                                                selected_checkpoint,
                                                0,
                                                &checkpoint) !=
        PORTABLE_TRACK_GEOMETRY_OK)
        return PORTABLE_DRIVER_TICK_GEOMETRY_ERROR;

    state->target_x = fixed_checkpoint_to_cell(checkpoint.x);
    state->target_y = fixed_checkpoint_to_cell(checkpoint.y);
    state->target_heading = target_heading;

    current = state->heading;
    delta = (uint16_t)(current - target_heading);
    if (delta != 0)
    {
        /* This is sub_8039894's unsigned wrap and two-sided thresholds. */
        if ((delta & UINT16_C(0x8000)) == 0)
        {
            if (delta <= UINT16_C(0x01ff))
            {
                current = (uint16_t)(current - UINT16_C(0x0140));
                changed = 1;
            }
        }
        else if (delta <= UINT16_C(0xfdff))
        {
            current = (uint16_t)(current + UINT16_C(0x0140));
            changed = 1;
        }
    }
    if (changed)
        state->heading = current;
    state->heading_copy = state->heading;
    state->heading_target_copy = state->heading;
    return changed ? PORTABLE_DRIVER_TICK_CHANGED
                   : PORTABLE_DRIVER_TICK_STABLE;
}
